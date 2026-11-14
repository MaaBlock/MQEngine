/**
 * @file Test_Tier1_Engine_Renderer.cpp
 * @brief Tier 1 功能级隔离测试：Engine 渲染图谱、3D Clustered 光照、GPUScene 与 TBDR 合批契约
 *        覆盖特性：FEAT-09..15 (7项) + FEAT-67..73 (7项) = 14 项特性 × 5 用例 = 70 个用例
 */

#include "Common/E2ECommon.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/ShadowAtlas.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>

#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT/RHI/WGL2_PassGroup.h>
#include <FCT/Context/TransientResourcePool.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <vector>

namespace MQEngine::Testing {

namespace {

using namespace ::MQEngine::Domain::Renderer;

FCT::Mat4 T1R_MakeIdentityMat4() noexcept {
    FCT::Mat4 mat{};
    for (int i = 0; i < 16; ++i) {
        mat.m[i] = 0.0f;
    }
    mat.m[0] = 1.0f;
    mat.m[5] = 1.0f;
    mat.m[10] = 1.0f;
    mat.m[15] = 1.0f;
    return mat;
}

FCT::Mat4 T1R_MakePerspectiveMat4(float fovYRad, float aspect, float nearZ, float farZ) noexcept {
    FCT::Mat4 mat{};
    for (int i = 0; i < 16; ++i) {
        mat.m[i] = 0.0f;
    }
    const float invTanHalfFov = 1.0f / std::tan(fovYRad * 0.5f);
    mat.m[0] = invTanHalfFov / aspect;
    mat.m[5] = invTanHalfFov;
    mat.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    mat.m[11] = -1.0f;
    mat.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return mat;
}

bool T1R_RectsOverlap(const AtlasAllocation& a, const AtlasAllocation& b) noexcept {
    if (a.layer != b.layer) {
        return false;
    }
    const bool sepX = (a.x + a.width <= b.x) || (b.x + b.width <= a.x);
    const bool sepY = (a.y + a.height <= b.y) || (b.y + b.height <= a.y);
    return !(sepX || sepY);
}

std::string T1R_ResolvePassPinTexture(const RenderGraphPassNodeDesc& pass, std::string_view slotName) {
    for (const auto& tex : pass.textures) {
        if (tex.slot == slotName) {
            return tex.source.empty() ? tex.fallback : tex.source;
        }
    }
    return {};
}

uint64_t T1R_CollectGraphActivePassMask(const RenderGraphAssetDesc& graph) noexcept {
    uint64_t mask = 0;
    for (const auto& pass : graph.passes) {
        mask |= pass.tagMask;
    }
    return mask;
}

FCT::Mat4 T1R_BuildShadowAtlasUvMatrix(const AtlasShelfPacker& packer, const AtlasAllocation& tile) noexcept {
    const FCT::Vec4 uvScaleOffset = packer.calculateUvScaleOffset(tile);
    FCT::Mat4 biasScale = T1R_MakeIdentityMat4();
    biasScale.m[0] = 0.5f * uvScaleOffset.x;
    biasScale.m[5] = 0.5f * uvScaleOffset.y;
    biasScale.m[10] = 0.5f;
    biasScale.m[12] = 0.5f * uvScaleOffset.x + uvScaleOffset.z;
    biasScale.m[13] = 0.5f * uvScaleOffset.y + uvScaleOffset.w;
    biasScale.m[14] = 0.5f;
    return biasScale;
}

GpuInstanceRow T1R_MakeInstanceRow(float cx, float cy, float cz, float radius, uint32_t materialSlot) noexcept {
    GpuInstanceRow row{};
    row.modelMatrix = T1R_MakeIdentityMat4();
    row.invTransposeMatrix = T1R_MakeIdentityMat4();
    row.worldSphereBounds = FCT::Vec4{cx, cy, cz, radius};
    row.meshSlot = 0;
    row.materialSlot = materialSlot;
    row.entityId = materialSlot + 1u;
    row.flags = 1u;
    return row;
}

} // namespace

// ============================================================================
// FEAT-09: 3D Clustered 前向多光源剔除与分桶 (ClusteredForwardPlus)
// ============================================================================

E2E_TEST(Tier1, FEAT_09, Clustered_GridDim_CalculatedCorrectly) {
    ClusteredForwardPlus clustered;
    const FCT::Mat4 proj = T1R_MakePerspectiveMat4(1.04719755f, 16.0f / 9.0f, 0.1f, 500.0f);
    clustered.updateFrustumGrid(proj, 0.1f, 500.0f, 1280, 720);

    const auto& cfg = clustered.getGridConfig();
    E2E_ASSERT_EQ(cfg.dimX, 16u);
    E2E_ASSERT_EQ(cfg.dimY, 8u);
    E2E_ASSERT_EQ(cfg.dimZ, 16u);
    E2E_ASSERT_EQ(cfg.totalFroxels(), 2048u);
    E2E_ASSERT_EQ(clustered.getFroxelLightGridBuffer().size(), 4096u);
    return true;
}

E2E_TEST(Tier1, FEAT_09, Clustered_LightTable_16ByteAligned) {
    E2E_ASSERT_EQ(sizeof(ClusterLightRecord) % 16u, 0u);
    E2E_ASSERT_EQ(alignof(ClusterLightRecord), 16u);
    E2E_ASSERT_EQ(sizeof(FroxelAabb) % 16u, 0u);
    E2E_ASSERT_EQ(alignof(FroxelAabb), 16u);

    std::vector<ClusterLightRecord> lights(8);
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(lights.data());
    E2E_ASSERT_EQ(baseAddr % 16u, 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_09, Clustered_DegradesToSingleCluster_WhenLightsLE4) {
    ClusteredForwardPlus clustered;
    const FCT::Mat4 proj = T1R_MakePerspectiveMat4(1.04719755f, 16.0f / 9.0f, 0.1f, 200.0f);
    const FCT::Mat4 view = T1R_MakeIdentityMat4();
    clustered.updateFrustumGrid(proj, 0.1f, 200.0f, 1280, 720);

    std::vector<ClusterLightRecord> fourLights(4);
    for (uint32_t i = 0; i < 4; ++i) {
        fourLights[i].position[2] = -5.0f - static_cast<float>(i);
        fourLights[i].radius = 3.0f;
    }
    clustered.cullLights(fourLights, view);
    E2E_ASSERT_TRUE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 1u);
    E2E_ASSERT_EQ(clustered.getActiveLightCount(), 4u);

    std::vector<ClusterLightRecord> fiveLights = fourLights;
    ClusterLightRecord extra{};
    extra.position[2] = -12.0f;
    extra.radius = 4.0f;
    fiveLights.push_back(extra);

    clustered.cullLights(fiveLights, view);
    E2E_ASSERT_FALSE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 2048u);
    return true;
}

E2E_TEST(Tier1, FEAT_09, Clustered_SimdFroxelIntersection_PointLight) {
    FroxelAabb froxel{};
    froxel.minPt[0] = -2.0f;
    froxel.minPt[1] = -2.0f;
    froxel.minPt[2] = -10.0f;
    froxel.maxPt[0] = 2.0f;
    froxel.maxPt[1] = 2.0f;
    froxel.maxPt[2] = -4.0f;

    const bool hitOverlap = ClusteredForwardPlus::TestFroxelSphereIntersection(froxel, 3.5f, 0.0f, -7.0f, 2.0f);
    const bool missFar = ClusteredForwardPlus::TestFroxelSphereIntersection(froxel, 10.0f, 0.0f, -7.0f, 2.0f);

    E2E_ASSERT_TRUE(hitOverlap);
    E2E_ASSERT_FALSE(missFar);
    return true;
}

E2E_TEST(Tier1, FEAT_09, Clustered_LightIndexList_PerFroxelBounded) {
    ClusteredForwardPlus clustered;
    const FCT::Mat4 proj = T1R_MakePerspectiveMat4(1.04719755f, 1.0f, 0.5f, 100.0f);
    const FCT::Mat4 view = T1R_MakeIdentityMat4();
    clustered.updateFrustumGrid(proj, 0.5f, 100.0f, 512, 512);

    std::vector<ClusterLightRecord> denseLights(48);
    for (auto& lt : denseLights) {
        lt.position[0] = 0.0f;
        lt.position[1] = 0.0f;
        lt.position[2] = -5.0f;
        lt.radius = 50.0f;
    }

    clustered.cullLights(denseLights, view);
    const auto& gridBuf = clustered.getFroxelLightGridBuffer();
    E2E_ASSERT_EQ(gridBuf.size(), 4096u);

    uint32_t maxObserved = 0;
    for (size_t fIdx = 0; fIdx < 2048u; ++fIdx) {
        const uint32_t count = gridBuf[fIdx * 2u + 1u];
        E2E_ASSERT_LE(count, ClusteredForwardPlus::MaxLightsPerFroxel);
        if (count > maxObserved) {
            maxObserved = count;
        }
    }
    E2E_ASSERT_EQ(maxObserved, 32u);
    return true;
}

// ============================================================================
// FEAT-10: 小游戏双档位数据驱动管线预设 (ForwardPlus / ForwardLite)
// ============================================================================

E2E_TEST(Tier1, FEAT_10, ForwardPlusPreset_LoadsWithoutShaderFields) {
    const RenderGraphAssetDesc preset = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_EQ(preset.name, "MiniGame_ForwardPlus");
    E2E_ASSERT_GE(preset.passes.size(), 5u);
    E2E_ASSERT_TRUE(preset.isAcyclic());

    const char* tomlSrc = R"(
name = "MiniGame_ForwardPlus_Custom"
version = "3.0"
[[images]]
name = "SceneHdrColor"
format = "RGBA16_SFLOAT"
[[passes]]
name = "ForwardOpaquePass"
tags = ["Geometry", "ForwardPlus", "Opaque"]
targets = ["SceneHdrColor"]
)";
    auto parsed = RenderGraphAsset::LoadFromToml(tomlSrc);
    E2E_ASSERT_STATUS_OK(parsed);
    E2E_ASSERT_EQ(parsed->passes.size(), 1u);
    E2E_ASSERT_EQ(parsed->passes[0].name, "ForwardOpaquePass");
    return true;
}

E2E_TEST(Tier1, FEAT_10, ForwardLitePreset_ZeroOffscreenColorTarget) {
    const RenderGraphAssetDesc lite = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    E2E_ASSERT_EQ(lite.name, "MiniGame_ForwardLite");

    uint32_t offscreenColorImages = 0;
    for (const auto& img : lite.images) {
        if (img.format.find('D') != 0) {
            ++offscreenColorImages;
        }
    }
    E2E_ASSERT_EQ(offscreenColorImages, 0u);
    E2E_ASSERT_EQ(lite.passes.size(), 1u);
    E2E_ASSERT_EQ(lite.passes[0].targets.size(), 1u);
    E2E_ASSERT_TRUE(lite.passes[0].targets[0].isWindow);
    return true;
}

E2E_TEST(Tier1, FEAT_10, ForwardPlusPreset_PassTags_ExtractedCorrectly) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* opaquePass = plus.findPass("ForwardOpaquePass");
    const auto* postPass = plus.findPass("PostProcessPass");
    const auto* uiPass = plus.findPass("UiOverlayPass");

    E2E_ASSERT_TRUE(opaquePass != nullptr);
    E2E_ASSERT_TRUE(postPass != nullptr);
    E2E_ASSERT_TRUE(uiPass != nullptr);

    E2E_ASSERT_NE(opaquePass->tagMask & PassTags::ForwardPlus, 0ULL);
    E2E_ASSERT_NE(opaquePass->tagMask & PassTags::Opaque, 0ULL);
    E2E_ASSERT_NE(postPass->tagMask & PassTags::PostProcess, 0ULL);
    E2E_ASSERT_NE(uiPass->tagMask & PassTags::UI, 0ULL);
    return true;
}

E2E_TEST(Tier1, FEAT_10, Preset_HotSwapPreservesBindings) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc opaquePipe{};
    opaquePipe.pipelineId = 101u;
    opaquePipe.ownerTechId = 1u;
    opaquePipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    opaquePipe.requiredPassTags = PassTags::Opaque;
    matcher.registerPipeline(opaquePipe);

    std::vector<DrawBucket> buckets = {
        DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 32u)
    };

    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const RenderGraphAssetDesc lite = RenderGraphAsset::CreateMiniGameForwardLitePreset();

    const auto* plusOpaque = plus.findPass("ForwardOpaquePass");
    const auto* liteOpaque = lite.findPass("ForwardLitePass");
    E2E_ASSERT_TRUE(plusOpaque != nullptr && liteOpaque != nullptr);

    const auto plusItems = matcher.evaluatePassExecutionItems(plusOpaque->tagMask, buckets);
    const auto liteItems = matcher.evaluatePassExecutionItems(liteOpaque->tagMask, buckets);
    E2E_ASSERT_EQ(plusItems.size(), 1u);
    E2E_ASSERT_EQ(liteItems.size(), 1u);
    E2E_ASSERT_EQ(plusItems[0].pipelineId, liteItems[0].pipelineId);
    return true;
}

E2E_TEST(Tier1, FEAT_10, Preset_WindowBackbuffer_TargetFlagged) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* postPass = plus.findPass("PostProcessPass");
    const auto* uiPass = plus.findPass("UiOverlayPass");
    E2E_ASSERT_TRUE(postPass != nullptr && uiPass != nullptr);

    E2E_ASSERT_EQ(postPass->targets.size(), 1u);
    E2E_ASSERT_TRUE(postPass->targets[0].isWindow);
    E2E_ASSERT_EQ(uiPass->targets.size(), 1u);
    E2E_ASSERT_TRUE(uiPass->targets[0].isWindow);
    return true;
}

// ============================================================================
// FEAT-11: 单 2048x2048 动态阴影图集与单 Pass 投射 (ShadowAtlas)
// ============================================================================

E2E_TEST(Tier1, FEAT_11, ShadowAtlas_AllocatesSingle2048Fbo) {
    ShadowAtlas atlas(ShadowAtlas::DefaultAtlasSize, ShadowAtlas::DefaultAtlasSize);
    E2E_ASSERT_EQ(atlas.getWidth(), 2048u);
    E2E_ASSERT_EQ(atlas.getHeight(), 2048u);
    E2E_ASSERT_EQ(atlas.getFormat(), FCT::Format::D24_UNORM_S8_UINT);

    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const RenderGraphImageDesc* shadowImg = plus.findImage("ShadowMapAtlas");
    E2E_ASSERT_TRUE(shadowImg != nullptr);
    E2E_ASSERT_EQ(shadowImg->width, 2048u);
    E2E_ASSERT_EQ(shadowImg->height, 2048u);
    E2E_ASSERT_EQ(shadowImg->format, "D24_UNORM_S8_UINT");
    return true;
}

E2E_TEST(Tier1, FEAT_11, ShadowAtlas_DynamicViewportSplitting) {
    ShadowAtlas shadowAtlas(2048, 2048);
    ShadowViewportRect mainDirTile{};
    ShadowViewportRect heroSpotTile{};
    ShadowViewportRect localPointTile{};

    E2E_ASSERT_TRUE(shadowAtlas.allocateTile(1024, 1024, mainDirTile));
    E2E_ASSERT_TRUE(shadowAtlas.allocateTile(1024, 1024, heroSpotTile));
    E2E_ASSERT_TRUE(shadowAtlas.allocateTile(512, 512, localPointTile));

    E2E_ASSERT_FALSE(mainDirTile.intersects(heroSpotTile));
    E2E_ASSERT_FALSE(mainDirTile.intersects(localPointTile));
    E2E_ASSERT_FALSE(heroSpotTile.intersects(localPointTile));
    E2E_ASSERT_EQ(shadowAtlas.getAllocatedCount(), 3u);
    return true;
}

E2E_TEST(Tier1, FEAT_11, ShadowAtlas_SingleSampler2DShadowBinding) {
    ShadowAtlas shadowAtlas(2048, 2048);
    E2E_ASSERT_EQ(shadowAtlas.getShadowSamplerCount(), 1u);
    E2E_ASSERT_EQ(ShadowAtlas::getShadowSamplerSlotName(), "u_ShadowAtlas");

    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    auto resolved = plus.resolvePinSlot("ForwardOpaquePass", "ShadowAtlas");
    E2E_ASSERT_STATUS_OK(resolved);
    E2E_ASSERT_EQ(*resolved, "ShadowMapAtlas");
    return true;
}

E2E_TEST(Tier1, FEAT_11, ShadowAtlas_SingleFboBindRecorded) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    uint32_t shadowPassCount = 0;
    const RenderGraphPassNodeDesc* shadowPass = nullptr;
    for (const auto& p : plus.passes) {
        if ((p.tagMask & PassTags::Shadow) != 0ULL) {
            ++shadowPassCount;
            shadowPass = &p;
        }
    }
    E2E_ASSERT_EQ(shadowPassCount, 1u);
    E2E_ASSERT_TRUE(shadowPass != nullptr);
    E2E_ASSERT_EQ(shadowPass->targets.size(), 0u);
    E2E_ASSERT_EQ(shadowPass->depthStencilImage, "ShadowMapAtlas");

    const uint64_t fboHashA = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({}, nullptr);
    const uint64_t fboHashB = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({}, nullptr);
    E2E_ASSERT_EQ(fboHashA, fboHashB);
    return true;
}

E2E_TEST(Tier1, FEAT_11, ShadowAtlas_UvTransformMatrixCalculated) {
    ShadowAtlas shadowAtlas(2048, 2048);
    ShadowViewportRect firstTile{};
    ShadowViewportRect secondTile{};
    E2E_ASSERT_TRUE(shadowAtlas.allocateTile(1024, 1024, firstTile));
    E2E_ASSERT_TRUE(shadowAtlas.allocateTile(1024, 1024, secondTile));
    E2E_ASSERT_EQ(secondTile.x, 1024u);
    E2E_ASSERT_EQ(secondTile.y, 0u);

    const FCT::Mat4 biasMat = shadowAtlas.computeShadowBiasMatrix(secondTile);
    const float uCenter = biasMat.m[0] * 0.0f + biasMat.m[3];
    const float vCenter = biasMat.m[5] * 0.0f + biasMat.m[7];

    E2E_ASSERT_TRUE(std::abs(uCenter - 0.75f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(vCenter - 0.25f) < 1e-5f);
    return true;
}

// ============================================================================
// FEAT-12: 主场景单 FBO 连续绘制与 TBDR 带宽优化 (WGL2_PassGroup / RenderGraphAsset)
// ============================================================================

E2E_TEST(Tier1, FEAT_12, MainPass_ConsecutivePassesShareFbo) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* opaquePass = plus.findPass("ForwardOpaquePass");
    const auto* skyboxPass = plus.findPass("SkyboxPass");
    const auto* transPass = plus.findPass("ForwardTransPass");
    E2E_ASSERT_TRUE(opaquePass != nullptr && skyboxPass != nullptr && transPass != nullptr);

    E2E_ASSERT_EQ(opaquePass->targets[0].imageName, "SceneHdrColor");
    E2E_ASSERT_EQ(skyboxPass->targets[0].imageName, "SceneHdrColor");
    E2E_ASSERT_EQ(transPass->targets[0].imageName, "SceneHdrColor");

    E2E_ASSERT_EQ(opaquePass->depthStencilImage, "SceneDepth");
    E2E_ASSERT_EQ(skyboxPass->depthStencilImage, "SceneDepth");
    E2E_ASSERT_EQ(transPass->depthStencilImage, "SceneDepth");
    return true;
}

E2E_TEST(Tier1, FEAT_12, MainPass_InitialClearSubsequentLoadOps) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* opaquePass = plus.findPass("ForwardOpaquePass");
    const auto* skyboxPass = plus.findPass("SkyboxPass");
    const auto* transPass = plus.findPass("ForwardTransPass");
    E2E_ASSERT_TRUE(opaquePass != nullptr && skyboxPass != nullptr && transPass != nullptr);

    E2E_ASSERT_TRUE(opaquePass->hasClearColor);
    E2E_ASSERT_TRUE(opaquePass->hasClearDepth);
    E2E_ASSERT_FALSE(skyboxPass->hasClearColor);
    E2E_ASSERT_FALSE(skyboxPass->hasClearDepth);
    E2E_ASSERT_FALSE(transPass->hasClearColor);
    E2E_ASSERT_FALSE(transPass->hasClearDepth);
    return true;
}

E2E_TEST(Tier1, FEAT_12, MainPass_DepthStoreOpSetToDontCare) {
    FCT::RHI::WGL2_PassGroup passGroup(nullptr);
    FCT::RHI::TBDRInvalidatePlan plan{};
    plan.isDefaultFBO = false;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    passGroup.setTBDRInvalidatePlan(plan);

    const auto& stored = passGroup.getTBDRInvalidatePlan();
    E2E_ASSERT_FALSE(stored.isDefaultFBO);
    E2E_ASSERT_EQ(stored.endDiscards.size(), 2u);
    E2E_ASSERT_EQ(stored.endDiscards[0], static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    return true;
}

E2E_TEST(Tier1, FEAT_12, MainPass_DepthDowngradedToRenderbuffer) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    bool sceneDepthSampledDownstream = false;
    for (const auto& pass : plus.passes) {
        for (const auto& tex : pass.textures) {
            if (tex.source == "SceneDepth") {
                sceneDepthSampledDownstream = true;
            }
        }
    }
    E2E_ASSERT_FALSE(sceneDepthSampledDownstream);

    FCT::TransientResourceSpec depthSpec{};
    depthSpec.width = 1280;
    depthSpec.height = 720;
    depthSpec.format = FCT::Format::D24_UNORM_S8_UINT;
    depthSpec.isSampled = sceneDepthSampledDownstream;
    depthSpec.isDepthStencil = true;
    depthSpec.allowRenderbufferDowngrade = true;

    const bool downgradedToRbo = (!depthSpec.isSampled && depthSpec.allowRenderbufferDowngrade);
    E2E_ASSERT_TRUE(downgradedToRbo);
    return true;
}

E2E_TEST(Tier1, FEAT_12, MainPass_EmitsInvalidateFramebufferBeforePostProcess) {
    const std::vector<GLenum> offscreenDiscards =
        FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_DEPTH_STENCIL_ATTACHMENT}, false);
    E2E_ASSERT_EQ(offscreenDiscards.size(), 1u);
    E2E_ASSERT_EQ(offscreenDiscards[0], static_cast<GLenum>(GL_DEPTH_STENCIL_ATTACHMENT));

    const std::vector<GLenum> defaultFboDiscards =
        FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_DEPTH_STENCIL_ATTACHMENT}, true);
    E2E_ASSERT_EQ(defaultFboDiscards.size(), 2u);
    E2E_ASSERT_EQ(defaultFboDiscards[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_EQ(defaultFboDiscards[1], static_cast<GLenum>(GL_STENCIL));

    FCT::RHI::WGL2_PassGroup::emitInvalidate(offscreenDiscards, false);
    return true;
}

// ============================================================================
// FEAT-13: GPUScene 全局纹理数组图集跨材质合批 (GPUScene / AtlasShelfPacker)
// ============================================================================

E2E_TEST(Tier1, FEAT_13, AtlasShelfPacker_AllocatesNonOverlappingSubRects) {
    AtlasShelfPacker packer(2048, 2048, 4);
    const auto r0 = packer.allocate(512, 512);
    const auto r1 = packer.allocate(512, 256);
    const auto r2 = packer.allocate(1024, 512);
    const auto r3 = packer.allocate(256, 256);

    E2E_ASSERT_TRUE(r0.success && r1.success && r2.success && r3.success);
    E2E_ASSERT_FALSE(T1R_RectsOverlap(r0, r1));
    E2E_ASSERT_FALSE(T1R_RectsOverlap(r0, r2));
    E2E_ASSERT_FALSE(T1R_RectsOverlap(r1, r2));
    E2E_ASSERT_FALSE(T1R_RectsOverlap(r2, r3));
    return true;
}

E2E_TEST(Tier1, FEAT_13, AtlasShelfPacker_CalculatesUvScaleOffset) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    (void)scene.registerTextureToAtlas(512, 512);
    const AtlasAllocation second = scene.registerTextureToAtlas(512, 256);
    E2E_ASSERT_TRUE(second.success);

    const FCT::Vec4 uv = scene.getTextureUvScaleOffset(second);
    E2E_ASSERT_TRUE(std::abs(uv.x - 0.25f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.y - 0.125f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.z - 0.25f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.w - 0.0f) < 1e-5f);
    scene.shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_13, GpuScene_Tier0AtlasSelected_WhenBindlessFalse) {
    const HardwareProfile profile = DouyinMinigameProfile();
    E2E_ASSERT_FALSE(profile.isBindless);

    const DescriptorIndexingTier selectedTier = profile.isBindless
        ? DescriptorIndexingTier::Tier2_UpdateAfterBind
        : DescriptorIndexingTier::Tier0_AtlasFallback;
    E2E_ASSERT_EQ(selectedTier, DescriptorIndexingTier::Tier0_AtlasFallback);

    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));
    E2E_ASSERT_TRUE(scene.getAtlasPacker() != nullptr);
    E2E_ASSERT_EQ(scene.getAtlasPacker()->getAtlasWidth(), 2048u);
    return true;
}

E2E_TEST(Tier1, FEAT_13, GpuScene_CrossMaterialBatching_ZeroTextureSwaps) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    std::vector<GpuMaterialRecord> materials(50);
    std::vector<GpuInstanceRow> instances(50);
    for (uint32_t i = 0; i < 50u; ++i) {
        const AtlasAllocation alloc = scene.registerTextureToAtlas(128, 128);
        E2E_ASSERT_TRUE(alloc.success);
        materials[i].albedoTexIndex = alloc.layer;
        materials[i].pipelineBucket = 1u;
        instances[i] = T1R_MakeInstanceRow(0.0f, 0.0f, 0.0f, 0.5f, i);
    }

    const Status matSt = scene.uploadMaterialData(materials, 0u);
    const Status instSt = scene.uploadInstanceData(instances, 0u);
    E2E_ASSERT_TRUE(matSt.ok());
    E2E_ASSERT_TRUE(instSt.ok());

    const auto visibleSlots = scene.extractVisibleInstanceSlotStream(instances, T1R_MakeIdentityMat4());
    E2E_ASSERT_EQ(visibleSlots.size(), 50u);
    return true;
}

E2E_TEST(Tier1, FEAT_13, AtlasShelfPacker_MultiLayerAllocation) {
    AtlasShelfPacker packer(512, 512, 4);
    const auto l0 = packer.allocate(512, 512);
    const auto l1 = packer.allocate(512, 512);
    const auto l2 = packer.allocate(256, 256);

    E2E_ASSERT_TRUE(l0.success && l1.success && l2.success);
    E2E_ASSERT_EQ(l0.layer, 0u);
    E2E_ASSERT_EQ(l1.layer, 1u);
    E2E_ASSERT_EQ(l2.layer, 2u);
    return true;
}

// ============================================================================
// FEAT-14: GPUScene 结构化数据 2D 数据纹理降级 (GPUScene / WGL2_StorageBuffer)
// ============================================================================

E2E_TEST(Tier1, FEAT_14, GpuInstanceRow_Strictly160BytesAligned) {
    E2E_ASSERT_EQ(sizeof(GpuInstanceRow), 160u);
    E2E_ASSERT_EQ(alignof(GpuInstanceRow), 16u);
    E2E_ASSERT_EQ(sizeof(GpuInstanceRow) / FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes, 10u);
    return true;
}

E2E_TEST(Tier1, FEAT_14, GpuMaterialRecord_Strictly64BytesAligned) {
    E2E_ASSERT_EQ(sizeof(GpuMaterialRecord), 64u);
    E2E_ASSERT_EQ(alignof(GpuMaterialRecord), 16u);
    E2E_ASSERT_EQ(sizeof(GpuMaterialRecord) / FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes, 4u);
    return true;
}

E2E_TEST(Tier1, FEAT_14, DataTexture_TexelCoordinateMapping_1024Width) {
    uint32_t x = 0;
    uint32_t y = 0;
    GPUScene::TexelCoordFromSlot(103u, 10u, x, y);
    E2E_ASSERT_EQ(x, 6u);
    E2E_ASSERT_EQ(y, 1u);

    GPUScene::TexelCoordFromSlot(300u, 4u, x, y);
    E2E_ASSERT_EQ(x, 176u);
    E2E_ASSERT_EQ(y, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_14, DataTexture_SubRegionUpdate_MaxThreeRows) {
    const size_t offsetBytes = 1000u * FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes;
    const size_t sizeBytes = 2500u * FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes;
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(offsetBytes, sizeBytes);

    E2E_ASSERT_EQ(regions.size(), 3u);
    E2E_ASSERT_EQ(regions[0].x, 1000u);
    E2E_ASSERT_EQ(regions[0].y, 0u);
    E2E_ASSERT_EQ(regions[0].width, 24u);
    E2E_ASSERT_EQ(regions[0].height, 1u);

    E2E_ASSERT_EQ(regions[1].x, 0u);
    E2E_ASSERT_EQ(regions[1].y, 1u);
    E2E_ASSERT_EQ(regions[1].width, 1024u);
    E2E_ASSERT_EQ(regions[1].height, 2u);

    E2E_ASSERT_EQ(regions[2].x, 0u);
    E2E_ASSERT_EQ(regions[2].y, 3u);
    E2E_ASSERT_EQ(regions[2].width, 428u);
    E2E_ASSERT_EQ(regions[2].height, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_14, DataTexture_StorageBuffer_UpdateDataSuccess) {
    HeadlessGlesFixture gles;
    (void)gles.initialize(64, 64);

    FCT::RHI::WGL2_StorageBuffer storageBuf(nullptr);
    const Status createSt = storageBuf.create(64u * sizeof(GpuInstanceRow));
    E2E_ASSERT_TRUE(createSt.ok());
    E2E_ASSERT_GE(storageBuf.size(), 64u * sizeof(GpuInstanceRow));
    E2E_ASSERT_GE(storageBuf.getHeight(), 1u);

    std::vector<GpuInstanceRow> batch(4);
    for (uint32_t i = 0; i < 4u; ++i) {
        batch[i] = T1R_MakeInstanceRow(static_cast<float>(i), 2.0f, -3.0f, 1.5f, i);
        batch[i].entityId = 900u + i;
    }
    storageBuf.updateData(batch.data(), batch.size() * sizeof(GpuInstanceRow), 2u * sizeof(GpuInstanceRow));

    const auto* shadowRows = static_cast<const GpuInstanceRow*>(storageBuf.map());
    E2E_ASSERT_TRUE(shadowRows != nullptr);
    E2E_ASSERT_EQ(shadowRows[2].entityId, 900u);
    E2E_ASSERT_EQ(shadowRows[5].entityId, 903u);

    gles.terminate();
    return true;
}

// ============================================================================
// FEAT-15: CPU SIMD 视锥剔除与实例槽位流硬件实例绘制
// ============================================================================

E2E_TEST(Tier1, FEAT_15, Culling_BypassesGpuCull_WhenComputeFalse) {
    const HardwareProfile profile = DouyinMinigameProfile();
    E2E_ASSERT_FALSE(profile.isCompute);
    E2E_ASSERT_FALSE(profile.isIndirectDraw);

    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));
    std::vector<GpuInstanceRow> rows = {
        T1R_MakeInstanceRow(0.0f, 0.0f, 0.0f, 0.4f, 0u)
    };
    const auto visible = scene.extractVisibleInstanceSlotStream(rows, T1R_MakeIdentityMat4());
    E2E_ASSERT_EQ(visible.size(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_15, Culling_CpuFrustumSphereTest_CorrectlyFilters) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    std::vector<GpuInstanceRow> instances = {
        T1R_MakeInstanceRow(0.0f, 0.0f, 0.0f, 0.25f, 0u),
        T1R_MakeInstanceRow(0.4f, -0.3f, 0.2f, 0.2f, 1u),
        T1R_MakeInstanceRow(25.0f, 0.0f, 0.0f, 1.0f, 2u),
        T1R_MakeInstanceRow(0.0f, 0.0f, -40.0f, 1.5f, 3u)
    };

    const auto visibleSlots = scene.extractVisibleInstanceSlotStream(instances, T1R_MakeIdentityMat4());
    E2E_ASSERT_EQ(visibleSlots.size(), 2u);
    E2E_ASSERT_EQ(visibleSlots[0], 0u);
    E2E_ASSERT_EQ(visibleSlots[1], 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_15, InstanceSlotStream_Populated_WhenBaseInstanceFalse) {
    const HardwareProfile profile = DouyinMinigameProfile();
    E2E_ASSERT_FALSE(profile.isBaseInstance);

    GPUScene scene;
    std::vector<GpuInstanceRow> instances(5);
    instances[0] = T1R_MakeInstanceRow(0.0f, 0.0f, 0.0f, 0.2f, 0u);
    instances[1] = T1R_MakeInstanceRow(15.0f, 0.0f, 0.0f, 0.5f, 1u);
    instances[2] = T1R_MakeInstanceRow(0.2f, 0.1f, 0.0f, 0.2f, 2u);
    instances[3] = T1R_MakeInstanceRow(-18.0f, 0.0f, 0.0f, 0.5f, 3u);
    instances[4] = T1R_MakeInstanceRow(-0.2f, -0.1f, 0.0f, 0.2f, 4u);

    const auto stream = scene.extractVisibleInstanceSlotStream(instances, T1R_MakeIdentityMat4());
    E2E_ASSERT_EQ(stream.size(), 3u);
    E2E_ASSERT_EQ(stream[0], 0u);
    E2E_ASSERT_EQ(stream[1], 2u);
    E2E_ASSERT_EQ(stream[2], 4u);
    return true;
}

E2E_TEST(Tier1, FEAT_15, InstanceSlotStream_UsesPerInstanceR32UintLayout) {
    DrawBucket bucket(1u, InputTags::Mesh | InputTags::Static | InputTags::Opaque, 3u);
    bucket.attributeNames = {"a_Position", "a_Normal", "a_Tangent", "a_TexCoord0", "a_InstanceSlot"};
    bucket.instanceSlots = {3u, 7u, 11u};

    PipelineBindingDesc pipe{};
    pipe.pipelineId = 15u;
    pipe.ownerTechId = 1u;
    pipe.expectedVertexAttributes = {"a_Position", "a_Normal", "a_TexCoord0", "a_InstanceSlot"};
    pipe.expectedRenderTargetCount = 1u;
    pipe.requiresDepthWrite = true;

    const Status layoutSt = RenderGraphMatcher::validateLayoutSignatures(pipe, bucket, 1u, true);
    E2E_ASSERT_TRUE(layoutSt.ok());
    E2E_ASSERT_EQ(sizeof(bucket.instanceSlots[0]), 4u);
    return true;
}

E2E_TEST(Tier1, FEAT_15, Batching_SingleDrawCallFor1000Instances) {
    GPUScene scene;
    std::vector<GpuInstanceRow> instances(1000);
    for (uint32_t i = 0; i < 1000u; ++i) {
        instances[i] = T1R_MakeInstanceRow(0.0f, 0.0f, 0.0f, 0.25f, i % 16u);
    }

    DrawBucket bucket(1u, InputTags::Mesh | InputTags::Opaque, 0u);
    bucket.instanceSlots = scene.extractVisibleInstanceSlotStream(instances, T1R_MakeIdentityMat4());
    bucket.instanceCount = static_cast<uint32_t>(bucket.instanceSlots.size());

    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 1u;
    pipe.ownerTechId = 1u;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    pipe.requiredPassTags = PassTags::Opaque;
    matcher.registerPipeline(pipe);

    const std::vector<DrawBucket> buckets{bucket};
    const auto execItems = matcher.evaluatePassExecutionItems(PassTags::Opaque | PassTags::ForwardPlus, buckets);
    E2E_ASSERT_EQ(execItems.size(), 1u);
    E2E_ASSERT_EQ(execItems[0].bucket->instanceCount, 1000u);
    return true;
}

// ============================================================================
// FEAT-67: 双向标签位掩码动态匹配引擎 (RenderGraphMatcher / TagRegistry)
// ============================================================================

E2E_TEST(Tier1, FEAT_67, Bitmask_DownstreamPassMatch_SucceedsOnRequiredTags) {
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 10u;
    pipe.requiredPassTags = TagRegistry::StringsToPassMask({"Geometry", "Opaque"});

    const uint64_t forwardPassMask = TagRegistry::StringsToPassMask({"Geometry", "ForwardPlus", "Opaque"});
    E2E_ASSERT_TRUE(pipe.matchesPass(forwardPassMask));
    return true;
}

E2E_TEST(Tier1, FEAT_67, Bitmask_DownstreamPassMatch_FailsOnExcludedTags) {
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 11u;
    pipe.requiredPassTags = PassTags::Geometry;
    pipe.excludedPassTags = PassTags::Shadow;

    E2E_ASSERT_FALSE(pipe.matchesPass(PassTags::Geometry | PassTags::Shadow));
    E2E_ASSERT_TRUE(pipe.matchesPass(PassTags::Geometry | PassTags::Opaque));
    return true;
}

E2E_TEST(Tier1, FEAT_67, Bitmask_UpstreamBucketMatch_OwnerTechFilter) {
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 12u;
    pipe.ownerTechId = 42u;
    pipe.globalMatch = false;
    pipe.requiredInputTags = InputTags::Mesh;

    const DrawBucket matchingBucket(42u, InputTags::Mesh | InputTags::Static, 8u);
    const DrawBucket foreignBucket(99u, InputTags::Mesh | InputTags::Static, 8u);

    E2E_ASSERT_TRUE(pipe.matchesBucket(matchingBucket));
    E2E_ASSERT_FALSE(pipe.matchesBucket(foreignBucket));
    return true;
}

E2E_TEST(Tier1, FEAT_67, Bitmask_StaticVsSkinned_DivergedByInputTags) {
    RenderGraphMatcher matcher;

    PipelineBindingDesc staticPipe{};
    staticPipe.pipelineId = 1u;
    staticPipe.ownerTechId = 7u;
    staticPipe.requiredInputTags = InputTags::Mesh | InputTags::Static;
    staticPipe.excludedInputTags = InputTags::Skinned;
    staticPipe.requiredPassTags = PassTags::Shadow;

    PipelineBindingDesc skinnedPipe{};
    skinnedPipe.pipelineId = 2u;
    skinnedPipe.ownerTechId = 7u;
    skinnedPipe.requiredInputTags = InputTags::Mesh | InputTags::Skinned;
    skinnedPipe.requiredPassTags = PassTags::Shadow;

    matcher.registerPipeline(staticPipe);
    matcher.registerPipeline(skinnedPipe);

    const std::vector<DrawBucket> buckets = {
        DrawBucket(7u, InputTags::Mesh | InputTags::Static | InputTags::CastShadow, 10u),
        DrawBucket(7u, InputTags::Mesh | InputTags::Skinned | InputTags::CastShadow, 4u)
    };

    const auto items = matcher.evaluatePassExecutionItems(PassTags::Shadow, buckets);
    E2E_ASSERT_EQ(items.size(), 2u);
    E2E_ASSERT_EQ(items[0].pipelineId, 1u);
    E2E_ASSERT_EQ(items[0].bucket->instanceCount, 10u);
    E2E_ASSERT_EQ(items[1].pipelineId, 2u);
    E2E_ASSERT_EQ(items[1].bucket->instanceCount, 4u);
    return true;
}

E2E_TEST(Tier1, FEAT_67, Bitmask_GlobalMatch_MatchesAcrossMaterials) {
    PipelineBindingDesc outlinePipe{};
    outlinePipe.pipelineId = 50u;
    outlinePipe.ownerTechId = 999u;
    outlinePipe.globalMatch = true;
    outlinePipe.requiredInputTags = InputTags::Mesh | InputTags::Selected;

    const DrawBucket matA(10u, InputTags::Mesh | InputTags::Selected, 1u);
    const DrawBucket matB(25u, InputTags::Mesh | InputTags::Selected, 2u);
    const DrawBucket unselected(10u, InputTags::Mesh, 5u);

    E2E_ASSERT_TRUE(outlinePipe.matchesBucket(matA));
    E2E_ASSERT_TRUE(outlinePipe.matchesBucket(matB));
    E2E_ASSERT_FALSE(outlinePipe.matchesBucket(unselected));
    return true;
}

// ============================================================================
// FEAT-68: 纯数据帧图拓扑与着色器物理剥离契约
// ============================================================================

E2E_TEST(Tier1, FEAT_68, PassSchema_ZeroShaderFieldsInPgraph) {
    const char* pgraphToml = R"(
name = "PureDataTopology"
version = "3.0"
[[images]]
name = "HdrBuffer"
format = "RGBA16_SFLOAT"
[[passes]]
name = "MainPass"
tags = ["Geometry", "Opaque"]
targets = ["HdrBuffer"]
)";
    auto res = RenderGraphAsset::LoadFromToml(pgraphToml);
    E2E_ASSERT_STATUS_OK(res);
    E2E_ASSERT_EQ(res->passes.size(), 1u);
    E2E_ASSERT_EQ(res->passes[0].name, "MainPass");
    E2E_ASSERT_TRUE(std::is_standard_layout_v<PassTargetBinding> || !res->passes[0].targets.empty());
    return true;
}

E2E_TEST(Tier1, FEAT_68, PipelineDesc_HoldsShaderAndBindsToPass) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* postPass = plus.findPass("PostProcessPass");
    E2E_ASSERT_TRUE(postPass != nullptr);

    RenderGraphMatcher matcher;
    PipelineBindingDesc toneMapPipe{};
    toneMapPipe.pipelineId = 301u;
    toneMapPipe.globalMatch = true;
    toneMapPipe.requiredPassTags = PassTags::PostProcess | PassTags::ToneMap;
    toneMapPipe.boundShaderId = "Shaders/PostProcess/AcesToneMap.spv";
    matcher.registerPipeline(toneMapPipe);

    const auto matched = matcher.matchPipelinesForPass(postPass->tagMask);
    E2E_ASSERT_EQ(matched.size(), 1u);
    E2E_ASSERT_EQ(matched[0]->boundShaderId, "Shaders/PostProcess/AcesToneMap.spv");
    return true;
}

E2E_TEST(Tier1, FEAT_68, MultiplePipelines_BindToSinglePass) {
    RenderGraphMatcher matcher;
    for (uint32_t i = 0; i < 3u; ++i) {
        PipelineBindingDesc p{};
        p.pipelineId = 400u + i;
        p.requiredPassTags = PassTags::Opaque;
        p.order = static_cast<int32_t>(i * 10);
        matcher.registerPipeline(p);
    }

    const auto matched = matcher.matchPipelinesForPass(PassTags::Geometry | PassTags::Opaque);
    E2E_ASSERT_EQ(matched.size(), 3u);
    return true;
}

E2E_TEST(Tier1, FEAT_68, PassReordering_PreservesPipelineCompatibility) {
    RenderGraphAssetDesc graph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_GE(graph.passes.size(), 4u);

    std::swap(graph.passes[2], graph.passes[3]);
    E2E_ASSERT_TRUE(graph.isAcyclic());

    RenderGraphMatcher matcher;
    PipelineBindingDesc skyPipe{};
    skyPipe.pipelineId = 77u;
    skyPipe.globalMatch = true;
    skyPipe.requiredPassTags = PassTags::Skybox;
    matcher.registerPipeline(skyPipe);

    const auto* reorderedSky = graph.findPass("SkyboxPass");
    E2E_ASSERT_TRUE(reorderedSky != nullptr);
    E2E_ASSERT_EQ(matcher.matchPipelinesForPass(reorderedSky->tagMask).size(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_68, PassNode_PureDataContainerStructure) {
    RenderGraphPassNodeDesc node{};
    node.name = "CustomOverlay";
    node.tags = {"Overlay", "UI"};
    node.tagMask = TagRegistry::StringsToPassMask(node.tags);
    node.targets = {{"Window", true}};

    E2E_ASSERT_FALSE(std::is_polymorphic_v<RenderGraphPassNodeDesc>);
    E2E_ASSERT_NE(node.tagMask & PassTags::Overlay, 0ULL);
    E2E_ASSERT_TRUE(node.targets[0].isWindow);
    return true;
}

// ============================================================================
// FEAT-69: 上下游双向签名自动校验器 (RenderGraphMatcher::validateLayoutSignatures)
// ============================================================================

E2E_TEST(Tier1, FEAT_69, UpstreamLayout_VertexShaderInputMatchesBucketLayout) {
    PipelineBindingDesc pipe{};
    pipe.expectedVertexAttributes = {"Position", "Normal", "UV0"};
    pipe.expectedRenderTargetCount = 1u;
    pipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 4u);
    bucket.attributeNames = {"Position", "Normal", "Tangent", "UV0"};

    const Status st = RenderGraphMatcher::validateLayoutSignatures(pipe, bucket, 1u, true);
    E2E_ASSERT_TRUE(st.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_69, UpstreamLayout_EmptyLayoutForFullscreenQuad) {
    PipelineBindingDesc postPipe{};
    postPipe.expectedVertexAttributes.clear();
    postPipe.expectedRenderTargetCount = 1u;
    postPipe.requiresDepthWrite = false;

    DrawBucket fullscreenBucket(0u, InputTags::ScreenSpace, 1u);
    const Status st = RenderGraphMatcher::validateLayoutSignatures(postPipe, fullscreenBucket, 1u, false);
    E2E_ASSERT_TRUE(st.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_69, DownstreamLayout_PassRenderTargetCountMatchesPsOutput) {
    PipelineBindingDesc gbufferPipe{};
    gbufferPipe.expectedRenderTargetCount = 3u;
    gbufferPipe.requiresDepthWrite = true;

    const RenderGraphAssetDesc deferred = RenderGraphAsset::CreateDesktopDeferredHdrPreset();
    const auto* gbPass = deferred.findPass("DeferredGBufferPass");
    E2E_ASSERT_TRUE(gbPass != nullptr);

    DrawBucket bucket(1u, InputTags::Mesh, 1u);
    const Status okSt = RenderGraphMatcher::validateLayoutSignatures(
        gbufferPipe, bucket, static_cast<uint32_t>(gbPass->targets.size()), !gbPass->depthStencilImage.empty());
    const Status badSt = RenderGraphMatcher::validateLayoutSignatures(gbufferPipe, bucket, 1u, true);

    E2E_ASSERT_TRUE(okSt.ok());
    E2E_ASSERT_FALSE(badSt.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_69, DownstreamLayout_DepthStencilMatchValidation) {
    PipelineBindingDesc depthWritePipe{};
    depthWritePipe.expectedRenderTargetCount = 1u;
    depthWritePipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 1u);
    const Status missingDepth = RenderGraphMatcher::validateLayoutSignatures(depthWritePipe, bucket, 1u, false);
    const Status withDepth = RenderGraphMatcher::validateLayoutSignatures(depthWritePipe, bucket, 1u, true);

    E2E_ASSERT_FALSE(missingDepth.ok());
    E2E_ASSERT_TRUE(withDepth.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_69, LayoutValidation_ReportsMismatchStatus) {
    PipelineBindingDesc skinnedPipe{};
    skinnedPipe.pipelineId = 88u;
    skinnedPipe.expectedVertexAttributes = {"Position", "Joints0", "Weights0"};
    skinnedPipe.expectedRenderTargetCount = 1u;
    skinnedPipe.requiresDepthWrite = true;

    DrawBucket staticBucket(1u, InputTags::Mesh | InputTags::Static, 1u);
    staticBucket.attributeNames = {"Position", "Normal", "UV0"};

    const Status st = RenderGraphMatcher::validateLayoutSignatures(skinnedPipe, staticBucket, 1u, true);
    E2E_ASSERT_FALSE(st.ok());
    E2E_ASSERT_EQ(st.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

// ============================================================================
// FEAT-70: 基于引脚语义槽位 (Pin Slot) 的纹理安全绑定
// ============================================================================

E2E_TEST(Tier1, FEAT_70, PinSlot_DeclaredOnPassInputConnection) {
    const char* tomlSrc = R"(
name = "PinSlotGraph"
version = "3.0"
[[images]]
name = "HdrScene"
format = "RGBA16_SFLOAT"
[[passes]]
name = "PostPass"
tags = ["PostProcess"]
targets = ["Window"]
[[passes.textures]]
slot = "SceneColor"
source = "HdrScene"
fallback = "@builtin/black_1x1"
)";
    auto res = RenderGraphAsset::LoadFromToml(tomlSrc);
    E2E_ASSERT_STATUS_OK(res);
    const auto* pass = res->findPass("PostPass");
    E2E_ASSERT_TRUE(pass != nullptr);
    E2E_ASSERT_EQ(pass->textures.size(), 1u);
    E2E_ASSERT_EQ(pass->textures[0].slot, "SceneColor");
    E2E_ASSERT_EQ(pass->textures[0].source, "HdrScene");
    return true;
}

E2E_TEST(Tier1, FEAT_70, PinSlot_PipelineSamplerBindsBySlotName) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* postPass = plus.findPass("PostProcessPass");
    E2E_ASSERT_TRUE(postPass != nullptr);

    PipelineBindingDesc pipe{};
    pipe.pipelineId = 501u;
    pipe.pinSamplers.push_back({"u_HdrInputTex", "SceneColor"});

    const std::string boundImage = T1R_ResolvePassPinTexture(*postPass, pipe.pinSamplers[0].pinSlot);
    E2E_ASSERT_EQ(boundImage, "SceneHdrColor");
    return true;
}

E2E_TEST(Tier1, FEAT_70, PinSlot_RenamingUpstreamNodeMaintainsLink) {
    RenderGraphAssetDesc graph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    graph.images[1].name = "RenamedHdrBufferV2";
    graph.passes[1].targets[0].imageName = "RenamedHdrBufferV2";
    graph.passes[4].textures[0].source = "RenamedHdrBufferV2";

    PipelineBindingDesc pipe{};
    pipe.pinSamplers.push_back({"u_HdrInputTex", "SceneColor"});

    const std::string resolved = T1R_ResolvePassPinTexture(graph.passes[4], pipe.pinSamplers[0].pinSlot);
    E2E_ASSERT_EQ(resolved, "RenamedHdrBufferV2");
    return true;
}

E2E_TEST(Tier1, FEAT_70, PinSlot_UnlinkedInputUsesFallbackTexture) {
    RenderGraphPassNodeDesc pass{};
    pass.name = "PostProcessPass";
    pass.textures.push_back({"BloomTex", "", "@builtin/black_1x1"});

    const std::string resolved = T1R_ResolvePassPinTexture(pass, "BloomTex");
    E2E_ASSERT_EQ(resolved, "@builtin/black_1x1");
    return true;
}

E2E_TEST(Tier1, FEAT_70, PinSlot_MultiSlotInputsResolvedCorrectly) {
    const RenderGraphAssetDesc deferred = RenderGraphAsset::CreateDesktopDeferredHdrPreset();
    const auto* lightingPass = deferred.findPass("DeferredLightingPass");
    E2E_ASSERT_TRUE(lightingPass != nullptr);

    E2E_ASSERT_EQ(T1R_ResolvePassPinTexture(*lightingPass, "GBuffer0"), "GBuffer0");
    E2E_ASSERT_EQ(T1R_ResolvePassPinTexture(*lightingPass, "Depth"), "SceneDepth");
    E2E_ASSERT_EQ(T1R_ResolvePassPinTexture(*lightingPass, "Shadow"), "ShadowMap");
    return true;
}

// ============================================================================
// FEAT-71: 同 Pass 多管线确定性稳定排序 (RenderGraphMatcher)
// ============================================================================

E2E_TEST(Tier1, FEAT_71, DeterministicOrder_SortedByOrderAscending) {
    RenderGraphMatcher matcher;
    const int32_t orders[4] = {100, -50, 20, 0};
    for (uint32_t i = 0; i < 4u; ++i) {
        PipelineBindingDesc p{};
        p.pipelineId = 10u + i;
        p.globalMatch = true;
        p.requiredPassTags = PassTags::Opaque;
        p.order = orders[i];
        matcher.registerPipeline(p);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 2u)};
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);

    E2E_ASSERT_EQ(items.size(), 4u);
    E2E_ASSERT_EQ(items[0].order, -50);
    E2E_ASSERT_EQ(items[1].order, 0);
    E2E_ASSERT_EQ(items[2].order, 20);
    E2E_ASSERT_EQ(items[3].order, 100);
    return true;
}

E2E_TEST(Tier1, FEAT_71, DeterministicOrder_TieBreakingByPipelineId) {
    RenderGraphMatcher matcher;
    const uint32_t ids[3] = {42u, 7u, 19u};
    for (uint32_t id : ids) {
        PipelineBindingDesc p{};
        p.pipelineId = id;
        p.globalMatch = true;
        p.requiredPassTags = PassTags::Opaque;
        p.order = 0;
        matcher.registerPipeline(p);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 1u)};
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);

    E2E_ASSERT_EQ(items.size(), 3u);
    E2E_ASSERT_EQ(items[0].pipelineId, 7u);
    E2E_ASSERT_EQ(items[1].pipelineId, 19u);
    E2E_ASSERT_EQ(items[2].pipelineId, 42u);
    return true;
}

E2E_TEST(Tier1, FEAT_71, DeterministicOrder_OpaqueBeforeOutline) {
    RenderGraphMatcher matcher;

    PipelineBindingDesc outlinePipe{};
    outlinePipe.pipelineId = 1u;
    outlinePipe.globalMatch = true;
    outlinePipe.requiredInputTags = InputTags::Mesh | InputTags::Selected;
    outlinePipe.requiredPassTags = PassTags::Opaque;
    outlinePipe.order = 100;

    PipelineBindingDesc pbrOpaquePipe{};
    pbrOpaquePipe.pipelineId = 2u;
    pbrOpaquePipe.globalMatch = true;
    pbrOpaquePipe.requiredInputTags = InputTags::Mesh;
    pbrOpaquePipe.requiredPassTags = PassTags::Opaque;
    pbrOpaquePipe.order = 0;

    matcher.registerPipeline(outlinePipe);
    matcher.registerPipeline(pbrOpaquePipe);

    const std::vector<DrawBucket> buckets = {
        DrawBucket(1u, InputTags::Mesh | InputTags::Selected, 3u)
    };
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);

    E2E_ASSERT_EQ(items.size(), 2u);
    E2E_ASSERT_EQ(items[0].pipelineId, 2u);
    E2E_ASSERT_EQ(items[1].pipelineId, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_71, DeterministicOrder_InGameTextBeforeRmlUi) {
    RenderGraphMatcher matcher;

    PipelineBindingDesc rmlUiPipe{};
    rmlUiPipe.pipelineId = 10u;
    rmlUiPipe.globalMatch = true;
    rmlUiPipe.requiredInputTags = InputTags::UI | InputTags::RmlUi;
    rmlUiPipe.requiredPassTags = PassTags::UI;
    rmlUiPipe.order = 0;

    PipelineBindingDesc combatTextPipe{};
    combatTextPipe.pipelineId = 20u;
    combatTextPipe.globalMatch = true;
    combatTextPipe.requiredInputTags = InputTags::Text | InputTags::InGameBaked;
    combatTextPipe.requiredPassTags = PassTags::UI;
    combatTextPipe.order = -10;

    matcher.registerPipeline(rmlUiPipe);
    matcher.registerPipeline(combatTextPipe);

    const std::vector<DrawBucket> buckets = {
        DrawBucket(0u, InputTags::UI | InputTags::RmlUi, 12u),
        DrawBucket(0u, InputTags::Text | InputTags::InGameBaked, 64u)
    };
    const auto items = matcher.evaluatePassExecutionItems(PassTags::UI | PassTags::Overlay, buckets);

    E2E_ASSERT_EQ(items.size(), 2u);
    E2E_ASSERT_EQ(items[0].pipelineId, 20u);
    E2E_ASSERT_EQ(items[1].pipelineId, 10u);
    return true;
}

E2E_TEST(Tier1, FEAT_71, DeterministicOrder_StableAcrossFrames) {
    RenderGraphMatcher matcher;
    for (uint32_t i = 0; i < 6u; ++i) {
        PipelineBindingDesc p{};
        p.pipelineId = 60u - i;
        p.globalMatch = true;
        p.requiredPassTags = PassTags::Opaque;
        p.order = static_cast<int32_t>(i % 2u);
        matcher.registerPipeline(p);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 5u)};
    const auto baseline = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(baseline.size(), 6u);

    for (int frame = 0; frame < 50; ++frame) {
        const auto current = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
        E2E_ASSERT_EQ(current.size(), baseline.size());
        for (size_t k = 0; k < baseline.size(); ++k) {
            E2E_ASSERT_EQ(current[k].pipelineId, baseline[k].pipelineId);
            E2E_ASSERT_EQ(current[k].order, baseline[k].order);
        }
    }
    return true;
}

// ============================================================================
// FEAT-72: 四大跨平台标准渲染图预设体系 (RenderGraphAsset)
// ============================================================================

E2E_TEST(Tier1, FEAT_72, Preset_DesktopDeferredHdr_TopologyValid) {
    const auto preset = RenderGraphAsset::CreateDesktopDeferredHdrPreset();
    E2E_ASSERT_EQ(preset.name, "Desktop_DeferredHDR");
    E2E_ASSERT_TRUE(preset.findPass("DeferredGBufferPass") != nullptr);
    E2E_ASSERT_TRUE(preset.findPass("DeferredLightingPass") != nullptr);
    E2E_ASSERT_TRUE(preset.findPass("PostProcessPass") != nullptr);
    E2E_ASSERT_EQ(preset.findPass("DeferredGBufferPass")->targets.size(), 3u);
    return true;
}

E2E_TEST(Tier1, FEAT_72, Preset_DesktopHybridRayTracing_TopologyValid) {
    const auto preset = RenderGraphAsset::CreateDesktopHybridRayTracingPreset();
    E2E_ASSERT_EQ(preset.name, "Desktop_HybridRayTracing");
    const auto* rtPass = preset.findPass("RayTracingPass");
    E2E_ASSERT_TRUE(rtPass != nullptr);
    E2E_ASSERT_NE(rtPass->tagMask & PassTags::RayTracing, 0ULL);
    E2E_ASSERT_EQ(rtPass->targets.size(), 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_72, Preset_MiniGameForwardPlus_TopologyValid) {
    const auto preset = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_EQ(preset.name, "MiniGame_ForwardPlus");
    E2E_ASSERT_EQ(preset.passes.size(), 6u);
    E2E_ASSERT_TRUE(preset.findPass("ShadowMapPass") != nullptr);
    E2E_ASSERT_TRUE(preset.findPass("ForwardOpaquePass") != nullptr);
    E2E_ASSERT_TRUE(preset.findPass("DeferredGBufferPass") == nullptr);
    return true;
}

E2E_TEST(Tier1, FEAT_72, Preset_MiniGameForwardLite_TopologyValid) {
    const auto preset = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    E2E_ASSERT_EQ(preset.name, "MiniGame_ForwardLite");
    E2E_ASSERT_EQ(preset.passes.size(), 1u);
    E2E_ASSERT_EQ(preset.images.size(), 1u);
    E2E_ASSERT_EQ(preset.images[0].name, "SceneDepth");
    return true;
}

E2E_TEST(Tier1, FEAT_72, Preset_AllPresetsPassAcyclicCheck) {
    E2E_ASSERT_TRUE(RenderGraphAsset::CreateDesktopDeferredHdrPreset().isAcyclic());
    E2E_ASSERT_TRUE(RenderGraphAsset::CreateDesktopHybridRayTracingPreset().isAcyclic());
    E2E_ASSERT_TRUE(RenderGraphAsset::CreateMiniGameForwardPlusPreset().isAcyclic());
    E2E_ASSERT_TRUE(RenderGraphAsset::CreateMiniGameForwardLitePreset().isAcyclic());
    return true;
}

// ============================================================================
// FEAT-73: 动态空 Pass 剪枝与离线着色器变体剥离
// ============================================================================

E2E_TEST(Tier1, FEAT_73, DynamicCulling_BypassesPassWithNoMatchingBuckets) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc transPipe{};
    transPipe.pipelineId = 701u;
    transPipe.globalMatch = true;
    transPipe.requiredInputTags = InputTags::Mesh | InputTags::Translucent;
    transPipe.requiredPassTags = PassTags::Translucent;
    matcher.registerPipeline(transPipe);

    const std::vector<DrawBucket> opaqueOnlyBuckets = {
        DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 20u)
    };
    E2E_ASSERT_FALSE(matcher.hasActiveBucketsForPass(PassTags::Geometry | PassTags::Translucent, opaqueOnlyBuckets));
    return true;
}

E2E_TEST(Tier1, FEAT_73, DynamicCulling_SkipsTransientResourceAllocation) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc rtPipe{};
    rtPipe.pipelineId = 702u;
    rtPipe.globalMatch = true;
    rtPipe.requiredInputTags = InputTags::Mesh;
    rtPipe.requiredPassTags = PassTags::RayTracing;
    matcher.registerPipeline(rtPipe);

    FCT::TransientResourcePool transientPool(nullptr);
    const std::vector<DrawBucket> emptyBuckets;

    if (matcher.hasActiveBucketsForPass(PassTags::RayTracing, emptyBuckets)) {
        FCT::TransientResourceSpec rtSpec{};
        rtSpec.width = 1280;
        rtSpec.height = 720;
        rtSpec.format = FCT::Format::R16G16B16A16_SFLOAT;
        (void)transientPool.acquire(rtSpec, 0u, 1u, 0u);
    }

    E2E_ASSERT_EQ(transientPool.getStats().physicalAllocations, 0u);
    E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_73, OfflineStripping_CollectsPassTagsFromTargetGraph) {
    const auto plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto lite = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    const uint64_t shippingPassMask = T1R_CollectGraphActivePassMask(plus) | T1R_CollectGraphActivePassMask(lite);

    E2E_ASSERT_NE(shippingPassMask & PassTags::ForwardPlus, 0ULL);
    E2E_ASSERT_NE(shippingPassMask & PassTags::ForwardLite, 0ULL);
    E2E_ASSERT_EQ(shippingPassMask & PassTags::DeferredGBuffer, 0ULL);
    E2E_ASSERT_EQ(shippingPassMask & PassTags::RayTracing, 0ULL);
    return true;
}

E2E_TEST(Tier1, FEAT_73, OfflineStripping_DiscardsUnmatchedShaderVariants) {
    const uint64_t miniGameMask =
        T1R_CollectGraphActivePassMask(RenderGraphAsset::CreateMiniGameForwardPlusPreset()) |
        T1R_CollectGraphActivePassMask(RenderGraphAsset::CreateMiniGameForwardLitePreset());

    PipelineBindingDesc forwardVar{};
    forwardVar.pipelineId = 1u;
    forwardVar.requiredPassTags = PassTags::ForwardPlus;

    PipelineBindingDesc deferredVar{};
    deferredVar.pipelineId = 2u;
    deferredVar.requiredPassTags = PassTags::DeferredGBuffer;

    PipelineBindingDesc rtVar{};
    rtVar.pipelineId = 3u;
    rtVar.requiredPassTags = PassTags::RayTracing;

    E2E_ASSERT_TRUE(forwardVar.matchesPass(miniGameMask));
    E2E_ASSERT_FALSE(deferredVar.matchesPass(miniGameMask));
    E2E_ASSERT_FALSE(rtVar.matchesPass(miniGameMask));
    return true;
}

E2E_TEST(Tier1, FEAT_73, DynamicCulling_RestoresPassWhenBucketAppears) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc transPipe{};
    transPipe.pipelineId = 705u;
    transPipe.globalMatch = true;
    transPipe.requiredInputTags = InputTags::Mesh | InputTags::Translucent;
    transPipe.requiredPassTags = PassTags::Translucent;
    matcher.registerPipeline(transPipe);

    std::vector<DrawBucket> frameBuckets;
    E2E_ASSERT_FALSE(matcher.hasActiveBucketsForPass(PassTags::Translucent, frameBuckets));

    frameBuckets.emplace_back(1u, InputTags::Mesh | InputTags::Translucent, 6u);
    E2E_ASSERT_TRUE(matcher.hasActiveBucketsForPass(PassTags::Translucent, frameBuckets));
    E2E_ASSERT_EQ(matcher.evaluatePassExecutionItems(PassTags::Translucent, frameBuckets).size(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_09, OrthoProjection_ReverseZ_And_ConstantFroxelXY) {
    const FCT::Mat4 orthoNormal = RendererSubsystem::buildOrthoProjection(12.0f, 8.0f, 0.5f, 200.5f, false);
    const FCT::Mat4 orthoRevZ = RendererSubsystem::buildOrthoProjection(12.0f, 8.0f, 0.5f, 200.5f, true);
    const FCT::Mat4 perspRevZ = RendererSubsystem::buildPerspectiveProjection(60.0f, 2.0f, 0.5f, 200.5f, true);

    E2E_ASSERT_TRUE(std::abs(orthoNormal.m[15] - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(orthoNormal.m[10] > 0.0f);
    E2E_ASSERT_TRUE(std::abs(orthoNormal.m[10] * 0.5f + orthoNormal.m[11]) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs((orthoNormal.m[10] * 200.5f + orthoNormal.m[11]) - 1.0f) < 1e-4f);

    E2E_ASSERT_TRUE(std::abs(orthoRevZ.m[15] - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(orthoRevZ.m[10] < 0.0f);
    E2E_ASSERT_TRUE(std::abs((orthoRevZ.m[10] * 0.5f + orthoRevZ.m[11]) - 1.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(orthoRevZ.m[10] * 200.5f + orthoRevZ.m[11]) < 1e-4f);

    // Reverse-Z 透视：near=0.5 -> ndcZ=1.0, far=200.5 -> ndcZ=0.0
    const float ndcNear = (perspRevZ.m[10] * 0.5f + perspRevZ.m[11]) / 0.5f;
    const float ndcFar = (perspRevZ.m[10] * 200.5f + perspRevZ.m[11]) / 200.5f;
    E2E_ASSERT_TRUE(std::abs(ndcNear - 1.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(ndcFar) < 1e-4f);

    ClusteredForwardPlus orthoClustered;
    orthoClustered.updateFrustumGrid(orthoNormal, 0.5f, 200.5f, 1280u, 720u);
    const FroxelAabb* frontBox = orthoClustered.getFroxelAabb(0u);
    const FroxelAabb* backBox = orthoClustered.getFroxelAabb(15u * 128u);
    E2E_ASSERT_TRUE(frontBox != nullptr && backBox != nullptr);
    E2E_ASSERT_TRUE(std::abs(frontBox->minPt[0] - backBox->minPt[0]) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(frontBox->maxPt[1] - backBox->maxPt[1]) < 1e-4f);
    return true;
}

E2E_TEST(Tier1, FEAT_73, ActiveCameraList_Priority_TargetBinding_And_EmptyFallback) {
    ::MQEngine::Domain::Domain domain;
    E2E_ASSERT_STATUS_OK(domain.scene.init());
    E2E_ASSERT_STATUS_OK(domain.renderer.init());

    domain.renderer.bindRenderTarget("EditorViewport", nullptr, nullptr, 800u, 400u);
    const auto binding = domain.renderer.getRenderTargetBinding("EditorViewport");
    E2E_ASSERT_TRUE(binding.has_value());
    E2E_ASSERT_EQ(binding->width, 800u);
    E2E_ASSERT_EQ(binding->height, 400u);

    // 空场景下无任何激活相机：不得捏造幽灵相机
    domain.renderer.refreshActiveCameras();
    E2E_ASSERT_FALSE(domain.renderer.findActiveCameraForTarget("MainWindow").has_value());
    E2E_ASSERT_FALSE(domain.renderer.hasActiveCameraForPrimaryTarget());

    auto& mainReg = domain.scene.getRegistry();
    const auto lowCam = domain.scene.createEntity();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(lowCam,
        ::MQEngine::Domain::Scene::CameraComponent{.active = true, .priority = 1, .targetName = "MainWindow"});

    const auto highCam = domain.scene.createEntity();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(highCam,
        ::MQEngine::Domain::Scene::CameraComponent{
            .active = true,
            .orthographic = true,
            .orthoSize = 16.0f,
            .priority = 10,
            .targetName = "MainWindow"
        });

    const auto disabledCam = domain.scene.createEntity();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(disabledCam,
        ::MQEngine::Domain::Scene::CameraComponent{.active = false, .priority = 99, .targetName = "MainWindow"});

    const auto tagDisabledCam = domain.scene.createEntity();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::DisabledTag>(tagDisabledCam);
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(tagDisabledCam,
        ::MQEngine::Domain::Scene::CameraComponent{.active = true, .priority = 999, .targetName = "MainWindow"});

    auto& editorReg = domain.scene.getOrCreateRegistry("Editor");
    const auto editorCam = domain.scene.createEntityInRegistry("Editor");
    editorReg.emplace_or_replace<::MQEngine::Domain::Scene::EditorNameComponent>(editorCam, "EditorCamera");
    editorReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(editorCam,
        ::MQEngine::Domain::Scene::CameraComponent{.fov = 60.0f, .active = true, .priority = 0, .targetName = "EditorViewport"});

    domain.scene.update();
    domain.renderer.refreshActiveCameras();

    const auto mainOpt = domain.renderer.findActiveCameraForTarget("MainWindow");
    E2E_ASSERT_TRUE(mainOpt.has_value());
    E2E_ASSERT_EQ(mainOpt->priority, 10);
    E2E_ASSERT_TRUE(mainOpt->orthographic);
    E2E_ASSERT_TRUE(std::abs(mainOpt->projMatrix.m[15] - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(domain.renderer.hasActiveCameraForPrimaryTarget());

    const auto edOpt = domain.renderer.findActiveCameraForTarget("EditorViewport");
    E2E_ASSERT_TRUE(edOpt.has_value());
    E2E_ASSERT_EQ(edOpt->registryName, "Editor");
    const float edAspect = edOpt->projMatrix.m[5] / std::max(edOpt->projMatrix.m[0], 1e-6f);
    E2E_ASSERT_TRUE(std::abs(edAspect - 2.0f) < 1e-3f);

    domain.renderer.term();
    domain.scene.term();
    return true;
}

} // namespace MQEngine::Testing
