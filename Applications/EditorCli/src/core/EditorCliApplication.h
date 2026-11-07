#ifndef ENGINE_EDITORCLIAPPLICATION_H
#define ENGINE_EDITORCLIAPPLICATION_H

#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace MQEngine {

    /**
     * @brief 结构化错误信封（对齐 ForgeAX AI-Native CLI 错误模型）
     */
    struct CliErrorEnvelope {
        std::string code;
        std::string message;
        std::string expected;
        std::string hint;
    };

    /**
     * @brief 单条 CLI 指令的结构化执行结果
     */
    struct CliCommandOutcome {
        bool ok = true;
        std::string command;
        uint64_t revision = 1;
        uint64_t frame = 0;
        ConfigNode data;
        CliErrorEnvelope error;

        ConfigNode toConfigNode() const;
    };

    /**
     * @brief AI-Native 命令行编辑器宿主（参考 ForgeAX Engine CLI / MCP 架构设计）
     * @note 支持：
     *   1. 渐进式指令树自发现：`help [--tree] [<domain>] [--json]`
     *   2. 乐观并发版本栅栏：`revision` 与 `--revision=<rev>`（过期返回 `live-revision-stale`）
     *   3. 闭环操作域：`project`、`dev`、`scene`、`entity`、`component`、`plugin`、`usercode`
     *   4. 纯净单文档 `--json` 标准输出（引擎底层日志自动分流至 stderr）
     *   5. Model Context Protocol (MCP) JSON-RPC 2.0 Stdio 模式（`mcp` / `--mcp`）
     */
    class EditorCliApplication : public Runtime::Application {
    public:
        EditorCliApplication();
        ~EditorCliApplication() override;

        void registerInfo() override;
        void init() override;
        void logicTick() override;

        CliCommandOutcome executeCommand(const std::string& rawCommandLine);
        void enqueueCommand(const std::string& commandLine);

    private:
        void parseStartupOptions();
        void ensureFallbackRegistry();
        void cleanupFallbackRegistry();
        void startStdinReaderThread();
        void stopStdinReaderThread();
        void drainCommandQueue();
        void emitBatchJsonIfNeeded();
        void emitJsonStdout(const ConfigNode& rootNode) const;
        void handleMcpStdioLine(const std::string& line);

        // 顶层指令分发
        CliCommandOutcome dispatchParsedTokens(const std::string& fullCmd,
                                               const std::vector<std::string>& positional,
                                               const std::unordered_map<std::string, std::string>& flags);

        // 各领域指令实现
        CliCommandOutcome handleHelpCommand(const std::vector<std::string>& positional,
                                            const std::unordered_map<std::string, std::string>& flags) const;
        CliCommandOutcome handleProjectCommand(const std::vector<std::string>& positional,
                                               const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handleDevCommand(const std::vector<std::string>& positional,
                                           const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handleSceneCommand(const std::vector<std::string>& positional,
                                             const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handleEntityCommand(const std::vector<std::string>& positional,
                                              const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handleComponentCommand(const std::vector<std::string>& positional,
                                                 const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handlePluginCommand(const std::vector<std::string>& positional,
                                              const std::unordered_map<std::string, std::string>& flags);
        CliCommandOutcome handleUserCodeCommand(const std::vector<std::string>& positional,
                                                const std::unordered_map<std::string, std::string>& flags);

        // 领域辅助与序列化函数
        ConfigNode buildCommandTreeNode(const std::string& filterDomain) const;
        ConfigNode buildDevStatusNode() const;
        ConfigNode buildPluginListNode() const;
        ConfigNode buildUserCodeListNode() const;
        ConfigNode buildComponentSchemaNode(const ComponentInfo& info) const;
        ConfigNode buildEntityInspectNode(entt::registry& reg, entt::entity ent, size_t regIndex) const;

        bool resolveEntityInCurrentScene(const std::string& identifier,
                                         entt::registry*& outReg,
                                         entt::entity& outEntity,
                                         size_t& outRegIndex) const;

        CliCommandOutcome makeSuccess(const std::string& cmd, ConfigNode data) const;
        CliCommandOutcome makeError(const std::string& cmd,
                                    const std::string& code,
                                    const std::string& message,
                                    const std::string& expected,
                                    const std::string& hint) const;
        uint64_t bumpRevision();

        mutable std::mutex m_queueMutex;
        std::deque<std::string> m_pendingCommands;
        std::vector<CliCommandOutcome> m_batchOutcomes;
        std::atomic<bool> m_running{false};
        bool m_interactiveStdin = false;
        bool m_autoExitAfterInitCmds = false;
        bool m_initCommandsExecuted = false;
        bool m_jsonMode = false;
        bool m_detailMode = false;
        bool m_mcpMode = false;
        std::string m_jsonOutputPath;
        uint64_t m_revision = 1;
        uint64_t m_tickCount = 0;
        UniquePtr<entt::registry> m_fallbackRegistry;
        std::thread m_stdinThread;
    };

} // namespace MQEngine

#endif // ENGINE_EDITORCLIAPPLICATION_H
