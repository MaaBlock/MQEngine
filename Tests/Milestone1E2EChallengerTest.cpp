#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <cassert>
#include <type_traits>

#include "Common/E2EAssert.h"
#include "Common/TestProfiles.h"
#include "Common/MockRhiContext.h"
#include <absl/status/status.h>
#include <absl/status/statusor.h>

using namespace MQEngine::Testing;

// -------------------------------------------------------------
// SFINAE / C++20 Concepts 用于探测 API 签名存在性
// -------------------------------------------------------------
template <typename T>
concept HasClearCommandLog = requires(T t) {
    t.clearCommandLog();
};

template <typename T>
concept HasGetCommandLog = requires(T t) {
    t.getCommandLog();
};

template <typename T>
concept HasGetMaxTextureSize = requires(T t) {
    t.getMaxTextureSize();
};

template <typename T>
concept HasResetCommandTrace = requires(T t) {
    t.resetCommandTrace();
};

template <typename T>
concept HasGetCommandTrace = requires(T t) {
    t.getCommandTrace();
};

// -------------------------------------------------------------
// 1. E2EAssert noexcept 测试函数
// -------------------------------------------------------------
bool noexcept_assert_true_failure() noexcept {
    E2E_ASSERT_TRUE(false);
    return true;
}

bool noexcept_assert_false_failure() noexcept {
    E2E_ASSERT_FALSE(true);
    return true;
}

bool noexcept_assert_eq_failure() noexcept {
    E2E_ASSERT_EQ(42, 99);
    return true;
}

bool noexcept_check_failure() noexcept {
    E2E_CHECK(false, "soft failure inside noexcept");
    return true; // E2E_CHECK 不应提早返回
}

bool noexcept_status_ok_success() noexcept {
    absl::Status s = absl::OkStatus();
    E2E_ASSERT_STATUS_OK(s);
    return true;
}

bool noexcept_status_ok_failure() noexcept {
    absl::Status s = absl::InternalError("adversarial internal error code 500");
    E2E_ASSERT_STATUS_OK(s);
    return true;
}

bool noexcept_statusor_success() noexcept {
    absl::StatusOr<int> val = 12345;
    E2E_ASSERT_STATUS_OK(val);
    return true;
}

bool noexcept_statusor_failure() noexcept {
    absl::StatusOr<int> val = absl::NotFoundError("adversarial missing key");
    E2E_ASSERT_STATUS_OK(val);
    return true;
}

int main(int argc, char** argv) {
    std::cout << "=========================================================" << std::endl;
    std::cout << "  MQEngine Milestone TEST-M1 对抗性验证套件 (Challenger 2)" << std::endl;
    std::cout << "=========================================================" << std::endl;

    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;

    auto reportResult = [&](std::string_view name, bool passed, std::string_view detail = "") {
        ++totalTests;
        if (passed) {
            ++passedTests;
            std::cout << "  [PASS] " << name;
            if (!detail.empty()) std::cout << " (" << detail << ")";
            std::cout << std::endl;
        } else {
            ++failedTests;
            std::cerr << "  [FAIL] " << name;
            if (!detail.empty()) std::cerr << " (" << detail << ")";
            std::cerr << std::endl;
        }
    };

    // -------------------------------------------------------------
    // 测试组 1: E2EAssert.h 零异常与断言行为验证
    // -------------------------------------------------------------
    std::cout << "\n--- 测试组 1: E2EAssert.h 零异常机制与 Status/StatusOr 契约 ---" << std::endl;

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_assert_true_failure();
        reportResult("E2E_ASSERT_TRUE(false) in noexcept 返回 false", ret == false);
        reportResult("E2E_ASSERT_TRUE(false) 增加 failureCount", ctx.failureCount == 1);
        reportResult("E2E_ASSERT_TRUE(false) 记录 checkCount", ctx.checkCount == 1);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_assert_false_failure();
        reportResult("E2E_ASSERT_FALSE(true) in noexcept 返回 false", ret == false);
        reportResult("E2E_ASSERT_FALSE(true) 增加 failureCount", ctx.failureCount == 1);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_assert_eq_failure();
        reportResult("E2E_ASSERT_EQ(42, 99) in noexcept 返回 false", ret == false);
        reportResult("E2E_ASSERT_EQ 增加 failureCount", ctx.failureCount == 1);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_check_failure();
        reportResult("E2E_CHECK(false) in noexcept 不中断流程（返回 true）", ret == true);
        reportResult("E2E_CHECK(false) 记录 failureCount", ctx.failureCount == 1);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_status_ok_success();
        reportResult("E2E_ASSERT_STATUS_OK(OkStatus) 成功通过", ret == true && ctx.failureCount == 0);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_status_ok_failure();
        bool msgMatched = ctx.lastErrorMessage.find("adversarial internal error code 500") != std::string::npos;
        reportResult("E2E_ASSERT_STATUS_OK(InternalError) 捕获失败返回 false", ret == false);
        reportResult("E2E_ASSERT_STATUS_OK(InternalError) 提取错误描述", msgMatched);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_statusor_success();
        reportResult("E2E_ASSERT_STATUS_OK(StatusOr<T> OK) 成功通过", ret == true && ctx.failureCount == 0);

        g_currentTestContext = nullptr;
    }

    {
        TestExecutionContext ctx;
        g_currentTestContext = &ctx;

        bool ret = noexcept_statusor_failure();
        bool msgMatched = ctx.lastErrorMessage.find("adversarial missing key") != std::string::npos;
        reportResult("E2E_ASSERT_STATUS_OK(StatusOr<T> Error) 捕获失败返回 false", ret == false);
        reportResult("E2E_ASSERT_STATUS_OK(StatusOr<T> Error) 提取错误描述", msgMatched);

        g_currentTestContext = nullptr;
    }

    // -------------------------------------------------------------
    // 测试组 2: MockRhiContext 画像自省与动态切换
    // -------------------------------------------------------------
    std::cout << "\n--- 测试组 2: MockRhiContext 画像自省与动态切换 ---" << std::endl;

    {
        MockRhiContext ctx{DouyinMinigameProfile()};

        bool minigameOk = 
            (ctx.isBindlessSupported() == false) &&
            (ctx.isReadWriteStorageBufferSupported() == false) &&
            (ctx.isIndirectDrawSupported() == false) &&
            (ctx.isBaseInstanceSupported() == false) &&
            (ctx.isRayTracingSupported() == false) &&
            (ctx.isMeshShaderSupported() == false) &&
            (ctx.isComputeSupported() == false) &&
            (ctx.getUniformBufferOffsetAlignment() == 256u) &&
            (ctx.getMaxUniformBufferRange() == 65536u) &&
            (ctx.isIntegrateGpu() == true);

        reportResult("DouyinMinigameProfile 8+项硬件自省精确值校验", minigameOk);

        // 动态切换到 DesktopProfile
        ctx.profile = DesktopProfile();

        bool desktopOk =
            (ctx.isBindlessSupported() == true) &&
            (ctx.isReadWriteStorageBufferSupported() == true) &&
            (ctx.isIndirectDrawSupported() == true) &&
            (ctx.isBaseInstanceSupported() == true) &&
            (ctx.isRayTracingSupported() == true) &&
            (ctx.isMeshShaderSupported() == true) &&
            (ctx.isComputeSupported() == true) &&
            (ctx.getUniformBufferOffsetAlignment() == 16u) &&
            (ctx.getMaxUniformBufferRange() == 134217728u) &&
            (ctx.isIntegrateGpu() == true);

        reportResult("动态热切换至 DesktopProfile 后自省精确值校验", desktopOk);

        // 动态切回 DouyinMinigameProfile
        ctx.profile = DouyinMinigameProfile();
        bool revertOk =
            (ctx.isBindlessSupported() == false) &&
            (ctx.getUniformBufferOffsetAlignment() == 256u) &&
            (ctx.getMaxUniformBufferRange() == 65536u);
        reportResult("动态回退至 DouyinMinigameProfile 正常还原", revertOk);
    }

    // -------------------------------------------------------------
    // 测试组 3: MockRhiContext 10,000 条命令录制压力测试
    // -------------------------------------------------------------
    std::cout << "\n--- 测试组 3: MockRhiContext 10,000 条命令录制压测与完整性 ---" << std::endl;

    {
        MockRhiContext ctx{DouyinMinigameProfile()};

        constexpr size_t kCommandCount = 10000;
        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < kCommandCount; ++i) {
            std::string cmd = "DrawInstanced_Index_" + std::to_string(i);
            ctx.recordCommand(cmd);
        }

        auto end = std::chrono::steady_clock::now();
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        reportResult("10,000 条命令录制耗时 < 50ms", durationMs < 50, std::to_string(durationMs) + " ms");
        reportResult("10,000 条命令录制计数精确匹配", ctx.getRecordedCommandCount() == kCommandCount);

        const auto& trace = ctx.getCommandTrace();
        bool traceSizeOk = (trace.size() == kCommandCount);
        bool sample0Ok = (!trace.empty() && trace[0] == "DrawInstanced_Index_0");
        bool sample4999Ok = (trace.size() > 4999 && trace[4999] == "DrawInstanced_Index_4999");
        bool sample9999Ok = (trace.size() > 9999 && trace[9999] == "DrawInstanced_Index_9999");

        reportResult("命令追踪日志条目总数匹配", traceSizeOk);
        reportResult("首条命令内容完整性校验", sample0Ok);
        reportResult("第5000条命令内容完整性校验", sample4999Ok);
        reportResult("末条命令内容完整性校验", sample9999Ok);

        // 清空命令日志测试
        ctx.resetCommandTrace();
        reportResult("调用 resetCommandTrace() 后计数归零", ctx.getRecordedCommandCount() == 0);
        reportResult("调用 resetCommandTrace() 后追踪列表为空", ctx.getCommandTrace().empty());
    }

    // -------------------------------------------------------------
    // 测试组 4: 编译期契约审查与 API 命名偏差探测
    // -------------------------------------------------------------
    std::cout << "\n--- 测试组 4: 编译期契约审查与 Worker 汇报差异探测 ---" << std::endl;

    {
        constexpr bool hasClearCommandLog = HasClearCommandLog<MockRhiContext>;
        constexpr bool hasGetCommandLog = HasGetCommandLog<MockRhiContext>;
        constexpr bool hasResetCommandTrace = HasResetCommandTrace<MockRhiContext>;
        constexpr bool hasGetCommandTrace = HasGetCommandTrace<MockRhiContext>;
        constexpr bool hasGetMaxTextureSize = HasGetMaxTextureSize<MockRhiContext>;

        std::cout << "  [静态反射自省] MockRhiContext API 命名探测:" << std::endl;
        std::cout << "    - HasClearCommandLog:    " << (hasClearCommandLog ? "TRUE" : "FALSE") << std::endl;
        std::cout << "    - HasGetCommandLog:      " << (hasGetCommandLog ? "TRUE" : "FALSE") << std::endl;
        std::cout << "    - HasResetCommandTrace:  " << (hasResetCommandTrace ? "TRUE" : "FALSE") << std::endl;
        std::cout << "    - HasGetCommandTrace:    " << (hasGetCommandTrace ? "TRUE" : "FALSE") << std::endl;
        std::cout << "    - HasGetMaxTextureSize:  " << (hasGetMaxTextureSize ? "TRUE" : "FALSE") << std::endl;

        reportResult("契约对齐: 实现方法名为 resetCommandTrace() 而非 clearCommandLog()", 
                     hasResetCommandTrace && !hasClearCommandLog);
        reportResult("契约对齐: 实现方法名为 getCommandTrace() 而非 getCommandLog()", 
                     hasGetCommandTrace && !hasGetCommandLog);
    }

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "对抗性验证统计: 总计 " << totalTests << " 项, 通过 " << passedTests
              << " 项, 失败 " << failedTests << " 项" << std::endl;
    std::cout << "=========================================================" << std::endl;

    return (failedTests == 0) ? 0 : 1;
}
