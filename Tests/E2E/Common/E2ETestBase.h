#pragma once

#include <filesystem>
#include <chrono>
#include <string>
#include <fstream>
#include "E2EAssert.h"
#include "TestProfiles.h"

namespace MQEngine::Testing {

/**
 * @brief E2E 测试用例基类，自动提供隔离沙箱临时目录与生命周期管理
 */
class E2ETestBase {
public:
    E2ETestBase() = default;
    virtual ~E2ETestBase() {
        tearDown();
    }

    virtual void setUp() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        m_sandboxPath = std::filesystem::temp_directory_path() / ("mq_e2e_sandbox_" + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(m_sandboxPath, ec);
    }

    virtual void tearDown() {
        if (!m_sandboxPath.empty() && std::filesystem::exists(m_sandboxPath)) {
            std::error_code ec;
            std::filesystem::remove_all(m_sandboxPath, ec);
        }
    }

    const std::filesystem::path& getSandboxPath() const noexcept {
        return m_sandboxPath;
    }

    std::filesystem::path getSandboxFilePath(const std::string& relPath) const {
        return m_sandboxPath / relPath;
    }

    bool writeSandboxFile(const std::string& relPath, const std::string& content) const {
        auto p = getSandboxFilePath(relPath);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return out.good();
    }

protected:
    std::filesystem::path m_sandboxPath;
};

} // namespace MQEngine::Testing
