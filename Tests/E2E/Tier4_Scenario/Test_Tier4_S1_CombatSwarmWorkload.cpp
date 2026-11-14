/**
 * @file Test_Tier4_S1_CombatSwarmWorkload.cpp
 * @brief Tier 4 真实负载综合场景 S1：千级怪潮同屏战斗、GPUScene 视锥剔除、32B 跳字合批与 Clustered Forward+ 稳态压测
 */
#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/Context/TransientResourcePool.h>

#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <Font/Composites/FontRuntimeComposite/BakedFont.h>
#include <Font/Composites/FontRuntimeComposite/InGameTextComponent.h>
#include <MQEngine/host/work/WorkHost.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using namespace MQEngine::Domain::Renderer;
        using namespace MQEngine::Resource::Font;

        FCT::Mat4 buildIdentityMat4() noexcept
        {
            FCT::Mat4 mat{};
            mat.m[0] = 1.0f;
            mat.m[5] = 1.0f;
            mat.m[10] = 1.0f;
            mat.m[15] = 1.0f;
            return mat;
        }

        FCT::Mat4 buildOrthoCombatViewProj(float halfExtent) noexcept
        {
            const float inv = (halfExtent > 1e-4f) ? (1.0f / halfExtent) : 0.01f;
            FCT::Mat4 mat{};
            mat.m[0] = inv;
            mat.m[5] = inv;
            mat.m[10] = inv;
            mat.m[15] = 1.0f;
            return mat;
        }

        std::vector<GpuInstanceRow> populateSwarmInstances(uint32_t totalCount, uint32_t visibleCount)
        {
            std::vector<GpuInstanceRow> rows(totalCount);
            const FCT::Mat4 identity = buildIdentityMat4();
            for (uint32_t i = 0; i < totalCount; ++i)
            {
                rows[i].modelMatrix = identity;
                rows[i].invTransposeMatrix = identity;
                rows[i].meshSlot = i % 8u;
                rows[i].materialSlot = i % 16u;
                rows[i].entityId = i + 1u;
                rows[i].flags = 1u;

                if (i < visibleCount)
                {
                    const float gridX = static_cast<float>(static_cast<int32_t>(i % 25u) - 12) * 3.0f;
                    const float gridZ = static_cast<float>(static_cast<int32_t>((i / 25u) % 40u) - 20) * 2.0f;
                    rows[i].worldSphereBounds = FCT::Vec4{gridX, 0.0f, gridZ, 1.5f};
                }
                else
                {
                    rows[i].worldSphereBounds = FCT::Vec4{500.0f, 500.0f, 500.0f, 1.0f};
                }
            }
            return rows;
        }

        std::vector<ClusterLightRecord> buildCombatLights(uint32_t lightCount)
        {
            std::vector<ClusterLightRecord> lights(lightCount);
            for (uint32_t i = 0; i < lightCount; ++i)
            {
                ClusterLightRecord& lt = lights[i];
                lt.position[0] = static_cast<float>(static_cast<int32_t>(i % 8u) - 4) * 3.5f;
                lt.position[1] = static_cast<float>(static_cast<int32_t>((i / 8u) % 4u) - 2) * 2.5f;
                lt.position[2] = -6.0f - static_cast<float>(i) * 2.2f;
                lt.radius = 12.0f;
                lt.color[0] = 1.0f;
                lt.color[1] = 0.85f;
                lt.color[2] = 0.6f;
                lt.intensity = 3.5f;
                lt.lightType = (i % 4u == 0u) ? 1u : 0u;
            }
            return lights;
        }

        struct InlineCounterTask
        {
            std::atomic<uint32_t>* counter{nullptr};

            void operator()() const noexcept
            {
                if (counter)
                {
                    counter->fetch_add(1u, std::memory_order_relaxed);
                }
            }
        };

        inline void settleSwarmDamageSimd(float* hpData, size_t count, float damagePerTick) noexcept
        {
#if defined(MQENGINE_SIMD_SSE)
            const __m128 dmgVec = _mm_set1_ps(damagePerTick);
            const __m128 zeroVec = _mm_setzero_ps();
            size_t i = 0;
            for (; i + 4u <= count; i += 4u)
            {
                const __m128 cur = _mm_loadu_ps(hpData + i);
                const __m128 next = _mm_max_ps(zeroVec, _mm_sub_ps(cur, dmgVec));
                _mm_storeu_ps(hpData + i, next);
            }
            for (; i < count; ++i)
            {
                hpData[i] = std::max(0.0f, hpData[i] - damagePerTick);
            }
#else
            for (size_t i = 0; i < count; ++i)
            {
                hpData[i] = std::max(0.0f, hpData[i] - damagePerTick);
            }
#endif
        }
    } // namespace

    // S1-01: 1000 怪物实例 GPUScene 2D 数据纹理上传、脏行跨度计算与 SIMD128 视锥剔除槽位流提取
    E2E_TEST(Tier4, FEAT_13, S1_01_Swarm1000InstancesGpuSceneUploadAndFrustumStream)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));

        std::vector<GpuMaterialRecord> materials(16);
        for (uint32_t m = 0; m < 16u; ++m)
        {
            materials[m].albedoTexIndex = m % 4u;
            materials[m].pipelineBucket = static_cast<uint16_t>(m % 2u);
        }
        E2E_ASSERT_STATUS_OK(gpuScene.uploadMaterialData(materials, 0u));

        const auto instances = populateSwarmInstances(1000u, 720u);
        E2E_ASSERT_STATUS_OK(gpuScene.uploadInstanceData(instances, 0u));

        auto dirtySpanOr = GPUScene::computeDirtyTextureRows(0u, 999u, 10u);
        E2E_ASSERT_STATUS_OK(dirtySpanOr);
        E2E_ASSERT_EQ(dirtySpanOr->startRow, 0u);
        E2E_ASSERT_EQ(dirtySpanOr->rowCount, 10u);

        const FCT::Mat4 viewProj = buildOrthoCombatViewProj(60.0f);
        const std::vector<uint32_t> visibleSlots = gpuScene.extractVisibleInstanceSlotStream(instances, viewProj);
        E2E_ASSERT_EQ(visibleSlots.size(), 720u);
        E2E_ASSERT_EQ(visibleSlots.front(), 0u);
        E2E_ASSERT_EQ(visibleSlots.back(), 719u);

        gpuScene.shutdown();
        return true;
    }

    // S1-02: 1000 条 32B 紧凑伤害跳字 MSDF 预烘焙合批（零 FreeType 触发，<= 4 DrawCalls）
    E2E_TEST(Tier4, FEAT_57, S1_02_Swarm1000DamageNumbersCompact32BInstancedBatching)
    {
        BakedFont combatFont;
        combatFont.initDefaultCombatPreset(512u, 512u);
        E2E_ASSERT_EQ(combatFont.getFreeTypeInvocationCount(), 0u);

        InGameTextComponent textComp;
        for (uint32_t i = 0; i < 250u; ++i)
        {
            const bool isCrit = (i % 5u == 0u);
            textComp.spawnDamageNumber(
                isCrit ? "-9999" : "-128",
                FCT::Vec3<>{static_cast<float>(i % 20u), 1.5f, static_cast<float>(i / 20u)},
                isCrit ? 1.5f : 1.0f,
                isCrit ? 0xFF3322FFu : 0xFFFFFFFFu,
                isCrit);
        }
        E2E_ASSERT_EQ(textComp.getActiveItems().size(), 250u);

        std::vector<GpuInGameTextQuadInstance> quadStream;
        textComp.buildQuadInstances(combatFont, quadStream);
        E2E_ASSERT_GT(quadStream.size(), 1000u);
        E2E_ASSERT_EQ(sizeof(GpuInGameTextQuadInstance), 32u);

        const float initialY = quadStream.front().worldPosAndScale[1];
        InGameTextComponent::evaluateBatchAnimations(quadStream, 0.016f);
        E2E_ASSERT_GT(quadStream.front().worldPosAndScale[1], initialY);

        const uint32_t drawCalls = InGameTextComponent::calculateDrawCallCount(quadStream.size(), 1024u);
        E2E_ASSERT_GE(drawCalls, 1u);
        E2E_ASSERT_LE(drawCalls, 2u);
        E2E_ASSERT_EQ(combatFont.getFreeTypeInvocationCount(), 0u);
        return true;
    }

    // S1-03: 32 盏动态战斗点光源 2048 Froxels SIMD128 分簇与 <=4 盏单簇自适应退化/恢复闭环
    E2E_TEST(Tier4, FEAT_09, S1_03_ClusteredForwardPlus32LightsAndAdaptiveDegradationLoop)
    {
        ClusteredForwardPlus clusterer;
        FCT::Mat4 proj = buildIdentityMat4();
        clusterer.updateFrustumGrid(proj, 0.1f, 120.0f, 1280u, 720u);
        E2E_ASSERT_EQ(clusterer.getGridConfig().totalFroxels(), 2048u);

        const FCT::Mat4 view = buildIdentityMat4();
        const auto heavyLights = buildCombatLights(32u);
        clusterer.cullLights(heavyLights, view);
        E2E_ASSERT_FALSE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 32u);
        E2E_ASSERT_FALSE(clusterer.getLightIndexListBuffer().empty());

        const auto bossEndingLights = buildCombatLights(3u);
        clusterer.cullLights(bossEndingLights, view);
        E2E_ASSERT_TRUE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 3u);

        clusterer.cullLights(heavyLights, view);
        E2E_ASSERT_FALSE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 32u);
        return true;
    }

    // S1-04: MiniGame_ForwardPlus 帧图预设加载、TBDR 深度丢弃安全审计与同 Pass 确定性排序
    E2E_TEST(Tier4, FEAT_70, S1_04_MiniGameForwardPlusPresetAndDeterministicBucketSort)
    {
        const RenderGraphAssetDesc forwardPlusGraph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        E2E_ASSERT_TRUE(forwardPlusGraph.isAcyclic());
        E2E_ASSERT_STATUS_OK(forwardPlusGraph.validateDepthSamplingHazard());

        RenderGraphMatcher matcher;
        PipelineBindingDesc opaquePipe{};
        opaquePipe.pipelineId = 101u;
        opaquePipe.ownerTechId = 1u;
        opaquePipe.name = "CombatToonOpaque";
        opaquePipe.requiredPassTags = PassTags::Opaque | PassTags::ForwardPlus;
        opaquePipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
        opaquePipe.order = 10;

        PipelineBindingDesc textPipe{};
        textPipe.pipelineId = 202u;
        textPipe.ownerTechId = 2u;
        textPipe.name = "InGameDamageMsdf";
        textPipe.requiredPassTags = PassTags::Opaque | PassTags::ForwardPlus;
        textPipe.requiredInputTags = InputTags::InGameBaked | InputTags::Text;
        textPipe.order = 50;

        PipelineBindingDesc deferredOnlyPipe{};
        deferredOnlyPipe.pipelineId = 999u;
        deferredOnlyPipe.ownerTechId = 3u;
        deferredOnlyPipe.name = "DesktopDeferredGBuffer";
        deferredOnlyPipe.requiredPassTags = PassTags::DeferredGBuffer;
        deferredOnlyPipe.requiredInputTags = InputTags::Mesh;

        matcher.registerPipeline(textPipe);
        matcher.registerPipeline(opaquePipe);
        matcher.registerPipeline(deferredOnlyPipe);

        const size_t strippedCount = matcher.stripUnusedPipelinesForGraph(forwardPlusGraph);
        E2E_ASSERT_GE(strippedCount, 1u);

        std::vector<DrawBucket> buckets;
        buckets.emplace_back(2u, InputTags::InGameBaked | InputTags::Text, 250u);
        buckets.emplace_back(1u, InputTags::Mesh | InputTags::Opaque, 720u);

        const auto execItems = matcher.evaluatePassExecutionItems(
            PassTags::Opaque | PassTags::ForwardPlus,
            buckets);
        E2E_ASSERT_EQ(execItems.size(), 2u);
        E2E_ASSERT_EQ(execItems[0].pipelineId, 101u);
        E2E_ASSERT_EQ(execItems[1].pipelineId, 202u);
        return true;
    }

    // S1-05: 引擎原生硬件 SIMD 批处理驱动千怪血量结算
    E2E_TEST(Tier4, FEAT_38, S1_05_NativeSimdSwarmDamageSettlementBatch)
    {
        std::vector<float> monsterHp(1024u, 500.0f);
        settleSwarmDamageSimd(monsterHp.data(), monsterHp.size(), 37.5f);

        E2E_ASSERT_NEAR(monsterHp[0], 462.5f, 1e-3f);
        E2E_ASSERT_NEAR(monsterHp[1023], 462.5f, 1e-3f);
        return true;
    }

    // S1-06: WorkHost 单线程时间片内联模式（SingleThreadInline）主循环预算排空验证
    E2E_TEST(Tier4, FEAT_43, S1_06_WorkHostSingleThreadInlineFrameBudgetDrain)
    {
        MQEngine::Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(MQEngine::Host::WorkHost::Mode::SingleThreadInline, 0u, 1u));
        E2E_ASSERT_EQ(workHost.getMode(), MQEngine::Host::WorkHost::Mode::SingleThreadInline);
        E2E_ASSERT_EQ(workHost.getWorkerCount(), 0u);

        std::atomic<uint32_t> executedTasks{0u};
        for (uint32_t i = 0; i < 64u; ++i)
        {
            E2E_ASSERT_STATUS_OK(workHost.postTask(InlineCounterTask{&executedTasks}));
        }

        for (uint32_t frame = 0; frame < 8u && executedTasks.load(std::memory_order_relaxed) < 64u; ++frame)
        {
            workHost.drainInline(4000u);
        }
        E2E_ASSERT_EQ(executedTasks.load(std::memory_order_relaxed), 64u);

        std::atomic<uint32_t> mainCallbacks{0u};
        workHost.enqueueMainThread(InlineCounterTask{&mainCallbacks});
        const size_t pumped = workHost.pumpMainThreadTasks(16u);
        E2E_ASSERT_EQ(pumped, 1u);
        E2E_ASSERT_EQ(mainCallbacks.load(std::memory_order_relaxed), 1u);

        workHost.term();
        return true;
    }

    // S1-07: WGL2_FrameRingUBO 连续多材质/多批次 256B 对齐分配与帧间零碎片复位
    E2E_TEST(Tier4, FEAT_26, S1_07_FrameRingUbo256ByteAlignedMultiDrawStaging)
    {
        FCT::WGL2_FrameRingUBO ringUbo;
        E2E_ASSERT_STATUS_OK(ringUbo.init(1024u * 1024u, 256u));

        for (uint32_t frame = 0; frame < 10u; ++frame)
        {
            ringUbo.reset();
            E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 0u);

            const auto cameraAlloc = ringUbo.allocate(sizeof(GpuSceneUniformData));
            E2E_ASSERT_NE(cameraAlloc.cpuPtr, nullptr);
            E2E_ASSERT_EQ(cameraAlloc.offset % 256u, 0u);

            GpuSceneUniformData sceneUniform{};
            sceneUniform.viewportSize = FCT::Vec4{750.0f, 1334.0f, 1.0f / 750.0f, 1.0f / 1334.0f};
            std::memcpy(cameraAlloc.cpuPtr, &sceneUniform, sizeof(sceneUniform));

            for (uint32_t batch = 0; batch < 32u; ++batch)
            {
                const auto batchAlloc = ringUbo.allocate(96u);
                E2E_ASSERT_NE(batchAlloc.cpuPtr, nullptr);
                E2E_ASSERT_EQ(batchAlloc.offset % 256u, 0u);
            }
            ringUbo.flushToGpu();
            E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 512u + 31u * 256u + 96u);
        }

        E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 512u + 31u * 256u + 96u);
        ringUbo.destroy();
        return true;
    }

    // S1-08: 60 帧连续波次刷怪/死亡消亡稳态回归（跳字生命周期自动回收 + 瞬态池复用率 >= 60%）
    E2E_TEST(Tier4, FEAT_32, S1_08_SixtyFrameWaveSpawnDespawnSteadyState)
    {
        BakedFont combatFont;
        combatFont.initDefaultCombatPreset(512u, 512u);
        InGameTextComponent textComp;

        FCT::WGL2_Context wgl2Ctx(nullptr);
        wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool transientPool(&wgl2Dev);
        FCT::TransientResourceSpec depthSpec{};
        depthSpec.width = 750u;
        depthSpec.height = 1334u;
        depthSpec.format = FCT::Format::D24_UNORM_S8_UINT;
        depthSpec.isSampled = false;
        depthSpec.isDepthStencil = true;

        FCT::TransientResourceSpec ldrColorSpec{};
        ldrColorSpec.width = 750u;
        ldrColorSpec.height = 1334u;
        ldrColorSpec.format = FCT::Format::R8G8B8A8_UNORM;
        ldrColorSpec.isSampled = true;

        std::vector<GpuInGameTextQuadInstance> frameQuads;
        for (uint32_t frame = 0; frame < 60u; ++frame)
        {
            if (frame % 6u == 0u)
            {
                for (uint32_t k = 0; k < 16u; ++k)
                {
                    textComp.spawnDamageNumber("-512", FCT::Vec3<>{0.0f, 1.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, false);
                }
            }
            textComp.tickItems(0.05f);
            textComp.buildQuadInstances(combatFont, frameQuads);

            (void)transientPool.acquire(depthSpec, 0u, 1u, frame);
            (void)transientPool.acquire(ldrColorSpec, 0u, 1u, frame);
            (void)transientPool.acquire(ldrColorSpec, 2u, 3u, frame);
            transientPool.tick(frame);
        }

        const auto stats = transientPool.getStats();
        E2E_ASSERT_GT(stats.getReuseRate(), 0.60f);
        E2E_ASSERT_LE(transientPool.getPoolSize(), 2u);

        textComp.tickItems(5.0f);
        E2E_ASSERT_TRUE(textComp.getActiveItems().empty());
        transientPool.clear();
        return true;
    }
} // namespace MQEngine::Testing
