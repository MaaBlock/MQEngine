#include "Common/E2ECommon.h"
#include <FCT_Compute.h>
#ifdef CHECK_STATUS
#undef CHECK_STATUS
#endif
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/RHI/TransformFeedback.h>
#include <FCT/RHI/WGL2_TransformFeedback.h>
#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT/UI/MiniGame_Window.h>
#include <FCT/UI/EventHandler.h>
#include <FCT/UI/CallBackHandler.h>
#include <FCT/Runtime/Emscripten_Runtime.h>
#include <FCT_SAI/FileSystem.h>
#include <FCT_SAI/Path.h>
#include <FCT_SAI/SystemFontProvider.h>
#include <FCT_SAI/VirtualKeyboard.h>
#include <FCT_WebJS/WebJS.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Testing {

// ============================================================================
// [FEAT-38] FCT_Compute 编译期静态多态 LuisaBackend - Tier 2 边界极限测试
// ============================================================================

namespace {

struct Tier2SingleFieldStruct {
    float weight{0.0f};
};

struct Tier2EightFieldStruct {
    float f0, f1, f2, f3, f4, f5, f6, f7;
};

} // namespace

E2E_TEST(Tier2, FEAT_38, FctCompute_UnsupportedFormatStatusOrRejection) {
    using ::FCT::Compute::ActiveBackend;

    const auto undefFmtOr = ActiveBackend::FormatOps::toPixelFormat(::FCT::Format::UNDEFINED);
    E2E_ASSERT_FALSE(undefFmtOr.ok());
    E2E_ASSERT_EQ(undefFmtOr.status().code(), absl::StatusCode::kInvalidArgument);

    const auto depthStorageOr = ActiveBackend::FormatOps::toPixelStorage(::FCT::Format::D24_UNORM_S8_UINT);
    E2E_ASSERT_FALSE(depthStorageOr.ok());
    E2E_ASSERT_EQ(depthStorageOr.status().code(), absl::StatusCode::kInvalidArgument);

    const auto depthFloatOr = ::FCT::Compute::ToBackendPixelFormat(::FCT::Format::D32_SFLOAT);
    E2E_ASSERT_FALSE(depthFloatOr.ok());
    E2E_ASSERT_EQ(depthFloatOr.status().code(), absl::StatusCode::kInvalidArgument);

    const auto unknownPixelFmt = static_cast<::FCT::Compute::PixelFormat>(0xFEu);
    E2E_ASSERT_EQ(::FCT::Compute::FromBackendPixelFormat(unknownPixelFmt), ::FCT::Format::UNDEFINED);
    return true;
}

E2E_TEST(Tier2, FEAT_38, FctCompute_NullAndDefaultHandleBoundarySafe) {
    ::FCT::Compute::BasicDevice<> emptyDevice;
    E2E_ASSERT_FALSE(emptyDevice.valid());
    E2E_ASSERT_FALSE(static_cast<bool>(emptyDevice));

    ::FCT::Compute::BasicBufferView<float> emptyBufView;
    E2E_ASSERT_FALSE(emptyBufView.valid());
    E2E_ASSERT_FALSE(static_cast<bool>(emptyBufView));

    ::FCT::Compute::BasicImage<float> emptyImg;
    E2E_ASSERT_FALSE(emptyImg.valid());
    E2E_ASSERT_FALSE(static_cast<bool>(emptyImg));

    ::FCT::Compute::BasicFormatImage<::FCT::Format::R32G32B32A32_SFLOAT> emptyFmtImage;
    E2E_ASSERT_FALSE(emptyFmtImage.valid());
    E2E_ASSERT_FALSE(static_cast<bool>(emptyFmtImage));
    return true;
}

E2E_TEST(Tier2, FEAT_38, FctCompute_MathExtremeValuesAndRoundTrip) {
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();
    const float subnormal = std::numeric_limits<float>::denorm_min();
    const ::FCT::Vec4 extremeVec(posInf, negInf, subnormal, -0.0f);

    const auto backendVec = ::FCT::Compute::ToBackendVec(extremeVec);
    const auto restoredVec = ::FCT::Compute::ToHostVec(backendVec);

    E2E_ASSERT_TRUE(std::isinf(restoredVec.x) && restoredVec.x > 0.0f);
    E2E_ASSERT_TRUE(std::isinf(restoredVec.y) && restoredVec.y < 0.0f);
    E2E_ASSERT_EQ(restoredVec.z, subnormal);
    E2E_ASSERT_EQ(restoredVec.w, 0.0f);

    const ::FCT::Mat4 zeroMat(
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);
    const auto restoredZero = ::FCT::Compute::ToHostMat4(::FCT::Compute::ToBackendMat4(zeroMat));
    for (size_t i = 0; i < 16u; ++i) {
        E2E_ASSERT_EQ(restoredZero.m[i], 0.0f);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_38, FctCompute_CompileTimeConceptsAndKernelDimConstraints) {
    static_assert(::FCT::Compute::ScalarConcept<float>);
    static_assert(::FCT::Compute::ScalarConcept<uint64_t>);
    static_assert(!::FCT::Compute::ScalarConcept<::FCT::Vec4>);

    static_assert(::FCT::Compute::FCTVectorConcept<::FCT::Vec2>);
    static_assert(::FCT::Compute::FCTVectorConcept<::FCT::Vec3<>>);
    static_assert(::FCT::Compute::FCTVectorConcept<::FCT::Compute::PackedFloat3>);
    static_assert(::FCT::Compute::FCTVectorConcept<::FCT::Vec4>);

    static_assert(::FCT::Compute::KernelDimensionConcept<1u>);
    static_assert(::FCT::Compute::KernelDimensionConcept<2u>);
    static_assert(::FCT::Compute::KernelDimensionConcept<3u>);
    static_assert(!::FCT::Compute::KernelDimensionConcept<0u>);
    static_assert(!::FCT::Compute::KernelDimensionConcept<4u>);

    static_assert(!::FCT::Compute::SupportedFormatConcept<::FCT::Format::UNDEFINED>);
    static_assert(!::FCT::Compute::SupportedFormatConcept<::FCT::Format::D24_UNORM_S8_UINT>);
    static_assert(!::FCT::Compute::AggregateStructConcept<int>);

    E2E_ASSERT_EQ(::FCT::Compute::DefaultBlockSize_t<1u>::totalThreads, 256u);
    E2E_ASSERT_EQ(::FCT::Compute::DefaultBlockSize_t<2u>::totalThreads, 256u);
    E2E_ASSERT_EQ(::FCT::Compute::DefaultBlockSize_t<3u>::totalThreads, 512u);
    E2E_ASSERT_EQ((::FCT::Compute::BlockSize<16u, 16u, 4u>::totalThreads), 1024u);
    return true;
}

E2E_TEST(Tier2, FEAT_38, FctCompute_StructuredBindingAndLayoutParityEdgeCases) {
    static_assert(::FCT::Compute::AggregateStructConcept<Tier2SingleFieldStruct>);
    static_assert(::FCT::Compute::AggregateStructConcept<Tier2EightFieldStruct>);
    static_assert(::FCT::Compute::Detail::DetectAggregateFieldCount<Tier2SingleFieldStruct>() == 1u);
    static_assert(::FCT::Compute::Detail::DetectAggregateFieldCount<Tier2EightFieldStruct>() == 8u);

    const Tier2SingleFieldStruct s1{3.25f};
    const auto t1 = ::FCT::Compute::Detail::StructuredBindingExtractor<1u>::toTuple(s1);
    E2E_ASSERT_EQ(std::get<0>(t1), 3.25f);

    const Tier2EightFieldStruct s8{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    const auto t8 = ::FCT::Compute::Detail::StructuredBindingExtractor<8u>::toTuple(s8);
    E2E_ASSERT_EQ(std::get<0>(t8), 1.0f);
    E2E_ASSERT_EQ(std::get<7>(t8), 8.0f);

    const auto invalidHw = static_cast<::FCT::Compute::HardwareBackend>(255u);
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(invalidHw), "cuda");
    return true;
}

// ============================================================================
// [FEAT-39] RHI TransformFeedback 辅助原语 - Tier 2 边界极限测试
// ============================================================================

E2E_TEST(Tier2, FEAT_39, XFB_ZeroVertexFeedbackExecution) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);

    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_NE(xfb.get(), nullptr);

    const auto createSt = xfb->create();
    E2E_ASSERT(createSt.ok(), createSt.ToString());
    E2E_ASSERT_NE(xfb->getNativeHandle(), 0u);

    // 重复 create() 保持原句柄幂等
    const uint32_t handleBefore = xfb->getNativeHandle();
    const auto dupCreateSt = xfb->create();
    E2E_ASSERT(dupCreateSt.ok(), dupCreateSt.ToString());
    E2E_ASSERT_EQ(xfb->getNativeHandle(), handleBefore);

    const auto beginSt = xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Points);
    E2E_ASSERT(beginSt.ok(), beginSt.ToString());
    E2E_ASSERT_TRUE(xfb->isActive());
    E2E_ASSERT_FALSE(xfb->isPaused());
    E2E_ASSERT_EQ(xfb->getCurrentMode(), ::FCT::RHI::FeedbackPrimitiveMode::Points);
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), handleBefore);

    // 0 顶点空反馈直接关闭并解除绑定
    const auto endSt = xfb->end();
    E2E_ASSERT(endSt.ok(), endSt.ToString());
    E2E_ASSERT_FALSE(xfb->isActive());

    xfb->unbind();
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_39, XFB_MaxBindingsSaturationLimit) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::RHI::WGL2_TransformFeedback xfb(&ctx);

    const auto createSt = xfb.create();
    E2E_ASSERT(createSt.ok(), createSt.ToString());

    // WebGL 2.0 / GLES 3.0 最小保证 MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS 为 4 槽位 (0..3)
    for (uint32_t slot = 0; slot < 4; ++slot) {
        const auto bindRangeSt = xfb.bindBufferRange(slot, 200u + slot, static_cast<size_t>(slot) * 256u, 1024u);
        E2E_ASSERT(bindRangeSt.ok(), bindRangeSt.ToString());

        const auto bindBaseSt = xfb.bindBufferBase(slot, 300u + slot);
        E2E_ASSERT(bindBaseSt.ok(), bindBaseSt.ToString());
    }

    const auto overflowSlot4 = xfb.bindBufferRange(4u, 999u, 0u, 1024u);
    E2E_ASSERT_FALSE(overflowSlot4.ok());
    E2E_ASSERT_EQ(overflowSlot4.code(), absl::StatusCode::kInvalidArgument);

    const auto overflowBase16 = xfb.bindBufferBase(16u, 999u);
    E2E_ASSERT_FALSE(overflowBase16.ok());
    E2E_ASSERT_EQ(overflowBase16.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_39, XFB_ZeroByteBindingRejection) {
    ::FCT::WGL2_Context ctx(nullptr);

    // 验证未调用 create() 的 TransformFeedback 实例 bind() 不会污染上下文当前绑定
    ::FCT::RHI::WGL2_TransformFeedback activeXfb(&ctx);
    const auto activeCreateSt = activeXfb.create();
    E2E_ASSERT(activeCreateSt.ok(), activeCreateSt.ToString());
    activeXfb.bind();
    const uint32_t boundHandle = ctx.getCurrentBoundTransformFeedback();
    E2E_ASSERT_NE(boundHandle, 0u);

    ::FCT::RHI::WGL2_TransformFeedback uninitializedXfb(&ctx);
    E2E_ASSERT_EQ(uninitializedXfb.getNativeHandle(), 0u);
    uninitializedXfb.bind();
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), boundHandle);

    const auto badSlotZeroByteSt = uninitializedXfb.bindBufferRange(4u, 0u, 0u, 0u);
    E2E_ASSERT_FALSE(badSlotZeroByteSt.ok());
    E2E_ASSERT_EQ(badSlotZeroByteSt.code(), absl::StatusCode::kInvalidArgument);

    // 验证配套 WGL2_StorageBuffer 0 字节分配与 0 字节子区域计算边界
    ::FCT::RHI::WGL2_StorageBuffer zeroBuf(&ctx);
    const auto sboSt = zeroBuf.create(0u);
    E2E_ASSERT(sboSt.ok(), sboSt.ToString());
    E2E_ASSERT_EQ(zeroBuf.size(), 0u);
    E2E_ASSERT_EQ(zeroBuf.getHeight(), 0u);
    E2E_ASSERT_EQ(zeroBuf.map(), nullptr);
    E2E_ASSERT_TRUE(::FCT::RHI::WGL2_StorageBuffer::calculateSubRegions(0u, 0u).empty());
    return true;
}

E2E_TEST(Tier2, FEAT_39, XFB_ReentrantBeginAndInvalidEndRejection) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::RHI::WGL2_TransformFeedback xfb(&ctx);

    const auto createSt = xfb.create();
    E2E_ASSERT(createSt.ok(), createSt.ToString());

    // 未激活前调用 end() 必须被状态机拦截
    const auto prematureEndSt = xfb.end();
    E2E_ASSERT_FALSE(prematureEndSt.ok());
    E2E_ASSERT_EQ(prematureEndSt.code(), absl::StatusCode::kFailedPrecondition);

    const auto beginLinesSt = xfb.begin(::FCT::RHI::FeedbackPrimitiveMode::Lines);
    E2E_ASSERT(beginLinesSt.ok(), beginLinesSt.ToString());
    E2E_ASSERT_EQ(xfb.getCurrentMode(), ::FCT::RHI::FeedbackPrimitiveMode::Lines);

    // 激活期间重入 begin() 必须返回 FailedPrecondition 且保持原图元模式不变
    const auto reentrantBeginSt = xfb.begin(::FCT::RHI::FeedbackPrimitiveMode::Points);
    E2E_ASSERT_FALSE(reentrantBeginSt.ok());
    E2E_ASSERT_EQ(reentrantBeginSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(xfb.getCurrentMode(), ::FCT::RHI::FeedbackPrimitiveMode::Lines);

    // 暂停状态下未 resume 直接 end() 同样受 GLES 3.0 契约拦截
    const auto pauseSt = xfb.pause();
    E2E_ASSERT(pauseSt.ok(), pauseSt.ToString());
    const auto endWhilePausedSt = xfb.end();
    E2E_ASSERT_FALSE(endWhilePausedSt.ok());
    E2E_ASSERT_EQ(endWhilePausedSt.code(), absl::StatusCode::kFailedPrecondition);

    const auto resumeSt = xfb.resume();
    E2E_ASSERT(resumeSt.ok(), resumeSt.ToString());
    const auto cleanEndSt = xfb.end();
    E2E_ASSERT(cleanEndSt.ok(), cleanEndSt.ToString());
    return true;
}

E2E_TEST(Tier2, FEAT_39, XFB_PauseResumeStateIdempotence) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::RHI::WGL2_TransformFeedback xfb(&ctx);

    const auto createSt = xfb.create();
    E2E_ASSERT(createSt.ok(), createSt.ToString());

    // 未激活时 pause / resume 均被拒绝
    E2E_ASSERT_EQ(xfb.pause().code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(xfb.resume().code(), absl::StatusCode::kFailedPrecondition);

    const auto beginSt = xfb.begin(::FCT::RHI::FeedbackPrimitiveMode::Triangles);
    E2E_ASSERT(beginSt.ok(), beginSt.ToString());

    // 未暂停时调用 resume 被拒绝
    E2E_ASSERT_EQ(xfb.resume().code(), absl::StatusCode::kFailedPrecondition);

    const auto firstPauseSt = xfb.pause();
    E2E_ASSERT(firstPauseSt.ok(), firstPauseSt.ToString());
    E2E_ASSERT_TRUE(xfb.isPaused());

    // 重复 pause 被拒绝且保持已暂停态
    const auto secondPauseSt = xfb.pause();
    E2E_ASSERT_EQ(secondPauseSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_TRUE(xfb.isPaused());

    const auto firstResumeSt = xfb.resume();
    E2E_ASSERT(firstResumeSt.ok(), firstResumeSt.ToString());
    E2E_ASSERT_FALSE(xfb.isPaused());

    // 重复 resume 被拒绝且保持运行态
    const auto secondResumeSt = xfb.resume();
    E2E_ASSERT_EQ(secondResumeSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_FALSE(xfb.isPaused());

    // 在激活+暂停组合态下直接调用 destroy()，验证自动恢复、关闭与上下文解绑无泄漏
    const auto pauseBeforeDestroySt = xfb.pause();
    E2E_ASSERT(pauseBeforeDestroySt.ok(), pauseBeforeDestroySt.ToString());
    xfb.destroy();

    E2E_ASSERT_FALSE(xfb.isActive());
    E2E_ASSERT_FALSE(xfb.isPaused());
    E2E_ASSERT_EQ(xfb.getNativeHandle(), 0u);
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), 0u);
    return true;
}

// ============================================================================
// [FEAT-40] MiniGame 画布窗口与 DPR 缩放 - Tier 2 边界极限测试
// ============================================================================

namespace {

struct WindowResizeTraceState {
    uint32_t callbackCount{0};
    int lastWidth{0};
    int lastHeight{0};
};

struct WindowResizeRecorderFunctor {
    WindowResizeTraceState* state{nullptr};

    void operator()(::FCT::Window* /*wnd*/, int width, int height) const noexcept {
        if (state != nullptr) {
            ++state->callbackCount;
            state->lastWidth = width;
            state->lastHeight = height;
        }
    }
};

} // namespace

E2E_TEST(Tier2, FEAT_40, MiniGameWindow_ZeroDimensionClampedToOne) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 0, 0, 2.0f, 2.0f);

    E2E_ASSERT_EQ(win.getWidth(), 1);
    E2E_ASSERT_EQ(win.getHeight(), 1);

    // 负 CSS 宽高热重载时物理帧缓冲仍严格钳位至 >= 1x1
    win.onCanvasResize(-320, -568, 2.0f);
    E2E_ASSERT_EQ(win.getWidth(), 1);
    E2E_ASSERT_EQ(win.getHeight(), 1);
    return true;
}

E2E_TEST(Tier2, FEAT_40, MiniGameWindow_ExtremeResolutionScaling) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 7680, 4320, 3.0f, 2.0f);

    E2E_ASSERT_EQ(win.getEffectiveDpr(), 2.0f);
    E2E_ASSERT_EQ(win.getWidth(), 15360);
    E2E_ASSERT_EQ(win.getHeight(), 8640);

    const ::FCT::Vec2 phys = win.cssToPhysical(3840.0f, 2160.0f);
    E2E_ASSERT_EQ(phys.x, 7680.0f);
    E2E_ASSERT_EQ(phys.y, 4320.0f);

    const ::FCT::Vec2 cssRoundTrip = win.physicalToCss(15360.0f, 8640.0f);
    E2E_ASSERT_EQ(cssRoundTrip.x, 7680.0f);
    E2E_ASSERT_EQ(cssRoundTrip.y, 4320.0f);
    return true;
}

E2E_TEST(Tier2, FEAT_40, MiniGameWindow_NegativeOrZeroDprClamp) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window zeroDprWin(&runtime, 400, 300, 0.0f, 2.0f);

    E2E_ASSERT_EQ(zeroDprWin.getWidth(), 1);
    E2E_ASSERT_EQ(zeroDprWin.getHeight(), 1);

    // 验证 m_effectiveDpr <= 0 时 physicalToCss 具备除零防护，不会产生 Inf/NaN
    const ::FCT::Vec2 safeCssZero = zeroDprWin.physicalToCss(240.0f, 180.0f);
    E2E_ASSERT_TRUE(std::isfinite(safeCssZero.x) && std::isfinite(safeCssZero.y));
    E2E_ASSERT_EQ(safeCssZero.x, 240.0f);
    E2E_ASSERT_EQ(safeCssZero.y, 180.0f);

    zeroDprWin.onCanvasResize(400, 300, -1.5f);
    E2E_ASSERT_EQ(zeroDprWin.getWidth(), 1);
    E2E_ASSERT_EQ(zeroDprWin.getHeight(), 1);
    const ::FCT::Vec2 safeCssNeg = zeroDprWin.physicalToCss(120.0f, 90.0f);
    E2E_ASSERT_EQ(safeCssNeg.x, 120.0f);
    E2E_ASSERT_EQ(safeCssNeg.y, 90.0f);

    // 验证 setDprCap 对 <= 0 非法上限输入保持原合法值不变
    ::FCT::MiniGame_Window cappedWin(&runtime, 400, 300, 3.0f, 2.0f);
    cappedWin.setDprCap(0.0f);
    cappedWin.setDprCap(-3.0f);
    E2E_ASSERT_EQ(cappedWin.getDprCap(), 2.0f);
    E2E_ASSERT_EQ(cappedWin.getEffectiveDpr(), 2.0f);
    return true;
}

E2E_TEST(Tier2, FEAT_40, MiniGameWindow_DprCapUpperLimitClamp) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 400, 300, 4.0f, 3.0f);

    // 系统 DPR 4.0 受 dprCap=3.0 降采样钳位
    E2E_ASSERT_EQ(win.getEffectiveDpr(), 3.0f);
    E2E_ASSERT_EQ(win.getWidth(), 1200);
    E2E_ASSERT_EQ(win.getHeight(), 900);

    // 动态收紧 dprCap=1.5 触发物理分辨率重算
    win.setDprCap(1.5f);
    E2E_ASSERT_EQ(win.getEffectiveDpr(), 1.5f);
    E2E_ASSERT_EQ(win.getWidth(), 600);
    E2E_ASSERT_EQ(win.getHeight(), 450);

    // dprCap=0.0 表示不设上限，直通系统 DPR
    ::FCT::MiniGame_Window uncappedWin(&runtime, 400, 300, 3.5f, 0.0f);
    E2E_ASSERT_EQ(uncappedWin.getEffectiveDpr(), 3.5f);
    E2E_ASSERT_EQ(uncappedWin.getWidth(), 1400);
    E2E_ASSERT_EQ(uncappedWin.getHeight(), 1050);
    return true;
}

E2E_TEST(Tier2, FEAT_40, MiniGameWindow_RapidResizeCallbackConsistency) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 100, 100, 2.0f, 2.0f);

    WindowResizeTraceState trace{};
    const auto cbId = win.getCallBack()->addResizeCallback(WindowResizeRecorderFunctor{&trace});

    for (int cssSide = 200; cssSide <= 500; cssSide += 100) {
        win.onCanvasResize(cssSide, cssSide, 2.0f);
    }

    E2E_ASSERT_EQ(trace.callbackCount, 4u);
    E2E_ASSERT_EQ(win.getWidth(), 1000);
    E2E_ASSERT_EQ(win.getHeight(), 1000);
    E2E_ASSERT_EQ(trace.lastWidth, 1000);
    E2E_ASSERT_EQ(trace.lastHeight, 1000);

    // 传入与当前 dprCap 差值在 1e-4 内的相同值不应重复触发 resize 回调
    win.setDprCap(2.0f);
    E2E_ASSERT_EQ(trace.callbackCount, 4u);

    win.getCallBack()->removeResizeCallback(cbId);
    return true;
}

// ============================================================================
// [FEAT-41] Emscripten 非阻塞帧循环与输入 - Tier 2 边界极限测试
// ============================================================================

namespace {

struct TouchDispatchTrace {
    uint32_t touchEventCount{0};
    ::FCT::TouchPhase lastPhase{::FCT::TouchPhase::Began};
    size_t lastAllTouchesCount{0};
    size_t lastChangedCount{0};
    uint32_t mouseDownCount{0};
    uint32_t mouseMoveCount{0};
    uint32_t mouseUpCount{0};
    int lastMouseX{0};
    int lastMouseY{0};
};

struct TouchEventRecorderFunctor {
    TouchDispatchTrace* trace{nullptr};

    void operator()(::FCT::Window* /*wnd*/, const ::FCT::TouchEvent& event) const noexcept {
        if (trace != nullptr) {
            ++trace->touchEventCount;
            trace->lastPhase = event.phase;
            trace->lastAllTouchesCount = event.touches.size();
            trace->lastChangedCount = event.changedTouches.size();
        }
    }
};

struct MouseDownRecorderFunctor {
    TouchDispatchTrace* trace{nullptr};

    void operator()(::FCT::Window* /*wnd*/, int x, int y) const noexcept {
        if (trace != nullptr) {
            ++trace->mouseDownCount;
            trace->lastMouseX = x;
            trace->lastMouseY = y;
        }
    }
};

struct MouseMoveRecorderFunctor {
    TouchDispatchTrace* trace{nullptr};

    void operator()(::FCT::Window* /*wnd*/, int x, int y) const noexcept {
        if (trace != nullptr) {
            ++trace->mouseMoveCount;
            trace->lastMouseX = x;
            trace->lastMouseY = y;
        }
    }
};

struct MouseUpRecorderFunctor {
    TouchDispatchTrace* trace{nullptr};

    void operator()(::FCT::Window* /*wnd*/, int x, int y) const noexcept {
        if (trace != nullptr) {
            ++trace->mouseUpCount;
            trace->lastMouseX = x;
            trace->lastMouseY = y;
        }
    }
};

struct MainLoopStepRecorderFunctor {
    uint32_t* frameCount{nullptr};
    double* accumulatedDt{nullptr};

    void operator()(double dt) const noexcept {
        if (frameCount != nullptr) {
            ++(*frameCount);
        }
        if (accumulatedDt != nullptr) {
            *accumulatedDt += dt;
        }
    }
};

} // namespace

E2E_TEST(Tier2, FEAT_41, Runtime_ZeroTouchesStateQuery) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 800, 600, 2.0f, 2.0f);
    runtime.setTargetWindow(&win);

    TouchDispatchTrace trace{};
    win.getCallBack()->addTouchCallback(TouchEventRecorderFunctor{&trace});

    E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), -1);
    E2E_ASSERT_FALSE(runtime.isLoopRunning());
    E2E_ASSERT_FALSE(runtime.isPaused());

    // 未经过 TouchStart 的孤立 TouchMove 应被静默忽略，不产生幽灵触点
    runtime.injectTouchMove(99, 128.0f, 256.0f, 0.8f);
    E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());
    E2E_ASSERT_EQ(trace.touchEventCount, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_41, Runtime_TenFingerSimultaneousTouchLimit) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 1000, 1000, 1.0f, 1.0f);
    runtime.setTargetWindow(&win);

    TouchDispatchTrace trace{};
    win.getCallBack()->addTouchCallback(TouchEventRecorderFunctor{&trace});
    win.getCallBack()->addLButtonDownCallback(MouseDownRecorderFunctor{&trace});
    win.getCallBack()->addLButtonUpCallback(MouseUpRecorderFunctor{&trace});

    for (int32_t id = 0; id < 10; ++id) {
        runtime.injectTouchStart(id, static_cast<float>(id * 80), 500.0f, 1.0f);
    }

    E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 10u);
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 0);
    E2E_ASSERT_EQ(trace.lastAllTouchesCount, 10u);
    E2E_ASSERT_EQ(trace.mouseDownCount, 1u);

    // 抬起主触点 0 后，主触点自动晋升至剩余最小活跃触点 1
    runtime.injectTouchEnd(0, 0.0f, 500.0f);
    E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 9u);
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 1);
    E2E_ASSERT_EQ(trace.mouseUpCount, 1u);

    for (int32_t id = 1; id < 10; ++id) {
        runtime.injectTouchEnd(id, static_cast<float>(id * 80), 500.0f);
    }

    E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), -1);
    return true;
}

E2E_TEST(Tier2, FEAT_41, Runtime_OutOfBoundsCoordinatesClamped) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 500, 500, 2.0f, 2.0f);
    runtime.setTargetWindow(&win);

    TouchDispatchTrace trace{};
    win.getCallBack()->addMouseMoveCallback(MouseMoveRecorderFunctor{&trace});

    runtime.injectTouchStart(0, -100.0f, 1400.0f, 1.0f);
    const auto& touches = runtime.getActiveTouches();
    const auto it = touches.find(0);
    E2E_ASSERT_TRUE(it != touches.end());
    E2E_ASSERT_EQ(it->second.physicalX, -100.0f);
    E2E_ASSERT_EQ(it->second.physicalY, 1400.0f);

    // 经窗口双坐标系换算并归一化钳位至 [0, 1] 视口边界
    const ::FCT::Vec2 cssPt = win.physicalToCss(it->second.physicalX, it->second.physicalY);
    const float normX = std::clamp(cssPt.x / static_cast<float>(win.getCssWidth()), 0.0f, 1.0f);
    const float normY = std::clamp(cssPt.y / static_cast<float>(win.getCssHeight()), 0.0f, 1.0f);
    E2E_ASSERT_EQ(normX, 0.0f);
    E2E_ASSERT_EQ(normY, 1.0f);

    runtime.injectTouchMove(0, -250.0f, 2200.0f, 0.5f);
    E2E_ASSERT_EQ(trace.mouseMoveCount, 1u);
    E2E_ASSERT_EQ(trace.lastMouseX, -250);
    E2E_ASSERT_EQ(trace.lastMouseY, 2200);
    return true;
}

E2E_TEST(Tier2, FEAT_41, Runtime_TouchCancelCleansActivePoint) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window win(&runtime, 400, 800, 2.0f, 2.0f);
    runtime.setTargetWindow(&win);

    TouchDispatchTrace trace{};
    win.getCallBack()->addTouchCallback(TouchEventRecorderFunctor{&trace});
    win.getCallBack()->addLButtonUpCallback(MouseUpRecorderFunctor{&trace});

    runtime.injectTouchStart(5, 200.0f, 400.0f, 1.0f);
    runtime.injectTouchStart(8, 300.0f, 500.0f, 1.0f);
    E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 2u);
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 5);

    // 来电或切后台触发主触点 Cancel：立即清理槽位、补发左键抬起并晋升副触点
    runtime.injectTouchCancel(5, 200.0f, 400.0f);
    E2E_ASSERT_EQ(trace.lastPhase, ::FCT::TouchPhase::Canceled);
    E2E_ASSERT_EQ(trace.mouseUpCount, 1u);
    E2E_ASSERT_EQ(runtime.getActiveTouches().size(), 1u);
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 8);

    runtime.injectTouchCancel(8, 300.0f, 500.0f);
    E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), -1);
    return true;
}

E2E_TEST(Tier2, FEAT_41, Runtime_FlappingPauseResumeFrameCount) {
    ::FCT::Emscripten_Runtime runtime;

    uint32_t executedFrames = 0;
    double accumulatedDt = 0.0;
    runtime.startMainLoop(MainLoopStepRecorderFunctor{&executedFrames, &accumulatedDt}, 60);
    E2E_ASSERT_TRUE(runtime.isLoopRunning());
    E2E_ASSERT_EQ(runtime.getTargetFps(), 60);

    for (int i = 0; i < 50; ++i) {
        runtime.stepFrame(0.016);
        runtime.pauseMainLoop();
        E2E_ASSERT_TRUE(runtime.isPaused());
        runtime.stepFrame(0.016);
        runtime.resumeMainLoop();
        E2E_ASSERT_FALSE(runtime.isPaused());
    }

    E2E_ASSERT_EQ(executedFrames, 50u);
    E2E_ASSERT_NEAR(accumulatedDt, 0.80, 1e-6);

    runtime.stopMainLoop();
    E2E_ASSERT_FALSE(runtime.isLoopRunning());
    runtime.stepFrame(0.016);
    E2E_ASSERT_EQ(executedFrames, 50u);
    return true;
}

// ============================================================================
// [FEAT-42] 沙箱双区虚拟文件路径映射 - Tier 2 边界极限测试
// ============================================================================

namespace {

std::atomic<uint64_t> g_vfsSandboxCounter{1};

class SandboxVfsScope {
public:
    explicit SandboxVfsScope(std::string_view tag) {
        m_prevPkgRoot = std::string(::FCT::SAI::FileSystem::getPkgRoot());
        m_prevCacheRoot = std::string(::FCT::SAI::FileSystem::getCacheRoot());

        const uint64_t seq = g_vfsSandboxCounter.fetch_add(1u, std::memory_order_relaxed);
        const auto tick = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());

        std::error_code ec;
        const auto sysTmp = std::filesystem::temp_directory_path(ec);
        const std::string baseDir = ::FCT::SAI::Path::join(
            ::FCT::SAI::Path::normalize(sysTmp.string()),
            absl::StrCat("mq_t2_fct_", tag, "_", seq, "_", tick));

        m_baseRoot = baseDir;
        m_pkgRoot = ::FCT::SAI::Path::join(baseDir, "pkg");
        m_cacheRoot = ::FCT::SAI::Path::join(baseDir, "cache");

        (void)::FCT::SAI::FileSystem::createDirectories(m_pkgRoot);
        (void)::FCT::SAI::FileSystem::createDirectories(m_cacheRoot);
        m_mountOk = ::FCT::SAI::FileSystem::setPkgRoot(m_pkgRoot).ok() &&
                    ::FCT::SAI::FileSystem::setCacheRoot(m_cacheRoot).ok();
    }

    ~SandboxVfsScope() {
        if (!m_prevPkgRoot.empty()) {
            (void)::FCT::SAI::FileSystem::setPkgRoot(m_prevPkgRoot);
        }
        if (!m_prevCacheRoot.empty()) {
            (void)::FCT::SAI::FileSystem::setCacheRoot(m_prevCacheRoot);
        }
        if (!m_baseRoot.empty()) {
            (void)::FCT::SAI::FileSystem::removeAll(m_baseRoot);
        }
    }

    SandboxVfsScope(const SandboxVfsScope&) = delete;
    SandboxVfsScope& operator=(const SandboxVfsScope&) = delete;

    [[nodiscard]] bool isMounted() const noexcept { return m_mountOk; }
    [[nodiscard]] const std::string& pkgRoot() const noexcept { return m_pkgRoot; }
    [[nodiscard]] const std::string& cacheRoot() const noexcept { return m_cacheRoot; }
    [[nodiscard]] const std::string& baseRoot() const noexcept { return m_baseRoot; }

private:
    std::string m_prevPkgRoot;
    std::string m_prevCacheRoot;
    std::string m_baseRoot;
    std::string m_pkgRoot;
    std::string m_cacheRoot;
    bool m_mountOk{false};
};

} // namespace

E2E_TEST(Tier2, FEAT_42, VFS_ZeroByteFileReadWrite) {
    SandboxVfsScope sandbox("zero_byte");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    const auto writeSt = ::FCT::SAI::FileSystem::writeBinaryFile("/cache/empty.bin", nullptr, 0u);
    E2E_ASSERT(writeSt.ok(), writeSt.ToString());
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/cache/empty.bin"));
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::isFile("/cache/empty.bin"));

    const auto readOr = ::FCT::SAI::FileSystem::readBinaryFile("/cache/empty.bin");
    E2E_ASSERT(readOr.ok(), readOr.status().ToString());
    E2E_ASSERT_EQ(readOr->size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_42, VFS_LongNestedPathHandling) {
    SandboxVfsScope sandbox("long_nested");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    // 1. 验证超 260 字符 (30 层目录) 虚拟路径词法解析与沙箱边界包含判定
    std::string ultraLongVirtual = "/cache";
    for (int i = 0; i < 30; ++i) {
        ultraLongVirtual += "/sub_dir_level";
    }
    ultraLongVirtual += "/asset_manifest.bin";
    E2E_ASSERT_GT(ultraLongVirtual.size(), 260u);
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::isVirtualPath(ultraLongVirtual));

    const auto resolvedLongOr = ::FCT::SAI::FileSystem::resolveVirtualPath(ultraLongVirtual, true);
    E2E_ASSERT(resolvedLongOr.ok(), resolvedLongOr.status().ToString());
    E2E_ASSERT_TRUE(resolvedLongOr->starts_with(::FCT::SAI::Path::lexicallyNormal(sandbox.cacheRoot())));

    // 2. 验证多级深度嵌套目录物理落盘自动建目录与回读一致性
    std::string deepVirtualPath = "/cache";
    for (int depth = 0; depth < 12; ++depth) {
        deepVirtualPath += absl::StrCat("/d", depth);
    }
    deepVirtualPath += "/manifest.bin";

    const std::array<uint8_t, 4> payload = {0x4D, 0x51, 0x32, 0x01};
    const auto writeSt = ::FCT::SAI::FileSystem::writeBinaryFile(deepVirtualPath, payload.data(), payload.size());
    E2E_ASSERT(writeSt.ok(), writeSt.ToString());
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists(deepVirtualPath));

    const auto readOr = ::FCT::SAI::FileSystem::readBinaryFile(deepVirtualPath);
    E2E_ASSERT(readOr.ok(), readOr.status().ToString());
    E2E_ASSERT_EQ(readOr->size(), payload.size());
    E2E_ASSERT_EQ((*readOr)[0], 0x4Du);
    E2E_ASSERT_EQ((*readOr)[3], 0x01u);
    return true;
}

E2E_TEST(Tier2, FEAT_42, VFS_PathTraversalAttackRejection) {
    SandboxVfsScope sandbox("traversal");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    const uint8_t evilByte = 0xAA;
    const auto escapeWriteSt = ::FCT::SAI::FileSystem::writeBinaryFile("/cache/../../etc/passwd", &evilByte, 1u);
    E2E_ASSERT_FALSE(escapeWriteSt.ok());
    E2E_ASSERT_EQ(escapeWriteSt.code(), absl::StatusCode::kPermissionDenied);

    const auto escapeReadOr = ::FCT::SAI::FileSystem::readBinaryFile("/pkg/../private/key.pem");
    E2E_ASSERT_FALSE(escapeReadOr.ok());
    E2E_ASSERT_EQ(escapeReadOr.status().code(), absl::StatusCode::kPermissionDenied);

    // 连续斜杠变异与盘符根注入同样必须返回 PermissionDenied
    const auto doubleSlashOr = ::FCT::SAI::FileSystem::resolveVirtualPath("/cache//evil_inject.bin", true);
    E2E_ASSERT_FALSE(doubleSlashOr.ok());
    E2E_ASSERT_EQ(doubleSlashOr.status().code(), absl::StatusCode::kPermissionDenied);

    const auto driveInjectOr = ::FCT::SAI::FileSystem::resolveVirtualPath("/cache/C:/Windows/system.ini", false);
    E2E_ASSERT_FALSE(driveInjectOr.ok());
    E2E_ASSERT_EQ(driveInjectOr.status().code(), absl::StatusCode::kPermissionDenied);
    return true;
}

E2E_TEST(Tier2, FEAT_42, VFS_PkgImmutableProtection) {
    SandboxVfsScope sandbox("pkg_immutable");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    // 通过宿主物理路径预置代码包只读资产
    const std::array<uint8_t, 4> wasmMagic = {0x00, 0x61, 0x73, 0x6D};
    const std::string hostPkgWasm = ::FCT::SAI::Path::join(sandbox.pkgRoot(), "main.wasm");
    const auto seedSt = ::FCT::SAI::FileSystem::writeBinaryFile(hostPkgWasm, wasmMagic.data(), wasmMagic.size());
    E2E_ASSERT(seedSt.ok(), seedSt.ToString());

    // 验证 /pkg/ 只读访问正常，写/删/重命名操作全部被拦截
    const uint8_t tamperByte = 0xFF;
    const auto overwriteSt = ::FCT::SAI::FileSystem::writeBinaryFile("/pkg/main.wasm", &tamperByte, 1u);
    E2E_ASSERT_FALSE(overwriteSt.ok());
    E2E_ASSERT_EQ(overwriteSt.code(), absl::StatusCode::kPermissionDenied);

    const auto unlinkSt = ::FCT::SAI::FileSystem::removeFile("/pkg/main.wasm");
    E2E_ASSERT_FALSE(unlinkSt.ok());
    E2E_ASSERT_EQ(unlinkSt.code(), absl::StatusCode::kPermissionDenied);

    const auto renameSt = ::FCT::SAI::FileSystem::rename("/pkg/main.wasm", "/cache/moved.wasm");
    E2E_ASSERT_FALSE(renameSt.ok());
    E2E_ASSERT_EQ(renameSt.code(), absl::StatusCode::kPermissionDenied);

    const auto intactReadOr = ::FCT::SAI::FileSystem::readBinaryFile("/pkg/main.wasm");
    E2E_ASSERT(intactReadOr.ok(), intactReadOr.status().ToString());
    E2E_ASSERT_EQ(intactReadOr->size(), 4u);
    E2E_ASSERT_EQ((*intactReadOr)[1], 0x61u);
    return true;
}

E2E_TEST(Tier2, FEAT_42, VFS_LifecycleWriteDeleteNotFound) {
    SandboxVfsScope sandbox("lifecycle");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    const std::array<uint8_t, 2> data = {0x12, 0x34};
    const auto writeSt = ::FCT::SAI::FileSystem::writeBinaryFile("/cache/temp_delete.bin", data.data(), data.size());
    E2E_ASSERT(writeSt.ok(), writeSt.ToString());
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/cache/temp_delete.bin"));

    const auto removeSt = ::FCT::SAI::FileSystem::removeFile("/cache/temp_delete.bin");
    E2E_ASSERT(removeSt.ok(), removeSt.ToString());
    E2E_ASSERT_FALSE(::FCT::SAI::FileSystem::exists("/cache/temp_delete.bin"));

    const auto removeAgainSt = ::FCT::SAI::FileSystem::removeFile("/cache/temp_delete.bin");
    E2E_ASSERT_FALSE(removeAgainSt.ok());
    E2E_ASSERT_EQ(removeAgainSt.code(), absl::StatusCode::kNotFound);

    const auto readDeletedOr = ::FCT::SAI::FileSystem::readBinaryFile("/cache/temp_delete.bin");
    E2E_ASSERT_FALSE(readDeletedOr.ok());
    E2E_ASSERT_EQ(readDeletedOr.status().code(), absl::StatusCode::kNotFound);
    return true;
}

// ============================================================================
// [FEAT-43] 无异常异步文件读取与直存下载 - Tier 2 边界极限测试
// ============================================================================

namespace {

struct AsyncStatusSyncBox {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed{false};
    absl::Status status;

    bool waitForCompletion(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mtx);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!completed) {
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                return completed;
            }
        }
        return true;
    }
};

struct AsyncStatusReceiverFunctor {
    AsyncStatusSyncBox* box{nullptr};

    void operator()(absl::Status st) const {
        if (box == nullptr) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(box->mtx);
            box->status = std::move(st);
            box->completed = true;
        }
        box->cv.notify_all();
    }
};

struct AsyncBinaryReadSyncBox {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed{false};
    absl::StatusOr<std::vector<uint8_t>> result{absl::UnknownError("pending")};

    bool waitForCompletion(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mtx);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!completed) {
            if (cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                return completed;
            }
        }
        return true;
    }
};

struct AsyncBinaryReadReceiverFunctor {
    AsyncBinaryReadSyncBox* box{nullptr};

    void operator()(absl::StatusOr<std::vector<uint8_t>> res) const {
        if (box == nullptr) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(box->mtx);
            box->result = std::move(res);
            box->completed = true;
        }
        box->cv.notify_all();
    }
};

} // namespace

E2E_TEST(Tier2, FEAT_43, AsyncIO_EmptyUrlAndPathRejection) {
    SandboxVfsScope sandbox("async_empty");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    // 空根目录配置与空路径写入必须显式返回 InvalidArgument
    E2E_ASSERT_EQ(::FCT::SAI::FileSystem::setPkgRoot("").code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(::FCT::SAI::FileSystem::setCacheRoot("").code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(::FCT::SAI::FileSystem::writeBinaryFile("", nullptr, 0u).code(), absl::StatusCode::kInvalidArgument);

    // 空回调保护：传入空 std::function 不得崩溃
    ::FCT::SAI::FileSystem::readBinaryFileAsync("/cache/anything.bin", {});
    ::FCT::SAI::FileSystem::downloadFileToPathAsync("https://cdn.example.com/res.bin", "/cache/out.bin", {});

    // 空目标虚拟路径立即通过回调返回 InvalidArgument
    AsyncStatusSyncBox emptyDestBox;
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/asset.bin",
        "",
        AsyncStatusReceiverFunctor{&emptyDestBox});
    E2E_ASSERT_TRUE(emptyDestBox.waitForCompletion());
    E2E_ASSERT_FALSE(emptyDestBox.status.ok());
    E2E_ASSERT_EQ(emptyDestBox.status.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_43, AsyncIO_DestinationOutsideCacheRejection) {
    SandboxVfsScope sandbox("async_outside_cache");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    // 1. 目标指向只读 /pkg/ 分区被拦截
    AsyncStatusSyncBox pkgDestBox;
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/test.bin",
        "/pkg/test.bin",
        AsyncStatusReceiverFunctor{&pkgDestBox});
    E2E_ASSERT_TRUE(pkgDestBox.waitForCompletion());
    E2E_ASSERT_FALSE(pkgDestBox.status.ok());
    E2E_ASSERT_EQ(pkgDestBox.status.code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_FALSE(::FCT::SAI::FileSystem::exists("/pkg/test.bin"));

    // 2. 目标为非虚拟相对路径被拦截
    AsyncStatusSyncBox relDestBox;
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/test.bin",
        "unscoped_local/test.bin",
        AsyncStatusReceiverFunctor{&relDestBox});
    E2E_ASSERT_TRUE(relDestBox.waitForCompletion());
    E2E_ASSERT_FALSE(relDestBox.status.ok());
    E2E_ASSERT_EQ(relDestBox.status.code(), absl::StatusCode::kInvalidArgument);

    // 3. 目标尝试利用 /cache/../ 穿透逃逸出缓存区被拦截为 PermissionDenied
    AsyncStatusSyncBox escapeDestBox;
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/test.bin",
        "/cache/../../escaped.bin",
        AsyncStatusReceiverFunctor{&escapeDestBox});
    E2E_ASSERT_TRUE(escapeDestBox.waitForCompletion());
    E2E_ASSERT_FALSE(escapeDestBox.status.ok());
    E2E_ASSERT_EQ(escapeDestBox.status.code(), absl::StatusCode::kPermissionDenied);
    return true;
}

E2E_TEST(Tier2, FEAT_43, AsyncIO_Network404ReturnsUnavailable) {
    SandboxVfsScope sandbox("async_stream_fail");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    // 1. 验证二进制输入流打开缺失源资源时无异常返回 NotFound
    const auto missingStreamOr = ::FCT::SAI::FileSystem::openBinaryInputStream("/pkg/missing_404.bin");
    E2E_ASSERT_FALSE(missingStreamOr.ok());
    E2E_ASSERT_EQ(missingStreamOr.status().code(), absl::StatusCode::kNotFound);

    // 2. 当下载落盘路径已被同名目录占用时，后台线程流打开失败并通过 Status 回调无异常上报错误
    const auto dirSt = ::FCT::SAI::FileSystem::createDirectories("/cache/occupied_target_dir");
    E2E_ASSERT(dirSt.ok(), dirSt.ToString());

    AsyncStatusSyncBox occupiedDestBox;
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "file:///non_existent_404_source.bin",
        "/cache/occupied_target_dir",
        AsyncStatusReceiverFunctor{&occupiedDestBox});
    E2E_ASSERT_TRUE(occupiedDestBox.waitForCompletion());
    E2E_ASSERT_FALSE(occupiedDestBox.status.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_43, AsyncIO_ReadMissingFileStatus) {
    SandboxVfsScope sandbox("async_read_missing");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    AsyncBinaryReadSyncBox missingCacheBox;
    ::FCT::SAI::FileSystem::readBinaryFileAsync(
        "/cache/non_existent_never_written.bin",
        AsyncBinaryReadReceiverFunctor{&missingCacheBox});
    E2E_ASSERT_TRUE(missingCacheBox.waitForCompletion());
    E2E_ASSERT_FALSE(missingCacheBox.result.ok());
    E2E_ASSERT_EQ(missingCacheBox.result.status().code(), absl::StatusCode::kNotFound);

    AsyncBinaryReadSyncBox traversalBox;
    ::FCT::SAI::FileSystem::readBinaryFileAsync(
        "/pkg/../forbidden_secret.bin",
        AsyncBinaryReadReceiverFunctor{&traversalBox});
    E2E_ASSERT_TRUE(traversalBox.waitForCompletion());
    E2E_ASSERT_FALSE(traversalBox.result.ok());
    E2E_ASSERT_EQ(traversalBox.result.status().code(), absl::StatusCode::kPermissionDenied);
    return true;
}

E2E_TEST(Tier2, FEAT_43, AsyncIO_MultipleDirectDownloadsIsolation) {
    SandboxVfsScope sandbox("async_multi_dl");
    E2E_ASSERT_TRUE(sandbox.isMounted());

    constexpr size_t kTaskCount = 5;
    const std::string srcDir = ::FCT::SAI::Path::join(sandbox.baseRoot(), "cdn_origin");
    const auto mkdirSt = ::FCT::SAI::FileSystem::createDirectories(srcDir);
    E2E_ASSERT(mkdirSt.ok(), mkdirSt.ToString());

    // 第 0 号文件构造 140KB 跨多个 64KB 流式分块，其余文件各具独立特征字节
    std::array<std::vector<uint8_t>, kTaskCount> expectedPayloads;
    std::array<std::string, kTaskCount> fileUrls;
    for (size_t i = 0; i < kTaskCount; ++i) {
        const size_t byteLen = (i == 0) ? (140u * 1024u + 37u) : (512u * (i + 1u));
        expectedPayloads[i].resize(byteLen, static_cast<uint8_t>(0x30u + i));
        expectedPayloads[i].front() = static_cast<uint8_t>(0xA0u + i);
        expectedPayloads[i].back() = static_cast<uint8_t>(0xE0u + i);

        const std::string srcFile = ::FCT::SAI::Path::join(srcDir, absl::StrCat("bundle_", i, ".bin"));
        const auto st = ::FCT::SAI::FileSystem::writeBinaryFile(
            srcFile, expectedPayloads[i].data(), expectedPayloads[i].size());
        E2E_ASSERT(st.ok(), st.ToString());
        fileUrls[i] = absl::StrCat("file://", srcFile);
    }

    std::array<AsyncStatusSyncBox, kTaskCount> boxes;
    for (size_t i = 0; i < kTaskCount; ++i) {
        const std::string destVirtual = absl::StrCat("/cache/download_", i, ".bin");
        ::FCT::SAI::FileSystem::downloadFileToPathAsync(
            fileUrls[i],
            destVirtual,
            AsyncStatusReceiverFunctor{&boxes[i]});
    }

    for (size_t i = 0; i < kTaskCount; ++i) {
        E2E_ASSERT_TRUE(boxes[i].waitForCompletion());
        E2E_ASSERT(boxes[i].status.ok(), boxes[i].status.ToString());

        const std::string destVirtual = absl::StrCat("/cache/download_", i, ".bin");
        const auto readBackOr = ::FCT::SAI::FileSystem::readBinaryFile(destVirtual);
        E2E_ASSERT(readBackOr.ok(), readBackOr.status().ToString());
        E2E_ASSERT_EQ(readBackOr->size(), expectedPayloads[i].size());
        E2E_ASSERT_EQ(readBackOr->front(), expectedPayloads[i].front());
        E2E_ASSERT_EQ(readBackOr->back(), expectedPayloads[i].back());
    }
    return true;
}

// ============================================================================
// [FEAT-44] 轻量宿主原生 JS VM 反射绑定 - Tier 2 边界极限测试
// ============================================================================

namespace {

struct NbrPingMethodFunctor {
    ::FCT::JS::Value operator()(const std::vector<::FCT::JS::Value>& args) const {
        if (args.empty()) {
            return ::FCT::JS::Value(0);
        }
        return ::FCT::JS::Value(args[0].toInt32() * 2);
    }
};

struct PromiseOrderRecorderFunctor {
    std::vector<int32_t>* orderLog{nullptr};
    int32_t* targetSlot{nullptr};

    void operator()(const ::FCT::JS::Value& val) const {
        const int32_t parsed = val.toInt32();
        if (targetSlot != nullptr) {
            *targetSlot = parsed;
        }
        if (orderLog != nullptr) {
            orderLog->push_back(parsed);
        }
    }
};

struct PromiseRejectRecorderFunctor {
    std::string* errorOut{nullptr};

    void operator()(const ::FCT::JS::Value& reason) const {
        if (errorOut != nullptr) {
            *errorOut = reason.toString();
        }
    }
};

struct MicrotaskCounterFunctor {
    uint32_t* executedCount{nullptr};

    void operator()() const noexcept {
        if (executedCount != nullptr) {
            ++(*executedCount);
        }
    }
};

} // namespace

E2E_TEST(Tier2, FEAT_44, WebJS_ZeroLengthTypedArrayInvalid) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    const size_t baselineHandles = env.getActiveHandleCount();

    {
        uint8_t dummyByte = 0x7F;
        ::FCT::JS::Object nullPtrView = env.createTypedArrayView(
            nullptr, 16u, ::FCT::JS::TypedArrayType::Uint8);
        auto* vmNull = env.getVMObject(nullPtrView.getHandle());
        E2E_ASSERT_NE(vmNull, nullptr);
        E2E_ASSERT_EQ(vmNull->type, ::FCT::JS::ValueType::TypedArray);
        E2E_ASSERT_EQ(vmNull->typedArrayData, nullptr);

        ::FCT::JS::Object zeroLenView = env.createTypedArrayView(
            &dummyByte, 0u, ::FCT::JS::TypedArrayType::Float32);
        auto* vmZero = env.getVMObject(zeroLenView.getHandle());
        E2E_ASSERT_NE(vmZero, nullptr);
        E2E_ASSERT_EQ(vmZero->typedArrayByteLength, 0u);

        const uint32_t ffiHandle = fct_webjs_create_typed_array(nullptr, 0u, 0u);
        auto* vmFfi = env.getVMObject(ffiHandle);
        E2E_ASSERT_NE(vmFfi, nullptr);
        E2E_ASSERT_EQ(vmFfi->typedArrayData, nullptr);
        E2E_ASSERT_EQ(vmFfi->typedArrayByteLength, 0u);
        fct_webjs_release_handle(ffiHandle);
    }

    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineHandles);
    return true;
}

E2E_TEST(Tier2, FEAT_44, WebJS_LargeHandleTableStressAllocation) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    const size_t baselineActive = env.getActiveHandleCount();

    constexpr size_t kStressCount = 10000;
    std::vector<uint32_t> handles;
    handles.reserve(kStressCount);

    for (size_t i = 0; i < kStressCount; ++i) {
        handles.push_back(fct_webjs_alloc_object());
    }
    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineActive + kStressCount);

    for (uint32_t h : handles) {
        fct_webjs_release_handle(h);
    }
    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineActive);
    E2E_ASSERT_GE(env.getFreeListSize(), kStressCount);

    // 再次分配句柄必须直接复用空闲列表槽位 (LIFO 弹出最后释放的句柄)
    const size_t freeBeforeReuse = env.getFreeListSize();
    const uint32_t reusedHandle = fct_webjs_alloc_object();
    E2E_ASSERT_EQ(reusedHandle, handles.back());
    E2E_ASSERT_EQ(env.getFreeListSize(), freeBeforeReuse - 1u);
    fct_webjs_release_handle(reusedHandle);
    return true;
}

E2E_TEST(Tier2, FEAT_44, WebJS_InvalidHandleQuerySafe) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    const size_t baselineActive = env.getActiveHandleCount();

    // 静态保留句柄 (0..4) 释放与保留均为空操作，Global 与预留 Null 句柄始终对应合法静态 VMObject
    env.releaseHandle(::FCT::JS::HandleNull);
    env.releaseHandle(::FCT::JS::HandleGlobal);
    env.retainHandle(::FCT::JS::HandleUndefined);
    E2E_ASSERT_NE(env.getVMObject(::FCT::JS::HandleGlobal), nullptr);
    E2E_ASSERT_NE(env.getVMObject(::FCT::JS::HandleNull), nullptr);
    E2E_ASSERT_EQ(env.getVMObject(::FCT::JS::HandleNull)->type, ::FCT::JS::ValueType::Null);

    // 未注册野句柄安全降级
    constexpr uint32_t kBogusHandle = 999999u;
    E2E_ASSERT_EQ(env.getVMObject(kBogusHandle), nullptr);
    ::FCT::JS::Object bogusObj(kBogusHandle);
    E2E_ASSERT_TRUE(bogusObj.get("missingProp").isUndefined());
    E2E_ASSERT_FALSE(bogusObj.has("missingProp"));
    E2E_ASSERT_TRUE(bogusObj.call("missingMethod").isUndefined());

    // 动态句柄释放后再访问不产生悬垂崩溃
    const uint32_t tempHandle = env.allocateHandle(::FCT::JS::ValueType::Object);
    E2E_ASSERT_NE(env.getVMObject(tempHandle), nullptr);
    env.releaseHandle(tempHandle);
    E2E_ASSERT_EQ(env.getVMObject(tempHandle), nullptr);
    E2E_ASSERT_EQ(fct_webjs_get_prop_handle(tempHandle, "any"), ::FCT::JS::HandleUndefined);
    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineActive);
    return true;
}

E2E_TEST(Tier2, FEAT_44, WebJS_NbrModeToggleBoundary) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    const size_t baselineActive = env.getActiveHandleCount();

    {
        ::FCT::JS::Object globalObj = env.global();
        const uint32_t nbrBridgeHandle = fct_webjs_alloc_object();
        fct_webjs_set_prop_string(nbrBridgeHandle, "backend", "NBR_DIRECT_FFI");
        fct_webjs_set_prop_number(nbrBridgeHandle, "enabled", 0.0);
        fct_webjs_set_prop_handle(::FCT::JS::HandleGlobal, "__tier2NbrBridge", nbrBridgeHandle);

        const uint32_t queriedHandle = fct_webjs_get_prop_handle(::FCT::JS::HandleGlobal, "__tier2NbrBridge");
        E2E_ASSERT_EQ(queriedHandle, nbrBridgeHandle);

        ::FCT::JS::Object nbrBridgeObj(queriedHandle);
        E2E_ASSERT_EQ(nbrBridgeObj.get("enabled").toInt32(), 0);

        // 切换 NBR 开关并验证原生方法直调契约
        nbrBridgeObj.set("enabled", ::FCT::JS::Value(true));
        E2E_ASSERT_TRUE(nbrBridgeObj.get("enabled").toBoolean());

        nbrBridgeObj.registerMethod("doubleValue", NbrPingMethodFunctor{});
        const ::FCT::JS::Value callRes = nbrBridgeObj.call("doubleValue", {::FCT::JS::Value(21)});
        E2E_ASSERT_EQ(callRes.toInt32(), 42);

        E2E_ASSERT_TRUE(globalObj.deleteProperty("__tier2NbrBridge"));
        fct_webjs_release_handle(queriedHandle);
        fct_webjs_release_handle(nbrBridgeHandle);
    }

    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineActive);
    return true;
}

E2E_TEST(Tier2, FEAT_44, WebJS_OutOfOrderPromiseResolve) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    const size_t baselineActive = env.getActiveHandleCount();

    {
        ::FCT::JS::Promise p1 = env.newPromise();
        ::FCT::JS::Promise p2 = env.newPromise();
        ::FCT::JS::Promise pReject = env.newPromise();

        std::vector<int32_t> settleOrder;
        int32_t res1 = 0;
        int32_t res2 = 0;
        std::string rejectReason;

        p1.then(PromiseOrderRecorderFunctor{&settleOrder, &res1});
        p2.then(PromiseOrderRecorderFunctor{&settleOrder, &res2});
        pReject.catchError(PromiseRejectRecorderFunctor{&rejectReason});

        // 乱序先兑现 p2，再兑现 p1
        p2.resolve(::FCT::JS::Value(200));
        p1.resolve(::FCT::JS::Value(100));

        // 已 settled 的 Promise 再次 resolve / reject 必须保持幂等忽略
        p1.resolve(::FCT::JS::Value(999));
        p1.reject(::FCT::JS::Value("late_error"));

        pReject.reject(::FCT::JS::Value("timeout_504"));
        env.runMicrotasks();

        E2E_ASSERT_EQ(settleOrder.size(), 2u);
        E2E_ASSERT_EQ(settleOrder[0], 200);
        E2E_ASSERT_EQ(settleOrder[1], 100);
        E2E_ASSERT_EQ(res1, 100);
        E2E_ASSERT_EQ(res2, 200);
        E2E_ASSERT_TRUE(p1.isFulfilled());
        E2E_ASSERT_FALSE(p1.isRejected());
        E2E_ASSERT_TRUE(pReject.isRejected());
        E2E_ASSERT_EQ(rejectReason, "timeout_504");

        // 对不存在的 Promise 句柄调用 C FFI resolve/reject 不崩溃，微任务队列有序排空
        fct_webjs_promise_resolve(999999u, ::FCT::JS::HandleNull);
        fct_webjs_promise_reject(999999u, ::FCT::JS::HandleNull);

        uint32_t microtaskRuns = 0;
        env.queueMicrotask(MicrotaskCounterFunctor{&microtaskRuns});
        env.runMicrotasks();
        E2E_ASSERT_EQ(microtaskRuns, 1u);
    }

    E2E_ASSERT_EQ(env.getActiveHandleCount(), baselineActive);
    return true;
}

// ============================================================================
// [FEAT-45] 跨平台系统字形批量光栅化契约 - Tier 2 边界极限测试
// ============================================================================

namespace {

class ScopedSystemFontSession {
public:
    ScopedSystemFontSession() : m_provider(::FCT::SAI::SystemFontProvider::create()) {
        if (m_provider) {
            m_initStatus = m_provider->init();
        } else {
            m_initStatus = absl::InternalError("SystemFontProvider::create returned null");
        }
    }

    ~ScopedSystemFontSession() {
        if (m_provider) {
            m_provider->shutdown();
        }
    }

    ScopedSystemFontSession(const ScopedSystemFontSession&) = delete;
    ScopedSystemFontSession& operator=(const ScopedSystemFontSession&) = delete;

    [[nodiscard]] const absl::Status& initStatus() const noexcept { return m_initStatus; }
    [[nodiscard]] ::FCT::SAI::SystemFontProvider* get() const noexcept { return m_provider.get(); }

private:
    ::FCT::UniquePtr<::FCT::SAI::SystemFontProvider> m_provider;
    absl::Status m_initStatus;
};

} // namespace

E2E_TEST(Tier2, FEAT_45, FontRaster_ZeroCodepointsBatchSafe) {
    ScopedSystemFontSession fontSession;
    E2E_ASSERT(fontSession.initStatus().ok(), fontSession.initStatus().ToString());

    const std::span<const ::FCT::SAI::GlyphRequest> emptyRequests{};
    const auto batchOr = fontSession.get()->rasterizeBatch("Microsoft YaHei", 20, 4, false, emptyRequests);
    E2E_ASSERT(batchOr.ok(), batchOr.status().ToString());
    E2E_ASSERT_TRUE(batchOr->glyphs.empty());
    E2E_ASSERT_EQ(batchOr->atlasWidth, 0u);
    E2E_ASSERT_EQ(batchOr->atlasHeight, 0u);
    E2E_ASSERT_TRUE(batchOr->pixelData.empty());
    return true;
}

E2E_TEST(Tier2, FEAT_45, FontRaster_LargeBatch500ChineseGlyphs) {
    ScopedSystemFontSession fontSession;
    E2E_ASSERT(fontSession.initStatus().ok(), fontSession.initStatus().ToString());

    constexpr size_t kGlyphCount = 500;
    std::vector<::FCT::SAI::GlyphRequest> requests(kGlyphCount);
    for (size_t i = 0; i < kGlyphCount; ++i) {
        requests[i].codepoint = static_cast<uint32_t>(0x4E00u + i);
        requests[i].fontSizePx = 18;
        requests[i].weight = 4;
        requests[i].italic = false;
        requests[i].isEmoji = false;
    }

    const auto batchOr = fontSession.get()->rasterizeBatch("Microsoft YaHei", 18, 4, false, requests);
    E2E_ASSERT(batchOr.ok(), batchOr.status().ToString());
    E2E_ASSERT_EQ(batchOr->glyphs.size(), kGlyphCount);
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::R8_UNORM);
    E2E_ASSERT_GT(batchOr->atlasWidth, 0u);
    E2E_ASSERT_GT(batchOr->atlasHeight, 0u);
    E2E_ASSERT_EQ(
        batchOr->pixelData.size(),
        static_cast<size_t>(batchOr->atlasWidth) * static_cast<size_t>(batchOr->atlasHeight));

    // 验证所有拼车字形坐标严格处于图集边界内
    for (const auto& g : batchOr->glyphs) {
        E2E_ASSERT_GT(g.width, 0);
        E2E_ASSERT_GT(g.height, 0);
        E2E_ASSERT_LE(g.atlasX + static_cast<uint32_t>(g.width), batchOr->atlasWidth);
        E2E_ASSERT_LE(g.atlasY + static_cast<uint32_t>(g.height), batchOr->atlasHeight);
    }
    return true;
}

E2E_TEST(Tier2, FEAT_45, FontRaster_OutOfBoundsUnicodeFallback) {
    ScopedSystemFontSession fontSession;
    E2E_ASSERT(fontSession.initStatus().ok(), fontSession.initStatus().ToString());

    const std::array<::FCT::SAI::GlyphRequest, 2> invalidRequests = {
        ::FCT::SAI::GlyphRequest{0x110000u, 20, 4, false, false},
        ::FCT::SAI::GlyphRequest{0xFFFFFFFFu, 20, 4, false, false}
    };

    // 同时传入不存在字体名与超 Unicode 上界码点，验证系统字库回退与空白替代保护无崩溃
    const auto batchOr = fontSession.get()->rasterizeBatch(
        "NonExistentFont_Tier2_Fallback_XYZ", 20, 4, false, invalidRequests);
    E2E_ASSERT(batchOr.ok(), batchOr.status().ToString());
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 2u);
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::R8_UNORM);
    E2E_ASSERT_GT(batchOr->glyphs[0].advanceX, 0);
    E2E_ASSERT_GT(batchOr->glyphs[1].advanceX, 0);
    return true;
}

E2E_TEST(Tier2, FEAT_45, FontRaster_FormatChannelBoundaryCheck) {
    ScopedSystemFontSession fontSession;
    E2E_ASSERT(fontSession.initStatus().ok(), fontSession.initStatus().ToString());

    const std::array<::FCT::SAI::GlyphRequest, 1> hanziReq = {
        ::FCT::SAI::GlyphRequest{0x5B57u, 24, 4, false, false}
    };
    const auto hanziBatchOr = fontSession.get()->rasterizeBatch("Microsoft YaHei", 24, 4, false, hanziReq);
    E2E_ASSERT(hanziBatchOr.ok(), hanziBatchOr.status().ToString());
    E2E_ASSERT_EQ(hanziBatchOr->format, ::FCT::SAI::FontPixelFormat::R8_UNORM);
    E2E_ASSERT_EQ(
        hanziBatchOr->pixelData.size(),
        static_cast<size_t>(hanziBatchOr->atlasWidth) * static_cast<size_t>(hanziBatchOr->atlasHeight) * 1u);

    const std::array<::FCT::SAI::GlyphRequest, 1> emojiReq = {
        ::FCT::SAI::GlyphRequest{0x1F525u, 24, 4, false, true}
    };
    const auto emojiBatchOr = fontSession.get()->rasterizeBatch("Segoe UI Emoji", 24, 4, false, emojiReq);
    E2E_ASSERT(emojiBatchOr.ok(), emojiBatchOr.status().ToString());
    E2E_ASSERT_EQ(emojiBatchOr->format, ::FCT::SAI::FontPixelFormat::RGBA8_UNORM);
    E2E_ASSERT_EQ(
        emojiBatchOr->pixelData.size(),
        static_cast<size_t>(emojiBatchOr->atlasWidth) * static_cast<size_t>(emojiBatchOr->atlasHeight) * 4u);
    return true;
}

E2E_TEST(Tier2, FEAT_45, FontRaster_MixedCodepointStreamIntegrity) {
    ScopedSystemFontSession fontSession;
    E2E_ASSERT(fontSession.initStatus().ok(), fontSession.initStatus().ToString());

    const std::array<::FCT::SAI::GlyphRequest, 3> mixedRequests = {
        ::FCT::SAI::GlyphRequest{static_cast<uint32_t>('X'), 22, 4, false, false},
        ::FCT::SAI::GlyphRequest{0x9053u, 22, 4, false, false},
        ::FCT::SAI::GlyphRequest{0x1F680u, 22, 4, false, true}
    };

    const auto batchOr = fontSession.get()->rasterizeBatch("Microsoft YaHei", 22, 4, false, mixedRequests);
    E2E_ASSERT(batchOr.ok(), batchOr.status().ToString());
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 3u);
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::RGBA8_UNORM);
    E2E_ASSERT_GT(batchOr->metrics.ascent, 0.0f);
    E2E_ASSERT_GE(batchOr->metrics.lineSpacing, batchOr->metrics.ascent);

    E2E_ASSERT_EQ(batchOr->glyphs[0].codepoint, static_cast<uint32_t>('X'));
    E2E_ASSERT_EQ(batchOr->glyphs[1].codepoint, 0x9053u);
    E2E_ASSERT_EQ(batchOr->glyphs[2].codepoint, 0x1F680u);
    for (const auto& glyph : batchOr->glyphs) {
        E2E_ASSERT_GT(glyph.advanceX, 0);
        E2E_ASSERT_LE(glyph.atlasX + static_cast<uint32_t>(glyph.width), batchOr->atlasWidth);
        E2E_ASSERT_LE(glyph.atlasY + static_cast<uint32_t>(glyph.height), batchOr->atlasHeight);
    }
    return true;
}

// ============================================================================
// [FEAT-46] 跨平台虚拟软键盘原语 - Tier 2 边界极限测试
// ============================================================================

namespace {

class ScopedVirtualKeyboardReset {
public:
    ScopedVirtualKeyboardReset() {
        (void)::FCT::SAI::VirtualKeyboard::hide();
        ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    }

    ~ScopedVirtualKeyboardReset() {
        (void)::FCT::SAI::VirtualKeyboard::hide();
        ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    }

    ScopedVirtualKeyboardReset(const ScopedVirtualKeyboardReset&) = delete;
    ScopedVirtualKeyboardReset& operator=(const ScopedVirtualKeyboardReset&) = delete;
};

struct KeyboardCallbackCapture {
    uint32_t callCount{0};
    std::string lastText;
};

struct KeyboardCaptureFunctor {
    KeyboardCallbackCapture* capture{nullptr};

    void operator()(std::string_view text) const {
        if (capture != nullptr) {
            ++capture->callCount;
            capture->lastText = std::string(text);
        }
    }
};

} // namespace

E2E_TEST(Tier2, FEAT_46, Keyboard_EmptyTextInputSafe) {
    ScopedVirtualKeyboardReset kbScope;

    ::FCT::SAI::VirtualKeyboardConfig cfg{};
    cfg.defaultValue = "prefill";
    cfg.maxLength = 50;
    cfg.confirmType = ::FCT::SAI::KeyboardConfirmType::Done;

    const auto showSt = ::FCT::SAI::VirtualKeyboard::show(cfg);
    E2E_ASSERT(showSt.ok(), showSt.ToString());
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());

    KeyboardCallbackCapture inputCapture{0u, "non_empty_sentinel"};
    ::FCT::SAI::VirtualKeyboard::setOnInput(KeyboardCaptureFunctor{&inputCapture});

    ::FCT::SAI::VirtualKeyboard::simulateInput("");
    E2E_ASSERT_EQ(inputCapture.callCount, 1u);
    E2E_ASSERT_TRUE(inputCapture.lastText.empty());

    // 验证宿主 C FFI 传入 nullptr 时降级为空字符串而不崩溃
    inputCapture.lastText = "dirty";
    fct_keyboard_dispatch_input(nullptr);
    E2E_ASSERT_EQ(inputCapture.callCount, 2u);
    E2E_ASSERT_TRUE(inputCapture.lastText.empty());
    return true;
}

E2E_TEST(Tier2, FEAT_46, Keyboard_MaxLengthStrictTruncation) {
    ScopedVirtualKeyboardReset kbScope;

    KeyboardCallbackCapture inputCapture{};
    ::FCT::SAI::VirtualKeyboard::setOnInput(KeyboardCaptureFunctor{&inputCapture});

    // 1. 纯 ASCII 超长截断至精确 10 个字符
    ::FCT::SAI::VirtualKeyboardConfig asciiCfg{};
    asciiCfg.maxLength = 10;
    asciiCfg.confirmType = ::FCT::SAI::KeyboardConfirmType::Send;
    const auto showAsciiSt = ::FCT::SAI::VirtualKeyboard::show(asciiCfg);
    E2E_ASSERT(showAsciiSt.ok(), showAsciiSt.ToString());

    ::FCT::SAI::VirtualKeyboard::simulateInput("1234567890EXTRA_CHARS_MUST_DROP");
    E2E_ASSERT_EQ(inputCapture.lastText, "1234567890");
    E2E_ASSERT_EQ(inputCapture.lastText.size(), 10u);

    // 2. 多字节 UTF-8 混合字符流按 Unicode 码点数截断，绝不劈裂 3 字节汉字或 4 字节 Emoji
    ::FCT::SAI::VirtualKeyboardConfig utf8Cfg{};
    utf8Cfg.maxLength = 5;
    const auto showUtf8St = ::FCT::SAI::VirtualKeyboard::show(utf8Cfg);
    E2E_ASSERT(showUtf8St.ok(), showUtf8St.ToString());

    const std::string mixedUtf8 = "A\xE4\xB8\xAD\xF0\x9F\x98\x80" "B\xE6\x96\x87\xF0\x9F\x9A\x80" "C";
    const std::string expectedFiveCodePoints = "A\xE4\xB8\xAD\xF0\x9F\x98\x80" "B\xE6\x96\x87";
    ::FCT::SAI::VirtualKeyboard::simulateInput(mixedUtf8);
    E2E_ASSERT_EQ(inputCapture.lastText, expectedFiveCodePoints);
    E2E_ASSERT_EQ(inputCapture.lastText.size(), 12u);

    // 3. maxLength = 0 极限边界：任何输入均被截断为空串
    ::FCT::SAI::VirtualKeyboardConfig zeroMaxCfg{};
    zeroMaxCfg.maxLength = 0;
    const auto showZeroSt = ::FCT::SAI::VirtualKeyboard::show(zeroMaxCfg);
    E2E_ASSERT(showZeroSt.ok(), showZeroSt.ToString());
    ::FCT::SAI::VirtualKeyboard::simulateInput("blocked_text");
    E2E_ASSERT_TRUE(inputCapture.lastText.empty());
    return true;
}

E2E_TEST(Tier2, FEAT_46, Keyboard_HideIdempotenceAndEmptyCallbackSafety) {
    ScopedVirtualKeyboardReset kbScope;

    // 初始未展开状态下 hide() 幂等成功
    const auto initialHideSt = ::FCT::SAI::VirtualKeyboard::hide();
    E2E_ASSERT(initialHideSt.ok(), initialHideSt.ToString());
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());

    KeyboardCallbackCapture inputCapture{};
    ::FCT::SAI::VirtualKeyboard::setOnInput(KeyboardCaptureFunctor{&inputCapture});

    // 未展开状态下注入输入/确认事件应被安全丢弃
    ::FCT::SAI::VirtualKeyboard::simulateInput("ignored_while_hidden");
    E2E_ASSERT_EQ(inputCapture.callCount, 0u);

    ::FCT::SAI::VirtualKeyboardConfig cfg{};
    cfg.defaultValue = "test";
    cfg.maxLength = 20;
    cfg.confirmType = ::FCT::SAI::KeyboardConfirmType::Go;
    const auto showSt = ::FCT::SAI::VirtualKeyboard::show(cfg);
    E2E_ASSERT(showSt.ok(), showSt.ToString());

    // 清空回调后触发 confirm / complete / C FFI 均安全无崩溃
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    ::FCT::SAI::VirtualKeyboard::simulateConfirm("ok");
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());

    const auto dupHideSt = ::FCT::SAI::VirtualKeyboard::hide();
    E2E_ASSERT(dupHideSt.ok(), dupHideSt.ToString());
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());
    return true;
}

E2E_TEST(Tier2, FEAT_46, Keyboard_ConfirmTypeSupportList) {
    ScopedVirtualKeyboardReset kbScope;

    const std::array<::FCT::SAI::KeyboardConfirmType, 5> allTypes = {
        ::FCT::SAI::KeyboardConfirmType::Done,
        ::FCT::SAI::KeyboardConfirmType::Next,
        ::FCT::SAI::KeyboardConfirmType::Search,
        ::FCT::SAI::KeyboardConfirmType::Go,
        ::FCT::SAI::KeyboardConfirmType::Send
    };

    for (auto confirmType : allTypes) {
        ::FCT::SAI::VirtualKeyboardConfig cfg{};
        cfg.defaultValue = "txt";
        cfg.maxLength = 100;
        cfg.confirmHold = true;
        cfg.confirmType = confirmType;

        const auto st = ::FCT::SAI::VirtualKeyboard::show(cfg);
        E2E_ASSERT(st.ok(), st.ToString());
        E2E_ASSERT_EQ(::FCT::SAI::VirtualKeyboard::getCurrentConfig().confirmType, confirmType);
    }

    // 验证 confirmHold=true 时点击确认键保持软键盘展开
    ::FCT::SAI::VirtualKeyboard::simulateConfirm("keep_open");
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());
    return true;
}

E2E_TEST(Tier2, FEAT_46, Keyboard_RapidShowHideTransition) {
    ScopedVirtualKeyboardReset kbScope;

    KeyboardCallbackCapture completeCapture{};
    ::FCT::SAI::VirtualKeyboard::setOnComplete(KeyboardCaptureFunctor{&completeCapture});

    for (int i = 0; i < 20; ++i) {
        ::FCT::SAI::VirtualKeyboardConfig cfg{};
        cfg.defaultValue = absl::StrCat("text_", i);
        cfg.maxLength = 50;
        cfg.confirmType = ::FCT::SAI::KeyboardConfirmType::Search;

        const auto showSt = ::FCT::SAI::VirtualKeyboard::show(cfg);
        E2E_ASSERT(showSt.ok(), showSt.ToString());
        E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());

        ::FCT::SAI::VirtualKeyboard::simulateComplete(cfg.defaultValue);
        E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());
    }

    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());
    E2E_ASSERT_EQ(completeCapture.callCount, 20u);
    E2E_ASSERT_EQ(completeCapture.lastText, "text_19");
    E2E_ASSERT_EQ(::FCT::SAI::VirtualKeyboard::getCurrentConfig().defaultValue, "text_19");
    return true;
}

} // namespace MQEngine::Testing
