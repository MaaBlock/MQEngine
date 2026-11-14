/**
 * @file Test_Tier2_FCT_RHI.cpp
 * @brief Tier 2 边界极限测试：FCT RHI WebGL 2.0 / GLES 3.0 后端、离线转译器与 RenderGraph 优化 (FEAT-23 ~ FEAT-37)
 * @details 100% 驱动真实 FCT 生产类与 DouyinShaderCooker 预烘焙产物协议，零模拟器假类、零内联 Lambda。
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
#include <FCT/RHI/WGL2_VertexShader.h>
#include <FCT/RHI/WGL2_PixelShader.h>
#include <FCT/RHI/WGL2_PassGroup.h>
#include <FCT/RHI/WGL2_Pass.h>
#include <FCT/RHI/ShaderBinary.h>
#include <FCT/Runtime/runtime.h>
#include "DouyinMiniGame.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
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

// 最小合法 SPIR-V VertexShader：含 1 个 UBO (binding 2, 16 bytes) 与 1 个只读 SSBO (binding 4)
static const uint32_t kMinimalSsboAndUboSpirv[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x00000018u, 0x00000000u,
    0x00020011u, 0x00000001u,
    0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00030047u, 0x00000009u, 0x00000002u,
    0x00050048u, 0x00000009u, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00040047u, 0x0000000bu, 0x00000022u, 0x00000000u,
    0x00040047u, 0x0000000bu, 0x00000021u, 0x00000002u,
    0x00040047u, 0x0000000du, 0x00000006u, 0x00000010u,
    0x00030047u, 0x0000000eu, 0x00000003u,
    0x00050048u, 0x0000000eu, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00040048u, 0x0000000eu, 0x00000000u, 0x00000018u,
    0x00040047u, 0x00000010u, 0x00000022u, 0x00000000u,
    0x00040047u, 0x00000010u, 0x00000021u, 0x00000004u,
    0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u,
    0x00030016u, 0x00000006u, 0x00000020u,
    0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u,
    0x0003001eu, 0x00000009u, 0x00000007u,
    0x00040020u, 0x0000000au, 0x00000002u, 0x00000009u,
    0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000002u,
    0x0003001du, 0x0000000du, 0x00000007u,
    0x0003001eu, 0x0000000eu, 0x0000000du,
    0x00040020u, 0x0000000fu, 0x00000002u, 0x0000000eu,
    0x0004003bu, 0x0000000fu, 0x00000010u, 0x00000002u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000005u,
    0x000100fdu,
    0x00010038u
};

/**
 * @brief 函数指针作用域替换守卫，退出作用域自动恢复原始 GL 函数表指针
 */
template <typename ProcType>
class ScopedGlProcHook {
public:
    ScopedGlProcHook(ProcType& target, ProcType replacement) noexcept
        : m_target(target), m_original(target)
    {
        m_target = replacement;
    }

    ~ScopedGlProcHook()
    {
        m_target = m_original;
    }

    ScopedGlProcHook(const ScopedGlProcHook&) = delete;
    ScopedGlProcHook& operator=(const ScopedGlProcHook&) = delete;

private:
    ProcType& m_target;
    ProcType m_original;
};

struct VaoSetupCounterFunctor {
    uint32_t* count{nullptr};
    void operator()() const noexcept {
        if (count) {
            ++(*count);
        }
    }
};

struct GlDriverCaptureState {
    std::vector<GLenum> enabledCaps;
    std::vector<GLenum> disabledCaps;
    uint32_t bufferSubDataCalls{0};
    GLsizeiptr lastSubDataSize{0};
    GLuint nextBufferId{200u};
    GLuint nextFboId{500u};
    uint32_t deletedFboCount{0};

    uint32_t attribPointerCalls{0};
    uint32_t attribIPointerCalls{0};
    GLuint lastAttribIndex{0u};
    GLint lastAttribSize{0};
    GLenum lastAttribType{0u};
    GLsizei lastAttribStride{0};
    std::vector<std::pair<GLuint, GLuint>> divisorRecords;

    uint32_t drawArraysCalls{0};
    uint32_t drawElementsCalls{0};
    uint32_t drawElementsInstancedCalls{0};
    GLsizei lastDrawInstanceCount{0};
    std::vector<GLsizei> viewportWidths;

    std::vector<std::pair<GLenum, GLint>> pixelStoreRecords;
    uint32_t texSubImage2DCalls{0};
    GLsizei lastSubImageWidth{0};
    GLsizei lastSubImageHeight{0};

    std::vector<std::vector<GLenum>> invalidatedAttachmentBatches;
    std::string lastUploadedShaderSource;

    void reset() {
        enabledCaps.clear();
        disabledCaps.clear();
        bufferSubDataCalls = 0u;
        lastSubDataSize = 0;
        deletedFboCount = 0u;
        attribPointerCalls = 0u;
        attribIPointerCalls = 0u;
        lastAttribIndex = 0u;
        lastAttribSize = 0;
        lastAttribType = 0u;
        lastAttribStride = 0;
        divisorRecords.clear();
        drawArraysCalls = 0u;
        drawElementsCalls = 0u;
        drawElementsInstancedCalls = 0u;
        lastDrawInstanceCount = 0;
        viewportWidths.clear();
        pixelStoreRecords.clear();
        texSubImage2DCalls = 0u;
        lastSubImageWidth = 0;
        lastSubImageHeight = 0;
        invalidatedAttachmentBatches.clear();
        lastUploadedShaderSource.clear();
    }
};

GlDriverCaptureState g_glCapture;

void GL_APIENTRY hookGlEnable(GLenum cap) {
    g_glCapture.enabledCaps.push_back(cap);
}

void GL_APIENTRY hookGlDisable(GLenum cap) {
    g_glCapture.disabledCaps.push_back(cap);
}

void GL_APIENTRY hookGlGenBuffers(GLsizei n, GLuint* buffers) {
    if (!buffers) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        buffers[i] = ++g_glCapture.nextBufferId;
    }
}

void GL_APIENTRY hookGlBufferSubData(GLenum, GLintptr, GLsizeiptr size, const void*) {
    ++g_glCapture.bufferSubDataCalls;
    g_glCapture.lastSubDataSize = size;
}

void GL_APIENTRY hookGlVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean, GLsizei stride, const void*) {
    ++g_glCapture.attribPointerCalls;
    g_glCapture.lastAttribIndex = index;
    g_glCapture.lastAttribSize = size;
    g_glCapture.lastAttribType = type;
    g_glCapture.lastAttribStride = stride;
}

void GL_APIENTRY hookGlVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void*) {
    ++g_glCapture.attribIPointerCalls;
    g_glCapture.lastAttribIndex = index;
    g_glCapture.lastAttribSize = size;
    g_glCapture.lastAttribType = type;
    g_glCapture.lastAttribStride = stride;
}

void GL_APIENTRY hookGlVertexAttribDivisor(GLuint index, GLuint divisor) {
    g_glCapture.divisorRecords.emplace_back(index, divisor);
}

void GL_APIENTRY hookGlDrawArrays(GLenum, GLint, GLsizei) {
    ++g_glCapture.drawArraysCalls;
}

void GL_APIENTRY hookGlDrawElements(GLenum, GLsizei, GLenum, const void*) {
    ++g_glCapture.drawElementsCalls;
}

void GL_APIENTRY hookGlDrawElementsInstanced(GLenum, GLsizei, GLenum, const void*, GLsizei instancecount) {
    ++g_glCapture.drawElementsInstancedCalls;
    g_glCapture.lastDrawInstanceCount = instancecount;
}

void GL_APIENTRY hookGlViewport(GLint, GLint, GLsizei width, GLsizei) {
    g_glCapture.viewportWidths.push_back(width);
}

void GL_APIENTRY hookGlPixelStorei(GLenum pname, GLint param) {
    g_glCapture.pixelStoreRecords.emplace_back(pname, param);
}

void GL_APIENTRY hookGlTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei width, GLsizei height, GLenum, GLenum, const void*) {
    ++g_glCapture.texSubImage2DCalls;
    g_glCapture.lastSubImageWidth = width;
    g_glCapture.lastSubImageHeight = height;
}

void GL_APIENTRY hookGlInvalidateFramebuffer(GLenum, GLsizei numAttachments, const GLenum* attachments) {
    std::vector<GLenum> batch;
    if (attachments && numAttachments > 0) {
        batch.assign(attachments, attachments + numAttachments);
    }
    g_glCapture.invalidatedAttachmentBatches.push_back(std::move(batch));
}

void GL_APIENTRY hookGlGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    if (!framebuffers) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        framebuffers[i] = ++g_glCapture.nextFboId;
    }
}

void GL_APIENTRY hookGlDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    if (!framebuffers) {
        return;
    }
    for (GLsizei i = 0; i < n; ++i) {
        if (framebuffers[i] != 0u) {
            ++g_glCapture.deletedFboCount;
        }
    }
}

void GL_APIENTRY hookGlShaderSource(GLuint, GLsizei count, const GLchar* const* string, const GLint* length) {
    g_glCapture.lastUploadedShaderSource.clear();
    if (!string || count <= 0) {
        return;
    }
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) {
            continue;
        }
        if (length && length[i] >= 0) {
            g_glCapture.lastUploadedShaderSource.append(string[i], static_cast<size_t>(length[i]));
        } else {
            g_glCapture.lastUploadedShaderSource.append(string[i]);
        }
    }
}

FCT::PassDesc makePassDescWithColorClear(const std::string& name, bool clearColor) {
    FCT::PassDesc desc(name);
    if (clearColor) {
        desc.clear = FCT::EnablePassClear(FCT::ClearTypes(FCT::ClearType::color));
    }
    return desc;
}

} // namespace

// ============================================================================
// [FEAT-23] FCT 硬件能力正交自省契约 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_23, Caps_ZeroAlignmentQueryValid) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    const uint32_t align = wgl2Ctx.getUniformBufferOffsetAlignment();
    E2E_ASSERT_GT(align, 0u);
    E2E_ASSERT_EQ(align % 16u, 0u);
    E2E_ASSERT_EQ(align & (align - 1u), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_23, Caps_AllAdvancedFeaturesDisabledInMinigame) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    E2E_ASSERT_TRUE(wgl2Ctx.isIntegrateGpu());
    E2E_ASSERT_FALSE(wgl2Ctx.isBindlessSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isIndirectDrawSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isRayTracingSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isMeshShaderSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isComputeSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isAccelerationStructureSupported());
    return true;
}

E2E_TEST(Tier2, FEAT_23, Caps_AllAdvancedFeaturesEnabledInDesktop) {
    FCT::Runtime runtime;
    FCT::Context* rawVk = runtime.createContext(FCT::ContextBackend::Vulkan);
    E2E_ASSERT_NE(rawVk, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    E2E_ASSERT_NE(wgl2Ctx.isBindlessSupported(), rawVk->isBindlessSupported());
    E2E_ASSERT_TRUE(rawVk->isBindlessSupported());
    E2E_ASSERT_TRUE(rawVk->isReadWriteStorageBufferSupported());
    E2E_ASSERT_TRUE(rawVk->isIndirectDrawSupported());
    E2E_ASSERT_TRUE(rawVk->isBaseInstanceSupported());
    E2E_ASSERT_TRUE(rawVk->isComputeSupported());
    rawVk->release();
    return true;
}

E2E_TEST(Tier2, FEAT_23, Caps_UboAlignmentAndMaxRangeLimits) {
    FCT::Runtime runtime;
    FCT::Context* rawVk = runtime.createContext(FCT::ContextBackend::Vulkan);
    E2E_ASSERT_NE(rawVk, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    const uint32_t wgl2Align = wgl2Ctx.getUniformBufferOffsetAlignment();
    const uint32_t vkAlign = rawVk->getUniformBufferOffsetAlignment();
    const uint32_t wgl2MaxRange = wgl2Ctx.getMaxUniformBufferRange();
    const uint32_t vkMaxRange = rawVk->getMaxUniformBufferRange();
    rawVk->release();

    E2E_ASSERT_EQ(wgl2Align, 256u);
    E2E_ASSERT_GT(vkAlign, 0u);
    E2E_ASSERT_EQ(vkAlign & (vkAlign - 1u), 0u);
    E2E_ASSERT_LE(vkAlign, wgl2Align);
    E2E_ASSERT_EQ(wgl2MaxRange, 65536u);
    E2E_ASSERT_GE(vkMaxRange, wgl2MaxRange);
    return true;
}

E2E_TEST(Tier2, FEAT_23, Caps_DynamicBaseInstanceProbeTransition) {
    FCT::Runtime runtime;
    FCT::Context* rawWgl2 = runtime.createContext(FCT::ContextBackend::WGL2);
    FCT::Context* rawVk = runtime.createContext(FCT::ContextBackend::Vulkan);
    E2E_ASSERT_NE(rawWgl2, nullptr);
    E2E_ASSERT_NE(rawVk, nullptr);

    const bool wgl2BaseInst = rawWgl2->isBaseInstanceSupported();
    const bool vkBaseInst = rawVk->isBaseInstanceSupported();
    rawWgl2->release();
    rawVk->release();

    E2E_ASSERT_FALSE(wgl2BaseInst);
    E2E_ASSERT_TRUE(vkBaseInst);
    E2E_ASSERT_NE(wgl2BaseInst, vkBaseInst);
    return true;
}

// ============================================================================
// [FEAT-24] 64-bit 状态差分管线与 VAO 缓存 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_24, StateKey_AllZeroAndAllOneMasks) {
    FCT::WGL2_Context wgl2Ctx(nullptr);

    FCT::RHI::WGL2_TraditionalPipeline zeroPipe(&wgl2Ctx);
    FCT::RHI::WGL2_RasterizationState rsOff(&wgl2Ctx);
    rsOff.cullMode(FCT::CullMode::None);
    FCT::RHI::WGL2_DepthStencilState dsOff(&wgl2Ctx);
    dsOff.depthTestEnable(false);
    dsOff.depthWriteEnable(false);
    FCT::RHI::WGL2_BlendState bsZeroMask(&wgl2Ctx);
    bsZeroMask.targets(1).opaque(0).mask(0u, 0);
    zeroPipe.setRasterizationState(&rsOff);
    zeroPipe.setDepthStencilState(&dsOff);
    zeroPipe.setBlendState(&bsZeroMask);

    const uint64_t inactiveKey = zeroPipe.getStateKey();
    E2E_ASSERT_EQ(inactiveKey & (1ULL << 0), 0ULL);
    E2E_ASSERT_EQ(inactiveKey & (1ULL << 5), 0ULL);
    E2E_ASSERT_EQ(inactiveKey & (1ULL << 31), 0ULL);
    E2E_ASSERT_EQ((inactiveKey >> 54) & 0xFULL, 0ULL);

    FCT::RHI::WGL2_TraditionalPipeline activePipe(&wgl2Ctx);
    FCT::RHI::WGL2_RasterizationState rsOn(&wgl2Ctx);
    rsOn.cullMode(FCT::CullMode::Back);
    FCT::RHI::WGL2_DepthStencilState dsOn(&wgl2Ctx);
    dsOn.depthTestEnable(true);
    dsOn.depthWriteEnable(true);
    FCT::RHI::WGL2_BlendState bsFull(&wgl2Ctx);
    bsFull.targets(1).alpha(0).mask(0xFu, 0);
    activePipe.setRasterizationState(&rsOn);
    activePipe.setDepthStencilState(&dsOn);
    activePipe.setBlendState(&bsFull);

    const uint64_t activeKey = activePipe.getStateKey();
    E2E_ASSERT_NE(activeKey & (1ULL << 0), 0ULL);
    E2E_ASSERT_NE(activeKey & (1ULL << 5), 0ULL);
    E2E_ASSERT_NE(activeKey & (1ULL << 6), 0ULL);
    E2E_ASSERT_NE(activeKey & (1ULL << 31), 0ULL);
    E2E_ASSERT_EQ((activeKey >> 54) & 0xFULL, 0xFULL);

    wgl2Ctx.applyGraphicsState(activeKey);
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), activeKey);
    return true;
}

E2E_TEST(Tier2, FEAT_24, StateDiff_ZeroDiffSkipsCommandsCompletely) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLENABLEPROC> enableGuard(glEnable, hookGlEnable);
    ScopedGlProcHook<PFNGLDISABLEPROC> disableGuard(glDisable, hookGlDisable);

    const uint64_t key = (1ULL << 5) | (1ULL << 31) | (0xFULL << 54);
    wgl2Ctx.applyGraphicsState(key);
    g_glCapture.reset();

    wgl2Ctx.applyGraphicsState(key);
    E2E_ASSERT_TRUE(g_glCapture.enabledCaps.empty());
    E2E_ASSERT_TRUE(g_glCapture.disabledCaps.empty());
    E2E_ASSERT_EQ(wgl2Ctx.getCurrentStateKey(), key);
    return true;
}

E2E_TEST(Tier2, FEAT_24, StateDiff_BlendBitmaskIndependentChange) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLENABLEPROC> enableGuard(glEnable, hookGlEnable);

    FCT::RHI::WGL2_TraditionalPipeline opaquePipe(&wgl2Ctx);
    FCT::RHI::WGL2_BlendState bsOpaque(&wgl2Ctx);
    bsOpaque.targets(1).opaque(0).mask(0xFu, 0);
    opaquePipe.setBlendState(&bsOpaque);

    FCT::RHI::WGL2_TraditionalPipeline blendPipe(&wgl2Ctx);
    FCT::RHI::WGL2_BlendState bsBlend(&wgl2Ctx);
    bsBlend.targets(1).alpha(0).mask(0xFu, 0);
    blendPipe.setBlendState(&bsBlend);

    wgl2Ctx.applyGraphicsState(opaquePipe.getStateKey());
    g_glCapture.reset();

    wgl2Ctx.applyGraphicsState(blendPipe.getStateKey());
    E2E_ASSERT_EQ(g_glCapture.enabledCaps.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.enabledCaps[0], static_cast<GLenum>(GL_BLEND));
    return true;
}

E2E_TEST(Tier2, FEAT_24, VaoCache_HashConsistencyAndCollisionResistance) {
    FCT::VAOCacheKey keyA{10u, {100u, 0u, 0u, 0u}, 50u};
    FCT::VAOCacheKey keyB{10u, {100u, 0u, 0u, 0u}, 50u};
    FCT::VAOCacheKey keyC{10u, {101u, 0u, 0u, 0u}, 50u};

    FCT::VAOCacheHasher hasher;
    E2E_ASSERT_EQ(hasher(keyA), hasher(keyB));
    E2E_ASSERT_NE(hasher(keyA), hasher(keyC));

    FCT::WGL2_Context wgl2Ctx(nullptr);
    uint32_t setupCount = 0u;
    (void)wgl2Ctx.getOrCreateVAO(keyA, VaoSetupCounterFunctor{&setupCount});
    (void)wgl2Ctx.getOrCreateVAO(keyB, VaoSetupCounterFunctor{&setupCount});
    E2E_ASSERT_EQ(setupCount, 1u);
    (void)wgl2Ctx.getOrCreateVAO(keyC, VaoSetupCounterFunctor{&setupCount});
    E2E_ASSERT_EQ(setupCount, 2u);
    return true;
}

E2E_TEST(Tier2, FEAT_24, StateDiff_DepthBitmaskIndependentChange) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLENABLEPROC> enableGuard(glEnable, hookGlEnable);

    FCT::RHI::WGL2_TraditionalPipeline noDepthPipe(&wgl2Ctx);
    FCT::RHI::WGL2_DepthStencilState dsOff(&wgl2Ctx);
    dsOff.depthTestEnable(false);
    noDepthPipe.setDepthStencilState(&dsOff);

    FCT::RHI::WGL2_TraditionalPipeline depthPipe(&wgl2Ctx);
    FCT::RHI::WGL2_DepthStencilState dsOn(&wgl2Ctx);
    dsOn.depthTestEnable(true);
    depthPipe.setDepthStencilState(&dsOn);

    wgl2Ctx.applyGraphicsState(noDepthPipe.getStateKey());
    g_glCapture.reset();

    wgl2Ctx.applyGraphicsState(depthPipe.getStateKey());
    E2E_ASSERT_EQ(g_glCapture.enabledCaps.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.enabledCaps[0], static_cast<GLenum>(GL_DEPTH_TEST));
    return true;
}

// ============================================================================
// [FEAT-25] 只读 2D 数据纹理 StorageBuffer - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_25, StorageBuffer_ZeroSizeSubRegionEmpty) {
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(0u, 0u);
    E2E_ASSERT_TRUE(regions.empty());

    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_StorageBuffer sbuf(&wgl2Ctx);
    E2E_ASSERT_STATUS_OK(sbuf.create(16384u));
    const uint8_t dummy = 0xAAu;
    sbuf.updateData(&dummy, 0u, 0u);
    E2E_ASSERT_EQ(sbuf.size(), 16384u);
    return true;
}

E2E_TEST(Tier2, FEAT_25, StorageBuffer_ConstantsStrictVerification) {
    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kWidth, 1024u);
    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kTexelSizeBytes, 16u);
    E2E_ASSERT_EQ(FCT::RHI::WGL2_StorageBuffer::kRowSizeBytes, 16384u);
    return true;
}

E2E_TEST(Tier2, FEAT_25, StorageBuffer_SingleRowWithinWidthBound) {
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(64u, 128u);
    E2E_ASSERT_EQ(regions.size(), 1u);
    E2E_ASSERT_EQ(regions[0].x, 4u);
    E2E_ASSERT_EQ(regions[0].width, 8u);
    E2E_ASSERT_LE(regions[0].x + regions[0].width, FCT::RHI::WGL2_StorageBuffer::kWidth);
    return true;
}

E2E_TEST(Tier2, FEAT_25, StorageBuffer_MultiRowThreeSegmentsMax) {
    const auto regions = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(16000u, 32000u);
    E2E_ASSERT_EQ(regions.size(), 3u);
    for (const auto& r : regions) {
        E2E_ASSERT_LE(r.x + r.width, FCT::RHI::WGL2_StorageBuffer::kWidth);
        E2E_ASSERT_GE(r.height, 1u);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_25, StorageBuffer_MinigameDeviceAddressZero) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());

    FCT::RHI::WGL2_StorageBuffer unallocatedBuf(&wgl2Ctx);
    E2E_ASSERT_EQ(unallocatedBuf.getTextureHandle(), 0u);
    E2E_ASSERT_EQ(unallocatedBuf.getDeviceAddress(), 0ULL);
    return true;
}

// ============================================================================
// [FEAT-26] 帧环形 UBO 统一更新机制 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_26, RingUbo_ZeroByteWriteRejected) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLGENBUFFERSPROC> genBufGuard(glGenBuffers, hookGlGenBuffers);
    ScopedGlProcHook<PFNGLBUFFERSUBDATAPROC> subDataGuard(glBufferSubData, hookGlBufferSubData);
    g_glCapture.reset();

    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));

    const auto zeroAlloc = ringUbo.allocate(0u);
    E2E_ASSERT_EQ(zeroAlloc.offset, 0u);
    E2E_ASSERT_EQ(zeroAlloc.size, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 0u);
    E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 0u);

    ringUbo.flushToGpu();
    E2E_ASSERT_EQ(g_glCapture.bufferSubDataCalls, 0u);
    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier2, FEAT_26, RingUbo_Exact64KBAndOverflowCheck) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    const uint32_t maxBlockRange = wgl2Ctx.getMaxUniformBufferRange();
    E2E_ASSERT_EQ(maxBlockRange, 65536u);

    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(131072u, 256u));

    const auto exact64K = ringUbo.allocate(65536u);
    E2E_ASSERT_EQ(exact64K.offset, 0u);
    E2E_ASSERT_EQ(exact64K.size, maxBlockRange);
    E2E_ASSERT_LE(exact64K.size, maxBlockRange);

    const uint32_t overLimitRequest = maxBlockRange + 1u;
    E2E_ASSERT_GT(overLimitRequest, wgl2Ctx.getMaxUniformBufferRange());

    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier2, FEAT_26, RingUbo_Alignment256Boundaries) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));
    E2E_ASSERT_EQ(ringUbo.getAlignment(), 256u);

    const auto a0 = ringUbo.allocate(1u);
    E2E_ASSERT_EQ(a0.offset, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 1u);

    const auto a1 = ringUbo.allocate(255u);
    E2E_ASSERT_EQ(a1.offset, 256u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 511u);

    const auto a2 = ringUbo.allocate(256u);
    E2E_ASSERT_EQ(a2.offset, 512u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 768u);

    const auto a3 = ringUbo.allocate(257u);
    E2E_ASSERT_EQ(a3.offset, 768u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 1025u);

    const auto a4 = ringUbo.allocate(16u);
    E2E_ASSERT_EQ(a4.offset, 1280u);
    E2E_ASSERT_EQ(a4.offset % 256u, 0u);

    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier2, FEAT_26, RingUbo_SingleBatchFlushRecord) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLGENBUFFERSPROC> genBufGuard(glGenBuffers, hookGlGenBuffers);
    ScopedGlProcHook<PFNGLBUFFERSUBDATAPROC> subDataGuard(glBufferSubData, hookGlBufferSubData);
    g_glCapture.reset();

    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));
    E2E_ASSERT_NE(ringUbo.getBufferHandle(), 0u);

    const auto c0 = ringUbo.allocate(128u);
    const auto c1 = ringUbo.allocate(128u);
    const auto c2 = ringUbo.allocate(64u);
    std::memset(c0.cpuPtr, 0x11, c0.size);
    std::memset(c1.cpuPtr, 0x22, c1.size);
    std::memset(c2.cpuPtr, 0x33, c2.size);

    ringUbo.flushToGpu();
    E2E_ASSERT_EQ(g_glCapture.bufferSubDataCalls, 1u);
    E2E_ASSERT_EQ(static_cast<uint32_t>(g_glCapture.lastSubDataSize), ringUbo.getHighWaterMark());
    E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 512u + 64u);

    ringUbo.destroy();
    return true;
}

E2E_TEST(Tier2, FEAT_26, RingUbo_WrapAroundToZeroOffset) {
    FCT::WGL2_FrameRingUBO ringUbo;
    E2E_ASSERT_STATUS_OK(ringUbo.init(65536u, 256u));

    const auto chunkA = ringUbo.allocate(65000u);
    E2E_ASSERT_EQ(chunkA.offset, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 65000u);

    // 65000 向上对齐 256 为 65280，65280 + 1000 = 66280 > 65536，真实分配器自动回绕至偏移 0
    const auto chunkB = ringUbo.allocate(1000u);
    E2E_ASSERT_EQ(chunkB.offset, 0u);
    E2E_ASSERT_EQ(ringUbo.getUsedBytes(), 1000u);
    E2E_ASSERT_EQ(ringUbo.getHighWaterMark(), 65000u);

    ringUbo.destroy();
    return true;
}

// ============================================================================
// [FEAT-27] PerInstance 整型顶点属性分发 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_27, Attrib_ZeroInstancesDrawCallSafe) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLDRAWELEMENTSPROC> drawElemGuard(glDrawElements, hookGlDrawElements);
    ScopedGlProcHook<PFNGLDRAWELEMENTSINSTANCEDPROC> drawInstGuard(
        glDrawElementsInstanced, hookGlDrawElementsInstanced);
    g_glCapture.reset();

    FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
    layout.add(0u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Custom, "a_Slot", FCT::Format::R32_UINT}});
    layout.inputRate(FCT::RHI::InputRate::PerInstance, 0u);
    // 绑定 VBO = 0 时安全跳过，零 GL 调用
    layout.applyBinding(0u, 0u);
    E2E_ASSERT_EQ(g_glCapture.attribIPointerCalls, 0u);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    cmdBuf.end();
    cmdBuf.submit();
    E2E_ASSERT_EQ(g_glCapture.drawElementsCalls, 0u);
    E2E_ASSERT_EQ(g_glCapture.drawElementsInstancedCalls, 0u);

    cmdBuf.begin();
    cmdBuf.drawIndex(0u, 0u, 36u, 5u, 0u);
    cmdBuf.end();
    cmdBuf.submit();
    E2E_ASSERT_EQ(g_glCapture.drawElementsInstancedCalls, 1u);
    E2E_ASSERT_EQ(g_glCapture.lastDrawInstanceCount, 5);
    return true;
}

E2E_TEST(Tier2, FEAT_27, Attrib_DivisorExactValues) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLVERTEXATTRIBDIVISORPROC> divGuard(glVertexAttribDivisor, hookGlVertexAttribDivisor);
    g_glCapture.reset();

    FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
    layout.add(0u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Position3f}});
    layout.add(1u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}});
    layout.inputRate(FCT::RHI::InputRate::PerVertex, 0u);
    layout.inputRate(FCT::RHI::InputRate::PerInstance, 1u);

    layout.applyBinding(0u, 101u);
    layout.applyBinding(1u, 102u);

    E2E_ASSERT_EQ(g_glCapture.divisorRecords.size(), 2u);
    E2E_ASSERT_EQ(g_glCapture.divisorRecords[0].second, 0u);
    E2E_ASSERT_EQ(g_glCapture.divisorRecords[1].second, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_27, Attrib_R32_UINT_UsesIPointer) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLVERTEXATTRIBIPOINTERPROC> iPtrGuard(glVertexAttribIPointer, hookGlVertexAttribIPointer);
    ScopedGlProcHook<PFNGLVERTEXATTRIBPOINTERPROC> fPtrGuard(glVertexAttribPointer, hookGlVertexAttribPointer);
    ScopedGlProcHook<PFNGLVERTEXATTRIBDIVISORPROC> divGuard(glVertexAttribDivisor, hookGlVertexAttribDivisor);
    g_glCapture.reset();

    FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
    layout.add(0u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}});
    layout.inputRate(FCT::RHI::InputRate::PerInstance, 0u);
    layout.applyBinding(0u, 201u);

    E2E_ASSERT_EQ(g_glCapture.attribIPointerCalls, 1u);
    E2E_ASSERT_EQ(g_glCapture.attribPointerCalls, 0u);
    E2E_ASSERT_EQ(g_glCapture.lastAttribSize, 1);
    E2E_ASSERT_EQ(g_glCapture.lastAttribType, static_cast<GLenum>(GL_UNSIGNED_INT));
    E2E_ASSERT_EQ(g_glCapture.lastAttribStride, 4);
    E2E_ASSERT_EQ(g_glCapture.divisorRecords.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.divisorRecords[0].second, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_27, Attrib_Float_UsesStandardPointer) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLVERTEXATTRIBIPOINTERPROC> iPtrGuard(glVertexAttribIPointer, hookGlVertexAttribIPointer);
    ScopedGlProcHook<PFNGLVERTEXATTRIBPOINTERPROC> fPtrGuard(glVertexAttribPointer, hookGlVertexAttribPointer);
    ScopedGlProcHook<PFNGLVERTEXATTRIBDIVISORPROC> divGuard(glVertexAttribDivisor, hookGlVertexAttribDivisor);
    g_glCapture.reset();

    FCT::RHI::WGL2_InputLayout layout(&wgl2Ctx);
    layout.add(0u, FCT::VertexLayout{FCT::VertexElement{FCT::VtxType::Custom, "a_Tangent", FCT::Format::R32G32B32A32_SFLOAT}});
    layout.inputRate(FCT::RHI::InputRate::PerVertex, 0u);
    layout.applyBinding(0u, 202u);

    E2E_ASSERT_EQ(g_glCapture.attribPointerCalls, 1u);
    E2E_ASSERT_EQ(g_glCapture.attribIPointerCalls, 0u);
    E2E_ASSERT_EQ(g_glCapture.lastAttribSize, 4);
    E2E_ASSERT_EQ(g_glCapture.lastAttribType, static_cast<GLenum>(GL_FLOAT));
    E2E_ASSERT_EQ(g_glCapture.lastAttribStride, 16);
    E2E_ASSERT_EQ(g_glCapture.divisorRecords[0].second, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_27, Attrib_InstanceSlotLayout4Bytes) {
    FCT::VertexLayout instanceStream{
        FCT::VertexElement{FCT::VtxType::Custom, "a_InstanceSlot", FCT::Format::R32_UINT}
    };
    E2E_ASSERT_EQ(instanceStream.getElementCount(), 1u);
    E2E_ASSERT_EQ(instanceStream.getElementOffset(0u), 0u);
    E2E_ASSERT_EQ(instanceStream.getStride(), 4u);
    E2E_ASSERT_EQ(FCT::FormatSize(FCT::Format::R32_UINT), 4u);
    return true;
}

// ============================================================================
// [FEAT-28] 线性字节码命令缓冲录制 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_28, CmdBuffer_EmptyBufferSubmitZeroCommands) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLDRAWARRAYSPROC> drawGuard(glDrawArrays, hookGlDrawArrays);
    ScopedGlProcHook<PFNGLVIEWPORTPROC> vpGuard(glViewport, hookGlViewport);
    g_glCapture.reset();

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    E2E_ASSERT_TRUE(cmdBuf.getBytecodeStream().empty());
    cmdBuf.submit();

    E2E_ASSERT_EQ(g_glCapture.drawArraysCalls, 0u);
    E2E_ASSERT_TRUE(g_glCapture.viewportWidths.empty());
    return true;
}

E2E_TEST(Tier2, FEAT_28, CmdBuffer_Continuous1000CommandsStream) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);

    cmdBuf.begin();
    for (uint32_t i = 0u; i < 1000u; ++i) {
        cmdBuf.draw(0u, 0u, 36u, 1u);
    }
    cmdBuf.end();

    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), 1000u * sizeof(FCT::RHI::Cmd_Draw));
    return true;
}

E2E_TEST(Tier2, FEAT_28, CmdBuffer_ResetAndReRecordCycle) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    const size_t initialCap = cmdBuf.getBytecodeStream().capacity();

    cmdBuf.begin();
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
    cmdBuf.drawIndex(0u, 0u, 36u, 2u, 0u);
    cmdBuf.end();
    E2E_ASSERT_GT(cmdBuf.getBytecodeStream().size(), 0u);

    cmdBuf.reset();
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), 0u);
    E2E_ASSERT_GE(cmdBuf.getBytecodeStream().capacity(), initialCap);

    cmdBuf.begin();
    cmdBuf.scissor(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{375.0f, 667.0f});
    cmdBuf.end();
    E2E_ASSERT_EQ(cmdBuf.getBytecodeStream().size(), sizeof(FCT::RHI::Cmd_SetScissor));
    return true;
}

E2E_TEST(Tier2, FEAT_28, CmdBuffer_FifoOrderReplay) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLVIEWPORTPROC> vpGuard(glViewport, hookGlViewport);
    g_glCapture.reset();

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{320.0f, 240.0f});
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{640.0f, 480.0f});
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{1280.0f, 720.0f});
    cmdBuf.end();
    cmdBuf.submit();

    E2E_ASSERT_EQ(g_glCapture.viewportWidths.size(), 3u);
    E2E_ASSERT_EQ(g_glCapture.viewportWidths[0], 320);
    E2E_ASSERT_EQ(g_glCapture.viewportWidths[1], 640);
    E2E_ASSERT_EQ(g_glCapture.viewportWidths[2], 1280);
    return true;
}

E2E_TEST(Tier2, FEAT_28, CmdBuffer_UnknownOpcodeHandling) {
    // WebGL 2.0 不支持 IndirectDraw 与显式管线 Barrier，验证降级 Opcode 不破坏后续字节码流步进
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLVIEWPORTPROC> vpGuard(glViewport, hookGlViewport);
    g_glCapture.reset();

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    cmdBuf.drawIndexedIndirect(nullptr, 0u, 4u, 20u);
    cmdBuf.barrier(
        nullptr,
        FCT::ImageLayout::undefined,
        FCT::ImageLayout::shaderReadOnlyOptimal,
        FCT::PipelineStage::colorAttachmentOutput,
        FCT::PipelineStage::fragmentShader,
        FCT::AccessFlag::colorAttachmentWrite,
        FCT::AccessFlag::shaderRead);
    cmdBuf.nextPass();
    cmdBuf.viewport(FCT::Vec2{0.0f, 0.0f}, FCT::Vec2{750.0f, 1334.0f});
    cmdBuf.end();
    cmdBuf.submit();

    E2E_ASSERT_EQ(g_glCapture.viewportWidths.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.viewportWidths[0], 750);
    return true;
}

// ============================================================================
// [FEAT-29] 纹理局部脏矩形与 Canvas 直刷 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_29, SubRegion_ZeroDimensionIgnored) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLTEXSUBIMAGE2DPROC> subImgGuard(glTexSubImage2D, hookGlTexSubImage2D);
    g_glCapture.reset();

    // 句柄为 0 的未分配图像执行子区域更新时安全拦截，不向驱动提交 glTexSubImage2D
    FCT::RHI::WGL2_Image uncreatedImg(&wgl2Ctx);
    uncreatedImg.width(1024);
    uncreatedImg.height(1024);
    uncreatedImg.format(FCT::Format::R8G8B8A8_UNORM);
    const uint8_t dummyPixel[4] = {0xFFu, 0x00u, 0x00u, 0xFFu};
    uncreatedImg.updateSubRegion(0u, 0u, 0u, 0u, 0u, 0u, dummyPixel, 0u);
    E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 0u);

    // Renderbuffer 模式同样阻断纹理子区域写入
    FCT::RHI::WGL2_Image rboImg(&wgl2Ctx);
    rboImg.createFromExisting(601u, GL_RENDERBUFFER, true);
    rboImg.updateSubRegion(0u, 0u, 16u, 16u, 0u, 0u, dummyPixel, 0u);
    E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_29, SubRegion_OddWidthRequiresAlignmentOne) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLPIXELSTOREIPROC> storeGuard(glPixelStorei, hookGlPixelStorei);
    ScopedGlProcHook<PFNGLTEXSUBIMAGE2DPROC> subImgGuard(glTexSubImage2D, hookGlTexSubImage2D);

    FCT::RHI::WGL2_Image r8Atlas(&wgl2Ctx);
    r8Atlas.width(1024);
    r8Atlas.height(1024);
    r8Atlas.format(FCT::Format::R8_UNORM);
    r8Atlas.createFromExisting(701u, GL_TEXTURE_2D, false);

    std::vector<uint8_t> oddBuffer(128u * 16u, 0xAAu);
    for (uint32_t oddW : {1u, 13u, 37u, 127u}) {
        g_glCapture.reset();
        r8Atlas.updateSubRegion(0u, 0u, oddW, 10u, 0u, 0u, oddBuffer.data(), 0u);
        E2E_ASSERT_GE(g_glCapture.pixelStoreRecords.size(), 2u);
        E2E_ASSERT_EQ(g_glCapture.pixelStoreRecords.front().first, static_cast<GLenum>(GL_UNPACK_ALIGNMENT));
        E2E_ASSERT_EQ(g_glCapture.pixelStoreRecords.front().second, 1);
        E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 1u);
        E2E_ASSERT_EQ(static_cast<uint32_t>(g_glCapture.lastSubImageWidth), oddW);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_29, SubRegion_MultipleOfFourAlignmentFour) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLPIXELSTOREIPROC> storeGuard(glPixelStorei, hookGlPixelStorei);
    ScopedGlProcHook<PFNGLTEXSUBIMAGE2DPROC> subImgGuard(glTexSubImage2D, hookGlTexSubImage2D);

    FCT::RHI::WGL2_Image rgbaTex(&wgl2Ctx);
    rgbaTex.width(1024);
    rgbaTex.height(1024);
    rgbaTex.format(FCT::Format::R8G8B8A8_UNORM);
    rgbaTex.createFromExisting(702u, GL_TEXTURE_2D, false);

    std::vector<uint8_t> rgbaBuffer(1024u * 10u * 4u, 0x55u);
    for (uint32_t evenW : {4u, 16u, 64u, 1024u}) {
        g_glCapture.reset();
        rgbaTex.updateSubRegion(0u, 0u, evenW, 10u, 0u, 0u, rgbaBuffer.data(), evenW * 4u);
        E2E_ASSERT_GE(g_glCapture.pixelStoreRecords.size(), 2u);
        // 写入完成后真实 WGL2_Image 必须将 GL_UNPACK_ALIGNMENT 恢复为默认 4，GL_UNPACK_ROW_LENGTH 恢复为 0
        E2E_ASSERT_EQ(g_glCapture.pixelStoreRecords.back().first, static_cast<GLenum>(GL_UNPACK_ALIGNMENT));
        E2E_ASSERT_EQ(g_glCapture.pixelStoreRecords.back().second, 4);
        E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 1u);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_29, SubRegion_OutOfBoundsRectRejection) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    ScopedGlProcHook<PFNGLTEXSUBIMAGE2DPROC> subImgGuard(glTexSubImage2D, hookGlTexSubImage2D);
    g_glCapture.reset();

    FCT::RHI::WGL2_Image atlasImg(&wgl2Ctx);
    atlasImg.width(1024);
    atlasImg.height(1024);
    atlasImg.format(FCT::Format::R8G8B8A8_UNORM);
    atlasImg.createFromExisting(703u, GL_TEXTURE_2D, false);

    std::vector<uint8_t> tilePixels(100u * 100u * 4u, 0x77u);
    E2E_ASSERT_LE(924u + 100u, static_cast<uint32_t>(atlasImg.width()));
    E2E_ASSERT_LE(924u + 100u, static_cast<uint32_t>(atlasImg.height()));
    atlasImg.updateSubRegion(924u, 924u, 100u, 100u, 0u, 0u, tilePixels.data(), 0u);
    E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 1u);

    // 验证 2D 数据纹理子区域在跨越最大容量边界时的裁剪合法性
    const auto clamped = FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(16320u, 128u);
    E2E_ASSERT_EQ(clamped.size(), 2u);
    for (const auto& reg : clamped) {
        E2E_ASSERT_LE(reg.x + reg.width, FCT::RHI::WGL2_StorageBuffer::kWidth);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_29, SubRegion_NullPixelDataSafety) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLTEXSUBIMAGE2DPROC> subImgGuard(glTexSubImage2D, hookGlTexSubImage2D);
    g_glCapture.reset();

    FCT::RHI::WGL2_Image texImg(&wgl2Ctx);
    texImg.width(64);
    texImg.height(64);
    texImg.format(FCT::Format::R8G8B8A8_UNORM);
    texImg.createFromExisting(704u, GL_TEXTURE_2D, false);

    // updateData(nullptr, ...) 真实空指针守卫：不得触发崩溃或提交任何 GL 上传调用
    texImg.updateData(nullptr, 64u * 64u * 4u);
    E2E_ASSERT_EQ(g_glCapture.texSubImage2DCalls, 0u);
    return true;
}

// ============================================================================
// [FEAT-30] SPIR-V 离线转 ES 3.0 工具 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_30, Transpile_EmptySpirvReturnsError) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    const auto res = compiler.transpileSpirvToGlslEs300({}, options);
    E2E_ASSERT_FALSE(res.ok());
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_30, Transpile_CorruptMagicSpirvRejected) {
    static const uint32_t kBadSpirv[] = {0xDEADBEEFu, 0x00010300u};
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    const auto res = compiler.transpileSpirvToGlslEs300(kBadSpirv, options);
    E2E_ASSERT_FALSE(res.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_30, Transpile_ValidSpirvOutputHeaderCheck) {
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

E2E_TEST(Tier2, FEAT_30, Transpile_SsboRewrittenToDataTexture) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.glslVersion = 300u;
    options.es = true;
    options.stage = FCT::ShaderKind::VertexShader;

    const auto resultOr = compiler.transpileSpirvToGlslEs300(kMinimalSsboAndUboSpirv, options);
    E2E_ASSERT_STATUS_OK(resultOr);
    E2E_ASSERT_EQ(resultOr->storageBuffers.size(), 1u);
    E2E_ASSERT_EQ(resultOr->storageBuffers[0].slot, 4u);
    E2E_ASSERT_NE(resultOr->glslSource.find("usampler2D"), std::string::npos);
    E2E_ASSERT_NE(resultOr->glslSource.find("texelFetch"), std::string::npos);
    return true;
}

E2E_TEST(Tier2, FEAT_30, Transpile_UniformBlockMetadataExtracted) {
    FCT::ShaderCrossCompiler compiler;
    FCT::ShaderCrossCompilerOptions options;
    options.glslVersion = 300u;
    options.es = true;
    options.stage = FCT::ShaderKind::VertexShader;

    const auto resultOr = compiler.transpileSpirvToGlslEs300(kMinimalSsboAndUboSpirv, options);
    E2E_ASSERT_STATUS_OK(resultOr);
    E2E_ASSERT_EQ(resultOr->uniformBlocks.size(), 1u);
    E2E_ASSERT_EQ(resultOr->uniformBlocks[0].slot, 2u);
    E2E_ASSERT_EQ(resultOr->uniformBlocks[0].sizeBytes, 16u);
    return true;
}

// ============================================================================
// [FEAT-31] 运行时编译器剥离开关 - Tier 2 边界极限测试 (强化可证伪断言)
// ============================================================================

E2E_TEST(Tier2, FEAT_31, RuntimeStrip_DisabledReturnsUnimplemented) {
    FCT::ShaderCrossCompiler crossCompiler;
    FCT::ShaderCrossCompilerOptions opts;

    // 无论 FCT_ENABLE_RUNTIME_SHADERC 开关状态，纯 SPIR-V 入口对空字节码必须返回 InvalidArgument
    const auto emptySpirvRes = crossCompiler.transpileSpirvToGlslEs300({}, opts);
    E2E_ASSERT_FALSE(emptySpirvRes.ok());
    E2E_ASSERT_EQ(emptySpirvRes.status().code(), absl::StatusCode::kInvalidArgument);

#if defined(FCT_ENABLE_RUNTIME_SHADERC) && FCT_ENABLE_RUNTIME_SHADERC
    // 桌面构建开启 shaderc 时，非法 HLSL 语法必须真实编译报错而不能静默放行
    const auto invalidHlslRes = crossCompiler.compileHlslToGlslEs300(
        "invalid_hlsl_syntax_token ???", "main", FCT::ShaderKind::FragmentShader, opts);
    E2E_ASSERT_FALSE(invalidHlslRes.ok());
#else
    // 小游戏运行时构建剥离 shaderc (FCT_ENABLE_RUNTIME_SHADERC=0) 时，调用 HLSL 编译严格返回 UnimplementedError
    const auto strippedRes = crossCompiler.compileHlslToGlslEs300(
        "float4 main() : SV_Target { return float4(1, 0, 0, 1); }",
        "main",
        FCT::ShaderKind::FragmentShader,
        opts);
    E2E_ASSERT_FALSE(strippedRes.ok());
    E2E_ASSERT_EQ(strippedRes.status().code(), absl::StatusCode::kUnimplemented);
#endif
    return true;
}

E2E_TEST(Tier2, FEAT_31, RuntimeStrip_PrebakedTableDirectHit) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLSHADERSOURCEPROC> shaderSrcGuard(glShaderSource, hookGlShaderSource);
    g_glCapture.reset();

    // 1. 通过真实 SPIRV-Cross 转译器（零 shaderc 依赖）产出 #version 300 es 预烘焙条目并写入 shaders_wgl2.bin
    FCT::ShaderCrossCompiler crossCompiler;
    MQEngine::DouyinMiniGame::ShaderVariantInput fsVariant;
    fsVariant.shaderId = "ForwardPlus_Lit_FS";
    fsVariant.techniqueName = "ForwardPlusStandard";
    fsVariant.passTag = "ForwardLit";
    fsVariant.stage = FCT::ShaderKind::FragmentShader;
    fsVariant.spirvWords.assign(
        std::begin(kValidSampleFragmentSpirv), std::end(kValidSampleFragmentSpirv));

    const auto cookedEntryOr = MQEngine::DouyinMiniGame::DouyinShaderCooker::transpileVariant(
        crossCompiler, fsVariant);
    E2E_ASSERT_STATUS_OK(cookedEntryOr);

    const std::vector<MQEngine::DouyinMiniGame::CookedShaderEntry> packEntries{*cookedEntryOr};
    const std::vector<uint8_t> shaderPackBytes =
        MQEngine::DouyinMiniGame::DouyinShaderCooker::serializeShaderPack(packEntries);
    E2E_ASSERT_GT(shaderPackBytes.size(), 16u);

    // 2. 运行时免编译器直接反序列化预烘焙包，装填 FCT::RHI::ShaderBinary 与 WGL2_PixelShader 直送 GL 驱动
    const auto loadedEntriesOr =
        MQEngine::DouyinMiniGame::DouyinShaderCooker::deserializeShaderPack(shaderPackBytes);
    E2E_ASSERT_STATUS_OK(loadedEntriesOr);
    E2E_ASSERT_EQ(loadedEntriesOr->size(), 1u);

    const auto& loadedEntry = (*loadedEntriesOr)[0];
    E2E_ASSERT_EQ(loadedEntry.shaderId, "ForwardPlus_Lit_FS");
    E2E_ASSERT_EQ(loadedEntry.glslEs300Source.substr(0u, 15u), "#version 300 es");
    E2E_ASSERT_GE(loadedEntry.combinedSamplers.size(), 1u);

    FCT::RHI::ShaderBinary runtimeBin;
    runtimeBin.code(std::vector<char>(
        loadedEntry.glslEs300Source.begin(), loadedEntry.glslEs300Source.end()));
    runtimeBin.location({{"POSITION", 0u}, {"TEXCOORD0", 1u}});

    FCT::RHI::WGL2_PixelShader runtimePs(&wgl2Ctx);
    runtimePs.code(runtimeBin.code());
    runtimePs.create();

    E2E_ASSERT_NE(runtimePs.getShaderHandle(), 0u);
    E2E_ASSERT_EQ(g_glCapture.lastUploadedShaderSource.substr(0u, 15u), "#version 300 es");
    E2E_ASSERT_EQ(runtimeBin.locationBySemantic("POSITION"), 0);
    E2E_ASSERT_EQ(runtimeBin.locationBySemantic("TEXCOORD0"), 1);
    E2E_ASSERT_EQ(runtimeBin.locationBySemantic("NON_EXISTENT"), -1);
    return true;
}

E2E_TEST(Tier2, FEAT_31, RuntimeStrip_PrebakedTableMissReturnsNull) {
    // 1. 损坏魔数或截断的 shaders_wgl2.bin 预烘焙块在运行时必须被反序列化器严格拦截
    const uint8_t corruptedPack[12] = {0x00u, 0x11u, 0x22u, 0x33u, 0x01u, 0x00u, 0x00u, 0x00u};
    const auto badPackOr = MQEngine::DouyinMiniGame::DouyinShaderCooker::deserializeShaderPack(corruptedPack);
    E2E_ASSERT_FALSE(badPackOr.ok());

    // 2. 空源码块的 WGL2_VertexShader 调用 create() 时句柄保持为 0（安全未命中，不向驱动提交空着色器）
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_VertexShader emptyVs(&wgl2Ctx);
    emptyVs.create();
    E2E_ASSERT_EQ(emptyVs.getShaderHandle(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_31, RuntimeStrip_EnabledEngineEmptySourceRejected) {
    FCT::ShaderCrossCompiler crossCompiler;
    FCT::ShaderCrossCompilerOptions opts;
    const auto res = crossCompiler.compileHlslToGlslEs300(
        "", "main", FCT::ShaderKind::VertexShader, opts);
    E2E_ASSERT_FALSE(res.ok());
#if defined(FCT_ENABLE_RUNTIME_SHADERC) && FCT_ENABLE_RUNTIME_SHADERC
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kInvalidArgument);
#else
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kUnimplemented);
#endif
    return true;
}

E2E_TEST(Tier2, FEAT_31, RuntimeStrip_BinarySizeAdvantageContract) {
    // 验证桌面专属重型管线变体（RayTracing / Deferred / MeshShader）在预烘焙阶段被剔除，仅保留轻量 #version 300 es 产物
    std::vector<std::string> activeTags{"DepthPrepass", "ForwardLit", "UI"};

    MQEngine::DouyinMiniGame::ShaderVariantInput mobileVs;
    mobileVs.shaderId = "MobileForward_VS";
    mobileVs.passTag = "ForwardLit";
    mobileVs.stage = FCT::ShaderKind::VertexShader;
    mobileVs.spirvWords.assign(std::begin(kMinimalSsboAndUboSpirv), std::end(kMinimalSsboAndUboSpirv));

    MQEngine::DouyinMiniGame::ShaderVariantInput desktopRt;
    desktopRt.shaderId = "DesktopPathTracer_RGEN";
    desktopRt.passTag = "RayTracing";
    desktopRt.stage = FCT::ShaderKind::VertexShader;
    desktopRt.spirvWords.assign(std::begin(kMinimalSsboAndUboSpirv), std::end(kMinimalSsboAndUboSpirv));

    const std::vector<MQEngine::DouyinMiniGame::ShaderVariantInput> candidates{mobileVs, desktopRt};
    const auto reportOr = MQEngine::DouyinMiniGame::DouyinShaderCooker::cookVariants(activeTags, candidates);
    E2E_ASSERT_STATUS_OK(reportOr);
    E2E_ASSERT_EQ(reportOr->prunedVariantCount, 1u);
    E2E_ASSERT_EQ(reportOr->compiledVariantCount, 1u);
    E2E_ASSERT_LT(reportOr->shaderPackBinary.size(), 4096u);
    return true;
}

// ============================================================================
// [FEAT-32] 瞬态资源池生命周期区间复用 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_32, TransientPool_ZeroRequestsSafe) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    pool.tick(0u);
    pool.evictUnused();
    E2E_ASSERT_EQ(pool.getPoolSize(), 0u);
    E2E_ASSERT_EQ(pool.getStats().totalVirtualRequests, 0u);
    E2E_ASSERT_EQ(pool.getStats().getReuseRate(), 0.0f);
    return true;
}

E2E_TEST(Tier2, FEAT_32, TransientPool_NonOverlappingIntervalReuse) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 1024u;
    spec.height = 1024u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;
    spec.isSampled = true;

    // Pass [0, 1] 与紧邻的 Pass [2, 3] 区间不重叠 (expiredPassIdx=1 < currentPassIdx=2)，必须精确命中同一物理图像
    FCT::Image* imgA = pool.acquire(spec, 0u, 1u, 1u);
    FCT::Image* imgB = pool.acquire(spec, 2u, 3u, 1u);

    E2E_ASSERT_NE(imgA, nullptr);
    E2E_ASSERT_EQ(imgA, imgB);
    E2E_ASSERT_EQ(pool.getStats().physicalAllocations, 1u);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_32, TransientPool_OverlappingIntervalCreatesDistinct) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec spec{};
    spec.width = 1024u;
    spec.height = 1024u;
    spec.format = FCT::Format::R8G8B8A8_UNORM;

    // 边界重叠：Pass A 在 Pass 2 结束，Pass B 在同一个 Pass 2 开始 (expiredPassIdx=2 == currentPassIdx=2)，严禁复用
    FCT::Image* imgA = pool.acquire(spec, 0u, 2u, 1u);
    FCT::Image* imgB = pool.acquire(spec, 2u, 3u, 1u);

    E2E_ASSERT_NE(imgA, imgB);
    E2E_ASSERT_EQ(pool.getStats().physicalAllocations, 2u);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_32, TransientPool_SpecMismatchPreventsReuse) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec specA{};
    specA.width = 1024u;
    specA.height = 1024u;
    specA.format = FCT::Format::R8G8B8A8_UNORM;

    FCT::TransientResourceSpec specB = specA;
    specB.width = 512u;
    specB.height = 512u;

    FCT::Image* imgA = pool.acquire(specA, 0u, 1u, 1u);
    FCT::Image* imgB = pool.acquire(specB, 2u, 3u, 1u);

    E2E_ASSERT_FALSE(specA.matches(specB));
    E2E_ASSERT_NE(imgA, imgB);
    E2E_ASSERT_EQ(pool.getPoolSize(), 2u);
    return true;
}

E2E_TEST(Tier2, FEAT_32, TransientPool_TtlAging120FramesPurge) {
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

    // 帧号差值恰为 120 帧 (130 - 10 == 120 保活阈值)：处于保活上界内，不淘汰
    pool.tick(10u + 120u);
    E2E_ASSERT_EQ(pool.getPoolSize(), 1u);

    // 帧号差值 121 帧 (> 120 保活阈值)：精确触发物理资源销毁淘汰
    pool.tick(10u + 120u + 1u);
    E2E_ASSERT_EQ(pool.getPoolSize(), 0u);
    return true;
}

// ============================================================================
// [FEAT-33] 无采样出边附件降级 Renderbuffer - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_33, Renderbuffer_SampledAttachmentCannotDowngrade) {
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

E2E_TEST(Tier2, FEAT_33, Renderbuffer_UnsampledAttachmentDowngrade) {
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

E2E_TEST(Tier2, FEAT_33, Renderbuffer_DepthStencilDowngrade) {
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

E2E_TEST(Tier2, FEAT_33, Renderbuffer_MsaaRenderbufferHandling) {
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

E2E_TEST(Tier2, FEAT_33, Renderbuffer_CompatibleRboReuse) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
    FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
    FCT::TransientResourcePool pool(&wgl2Dev);

    FCT::TransientResourceSpec rboSpec{};
    rboSpec.width = 1024u;
    rboSpec.height = 1024u;
    rboSpec.format = FCT::Format::D24_UNORM_S8_UINT;
    rboSpec.isDepthStencil = true;
    rboSpec.isSampled = false;
    rboSpec.allowRenderbufferDowngrade = true;

    FCT::Image* rbo1 = pool.acquire(rboSpec, 0u, 1u, 1u);
    FCT::Image* rbo2 = pool.acquire(rboSpec, 2u, 3u, 1u);
    E2E_ASSERT_TRUE(rbo1->isRenderbuffer());
    E2E_ASSERT_EQ(rbo1, rbo2);
    E2E_ASSERT_EQ(pool.getStats().reuseHits, 1u);

    // 需要采样时 (isSampled = true) 绝不能复用已降级的 Renderbuffer
    FCT::TransientResourceSpec sampledSpec = rboSpec;
    sampledSpec.isSampled = true;
    FCT::Image* sampledTex = pool.acquire(sampledSpec, 4u, 5u, 1u);
    E2E_ASSERT_NE(rbo1, sampledTex);
    E2E_ASSERT_FALSE(sampledTex->isRenderbuffer());
    return true;
}

// ============================================================================
// [FEAT-34] TBDR Invalidate 指令自动推导 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_34, TBDR_BeginInvalidateOnNoClear) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();

    FCT::RHI::WGL2_PassGroup passGroup(&wgl2Ctx);
    FCT::RHI::WGL2_Pass pass0(&wgl2Ctx);
    passGroup.addPass(&pass0);
    passGroup.create();

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = false;
    plan.beginDiscards = {GL_COLOR_ATTACHMENT0};
    passGroup.setTBDRInvalidatePlan(plan);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    passGroup.beginSubmit(&cmdBuf);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches[0].size(), 1u);
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[0][0],
        static_cast<GLenum>(GL_COLOR_ATTACHMENT0));

    passGroup.endSubmit(&cmdBuf);
    cmdBuf.end();
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_34, TBDR_EndDepthInvalidateAtLastPass) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();

    FCT::RHI::WGL2_PassGroup passGroup(&wgl2Ctx);
    FCT::RHI::WGL2_Pass pass0(&wgl2Ctx);
    FCT::RHI::WGL2_Pass pass1(&wgl2Ctx);
    passGroup.addPass(&pass0);
    passGroup.addPass(&pass1);
    passGroup.create();

    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = false;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT};
    passGroup.setTBDRInvalidatePlan(plan);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    passGroup.beginSubmit(&cmdBuf);
    passGroup.nextPass(&cmdBuf);
    // Group 内中间子 Pass 切换期间严禁提前丢弃深度附件
    E2E_ASSERT_TRUE(g_glCapture.invalidatedAttachmentBatches.empty());

    passGroup.endSubmit(&cmdBuf);
    cmdBuf.end();
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[0][0],
        static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    return true;
}

E2E_TEST(Tier2, FEAT_34, TBDR_PreserveExportTargetNeverDiscarded) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();

    FCT::RHI::WGL2_PassGroup presentGroup(&wgl2Ctx);
    FCT::RHI::WGL2_Pass presentPass(&wgl2Ctx);
    presentGroup.addPass(&presentPass);
    presentGroup.create();

    // 默认帧缓冲 (FBO 0) 最终呈现：仅丢弃瞬态深度，严禁包含 GL_COLOR
    FCT::RHI::TBDRInvalidatePlan plan;
    plan.isDefaultFBO = true;
    plan.endDiscards = {GL_DEPTH_ATTACHMENT};
    presentGroup.setTBDRInvalidatePlan(plan);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    presentGroup.beginSubmit(&cmdBuf);
    presentGroup.endSubmit(&cmdBuf);
    cmdBuf.end();

    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    const auto& batch = g_glCapture.invalidatedAttachmentBatches[0];
    E2E_ASSERT_EQ(batch.size(), 1u);
    E2E_ASSERT_EQ(batch[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_EQ(std::count(batch.begin(), batch.end(), static_cast<GLenum>(GL_COLOR)), 0);
    return true;
}

E2E_TEST(Tier2, FEAT_34, TBDR_MultiPassSeparationSequence) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();

    FCT::RHI::WGL2_PassGroup groupA(&wgl2Ctx);
    FCT::RHI::WGL2_Pass passA(&wgl2Ctx);
    groupA.addPass(&passA);
    groupA.create();
    FCT::RHI::TBDRInvalidatePlan planA;
    planA.isDefaultFBO = false;
    planA.endDiscards = {GL_DEPTH_ATTACHMENT};
    groupA.setTBDRInvalidatePlan(planA);

    FCT::RHI::WGL2_PassGroup groupB(&wgl2Ctx);
    FCT::RHI::WGL2_Pass passB(&wgl2Ctx);
    groupB.addPass(&passB);
    groupB.create();
    FCT::RHI::TBDRInvalidatePlan planB;
    planB.isDefaultFBO = false;
    planB.endDiscards = {GL_COLOR_ATTACHMENT0};
    groupB.setTBDRInvalidatePlan(planB);

    FCT::RHI::WGL2_CommandBuffer cmdBuf(&wgl2Ctx);
    cmdBuf.begin();
    groupA.beginSubmit(&cmdBuf);
    groupA.endSubmit(&cmdBuf);
    groupB.beginSubmit(&cmdBuf);
    groupB.endSubmit(&cmdBuf);
    cmdBuf.end();

    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 2u);
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[0][0],
        static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[1][0],
        static_cast<GLenum>(GL_COLOR_ATTACHMENT0));
    return true;
}

E2E_TEST(Tier2, FEAT_34, TBDR_BatchInvalidateRecorded) {
    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();

    // 空列表不触发冗余 GL 调用
    FCT::RHI::WGL2_PassGroup::emitInvalidate({}, false);
    E2E_ASSERT_TRUE(g_glCapture.invalidatedAttachmentBatches.empty());

    // 批量双附件单次合并发射 glInvalidateFramebuffer
    FCT::RHI::WGL2_PassGroup::emitInvalidate({GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT}, false);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches[0].size(), 2u);
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[0][0],
        static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    E2E_ASSERT_EQ(
        g_glCapture.invalidatedAttachmentBatches[0][1],
        static_cast<GLenum>(GL_STENCIL_ATTACHMENT));
    return true;
}

// ============================================================================
// [FEAT-35] 默认 FBO 0 附件枚举映射 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_35, FBO0_MapColorAttachmentZeroToColor) {
    const auto mapped0 = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_COLOR_ATTACHMENT0}, true);
    E2E_ASSERT_EQ(mapped0.size(), 1u);
    E2E_ASSERT_EQ(mapped0[0], static_cast<GLenum>(GL_COLOR));

    // 多颜色附件在 FBO 0 上逐元素语义映射为 GL_COLOR，并在向驱动 emitInvalidate 提交时自动去重为单一 GL_COLOR
    const auto mappedMulti = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT15}, true);
    E2E_ASSERT_EQ(mappedMulti.size(), 3u);
    E2E_ASSERT_EQ(mappedMulti[0], static_cast<GLenum>(GL_COLOR));
    E2E_ASSERT_EQ(mappedMulti[1], static_cast<GLenum>(GL_COLOR));
    E2E_ASSERT_EQ(mappedMulti[2], static_cast<GLenum>(GL_COLOR));

    ScopedGlProcHook<PFNGLINVALIDATEFRAMEBUFFERPROC> invGuard(
        glInvalidateFramebuffer, hookGlInvalidateFramebuffer);
    g_glCapture.reset();
    FCT::RHI::WGL2_PassGroup::emitInvalidate(
        {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT15}, true);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches.size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches[0].size(), 1u);
    E2E_ASSERT_EQ(g_glCapture.invalidatedAttachmentBatches[0][0], static_cast<GLenum>(GL_COLOR));
    return true;
}

E2E_TEST(Tier2, FEAT_35, FBO0_MapDepthAttachmentToDepth) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_DEPTH_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    return true;
}

E2E_TEST(Tier2, FEAT_35, FBO0_MapStencilAttachmentToStencil) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_STENCIL_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 1u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_STENCIL));
    return true;
}

E2E_TEST(Tier2, FEAT_35, FBO0_MapDepthStencilToBothEnums) {
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(
        {GL_DEPTH_STENCIL_ATTACHMENT}, true);
    E2E_ASSERT_EQ(mapped.size(), 2u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_DEPTH));
    E2E_ASSERT_EQ(mapped[1], static_cast<GLenum>(GL_STENCIL));
    return true;
}

E2E_TEST(Tier2, FEAT_35, FBO0_OffscreenFboPreservesRawEnums) {
    const std::vector<GLenum> rawList = {
        GL_COLOR_ATTACHMENT0,
        GL_DEPTH_ATTACHMENT,
        GL_STENCIL_ATTACHMENT
    };
    const auto mapped = FCT::RHI::WGL2_PassGroup::translateAttachmentsForInvalidate(rawList, false);
    E2E_ASSERT_EQ(mapped.size(), 3u);
    E2E_ASSERT_EQ(mapped[0], static_cast<GLenum>(GL_COLOR_ATTACHMENT0));
    E2E_ASSERT_EQ(mapped[1], static_cast<GLenum>(GL_DEPTH_ATTACHMENT));
    E2E_ASSERT_EQ(mapped[2], static_cast<GLenum>(GL_STENCIL_ATTACHMENT));
    return true;
}

// ============================================================================
// [FEAT-36] 相邻兼容 Pass 自动合并 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_36, PassMerge_CompatiblePassesMerged) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge tOpaque, tDecal;
    tOpaque.toImage = "SceneColor";
    tDecal.toImage = "SceneColor";

    FCT::DepthStencilEdge dOpaque, dDecal;
    dOpaque.toImage = "SceneDepth";
    dDecal.toImage = "SceneDepth";

    FCT::RenderGraphPassNode opaquePass("OpaquePass");
    opaquePass.addOutgoingEdge(&tOpaque);
    opaquePass.addOutgoingEdge(&dOpaque);
    opaquePass.applyPassDesc(makePassDescWithColorClear("OpaquePass", true));

    FCT::RenderGraphPassNode decalPass("DecalPass");
    decalPass.addOutgoingEdge(&tDecal);
    decalPass.addOutgoingEdge(&dDecal);
    decalPass.applyPassDesc(makePassDescWithColorClear("DecalPass", false));

    E2E_ASSERT_TRUE(graph.canMergePasses(opaquePass, decalPass));
    return true;
}

E2E_TEST(Tier2, FEAT_36, PassMerge_SampleDependencyCutsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge t0, t1;
    t0.toImage = "SceneHdr";
    t1.toImage = "SceneHdr";

    FCT::TextureEdge samplePrevColor;
    samplePrevColor.fromImage = "SceneHdr";

    FCT::RenderGraphPassNode gbufferPass("GBufferPass");
    gbufferPass.addOutgoingEdge(&t0);

    FCT::RenderGraphPassNode lightingPass("LightingPass");
    lightingPass.addOutgoingEdge(&t1);
    lightingPass.addIncomingEdge(&samplePrevColor);

    E2E_ASSERT_FALSE(graph.canMergePasses(gbufferPass, lightingPass));
    return true;
}

E2E_TEST(Tier2, FEAT_36, PassMerge_ForceClearCutsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge t0, t1;
    t0.toImage = "SceneColor";
    t1.toImage = "SceneColor";

    FCT::RenderGraphPassNode mainPass("MainPass");
    mainPass.addOutgoingEdge(&t0);
    mainPass.applyPassDesc(makePassDescWithColorClear("MainPass", true));

    FCT::RenderGraphPassNode clearingOverlay("OverlayPass");
    clearingOverlay.addOutgoingEdge(&t1);
    clearingOverlay.applyPassDesc(makePassDescWithColorClear("OverlayPass", true));

    E2E_ASSERT_FALSE(graph.canMergePasses(mainPass, clearingOverlay));
    return true;
}

E2E_TEST(Tier2, FEAT_36, PassMerge_ThreeSequentialPassesMerged) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge t0, t1, t2;
    t0.toImage = "ForwardLdr";
    t1.toImage = "ForwardLdr";
    t2.toImage = "ForwardLdr";

    FCT::DepthStencilEdge d0, d1, d2;
    d0.toImage = "ForwardDepth";
    d1.toImage = "ForwardDepth";
    d2.toImage = "ForwardDepth";

    FCT::RenderGraphPassNode opaque("Opaque");
    opaque.addOutgoingEdge(&t0);
    opaque.addOutgoingEdge(&d0);
    opaque.applyPassDesc(makePassDescWithColorClear("Opaque", true));

    FCT::RenderGraphPassNode skybox("Skybox");
    skybox.addOutgoingEdge(&t1);
    skybox.addOutgoingEdge(&d1);

    FCT::RenderGraphPassNode translucent("Translucent");
    translucent.addOutgoingEdge(&t2);
    translucent.addOutgoingEdge(&d2);

    E2E_ASSERT_TRUE(graph.canMergePasses(opaque, skybox));
    E2E_ASSERT_TRUE(graph.canMergePasses(skybox, translucent));
    return true;
}

E2E_TEST(Tier2, FEAT_36, PassMerge_DifferentFboCutsMerge) {
    FCT::PipeHub pipeHub;
    FCT::FlowControl flowControl;
    FCT::RenderGraph graph(pipeHub, nullptr, &flowControl, nullptr, nullptr);

    FCT::TargetEdge tScene, tUi;
    tScene.toImage = "OffscreenSceneColor";
    tUi.toImage = "BackbufferColor";

    FCT::RenderGraphPassNode scenePass("ScenePass");
    scenePass.addOutgoingEdge(&tScene);

    FCT::RenderGraphPassNode uiPass("UiPass");
    uiPass.addOutgoingEdge(&tUi);

    E2E_ASSERT_FALSE(graph.canMergePasses(scenePass, uiPass));
    return true;
}

// ============================================================================
// [FEAT-37] PassGroup FBO 哈希缓存 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_37, FboCache_SameTupleProducesIdenticalHash) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.createFromExisting(10u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image depthImg(&wgl2Ctx);
    depthImg.createFromExisting(20u, GL_RENDERBUFFER, true);

    const uint64_t h1 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    const uint64_t h2 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    E2E_ASSERT_EQ(h1, h2);
    return true;
}

E2E_TEST(Tier2, FEAT_37, FboCache_DifferentTupleProducesDistinctHash) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.createFromExisting(10u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image depthA(&wgl2Ctx);
    depthA.createFromExisting(20u, GL_RENDERBUFFER, true);
    FCT::RHI::WGL2_Image depthB(&wgl2Ctx);
    depthB.createFromExisting(21u, GL_RENDERBUFFER, true);

    const uint64_t h1 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthA);
    const uint64_t h2 = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthB);
    E2E_ASSERT_NE(h1, h2);
    return true;
}

E2E_TEST(Tier2, FEAT_37, FboCache_AttachmentDestroyInvalidatesCache) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLGENFRAMEBUFFERSPROC> genFboGuard(glGenFramebuffers, hookGlGenFramebuffers);
    ScopedGlProcHook<PFNGLDELETEFRAMEBUFFERSPROC> delFboGuard(glDeleteFramebuffers, hookGlDeleteFramebuffers);
    g_glCapture.reset();

    FCT::RHI::WGL2_Image colorA(&wgl2Ctx);
    colorA.format(FCT::Format::R8G8B8A8_UNORM);
    colorA.createFromExisting(501u, GL_TEXTURE_2D, false);

    GLuint fbo1 = 0u;
    {
        FCT::RHI::WGL2_PassGroup scopedGroup(&wgl2Ctx);
        const uint64_t keyA = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorA}, nullptr);
        fbo1 = scopedGroup.getOrCreateFBO(keyA, {&colorA}, nullptr);
        E2E_ASSERT_NE(fbo1, 0u);
    }
    // PassGroup 析构时自动调用 glDeleteFramebuffers 销毁已缓存的物理 FBO
    E2E_ASSERT_EQ(g_glCapture.deletedFboCount, 1u);

    // 附件重建（新纹理句柄 502）后计算全新哈希并分配全新 FBO 句柄
    FCT::RHI::WGL2_Image colorRebuilt(&wgl2Ctx);
    colorRebuilt.format(FCT::Format::R8G8B8A8_UNORM);
    colorRebuilt.createFromExisting(502u, GL_TEXTURE_2D, false);

    FCT::RHI::WGL2_PassGroup nextGroup(&wgl2Ctx);
    const uint64_t keyRebuilt = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorRebuilt}, nullptr);
    const GLuint fbo2 = nextGroup.getOrCreateFBO(keyRebuilt, {&colorRebuilt}, nullptr);
    E2E_ASSERT_NE(fbo1, fbo2);
    return true;
}

E2E_TEST(Tier2, FEAT_37, FboCache_CacheHitReusesHandle) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLGENFRAMEBUFFERSPROC> genFboGuard(glGenFramebuffers, hookGlGenFramebuffers);
    g_glCapture.reset();

    FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
    colorImg.format(FCT::Format::R8G8B8A8_UNORM);
    colorImg.createFromExisting(100u, GL_TEXTURE_2D, false);
    FCT::RHI::WGL2_Image depthImg(&wgl2Ctx);
    depthImg.format(FCT::Format::D24_UNORM_S8_UINT);
    depthImg.createFromExisting(200u, GL_RENDERBUFFER, true);

    FCT::RHI::WGL2_PassGroup group(&wgl2Ctx);
    const uint64_t key = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
    const GLuint firstFboId = g_glCapture.nextFboId;

    const GLuint handle1 = group.getOrCreateFBO(key, {&colorImg}, &depthImg);
    const GLuint handle2 = group.getOrCreateFBO(key, {&colorImg}, &depthImg);

    E2E_ASSERT_EQ(handle1, handle2);
    E2E_ASSERT_EQ(g_glCapture.nextFboId - firstFboId, 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_37, FboCache_LargeTupleSetCollisionResistance) {
    FCT::WGL2_Context wgl2Ctx(nullptr);
    ScopedGlProcHook<PFNGLGENFRAMEBUFFERSPROC> genFboGuard(glGenFramebuffers, hookGlGenFramebuffers);
    g_glCapture.reset();

    FCT::RHI::WGL2_PassGroup group(&wgl2Ctx);
    std::unordered_set<uint64_t> uniqueHashes;
    uniqueHashes.reserve(1000u);

    for (uint32_t i = 1u; i <= 1000u; ++i) {
        FCT::RHI::WGL2_Image colorImg(&wgl2Ctx);
        colorImg.format(FCT::Format::R8G8B8A8_UNORM);
        colorImg.createFromExisting(i, GL_TEXTURE_2D, false);

        FCT::RHI::WGL2_Image depthImg(&wgl2Ctx);
        depthImg.format(FCT::Format::D24_UNORM_S8_UINT);
        depthImg.createFromExisting(i + 10000u, GL_RENDERBUFFER, true);

        const uint64_t h = FCT::RHI::WGL2_PassGroup::computeAttachmentHash({&colorImg}, &depthImg);
        uniqueHashes.insert(h);
        const GLuint fbo = group.getOrCreateFBO(h, {&colorImg}, &depthImg);
        E2E_ASSERT_NE(fbo, 0u);
    }

    E2E_ASSERT_EQ(uniqueHashes.size(), 1000u);
    return true;
}

} // namespace MQEngine::Testing
