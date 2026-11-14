#include "Common/E2ECommon.h"

#include <FCT_Compute.h>
#ifdef CHECK_STATUS
#undef CHECK_STATUS
#endif
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/RHI/TransformFeedback.h>
#include <FCT/RHI/WGL2_TransformFeedback.h>
#include <FCT/RHI/WGL2_Swapchain.h>
#include <FCT/UI/EventHandler.h>
#include <FCT/UI/MiniGame_Window.h>
#include <FCT/UI/CallBackHandler.h>
#include <FCT/Runtime/Emscripten_Runtime.h>
#include <FCT_SAI/FileSystem.h>
#include <FCT_SAI/SystemFontProvider.h>
#include <FCT_SAI/VirtualKeyboard.h>
#include <FCT_WebJS/WebJS.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct FctComputeTier1Particle {
    ::FCT::Compute::Float4 position;
    ::FCT::Compute::Float4 velocity;
    float mass;
    uint32_t flags;
};

FCT_COMPUTE_STRUCT(FctComputeTier1Particle, position, velocity, mass, flags);

namespace MQEngine::Testing {

namespace {

struct Tier1FctResizeCapture {
    bool fired{false};
    int width{0};
    int height{0};

    void operator()(::FCT::Window* /*wnd*/, int w, int h) {
        fired = true;
        width = w;
        height = h;
    }
};

struct Tier1FctResizePtrObserver {
    Tier1FctResizeCapture* target{nullptr};

    void operator()(::FCT::Window* wnd, int w, int h) const {
        if (target) {
            (*target)(wnd, w, h);
        }
    }
};

struct Tier1FctFrameCounter {
    uint32_t* counter{nullptr};

    void operator()(double /*dt*/) const {
        if (counter) {
            ++(*counter);
        }
    }
};

struct Tier1FctMouseClickCounter {
    int* count{nullptr};
    int* lastX{nullptr};
    int* lastY{nullptr};

    void operator()(::FCT::Window* /*wnd*/, int x, int y) const {
        if (count) {
            ++(*count);
        }
        if (lastX) {
            *lastX = x;
        }
        if (lastY) {
            *lastY = y;
        }
    }
};

struct Tier1FctTouchRecorder {
    std::vector<::FCT::TouchPhase>* phases{nullptr};
    size_t* lastTouchCount{nullptr};

    void operator()(::FCT::Window* /*wnd*/, const ::FCT::TouchEvent& evt) const {
        if (phases) {
            phases->push_back(evt.phase);
        }
        if (lastTouchCount) {
            *lastTouchCount = evt.touches.size();
        }
    }
};

struct Tier1FctSandboxVfsScope {
    E2ETestBase sandbox;
    std::string pkgDir;
    std::string cacheDir;

    Tier1FctSandboxVfsScope() {
        sandbox.setUp();
        const auto pkgPath = sandbox.getSandboxFilePath("pkg");
        const auto cachePath = sandbox.getSandboxFilePath("cache");
        std::error_code ec;
        std::filesystem::create_directories(pkgPath, ec);
        std::filesystem::create_directories(cachePath, ec);
        pkgDir = pkgPath.generic_string();
        cacheDir = cachePath.generic_string();
    }

    ::FCT::Status mountZones() const {
        auto sPkg = ::FCT::SAI::FileSystem::setPkgRoot(pkgDir);
        if (!sPkg.ok()) {
            return sPkg;
        }
        return ::FCT::SAI::FileSystem::setCacheRoot(cacheDir);
    }

    ::FCT::Status seedPkgBinary(std::string_view relSubpath, std::span<const uint8_t> bytes) const {
        const auto fullPath = (std::filesystem::path(pkgDir) / std::string(relSubpath)).generic_string();
        return ::FCT::SAI::FileSystem::writeBinaryFile(fullPath, bytes.data(), bytes.size(), true);
    }
};

struct Tier1FctAsyncBinaryReadReceiver {
    std::promise<::FCT::StatusOr<std::vector<uint8_t>>>* promise{nullptr};

    void operator()(::FCT::StatusOr<std::vector<uint8_t>> result) const {
        if (promise) {
            promise->set_value(std::move(result));
        }
    }
};

struct Tier1FctAsyncDownloadReceiver {
    std::promise<::FCT::Status>* promise{nullptr};

    void operator()(::FCT::Status status) const {
        if (promise) {
            promise->set_value(std::move(status));
        }
    }
};

struct Tier1FctHostAddMethod {
    ::FCT::JS::Value operator()(const std::vector<::FCT::JS::Value>& args) const {
        if (args.size() >= 2) {
            return ::FCT::JS::Value(args[0].toInt32() + args[1].toInt32());
        }
        return ::FCT::JS::Value(0);
    }
};

struct Tier1FctPromiseResolveReceiver {
    int* outVal{nullptr};
    bool* outTriggered{nullptr};

    void operator()(const ::FCT::JS::Value& val) const {
        if (outTriggered) {
            *outTriggered = true;
        }
        if (outVal) {
            *outVal = val.toInt32();
        }
    }
};

struct Tier1FctKeyboardInputReceiver {
    std::string* outText{nullptr};

    void operator()(std::string_view text) const {
        if (outText) {
            outText->assign(text.data(), text.size());
        }
    }
};

struct Tier1FctKeyboardConfirmReceiver {
    bool* outConfirmed{nullptr};
    std::string* outText{nullptr};

    void operator()(std::string_view text) const {
        if (outConfirmed) {
            *outConfirmed = true;
        }
        if (outText) {
            outText->assign(text.data(), text.size());
        }
    }
};

} // namespace

// ============================================================================
// [FEAT-38] FCT_Compute 编译期静态多态 LuisaBackend 与零开销抽象 (PRD 06 §1)
// ============================================================================

static_assert(::FCT::Compute::ComputeBackendConcept<::FCT::Compute::ActiveBackend>,
              "ActiveBackend 必须满足 ComputeBackendConcept 静态多态契约");
static_assert(std::same_as<::FCT::Compute::ActiveBackend, ::FCT::Compute::LuisaBackend>,
              "桌面端 FCT_Compute 默认后端必须为 LuisaBackend");
static_assert(sizeof(::FCT::Compute::ComputeContext) == sizeof(::FCT::Compute::LuisaBackend::ContextImpl),
              "ComputeContext 必须保持与底层后端 ContextImpl 零开销同尺寸");

E2E_TEST(Tier1, FEAT_38, FctCompute_LuisaBackendConceptAndNames) {
    E2E_ASSERT_EQ(::FCT::Compute::ComputeContext::computeBackendName(), "LuisaCompute");
    E2E_ASSERT_EQ(::FCT::Compute::ComputeDevice::computeBackendName(), "LuisaCompute");
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(::FCT::Compute::HardwareBackend::Cuda), "cuda");
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(::FCT::Compute::HardwareBackend::DirectX), "dx");
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(::FCT::Compute::HardwareBackend::Vulkan), "vk");
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(::FCT::Compute::HardwareBackend::Metal), "metal");
    E2E_ASSERT_EQ(::FCT::Compute::ToHardwareBackendName(::FCT::Compute::HardwareBackend::Fallback), "fallback");
    return true;
}

E2E_TEST(Tier1, FEAT_38, FctCompute_MathConversionHostBackendRoundTrip) {
    const ::FCT::Vec2 v2(1.25f, -3.5f);
    const ::FCT::Vec3<> v3(4.0f, 5.5f, -6.25f);
    const ::FCT::Vec4 v4(7.0f, -8.0f, 9.5f, 1.0f);

    const auto bv2 = ::FCT::Compute::ToBackendVec(v2);
    const auto bv3 = ::FCT::Compute::ToBackendVec(v3);
    const auto bv4 = ::FCT::Compute::ToBackendVec(v4);

    const auto rv2 = ::FCT::Compute::ToHostVec(bv2);
    const auto rv3 = ::FCT::Compute::ToHostVec(bv3);
    const auto rv4 = ::FCT::Compute::ToHostVec(bv4);

    E2E_ASSERT_EQ(rv2.x, 1.25f);
    E2E_ASSERT_EQ(rv2.y, -3.5f);
    E2E_ASSERT_EQ(rv3.x, 4.0f);
    E2E_ASSERT_EQ(rv3.y, 5.5f);
    E2E_ASSERT_EQ(rv3.z, -6.25f);
    E2E_ASSERT_EQ(rv4.x, 7.0f);
    E2E_ASSERT_EQ(rv4.w, 1.0f);

    const ::FCT::Mat4 hostMat(
        1.0f,  2.0f,  3.0f,  4.0f,
        5.0f,  6.0f,  7.0f,  8.0f,
        9.0f,  10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f);
    const auto backendMat = ::FCT::Compute::ToBackendMat4(hostMat);
    // 验证 FCT 行主序与 Luisa 列主序互转严格一致
    E2E_ASSERT_EQ(backendMat[0].y, 5.0f);
    E2E_ASSERT_EQ(backendMat[3].x, 4.0f);
    E2E_ASSERT_EQ(backendMat[2].w, 15.0f);

    const ::FCT::Mat4 restored = ::FCT::Compute::ToHostMat4(backendMat);
    for (size_t i = 0; i < 16u; ++i) {
        E2E_ASSERT_EQ(restored.m[i], hostMat.m[i]);
    }
    return true;
}

E2E_TEST(Tier1, FEAT_38, FctCompute_PackedFloat3AndStorageLayoutTraits) {
    using PackedT = ::FCT::Compute::StorageType_t<::FCT::Vec3<>>;
    static_assert(std::same_as<PackedT, ::FCT::Compute::PackedFloat3>);
    static_assert(sizeof(PackedT) == 12u);
    static_assert(::FCT::Compute::IsLayoutIdentical_v<::FCT::Vec3<>, PackedT>);
    static_assert(::FCT::Compute::BufferElementConcept<::FCT::Vec3<>>);
    static_assert(::FCT::Compute::BufferElementConcept<FctComputeTier1Particle>);

    const ::FCT::Vec3<> srcVec(1.5f, -2.25f, 3.75f);
    const ::FCT::Compute::PackedFloat3 packed(srcVec);
    const ::FCT::Vec3<> unpacked = static_cast<::FCT::Vec3<>>(packed);
    E2E_ASSERT_EQ(unpacked.x, 1.5f);
    E2E_ASSERT_EQ(unpacked.y, -2.25f);
    E2E_ASSERT_EQ(unpacked.z, 3.75f);
    return true;
}

E2E_TEST(Tier1, FEAT_38, FctCompute_FormatTraitsAndPixelFormatMapping) {
    static_assert(::FCT::Compute::SupportedFormatConcept<::FCT::Format::R8G8B8A8_UNORM>);
    static_assert(::FCT::Compute::SupportedFormatConcept<::FCT::Format::R32G32B32A32_SFLOAT>);
    static_assert(::FCT::Compute::FormatTraits<::FCT::Format::R32G32B32A32_SFLOAT>::channels == 4u);
    static_assert(::FCT::Compute::FormatTraits<::FCT::Format::R32G32B32A32_SFLOAT>::byteSize == 16u);

    const auto rgba8FmtOr = ::FCT::Compute::ToBackendPixelFormat(::FCT::Format::R8G8B8A8_UNORM);
    E2E_ASSERT_TRUE(rgba8FmtOr.ok());
    E2E_ASSERT_EQ(::FCT::Compute::FromBackendPixelFormat(*rgba8FmtOr), ::FCT::Format::R8G8B8A8_UNORM);

    const auto rgba32FmtOr = ::FCT::Compute::ToBackendPixelFormat(::FCT::Format::R32G32B32A32_SFLOAT);
    E2E_ASSERT_TRUE(rgba32FmtOr.ok());
    E2E_ASSERT_EQ(::FCT::Compute::FromBackendPixelFormat(*rgba32FmtOr), ::FCT::Format::R32G32B32A32_SFLOAT);

    const auto storageOr = ::FCT::Compute::ToBackendPixelStorage(::FCT::Format::R16G16B16A16_SFLOAT);
    E2E_ASSERT_TRUE(storageOr.ok());
    E2E_ASSERT_EQ(*storageOr, ::FCT::Compute::ToBackendPixelStorage<::FCT::Format::R16G16B16A16_SFLOAT>());
    return true;
}

E2E_TEST(Tier1, FEAT_38, FctCompute_StructReflectionAndCommandBatchInvariants) {
    static_assert(::FCT::Compute::AggregateStructConcept<FctComputeTier1Particle>);
    static_assert(::FCT::Compute::Detail::DetectAggregateFieldCount<FctComputeTier1Particle>() == 4u);

    FctComputeTier1Particle particle{
        ::FCT::Compute::Float4(1.0f, 2.0f, 3.0f, 1.0f),
        ::FCT::Compute::Float4(0.0f, 9.8f, 0.0f, 0.0f),
        4.5f,
        7u};
    auto fields = ::FCT::Compute::Detail::StructuredBindingExtractor<4u>::toTuple(particle);
    E2E_ASSERT_EQ(std::get<2>(fields), 4.5f);
    E2E_ASSERT_EQ(std::get<3>(fields), 7u);

    ::FCT::Compute::CommandBatch batch;
    E2E_ASSERT_TRUE(batch.empty());
    ::FCT::Compute::CommandBatch movedBatch(std::move(batch));
    E2E_ASSERT_TRUE(movedBatch.empty());

    ::FCT::Compute::BasicBufferView<float> emptyBufView;
    E2E_ASSERT_FALSE(emptyBufView.valid());
    E2E_ASSERT_FALSE(static_cast<bool>(emptyBufView));
    return true;
}

// ============================================================================
// [FEAT-39] RHI TransformFeedback 辅助原语 (PRD 06 §1)
// ============================================================================

E2E_TEST(Tier1, FEAT_39, XFB_BeginTransformFeedbackRecord) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);
    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_TRUE(xfb != nullptr);

    E2E_ASSERT_STATUS_OK(xfb->create());
    E2E_ASSERT_GT(xfb->getNativeHandle(), 0u);
    E2E_ASSERT_FALSE(xfb->isActive());

    E2E_ASSERT_STATUS_OK(xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Points));
    E2E_ASSERT_TRUE(xfb->isActive());
    E2E_ASSERT_EQ(xfb->getCurrentMode(), ::FCT::RHI::FeedbackPrimitiveMode::Points);
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), xfb->getNativeHandle());

    E2E_ASSERT_STATUS_OK(xfb->end());
    return true;
}

E2E_TEST(Tier1, FEAT_39, XFB_RasterizerDiscardEnable) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);
    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_TRUE(xfb != nullptr);
    E2E_ASSERT_STATUS_OK(xfb->create());

    // WebGL2 纯计算仿真通道：开启 GL_RASTERIZER_DISCARD 后执行 TransformFeedback 捕获
    E2E_ASSERT_TRUE(::glEnable != nullptr);
    E2E_ASSERT_TRUE(::glDisable != nullptr);
    ::glEnable(GL_RASTERIZER_DISCARD);

    E2E_ASSERT_STATUS_OK(xfb->bindBufferBase(0u, 201u));
    E2E_ASSERT_STATUS_OK(xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Points));
    E2E_ASSERT_TRUE(xfb->isActive());
    E2E_ASSERT_FALSE(xfb->isPaused());

    E2E_ASSERT_STATUS_OK(xfb->end());
    ::glDisable(GL_RASTERIZER_DISCARD);
    E2E_ASSERT_FALSE(xfb->isActive());

    return true;
}

E2E_TEST(Tier1, FEAT_39, XFB_EndTransformFeedbackRecord) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);
    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_STATUS_OK(xfb->create());

    // 未激活时调用 end() 必须被拦截
    const ::FCT::Status endBeforeBegin = xfb->end();
    E2E_ASSERT_FALSE(endBeforeBegin.ok());

    E2E_ASSERT_STATUS_OK(xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Triangles));
    E2E_ASSERT_STATUS_OK(xfb->pause());
    E2E_ASSERT_TRUE(xfb->isPaused());

    // 暂停态直接调用 end() 必须被拦截，resume() 后才允许结束
    const ::FCT::Status endWhilePaused = xfb->end();
    E2E_ASSERT_FALSE(endWhilePaused.ok());

    E2E_ASSERT_STATUS_OK(xfb->resume());
    E2E_ASSERT_FALSE(xfb->isPaused());
    E2E_ASSERT_STATUS_OK(xfb->end());
    E2E_ASSERT_FALSE(xfb->isActive());

    xfb->unbind();
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_39, XFB_BufferBindingRange) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);
    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_STATUS_OK(xfb->create());

    E2E_ASSERT_STATUS_OK(xfb->bindBufferRange(0u, 10u, 0u, 4096u));
    E2E_ASSERT_STATUS_OK(xfb->bindBufferRange(3u, 13u, 512u, 2048u));

    // WebGL2 / GLES 3.0 最多支持 4 个绑定槽位 (0..3)，槽位 4 必须返回参数错误
    const ::FCT::Status outOfBoundsStatus = xfb->bindBufferRange(4u, 14u, 0u, 1024u);
    E2E_ASSERT_FALSE(outOfBoundsStatus.ok());
    E2E_ASSERT_EQ(outOfBoundsStatus.code(), absl::StatusCode::kInvalidArgument);

    return true;
}

E2E_TEST(Tier1, FEAT_39, XFB_ExecutionSequenceInSubmit) {
    ::FCT::WGL2_Context ctx(nullptr);
    ::FCT::WGL2_Device device(&ctx);
    ::FCT::UniquePtr<::FCT::RHI::TransformFeedback> xfb(device.newRhiTransformFeedback());
    E2E_ASSERT_STATUS_OK(xfb->create());

    E2E_ASSERT_STATUS_OK(xfb->bindBufferRange(0u, 88u, 0u, 16000u));
    ::glEnable(GL_RASTERIZER_DISCARD);
    E2E_ASSERT_STATUS_OK(xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Points));

    // 激活期间重复 begin 必须返回前置状态错误
    const ::FCT::Status duplicateBegin = xfb->begin(::FCT::RHI::FeedbackPrimitiveMode::Lines);
    E2E_ASSERT_FALSE(duplicateBegin.ok());

    ::glDrawArrays(GL_POINTS, 0, 1000);
    E2E_ASSERT_STATUS_OK(xfb->end());
    ::glDisable(GL_RASTERIZER_DISCARD);

    xfb->destroy();
    E2E_ASSERT_EQ(xfb->getNativeHandle(), 0u);
    E2E_ASSERT_EQ(ctx.getCurrentBoundTransformFeedback(), 0u);
    return true;
}

// ============================================================================
// [FEAT-40] MiniGame 画布窗口与 DPR 缩放 (PRD 06 §2.1)
// ============================================================================

E2E_TEST(Tier1, FEAT_40, MiniGameWindow_DprCapDefault) {
    ::FCT::MiniGame_Window win(nullptr, 390, 844, 3.0f, 2.0f);

    E2E_ASSERT_EQ(win.getEffectiveDpr(), 2.0f);
    E2E_ASSERT_EQ(win.getWidth(), 780);
    E2E_ASSERT_EQ(win.getHeight(), 1688);

    return true;
}

E2E_TEST(Tier1, FEAT_40, MiniGameWindow_CanvasResolutionScaling) {
    ::FCT::MiniGame_Window win(nullptr, 390, 844, 2.0f, 2.0f);

    E2E_ASSERT_EQ(win.getCssWidth(), 390);
    E2E_ASSERT_EQ(win.getCssHeight(), 844);
    E2E_ASSERT_EQ(win.getWidth(), 780);
    E2E_ASSERT_EQ(win.getHeight(), 1688);

    const ::FCT::Vec2 physPos = win.cssToPhysical(100.0f, 200.0f);
    E2E_ASSERT_EQ(physPos.x, 200.0f);
    E2E_ASSERT_EQ(physPos.y, 400.0f);

    const ::FCT::Vec2 cssPos = win.physicalToCss(physPos.x, physPos.y);
    E2E_ASSERT_EQ(cssPos.x, 100.0f);
    E2E_ASSERT_EQ(cssPos.y, 200.0f);

    return true;
}

E2E_TEST(Tier1, FEAT_40, MiniGameWindow_LowDprUncapped) {
    ::FCT::MiniGame_Window win(nullptr, 390, 844, 1.5f, 2.0f);

    E2E_ASSERT_EQ(win.getEffectiveDpr(), 1.5f);
    E2E_ASSERT_EQ(win.getWidth(), 585);
    E2E_ASSERT_EQ(win.getHeight(), 1266);

    return true;
}

E2E_TEST(Tier1, FEAT_40, MiniGameWindow_ContextAttributes) {
    ::FCT::WGL2_Context ctx(nullptr);
    E2E_ASSERT_TRUE(ctx.isIntegrateGpu());
    E2E_ASSERT_FALSE(ctx.isRayTracingSupported());
    E2E_ASSERT_FALSE(ctx.isMeshShaderSupported());
    E2E_ASSERT_FALSE(ctx.isComputeSupported());

    ::FCT::RHI::WGL2_Swapchain swapchain(&ctx);
    swapchain.setCanvasTarget(true);
    E2E_ASSERT_TRUE(swapchain.isCanvasTarget());
    E2E_ASSERT_EQ(swapchain.getImageCount(), 1u);
    swapchain.present();

    return true;
}

E2E_TEST(Tier1, FEAT_40, MiniGameWindow_ResizeCallback) {
    ::FCT::MiniGame_Window win(nullptr, 400, 300, 2.0f, 2.0f);

    Tier1FctResizeCapture capture{};
    win.getCallBack()->addResizeCallback(Tier1FctResizePtrObserver{&capture});

    win.onCanvasResize(800, 600, 2.0f);
    E2E_ASSERT_TRUE(capture.fired);
    E2E_ASSERT_EQ(capture.width, 1600);
    E2E_ASSERT_EQ(capture.height, 1200);

    return true;
}

// ============================================================================
// [FEAT-41] Emscripten 非阻塞帧循环与输入 (PRD 06 §2.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_41, Runtime_NonBlockingSingleTick) {
    ::FCT::Emscripten_Runtime runtime;
    uint32_t frameCount = 0;

    runtime.startMainLoop(Tier1FctFrameCounter{&frameCount}, 60);
    E2E_ASSERT_TRUE(runtime.isLoopRunning());
    E2E_ASSERT_EQ(runtime.getTargetFps(), 60);

    runtime.stepFrame(0.016667);
    E2E_ASSERT_EQ(frameCount, 1u);
    runtime.stepFrame(0.016667);
    E2E_ASSERT_EQ(frameCount, 2u);

    runtime.stopMainLoop();
    E2E_ASSERT_FALSE(runtime.isLoopRunning());
    return true;
}

E2E_TEST(Tier1, FEAT_41, Runtime_TouchCoordinateNormalization) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window wnd(&runtime, 390, 844, 2.0f, 2.0f);
    runtime.setTargetWindow(&wnd);

    runtime.injectTouchStart(0, 195.0f, 422.0f);

    const auto& touches = runtime.getActiveTouches();
    E2E_ASSERT_EQ(touches.size(), 1u);
    const auto itTouch = touches.find(0);
    E2E_ASSERT_TRUE(itTouch != touches.end());
    E2E_ASSERT_EQ(itTouch->second.logicalX, 195.0f);
    E2E_ASSERT_EQ(itTouch->second.logicalY, 422.0f);

    const ::FCT::Vec2 phys = wnd.cssToPhysical(itTouch->second.logicalX, itTouch->second.logicalY);
    const float normX = phys.x / static_cast<float>(wnd.getWidth());
    const float normY = phys.y / static_cast<float>(wnd.getHeight());
    E2E_ASSERT_TRUE(std::abs(normX - 0.5f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(normY - 0.5f) < 1e-5f);

    return true;
}

E2E_TEST(Tier1, FEAT_41, Runtime_MultiTouchDispatch) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window wnd(&runtime, 800, 600, 2.0f, 2.0f);
    runtime.setTargetWindow(&wnd);

    int primaryMouseDowns = 0;
    int mouseX = 0;
    int mouseY = 0;
    wnd.getCallBack()->addLButtonDownCallback(
        Tier1FctMouseClickCounter{&primaryMouseDowns, &mouseX, &mouseY});

    runtime.injectTouchStart(101, 100.0f, 200.0f);
    runtime.injectTouchStart(102, 300.0f, 400.0f);

    const auto& touches = runtime.getActiveTouches();
    E2E_ASSERT_EQ(touches.size(), 2u);
    E2E_ASSERT_TRUE(touches.find(101) != touches.end());
    E2E_ASSERT_TRUE(touches.find(102) != touches.end());
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 101);
    E2E_ASSERT_EQ(primaryMouseDowns, 1);

    // 主手指抬起后活跃副手指晋升为主触控
    runtime.injectTouchEnd(101, 100.0f, 200.0f);
    E2E_ASSERT_EQ(runtime.getPrimaryTouchId(), 102);

    return true;
}

E2E_TEST(Tier1, FEAT_41, Runtime_TouchPhaseMapping) {
    ::FCT::Emscripten_Runtime runtime;
    ::FCT::MiniGame_Window wnd(&runtime, 375, 667, 2.0f, 2.0f);
    runtime.setTargetWindow(&wnd);

    std::vector<::FCT::TouchPhase> phases;
    size_t lastActiveCount = 0;
    wnd.getCallBack()->addTouchCallback(Tier1FctTouchRecorder{&phases, &lastActiveCount});

    runtime.injectTouchStart(1, 50.0f, 60.0f);
    runtime.injectTouchMove(1, 70.0f, 80.0f);
    runtime.injectTouchEnd(1, 70.0f, 80.0f);
    runtime.injectTouchStart(2, 90.0f, 100.0f);
    runtime.injectTouchCancel(2, 90.0f, 100.0f);

    E2E_ASSERT_EQ(phases.size(), 5u);
    E2E_ASSERT_EQ(phases[0], ::FCT::TouchPhase::Began);
    E2E_ASSERT_EQ(phases[1], ::FCT::TouchPhase::Moved);
    E2E_ASSERT_EQ(phases[2], ::FCT::TouchPhase::Ended);
    E2E_ASSERT_EQ(phases[3], ::FCT::TouchPhase::Began);
    E2E_ASSERT_EQ(phases[4], ::FCT::TouchPhase::Canceled);
    E2E_ASSERT_TRUE(runtime.getActiveTouches().empty());

    return true;
}

E2E_TEST(Tier1, FEAT_41, Runtime_LifecyclePauseResume) {
    ::FCT::Emscripten_Runtime runtime;
    uint32_t frameCount = 0;
    runtime.startMainLoop(Tier1FctFrameCounter{&frameCount}, 60);

    runtime.pauseMainLoop();
    E2E_ASSERT_TRUE(runtime.isPaused());

    runtime.stepFrame(0.016667);
    E2E_ASSERT_EQ(frameCount, 0u);

    runtime.resumeMainLoop();
    E2E_ASSERT_FALSE(runtime.isPaused());
    runtime.stepFrame(0.016667);
    E2E_ASSERT_EQ(frameCount, 1u);

    runtime.stopMainLoop();
    return true;
}

// ============================================================================
// [FEAT-42] 沙箱双区虚拟文件路径映射 (PRD 06 §3.1)
// ============================================================================

E2E_TEST(Tier1, FEAT_42, VFS_PkgPathPrefixMapping) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t payload[2] = {0x01u, 0x02u};
    E2E_ASSERT_STATUS_OK(scope.seedPkgBinary("shaders/pbr.bin", payload));

    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::isVirtualPath("/pkg/shaders/pbr.bin"));
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/pkg/shaders/pbr.bin"));

    auto res = ::FCT::SAI::FileSystem::readBinaryFile("/pkg/shaders/pbr.bin");
    E2E_ASSERT_STATUS_OK(res.status());
    E2E_ASSERT_EQ(res->size(), 2u);
    E2E_ASSERT_EQ((*res)[0], 0x01u);
    E2E_ASSERT_EQ((*res)[1], 0x02u);

    return true;
}

E2E_TEST(Tier1, FEAT_42, VFS_CachePathPrefixMapping) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t data[2] = {0xAAu, 0xBBu};
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::isVirtualPath("/cache/asset_cache.bin"));
    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::writeBinaryFile("/cache/asset_cache.bin", data, sizeof(data), true));
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/cache/asset_cache.bin"));

    return true;
}

E2E_TEST(Tier1, FEAT_42, VFS_RejectWriteToPkgZone) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t byteVal = 0x11u;
    const ::FCT::Status s = ::FCT::SAI::FileSystem::writeBinaryFile("/pkg/readonly_probe.bin", &byteVal, 1u, true);
    E2E_ASSERT_FALSE(s.ok());
    E2E_ASSERT_EQ(s.code(), absl::StatusCode::kPermissionDenied);

    return true;
}

E2E_TEST(Tier1, FEAT_42, VFS_AllowReadWriteInCacheZone) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t byteVal = 0x22u;
    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::writeBinaryFile("/cache/rw_data.bin", &byteVal, 1u, true));

    auto readRes = ::FCT::SAI::FileSystem::readBinaryFile("/cache/rw_data.bin");
    E2E_ASSERT_STATUS_OK(readRes.status());
    E2E_ASSERT_EQ(readRes->size(), 1u);
    E2E_ASSERT_EQ((*readRes)[0], 0x22u);

    return true;
}

E2E_TEST(Tier1, FEAT_42, VFS_UnlinkCacheFile) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t zeroByte = 0x00u;
    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::writeBinaryFile("/cache/temp.bin", &zeroByte, 1u, true));
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/cache/temp.bin"));

    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::removeFile("/cache/temp.bin"));
    E2E_ASSERT_FALSE(::FCT::SAI::FileSystem::exists("/cache/temp.bin"));

    return true;
}

// ============================================================================
// [FEAT-43] 无异常异步文件读取与直存下载 (PRD 06 §3.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_43, AsyncIO_ReadBinarySuccess) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const uint8_t meshPayload[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    E2E_ASSERT_STATUS_OK(scope.seedPkgBinary("mesh.bin", meshPayload));

    std::promise<::FCT::StatusOr<std::vector<uint8_t>>> readPromise;
    auto future = readPromise.get_future();
    ::FCT::SAI::FileSystem::readBinaryFileAsync(
        "/pkg/mesh.bin",
        Tier1FctAsyncBinaryReadReceiver{&readPromise});

    auto res = future.get();
    E2E_ASSERT_STATUS_OK(res.status());
    E2E_ASSERT_EQ(res->size(), 4u);
    E2E_ASSERT_EQ((*res)[0], 0xDEu);
    E2E_ASSERT_EQ((*res)[3], 0xEFu);

    return true;
}

E2E_TEST(Tier1, FEAT_43, AsyncIO_ReadNonExistentReturnsNotFound) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    std::promise<::FCT::StatusOr<std::vector<uint8_t>>> readPromise;
    auto future = readPromise.get_future();
    ::FCT::SAI::FileSystem::readBinaryFileAsync(
        "/pkg/non_existent.bin",
        Tier1FctAsyncBinaryReadReceiver{&readPromise});

    auto res = future.get();
    E2E_ASSERT_FALSE(res.ok());
    E2E_ASSERT_EQ(res.status().code(), absl::StatusCode::kNotFound);

    return true;
}

E2E_TEST(Tier1, FEAT_43, AsyncIO_DownloadFileDirectToDisk) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    const auto cdnSourcePath = scope.sandbox.getSandboxFilePath("cdn_origin/mesh_bundle.bin");
    const uint8_t originBytes[4] = {0x50u, 0x4Bu, 0x03u, 0x04u};
    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::writeBinaryFile(
        cdnSourcePath.generic_string(), originBytes, sizeof(originBytes), true));

    std::promise<::FCT::Status> dlPromise;
    auto future = dlPromise.get_future();
    const std::string fileUrl = "file://" + cdnSourcePath.generic_string();
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        fileUrl,
        "/cache/downloaded_mesh.bin",
        Tier1FctAsyncDownloadReceiver{&dlPromise});

    const ::FCT::Status dlStatus = future.get();
    E2E_ASSERT_STATUS_OK(dlStatus);
    E2E_ASSERT_TRUE(::FCT::SAI::FileSystem::exists("/cache/downloaded_mesh.bin"));

    auto verified = ::FCT::SAI::FileSystem::readBinaryFile("/cache/downloaded_mesh.bin");
    E2E_ASSERT_STATUS_OK(verified.status());
    E2E_ASSERT_EQ(verified->size(), 4u);
    E2E_ASSERT_EQ((*verified)[0], 0x50u);

    return true;
}

E2E_TEST(Tier1, FEAT_43, AsyncIO_DownloadInvalidUrlFails) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    // 1. 非 /cache/ 目标区（只读 /pkg/）必须被立即拒绝
    std::promise<::FCT::Status> pkgTargetPromise;
    auto pkgFuture = pkgTargetPromise.get_future();
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/404.bin",
        "/pkg/forbidden.bin",
        Tier1FctAsyncDownloadReceiver{&pkgTargetPromise});
    const ::FCT::Status pkgStatus = pkgFuture.get();
    E2E_ASSERT_FALSE(pkgStatus.ok());
    E2E_ASSERT_EQ(pkgStatus.code(), absl::StatusCode::kInvalidArgument);

    // 2. 目录逃逸目标路径必须被安全网拦截
    std::promise<::FCT::Status> escapePromise;
    auto escapeFuture = escapePromise.get_future();
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "https://cdn.example.com/404.bin",
        "/cache/../escape.bin",
        Tier1FctAsyncDownloadReceiver{&escapePromise});
    const ::FCT::Status escapeStatus = escapeFuture.get();
    E2E_ASSERT_FALSE(escapeStatus.ok());
    E2E_ASSERT_EQ(escapeStatus.code(), absl::StatusCode::kPermissionDenied);

    return true;
}

E2E_TEST(Tier1, FEAT_43, AsyncIO_NoHeapPeakOnStreamDownload) {
    Tier1FctSandboxVfsScope scope;
    E2E_ASSERT_STATUS_OK(scope.mountZones());

    // 构造 256 KB 测试载荷（超过 64 KB 流式分块缓冲尺寸，验证多分块直写落盘）
    std::vector<uint8_t> largePayload(256u * 1024u, 0u);
    for (size_t i = 0; i < largePayload.size(); ++i) {
        largePayload[i] = static_cast<uint8_t>(i & 0xFFu);
    }
    const auto srcFile = scope.sandbox.getSandboxFilePath("cdn_origin/large_stream.pak");
    E2E_ASSERT_STATUS_OK(::FCT::SAI::FileSystem::writeBinaryFile(
        srcFile.generic_string(), largePayload.data(), largePayload.size(), true));

    std::promise<::FCT::Status> dlPromise;
    auto future = dlPromise.get_future();
    ::FCT::SAI::FileSystem::downloadFileToPathAsync(
        "file://" + srcFile.generic_string(),
        "/cache/stream.bin",
        Tier1FctAsyncDownloadReceiver{&dlPromise});

    E2E_ASSERT_STATUS_OK(future.get());
    auto loaded = ::FCT::SAI::FileSystem::readBinaryFile("/cache/stream.bin");
    E2E_ASSERT_STATUS_OK(loaded.status());
    E2E_ASSERT_EQ(loaded->size(), largePayload.size());
    E2E_ASSERT_EQ((*loaded)[0], 0u);
    E2E_ASSERT_EQ((*loaded)[65537], static_cast<uint8_t>(65537u & 0xFFu));
    E2E_ASSERT_EQ((*loaded)[largePayload.size() - 1u], static_cast<uint8_t>((largePayload.size() - 1u) & 0xFFu));

    return true;
}

// ============================================================================
// [FEAT-44] 轻量宿主原生 JS VM 反射绑定 (PRD 06 §4)
// ============================================================================

E2E_TEST(Tier1, FEAT_44, WebJS_HandleTableLifecycle) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();

    const uint32_t handle1 = fct_webjs_alloc_object();
    const uint32_t handle2 = fct_webjs_alloc_object();

    E2E_ASSERT_GE(handle1, ::FCT::JS::HandleFirstDynamic);
    E2E_ASSERT_NE(handle1, handle2);
    E2E_ASSERT_TRUE(env.getVMObject(handle1) != nullptr);
    E2E_ASSERT_TRUE(env.getVMObject(handle2) != nullptr);

    fct_webjs_release_handle(handle1);
    E2E_ASSERT_TRUE(env.getVMObject(handle1) == nullptr);

    fct_webjs_release_handle(handle2);
    E2E_ASSERT_TRUE(env.getVMObject(handle2) == nullptr);

    return true;
}

E2E_TEST(Tier1, FEAT_44, WebJS_TypedArrayZeroCopyView) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    uint8_t wasmMemory[16] = {10u, 20u, 30u, 40u, 50u, 60u};

    // 1. 创建子区间零拷贝 TypedArray 视图 (偏移量 2，长度 4)
    ::FCT::JS::Object viewObj = env.createTypedArrayView(
        wasmMemory + 2, 4u, ::FCT::JS::TypedArrayType::Uint8);
    E2E_ASSERT_TRUE(viewObj.isValid());

    auto* vm = env.getVMObject(viewObj.getHandle());
    E2E_ASSERT_TRUE(vm != nullptr);
    E2E_ASSERT_EQ(vm->typedArrayData, static_cast<const void*>(wasmMemory + 2));
    E2E_ASSERT_EQ(vm->typedArrayByteLength, 4u);

    auto* viewBytes = static_cast<uint8_t*>(const_cast<void*>(vm->typedArrayData));
    E2E_ASSERT_EQ(viewBytes[0], 30u);
    E2E_ASSERT_EQ(viewBytes[1], 40u);

    // 2. 通过零拷贝视图就地写入，验证底层线性内存同步更新
    viewBytes[0] = 99u;
    viewBytes[1] = 88u;
    E2E_ASSERT_EQ(wasmMemory[2], 99u);
    E2E_ASSERT_EQ(wasmMemory[3], 88u);

    // 3. 空指针与零长度视图元数据校验
    ::FCT::JS::Object emptyView = env.createTypedArrayView(
        nullptr, 0u, ::FCT::JS::TypedArrayType::Uint8);
    auto* emptyVm = env.getVMObject(emptyView.getHandle());
    E2E_ASSERT_TRUE(emptyVm != nullptr);
    E2E_ASSERT_TRUE(emptyVm->typedArrayData == nullptr);
    E2E_ASSERT_EQ(emptyVm->typedArrayByteLength, 0u);

    return true;
}

E2E_TEST(Tier1, FEAT_44, WebJS_CallHostFunctionWithReturn) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    ::FCT::JS::Object hostObj = env.newObject();
    hostObj.registerMethod("add", Tier1FctHostAddMethod{});

    const ::FCT::JS::Value res = hostObj.call(
        "add", {::FCT::JS::Value(10), ::FCT::JS::Value(20)});
    E2E_ASSERT_TRUE(res.isNumber());
    E2E_ASSERT_EQ(res.toInt32(), 30);

    return true;
}

E2E_TEST(Tier1, FEAT_44, WebJS_PromiseResolveCallback) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    ::FCT::JS::Promise prom = env.newPromise();
    E2E_ASSERT_FALSE(prom.isSettled());

    int capturedValue = 0;
    bool callbackFired = false;
    prom.then(Tier1FctPromiseResolveReceiver{&capturedValue, &callbackFired});

    prom.resolve(::FCT::JS::Value(42));
    E2E_ASSERT_FALSE(callbackFired);

    env.runMicrotasks();
    E2E_ASSERT_TRUE(callbackFired);
    E2E_ASSERT_EQ(capturedValue, 42);
    E2E_ASSERT_TRUE(prom.isFulfilled());

    return true;
}

E2E_TEST(Tier1, FEAT_44, WebJS_NBR_ServiceExport) {
    auto& env = ::FCT::JS::WebJSEnvironment::instance();
    ::FCT::JS::Object globalObj = env.global();

    const uint32_t nbrServiceHandle = fct_webjs_alloc_object();
    E2E_ASSERT_GE(nbrServiceHandle, ::FCT::JS::HandleFirstDynamic);

    fct_webjs_set_prop_string(nbrServiceHandle, "bridgeMode", "NBR");
    fct_webjs_set_prop_number(nbrServiceHandle, "nbrCompatible", 1.0);
    fct_webjs_set_prop_handle(::FCT::JS::HandleGlobal, "__mq_nbr_service__", nbrServiceHandle);

    const uint32_t resolvedHandle = fct_webjs_get_prop_handle(::FCT::JS::HandleGlobal, "__mq_nbr_service__");
    E2E_ASSERT_EQ(resolvedHandle, nbrServiceHandle);

    ::FCT::JS::Object nbrObj(nbrServiceHandle);
    E2E_ASSERT_EQ(nbrObj.get("bridgeMode").toString(), "NBR");
    E2E_ASSERT_TRUE(nbrObj.get("nbrCompatible").toBoolean());

    // 动态切换开关并验证双向状态读写
    fct_webjs_set_prop_number(nbrServiceHandle, "nbrCompatible", 0.0);
    E2E_ASSERT_FALSE(nbrObj.get("nbrCompatible").toBoolean());

    fct_webjs_set_prop_number(nbrServiceHandle, "nbrCompatible", 1.0);
    E2E_ASSERT_TRUE(nbrObj.get("nbrCompatible").toBoolean());

    (void)globalObj.deleteProperty("__mq_nbr_service__");
    fct_webjs_release_handle(nbrServiceHandle);
    return true;
}

// ============================================================================
// [FEAT-45] 跨平台系统字形批量光栅化契约 (PRD 06 §5.1)
// ============================================================================

E2E_TEST(Tier1, FEAT_45, FontRaster_SingleAsciiGlyph) {
    auto provider = ::FCT::SAI::SystemFontProvider::create();
    E2E_ASSERT_TRUE(provider != nullptr);
    E2E_ASSERT_STATUS_OK(provider->init());

    const ::FCT::SAI::GlyphRequest req{
        .codepoint = static_cast<uint32_t>('A'),
        .fontSizePx = 24,
        .weight = 4,
        .italic = false,
        .isEmoji = false
    };
    auto batchOr = provider->rasterizeBatch("Microsoft YaHei", 24, 4, false, std::span<const ::FCT::SAI::GlyphRequest>(&req, 1u));
    E2E_ASSERT_STATUS_OK(batchOr.status());
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::R8_UNORM);
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 1u);
    E2E_ASSERT_EQ(batchOr->glyphs[0].codepoint, static_cast<uint32_t>('A'));
    E2E_ASSERT_GT(batchOr->glyphs[0].advanceX, 0);
    E2E_ASSERT_GT(batchOr->glyphs[0].width, 0);
    E2E_ASSERT_GT(batchOr->glyphs[0].height, 0);

    provider->shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_45, FontRaster_ChineseCharacter) {
    auto provider = ::FCT::SAI::SystemFontProvider::create();
    E2E_ASSERT_TRUE(provider != nullptr);
    E2E_ASSERT_STATUS_OK(provider->init());

    const ::FCT::SAI::GlyphRequest req{
        .codepoint = 0x4E2Du,
        .fontSizePx = 24,
        .weight = 4,
        .italic = false,
        .isEmoji = false
    };
    auto batchOr = provider->rasterizeBatch("Microsoft YaHei", 24, 4, false, std::span<const ::FCT::SAI::GlyphRequest>(&req, 1u));
    E2E_ASSERT_STATUS_OK(batchOr.status());
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::R8_UNORM);
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 1u);
    E2E_ASSERT_EQ(batchOr->glyphs[0].codepoint, 0x4E2Du);
    E2E_ASSERT_GT(batchOr->glyphs[0].width, 0);
    E2E_ASSERT_GT(batchOr->glyphs[0].height, 0);
    E2E_ASSERT_EQ(batchOr->pixelData.size(), static_cast<size_t>(batchOr->atlasWidth) * batchOr->atlasHeight);

    provider->shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_45, FontRaster_BatchGridMultiGlyphs) {
    auto provider = ::FCT::SAI::SystemFontProvider::create();
    E2E_ASSERT_TRUE(provider != nullptr);
    E2E_ASSERT_STATUS_OK(provider->init());

    const char kWord[] = "HelloWorld";
    std::vector<::FCT::SAI::GlyphRequest> requests;
    requests.reserve(10u);
    for (size_t i = 0; i < 10u; ++i) {
        requests.push_back(::FCT::SAI::GlyphRequest{
            .codepoint = static_cast<uint32_t>(kWord[i]),
            .fontSizePx = 20,
            .weight = 4,
            .italic = false,
            .isEmoji = false
        });
    }

    auto batchOr = provider->rasterizeBatch("Microsoft YaHei", 20, 4, false, requests);
    E2E_ASSERT_STATUS_OK(batchOr.status());
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 10u);
    E2E_ASSERT_EQ(batchOr->glyphs[0].codepoint, static_cast<uint32_t>('H'));
    for (const auto& g : batchOr->glyphs) {
        E2E_ASSERT_LE(g.atlasX + static_cast<uint32_t>(g.width), batchOr->atlasWidth);
        E2E_ASSERT_LE(g.atlasY + static_cast<uint32_t>(g.height), batchOr->atlasHeight);
    }

    provider->shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_45, FontRaster_EmojiRgba8Format) {
    auto provider = ::FCT::SAI::SystemFontProvider::create();
    E2E_ASSERT_TRUE(provider != nullptr);
    E2E_ASSERT_STATUS_OK(provider->init());

    const ::FCT::SAI::GlyphRequest emojiReq{
        .codepoint = 0x1F600u,
        .fontSizePx = 24,
        .weight = 4,
        .italic = false,
        .isEmoji = true
    };
    auto batchOr = provider->rasterizeBatch("Segoe UI Emoji", 24, 4, false, std::span<const ::FCT::SAI::GlyphRequest>(&emojiReq, 1u));
    E2E_ASSERT_STATUS_OK(batchOr.status());
    E2E_ASSERT_EQ(batchOr->format, ::FCT::SAI::FontPixelFormat::RGBA8_UNORM);
    E2E_ASSERT_EQ(batchOr->pixelData.size(), static_cast<size_t>(batchOr->atlasWidth) * batchOr->atlasHeight * 4u);
    E2E_ASSERT_GT(batchOr->glyphs[0].width, 0);

    provider->shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_45, FontRaster_EmptyCodepointSpan) {
    auto provider = ::FCT::SAI::SystemFontProvider::create();
    E2E_ASSERT_TRUE(provider != nullptr);
    E2E_ASSERT_STATUS_OK(provider->init());

    const std::span<const ::FCT::SAI::GlyphRequest> emptyRequests{};
    auto batchOr = provider->rasterizeBatch("Microsoft YaHei", 20, 4, false, emptyRequests);
    E2E_ASSERT_STATUS_OK(batchOr.status());
    E2E_ASSERT_EQ(batchOr->glyphs.size(), 0u);
    E2E_ASSERT_EQ(batchOr->atlasWidth, 0u);
    E2E_ASSERT_EQ(batchOr->atlasHeight, 0u);

    provider->shutdown();
    return true;
}

// ============================================================================
// [FEAT-46] 跨平台虚拟软键盘原语 (PRD 06 §5.2)
// ============================================================================

E2E_TEST(Tier1, FEAT_46, Keyboard_ShowWithConfig) {
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    ::FCT::SAI::VirtualKeyboardConfig cfg;
    cfg.defaultValue = "initial";
    cfg.maxLength = 50;
    cfg.multiple = false;
    cfg.confirmHold = false;
    cfg.confirmType = ::FCT::SAI::KeyboardConfirmType::Search;

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::show(cfg));
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());

    const auto activeCfg = ::FCT::SAI::VirtualKeyboard::getCurrentConfig();
    E2E_ASSERT_EQ(activeCfg.defaultValue, "initial");
    E2E_ASSERT_EQ(activeCfg.maxLength, 50);
    E2E_ASSERT_EQ(activeCfg.confirmType, ::FCT::SAI::KeyboardConfirmType::Search);

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::hide());
    return true;
}

E2E_TEST(Tier1, FEAT_46, Keyboard_HideTransitionsState) {
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    ::FCT::SAI::VirtualKeyboardConfig cfg;
    cfg.defaultValue = "text";
    cfg.maxLength = 100;

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::show(cfg));
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::hide());
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());

    return true;
}

E2E_TEST(Tier1, FEAT_46, Keyboard_TextInputCallback) {
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    std::string received;
    ::FCT::SAI::VirtualKeyboard::setOnInput(Tier1FctKeyboardInputReceiver{&received});

    ::FCT::SAI::VirtualKeyboardConfig cfg;
    cfg.maxLength = 100;
    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::show(cfg));

    ::FCT::SAI::VirtualKeyboard::simulateInput("PlayerName");
    E2E_ASSERT_EQ(received, "PlayerName");

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::hide());
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_46, Keyboard_ConfirmEventCallback) {
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    bool confirmed = false;
    std::string confirmedText;
    ::FCT::SAI::VirtualKeyboard::setOnConfirm(Tier1FctKeyboardConfirmReceiver{&confirmed, &confirmedText});

    ::FCT::SAI::VirtualKeyboardConfig cfg;
    cfg.maxLength = 100;
    cfg.confirmHold = false;
    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::show(cfg));

    ::FCT::SAI::VirtualKeyboard::simulateConfirm("SubmitChat");
    E2E_ASSERT_TRUE(confirmed);
    E2E_ASSERT_EQ(confirmedText, "SubmitChat");
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());

    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_46, Keyboard_MaxLengthClamp) {
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    std::string clampedText;
    ::FCT::SAI::VirtualKeyboard::setOnInput(Tier1FctKeyboardInputReceiver{&clampedText});

    ::FCT::SAI::VirtualKeyboardConfig cfg;
    cfg.maxLength = 5;
    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::show(cfg));

    ::FCT::SAI::VirtualKeyboard::simulateInput("123456789");
    E2E_ASSERT_EQ(clampedText, "12345");
    E2E_ASSERT_EQ(clampedText.length(), 5u);

    E2E_ASSERT_STATUS_OK(::FCT::SAI::VirtualKeyboard::hide());
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

} // namespace MQEngine::Testing
