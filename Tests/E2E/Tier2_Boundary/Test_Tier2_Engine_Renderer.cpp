/**
 * @file Test_Tier2_Engine_Renderer.cpp
 * @brief Tier 2 边界极限、空载与异常降级测试：Engine 渲染图谱、3D Clustered 光照、ShadowAtlas 与 GPUScene
 *        覆盖特性：FEAT-09..15 (7项) + FEAT-67..73 (7项) = 14 项特性 × 5 用例 = 70 个用例
 */

#include "Common/E2ECommon.h"

#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
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
#include <limits>
#include <type_traits>
#include <vector>

namespace MQEngine::Testing {

namespace {

using namespace ::MQEngine::Domain::Renderer;

FCT::Mat4 T2R_MakeIdentityMat4() noexcept {
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

FCT::Mat4 T2R_MakePerspectiveMat4(float fovYRad, float aspect, float nearZ, float farZ) noexcept {
    FCT::Mat4 mat{};
    for (int i = 0; i < 16; ++i) {
        mat.m[i] = 0.0f;
    }
    const float invTan = 1.0f / std::tan(fovYRad * 0.5f);
    mat.m[0] = invTan / aspect;
    mat.m[5] = invTan;
    mat.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    mat.m[11] = -1.0f;
    mat.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return mat;
}

GpuInstanceRow T2R_MakeBoundedInstance(float cx, float cy, float cz, float radius, uint32_t materialSlot) noexcept {
    GpuInstanceRow row{};
    row.modelMatrix = T2R_MakeIdentityMat4();
    row.invTransposeMatrix = T2R_MakeIdentityMat4();
    row.worldSphereBounds = FCT::Vec4{cx, cy, cz, radius};
    row.meshSlot = 0u;
    row.materialSlot = materialSlot;
    row.entityId = materialSlot + 1u;
    row.flags = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(cz) && std::isfinite(radius) && radius > 0.0f) ? 1u : 0u;
    return row;
}

StatusOr<RenderGraphAssetDesc> T2R_ResolvePresetByName(std::string_view presetName) {
    if (presetName == "Desktop_DeferredHDR") {
        return RenderGraphAsset::CreateDesktopDeferredHdrPreset();
    }
    if (presetName == "Desktop_HybridRayTracing") {
        return RenderGraphAsset::CreateDesktopHybridRayTracingPreset();
    }
    if (presetName == "MiniGame_ForwardPlus") {
        return RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    }
    if (presetName == "MiniGame_ForwardLite") {
        return RenderGraphAsset::CreateMiniGameForwardLitePreset();
    }
    return absl::NotFoundError(absl::StrCat("未知渲染图预设: ", presetName));
}

uint32_t T2R_ClampAtlasLayerSlot(uint32_t requestedSlot, uint32_t maxLayers) noexcept {
    if (maxLayers == 0u || requestedSlot >= maxLayers) {
        return 0u;
    }
    return requestedSlot;
}

Status T2R_UploadInstanceSlotRange(GPUScene& scene, std::span<const GpuInstanceRow> allRows, uint32_t firstSlot, uint32_t lastSlotInclusive) {
    if (firstSlot > lastSlotInclusive || lastSlotInclusive >= allRows.size()) {
        return absl::InvalidArgumentError("实例槽位区间反转或越界");
    }
    const size_t count = static_cast<size_t>(lastSlotInclusive - firstSlot + 1u);
    return scene.uploadInstanceData(allRows.subspan(firstSlot, count), firstSlot);
}

} // namespace

// ============================================================================
// FEAT-09: 3D Clustered 光照边界与异常退化验证
// ============================================================================

E2E_TEST(Tier2, FEAT_09, Clustered_ZeroLights_DegradeToSingleCluster) {
    ClusteredForwardPlus clustered;
    const FCT::Mat4 proj = T2R_MakePerspectiveMat4(1.04719755f, 16.0f / 9.0f, 0.1f, 200.0f);
    clustered.updateFrustumGrid(proj, 0.1f, 200.0f, 1280, 720);

    const std::vector<ClusterLightRecord> emptyLights;
    clustered.cullLights(emptyLights, T2R_MakeIdentityMat4());

    E2E_ASSERT_TRUE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getActiveLightCount(), 0u);
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 1u);
    E2E_ASSERT_EQ(clustered.getLightIndexListBuffer().size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_09, Clustered_MaxLights_FroxelListTruncationAt32) {
    ClusteredForwardPlus clustered;
    const FCT::Mat4 proj = T2R_MakePerspectiveMat4(1.04719755f, 1.0f, 0.5f, 100.0f);
    clustered.updateFrustumGrid(proj, 0.5f, 100.0f, 512, 512);

    std::vector<ClusterLightRecord> lights(64);
    for (auto& lt : lights) {
        lt.position[0] = 0.0f;
        lt.position[1] = 0.0f;
        lt.position[2] = -6.0f;
        lt.radius = 80.0f;
    }

    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    E2E_ASSERT_FALSE(clustered.isDegradedToSingleCluster());

    const auto& grid = clustered.getFroxelLightGridBuffer();
    uint32_t peakCount = 0;
    for (size_t i = 0; i < 2048u; ++i) {
        const uint32_t c = grid[i * 2u + 1u];
        E2E_ASSERT_LE(c, ClusteredForwardPlus::MaxLightsPerFroxel);
        if (c > peakCount) {
            peakCount = c;
        }
    }
    E2E_ASSERT_EQ(peakCount, 32u);
    return true;
}

E2E_TEST(Tier2, FEAT_09, Clustered_InvalidLight_NegativeRadiusAndNanSkipped) {
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    FroxelAabb box{};
    box.minPt[0] = -5.0f;
    box.minPt[1] = -5.0f;
    box.minPt[2] = -20.0f;
    box.maxPt[0] = 5.0f;
    box.maxPt[1] = 5.0f;
    box.maxPt[2] = -1.0f;

    E2E_ASSERT_FALSE(ClusteredForwardPlus::TestFroxelSphereIntersection(box, 0.0f, 0.0f, -5.0f, nanVal));
    E2E_ASSERT_FALSE(ClusteredForwardPlus::TestFroxelSphereIntersection(box, nanVal, 0.0f, -5.0f, nanVal));

    ClusteredForwardPlus clustered;
    clustered.updateFrustumGrid(T2R_MakePerspectiveMat4(1.04719755f, 1.0f, 0.5f, 100.0f), 0.5f, 100.0f, 512, 512);

    std::vector<ClusterLightRecord> lights(5);
    lights[0].position[2] = -5.0f;
    lights[0].radius = 3.0f;

    lights[1].position[2] = -5.0f;
    lights[1].radius = nanVal;

    lights[2].position[0] = nanVal;
    lights[2].radius = nanVal;

    lights[3].position[2] = 80.0f;
    lights[3].radius = -2.0f;

    lights[4].position[2] = 80.0f;
    lights[4].radius = 0.0f;

    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    for (uint32_t idx : clustered.getLightIndexListBuffer()) {
        E2E_ASSERT_EQ(idx, 0u);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_09, Clustered_LogZBoundary_SliceIndexClamping) {
    ClusteredForwardPlus clustered;
    clustered.updateFrustumGrid(T2R_MakePerspectiveMat4(1.04719755f, 1.0f, 0.1f, 100.0f), 0.1f, 100.0f, 512, 512);

    std::vector<ClusterLightRecord> lights(5);
    lights[0].position[2] = -0.11f;
    lights[0].radius = 0.05f;
    lights[1].position[2] = -99.8f;
    lights[1].radius = 0.5f;
    lights[2].position[2] = -250.0f;
    lights[2].radius = 1.0f;
    lights[3].position[2] = 25.0f;
    lights[3].radius = 1.0f;
    lights[4].position[2] = -10.0f;
    lights[4].radius = 1.0f;

    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    const auto& indexList = clustered.getLightIndexListBuffer();
    E2E_ASSERT_GT(indexList.size(), 0u);

    for (uint32_t lightIdx : indexList) {
        E2E_ASSERT_NE(lightIdx, 2u);
        E2E_ASSERT_NE(lightIdx, 3u);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_09, Clustered_Threshold_FourVsFiveLightsTopologyTransition) {
    ClusteredForwardPlus clustered;
    clustered.updateFrustumGrid(T2R_MakePerspectiveMat4(1.04719755f, 1.0f, 0.1f, 150.0f), 0.1f, 150.0f, 640, 640);

    std::vector<ClusterLightRecord> lights(4);
    for (uint32_t i = 0; i < 4u; ++i) {
        lights[i].position[2] = -4.0f - static_cast<float>(i);
        lights[i].radius = 2.5f;
    }

    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    E2E_ASSERT_TRUE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 1u);

    ClusterLightRecord fifth{};
    fifth.position[2] = -10.0f;
    fifth.radius = 3.0f;
    lights.push_back(fifth);

    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    E2E_ASSERT_FALSE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 2048u);

    lights.pop_back();
    clustered.cullLights(lights, T2R_MakeIdentityMat4());
    E2E_ASSERT_TRUE(clustered.isDegradedToSingleCluster());
    E2E_ASSERT_EQ(clustered.getGridConfig().totalFroxels(), 1u);
    return true;
}

// ============================================================================
// FEAT-10: 小游戏管线预设空载、极限附件与热切换边界
// ============================================================================

E2E_TEST(Tier2, FEAT_10, Preset_ZeroDrawCalls_CleanPassExecution) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 1u;
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    pipe.requiredPassTags = PassTags::Opaque;
    matcher.registerPipeline(pipe);

    const std::vector<DrawBucket> emptyBuckets;
    const auto activePasses = matcher.evaluateActivePassNames(plus, emptyBuckets);
    E2E_ASSERT_EQ(activePasses.size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_10, Preset_MaxAttachments_DeepGraphLinkage) {
    const char* deepToml = R"(
name = "DeepMrtGraph"
version = "3.0"
[[images]]
name = "MRT0"
format = "RGBA8_UNORM"
[[images]]
name = "MRT1"
format = "RGBA8_UNORM"
[[images]]
name = "MRT2"
format = "RGBA16_SFLOAT"
[[images]]
name = "MRT3"
format = "RGBA16_SFLOAT"
[[images]]
name = "Depth0"
format = "D24_UNORM_S8_UINT"
[[images]]
name = "LightAccum"
format = "RGBA16_SFLOAT"

[[passes]]
name = "GBufferMrtPass"
tags = ["Geometry", "DeferredGBuffer"]
targets = ["MRT0", "MRT1", "MRT2", "MRT3"]
depth_stencil = "Depth0"

[[passes]]
name = "ResolvePass"
tags = ["Lighting"]
targets = ["LightAccum"]
[[passes.textures]]
slot = "G0"
source = "MRT0"
[[passes.textures]]
slot = "G3"
source = "MRT3"

[[passes]]
name = "FinalPass"
tags = ["PostProcess"]
targets = ["Window"]
[[passes.textures]]
slot = "SceneColor"
source = "LightAccum"
)";
    auto parsed = RenderGraphAsset::LoadFromToml(deepToml);
    E2E_ASSERT_STATUS_OK(parsed);
    E2E_ASSERT_TRUE(parsed->isAcyclic());
    E2E_ASSERT_EQ(parsed->passes[0].targets.size(), 4u);
    E2E_ASSERT_EQ(parsed->countOffscreenColorImages(), 5u);
    return true;
}

E2E_TEST(Tier2, FEAT_10, Preset_MalformedAsset_MissingRequiredDepthRejected) {
    const char* malformedToml = R"(
name = "BrokenDepthGraph"
version = "3.0"
[[images]]
name = "SceneHdrColor"
format = "RGBA16_SFLOAT"
[[passes]]
name = "OpaquePass"
tags = ["Geometry", "Opaque"]
targets = ["SceneHdrColor"]
depth_stencil = "NonExistentDepthAttachment"
)";
    auto res = RenderGraphAsset::LoadFromToml(malformedToml);
    E2E_ASSERT_FALSE(res.ok());
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_10, Preset_ForwardLite_ZeroOffscreenColorRTAvoidance) {
    const RenderGraphAssetDesc lite = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    E2E_ASSERT_EQ(lite.countOffscreenColorImages(), 0u);
    E2E_ASSERT_EQ(lite.getOffscreenColorAttachmentCount(), 0u);
    E2E_ASSERT_TRUE(lite.isDirectToBackbuffer());
    return true;
}

E2E_TEST(Tier2, FEAT_10, Preset_HotSwitch_ForwardPlusToForwardLiteOnMemoryAlert) {
    RenderGraphAssetDesc activeGraph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_EQ(activeGraph.getOffscreenColorAttachmentCount(), 1u);
    E2E_ASSERT_FALSE(activeGraph.isDirectToBackbuffer());

    RenderGraphMatcher matcher;
    PipelineBindingDesc geomPipe{};
    geomPipe.pipelineId = 10u;
    geomPipe.globalMatch = true;
    geomPipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    geomPipe.requiredPassTags = PassTags::Geometry | PassTags::Opaque;
    matcher.registerPipeline(geomPipe);

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 16u)};
    E2E_ASSERT_TRUE(matcher.hasActiveBucketsForPass(activeGraph.findPass("ForwardOpaquePass")->tagMask, buckets));

    activeGraph = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    E2E_ASSERT_EQ(activeGraph.getOffscreenColorAttachmentCount(), 0u);
    E2E_ASSERT_TRUE(activeGraph.isDirectToBackbuffer());
    E2E_ASSERT_TRUE(matcher.hasActiveBucketsForPass(activeGraph.findPass("ForwardLitePass")->tagMask, buckets));
    return true;
}

// ============================================================================
// FEAT-11: 动态阴影图集容量耗尽、视口越界与防渗色边界 (ShadowAtlas)
// ============================================================================

E2E_TEST(Tier2, FEAT_11, ShadowAtlas_ZeroShadowCasters_BypassPass) {
    ShadowAtlas atlas(2048, 2048);
    RenderGraphMatcher matcher;
    PipelineBindingDesc shadowPipe{};
    shadowPipe.pipelineId = 20u;
    shadowPipe.globalMatch = true;
    shadowPipe.requiredInputTags = InputTags::Mesh | InputTags::CastShadow;
    shadowPipe.requiredPassTags = PassTags::Shadow;
    matcher.registerPipeline(shadowPipe);

    const std::vector<DrawBucket> nonShadowBuckets = {
        DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 25u)
    };
    E2E_ASSERT_FALSE(matcher.hasActiveBucketsForPass(PassTags::Shadow, nonShadowBuckets));
    E2E_ASSERT_EQ(atlas.getAllocatedCount(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_11, ShadowAtlas_MaxShadowCasters_AtlasExhaustionPriorityCull) {
    ShadowAtlas atlas(2048, 2048);
    for (uint32_t idx = 0; idx < 16u; ++idx) {
        auto tileOr = atlas.allocateGridTile(idx, 512u);
        E2E_ASSERT_STATUS_OK(tileOr);
    }
    E2E_ASSERT_EQ(atlas.getAllocatedCount(), 16u);

    auto overflowTile = atlas.allocateGridTile(16u, 512u);
    E2E_ASSERT_FALSE(overflowTile.ok());
    E2E_ASSERT_EQ(overflowTile.status().code(), absl::StatusCode::kResourceExhausted);

    ShadowViewportRect shelfOverflow{};
    E2E_ASSERT_FALSE(atlas.allocateTile(256u, 256u, shelfOverflow));
    return true;
}

E2E_TEST(Tier2, FEAT_11, ShadowAtlas_InvalidViewport_OutOfBoundsRejected) {
    ShadowAtlas atlas(2048, 2048);
    const Status outOfBounds = atlas.insertCustomViewport(1800u, 0u, 512u, 512u);
    E2E_ASSERT_FALSE(outOfBounds.ok());
    E2E_ASSERT_EQ(outOfBounds.code(), absl::StatusCode::kOutOfRange);

    const Status zeroDim = atlas.insertCustomViewport(0u, 0u, 0u, 512u);
    E2E_ASSERT_FALSE(zeroDim.ok());
    E2E_ASSERT_EQ(zeroDim.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_11, ShadowAtlas_SingleFboBind_AllCastersBatching) {
    ShadowAtlas atlas(2048, 2048);
    ShadowViewportRect rects[4]{};
    for (int i = 0; i < 4; ++i) {
        E2E_ASSERT_TRUE(atlas.allocateTile(1024u, 1024u, rects[i]));
    }
    E2E_ASSERT_EQ(atlas.getAllocatedCount(), 4u);
    E2E_ASSERT_EQ(atlas.getShadowSamplerCount(), 1u);

    const auto plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    uint32_t shadowPasses = 0;
    for (const auto& p : plus.passes) {
        if ((p.tagMask & PassTags::Shadow) != 0ULL) {
            ++shadowPasses;
        }
    }
    E2E_ASSERT_EQ(shadowPasses, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_11, ShadowAtlas_DynamicScissor_BleedPrevention) {
    ShadowAtlas atlas(2048, 2048);
    ShadowViewportRect leftTile{};
    ShadowViewportRect rightTile{};
    E2E_ASSERT_TRUE(atlas.allocateTile(512u, 512u, leftTile));
    E2E_ASSERT_TRUE(atlas.allocateTile(512u, 512u, rightTile));
    E2E_ASSERT_FALSE(leftTile.intersects(rightTile));

    const ShadowViewportRect scissorLeft{leftTile.x + 1u, leftTile.y + 1u, leftTile.width - 2u, leftTile.height - 2u};
    const ShadowViewportRect scissorRight{rightTile.x + 1u, rightTile.y + 1u, rightTile.width - 2u, rightTile.height - 2u};
    E2E_ASSERT_LT(scissorLeft.x + scissorLeft.width, scissorRight.x);

    const FCT::Vec4 uvRight = atlas.computeUvScaleOffset(rightTile);
    E2E_ASSERT_TRUE(std::abs(uvRight.z - 0.25f) < 1e-5f);
    return true;
}

// ============================================================================
// FEAT-12: 主场景单 FBO 连续绘制与 TBDR 深度丢弃防呆边界
// ============================================================================

E2E_TEST(Tier2, FEAT_12, MainFbo_NoOpaque_OnlySkyboxAndTranslucentMerged) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* skyPass = plus.findPass("SkyboxPass");
    const auto* transPass = plus.findPass("ForwardTransPass");
    E2E_ASSERT_TRUE(skyPass != nullptr && transPass != nullptr);
    E2E_ASSERT_EQ(skyPass->targets[0].imageName, transPass->targets[0].imageName);
    E2E_ASSERT_EQ(skyPass->depthStencilImage, transPass->depthStencilImage);

    RenderGraphMatcher matcher;
    PipelineBindingDesc opaquePipe{};
    opaquePipe.pipelineId = 1u;
    opaquePipe.globalMatch = true;
    opaquePipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    opaquePipe.requiredPassTags = PassTags::Opaque;

    PipelineBindingDesc transPipe{};
    transPipe.pipelineId = 2u;
    transPipe.globalMatch = true;
    transPipe.requiredInputTags = InputTags::Mesh | InputTags::Translucent;
    transPipe.requiredPassTags = PassTags::Translucent;

    matcher.registerPipeline(opaquePipe);
    matcher.registerPipeline(transPipe);

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh | InputTags::Translucent, 12u)};
    E2E_ASSERT_FALSE(matcher.hasActiveBucketsForPass(plus.findPass("ForwardOpaquePass")->tagMask, buckets));
    E2E_ASSERT_TRUE(matcher.hasActiveBucketsForPass(transPass->tagMask, buckets));
    return true;
}

E2E_TEST(Tier2, FEAT_12, MainFbo_MaxDrawItems_SingleSessionPreservation) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 10u;
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    pipe.requiredPassTags = PassTags::Opaque;
    matcher.registerPipeline(pipe);

    std::vector<DrawBucket> buckets;
    buckets.reserve(512);
    for (uint32_t i = 0; i < 512u; ++i) {
        buckets.emplace_back(i + 1u, InputTags::Mesh | InputTags::Opaque, 2u);
    }

    const auto items = matcher.evaluatePassExecutionItems(PassTags::Geometry | PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(items.size(), 512u);
    return true;
}

E2E_TEST(Tier2, FEAT_12, MainFbo_IllegalDepthReadDownstream_DetectedAndRejected) {
    RenderGraphAssetDesc graph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_STATUS_OK(graph.validateDepthSamplingHazard());

    for (auto& p : graph.passes) {
        if (p.name == "PostProcessPass") {
            p.textures.push_back({"IllegalDepthRead", "SceneDepth", ""});
        }
    }

    const Status hazardSt = graph.validateDepthSamplingHazard();
    E2E_ASSERT_FALSE(hazardSt.ok());
    E2E_ASSERT_EQ(hazardSt.code(), absl::StatusCode::kFailedPrecondition);
    return true;
}

E2E_TEST(Tier2, FEAT_12, MainFbo_TbdrDepthInvalidate_BeforeFboUnbind) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    const auto* transPass = plus.findPass("ForwardTransPass");
    E2E_ASSERT_TRUE(transPass != nullptr);
    E2E_ASSERT_TRUE(transPass->discardDepthAfterPass);

    FCT::RHI::WGL2_PassGroup group(nullptr);
    FCT::RHI::TBDRInvalidatePlan plan{};
    plan.isDefaultFBO = true;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    group.setTBDRInvalidatePlan(plan);

    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        group.getTBDRInvalidatePlan().endDiscards, group.getTBDRInvalidatePlan().isDefaultFBO);
    E2E_ASSERT_EQ(mapped.size(), 2u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_EQ(mapped[1], static_cast<GLenum>(GL_STENCIL));
    return true;
}

E2E_TEST(Tier2, FEAT_12, MainFbo_RenderbufferDowngrade_DepthCreatedAsRbo) {
    FCT::TransientResourceSpec unsampledDepth{};
    unsampledDepth.width = 1280;
    unsampledDepth.height = 720;
    unsampledDepth.format = FCT::Format::D24_UNORM_S8_UINT;
    unsampledDepth.isSampled = false;
    unsampledDepth.isDepthStencil = true;
    unsampledDepth.allowRenderbufferDowngrade = true;

    FCT::TransientResourceSpec sampledShadowDepth = unsampledDepth;
    sampledShadowDepth.isSampled = true;

    E2E_ASSERT_FALSE(unsampledDepth.matches(sampledShadowDepth));
    E2E_ASSERT_TRUE(!unsampledDepth.isSampled && unsampledDepth.allowRenderbufferDowngrade);
    E2E_ASSERT_FALSE(!sampledShadowDepth.isSampled && sampledShadowDepth.allowRenderbufferDowngrade);
    return true;
}

// ============================================================================
// FEAT-13: GPUScene 纹理数组满载拒绝与越界槽位钳制边界
// ============================================================================

E2E_TEST(Tier2, FEAT_13, GPUScene_ZeroTextures_IdentitySlotZero) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    AtlasAllocation unallocated{};
    const FCT::Vec4 uv = scene.getTextureUvScaleOffset(unallocated);
    E2E_ASSERT_TRUE(std::abs(uv.x - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.y - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.z - 0.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(uv.w - 0.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier2, FEAT_13, GPUScene_MaxArrayLayers_AllocationRejectionAt256) {
    AtlasShelfPacker packer(2048, 2048, 256);
    for (uint32_t layer = 0; layer < 256u; ++layer) {
        const AtlasAllocation alloc = packer.allocate(2048, 2048);
        E2E_ASSERT_TRUE(alloc.success);
        E2E_ASSERT_EQ(alloc.layer, layer);
    }

    const AtlasAllocation overflow = packer.allocate(64, 64);
    E2E_ASSERT_FALSE(overflow.success);
    return true;
}

E2E_TEST(Tier2, FEAT_13, GPUScene_InvalidSlot_ClampedToFallback) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));
    const uint32_t maxLayers = scene.getAtlasPacker()->getMaxLayers();

    E2E_ASSERT_EQ(T2R_ClampAtlasLayerSlot(0xFFFFFFFFu, maxLayers), 0u);
    E2E_ASSERT_EQ(T2R_ClampAtlasLayerSlot(maxLayers, maxLayers), 0u);
    E2E_ASSERT_EQ(T2R_ClampAtlasLayerSlot(3u, maxLayers), 3u);
    return true;
}

E2E_TEST(Tier2, FEAT_13, GPUScene_DynamicLayerAllocationRecord) {
    AtlasShelfPacker packer(1024, 1024, 8);
    const auto a0 = packer.allocate(1024, 512);
    const auto a1 = packer.allocate(1024, 512);
    const auto a2 = packer.allocate(512, 512);

    E2E_ASSERT_TRUE(a0.success && a1.success && a2.success);
    E2E_ASSERT_EQ(a0.layer, 0u);
    E2E_ASSERT_EQ(a1.layer, 0u);
    E2E_ASSERT_EQ(a2.layer, 1u);

    GpuTextureRecord rec{};
    rec.uvScaleOffset = packer.calculateUvScaleOffset(a2);
    rec.bindlessOrPageIndex = a2.layer;
    E2E_ASSERT_EQ(sizeof(GpuTextureRecord), 32u);
    E2E_ASSERT_EQ(rec.bindlessOrPageIndex, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_13, GPUScene_BatchingAcrossMaterialsViaArray) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    std::vector<GpuMaterialRecord> mats(128);
    std::vector<GpuInstanceRow> insts(128);
    for (uint32_t i = 0; i < 128u; ++i) {
        const auto alloc = scene.registerTextureToAtlas(128, 128);
        E2E_ASSERT_TRUE(alloc.success);
        mats[i].albedoTexIndex = alloc.layer;
        insts[i] = T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 0.4f, i);
    }

    E2E_ASSERT_STATUS_OK(scene.uploadMaterialData(mats, 0u));
    E2E_ASSERT_STATUS_OK(scene.uploadInstanceData(insts, 0u));

    const auto visible = scene.extractVisibleInstanceSlotStream(insts, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(visible.size(), 128u);
    return true;
}

// ============================================================================
// FEAT-14: 2D 数据纹理空更新、精确跨行切分与反转区间防护
// ============================================================================

E2E_TEST(Tier2, FEAT_14, DataTexture_ZeroRowUpdateSafe) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    const std::span<const GpuInstanceRow> emptyInstances;
    const Status st = scene.uploadInstanceData(emptyInstances, 0u);
    E2E_ASSERT_TRUE(st.ok());

    const auto emptyRegions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(0u, 0u);
    E2E_ASSERT_EQ(emptyRegions.size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_14, DataTexture_TexelCoordinateMapping_Exact1024) {
    uint32_t x = 0;
    uint32_t y = 0;

    GPUScene::TexelCoordFromSlot(0u, 1u, x, y);
    E2E_ASSERT_EQ(x, 0u);
    E2E_ASSERT_EQ(y, 0u);

    GPUScene::TexelCoordFromSlot(1023u, 1u, x, y);
    E2E_ASSERT_EQ(x, 1023u);
    E2E_ASSERT_EQ(y, 0u);

    GPUScene::TexelCoordFromSlot(1024u, 1u, x, y);
    E2E_ASSERT_EQ(x, 0u);
    E2E_ASSERT_EQ(y, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_14, DataTexture_InvertedSlotRangeRejected) {
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    std::vector<GpuInstanceRow> rows(16, T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 1.0f, 0u));
    const Status invertedSt = T2R_UploadInstanceSlotRange(scene, rows, 10u, 4u);
    E2E_ASSERT_FALSE(invertedSt.ok());

    const Status validSt = T2R_UploadInstanceSlotRange(scene, rows, 4u, 10u);
    E2E_ASSERT_TRUE(validSt.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_14, DataTexture_SubRegionUpdate_ThreeRowsExact) {
    const size_t offset = 512u * FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes;
    const size_t size = 2048u * FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes;
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(offset, size);

    E2E_ASSERT_EQ(regions.size(), 3u);
    E2E_ASSERT_EQ(regions[0].x, 512u);
    E2E_ASSERT_EQ(regions[0].width, 512u);
    E2E_ASSERT_EQ(regions[1].x, 0u);
    E2E_ASSERT_EQ(regions[1].width, 1024u);
    E2E_ASSERT_EQ(regions[1].height, 1u);
    E2E_ASSERT_EQ(regions[2].x, 0u);
    E2E_ASSERT_EQ(regions[2].width, 512u);
    return true;
}

E2E_TEST(Tier2, FEAT_14, DataTexture_Struct16ByteAlignmentInvariant) {
    E2E_ASSERT_EQ(sizeof(GpuInstanceRow), 160u);
    E2E_ASSERT_EQ(sizeof(GpuMaterialRecord), 64u);
    E2E_ASSERT_EQ(sizeof(GpuTextureRecord), 32u);
    E2E_ASSERT_EQ(sizeof(GpuSceneUniformData), 512u);

    E2E_ASSERT_EQ(alignof(GpuInstanceRow), 16u);
    E2E_ASSERT_EQ(alignof(GpuMaterialRecord), 16u);
    E2E_ASSERT_EQ(alignof(GpuTextureRecord), 16u);
    E2E_ASSERT_EQ(alignof(GpuSceneUniformData), 16u);
    return true;
}

// ============================================================================
// FEAT-15: 视锥剔除全空、全满、NaN 包围球与确定性槽位流边界
// ============================================================================

E2E_TEST(Tier2, FEAT_15, Culling_ZeroVisibleInstances_SkipDraw) {
    GPUScene scene;
    std::vector<GpuInstanceRow> farInstances(32);
    for (uint32_t i = 0; i < 32u; ++i) {
        farInstances[i] = T2R_MakeBoundedInstance(100.0f + static_cast<float>(i), 0.0f, 0.0f, 1.0f, i);
    }

    const auto slots = scene.extractVisibleInstanceSlotStream(farInstances, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(slots.size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_15, Culling_AllVisible1000InstancesPreserved) {
    GPUScene scene;
    std::vector<GpuInstanceRow> allVisible(1000);
    for (uint32_t i = 0; i < 1000u; ++i) {
        allVisible[i] = T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 0.5f, i % 32u);
    }

    const auto slots = scene.extractVisibleInstanceSlotStream(allVisible, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(slots.size(), 1000u);
    E2E_ASSERT_EQ(slots.front(), 0u);
    E2E_ASSERT_EQ(slots.back(), 999u);
    return true;
}

E2E_TEST(Tier2, FEAT_15, Culling_ZeroRadiusOrNanSphereCulled) {
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    GPUScene scene;

    std::vector<GpuInstanceRow> rows = {
        T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 0.0f, 0u),
        T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, -3.0f, 1u),
        T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, nanVal, 2u),
        T2R_MakeBoundedInstance(nanVal, 0.0f, 0.0f, 1.0f, 3u),
        T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 0.4f, 4u)
    };

    const auto slots = scene.extractVisibleInstanceSlotStream(rows, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(slots.size(), 1u);
    E2E_ASSERT_EQ(slots[0], 4u);
    return true;
}

E2E_TEST(Tier2, FEAT_15, Culling_PerInstanceSlotStreamFallback) {
    const HardwareProfile profile = DouyinMinigameProfile();
    E2E_ASSERT_FALSE(profile.isBaseInstance);

    GPUScene scene;
    std::vector<GpuInstanceRow> rows = {
        T2R_MakeBoundedInstance(50.0f, 0.0f, 0.0f, 0.5f, 0u),
        T2R_MakeBoundedInstance(0.0f, 0.0f, 0.0f, 0.5f, 1u),
        T2R_MakeBoundedInstance(-50.0f, 0.0f, 0.0f, 0.5f, 2u),
        T2R_MakeBoundedInstance(0.1f, 0.1f, 0.0f, 0.5f, 3u)
    };

    const auto slotStream = scene.extractVisibleInstanceSlotStream(rows, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(slotStream.size(), 2u);
    E2E_ASSERT_EQ(slotStream[0], 1u);
    E2E_ASSERT_EQ(slotStream[1], 3u);
    return true;
}

E2E_TEST(Tier2, FEAT_15, Culling_SlotStreamDeterministicOrder) {
    GPUScene scene;
    std::vector<GpuInstanceRow> rows(64);
    for (uint32_t i = 0; i < 64u; ++i) {
        const float cx = (i % 2u == 0u) ? 0.0f : 40.0f;
        rows[i] = T2R_MakeBoundedInstance(cx, 0.0f, 0.0f, 0.3f, i);
    }

    const auto runA = scene.extractVisibleInstanceSlotStream(rows, T2R_MakeIdentityMat4());
    const auto runB = scene.extractVisibleInstanceSlotStream(rows, T2R_MakeIdentityMat4());
    E2E_ASSERT_EQ(runA.size(), 32u);
    E2E_ASSERT_EQ(runA.size(), runB.size());

    for (size_t i = 0; i < runA.size(); ++i) {
        E2E_ASSERT_EQ(runA[i], runB[i]);
        if (i > 0u) {
            E2E_ASSERT_LT(runA[i - 1u], runA[i]);
        }
    }
    return true;
}

// ============================================================================
// FEAT-67: 双向标签位掩码边界与冲突优先级
// ============================================================================

E2E_TEST(Tier2, FEAT_67, Bitmask_ZeroInputTags_FailsRequirement) {
    PipelineBindingDesc pipe{};
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;

    const DrawBucket zeroTagBucket(1u, InputTags::None, 10u);
    E2E_ASSERT_FALSE(pipe.matchesBucket(zeroTagBucket));
    return true;
}

E2E_TEST(Tier2, FEAT_67, Bitmask_ZeroRequirementMask_MatchesAnyPass) {
    PipelineBindingDesc debugPipe{};
    debugPipe.requiredPassTags = PassTags::None;
    debugPipe.excludedPassTags = PassTags::None;

    E2E_ASSERT_TRUE(debugPipe.matchesPass(PassTags::Shadow));
    E2E_ASSERT_TRUE(debugPipe.matchesPass(PassTags::ForwardPlus | PassTags::Opaque));
    E2E_ASSERT_TRUE(debugPipe.matchesPass(PassTags::None));
    return true;
}

E2E_TEST(Tier2, FEAT_67, Bitmask_ConflictingMask_ExcludedTakesPrecedence) {
    PipelineBindingDesc pipe{};
    pipe.requiredPassTags = PassTags::Geometry;
    pipe.excludedPassTags = PassTags::Geometry | PassTags::Shadow;

    E2E_ASSERT_FALSE(pipe.matchesPass(PassTags::Geometry));
    E2E_ASSERT_FALSE(pipe.matchesPass(PassTags::Geometry | PassTags::Opaque));
    return true;
}

E2E_TEST(Tier2, FEAT_67, Bitmask_StaticVsSkinned_StrictSeparation) {
    PipelineBindingDesc staticPipe{};
    staticPipe.globalMatch = true;
    staticPipe.requiredInputTags = InputTags::Mesh | InputTags::Static;
    staticPipe.excludedInputTags = InputTags::Skinned;

    PipelineBindingDesc skinnedPipe{};
    skinnedPipe.globalMatch = true;
    skinnedPipe.requiredInputTags = InputTags::Mesh | InputTags::Skinned;
    skinnedPipe.excludedInputTags = InputTags::Static;

    const DrawBucket hybridInvalidBucket(1u, InputTags::Mesh | InputTags::Static | InputTags::Skinned, 5u);
    E2E_ASSERT_FALSE(staticPipe.matchesBucket(hybridInvalidBucket));
    E2E_ASSERT_FALSE(skinnedPipe.matchesBucket(hybridInvalidBucket));
    return true;
}

E2E_TEST(Tier2, FEAT_67, Bitmask_GlobalMatchFlag_IgnoresTechId) {
    PipelineBindingDesc globalShadow{};
    globalShadow.ownerTechId = 1u;
    globalShadow.globalMatch = true;
    globalShadow.requiredInputTags = InputTags::Mesh | InputTags::CastShadow;

    for (uint32_t techId = 1u; techId <= 10u; ++techId) {
        const DrawBucket b(techId, InputTags::Mesh | InputTags::CastShadow, 1u);
        E2E_ASSERT_TRUE(globalShadow.matchesBucket(b));
    }
    return true;
}

// ============================================================================
// FEAT-68: 纯数据 Pass 结构内存布局与非法着色器字段拦截
// ============================================================================

E2E_TEST(Tier2, FEAT_68, PassSchema_StandardLayoutPureData) {
    RenderGraphPassNodeDesc desc{};
    desc.name = "ForwardOpaquePass";
    desc.tagMask = PassTags::Opaque;
    desc.hasClearColor = true;
    desc.hasClearDepth = true;
    desc.discardDepthAfterPass = true;
    desc.targets = {{"SceneHdrColor", false}};
    desc.depthStencilImage = "SceneDepth";

    const PureDataPassNode pod = desc.toPureDataNode(7u);
    E2E_ASSERT_TRUE(std::is_standard_layout_v<PureDataPassNode>);
    E2E_ASSERT_TRUE(std::is_trivially_copyable_v<PureDataPassNode>);
    E2E_ASSERT_EQ(pod.id, 7u);
    E2E_ASSERT_EQ(pod.targetCount, 1u);
    E2E_ASSERT_EQ(pod.discardDepthOnExit, 1u);

    const char* illegalPgraph = R"(
name = "IllegalShaderInGraph"
[[passes]]
name = "MainPass"
tags = ["Opaque"]
shader = "illegal_embedded_shader.glsl"
)";
    auto rejected = RenderGraphAsset::LoadFromToml(illegalPgraph);
    E2E_ASSERT_FALSE(rejected.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_68, PassSchema_ZeroBoundPipelinesExecution) {
    RenderGraphMatcher matcher;
    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 10u)};
    const auto matched = matcher.matchPipelinesForPass(PassTags::Opaque);
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);

    E2E_ASSERT_EQ(matched.size(), 0u);
    E2E_ASSERT_EQ(items.size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_68, PassSchema_InvalidPassIdRejection) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    E2E_ASSERT_TRUE(plus.findPass("NonExistentPassId_999") == nullptr);

    auto badPin = plus.resolvePinSlot("NonExistentPassId_999", "SceneColor");
    E2E_ASSERT_FALSE(badPin.ok());
    E2E_ASSERT_EQ(badPin.status().code(), absl::StatusCode::kNotFound);
    return true;
}

E2E_TEST(Tier2, FEAT_68, PassSchema_MultiplePipelinesBinding) {
    const char* techToml = R"(
tech_id = 5
[[pipelines]]
id = 501
name = "OpaqueStatic"
required_input_tags = ["Mesh", "Static"]
required_pass_tags = ["Opaque"]
shader = "pbr_static.spv"

[[pipelines]]
id = 502
name = "OpaqueSkinned"
required_input_tags = ["Mesh", "Skinned"]
required_pass_tags = ["Opaque"]
shader = "pbr_skinned.spv"
)";
    auto pipesOr = RenderGraphMatcher::LoadTechFromToml(techToml, 5u);
    E2E_ASSERT_STATUS_OK(pipesOr);
    E2E_ASSERT_EQ(pipesOr->size(), 2u);

    RenderGraphMatcher matcher;
    for (const auto& p : *pipesOr) {
        matcher.registerPipeline(p);
    }
    E2E_ASSERT_EQ(matcher.matchPipelinesForPass(PassTags::Opaque).size(), 2u);
    return true;
}

E2E_TEST(Tier2, FEAT_68, PassSchema_ReorderingPreservesCompatibility) {
    RenderGraphAssetDesc graph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    std::swap(graph.passes[2], graph.passes[3]);
    graph.passes[2].discardDepthAfterPass = false;
    graph.passes[3].discardDepthAfterPass = true;

    E2E_ASSERT_TRUE(graph.isAcyclic());
    E2E_ASSERT_STATUS_OK(graph.validateDepthSamplingHazard());
    return true;
}

// ============================================================================
// FEAT-69: 上下游签名失配拦截边界
// ============================================================================

E2E_TEST(Tier2, FEAT_69, Layout_EmptyLayoutForFullscreenQuad) {
    PipelineBindingDesc fsPipe{};
    fsPipe.expectedVertexAttributes.clear();
    fsPipe.expectedRenderTargetCount = 1u;
    fsPipe.requiresDepthWrite = false;

    DrawBucket emptyAttrBucket(0u, InputTags::ScreenSpace, 1u);
    const Status st = RenderGraphMatcher::validateLayoutSignatures(fsPipe, emptyAttrBucket, 1u, false);
    E2E_ASSERT_TRUE(st.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_69, Layout_MissingVertexAttributeRejected) {
    PipelineBindingDesc pipe{};
    pipe.expectedVertexAttributes = {"a_Position", "a_Normal", "a_Joints0"};
    pipe.expectedRenderTargetCount = 1u;
    pipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 1u);
    bucket.attributeNames = {"a_Position", "a_Normal"};

    const Status st = RenderGraphMatcher::validateLayoutSignatures(pipe, bucket, 1u, true);
    E2E_ASSERT_FALSE(st.ok());
    E2E_ASSERT_EQ(st.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_69, Layout_TargetCountMismatchRejected) {
    PipelineBindingDesc mrtPipe{};
    mrtPipe.expectedRenderTargetCount = 3u;
    mrtPipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 1u);
    const Status st = RenderGraphMatcher::validateLayoutSignatures(mrtPipe, bucket, 1u, true);
    E2E_ASSERT_FALSE(st.ok());
    E2E_ASSERT_EQ(st.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_69, Layout_MissingDepthAttachmentRejected) {
    PipelineBindingDesc depthPipe{};
    depthPipe.expectedRenderTargetCount = 1u;
    depthPipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 1u);
    const Status st = RenderGraphMatcher::validateLayoutSignatures(depthPipe, bucket, 1u, false);
    E2E_ASSERT_FALSE(st.ok());
    E2E_ASSERT_EQ(st.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_69, Layout_DepthAndColorMatchedSuccess) {
    PipelineBindingDesc pipe{};
    pipe.expectedVertexAttributes = {"a_Position", "a_UV0"};
    pipe.expectedRenderTargetCount = 2u;
    pipe.requiresDepthWrite = true;

    DrawBucket bucket(1u, InputTags::Mesh, 8u);
    bucket.attributeNames = {"a_Position", "a_Normal", "a_UV0"};

    const Status st = RenderGraphMatcher::validateLayoutSignatures(pipe, bucket, 2u, true);
    E2E_ASSERT_TRUE(st.ok());
    return true;
}

// ============================================================================
// FEAT-70: Pin Slot 未连线回退、未知槽位安全拒绝与独立多槽映射
// ============================================================================

E2E_TEST(Tier2, FEAT_70, PinSlot_UnlinkedSlotUsesFallback) {
    RenderGraphAssetDesc graph{};
    RenderGraphPassNodeDesc post{};
    post.name = "PostPass";
    post.textures.push_back({"BloomTex", "", "@builtin/black_1x1"});
    graph.passes.push_back(post);

    auto resolved = graph.resolvePinSlot("PostPass", "BloomTex");
    E2E_ASSERT_STATUS_OK(resolved);
    E2E_ASSERT_EQ(*resolved, "@builtin/black_1x1");
    return true;
}

E2E_TEST(Tier2, FEAT_70, PinSlot_UnknownSlotQuerySafe) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    auto res = plus.resolvePinSlot("PostProcessPass", "NonExistentPinSlot");
    E2E_ASSERT_FALSE(res.ok());
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kNotFound);
    return true;
}

E2E_TEST(Tier2, FEAT_70, PinSlot_UpstreamRenamingZeroBreakage) {
    RenderGraphAssetDesc graph = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    graph.images[1].name = "HdrColorRenamed";
    graph.passes[1].targets[0].imageName = "HdrColorRenamed";
    graph.passes[4].textures[0].source = "HdrColorRenamed";

    auto resolved = graph.resolvePinSlot("PostProcessPass", "SceneColor");
    E2E_ASSERT_STATUS_OK(resolved);
    E2E_ASSERT_EQ(*resolved, "HdrColorRenamed");
    return true;
}

E2E_TEST(Tier2, FEAT_70, PinSlot_MultipleSlotsIndependentMapping) {
    const RenderGraphAssetDesc deferred = RenderGraphAsset::CreateDesktopDeferredHdrPreset();
    auto g0 = deferred.resolvePinSlot("DeferredLightingPass", "GBuffer0");
    auto g2 = deferred.resolvePinSlot("DeferredLightingPass", "GBuffer2");
    auto sh = deferred.resolvePinSlot("DeferredLightingPass", "Shadow");

    E2E_ASSERT_STATUS_OK(g0);
    E2E_ASSERT_STATUS_OK(g2);
    E2E_ASSERT_STATUS_OK(sh);
    E2E_ASSERT_EQ(*g0, "GBuffer0");
    E2E_ASSERT_EQ(*g2, "GBuffer2");
    E2E_ASSERT_EQ(*sh, "ShadowMap");
    return true;
}

E2E_TEST(Tier2, FEAT_70, PinSlot_EmptySourceFallsBack) {
    RenderGraphAssetDesc graph{};
    RenderGraphPassNodeDesc p{};
    p.name = "CompositePass";
    p.textures.push_back({"LutSlot", "", "@builtin/neutral_lut"});
    p.textures.push_back({"DanglingSlot", "", ""});
    graph.passes.push_back(p);

    auto okFallback = graph.resolvePinSlot("CompositePass", "LutSlot");
    E2E_ASSERT_STATUS_OK(okFallback);
    E2E_ASSERT_EQ(*okFallback, "@builtin/neutral_lut");

    auto dangling = graph.resolvePinSlot("CompositePass", "DanglingSlot");
    E2E_ASSERT_FALSE(dangling.ok());
    return true;
}

// ============================================================================
// FEAT-71: 负数/零/正数 Order、同键 FIFO 稳定性与逆序输入等价边界
// ============================================================================

E2E_TEST(Tier2, FEAT_71, DeterministicOrder_NegativeZeroPositiveSorting) {
    RenderGraphMatcher matcher;
    const int32_t orders[5] = {1000, -1000, 0, -1, 1};
    for (uint32_t i = 0; i < 5u; ++i) {
        PipelineBindingDesc p{};
        p.pipelineId = i + 1u;
        p.globalMatch = true;
        p.requiredPassTags = PassTags::Opaque;
        p.order = orders[i];
        matcher.registerPipeline(p);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 1u)};
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(items.size(), 5u);
    E2E_ASSERT_EQ(items[0].order, -1000);
    E2E_ASSERT_EQ(items[1].order, -1);
    E2E_ASSERT_EQ(items[2].order, 0);
    E2E_ASSERT_EQ(items[3].order, 1);
    E2E_ASSERT_EQ(items[4].order, 1000);
    return true;
}

E2E_TEST(Tier2, FEAT_71, DeterministicOrder_TieBreakingByPipelineIdAscending) {
    RenderGraphMatcher matcher;
    const uint32_t ids[4] = {900u, 3u, 45u, 12u};
    for (uint32_t id : ids) {
        PipelineBindingDesc p{};
        p.pipelineId = id;
        p.globalMatch = true;
        p.requiredPassTags = PassTags::Opaque;
        p.order = 5;
        matcher.registerPipeline(p);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 1u)};
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(items.size(), 4u);
    E2E_ASSERT_EQ(items[0].pipelineId, 3u);
    E2E_ASSERT_EQ(items[1].pipelineId, 12u);
    E2E_ASSERT_EQ(items[2].pipelineId, 45u);
    E2E_ASSERT_EQ(items[3].pipelineId, 900u);
    return true;
}

E2E_TEST(Tier2, FEAT_71, DeterministicOrder_ExactTiePreservesFifoOrder) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 7u;
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh;
    pipe.requiredPassTags = PassTags::Opaque;
    pipe.order = 0;
    matcher.registerPipeline(pipe);

    const std::vector<DrawBucket> buckets = {
        DrawBucket(10u, InputTags::Mesh, 11u),
        DrawBucket(20u, InputTags::Mesh, 22u),
        DrawBucket(30u, InputTags::Mesh, 33u)
    };
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(items.size(), 3u);
    E2E_ASSERT_EQ(items[0].bucket->instanceCount, 11u);
    E2E_ASSERT_EQ(items[1].bucket->instanceCount, 22u);
    E2E_ASSERT_EQ(items[2].bucket->instanceCount, 33u);
    return true;
}

E2E_TEST(Tier2, FEAT_71, DeterministicOrder_SemanticOrderHierarchy) {
    RenderGraphMatcher matcher;

    PipelineBindingDesc rmlUi{};
    rmlUi.pipelineId = 1u;
    rmlUi.globalMatch = true;
    rmlUi.requiredPassTags = PassTags::Overlay;
    rmlUi.order = 200;

    PipelineBindingDesc outline{};
    outline.pipelineId = 2u;
    outline.globalMatch = true;
    outline.requiredPassTags = PassTags::Overlay;
    outline.order = 100;

    PipelineBindingDesc inGameText{};
    inGameText.pipelineId = 3u;
    inGameText.globalMatch = true;
    inGameText.requiredPassTags = PassTags::Overlay;
    inGameText.order = 50;

    PipelineBindingDesc baseWorld{};
    baseWorld.pipelineId = 4u;
    baseWorld.globalMatch = true;
    baseWorld.requiredPassTags = PassTags::Overlay;
    baseWorld.order = 0;

    matcher.registerPipeline(rmlUi);
    matcher.registerPipeline(outline);
    matcher.registerPipeline(inGameText);
    matcher.registerPipeline(baseWorld);

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 1u)};
    const auto items = matcher.evaluatePassExecutionItems(PassTags::Overlay, buckets);
    E2E_ASSERT_EQ(items.size(), 4u);
    E2E_ASSERT_EQ(items[0].pipelineId, 4u);
    E2E_ASSERT_EQ(items[1].pipelineId, 3u);
    E2E_ASSERT_EQ(items[2].pipelineId, 2u);
    E2E_ASSERT_EQ(items[3].pipelineId, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_71, DeterministicOrder_ReverseInputDeterministicEquivalence) {
    RenderGraphMatcher forwardMatcher;
    RenderGraphMatcher reverseMatcher;

    std::vector<PipelineBindingDesc> defs(8);
    for (uint32_t i = 0; i < 8u; ++i) {
        defs[i].pipelineId = 100u + i;
        defs[i].globalMatch = true;
        defs[i].requiredPassTags = PassTags::Opaque;
        defs[i].order = static_cast<int32_t>((i % 3u) * 10u) - 10;
    }

    for (size_t i = 0; i < defs.size(); ++i) {
        forwardMatcher.registerPipeline(defs[i]);
        reverseMatcher.registerPipeline(defs[defs.size() - 1u - i]);
    }

    const std::vector<DrawBucket> buckets = {DrawBucket(1u, InputTags::Mesh, 4u)};
    const auto fwd = forwardMatcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    const auto rev = reverseMatcher.evaluatePassExecutionItems(PassTags::Opaque, buckets);
    E2E_ASSERT_EQ(fwd.size(), rev.size());
    for (size_t i = 0; i < fwd.size(); ++i) {
        E2E_ASSERT_EQ(fwd[i].pipelineId, rev[i].pipelineId);
        E2E_ASSERT_EQ(fwd[i].order, rev[i].order);
    }
    return true;
}

// ============================================================================
// FEAT-72: 预设查找失败、有向环检测与深拷贝隔离边界
// ============================================================================

E2E_TEST(Tier2, FEAT_72, Preset_UnknownPresetReturnsNotFound) {
    auto unknown = T2R_ResolvePresetByName("Console_NonExistentPreset_X");
    E2E_ASSERT_FALSE(unknown.ok());
    E2E_ASSERT_EQ(unknown.status().code(), absl::StatusCode::kNotFound);

    auto missingFile = RenderGraphAsset::LoadFromFile("/non_existent_dir/missing_preset.pgraph");
    E2E_ASSERT_FALSE(missingFile.ok());
    E2E_ASSERT_EQ(missingFile.status().code(), absl::StatusCode::kNotFound);
    return true;
}

E2E_TEST(Tier2, FEAT_72, Preset_CyclicDependencyGraphDetected) {
    const char* cyclicToml = R"(
name = "CyclicGraph"
version = "3.0"
[[images]]
name = "TexA"
format = "RGBA8_UNORM"
[[images]]
name = "TexB"
format = "RGBA8_UNORM"

[[passes]]
name = "PassA"
tags = ["PostProcess"]
targets = ["TexA"]
[[passes.textures]]
slot = "InB"
source = "TexB"

[[passes]]
name = "PassB"
tags = ["PostProcess"]
targets = ["TexB"]
[[passes.textures]]
slot = "InA"
source = "TexA"
)";
    auto res = RenderGraphAsset::LoadFromToml(cyclicToml);
    E2E_ASSERT_FALSE(res.ok());
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_72, Preset_FourStandardPresetsValidation) {
    const std::string_view names[4] = {
        "Desktop_DeferredHDR",
        "Desktop_HybridRayTracing",
        "MiniGame_ForwardPlus",
        "MiniGame_ForwardLite"
    };
    for (std::string_view name : names) {
        auto presetOr = T2R_ResolvePresetByName(name);
        E2E_ASSERT_STATUS_OK(presetOr);
        E2E_ASSERT_EQ(presetOr->name, name);
        E2E_ASSERT_TRUE(presetOr->isAcyclic());
        E2E_ASSERT_STATUS_OK(presetOr->validateDepthSamplingHazard());
    }
    return true;
}

E2E_TEST(Tier2, FEAT_72, Preset_LinearDagAcyclicValidation) {
    RenderGraphAssetDesc dag{};
    dag.name = "LinearChainDag";
    for (uint32_t i = 0; i < 6u; ++i) {
        const std::string outImg = absl::StrCat("ChainImg_", i);
        dag.images.push_back({outImg, "RGBA8_UNORM", 1.0f, 1.0f, 0, 0});

        RenderGraphPassNodeDesc p{};
        p.name = absl::StrCat("ChainPass_", i);
        p.tagMask = PassTags::PostProcess;
        if (i == 5u) {
            p.targets.push_back({"Window", true});
        } else {
            p.targets.push_back({outImg, false});
        }
        if (i > 0u) {
            p.textures.push_back({"Prev", absl::StrCat("ChainImg_", i - 1u), ""});
        }
        dag.passes.push_back(p);
    }

    E2E_ASSERT_TRUE(dag.isAcyclic());
    return true;
}

E2E_TEST(Tier2, FEAT_72, Preset_DeepCopyIndependence) {
    const RenderGraphAssetDesc original = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    RenderGraphAssetDesc mutatedCopy = original;

    mutatedCopy.name = "MutatedClone";
    mutatedCopy.passes.clear();
    mutatedCopy.images.clear();

    E2E_ASSERT_EQ(original.name, "MiniGame_ForwardPlus");
    E2E_ASSERT_EQ(original.passes.size(), 6u);
    E2E_ASSERT_EQ(original.images.size(), 3u);
    E2E_ASSERT_EQ(mutatedCopy.passes.size(), 0u);
    return true;
}

// ============================================================================
// FEAT-73: 动态空载旁路、离线变体剥离与级联裁剪边界
// ============================================================================

E2E_TEST(Tier2, FEAT_73, DynamicCulling_ZeroBucketsBypassed) {
    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 1u;
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    pipe.requiredPassTags = PassTags::Opaque;
    matcher.registerPipeline(pipe);

    const std::vector<DrawBucket> zeroCountBuckets = {
        DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 0u)
    };
    E2E_ASSERT_FALSE(matcher.hasActiveBucketsForPass(PassTags::Opaque, zeroCountBuckets));
    E2E_ASSERT_EQ(matcher.evaluatePassExecutionItems(PassTags::Opaque, zeroCountBuckets).size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_73, DynamicCulling_SkipResourceAllocOnBypass) {
    const RenderGraphAssetDesc plus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
    RenderGraphMatcher matcher;
    PipelineBindingDesc uiPipe{};
    uiPipe.pipelineId = 9u;
    uiPipe.globalMatch = true;
    uiPipe.requiredInputTags = InputTags::UI;
    uiPipe.requiredPassTags = PassTags::UI;
    matcher.registerPipeline(uiPipe);

    FCT::TransientResourcePool pool(nullptr);
    const std::vector<DrawBucket> uiOnlyBuckets = {DrawBucket(0u, InputTags::UI, 4u)};
    const auto activeNames = matcher.evaluateActivePassNames(plus, uiOnlyBuckets);

    E2E_ASSERT_EQ(activeNames.size(), 1u);
    E2E_ASSERT_EQ(activeNames[0], "UiOverlayPass");
    E2E_ASSERT_EQ(pool.getStats().physicalAllocations, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_73, DynamicCulling_ReactivateWhenBucketsAppear) {
    const RenderGraphAssetDesc lite = RenderGraphAsset::CreateMiniGameForwardLitePreset();
    RenderGraphMatcher matcher;
    PipelineBindingDesc pipe{};
    pipe.pipelineId = 3u;
    pipe.globalMatch = true;
    pipe.requiredInputTags = InputTags::Mesh | InputTags::Opaque;
    pipe.requiredPassTags = PassTags::ForwardLite;
    matcher.registerPipeline(pipe);

    std::vector<DrawBucket> buckets;
    E2E_ASSERT_EQ(matcher.evaluateActivePassNames(lite, buckets).size(), 0u);

    buckets.emplace_back(1u, InputTags::Mesh | InputTags::Opaque, 8u);
    const auto activeAfter = matcher.evaluateActivePassNames(lite, buckets);
    E2E_ASSERT_EQ(activeAfter.size(), 1u);
    E2E_ASSERT_EQ(activeAfter[0], "ForwardLitePass");
    return true;
}

E2E_TEST(Tier2, FEAT_73, OfflineStripping_PruneDeferredVariantsForForwardPlus) {
    const RenderGraphAssetDesc forwardPlus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();

    RenderGraphMatcher matcher;
    PipelineBindingDesc fwdPipe{};
    fwdPipe.pipelineId = 1u;
    fwdPipe.requiredPassTags = PassTags::ForwardPlus | PassTags::Opaque;

    PipelineBindingDesc gbufPipe{};
    gbufPipe.pipelineId = 2u;
    gbufPipe.requiredPassTags = PassTags::DeferredGBuffer;

    PipelineBindingDesc rtPipe{};
    rtPipe.pipelineId = 3u;
    rtPipe.requiredPassTags = PassTags::RayTracing;

    PipelineBindingDesc editorPipe{};
    editorPipe.pipelineId = 4u;
    editorPipe.requiredPassTags = PassTags::Editor;

    matcher.registerPipeline(fwdPipe);
    matcher.registerPipeline(gbufPipe);
    matcher.registerPipeline(rtPipe);
    matcher.registerPipeline(editorPipe);

    const size_t strippedCount = matcher.stripUnusedPipelinesForGraph(forwardPlus);
    E2E_ASSERT_EQ(strippedCount, 3u);
    E2E_ASSERT_EQ(matcher.getPipelines().size(), 1u);
    E2E_ASSERT_EQ(matcher.getPipelines()[0].pipelineId, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_73, DynamicCulling_CascadedPruningDownstream) {
    RenderGraphAssetDesc graph{};
    graph.name = "CascadePruneGraph";
    graph.images.push_back({"OrphanHdr", "RGBA16_SFLOAT", 1.0f, 1.0f, 0, 0});

    RenderGraphPassNodeDesc producer{};
    producer.name = "UpstreamProducerPass";
    producer.tagMask = PassTags::Opaque;
    producer.targets = {{"OrphanHdr", false}};

    RenderGraphPassNodeDesc consumer{};
    consumer.name = "WindowConsumerPass";
    consumer.tagMask = PassTags::PostProcess;
    consumer.targets = {{"Window", true}};
    consumer.textures = {{"SceneColor", "OrphanHdr", ""}};

    graph.passes.push_back(producer);
    graph.passes.push_back(consumer);

    RenderGraphMatcher matcher;
    PipelineBindingDesc prodPipe{};
    prodPipe.pipelineId = 1u;
    prodPipe.globalMatch = true;
    prodPipe.requiredInputTags = InputTags::Mesh;
    prodPipe.requiredPassTags = PassTags::Opaque;

    PipelineBindingDesc consPipe{};
    consPipe.pipelineId = 2u;
    consPipe.globalMatch = true;
    consPipe.requiredInputTags = InputTags::ToneMap;
    consPipe.requiredPassTags = PassTags::PostProcess;

    matcher.registerPipeline(prodPipe);
    matcher.registerPipeline(consPipe);

    const std::vector<DrawBucket> meshOnlyNoPost = {DrawBucket(1u, InputTags::Mesh, 5u)};
    const auto activeWhenConsumerIdle = matcher.evaluateActivePassNames(graph, meshOnlyNoPost);
    E2E_ASSERT_EQ(activeWhenConsumerIdle.size(), 0u);

    const std::vector<DrawBucket> fullChain = {
        DrawBucket(1u, InputTags::Mesh, 5u),
        DrawBucket(1u, InputTags::ToneMap, 1u)
    };
    const auto activeWhenConnected = matcher.evaluateActivePassNames(graph, fullChain);
    E2E_ASSERT_EQ(activeWhenConnected.size(), 2u);
    return true;
}

} // namespace MQEngine::Testing
