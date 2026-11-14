/**
 * @file Test_Tier1_FCT_RHI.cpp
 * @brief Tier 1 功能测试：FCT RHI WebGL 2.0 / GLES 3.0 后端、离线转译器与 RenderGraph 优化 (FEAT-23 ~ FEAT-37)
 */
#include "Common/E2ECommon.h"
#include <FCT/Context/Context.h>
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/Context/TransientResourcePool.h>
#include <FCT/Context/ShaderCrossCompiler.h>
#include <FCT/Context/RenderGraph.h>
#include <FCT/Context/SingleBufferImage.h>
#include <FCT/RHI/WGL2_TraditionalPipeline.h>
#include <FCT/RHI/WGL2_BlendState.h>
#include <FCT/RHI/WGL2_RasterizationState.h>
#include <FCT/RHI/WGL2_DepthStencilState.h>
#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT/RHI/WGL2_InputLayout.h>
#include <FCT/RHI/WGL2_CommandBuffer.h>
#include <FCT/RHI/WGL2_Image.h>
#include <FCT/RHI/WGL2_PassGroup.h>
#include <FCT/RHI/WGL2_Pass.h>
#include <FCT/RHI/ShaderBinary.h>
#include <FCT/Runtime/runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Testing {

namespace {

static const uint32_t kValidSampleFragmentSpirv[] = {
    0x07230203u, 0x00010300u, 0x000d000bu, 0x00000019u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x00000016u, 0x00030010u,
    0x00000004u, 0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000cu,
    0x00000022u, 0x00000000u, 0x00040047u, 0x0000000cu, 0x00000021u, 0x00000000u, 0x00040047u, 0x00000010u,
    0x00000022u, 0x00000000u, 0x00040047u, 0x00000010u, 0x00000021u, 0x00000001u, 0x00040047u, 0x00000016u,
    0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
    0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000008u,
    0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u, 0x00090019u, 0x0000000au,
    0x00000006u, 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000001u, 0x00000000u, 0x00040020u,
    0x0000000bu, 0x00000000u, 0x0000000au, 0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000000u, 0x0002001au,
    0x0000000eu, 0x00040020u, 0x0000000fu, 0x00000000u, 0x0000000eu, 0x0004003bu, 0x0000000fu, 0x00000010u,
    0x00000000u, 0x0003001bu, 0x00000012u, 0x0000000au, 0x00040017u, 0x00000014u, 0x00000006u, 0x00000002u,
    0x00040020u, 0x00000015u, 0x00000001u, 0x00000014u, 0x0004003bu, 0x00000015u, 0x00000016u, 0x00000001u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du,
    0x0000000au, 0x0000000du, 0x0000000cu, 0x0004003du, 0x0000000eu, 0x00000011u, 0x00000010u, 0x00050056u,
    0x00000012u, 0x00000013u, 0x0000000du, 0x00000011u, 0x0004003du, 0x00000014u, 0x00000017u, 0x00000016u,
    0x00050057u, 0x00000007u, 0x00000018u, 0x00000013u, 0x00000017u, 0x0003003eu, 0x00000009u, 0x00000018u,
    0x000100fdu, 0x00010038u
};

struct VaoSetupCounterFunctor {
    uint32_t* callCount{nullptr};
    void operator()() const noexcept {
        if (callCount) {
            ++(*callCount);
        }
    }
};

GLuint s_nextHeadlessFboId = 10u;

void stubGenNonZeroFramebuffers(GLsizei n, GLuint* ids) {
    if (!ids) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        ids[i] = ++s_nextHeadlessFboId;
    }
}

FCT::PassDesc makeColorPassDescWithClear(const std::string& name, bool clearColor) {
    FCT::PassDesc desc(name);
    if (clearColor) {
        desc.clear = FCT::EnablePassClear(FCT::ClearTypes(FCT::ClearType::color));
    }
    return desc;
}

struct ScopedFctContextRef {
    FCT::Context* ptr{nullptr};
    explicit ScopedFctContextRef(FCT::Context* p) noexcept : ptr(p) {}
    ~ScopedFctContextRef() {
        if (ptr) {
            ptr->release();
        }
    }
};

template <typename ProcT>
struct ScopedT1GlProcHook {
    ProcT& slot;
    ProcT previous;
    ScopedT1GlProcHook(ProcT& target, ProcT replacement) noexcept : slot(target), previous(target) {
        slot = replacement;
    }
    ~ScopedT1GlProcHook() {
        slot = previous;
    }
};

uint32_t g_t1GlStateCallCount = 0u;
GLint g_t1ReportedUboAlignment = 256;
GLint g_t1ReportedMaxUboRange = 65536;

void hookT1GlEnable(GLenum /*cap*/) {
    ++g_t1GlStateCallCount;
}

void hookT1GlDisable(GLenum /*cap*/) {
    ++g_t1GlStateCallCount;
}

void hookT1GlColorMask(GLboolean /*r*/, GLboolean /*g*/, GLboolean /*b*/, GLboolean /*a*/) {
    ++g_t1GlStateCallCount;
}

void hookT1GlGetIntegerv(GLenum pname, GLint* data) {
    if (!data) {
        return;
    }
    if (pname == GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT) {
        *data = g_t1ReportedUboAlignment;
    } else if (pname == GL_MAX_UNIFORM_BLOCK_SIZE) {
        *data = g_t1ReportedMaxUboRange;
    }
}

} // namespace

// ============================================================================
// [FEAT-23] FCT 硬件能力正交自省契约 (PRD 04 §1)
// ============================================================================

E2E_TEST(Tier1, FEAT_23, Caps_DouyinMinigame_Defaults) {
    FCT::WGL2_Context wgl2Ctx(nullptr);

    E2E_ASSERT_TRUE(wgl2Ctx.isIntegrateGpu());
    E2E_ASSERT_FALSE(wgl2Ctx.isBindlessSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isIndirectDrawSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isRayTracingSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isMeshShaderSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isComputeSupported());
    return true;
}

E2E_TEST(Tier1, FEAT_23, Caps_Desktop_Defaults) {
    FCT::Runtime runtime;
    ScopedFctContextRef vkGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(vkGuard.ptr, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::Context& desktopCtx = *vkGuard.ptr;

    E2E_ASSERT_FALSE(wgl2Ctx.isBindlessSupported());
    E2E_ASSERT_TRUE(desktopCtx.isBindlessSupported());
    E2E_ASSERT_TRUE(desktopCtx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_TRUE(desktopCtx.isIndirectDrawSupported());
    E2E_ASSERT_TRUE(desktopCtx.isBaseInstanceSupported());
    E2E_ASSERT_TRUE(desktopCtx.isComputeSupported());
    return true;
}

E2E_TEST(Tier1, FEAT_23, Caps_UboAlignment_DouyinVsDesktop) {
    FCT::Runtime runtime;
    ScopedFctContextRef vkGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(vkGuard.ptr, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    const uint32_t wgl2Align = wgl2Ctx.getUniformBufferOffsetAlignment();
    const uint32_t desktopAlign = vkGuard.ptr->getUniformBufferOffsetAlignment();

    E2E_ASSERT_EQ(wgl2Align, 256u);
    E2E_ASSERT_GT(desktopAlign, 0u);
    E2E_ASSERT_EQ(desktopAlign & (desktopAlign - 1u), 0u);
    E2E_ASSERT_LE(desktopAlign, wgl2Align);

    ScopedT1GlProcHook<::PFNGLGETINTEGERVPROC> getIntGuard(::glGetIntegerv, hookT1GlGetIntegerv);
    g_t1ReportedUboAlignment = 64;
    E2E_ASSERT_EQ(wgl2Ctx.getUniformBufferOffsetAlignment(), 64u);
    g_t1ReportedUboAlignment = 256;
    return true;
}

E2E_TEST(Tier1, FEAT_23, Caps_MaxUboRange_DouyinVsDesktop) {
    FCT::Runtime runtime;
    ScopedFctContextRef vkGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(vkGuard.ptr, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    const uint32_t wgl2MaxRange = wgl2Ctx.getMaxUniformBufferRange();
    const uint32_t desktopMaxRange = vkGuard.ptr->getMaxUniformBufferRange();

    E2E_ASSERT_EQ(wgl2MaxRange, 65536u);
    E2E_ASSERT_GE(desktopMaxRange, wgl2MaxRange);

    ScopedT1GlProcHook<::PFNGLGETINTEGERVPROC> getIntGuard(::glGetIntegerv, hookT1GlGetIntegerv);
    g_t1ReportedMaxUboRange = 131072;
    E2E_ASSERT_EQ(wgl2Ctx.getMaxUniformBufferRange(), 131072u);
    g_t1ReportedMaxUboRange = 65536;
    return true;
}

E2E_TEST(Tier1, FEAT_23, Caps_BaseInstanceDynamicProbe) {
    FCT::Runtime runtime;
    ScopedFctContextRef wgl2Guard(runtime.createContext(FCT::ContextBackend::WGL2));
    ScopedFctContextRef vkGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(wgl2Guard.ptr, nullptr);
    E2E_ASSERT_NE(vkGuard.ptr, nullptr);

    E2E_ASSERT_FALSE(wgl2Guard.ptr->isBaseInstanceSupported());
    E2E_ASSERT_FALSE(wgl2Guard.ptr->isAccelerationStructureSupported());
    E2E_ASSERT_TRUE(vkGuard.ptr->isBaseInstanceSupported());
    E2E_ASSERT_NE(wgl2Guard.ptr->isBaseInstanceSupported(), vkGuard.ptr->isBaseInstanceSupported());
    return true;
}

// ============================================================================
// [FEAT-24] 64-bit 状态差分管线与 VAO 缓存 (PRD 04 §2.1)
// ============================================================================

E2E_TEST(Tier1, FEAT_24, StateKey_EncodeIdentity) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_TraditionalPipeline pipeline(&wgl2Ctx);

    FCT::RHI::WGL2_RasterizationState rs(&wgl2Ctx);
    rs.cullMode(FCT::CullMode::Front);
    rs.frontFace(FCT::FrontFace::Clockwise);

    FCT::RHI::WGL2_DepthStencilState ds(&wgl2Ctx);
    ds.depthTestEnable(true);
    ds.depthWriteEnable(true);

    FCT::RHI::WGL2_BlendState bs(&wgl2Ctx);
    bs.targets(1).alpha(0).mask(0xFu, 0);

    pipeline.setRasterizationState(&rs);
    pipeline.setDepthStencilState(&ds);
    pipeline.setBlendState(&bs);

    const uint64_t key = pipeline.getStateKey();
    E2E_ASSERT_NE(key & (1ULL << 0), 0ULL);
    E2E_ASSERT_NE(key & (1ULL << 1), 0ULL);
    E2E_ASSERT_NE(key & (1ULL << 2), 0ULL);
    E2E_ASSERT_NE(key & (1ULL << 5), 0ULL);
    E2E_ASSERT_NE(key & (1ULL << 6), 0ULL);
    E2E_ASSERT_NE(key & (1ULL << 31), 0ULL);
    E2E_ASSERT_EQ((key >> 54) & 0xFULL, 0xFULL);
    return true;
}

E2E_TEST(Tier1, FEAT_24, StateDiff_ZeroDiffSkipsCommands) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedT1GlProcHook<::PFNGLENABLEPROC> enGuard(::glEnable, hookT1GlEnable);
    ScopedT1GlProcHook<::PFNGLDISABLEPROC> disGuard(::glDisable, hookT1GlDisable);
    ScopedT1GlProcHook<::PFNGLCOLORMASKPROC> maskGuard(::glColorMask, hookT1GlColorMask);

    g_t1GlStateCallCount = 0u;
    const uint64_t targetKey = (1ULL << 5) | (1ULL << 31) | (0xFULL << 54);
    wgl2Ctx.applyGraphicsState(targetKey);
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), targetKey);
    E2E_ASSERT_GT(g_t1GlStateCallCount, 0u);

    g_t1GlStateCallCount = 0u;
    wgl2Ctx.applyGraphicsState(targetKey);
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), targetKey);
    E2E_ASSERT_EQ(g_t1GlStateCallCount, 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_24, StateDiff_BlendBitmaskChange) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_TraditionalPipeline pipeOpaque(&wgl2Ctx);
    FCT::RHI::WGL2_BlendState bsOff(&wgl2Ctx);
    bsOff.targets(1).opaque(0).mask(0xFu, 0);
    pipeOpaque.setBlendState(&bsOff);

    FCT::RHI::WGL2_TraditionalPipeline pipeAlpha(&wgl2Ctx);
    FCT::RHI::WGL2_BlendState bsOn(&wgl2Ctx);
    bsOn.targets(1).alpha(0).mask(0xFu, 0);
    pipeAlpha.setBlendState(&bsOn);

    E2E_ASSERT_EQ(pipeOpaque.getStateKey() & (1ULL << 31), 0ULL);
    E2E_ASSERT_NE(pipeAlpha.getStateKey() & (1ULL << 31), 0ULL);

    wgl2Ctx.applyGraphicsState(pipeOpaque.getStateKey());
    wgl2Ctx.applyGraphicsState(pipeAlpha.getStateKey());
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), pipeAlpha.getStateKey());
    return true;
}

E2E_TEST(Tier1, FEAT_24, StateDiff_DepthStencilBitmaskChange) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_TraditionalPipeline pipeNoDepth(&wgl2Ctx);
    FCT::RHI::WGL2_DepthStencilState dsOff(&wgl2Ctx);
    dsOff.depthTestEnable(false);
    dsOff.depthWriteEnable(false);
    pipeNoDepth.setDepthStencilState(&dsOff);

    FCT::RHI::WGL2_TraditionalPipeline pipeDepth(&wgl2Ctx);
    FCT::RHI::WGL2_DepthStencilState dsOn(&wgl2Ctx);
    dsOn.depthTestEnable(true);
    dsOn.depthWriteEnable(true);
    pipeDepth.setDepthStencilState(&dsOn);

    E2E_ASSERT_EQ(pipeNoDepth.getStateKey() & (1ULL << 5), 0ULL);
    E2E_ASSERT_NE(pipeDepth.getStateKey() & (1ULL << 5), 0ULL);

    wgl2Ctx.applyGraphicsState(pipeNoDepth.getStateKey());
    wgl2Ctx.applyGraphicsState(pipeDepth.getStateKey());
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), pipeDepth.getStateKey());
    return true;
}

E2E_TEST(Tier1, FEAT_24, VaoCache_SameBindingsProduceSameHash) {
    FCT::VAOCacheKey key1{101u, {201u, 0u, 0u, 0u}, 301u};
    FCT::VAOCacheKey key2{101u, {201u, 0u, 0u, 0u}, 301u};
    FCT::VAOCacheKey key3{101u, {202u, 0u, 0u, 0u}, 301u};

    FCT::VAOCacheHasher hasher;
    E2E_ASSERT_EQ(key1, key2);
    E2E_ASSERT_FALSE(key1 == key3);
    E2E_ASSERT_EQ(hasher(key1), hasher(key2));
    E2E_ASSERT_NE(hasher(key1), hasher(key3));

    FCT::WGL2_Context wgl2Ctx(nullptr);
    uint32_t setupCalls = 0u;
    (void)wgl2Ctx.getOrCreateVAO(key1, VaoSetupCounterFunctor{&setupCalls});
    (void)wgl2Ctx.getOrCreateVAO(key2, VaoSetupCounterFunctor{&setupCalls});
    E2E_ASSERT_EQ(setupCalls, 1u);
    return true;
}

// ============================================================================
// [FEAT-25] 只读 2D 数据纹理 StorageBuffer (PRD 04 §2.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_25, StorageBuffer_AllocationDimensions) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_StorageBuffer sbuf(&wgl2Ctx);
    E2E_ASSERT_STATUS_OK(sbuf.create(65536u));

    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kWidth, 1024u);
    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes, 16u);
    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kRowSizeBytes, 16384u);
    E2E_ASSERT_EQ(sbuf.getWidth(), 1024u);
    E2E_ASSERT_EQ(sbuf.getHeight(), 4u);
    E2E_ASSERT_EQ(sbuf.size(), 65536u);
    return true;
}

E2E_TEST(Tier1, FEAT_25, StorageBuffer_SingleRowSubRegion) {
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(32u, 64u);
    E2E_ASSERT_EQ(regions.size(), 1u);
    E2E_ASSERT_EQ(regions[0].x, 2u);
    E2E_ASSERT_EQ(regions[0].y, 0u);
    E2E_ASSERT_EQ(regions[0].width, 4u);
    E2E_ASSERT_EQ(regions[0].height, 1u);
    E2E_ASSERT_LE(regions[0].x + regions[0].width, FCT::RHI::WGL2_StorageBuffer::kWidth);
    return true;
}

E2E_TEST(Tier1, FEAT_25, StorageBuffer_MultiRowThreeSubRegions) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_StorageBuffer sbuf(&wgl2Ctx);
    E2E_ASSERT_STATUS_OK(sbuf.create(65536u));

    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(16000u, 40000u);
    E2E_ASSERT_EQ(regions.size(), 3u);

    std::vector<uint8_t> payload(40000u, 0x5Au);
    sbuf.updateData(payload.data(), payload.size(), 16000u);
    const auto* mapped = static_cast<const uint8_t*>(sbuf.map());
    E2E_ASSERT_NE(mapped, nullptr);
    E2E_ASSERT_EQ(mapped[16000u], 0x5Au);
    E2E_ASSERT_EQ(mapped[55999u], 0x5Au);
    sbuf.unmap();
    return true;
}

E2E_TEST(Tier1, FEAT_25, StorageBuffer_AlignmentRequirement) {
    const auto regionsAligned = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(0u, 64u);
    E2E_ASSERT_EQ(regionsAligned.size(), 1u);
    E2E_ASSERT_EQ(regionsAligned[0].width, 4u);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_StorageBuffer sbuf(&wgl2Ctx);
    E2E_ASSERT_STATUS_OK(sbuf.create(20u));
    E2E_ASSERT_EQ(sbuf.size(), 32u);

    const auto regionsAligned2 = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(16u, 32u);
    E2E_ASSERT_EQ(regionsAligned2.size(), 1u);
    E2E_ASSERT_EQ(regionsAligned2[0].x, 1u);
    E2E_ASSERT_EQ(regionsAligned2[0].width, 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_25, StorageBuffer_DeviceAddressZeroOrHandle) {
    FCT::Runtime runtime;
    ScopedFctContextRef vkGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(vkGuard.ptr, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_TRUE(vkGuard.ptr->isReadWriteStorageBufferSupported());

    FCT::RHI::WGL2_StorageBuffer sbuf(&wgl2Ctx);
    E2E_ASSERT_EQ(sbuf.getDeviceAddress(), 0ULL);

    E2E_ASSERT_STATUS_OK(sbuf.create(128u));
    E2E_ASSERT_GE(sbuf.size(), 128u);
    E2E_ASSERT_EQ(sbuf.getDeviceAddress(), static_cast<uint64_t>(sbuf.getTextureHandle()));
    return true;
}

// ============================================================================
// [FEAT-26] 帧环形 UBO 统一更新机制 (PRD 04 §2.3)
// ============================================================================

E2E_TEST(Tier1, FEAT_26, RingUbo_OffsetAlignment256) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));

    const auto alloc1 = ringUbo.allocate(80u);
    const auto alloc2 = ringUbo.allocate(128u);

    E2E_ASSERT_EQ(alloc1.offset, 0u);
    E2E_ASSERT_EQ(alloc2.offset, 256u);
    E2E_ASSERT_EQ(alloc2.offset % 256u, 0u);
    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier1, FEAT_26, RingUbo_SingleBatchSubData) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));

    const auto a0 = ringUbo.allocate(64u);
    const auto a1 = ringUbo.allocate(64u);
    const auto a2 = ringUbo.allocate(64u);
    std::memset(a0.cpuPtr, 0x11, a0.size);
    std::memset(a1.cpuPtr, 0x22, a1.size);
    std::memset(a2.cpuPtr, 0x33, a2.size);

    E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 512u + 64u);
    ringUbo.flushToGpu();
    E2E_ASSERT_EQ(a0.cpuPtr[0], 0x11u);
    E2E_ASSERT_EQ(a2.cpuPtr[0], 0x33u);
    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier1, FEAT_26, RingUbo_BindBufferRangeOffset) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));

    auto& ringUbo = wgl2Ctx.getFrameRingUBO();
    E2E_ASSERT_EQ(ringUbo.getAlignment(), 256u);

    const auto first = ringUbo.allocate(128u);
    const auto second = ringUbo.allocate(128u);
    const auto third = ringUbo.allocate(128u);

    E2E_ASSERT_EQ(first.offset, 0u);
    E2E_ASSERT_EQ(second.offset, 256u);
    E2E_ASSERT_EQ(third.offset, 512u);
    E2E_ASSERT_LE(third.size, wgl2Ctx.getMaxUniformBufferRange());
    return true;
}

E2E_TEST(Tier1, FEAT_26, RingUbo_WrapAroundReset) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));

    const auto nearEnd = ringUbo.allocate(65000u);
    E2E_ASSERT_EQ(nearEnd.offset, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 65000u);

    const auto wrapped = ringUbo.allocate(1024u);
    E2E_ASSERT_EQ(wrapped.offset, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 1024u);

    ringUbo.reset();
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 0u);
    E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 0u);
    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier1, FEAT_26, RingUbo_ZeroGlUniformCalls) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(16384u, 256u));

    const float viewProj[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const auto alloc = ringUbo.allocate(sizeof(viewProj));
    E2E_ASSERT_NE(alloc.cpuPtr, nullptr);
    std::memcpy(alloc.cpuPtr, viewProj, sizeof(viewProj));

    float readBack[4] = {};
    std::memcpy(readBack, alloc.cpuPtr, sizeof(readBack));
    E2E_ASSERT_NEAR(readBack[0], 1.0f, 1e-6f);
    E2E_ASSERT_NEAR(readBack[3], 1.0f, 1e-6f);
    ringUbo.destroy();
    return true;
}

// ============================================================================
// [FEAT-27] PerInstance 整型顶点属性分发 (PRD 04 §2.3)
// ============================================================================

E2E_TEST(Tier1, FEAT_27, AttribDivisor_PerInstanceIsOne) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
    layout.add(0u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Position3f}});
    layout.add(1u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}});
    layout.inputRate(FCT::RHI::InputRate::PerVertex, 0u);
    layout.inputRate(FCT::RHI::InputRate::PerInstance, 1u);
    layout.applyBinding(0u, 11u);
    layout.applyBinding(1u, 12u);

    const auto& rates = layout.getInputRates();
    E2E_ASSERT_EQ(rates.find(0u)->second, FCT::RHI::InputRate::PerVertex);
    E2E_ASSERT_EQ(rates.find(1u)->second, FCT::RHI::InputRate::PerInstance);
    return true;
}

E2E_TEST(Tier1, FEAT_27, AttribIPointer_R32_UINT_Format) {
    FCT::VertexElement intSlotElem{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT};
    FCT::VertexElement floatPosElem{FCT::VtxType::Position3f};

    E2E_ASSERT_EQ(intSlotElem.getFormat(), FCT::Format::R32_UINT);
    E2E_ASSERT_EQ(FCT::WGL2_TranslateType(intSlotElem.getFormat()), static_cast<GLenum>(GL_UNSIGNED_INT));
    E2E_ASSERT_NE(floatPosElem.getFormat(), FCT::Format::R32_UINT);
    return true;
}

E2E_TEST(Tier1, FEAT_27, AttribPointer_FloatFormatFallback) {
    FCT::VertexElement vec4Elem{FCT::VtxType::Custom, "a_Tangent", FCT::Format::R32G32B32A32_SFLOAT};
    E2E_ASSERT_EQ(FCT::FormatSize(vec4Elem.getFormat()), 16u);
    E2E_ASSERT_EQ(FCT::WGL2_TranslateType(vec4Elem.getFormat()), static_cast<GLenum>(GL_FLOAT));
    return true;
}

E2E_TEST(Tier1, FEAT_27, InputLayout_MultiStreamHashing) {
    FCT::VertexLayout stream0{
        FCT::VertexElement{FCT::VtxType::Position3f},
        FCT::VertexElement{FCT::VtxType::Normal3f}
    };
    FCT::VertexLayout stream1{FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}};

    FCT::RHI::WGL2_InputLayout layoutA(nullptr);
    layoutA.add(0u, stream0);
    layoutA.add(1u, stream1);
    layoutA.inputRate(FCT::RHI::InputRate::PerInstance, 1u);

    FCT::RHI::WGL2_InputLayout layoutB(nullptr);
    layoutB.add(0u, stream0);
    layoutB.add(1u, stream1);
    layoutB.inputRate(FCT::RHI::InputRate::PerInstance, 1u);

    FCT::RHI::WGL2_InputLayout layoutC(nullptr);
    layoutC.add(0u, stream0);

    E2E_ASSERT_EQ(layoutA.getHash(), layoutB.getHash());
    E2E_ASSERT_NE(layoutA.getHash(), layoutC.getHash());
    return true;
}

E2E_TEST(Tier1, FEAT_27, InputLayout_StrideAndOffsetCalculation) {
    FCT::VertexLayout meshStream{
        FCT::VertexElement{FCT::VtxType::Position3f},
        FCT::VertexElement{FCT::VtxType::TexCoord2f}
    };
    FCT::VertexLayout instanceStream{FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}};

    E2E_ASSERT_EQ(meshStream.getElementCount(), 2u);
    E2E_ASSERT_EQ(meshStream.getElementOffset(0), 0u);
    E2E_ASSERT_EQ(meshStream.getElementOffset(1), 12u);
    E2E_ASSERT_EQ(meshStream.getStride(), 20u);
    E2E_ASSERT_EQ(instanceStream.getStride(), 4u);
    return true;
}

// ============================================================================
// [FEAT-28] 线性字节码命令缓冲录制 (PRD 04 §2.4)
// ============================================================================

E2E_TEST(Tier1, FEAT_28, CmdBuffer_RecordLinearPODStream) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);

    cmdBuf.begin();
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
    cmdBuf.scissor(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
    cmdBuf.draw(0u, 0u, 36u, 100u);
    cmdBuf.end();

    const size_t expectedBytes =
        sizeof(FCT::RHI::Cmd_SetViewport) +
        sizeof(FCT::RHI::Cmd_SetScissor) +
        sizeof(FCT::RHI::Cmd_Draw);
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), expectedBytes);
    return true;
}

E2E_TEST(Tier1, FEAT_28, CmdBuffer_ZeroHeapAllocOnRecord) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);

    const size_t reservedCapacity = cmdBuf.getBytecodeStream().capacity();
    E2E_ASSERT_GE(reservedCapacity, 65536u);

    cmdBuf.begin();
    for (uint32_t i = 0; i < 50u; ++i) {
        cmdBuf.drawIndex(0u, 0u, 36u, 1u, 0u);
    }
    cmdBuf.end();

    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().capacity(), reservedCapacity);
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), 50u * sizeof(FCT::RHI::Cmd_DrawIndex));
    return true;
}

E2E_TEST(Tier1, FEAT_28, CmdBuffer_OpcodeSequentialOrder) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);

    cmdBuf.begin();
    cmdBuf.bindPipeline(nullptr);
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{1280.0f, 720.0f});
    cmdBuf.drawIndex(0u, 0u, 12u, 4u, 0u);
    cmdBuf.nextPass();
    cmdBuf.end();

    const auto& bytes = cmdBuf.getBytecodeStream();
    size_t offset = 0u;
    E2E_ASSERT_EQ(static_cast<FCT::RHI::WGL2_CmdOpcode>(bytes[offset]), FCT::RHI::WGL2_CmdOpcode::BindPipeline);
    offset += sizeof(FCT::RHI::Cmd_BindPipeline);
    E2E_ASSERT_EQ(static_cast<FCT::RHI::WGL2_CmdOpcode>(bytes[offset]), FCT::RHI::WGL2_CmdOpcode::SetViewport);
    offset += sizeof(FCT::RHI::Cmd_SetViewport);
    E2E_ASSERT_EQ(static_cast<FCT::RHI::WGL2_CmdOpcode>(bytes[offset]), FCT::RHI::WGL2_CmdOpcode::DrawIndex);
    offset += sizeof(FCT::RHI::Cmd_DrawIndex);
    E2E_ASSERT_EQ(static_cast<FCT::RHI::WGL2_CmdOpcode>(bytes[offset]), FCT::RHI::WGL2_CmdOpcode::NextPass);
    return true;
}

E2E_TEST(Tier1, FEAT_28, CmdBuffer_ResetClearsCount) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    const size_t initialCap = cmdBuf.getBytecodeStream().capacity();

    cmdBuf.begin();
    cmdBuf.draw(0u, 0u, 6u, 1u);
    cmdBuf.end();
    E2E_ASSERT_GT(cmdBuf.getBytecodeStream().size(), 0u);

    cmdBuf.reset();
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), 0u);
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().capacity(), initialCap);
    return true;
}

E2E_TEST(Tier1, FEAT_28, CmdBuffer_BatchExecutionDispatch) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_TraditionalPipeline pipeline(&wgl2Ctx);
    FCT::RHI::WGL2_BlendState bs(&wgl2Ctx);
    bs.targets(1).alpha(0).mask(0xFu, 0);
    pipeline.setBlendState(&bs);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    cmdBuf.bindPipeline(&pipeline);
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{800.0f, 600.0f});
    cmdBuf.drawIndex(0u, 0u, 36u, 8u, 0u);
    cmdBuf.end();
    cmdBuf.submit();

    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), pipeline.getStateKey());
    return true;
}

// ============================================================================
// [FEAT-29] 纹理局部脏矩形与 Canvas 直刷 (PRD 04 §2.4)
// ============================================================================

E2E_TEST(Tier1, FEAT_29, SubRegion_R8Glyph_UnpackAlignment1) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image r8Atlas(&wgl2Ctx);
    r8Atlas.width(1024);
    r8Atlas.height(1024);
    r8Atlas.format(FCT::Format::R8_UNORM);
    r8Atlas.createFromExisting(501u, GL_TEXTURE_2D, false);

    E2E_ASSERT_EQ(FCT::WGL2_GetFormatBytesPerPixel(FCT::Format::R8_UNORM), 1u);
    E2E_ASSERT_EQ(FCT::WGL2_TranslateInternalFormat(FCT::Format::R8_UNORM), static_cast<GLenum>(GL_R8));

    std::vector<uint8_t> oddGlyph(37u * 29u, 0xFFu);
    r8Atlas.updateSubRegion(12u, 16u, 37u, 29u, 0u, 0u, oddGlyph.data(), 0u);
    E2E_ASSERT_EQ(r8Atlas.getTextureHandle(), 501u);
    return true;
}

E2E_TEST(Tier1, FEAT_29, SubRegion_RowPitchHandling) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image rgbaTex(&wgl2Ctx);
    rgbaTex.width(512);
    rgbaTex.height(512);
    rgbaTex.format(FCT::Format::R8G8B8A8_UNORM);
    rgbaTex.createFromExisting(502u, GL_TEXTURE_2D, false);

    const uint32_t bpp = FCT::WGL2_GetFormatBytesPerPixel(FCT::Format::R8G8B8A8_UNORM);
    E2E_ASSERT_EQ(bpp, 4u);

    const size_t rowPitchBytes = 128u * bpp;
    std::vector<uint8_t> pitchedPixels(rowPitchBytes * 32u, 0x7Fu);
    rgbaTex.updateSubRegion(0u, 0u, 64u, 32u, 0u, 0u, pitchedPixels.data(), rowPitchBytes);
    E2E_ASSERT_EQ(rgbaTex.getGLTarget(), static_cast<GLenum>(GL_TEXTURE_2D));
    return true;
}

E2E_TEST(Tier1, FEAT_29, SubRegion_BoundsValidation) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));

    FCT::SingleBufferImage atlasImg(&wgl2Ctx);
    atlasImg.width(1024);
    atlasImg.height(1024);
    atlasImg.format(FCT::Format::R8G8B8A8_UNORM);
    atlasImg.create(false);

    std::vector<uint8_t> subPixels(64u * 64u * 4u, 0xCCu);
    atlasImg.updateSubRegion(128u, 256u, 64u, 64u, 0u, 0u, subPixels.data(), 0u);
    E2E_ASSERT_EQ(atlasImg.width(), 1024);
    E2E_ASSERT_EQ(atlasImg.height(), 1024);
    return true;
}

E2E_TEST(Tier1, FEAT_29, SubRegion_MipAndArrayLayerParams) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image arrayTex(&wgl2Ctx);
    arrayTex.width(512);
    arrayTex.height(512);
    arrayTex.arrayLayers(4);
    arrayTex.format(FCT::Format::R8G8B8A8_UNORM);
    arrayTex.createFromExisting(503u, GL_TEXTURE_2D_ARRAY, false);

    E2E_ASSERT_EQ(arrayTex.getGLTarget(), static_cast<GLenum>(GL_TEXTURE_2D_ARRAY));
    std::vector<uint8_t> mipSlice(128u * 128u * 4u, 0x99u);
    arrayTex.updateSubRegion(0u, 0u, 128u, 128u, 2u, 1u, mipSlice.data(), 0u);
    E2E_ASSERT_EQ(arrayTex.arrayLayers(), 4);
    return true;
}

E2E_TEST(Tier1, FEAT_29, SubRegion_NullDataSafety) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image rboImg(&wgl2Ctx);
    rboImg.width(256);
    rboImg.height(256);
    rboImg.format(FCT::Format::R8G8B8A8_UNORM);
    rboImg.createFromExisting(504u, GL_RENDERBUFFER, true);

    rboImg.updateData(nullptr, 0u);
    const uint8_t pixel[4] = {255u, 0u, 0u, 255u};
    rboImg.updateSubRegion(0u, 0u, 1u, 1u, 0u, 0u, pixel, 0u);
    E2E_ASSERT_TRUE(rboImg.isRenderbuffer());
    return true;
}

// ============================================================================
// [FEAT-30] SPIR-V 离线转 ES 3.0 工具 (PRD 04 §3)
// ============================================================================

E2E_TEST(Tier1, FEAT_30, Transpile_OutputHeaderVersion300Es) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.glslVersion = 300u;
    options.es = true;
    options.stage = FCT::ShaderKind::FragmentShader;

    const auto resultOr = compiler.transpileSpirvToGlslEs300(kValidSampleFragmentSpirv, options);
    E2E_ASSERT_STATUS_OK(resultOr);
    E2E_ASSERT_TRUE(resultOr->success);
    E2E_ASSERT_EQ(resultOr->glslSource.substr(0u, 15u), "#version 300 es");
    return true;
}

E2E_TEST(Tier1, FEAT_30, Transpile_CombinedSamplerMapping) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.glslVersion = 300u;
    options.es = true;
    options.emitCombinedSamplers = true;
    options.stage = FCT::ShaderKind::FragmentShader;

    const auto resultOr = compiler.transpileSpirvToGlslEs300(kValidSampleFragmentSpirv, options);
    E2E_ASSERT_STATUS_OK(resultOr);
    E2E_ASSERT_GE(resultOr->combinedSamplers.size(), 1u);
    E2E_ASSERT_FALSE(resultOr->combinedSamplers[0].combinedSamplerName.empty());
    return true;
}

E2E_TEST(Tier1, FEAT_30, Transpile_UniformBlockExtraction) {
    FCT::ConstLayout frameLayout("PerObject", FCT::ConstElement(FCT::ConstType::Mat4, "u_Model"));
    FCT::RHI::ShaderBinary bin;
    bin.addConstBufferLocation(frameLayout, 0u, 1u);

    const auto cbLocs = bin.constBufferLocations();
    E2E_ASSERT_EQ(cbLocs.size(), 1u);
    E2E_ASSERT_EQ(cbLocs.find("PerObject")->second.second, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_30, Transpile_VertexAttributeLocations) {
    FCT::RHI::ShaderBinary bin;
    bin.location({{"POSITION", 0u}, {"NORMAL", 1u}, {"TEXCOORD0", 2u}});

    E2E_ASSERT_EQ(bin.locationBySemantic("POSITION"), 0);
    E2E_ASSERT_EQ(bin.locationBySemantic("NORMAL"), 1);
    E2E_ASSERT_EQ(bin.locationBySemantic("TEXCOORD0"), 2);
    E2E_ASSERT_EQ(bin.locationBySemantic("COLOR0"), -1);
    return true;
}

E2E_TEST(Tier1, FEAT_30, Transpile_SSBOToDataTextureRewrite) {
    FCT::RHI::ShaderBinary bin;
    FCT::TextureElement sceneDataTex{"s_GpuSceneData"};
    bin.addTextureLocation(sceneDataTex, 0u, 4u);

    const auto loc = bin.getTextureLocation("s_GpuSceneData");
    E2E_ASSERT_EQ(loc.first, 0u);
    E2E_ASSERT_EQ(loc.second, 4u);
    return true;
}

// ============================================================================
// [FEAT-31] 运行时编译器剥离开关 (PRD 04 §3)
// ============================================================================

E2E_TEST(Tier1, FEAT_31, RuntimeStrip_DirectSourceLoading) {
    const std::string prebakedEs300 =
        "#version 300 es\n"
        "precision highp float;\n"
        "layout(location = 0) in vec3 a_Pos;\n"
        "void main() { gl_Position = vec4(a_Pos, 1.0); }\n";

    FCT::RHI::ShaderBinary bin;
    bin.code(std::vector<char>(prebakedEs300.begin(), prebakedEs300.end()));
    bin.location({{"POSITION", 0u}});

    const auto raw = bin.code();
    const std::string loaded(raw.begin(), raw.end());
    E2E_ASSERT_EQ(loaded.substr(0u, 15u), "#version 300 es");
    E2E_ASSERT_EQ(bin.locationBySemantic("POSITION"), 0);
    return true;
}

E2E_TEST(Tier1, FEAT_31, RuntimeStrip_OfflinePrebakedBinaryMatch) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.glslVersion = 300u;
    options.es = true;
    options.enable420Pack = false;

    const auto transpiledOr = compiler.transpileSpirvToGlslEs300(kValidSampleFragmentSpirv, options);
    E2E_ASSERT_STATUS_OK(transpiledOr);

    FCT::RHI::ShaderBinary prebakedBin;
    prebakedBin.code(std::vector<char>(transpiledOr->glslSource.begin(), transpiledOr->glslSource.end()));
    const auto stored = prebakedBin.code();
    E2E_ASSERT_EQ(stored.size(), transpiledOr->glslSource.size());
    E2E_ASSERT_EQ(std::string_view(stored.data(), 15u), "#version 300 es");
    return true;
}

E2E_TEST(Tier1, FEAT_31, RuntimeStrip_ShadercDisabledStatus) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    const std::vector<uint32_t> emptySpirv;

    const auto badOr = compiler.transpileSpirvToGlslEs300(emptySpirv, options);
    E2E_ASSERT_FALSE(badOr.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_31, RuntimeStrip_ParallelCompilationFlag) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_TraditionalPipeline pipeline(&wgl2Ctx);
    E2E_ASSERT_STATUS_OK(pipeline.create());
    E2E_ASSERT_NE(pipeline.getProgramHandle(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_31, RuntimeStrip_ZeroShadercDependency) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.enable420Pack = false;

    const auto resOr = compiler.transpileSpirvToGlslEs300(kValidSampleFragmentSpirv, options);
    E2E_ASSERT_STATUS_OK(resOr);
    E2E_ASSERT_EQ(resOr->glslSource.find("layout(binding"), std::string::npos);
    return true;
}

// ============================================================================
// [FEAT-32] 瞬态资源池生命周期区间复用 (PRD 05 §1.1)
// ============================================================================

E2E_TEST(Tier1, FEAT_32, TransientPool_IdenticalSpecIntervalReuse) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 1024u;
    spec.height = 1024u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;
    spec.isSampled = true;

    FCT::Image* imgA = pool.acquire(spec, 0u, 1u, 1u);
    FCT::Image* imgB = pool.acquire(spec, 2u, 3u, 1u);

    E2E_ASSERT_NE(imgA, nullptr);
    E2E_ASSERT_EQ(imgA, imgB);
    E2E_ASSERT_EQ(pool.getStats().physicalAllocations, 1u);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_32, TransientPool_OverlappingIntervalCreatesNew) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 1024u;
    spec.height = 1024u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;

    FCT::Image* imgA = pool.acquire(spec, 0u, 2u, 1u);
    FCT::Image* imgB = pool.acquire(spec, 1u, 3u, 1u);

    E2E_ASSERT_NE(imgA, imgB);
    E2E_ASSERT_EQ(pool.getStats().physicalAllocations, 2u);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_32, TransientPool_ReuseRateThreshold60Percent) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 750u;
    spec.height = 1334u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;

    for (uint32_t step = 0u; step < 5u; ++step) {
        (void)pool.acquire(spec, step * 2u, step * 2u + 1u, 1u);
    }

    const auto stats = pool.getStats();
    E2E_ASSERT_EQ(stats.physicalAllocations, 1u);
    E2E_ASSERT_EQ(stats.reuseHits, 4u);
    E2E_ASSERT_GE(stats.getReuseRate(), 0.60f);
    return true;
}

E2E_TEST(Tier1, FEAT_32, TransientPool_TTLAging120Frames) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 512u;
    spec.height = 512u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;

    (void)pool.acquire(spec, 0u, 1u, 10u);
    E2E_ASSERT_EQ(pool.getPoolSize(), 1u);

    pool.tick(130u);
    E2E_ASSERT_EQ(pool.getPoolSize(), 1u);

    pool.tick(131u);
    E2E_ASSERT_EQ(pool.getPoolSize(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_32, TransientPool_SpecMismatchAllocatesDistinct) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec fullSpec{};
    fullSpec.width = 1024u;
    fullSpec.height = 1024u;
    fullSpec.format = FCT::Format::R8G8B8A8_UNORM;

    FCT::TransientResourceSpec halfSpec = fullSpec;
    halfSpec.width = 512u;
    halfSpec.height = 512u;

    FCT::Image* imgFull = pool.acquire(fullSpec, 0u, 1u, 1u);
    FCT::Image* imgHalf = pool.acquire(halfSpec, 2u, 3u, 1u);

    E2E_ASSERT_NE(imgFull, imgHalf);
    E2E_ASSERT_EQ(pool.getPoolSize(), 2u);
    return true;
}

// ============================================================================
// [FEAT-33] 无采样出边附件降级 Renderbuffer (PRD 05 §1.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_33, Renderbuffer_DowngradeFlagTrue) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 750u;
    spec.height = 1334u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;
    spec.isSampled = false;
    spec.allowRenderbufferDowngrade = true;

    FCT::Image* img = pool.acquire(spec, 0u, 1u, 1u);
    E2E_ASSERT_NE(img, nullptr);
    E2E_ASSERT_TRUE(img->isRenderbuffer());
    return true;
}

E2E_TEST(Tier1, FEAT_33, Renderbuffer_SampledAttachmentNoDowngrade) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 750u;
    spec.height = 1334u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;
    spec.isSampled = true;
    spec.allowRenderbufferDowngrade = true;

    FCT::Image* img = pool.acquire(spec, 0u, 1u, 1u);
    E2E_ASSERT_NE(img, nullptr);
    E2E_ASSERT_FALSE(img->isRenderbuffer());
    return true;
}

E2E_TEST(Tier1, FEAT_33, Renderbuffer_DepthStencilAttachmentDowngrade) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 750u;
    spec.height = 1334u;
    spec.format = FCT::Format::D24_UNORM_S8_UINT;
    spec.isDepthStencil = true;
    spec.isSampled = false;
    spec.allowRenderbufferDowngrade = true;

    FCT::Image* img = pool.acquire(spec, 0u, 2u, 1u);
    E2E_ASSERT_NE(img, nullptr);
    E2E_ASSERT_TRUE(img->isRenderbuffer());
    return true;
}

E2E_TEST(Tier1, FEAT_33, Renderbuffer_MsaaRenderbufferAllocation) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 750u;
    spec.height = 1334u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;
    spec.samples = FCT::Samples::sample_4;
    spec.isSampled = false;
    spec.allowRenderbufferDowngrade = true;

    FCT::Image* img = pool.acquire(spec, 0u, 1u, 1u);
    E2E_ASSERT_NE(img, nullptr);
    E2E_ASSERT_TRUE(img->isRenderbuffer());
    E2E_ASSERT_EQ(img->samples(), FCT::Samples::sample_4);
    return true;
}

E2E_TEST(Tier1, FEAT_33, Renderbuffer_ReuseBetweenCompatibleRenderbuffers) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec rboSpec{};
    rboSpec.width = 750u;
    rboSpec.height = 1334u;
    rboSpec.format = FCT::Format::D24_UNORM_S8_UINT;
    rboSpec.isDepthStencil = true;
    rboSpec.isSampled = false;
    rboSpec.allowRenderbufferDowngrade = true;

    FCT::Image* rbo1 = pool.acquire(rboSpec, 0u, 1u, 1u);
    FCT::Image* rbo2 = pool.acquire(rboSpec, 2u, 3u, 1u);
    E2E_ASSERT_EQ(rbo1, rbo2);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 1u);

    FCT::TransientResourceSpec sampledSpec = rboSpec;
    sampledSpec.isSampled = true;
    FCT::Image* tex3 = pool.acquire(sampledSpec, 4u, 5u, 1u);
    E2E_ASSERT_NE(rbo1, tex3);
    E2E_ASSERT_FALSE(tex3->isRenderbuffer());
    return true;
}

// ============================================================================
// [FEAT-34] TBDR Invalidate 指令自动推导 (PRD 05 §2)
// ============================================================================

E2E_TEST(Tier1, FEAT_34, TBDR_InvalidatePlan_BeginDiscards) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_PassGroup group(&wgl2Ctx);

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = false;
    plan.beginDiscards = {GL_COLOR_ATTACHMENT0};
    group.setTBDRInvalidatePlan(plan);

    E2E_ASSERT_EQ(group.getTBDRInvalidatePlan().beginDiscards.size(), 1u);
    E2E_ASSERT_EQ(group.getTBDRInvalidatePlan().beginDiscards[0], static_cast<GLenum>(GL_COLOR_ATTACHMENT0));
    return true;
}

E2E_TEST(Tier1, FEAT_34, TBDR_InvalidatePlan_EndDepthDiscard) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_PassGroup group(&wgl2Ctx);

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = false;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT};
    group.setTBDRInvalidatePlan(plan);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    group.beginSubmit(&cmdBuf);
    group.endSubmit(&cmdBuf);
    E2E_ASSERT_EQ(group.getTBDRInvalidatePlan().endDiscards.size(), 1u);
    E2E_ASSERT_EQ(group.getTBDRInvalidatePlan().endDiscards[0], static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    return true;
}

E2E_TEST(Tier1, FEAT_34, TBDR_InvalidatePlan_PreserveExportTarget) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_PassGroup presentGroup(&wgl2Ctx);

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = true;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT};
    presentGroup.setTBDRInvalidatePlan(plan);

    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        presentGroup.getTBDRInvalidatePlan().endDiscards, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_NE(mapped[0], static_cast<GLenum>(GL_COLOR));
    return true;
}

E2E_TEST(Tier1, FEAT_34, TBDR_InvalidatePlan_MultiPassSequence) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_PassGroup group(&wgl2Ctx);
    FCT::RHI::WGL2_Pass pass0(&wgl2Ctx);
    FCT::RHI::WGL2_Pass pass1(&wgl2Ctx);
    group.addPass(&pass0);
    group.addPass(&pass1);
    group.create();

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = true;
    plan.beginDiscards = {GL_DEPTH_ATTACHMENT};
    plan.endDiscards = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    group.setTBDRInvalidatePlan(plan);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    group.beginSubmit(&cmdBuf);
    group.nextPass(&cmdBuf);
    group.endSubmit(&cmdBuf);
    cmdBuf.end();

    E2E_ASSERT_EQ(group.getTBDRInvalidatePlan().endDiscards.size(), 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_34, TBDR_EmitInvalidate_DirectCallValidation) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    (void)wgl2Ctx;

    FCT::RHI::WGL2_PassGroup::emitInvalidate({GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT}, false);
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT}, false);
    E2E_ASSERT_EQ(mapped.size(), 2u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    return true;
}

// ============================================================================
// [FEAT-35] 默认 FBO 0 附件枚举映射 (PRD 05 §2)
// ============================================================================

E2E_TEST(Tier1, FEAT_35, FBO0_MapColorAttachment0ToColor) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_COLOR_ATTACHMENT0}, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_COLOR));
    return true;
}

E2E_TEST(Tier1, FEAT_35, FBO0_MapDepthAttachmentToDepth) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_DEPTH_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    return true;
}

E2E_TEST(Tier1, FEAT_35, FBO0_MapStencilAttachmentToStencil) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_STENCIL_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_STENCIL));
    return true;
}

E2E_TEST(Tier1, FEAT_35, FBO0_MapDepthStencilToBothDepthAndStencil) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_DEPTH_STENCIL_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 2u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_EQ(mapped[1], static_cast<GLenum>(GL_STENCIL));
    return true;
}

E2E_TEST(Tier1, FEAT_35, FBO0_OffscreenFboPreservesRawEnums) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate({GL_COLOR_ATTACHMENT0}, false);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_COLOR_ATTACHMENT0));
    return true;
}

// ============================================================================
// [FEAT-36] 相邻兼容 Pass 自动合并 (PRD 05 §3)
// ============================================================================

E2E_TEST(Tier1, FEAT_36, PassMerge_IdenticalOutputsMerged) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge targetA;
    targetA.toImage = "SceneColor";
    FCT::TargetEdge targetB;
    targetB.toImage = "SceneColor";

    FCT::RenderGraphPassNode passA("OpaquePass");
    passA.addOutgoingEdge(&targetA);
    passA.applyPassDesc(makeColorPassDescWithClear("OpaquePass", true));

    FCT::RenderGraphPassNode passB("TransparentPass");
    passB.addOutgoingEdge(&targetB);
    passB.applyPassDesc(makeColorPassDescWithClear("TransparentPass", false));

    E2E_ASSERT_TRUE(graph.canMergePasses(passA, passB));
    return true;
}

E2E_TEST(Tier1, FEAT_36, PassMerge_SamplingDependencyPreventsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge targetA;
    targetA.toImage = "SceneHdr";
    FCT::TargetEdge targetB;
    targetB.toImage = "SceneHdr";
    FCT::TextureEdge sampleEdge;
    sampleEdge.fromImage = "SceneHdr";

    FCT::RenderGraphPassNode passA("ForwardPass");
    passA.addOutgoingEdge(&targetA);

    FCT::RenderGraphPassNode passB("PostReadPass");
    passB.addOutgoingEdge(&targetB);
    passB.addIncomingEdge(&sampleEdge);

    E2E_ASSERT_FALSE(graph.canMergePasses(passA, passB));
    return true;
}

E2E_TEST(Tier1, FEAT_36, PassMerge_ForceClearSubsequentPreventsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge targetA;
    targetA.toImage = "SceneColor";
    FCT::TargetEdge targetB;
    targetB.toImage = "SceneColor";

    FCT::RenderGraphPassNode passA("BasePass");
    passA.addOutgoingEdge(&targetA);

    FCT::RenderGraphPassNode passB("ClearingOverlayPass");
    passB.addOutgoingEdge(&targetB);
    passB.applyPassDesc(makeColorPassDescWithClear("ClearingOverlayPass", true));

    E2E_ASSERT_FALSE(graph.canMergePasses(passA, passB));
    return true;
}

E2E_TEST(Tier1, FEAT_36, PassMerge_ThreeSequentialPasses) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge t0, t1, t2;
    t0.toImage = "MainLdr";
    t1.toImage = "MainLdr";
    t2.toImage = "MainLdr";

    FCT::DepthStencilEdge d0, d1, d2;
    d0.toImage = "MainDepth";
    d1.toImage = "MainDepth";
    d2.toImage = "MainDepth";

    FCT::RenderGraphPassNode opaque("OpaquePass");
    opaque.addOutgoingEdge(&t0);
    opaque.addOutgoingEdge(&d0);
    opaque.applyPassDesc(makeColorPassDescWithClear("OpaquePass", true));

    FCT::RenderGraphPassNode skybox("SkyboxPass");
    skybox.addOutgoingEdge(&t1);
    skybox.addOutgoingEdge(&d1);

    FCT::RenderGraphPassNode uiOverlay("UiOverlayPass");
    uiOverlay.addOutgoingEdge(&t2);
    uiOverlay.addOutgoingEdge(&d2);

    E2E_ASSERT_TRUE(graph.canMergePasses(opaque, skybox));
    E2E_ASSERT_TRUE(graph.canMergePasses(skybox, uiOverlay));

    FCT::TextureEdge hazardEdge;
    hazardEdge.fromImage = "MainLdr";
    uiOverlay.addIncomingEdge(&hazardEdge);
    E2E_ASSERT_FALSE(graph.canMergePasses(skybox, uiOverlay));
    return true;
}

E2E_TEST(Tier1, FEAT_36, PassMerge_DifferentTargetCountPreventsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge tA0, tB0, tB1;
    tA0.toImage = "Color0";
    tB0.toImage = "Color0";
    tB1.toImage = "Color1";

    FCT::RenderGraphPassNode singleTargetPass("SingleTarget");
    singleTargetPass.addOutgoingEdge(&tA0);

    FCT::RenderGraphPassNode mrtPass("MrtPass");
    mrtPass.addOutgoingEdge(&tB0);
    mrtPass.addOutgoingEdge(&tB1);

    E2E_ASSERT_FALSE(graph.canMergePasses(singleTargetPass, mrtPass));
    return true;
}

// ============================================================================
// [FEAT-37] PassGroup FBO 哈希缓存 (PRD 05 §3.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_37, FboCache_ComputeHashDeterministic) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.createFromExisting(101u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image depthImg(&wgl2Ctx);
    depthImg.createFromExisting(202u, GL_RENDERBUFFER, true);

    const uint64_t h1 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    const uint64_t h2 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    E2E_ASSERT_EQ(h1, h2);
    return true;
}

E2E_TEST(Tier1, FEAT_37, FboCache_DistinctAttachmentsDistinctHash) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image colorA(&wgl2Ctx);
    colorA.createFromExisting(101u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image colorB(&wgl2Ctx);
    colorB.createFromExisting(102u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image depth(&wgl2Ctx);
    depth.createFromExisting(202u, GL_RENDERBUFFER, true);

    const uint64_t h1 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorA}, &depth);
    const uint64_t h2 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorB}, &depth);
    E2E_ASSERT_NE(h1, h2);
    return true;
}

E2E_TEST(Tier1, FEAT_37, FboCache_GetOrCreateReturnsCachedHandle) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    glGenFramebuffers = stubGenNonZeroFramebuffers;

    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.format(FCT::Format::R8G8B8A8_UNORM);
    colorImg.createFromExisting(301u, GL_TEXTURE_2D, false);

    FCT::RHI::WGL2_Image depthImg(&wgl2Ctx);
    depthImg.format(FCT::Format::D24_UNORM_S8_UINT);
    depthImg.createFromExisting(302u, GL_RENDERBUFFER, true);

    FCT::RHI::WGL2_PassGroup passGroup(&wgl2Ctx);
    const uint64_t key = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    const GLuint fboFirst = passGroup.getOrCreateFBO(key, {&colorImg}, &depthImg);
    const GLuint fboSecond = passGroup.getOrCreateFBO(key, {&colorImg}, &depthImg);

    E2E_ASSERT_NE(fboFirst, 0u);
    E2E_ASSERT_EQ(fboFirst, fboSecond);
    return true;
}

E2E_TEST(Tier1, FEAT_37, FboCache_DepthOnlyFramebufferHash) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image depthOnly(&wgl2Ctx);
    depthOnly.format(FCT::Format::D32_SFLOAT);
    depthOnly.createFromExisting(401u, GL_TEXTURE_2D, false);

    const uint64_t hDepthOnly = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({}, &depthOnly);
    const uint64_t hEmpty = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({}, nullptr);
    E2E_ASSERT_NE(hDepthOnly, hEmpty);
    return true;
}

E2E_TEST(Tier1, FEAT_37, FboCache_DestructorDeletesCachedFBOs) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    glGenFramebuffers = stubGenNonZeroFramebuffers;

    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.format(FCT::Format::R8G8B8A8_UNORM);
    colorImg.createFromExisting(501u, GL_TEXTURE_2D, false);

    GLuint allocatedFbo = 0u;
    {
        FCT::RHI::WGL2_PassGroup scopedGroup(&wgl2Ctx);
        const uint64_t key = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, nullptr);
        allocatedFbo = scopedGroup.getOrCreateFBO(key, {&colorImg}, nullptr);
        E2E_ASSERT_NE(allocatedFbo, 0u);
    }

    FCT::RHI::WGL2_PassGroup nextGroup(&wgl2Ctx);
    const uint64_t key = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, nullptr);
    const GLuint freshFbo = nextGroup.getOrCreateFBO(key, {&colorImg}, nullptr);
    E2E_ASSERT_NE(freshFbo, allocatedFbo);
    return true;
}

} // namespace MQEngine::Testing
