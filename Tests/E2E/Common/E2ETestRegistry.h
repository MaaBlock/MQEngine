#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include "E2EAssert.h"

namespace MQEngine::Testing {

/**
 * @brief E2E 测试所属层级
 */
enum class TestTier {
    Tier1,  // 功能特性隔离覆盖 (FEAT-01 ~ FEAT-73)
    Tier2,  // 边界极限、缺省与降级容错
    Tier3,  // 跨子系统两两组合契约
    Tier4,  // 业务全链路综合应用场景 (S1 ~ S5)
    All     // 全量运行
};

/**
 * @brief 测试用例注册元数据与执行入口
 */
struct TestCaseInfo {
    TestTier tier;
    std::string featureId;
    std::string testName;
    std::function<bool()> testFunc;
};

/**
 * @brief 轻量级全局测试用例注册表与执行驱动器
 */
class E2ETestRegistry {
public:
    static E2ETestRegistry& instance();

    void registerTest(TestTier tier, std::string featureId, std::string testName, std::function<bool()> func);

    int run(TestTier tierFilter, int argc, char** argv);

    size_t getTestCount(TestTier tierFilter) const;

    const std::vector<TestCaseInfo>& getTests() const noexcept { return m_tests; }

private:
    E2ETestRegistry() = default;
    std::vector<TestCaseInfo> m_tests;
};

#define E2E_TEST(tier, featureId, testName) \
    bool test_##tier##_##featureId##_##testName(); \
    static const bool reg_##tier##_##featureId##_##testName = []() { \
        ::MQEngine::Testing::E2ETestRegistry::instance().registerTest( \
            ::MQEngine::Testing::TestTier::tier, #featureId, #testName, test_##tier##_##featureId##_##testName); \
        return true; \
    }(); \
    bool test_##tier##_##featureId##_##testName()

} // namespace MQEngine::Testing
