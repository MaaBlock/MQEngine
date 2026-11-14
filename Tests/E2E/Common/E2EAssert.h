#pragma once

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <absl/strings/str_cat.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>

namespace MQEngine::Testing {

/**
 * @brief 测试用例执行上下文，记录断言计数与失败详情（纯无异常设计）
 */
struct TestExecutionContext {
    uint32_t checkCount{0};
    uint32_t failureCount{0};
    std::string lastErrorMessage;

    void recordFailure(std::string_view file, int line, std::string_view expr, std::string_view msg) {
        ++failureCount;
        lastErrorMessage = absl::StrCat(file, ":", line, " 断言失败: [", expr, "] - ", msg);
        std::cerr << "[E2E 断言失败] " << lastErrorMessage << std::endl;
    }
};

inline thread_local TestExecutionContext* g_currentTestContext = nullptr;

#define E2E_ASSERT(cond, msg) \
    do { \
        if (::MQEngine::Testing::g_currentTestContext) { \
            ::MQEngine::Testing::g_currentTestContext->checkCount++; \
            if (!(cond)) { \
                ::MQEngine::Testing::g_currentTestContext->recordFailure(__FILE__, __LINE__, #cond, msg); \
                return false; \
            } \
        } else { \
            if (!(cond)) { \
                assert(false && "E2E assertion failed"); \
            } \
        } \
    } while (0)

#define E2E_CHECK(cond, msg) \
    do { \
        if (::MQEngine::Testing::g_currentTestContext) { \
            ::MQEngine::Testing::g_currentTestContext->checkCount++; \
            if (!(cond)) { \
                ::MQEngine::Testing::g_currentTestContext->recordFailure(__FILE__, __LINE__, #cond, msg); \
            } \
        } \
    } while (0)

#define E2E_ASSERT_TRUE(cond) E2E_ASSERT((cond), "期望表达式为 true")
#define E2E_ASSERT_FALSE(cond) E2E_ASSERT(!(cond), "期望表达式为 false")

template <typename S>
inline std::string getStatusMessage(const S& s) {
    if constexpr (requires { s.status(); }) {
        return s.status().ToString();
    } else {
        return s.ToString();
    }
}

#define E2E_ASSERT_STATUS_OK(status_expr) \
    do { \
        auto&& _res = (status_expr); \
        if (!_res.ok()) { \
            E2E_ASSERT(false, ::MQEngine::Testing::getStatusMessage(_res)); \
        } \
    } while (0)

#define E2E_ASSERT_EQ(a, b) \
    E2E_ASSERT((a) == (b), ::absl::StrCat("期望相等: ", #a, " == ", #b))

#define E2E_ASSERT_NE(a, b) \
    E2E_ASSERT((a) != (b), ::absl::StrCat("期望不相等: ", #a, " != ", #b))

#define E2E_ASSERT_GE(a, b) \
    E2E_ASSERT((a) >= (b), ::absl::StrCat("期望大于等于: ", #a, " >= ", #b))

#define E2E_ASSERT_LE(a, b) \
    E2E_ASSERT((a) <= (b), ::absl::StrCat("期望小于等于: ", #a, " <= ", #b))

#define E2E_ASSERT_GT(a, b) \
    E2E_ASSERT((a) > (b), ::absl::StrCat("期望大于: ", #a, " > ", #b))

#define E2E_ASSERT_LT(a, b) \
    E2E_ASSERT((a) < (b), ::absl::StrCat("期望小于: ", #a, " < ", #b))

#define E2E_ASSERT_NEAR(a, b, eps) \
    E2E_ASSERT(std::abs(static_cast<double>(a) - static_cast<double>(b)) <= static_cast<double>(eps), \
               ::absl::StrCat("期望近似相等: |", #a, " - ", #b, "| <= ", #eps))

} // namespace MQEngine::Testing
