/**
 * @file Test_Tier3_Renderer_Clustering_GPUScene.cpp
 * @brief Tier 3 正交组合测试：Clustered Forward+ 光照分簇 × 动态视口阴影图集 × GPUScene 跨材质合批 × TBDR 瞬态池与内存告警自救
 *        覆盖特性：FEAT_08, FEAT_09, FEAT_10, FEAT_11, FEAT_12, FEAT_13, FEAT_14, FEAT_15, FEAT_33, FEAT_34, FEAT_35, FEAT_36
 */
#include "Common/E2ECommon.h"

#ifndef E2E_ASSERT_NEAR
#define E2E_ASSERT_NEAR(val1, val2, absError) \
    E2E_ASSERT(std::abs((val1) - (val2)) <= (absError), \
               ::absl::StrCat("期望近似相等: |", #val1, " - ", #val2, "| <= ", #absError))
#endif

#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/Context/TransientResourcePool.h>
#include <FCT/Context/RenderGraph.h>
#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT/RHI/WGL2_PassGroup.h>
#include <FCT/RHI/WGL2_InputLayout.h>
#include <FCT/RHI/WGL2_Image.h>
#include <FCT/UI/MiniGame_Window.h>
#include <FCT/Runtime/Emscripten_Runtime.h>

#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/domain/renderer/ShadowAtlas.h>
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include "DouyinMiniGame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        void T3Clust_InitHeadlessWgl2(FCT::WGL2_Context& ctx)
        {
            ctx.createPlatform(FCT::ContextCreateFlags(0));
        }

        // 构造正交盒裁剪矩阵 [-halfExtent, +halfExtent]^3，供 Gribb-Hartmann 平面提取确定性验证
        FCT::Mat4 T3Clust_MakeOrthoBoxMatrix(float halfExtent) noexcept
        {
            FCT::Mat4 m;
            m.identity();
            const float inv = (halfExtent > 1e-4f) ? (1.0f / halfExtent) : 1.0f;
            m.m[0] = inv;
            m.m[5] = inv;
            m.m[10] = inv;
            m.m[15] = 1.0f;
            return m;
        }

        // 内存告警协调器绑定的场景非活跃图集回收函数对象（零内联 Lambda）
        struct T3Clust_GpuSceneAssetTrimFunctor
        {
            Domain::Renderer::GPUScene* gpuScene{nullptr};
            uint32_t* trimInvokeCount{nullptr};

            uint64_t operator()(int32_t level) const noexcept
            {
                if (trimInvokeCount != nullptr)
                {
                    ++(*trimInvokeCount);
                }
                if (gpuScene != nullptr && level >= DouyinMiniGame::MemoryWarningCoordinator::kLevelLow)
                {
                    if (auto* packer = gpuScene->getAtlasPacker())
                    {
                        packer->reset();
                    }
                    return 512ULL * 1024ULL;
                }
                return 128ULL * 1024ULL;
            }
        };
    } // namespace

    // 1. FEAT_09 × FEAT_38 × FEAT_25: 16x8x16=2048 Froxels 光源分簇、单簇 32 光源上限与 SIMD 变换上传至 WGL2_StorageBuffer
    E2E_TEST(Tier3, FEAT_09, Pair_ClustGpu_Froxel2048CullingWithSimdMatrixAndStorageBuffer)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        Domain::Renderer::ClusteredForwardPlus clusterer;
        FCT::Mat4 proj;
        proj.identity();
        proj.m[0] = 1.2f;
        proj.m[5] = 1.0f;
        clusterer.updateFrustumGrid(proj, 0.1f, 100.0f, 750u, 1334u);
        E2E_ASSERT_EQ(clusterer.getGridConfig().totalFroxels(), 2048u);

        // 先通过引擎原生硬件 SIMD / 矩阵变换批量平移 40 盏局部点光源坐标至相机前方 z = -6.0 密集区域，触发单簇 32 上限钳制
        constexpr size_t kLightCount = 40u;
        std::vector<FCT::Vec4> localLightCenters(kLightCount);
        std::vector<FCT::Vec4> worldLightCenters(kLightCount);
        for (size_t i = 0; i < kLightCount; ++i)
        {
            const float offset = static_cast<float>(static_cast<int32_t>(i % 5u) - 2) * 0.25f;
            localLightCenters[i] = FCT::Vec4{offset, offset * 0.5f, 0.0f, 1.0f};
        }

        FCT::Mat4 rigTransform;
        rigTransform.identity();
        rigTransform.m[11] = -6.0f; // 行主序平移 Z = -6.0
#if defined(MQENGINE_SIMD_SSE)
        __m128 col0 = _mm_loadu_ps(&rigTransform.m[0]);
        __m128 col1 = _mm_loadu_ps(&rigTransform.m[4]);
        __m128 col2 = _mm_loadu_ps(&rigTransform.m[8]);
        __m128 col3 = _mm_loadu_ps(&rigTransform.m[12]);
        _MM_TRANSPOSE4_PS(col0, col1, col2, col3);
        for (size_t i = 0; i < kLightCount; ++i)
        {
            const FCT::Vec4& src = localLightCenters[i];
            const __m128 res = _mm_add_ps(
                _mm_add_ps(_mm_mul_ps(col0, _mm_set1_ps(src.x)), _mm_mul_ps(col1, _mm_set1_ps(src.y))),
                _mm_add_ps(_mm_mul_ps(col2, _mm_set1_ps(src.z)), _mm_mul_ps(col3, _mm_set1_ps(src.w))));
            alignas(16) float out[4];
            _mm_store_ps(out, res);
            worldLightCenters[i] = FCT::Vec4{out[0], out[1], out[2], out[3]};
        }
#else
        for (size_t i = 0; i < kLightCount; ++i)
        {
            const FCT::Vec4& v = localLightCenters[i];
            worldLightCenters[i] = FCT::Vec4{
                rigTransform.m[0] * v.x + rigTransform.m[1] * v.y + rigTransform.m[2] * v.z + rigTransform.m[3] * v.w,
                rigTransform.m[4] * v.x + rigTransform.m[5] * v.y + rigTransform.m[6] * v.z + rigTransform.m[7] * v.w,
                rigTransform.m[8] * v.x + rigTransform.m[9] * v.y + rigTransform.m[10] * v.z + rigTransform.m[11] * v.w,
                rigTransform.m[12] * v.x + rigTransform.m[13] * v.y + rigTransform.m[14] * v.z + rigTransform.m[15] * v.w};
        }
#endif

        std::vector<Domain::Renderer::ClusterLightRecord> lights(kLightCount);
        for (size_t i = 0; i < kLightCount; ++i)
        {
            lights[i].position[0] = worldLightCenters[i].x;
            lights[i].position[1] = worldLightCenters[i].y;
            lights[i].position[2] = worldLightCenters[i].z;
            lights[i].radius = 6.5f;
            lights[i].intensity = 2.5f;
            lights[i].lightType = 0u;
        }

        FCT::Mat4 viewMat;
        viewMat.identity();
        clusterer.cullLights(lights, viewMat);

        E2E_ASSERT_FALSE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 40u);
        E2E_ASSERT_EQ(clusterer.getFroxelLightGridBuffer().size(), 2048u * 2u);
        E2E_ASSERT_FALSE(clusterer.getLightIndexListBuffer().empty());

        uint32_t maxObservedInFroxel = 0u;
        for (uint32_t fIdx = 0u; fIdx < 2048u; ++fIdx)
        {
            const uint32_t cnt = clusterer.getFroxelLightCount(fIdx);
            E2E_ASSERT_LE(cnt, Domain::Renderer::ClusteredForwardPlus::MaxLightsPerFroxel);
            maxObservedInFroxel = std::max(maxObservedInFroxel, cnt);
        }
        E2E_ASSERT_EQ(maxObservedInFroxel, Domain::Renderer::ClusteredForwardPlus::MaxLightsPerFroxel);

        // Log-Z 切片单调递增校验
        const uint32_t nearSlice = Domain::Renderer::ClusteredForwardPlus::computeSliceIndex(-0.15f, 0.1f, 100.0f, 16u);
        const uint32_t midSlice = Domain::Renderer::ClusteredForwardPlus::computeSliceIndex(-6.0f, 0.1f, 100.0f, 16u);
        const uint32_t farSlice = Domain::Renderer::ClusteredForwardPlus::computeSliceIndex(-95.0f, 0.1f, 100.0f, 16u);
        E2E_ASSERT_LE(nearSlice, midSlice);
        E2E_ASSERT_LT(midSlice, farSlice);
        E2E_ASSERT_EQ(farSlice, 15u);

        // 将分簇网格与光源索引表写入 2D 数据纹理模拟的 WGL2_StorageBuffer
        const auto& gridBuf = clusterer.getFroxelLightGridBuffer();
        FCT::RHI::WGL2_StorageBuffer gpuGridBuffer(&wgl2Ctx);
        E2E_ASSERT_STATUS_OK(gpuGridBuffer.create(gridBuf.size() * sizeof(uint32_t)));
        gpuGridBuffer.updateData(gridBuf.data(), gridBuf.size() * sizeof(uint32_t), 0u);

        const auto* mappedGrid = static_cast<const uint32_t*>(gpuGridBuffer.map());
        E2E_ASSERT_NE(mappedGrid, nullptr);
        E2E_ASSERT_EQ(mappedGrid[1], clusterer.getFroxelLightCount(0u));
        gpuGridBuffer.unmap();

        return true;
    }

    // 2. FEAT_09 × FEAT_67: <= 4 盏光源单簇自适应退化、非法 NaN 光源过滤与 > 4 盏满血 2048 簇恢复
    E2E_TEST(Tier3, FEAT_09, Pair_ClustGpu_SingleClusterDegradationAndRecoveryWithFroxelSphereTest)
    {
        Domain::Renderer::ClusteredForwardPlus clusterer;
        FCT::Mat4 viewMat;
        viewMat.identity();

        // 构造 3 盏合法光源 + 2 盏非法光源（半径 <= 0 与 NaN 坐标），有效数 = 3 <= 4，触发单簇退化
        std::vector<Domain::Renderer::ClusterLightRecord> sparseLights(5);
        for (size_t i = 0; i < 3u; ++i)
        {
            sparseLights[i].position[0] = static_cast<float>(i) * 1.5f;
            sparseLights[i].position[1] = 0.0f;
            sparseLights[i].position[2] = -4.0f;
            sparseLights[i].radius = 3.0f;
        }
        sparseLights[3].radius = 0.0f;
        sparseLights[4].position[0] = std::numeric_limits<float>::quiet_NaN();
        sparseLights[4].radius = 5.0f;

        clusterer.cullLights(sparseLights, viewMat);
        E2E_ASSERT_TRUE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 3u);
        E2E_ASSERT_EQ(clusterer.getGridConfig().totalFroxels(), 1u);
        E2E_ASSERT_EQ(clusterer.getFroxelLightCount(0u), 3u);

        // 动态扩容至 9 盏有效光源（含非 4 倍数尾部标量通道），自动恢复 16x8x16 = 2048 Froxels
        std::vector<Domain::Renderer::ClusterLightRecord> denseLights(9);
        for (size_t i = 0; i < denseLights.size(); ++i)
        {
            denseLights[i].position[0] = (static_cast<float>(i) - 4.0f) * 0.8f;
            denseLights[i].position[1] = 0.0f;
            denseLights[i].position[2] = -8.0f;
            denseLights[i].radius = 4.0f;
        }
        clusterer.cullLights(denseLights, viewMat);
        E2E_ASSERT_FALSE(clusterer.isDegradedToSingleCluster());
        E2E_ASSERT_EQ(clusterer.getActiveLightCount(), 9u);
        E2E_ASSERT_EQ(clusterer.getGridConfig().totalFroxels(), 2048u);

        const Domain::Renderer::FroxelAabb* sampleBox = clusterer.getFroxelAabb(120u);
        E2E_ASSERT_NE(sampleBox, nullptr);
        const float boxCenterX = 0.5f * (sampleBox->minPt[0] + sampleBox->maxPt[0]);
        const float boxCenterY = 0.5f * (sampleBox->minPt[1] + sampleBox->maxPt[1]);
        const float boxCenterZ = 0.5f * (sampleBox->minPt[2] + sampleBox->maxPt[2]);
        E2E_ASSERT_TRUE(Domain::Renderer::ClusteredForwardPlus::TestFroxelSphereIntersection(
            *sampleBox, boxCenterX, boxCenterY, boxCenterZ, 0.25f));
        E2E_ASSERT_FALSE(Domain::Renderer::ClusteredForwardPlus::TestFroxelSphereIntersection(
            *sampleBox, boxCenterX + 500.0f, boxCenterY, boxCenterZ, 1.0f));

        return true;
    }

    // 3. FEAT_10 × FEAT_73: MiniGameForwardPlus 与 MiniGameForwardLite 双档预设切换、PinSlot 解析与离线管线裁剪
    E2E_TEST(Tier3, FEAT_10, Pair_ClustGpu_ForwardPlusVsForwardLitePresetPinSlotAndMatcher)
    {
        const auto forwardPlusGraph = Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        const auto forwardLiteGraph = Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardLitePreset();

        E2E_ASSERT_TRUE(forwardPlusGraph.isAcyclic());
        E2E_ASSERT_TRUE(forwardLiteGraph.isAcyclic());
        E2E_ASSERT_NE(forwardPlusGraph.findPass("ShadowMapPass"), nullptr);
        E2E_ASSERT_EQ(forwardLiteGraph.findPass("ShadowMapPass"), nullptr);

        // ForwardPlus 主通道必须包含 ShadowMap 引脚连线，而 ForwardLite 无阴影采样依赖
        bool foundShadowPinInPlus = false;
        for (const auto& pass : forwardPlusGraph.passes)
        {
            for (const auto& texIn : pass.textures)
            {
                if (texIn.source.find("Shadow") != std::string::npos || texIn.slot.find("Shadow") != std::string::npos)
                {
                    foundShadowPinInPlus = true;
                    auto resolvedOr = forwardPlusGraph.resolvePinSlot(pass.name, texIn.slot);
                    E2E_ASSERT_STATUS_OK(resolvedOr);
                    E2E_ASSERT_FALSE(resolvedOr->empty());
                }
            }
        }
        E2E_ASSERT_TRUE(foundShadowPinInPlus);

        // 注册 Shadow、ForwardPlus、ForwardLite、DeferredGBuffer 四套管线变体，验证离线针对两张小游戏预设的精确剥离
        std::vector<Domain::Renderer::PipelineBindingDesc> allVariants(4);
        allVariants[0].pipelineId = 1u;
        allVariants[0].name = "ShadowCaster_VSPS";
        allVariants[0].requiredPassTags = Domain::Renderer::PassTags::Shadow;

        allVariants[1].pipelineId = 2u;
        allVariants[1].name = "ClusteredLit_ForwardPlus";
        allVariants[1].requiredPassTags = Domain::Renderer::PassTags::ForwardPlus;

        allVariants[2].pipelineId = 3u;
        allVariants[2].name = "MobileLit_ForwardLite";
        allVariants[2].requiredPassTags = Domain::Renderer::PassTags::ForwardLite;

        allVariants[3].pipelineId = 4u;
        allVariants[3].name = "Desktop_DeferredGBuffer";
        allVariants[3].requiredPassTags = Domain::Renderer::PassTags::DeferredGBuffer;

        const auto keptForPlus = Domain::Renderer::RenderGraphMatcher::StripUnusedPipelinesForGraph(
            forwardPlusGraph, allVariants);
        const auto keptForLite = Domain::Renderer::RenderGraphMatcher::StripUnusedPipelinesForGraph(
            forwardLiteGraph, allVariants);

        // DeferredGBuffer 在两个小游戏预设中均必须被剥离；ShadowCaster 仅在 ForwardPlus 中保留，在 ForwardLite 中被剥离
        bool plusHasShadow = false;
        bool plusHasDeferred = false;
        for (const auto& p : keptForPlus)
        {
            if (p.pipelineId == 1u) plusHasShadow = true;
            if (p.pipelineId == 4u) plusHasDeferred = true;
        }
        E2E_ASSERT_TRUE(plusHasShadow);
        E2E_ASSERT_FALSE(plusHasDeferred);

        bool liteHasShadow = false;
        bool liteHasForwardLite = false;
        for (const auto& p : keptForLite)
        {
            if (p.pipelineId == 1u) liteHasShadow = true;
            if (p.pipelineId == 3u) liteHasForwardLite = true;
        }
        E2E_ASSERT_FALSE(liteHasShadow);
        E2E_ASSERT_TRUE(liteHasForwardLite);

        return true;
    }

    // 4. FEAT_11 × FEAT_09: 单张 2048x2048 动态视口阴影图集不重叠切分、偏置矩阵与聚光灯阴影通道联动
    E2E_TEST(Tier3, FEAT_11, Pair_ClustGpu_ShadowAtlasDynamicViewportTilesWithBiasMatrixAndSingleSampler)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        Domain::Renderer::ShadowAtlas shadowAtlas(2048u, 2048u);
        E2E_ASSERT_STATUS_OK(shadowAtlas.initGpuResources(&wgl2Ctx));
        E2E_ASSERT_NE(shadowAtlas.getAtlasImage(), nullptr);
        E2E_ASSERT_EQ(shadowAtlas.getShadowSamplerCount(), 1u);
        E2E_ASSERT_EQ(shadowAtlas.getShadowSamplerSlotName(), "u_ShadowAtlas");
        E2E_ASSERT_EQ(shadowAtlas.getFormat(), FCT::Format::D24_UNORM_S8_UINT);

        // 主平行光分配 1024x1024 高清瓦片，4 盏局部聚光灯各分配 512x512 瓦片
        Domain::Renderer::ShadowViewportRect dirRect{};
        E2E_ASSERT_TRUE(shadowAtlas.allocateTile(1024u, 1024u, dirRect));

        std::vector<Domain::Renderer::ClusterLightRecord> spotLights(4);
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            Domain::Renderer::ShadowViewportRect spotRect{};
            E2E_ASSERT_TRUE(shadowAtlas.allocateTile(512u, 512u, spotRect));
            E2E_ASSERT_FALSE(dirRect.intersects(spotRect));

            spotLights[i].lightType = 1u;
            spotLights[i].position[0] = static_cast<float>(i) * 2.0f;
            spotLights[i].position[2] = -5.0f;
            spotLights[i].radius = 8.0f;
            spotLights[i].shadowChannel = i + 1u;
        }
        E2E_ASSERT_EQ(shadowAtlas.getAllocatedCount(), 5u);

        // 校验全部已分配子视口两两严格不重叠且不越出 2048x2048 边界
        const auto& rects = shadowAtlas.getAllocatedTiles();
        for (size_t i = 0; i < rects.size(); ++i)
        {
            E2E_ASSERT_LE(rects[i].x + rects[i].width, 2048u);
            E2E_ASSERT_LE(rects[i].y + rects[i].height, 2048u);
            for (size_t j = i + 1u; j < rects.size(); ++j)
            {
                E2E_ASSERT_FALSE(rects[i].intersects(rects[j]));
            }
        }

        // 手动插入与主平行光重叠的自定义视口及越界网格下标必须被拦截
        Status overlapStatus = shadowAtlas.insertCustomViewport(dirRect.x, dirRect.y, 256u, 256u);
        E2E_ASSERT_FALSE(overlapStatus.ok());

        shadowAtlas.reset();
        E2E_ASSERT_EQ(shadowAtlas.getAllocatedCount(), 0u);
        auto validGridTileOr = shadowAtlas.allocateGridTile(0u, 512u);
        E2E_ASSERT_STATUS_OK(validGridTileOr);
        auto outOfRangeTileOr = shadowAtlas.allocateGridTile(16u, 512u); // 2048/512 = 4x4 = 16 格，下标 16 越界
        E2E_ASSERT_FALSE(outOfRangeTileOr.ok());

        const FCT::Vec4 uvScaleOffset = shadowAtlas.computeUvScaleOffset(validGridTileOr.value());
        E2E_ASSERT_NEAR(uvScaleOffset.x, 0.25f, 1e-4f);
        E2E_ASSERT_NEAR(uvScaleOffset.y, 0.25f, 1e-4f);

        const FCT::Mat4 biasMat = shadowAtlas.computeShadowBiasMatrix(validGridTileOr.value());
        E2E_ASSERT_GT(std::abs(biasMat.m[0]), 0.0f);

        shadowAtlas.releaseGpuResources();
        return true;
    }

    // 5. FEAT_12 × FEAT_40: 单 FBO 前向渲染通道直写默认帧缓冲（零离屏颜色 RT）与 FCT::RenderGraph 实例化闭环
    E2E_TEST(Tier3, FEAT_12, Pair_ClustGpu_SingleFboForwardPlusDirectBackbufferZeroOffscreenColor)
    {
        FCT::Emscripten_Runtime runtime;
        FCT::MiniGame_Window miniWnd(&runtime, 375, 667, 2.0f, 2.0f);
        miniWnd.createPlatform();

        FCT::WGL2_Context wgl2Ctx(&runtime);
        wgl2Ctx.create(FCT::ContextCreateFlag::withModuleResourceManage |
                       FCT::ContextCreateFlag::withModuleRenderGraph);

        const auto forwardPlus = Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        const auto forwardLite = Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardLitePreset();
        const auto desktopHdr = Domain::Renderer::RenderGraphAsset::CreateDesktopDeferredHdrPreset();

        // PRD 10 §3.1-3.2：ForwardPlus 保留单张非 MRT 离屏 HDR 缓冲，ForwardLite 直写窗口 Backbuffer (0 离屏)
        E2E_ASSERT_FALSE(forwardPlus.isDirectToBackbuffer());
        E2E_ASSERT_EQ(forwardPlus.countOffscreenColorImages(), 1u);
        E2E_ASSERT_STATUS_OK(forwardPlus.validateDepthSamplingHazard());

        E2E_ASSERT_TRUE(forwardLite.isDirectToBackbuffer());
        E2E_ASSERT_EQ(forwardLite.countOffscreenColorImages(), 0u);
        E2E_ASSERT_STATUS_OK(forwardLite.validateDepthSamplingHazard());

        E2E_ASSERT_GT(desktopHdr.countOffscreenColorImages(), 0u);

        // 将小游戏纯数据预设映射进真实 FCT::RenderGraph 模块
        FCT::RenderGraph* fctRg = wgl2Ctx.getModule<FCT::RenderGraph>();
        E2E_ASSERT_NE(fctRg, nullptr);
        E2E_ASSERT_STATUS_OK(Domain::Renderer::RenderGraphAsset::ApplyToFctRenderGraph(
            forwardLite, fctRg, &miniWnd));
        E2E_ASSERT_FALSE(fctRg->getOriginalPasses().empty());

        return true;
    }

    // 6. FEAT_13 × FEAT_29: GPUScene Shelf 图集打包、GL_TEXTURE_2D_ARRAY 256 层上限防卫与非法槽位回退白贴图
    E2E_TEST(Tier3, FEAT_13, Pair_ClustGpu_GpuSceneShelfAtlasAndTexture2DArrayLayerBounds)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));
        E2E_ASSERT_EQ(gpuScene.getDescriptorTier(), Domain::Renderer::DescriptorIndexingTier::Tier0_AtlasFallback);

        const auto allocA = gpuScene.registerTextureToAtlas(512u, 512u);
        const auto allocB = gpuScene.registerTextureToAtlas(256u, 256u);
        E2E_ASSERT_TRUE(allocA.success);
        E2E_ASSERT_TRUE(allocB.success);
        E2E_ASSERT_EQ(allocA.layer, allocB.layer);
        E2E_ASSERT_GE(allocB.x, allocA.x + allocA.width);

        const FCT::Vec4 uvA = gpuScene.getTextureUvScaleOffset(allocA);
        const FCT::Vec4 uvB = gpuScene.getTextureUvScaleOffset(allocB);
        E2E_ASSERT_NEAR(uvA.x, 512.0f / 2048.0f, 1e-4f);
        E2E_ASSERT_NEAR(uvB.z, static_cast<float>(allocB.x) / 2048.0f, 1e-4f);

        // GL_TEXTURE_2D_ARRAY 最大支持 256 层（0..255），第 256 层必须返回 ResourceExhaustedError
        E2E_ASSERT_STATUS_OK(gpuScene.allocateTextureLayer(3u, uvA));
        E2E_ASSERT_STATUS_OK(gpuScene.allocateTextureLayer(255u, uvB));
        Status layerOverflow = gpuScene.allocateTextureLayer(256u, uvB);
        E2E_ASSERT_FALSE(layerOverflow.ok());
        E2E_ASSERT_EQ(layerOverflow.code(), absl::StatusCode::kResourceExhausted);

        // 查询未注册的越界槽位 9999 时，必须安全回退至 Slot 0 默认纯白单位 UV 记录，绝不越界崩溃
        const auto fallbackRec = gpuScene.getTextureRecord(9999u);
        E2E_ASSERT_EQ(fallbackRec.bindlessOrPageIndex, 0u);
        E2E_ASSERT_NEAR(fallbackRec.uvScaleOffset.x, 1.0f, 1e-4f);
        E2E_ASSERT_NEAR(fallbackRec.uvScaleOffset.z, 0.0f, 1e-4f);

        // 模拟异步纹理解码完成后对占位槽位 2 执行热更新
        Domain::Renderer::GpuTextureRecord hotSwapped{};
        hotSwapped.uvScaleOffset = uvB;
        hotSwapped.bindlessOrPageIndex = allocB.layer;
        hotSwapped.flags = 0u;
        gpuScene.updateTextureRecord(2u, hotSwapped);
        E2E_ASSERT_NEAR(gpuScene.getTextureRecord(2u).uvScaleOffset.z, uvB.z, 1e-4f);

        gpuScene.shutdown();
        return true;
    }

    // 7. FEAT_13 × FEAT_71: 跨材质合批材质表上传与同 Pass 单一不透明分桶零切换材质调度
    E2E_TEST(Tier3, FEAT_13, Pair_ClustGpu_CrossMaterialBatchingSingleBucketWithMaterialTableUpload)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));

        // 8 种不同 PBR 参数与子图集贴图的材质统一写入 GpuMaterialTable，无需拆分材质描述符集
        std::vector<Domain::Renderer::GpuMaterialRecord> materials(8);
        for (uint32_t i = 0u; i < 8u; ++i)
        {
            const auto subAtlas = gpuScene.registerTextureToAtlas(128u, 128u);
            E2E_ASSERT_TRUE(subAtlas.success);
            const FCT::Vec4 uvRect = gpuScene.getTextureUvScaleOffset(subAtlas);
            const uint32_t texSlot = 10u + i;
            Domain::Renderer::GpuTextureRecord texRec{};
            texRec.uvScaleOffset = uvRect;
            texRec.bindlessOrPageIndex = subAtlas.layer;
            gpuScene.updateTextureRecord(texSlot, texRec);

            materials[i].baseColorFactor = FCT::Vec4{0.1f * static_cast<float>(i + 1u), 0.5f, 0.8f, 1.0f};
            materials[i].albedoTexIndex = texSlot;
            materials[i].shadingModelId = 1u;
            materials[i].pipelineBucket = 0u;
        }
        E2E_ASSERT_STATUS_OK(gpuScene.uploadMaterialData(materials, 0u));

        // 跨材质合批后 8 种材质共享同一管线变体与单一 DrawBucket（1 次合批绘制调用）
        Domain::Renderer::RenderGraphMatcher matcher;
        Domain::Renderer::PipelineBindingDesc unifiedOpaquePipe;
        unifiedOpaquePipe.pipelineId = 100u;
        unifiedOpaquePipe.ownerTechId = 1u;
        unifiedOpaquePipe.name = "UnifiedGpuSceneOpaque";
        unifiedOpaquePipe.requiredPassTags = Domain::Renderer::PassTags::Opaque | Domain::Renderer::PassTags::ForwardPlus;
        unifiedOpaquePipe.requiredInputTags = Domain::Renderer::InputTags::Mesh | Domain::Renderer::InputTags::Opaque;
        matcher.registerPipeline(unifiedOpaquePipe);

        Domain::Renderer::DrawBucket mergedBucket(
            1u,
            Domain::Renderer::InputTags::Mesh | Domain::Renderer::InputTags::Opaque,
            64u);
        std::vector<Domain::Renderer::DrawBucket> frameBuckets = {mergedBucket};

        const uint64_t passMask = Domain::Renderer::PassTags::Opaque | Domain::Renderer::PassTags::ForwardPlus;
        const auto execItems = matcher.evaluatePassExecutionItems(passMask, frameBuckets);
        E2E_ASSERT_EQ(execItems.size(), 1u);
        E2E_ASSERT_EQ(execItems[0].bucket->instanceCount, 64u);

        gpuScene.shutdown();
        return true;
    }

    // 8. FEAT_14 × FEAT_25: 1024 宽 2D 数据纹理位运算寻址、跨行脏区间计算与 WGL2_StorageBuffer 最多 3 段子区切分
    E2E_TEST(Tier3, FEAT_14, Pair_ClustGpu_DataTexture1024WideCoordMappingAndDirtyRowSpan)
    {
        // GpuInstanceRow = 160B = 10 个 RGBA32UI 纹素；第 102 个实例起始纹素 = 1020，第 103 个起始纹素 = 1030（跨入第 1 行 x=6）
        constexpr uint32_t kInstanceTexelSize = sizeof(Domain::Renderer::GpuInstanceRow) / 16u;
        static_assert(kInstanceTexelSize == 10u, "GpuInstanceRow 必须占 10 个 16B 纹素");

        uint32_t x102 = 0u, y102 = 0u;
        uint32_t x103 = 0u, y103 = 0u;
        Domain::Renderer::GPUScene::TexelCoordFromSlot(102u, kInstanceTexelSize, x102, y102);
        Domain::Renderer::GPUScene::TexelCoordFromSlot(103u, kInstanceTexelSize, x103, y103);
        E2E_ASSERT_EQ(x102, 1020u);
        E2E_ASSERT_EQ(y102, 0u);
        E2E_ASSERT_EQ(x103, 6u);
        E2E_ASSERT_EQ(y103, 1u);

        // 槽位 [102, 103] 横跨第 0 行末尾与第 1 行开头，脏行跨度应为 startRow=0, rowCount=2
        auto dirtySpanOr = Domain::Renderer::GPUScene::computeDirtyTextureRows(102u, 103u, kInstanceTexelSize);
        E2E_ASSERT_STATUS_OK(dirtySpanOr);
        E2E_ASSERT_EQ(dirtySpanOr->startRow, 0u);
        E2E_ASSERT_EQ(dirtySpanOr->rowCount, 2u);

        // 逆序区间 startSlot > endSlot 必须返回 InvalidArgumentError
        auto badSpanOr = Domain::Renderer::GPUScene::computeDirtyTextureRows(50u, 20u, kInstanceTexelSize);
        E2E_ASSERT_FALSE(badSpanOr.ok());
        E2E_ASSERT_EQ(badSpanOr.status().code(), absl::StatusCode::kInvalidArgument);

        // 联动 WGL2_StorageBuffer 子区拆解：跨多行的连续更新至多拆分为头/中整行/尾 3 个矩形子区
        const size_t byteOffset = 102u * sizeof(Domain::Renderer::GpuInstanceRow);
        const size_t byteSize = 250u * sizeof(Domain::Renderer::GpuInstanceRow);
        const auto subRegions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(byteOffset, byteSize);
        E2E_ASSERT_GE(subRegions.size(), 2u);
        E2E_ASSERT_LE(subRegions.size(), 3u);

        return true;
    }

    // 9. FEAT_14 × FEAT_38: 超出初始 4MB 预分配容量的海量实例矩阵动态扩容上传（突破 WebGL2 64KB UBO 硬限）
    E2E_TEST(Tier3, FEAT_14, Pair_ClustGpu_DynamicGrowthBeyond4MBInstanceDataAndSimdTransform)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);
        E2E_ASSERT_EQ(wgl2Ctx.getMaxUniformBufferRange(), 65536u);

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));
        E2E_ASSERT_NE(gpuScene.getDataTextureBuffer(), nullptr);
        const size_t initialBytes = gpuScene.getDataTextureBuffer()->size();
        E2E_ASSERT_EQ(initialBytes, 4u * 1024u * 1024u);

        // 27,000 个实例 * 160B = 4,320,000B (> 4MB 初始容量，且为 64KB UBO 上限的 65 倍以上)
        constexpr size_t kLargeInstanceCount = 27000u;
        std::vector<Domain::Renderer::GpuInstanceRow> instances(kLargeInstanceCount);
        for (size_t i = 0; i < kLargeInstanceCount; ++i)
        {
            instances[i].modelMatrix.identity();
            instances[i].invTransposeMatrix.identity();
            instances[i].worldSphereBounds = FCT::Vec4{static_cast<float>(i % 128u), 0.0f, -5.0f, 1.0f};
            instances[i].entityId = static_cast<uint32_t>(100000u + i);
            instances[i].materialSlot = static_cast<uint32_t>(i & 7u);
            instances[i].flags = 1u;
        }

        E2E_ASSERT_STATUS_OK(gpuScene.uploadInstanceData(instances, 0u));
        E2E_ASSERT_GE(gpuScene.getDataTextureBuffer()->size(), kLargeInstanceCount * sizeof(Domain::Renderer::GpuInstanceRow));

        const auto* wgl2Storage = dynamic_cast<const FCT::RHI::WGL2_StorageBuffer*>(gpuScene.getDataTextureBuffer());
        E2E_ASSERT_NE(wgl2Storage, nullptr);
        E2E_ASSERT_EQ(wgl2Storage->getWidth(), 1024u);
        E2E_ASSERT_GT(wgl2Storage->getHeight(), 256u);

        // 映射读回最后一个实例（下标 26999），确认扩容重分配与子区拷贝无数据截断
        const auto* gpuRows = static_cast<const Domain::Renderer::GpuInstanceRow*>(gpuScene.getDataTextureBuffer()->map());
        E2E_ASSERT_NE(gpuRows, nullptr);
        E2E_ASSERT_EQ(gpuRows[kLargeInstanceCount - 1u].entityId, 126999u);
        gpuScene.getDataTextureBuffer()->unmap();

        gpuScene.shutdown();
        return true;
    }

    // 10. FEAT_15 × FEAT_27: GPUScene 两级 SIMD128 视锥剔除输出紧凑 R32_UINT 槽位流与 WGL2 PerInstance 顶点布局闭环
    E2E_TEST(Tier3, FEAT_15, Pair_ClustGpu_SimdFrustumCullingSlotStreamWithPerInstanceInputLayout)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));

        // 正交裁剪盒 [-10, +10]^3，构造 11 个实例（含 4 路 SIMD 整批 + 尾部标量 + 隐藏标记 + NaN 包围球 + 盒外剔除）
        const FCT::Mat4 viewProj = T3Clust_MakeOrthoBoxMatrix(10.0f);
        std::vector<Domain::Renderer::GpuInstanceRow> sceneRows(11);
        for (uint32_t i = 0u; i < 11u; ++i)
        {
            sceneRows[i].modelMatrix.identity();
            sceneRows[i].invTransposeMatrix.identity();
            sceneRows[i].entityId = i;
            sceneRows[i].flags = 1u;
        }
        // 可见槽位：0, 2, 4, 6, 9, 10（共 6 个）
        sceneRows[0].worldSphereBounds = FCT::Vec4{0.0f, 0.0f, 0.0f, 1.0f};      // 可见
        sceneRows[1].worldSphereBounds = FCT::Vec4{25.0f, 0.0f, 0.0f, 1.0f};     // 右侧盒外剔除
        sceneRows[2].worldSphereBounds = FCT::Vec4{-9.5f, 0.0f, 0.0f, 1.0f};     // 压左边界相交可见
        sceneRows[3].worldSphereBounds = FCT::Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        sceneRows[3].flags = 0u;                                                  // 隐藏标记过滤
        sceneRows[4].worldSphereBounds = FCT::Vec4{3.0f, -4.0f, 2.0f, 1.5f};     // 可见
        sceneRows[5].worldSphereBounds = FCT::Vec4{0.0f, 0.0f, -30.0f, 2.0f};    // 远端盒外剔除
        sceneRows[6].worldSphereBounds = FCT::Vec4{8.0f, 8.0f, 8.0f, 0.5f};      // 可见
        sceneRows[7].worldSphereBounds = FCT::Vec4{0.0f, 0.0f, 0.0f, -1.0f};     // 非正半径过滤
        sceneRows[8].worldSphereBounds = FCT::Vec4{
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f};          // NaN 安全过滤
        sceneRows[9].worldSphereBounds = FCT::Vec4{-2.0f, 5.0f, -6.0f, 1.0f};    // 可见（尾部标量路径）
        sceneRows[10].worldSphereBounds = FCT::Vec4{0.0f, -10.4f, 0.0f, 0.6f};   // 球体擦边相交可见

        const std::vector<uint32_t> visibleSlots = gpuScene.extractVisibleInstanceSlotStream(sceneRows, viewProj);
        const std::vector<uint32_t> expectedSlots = {0u, 2u, 4u, 6u, 9u, 10u};
        E2E_ASSERT_EQ(visibleSlots, expectedSlots);

        // 填入 DrawBucket 并绑定至 WGL2_InputLayout Slot 1 (PerInstance R32_UINT -> glVertexAttribIPointer + Divisor 1)
        Domain::Renderer::DrawBucket instancedBucket(
            1u,
            Domain::Renderer::InputTags::Mesh | Domain::Renderer::InputTags::Opaque,
            static_cast<uint32_t>(visibleSlots.size()));
        instancedBucket.instanceSlots = visibleSlots;
        E2E_ASSERT_EQ(instancedBucket.instanceCount, 6u);

        FCT::VertexLayout slotStreamLayout{
            FCT::VertexElement{FCT::Format::R32_UINT, FCT::VtxType::Custom, "a_InstanceSlot"}
        };
        FCT::RHI::WGL2_InputLayout wgl2Layout(&wgl2Ctx);
        wgl2Layout.vertexLayout(slotStreamLayout, 1u);
        wgl2Layout.inputRate(FCT::RHI::InputRate::PerInstance, 1u);
        wgl2Layout.create();
        const auto& slotRates = wgl2Layout.getInputRates();
        const auto rateIt = slotRates.find(1u);
        E2E_ASSERT_TRUE(rateIt != slotRates.end());
        E2E_ASSERT_EQ(rateIt->second, FCT::RHI::InputRate::PerInstance);
        const auto& slotLayouts = wgl2Layout.getVertexLayouts();
        const auto layoutIt = slotLayouts.find(1u);
        E2E_ASSERT_TRUE(layoutIt != slotLayouts.end());
        E2E_ASSERT_EQ(layoutIt->second.getStride(), sizeof(uint32_t));
        wgl2Layout.applyBinding(1u, 33u);

        gpuScene.shutdown();
        return true;
    }

    // 11. FEAT_33 × FEAT_11: 只写主通道深度降级为 WebGLRenderbuffer 与需采样的阴影深度纹理隔离校验
    E2E_TEST(Tier3, FEAT_33, Pair_ClustGpu_WriteOnlyDepthRenderbufferDowngradeVsSampledShadowTexture)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool transientPool(&wgl2Dev);

        // 主前向通道深度模板缓冲：仅用于硬件深度测试，不被下游采样 -> 自动降级为 Renderbuffer 省去纹理采样电路开销
        FCT::TransientResourceSpec mainDepthSpec;
        mainDepthSpec.width = 750u;
        mainDepthSpec.height = 1334u;
        mainDepthSpec.format = FCT::Format::D24_UNORM_S8_UINT;
        mainDepthSpec.isDepthStencil = true;
        mainDepthSpec.isSampled = false;
        mainDepthSpec.allowRenderbufferDowngrade = true;
        mainDepthSpec.usage = FCT::ImageUsage::DepthStencil;

        // 阴影图集深度缓冲：必须被主光照通道采样 -> 保持 GL_TEXTURE_2D 深度纹理身份
        FCT::TransientResourceSpec shadowDepthSpec;
        shadowDepthSpec.width = 2048u;
        shadowDepthSpec.height = 2048u;
        shadowDepthSpec.format = FCT::Format::D24_UNORM_S8_UINT;
        shadowDepthSpec.isDepthStencil = true;
        shadowDepthSpec.isSampled = true;
        shadowDepthSpec.allowRenderbufferDowngrade = true;
        shadowDepthSpec.usage = FCT::ImageUsage::DepthStencil | FCT::ImageUsage::Texture;

        FCT::Image* mainDepthImg = transientPool.acquire(mainDepthSpec, 1u, 2u, 1u);
        FCT::Image* shadowDepthImg = transientPool.acquire(shadowDepthSpec, 0u, 2u, 1u);
        E2E_ASSERT_NE(mainDepthImg, nullptr);
        E2E_ASSERT_NE(shadowDepthImg, nullptr);
        E2E_ASSERT_TRUE(mainDepthImg->isRenderbuffer());
        E2E_ASSERT_FALSE(shadowDepthImg->isRenderbuffer());

        // 对比 ShadowAtlas 持久化深度图集亦必须为非 Renderbuffer 的可采样纹理
        Domain::Renderer::ShadowAtlas atlas(2048u, 2048u);
        E2E_ASSERT_STATUS_OK(atlas.initGpuResources(&wgl2Ctx));
        E2E_ASSERT_NE(atlas.getAtlasImage(), nullptr);
        E2E_ASSERT_FALSE(atlas.getAtlasImage()->isRenderbuffer());
        atlas.releaseGpuResources();

        return true;
    }

    // 12. FEAT_34 × FEAT_12: RenderGraph::canMergePasses 同目标无采样依赖合并与跨通道纹理采样强制切分
    E2E_TEST(Tier3, FEAT_34, Pair_ClustGpu_RenderGraphPassMergeCompatibilityVsSampledTextureSplit)
    {
        FCT::Emscripten_Runtime runtime;
        FCT::WGL2_Context wgl2Ctx(&runtime);
        wgl2Ctx.create(FCT::ContextCreateFlag::withModuleResourceManage |
                       FCT::ContextCreateFlag::withModuleRenderGraph);
        FCT::RenderGraph* rg = wgl2Ctx.getModule<FCT::RenderGraph>();
        E2E_ASSERT_NE(rg, nullptr);

        // 场景 A：OpaquePass 与 TranslucentPass 共享同一 SceneColor + SceneDepth，且后者不把前者输出作纹理采样 -> 可合并至同一 PassGroup/FBO
        FCT::TargetEdge opaqueColorEdge;
        opaqueColorEdge.fromPass = "OpaquePass";
        opaqueColorEdge.toImage = "SceneColor";
        FCT::DepthStencilEdge opaqueDepthEdge;
        opaqueDepthEdge.fromPass = "OpaquePass";
        opaqueDepthEdge.toImage = "SceneDepth";

        FCT::TargetEdge transColorEdge;
        transColorEdge.fromPass = "TranslucentPass";
        transColorEdge.toImage = "SceneColor";
        FCT::DepthStencilEdge transDepthEdge;
        transDepthEdge.fromPass = "TranslucentPass";
        transDepthEdge.toImage = "SceneDepth";

        FCT::RenderGraphPassNode opaqueNode("OpaquePass");
        opaqueNode.addOutgoingEdge(&opaqueColorEdge);
        opaqueNode.addOutgoingEdge(&opaqueDepthEdge);

        FCT::RenderGraphPassNode translucentNode("TranslucentPass");
        translucentNode.addOutgoingEdge(&transColorEdge);
        translucentNode.addOutgoingEdge(&transDepthEdge);

        E2E_ASSERT_TRUE(rg->canMergePasses(opaqueNode, translucentNode));

        // 场景 B：PostToneMapPass 将 SceneColor 作为 TextureEdge 采样输入并写出 Backbuffer -> 严禁与 OpaquePass 合并
        FCT::TextureEdge sampleSceneColorEdge;
        sampleSceneColorEdge.fromImage = "SceneColor";
        sampleSceneColorEdge.toPass = "PostToneMapPass";
        FCT::TargetEdge backbufferEdge;
        backbufferEdge.fromPass = "PostToneMapPass";
        backbufferEdge.toImage = "Backbuffer";

        FCT::RenderGraphPassNode postNode("PostToneMapPass");
        postNode.addIncomingEdge(&sampleSceneColorEdge);
        postNode.addOutgoingEdge(&backbufferEdge);

        E2E_ASSERT_FALSE(rg->canMergePasses(opaqueNode, postNode));

        return true;
    }

    // 13. FEAT_35 × FEAT_70: TBDR 帧末深度丢弃计划发射与下游深度采样冲突静态防呆拦截
    E2E_TEST(Tier3, FEAT_35, Pair_ClustGpu_TbdrDepthDiscardInvalidatePlanAndDepthSamplingHazardGuard)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);

        // 1) 合法 TBDR 丢弃配置：主通道结束后丢弃深度且无下游采样该深度
        const std::string validDiscardToml = R"(
name = "ValidTbdrDiscardGraph"
version = "3.0"

[[images]]
name = "MainDepth"
format = "D24_UNORM_S8_UINT"

[[passes]]
name = "ForwardMainPass"
tags = ["Opaque", "ForwardLite"]
clear_color = [0.05, 0.05, 0.08, 1.0]
clear_depth = 1.0
discard_depth = true
depth_stencil = "MainDepth"
targets = [{ image = "WindowBackbuffer", is_window = true }]
)";
        auto validGraphOr = Domain::Renderer::RenderGraphAsset::LoadFromToml(validDiscardToml);
        E2E_ASSERT_STATUS_OK(validGraphOr);
        E2E_ASSERT_STATUS_OK(validGraphOr->validateDepthSamplingHazard());
        const auto* mainPass = validGraphOr->findPass("ForwardMainPass");
        E2E_ASSERT_NE(mainPass, nullptr);
        E2E_ASSERT_TRUE(mainPass->discardDepthAfterPass);
        E2E_ASSERT_EQ(mainPass->toPureDataNode(1u).discardDepthOnExit, 1u);

        // 2) 非法冲突配置：上游 Pass 声明 discard_depth = true，下游 Pass 却采样 MainDepth -> 必须报错拦截
        const std::string hazardToml = R"(
name = "HazardTbdrDiscardGraph"
version = "3.0"

[[images]]
name = "MainDepth"
format = "D24_UNORM_S8_UINT"

[[images]]
name = "SceneColor"
format = "R8G8B8A8_UNORM"

[[passes]]
name = "ForwardMainPass"
tags = ["Opaque"]
discard_depth = true
depth_stencil = "MainDepth"
targets = [{ image = "SceneColor", is_window = false }]

[[passes]]
name = "DepthFogPass"
tags = ["PostProcess"]
textures = [{ slot = "u_DepthTex", source = "MainDepth", fallback = "black" }]
targets = [{ image = "WindowBackbuffer", is_window = true }]
)";
        auto hazardGraphOr = Domain::Renderer::RenderGraphAsset::LoadFromToml(hazardToml);
        E2E_ASSERT_FALSE(hazardGraphOr.ok());

        // 3) 绑定至 WGL2_PassGroup 执行 TBDR Invalidate 发射（默认帧缓冲丢弃深度模板回写带宽）
        FCT::RHI::TBDRInvalidatePlan tbdrPlan;
        tbdrPlan.isDefaultFBO = true;
        tbdrPlan.endDiscards = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};

        FCT::RHI::WGL2_PassGroup passGroup(&wgl2Ctx);
        passGroup.setTBDRInvalidatePlan(tbdrPlan);
        E2E_ASSERT_TRUE(passGroup.getTBDRInvalidatePlan().isDefaultFBO);
        E2E_ASSERT_EQ(passGroup.getTBDRInvalidatePlan().endDiscards.size(), 2u);
        FCT::RHI::WGL2_PassGroup::emitInvalidate(tbdrPlan.endDiscards, true);

        return true;
    }

    // 14. FEAT_36 × FEAT_12: WebGL2 默认帧缓冲 FBO 0 (GL_COLOR/GL_DEPTH/GL_STENCIL) 与离屏 FBO 附件枚举双向精确映射
    E2E_TEST(Tier3, FEAT_36, Pair_ClustGpu_DefaultFboZeroInvalidateEnumTranslationVsOffscreenFbo)
    {
        const auto litePreset = Domain::Renderer::RenderGraphAsset::CreateMiniGameForwardLitePreset();
        E2E_ASSERT_TRUE(litePreset.isDirectToBackbuffer());

        const std::vector<GLenum> rawAttachments = {
            GL_COLOR_ATTACHMENT0,
            GL_DEPTH_ATTACHMENT,
            GL_STENCIL_ATTACHMENT
        };

        // 默认帧缓冲 (FBO 0, isWindow == true) 严禁向 glInvalidateFramebuffer 传递 GL_COLOR_ATTACHMENT0（否则触发 GL_INVALID_ENUM）
        const auto defaultFboTokens = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(rawAttachments, true);
        E2E_ASSERT_EQ(defaultFboTokens.size(), 3u);
        E2E_ASSERT_EQ(defaultFboTokens[0], static_cast<GLenum>(GL_COLOR));
        E2E_ASSERT_EQ(defaultFboTokens[1], static_cast<GLenum>(GL_DEPTH));
        E2E_ASSERT_EQ(defaultFboTokens[2], static_cast<GLenum>(GL_STENCIL));

        // 组合枚举 GL_DEPTH_STENCIL_ATTACHMENT 在 FBO 0 下亦须拆解或映射为合法的默认帧缓冲枚举，不含 GL_DEPTH_ATTACHMENT
        const std::vector<GLenum> combinedDs = {GL_DEPTH_STENCIL_ATTACHMENT};
        const auto defaultDsTokens = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(combinedDs, true);
        E2E_ASSERT_FALSE(defaultDsTokens.empty());
        for (const GLenum token : defaultDsTokens)
        {
            E2E_ASSERT_TRUE(token == static_cast<GLenum>(GL_DEPTH) || token == static_cast<GLenum>(GL_STENCIL));
        }

        // 离屏 FBO (isDefaultFBO == false) 必须原样保留 GL_COLOR_ATTACHMENT0 / GL_DEPTH_ATTACHMENT
        const auto offscreenTokens = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(rawAttachments, false);
        E2E_ASSERT_EQ(offscreenTokens.size(), 3u);
        E2E_ASSERT_EQ(offscreenTokens[0], static_cast<GLenum>(GL_COLOR_ATTACHMENT0));
        E2E_ASSERT_EQ(offscreenTokens[1], static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
        E2E_ASSERT_EQ(offscreenTokens[2], static_cast<GLenum>(GL_STENCIL_ATTACHMENT));

        return true;
    }

    // 15. FEAT_08 × FEAT_32 × FEAT_59: 抖音 tt.onMemoryWarning 分级联动瞬态渲染池、双轨字体图集与 GPUScene 缓存自救
    E2E_TEST(Tier3, FEAT_08, Pair_ClustGpu_DouyinMemoryWarningCoordinatorCascadeAcrossPoolFontAndGpuScene)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Clust_InitHeadlessWgl2(wgl2Ctx);
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool transientPool(&wgl2Dev);

        // 预热瞬态渲染池：驻留 2 块已完成当前帧生命周期的临时缓冲
        FCT::TransientResourceSpec tempColorSpec;
        tempColorSpec.width = 512u;
        tempColorSpec.height = 512u;
        tempColorSpec.format = FCT::Format::R8G8B8A8_UNORM;
        tempColorSpec.usage = FCT::ImageUsage::RenderTarget | FCT::ImageUsage::Texture;
        (void)transientPool.acquire(tempColorSpec, 0u, 1u, 1u);
        (void)transientPool.acquire(tempColorSpec, 1u, 2u, 1u);
        E2E_ASSERT_EQ(transientPool.getPoolSize(), 2u);

        // 预热双轨字体系统（小游戏混合模式）与 GPUScene Shelf 图集
        Resource::Font::FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(Resource::Font::FontBackendMode::MiniGame_Hybrid));
        E2E_ASSERT_STATUS_OK(fontSys.initGpuResources(&wgl2Ctx));
        const char32_t warmChars[] = {U'战', U'斗', U'胜', U'利'};
        E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(warmChars, 24u));
        const uint32_t genBeforeWarning = fontSys.getAtlasGeneration();

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));
        const auto preWarnAlloc = gpuScene.registerTextureToAtlas(1024u, 1024u);
        E2E_ASSERT_TRUE(preWarnAlloc.success);

        uint32_t customAssetTrimCalls = 0u;
        auto hostApi = DouyinMiniGame::createWebJsDouyinHostApi();
        DouyinMiniGame::MemoryWarningCoordinator memCoordinator(hostApi.get());
        memCoordinator.bindTransientResourcePool(&transientPool);
        memCoordinator.bindFontSystem(&fontSys);
        memCoordinator.setAssetCacheTrimCallback(
            T3Clust_GpuSceneAssetTrimFunctor{&gpuScene, &customAssetTrimCalls});

        // Level 5 (Moderate) -> Level 10 (Low) -> Level 15 (Critical) 逐级自救
        E2E_ASSERT_STATUS_OK(memCoordinator.handleMemoryWarning(DouyinMiniGame::MemoryWarningCoordinator::kLevelModerate));
        E2E_ASSERT_STATUS_OK(memCoordinator.handleMemoryWarning(DouyinMiniGame::MemoryWarningCoordinator::kLevelLow));
        E2E_ASSERT_STATUS_OK(memCoordinator.handleMemoryWarning(DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical));

        const auto& stats = memCoordinator.getStats();
        E2E_ASSERT_EQ(stats.totalWarningsHandled, 3u);
        E2E_ASSERT_EQ(stats.lastWarningLevel, DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical);
        E2E_ASSERT_GE(customAssetTrimCalls, 1u);
        E2E_ASSERT_GT(stats.totalBytesReclaimed, 0u);

        // Critical 告警后：瞬态池闲置缓冲已被强制清空，字体图集代数递增，GPUScene 图集复位可重新从 (0,0) 分配
        E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);
        E2E_ASSERT_GT(fontSys.getAtlasGeneration(), genBeforeWarning);
        const auto postWarnAlloc = gpuScene.registerTextureToAtlas(1024u, 1024u);
        E2E_ASSERT_TRUE(postWarnAlloc.success);
        E2E_ASSERT_EQ(postWarnAlloc.x, 0u);
        E2E_ASSERT_EQ(postWarnAlloc.y, 0u);

        gpuScene.shutdown();
        fontSys.term();
        return true;
    }

} // namespace MQEngine::Testing
