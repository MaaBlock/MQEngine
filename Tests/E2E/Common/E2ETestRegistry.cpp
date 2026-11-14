#include "E2ETestRegistry.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <exception>
#include <cstdlib>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#if defined(BOOST_NO_EXCEPTIONS) || !defined(__cpp_exceptions)
#include <boost/assert/source_location.hpp>

namespace boost {
[[noreturn]] void throw_exception(std::exception const& e) {
    (void)e;
    std::abort();
}
[[noreturn]] void throw_exception(std::exception const& e, boost::source_location const& loc) {
    (void)e;
    (void)loc;
    std::abort();
}
} // namespace boost
#endif

namespace MQEngine::Testing {

E2ETestRegistry& E2ETestRegistry::instance() {
    static E2ETestRegistry reg;
    return reg;
}

void E2ETestRegistry::registerTest(TestTier tier, std::string featureId, std::string testName, std::function<bool()> func) {
    m_tests.push_back({tier, std::move(featureId), std::move(testName), std::move(func)});
}

size_t E2ETestRegistry::getTestCount(TestTier tierFilter) const {
    if (tierFilter == TestTier::All) {
        return m_tests.size();
    }
    size_t count = 0;
    for (const auto& test : m_tests) {
        if (test.tier == tierFilter) {
            ++count;
        }
    }
    return count;
}

static const char* tierToString(TestTier tier) {
    switch (tier) {
        case TestTier::Tier1: return "Tier1_Functional";
        case TestTier::Tier2: return "Tier2_Boundary";
        case TestTier::Tier3: return "Tier3_Interactions";
        case TestTier::Tier4: return "Tier4_Scenarios";
        case TestTier::All:   return "All";
    }
    return "Unknown";
}

int E2ETestRegistry::run(TestTier tierFilter, int argc, char** argv) {
    std::string filterSubstring;
    bool listOnly = false;
    bool headless = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--filter=", 0) == 0) {
            filterSubstring = arg.substr(9);
        } else if (arg == "--list") {
            listOnly = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--verbose") {
            verbose = true;
        }
    }

    if (listOnly) {
        std::cout << "=== 已注册 E2E 测试列表 (层级: " << tierToString(tierFilter) << ") ===" << std::endl;
        for (const auto& test : m_tests) {
            if (tierFilter != TestTier::All && test.tier != tierFilter) {
                continue;
            }
            std::cout << "  [" << tierToString(test.tier) << "] "
                      << test.featureId << "::" << test.testName << std::endl;
        }
        return 0;
    }

    std::cout << "=========================================================" << std::endl;
    std::cout << "  MQEngine Douyin Minigame E2E 测试运行器 - " << tierToString(tierFilter) << std::endl;
    if (headless) {
#ifdef _WIN32
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
        std::cout << "  [运行模式] 无头运行模式 (--headless)" << std::endl;
    }
    if (!filterSubstring.empty()) {
        std::cout << "  [过滤器] 匹配名称: \"" << filterSubstring << "\"" << std::endl;
    }
    std::cout << "=========================================================" << std::endl;

    uint32_t executedCount = 0;
    uint32_t passedCount = 0;
    uint32_t failedCount = 0;

    auto totalStart = std::chrono::steady_clock::now();

    for (const auto& test : m_tests) {
        if (tierFilter != TestTier::All && test.tier != tierFilter) {
            continue;
        }

        std::string fullName = test.featureId + "::" + test.testName;
        if (!filterSubstring.empty() && fullName.find(filterSubstring) == std::string::npos) {
            continue;
        }

        ++executedCount;
        TestExecutionContext context;
        g_currentTestContext = &context;

        auto testStart = std::chrono::steady_clock::now();
        bool result = false;
        if (test.testFunc) {
            result = test.testFunc();
        }
        auto testEnd = std::chrono::steady_clock::now();
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(testEnd - testStart).count();

        g_currentTestContext = nullptr;

        bool hasFailed = (!result) || (context.failureCount > 0);
        if (!hasFailed) {
            ++passedCount;
            if (verbose) {
                std::cout << "  [PASS] " << std::left << std::setw(50) << fullName
                          << " (" << durationUs / 1000.0 << " ms, "
                          << context.checkCount << " 断言)" << std::endl;
            }
        } else {
            ++failedCount;
            std::cout << "  [FAIL] " << std::left << std::setw(50) << fullName
                      << " (" << durationUs / 1000.0 << " ms)" << std::endl;
            if (!context.lastErrorMessage.empty()) {
                std::cout << "         错误: " << context.lastErrorMessage << std::endl;
            }
        }
    }

    auto totalEnd = std::chrono::steady_clock::now();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalEnd - totalStart).count();

    std::cout << "---------------------------------------------------------" << std::endl;
    std::cout << "测试结果摘要:" << std::endl;
    std::cout << "  总计执行: " << executedCount << " 用例" << std::endl;
    std::cout << "  通过用例: " << passedCount << std::endl;
    std::cout << "  失败用例: " << failedCount << std::endl;
    std::cout << "  耗时总计: " << totalDurationMs << " ms" << std::endl;
    std::cout << "=========================================================" << std::endl;

    return (failedCount == 0) ? 0 : 1;
}

} // namespace MQEngine::Testing
