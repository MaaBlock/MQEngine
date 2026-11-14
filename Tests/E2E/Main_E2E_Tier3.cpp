#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Runtime/runtime.h>

using namespace MQEngine::Testing;

namespace {
struct ScopedContextRef {
    FCT::Context* ptr{nullptr};
    explicit ScopedContextRef(FCT::Context* p) noexcept : ptr(p) {}
    ~ScopedContextRef() {
        if (ptr) {
            ptr->release();
        }
    }
};
} // namespace

E2E_TEST(Tier3, FEAT_23, Smoke_Tier3_Profile_DesktopVsMinigame) {
    FCT::Runtime runtime;
    ScopedContextRef desktopGuard(runtime.createContext(FCT::ContextBackend::Vulkan));
    E2E_ASSERT_NE(desktopGuard.ptr, nullptr);

    FCT::WGL2_Context wgl2Ctx(nullptr);
    FCT::Context& desktopCtx = *desktopGuard.ptr;

    // 真实 WGL2_Context 与真实桌面 VK_Context 跨后端硬件自省契约比对
    E2E_ASSERT_NE(wgl2Ctx.isBindlessSupported(), desktopCtx.isBindlessSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isBindlessSupported());
    E2E_ASSERT_TRUE(desktopCtx.isBindlessSupported());

    E2E_ASSERT_NE(wgl2Ctx.isReadWriteStorageBufferSupported(), desktopCtx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_FALSE(wgl2Ctx.isReadWriteStorageBufferSupported());
    E2E_ASSERT_TRUE(desktopCtx.isReadWriteStorageBufferSupported());

    E2E_ASSERT_GE(desktopCtx.getMaxUniformBufferRange(), wgl2Ctx.getMaxUniformBufferRange());
    E2E_ASSERT_LE(desktopCtx.getUniformBufferOffsetAlignment(), wgl2Ctx.getUniformBufferOffsetAlignment());

    return true;
}

E2E_TEST(Tier3, FEAT_INFRA, Smoke_Tier3_HeadlessGles_Probe) {
    HeadlessGlesFixture fixture;
    bool inited = fixture.initialize(64, 64);
    if (inited) {
        E2E_ASSERT_TRUE(fixture.isAvailable());
        E2E_ASSERT_EQ(fixture.getWidth(), 64u);
        E2E_ASSERT_EQ(fixture.getHeight(), 64u);
        fixture.terminate();
        E2E_ASSERT_FALSE(fixture.isAvailable());
    } else {
        // 在无 ANGLE 的 CI 环境下优雅跳过硬件驱动依赖，保证契约不崩溃
        E2E_ASSERT_FALSE(fixture.isAvailable());
    }
    return true;
}

int main(int argc, char** argv) {
    return E2ETestRegistry::instance().run(TestTier::Tier3, argc, argv);
}
