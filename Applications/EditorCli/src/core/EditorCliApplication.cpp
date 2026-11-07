#include "EditorCliApplication.h"
#include <absl/strings/numbers.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

namespace MQEngine {

    namespace {

        int g_savedStdoutFd = -1;

        void RedirectStdoutToStderrForJsonMode() {
#ifdef _WIN32
            if (g_savedStdoutFd < 0) {
                fflush(stdout);
                g_savedStdoutFd = _dup(_fileno(stdout));
                if (g_savedStdoutFd >= 0) {
                    _dup2(_fileno(stderr), _fileno(stdout));
                }
            }
#endif
        }

        void WritePureStdoutLine(const std::string& text) {
            std::string payload = text;
            if (payload.empty() || payload.back() != '\n') {
                payload.push_back('\n');
            }
#ifdef _WIN32
            if (g_savedStdoutFd >= 0) {
                _write(g_savedStdoutFd, payload.data(), static_cast<unsigned int>(payload.size()));
                return;
            }
#endif
            std::cout << payload << std::flush;
        }

        std::string SerializeNodeToJson(const ConfigNode& node) {
            return ConfigDocument(node).serialize(ConfigFormat::Json);
        }

        void SetNodeArray(ConfigNode& parent, const std::string& key, ConfigArray arr) {
            parent.setNode(key, ConfigNode(std::move(arr)));
        }

        std::string TrimString(std::string_view sv) {
            size_t first = sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                return "";
            }
            size_t last = sv.find_last_not_of(" \t\r\n");
            return std::string(sv.substr(first, last - first + 1));
        }

        std::vector<std::string> SplitByChar(std::string_view sv, char delimiter) {
            std::vector<std::string> parts;
            size_t start = 0;
            while (start <= sv.size()) {
                size_t pos = sv.find(delimiter, start);
                std::string token = (pos == std::string_view::npos)
                    ? TrimString(sv.substr(start))
                    : TrimString(sv.substr(start, pos - start));
                if (!token.empty()) {
                    parts.push_back(std::move(token));
                }
                if (pos == std::string_view::npos) {
                    break;
                }
                start = pos + 1;
            }
            return parts;
        }

        void TokenizeCommand(const std::string& cmdLine,
                             std::vector<std::string>& outPositional,
                             std::unordered_map<std::string, std::string>& outFlags) {
            outPositional.clear();
            outFlags.clear();

            std::vector<std::string> rawTokens;
            std::string current;
            bool inQuotes = false;
            char quoteChar = '\0';

            for (size_t i = 0; i < cmdLine.size(); ++i) {
                char c = cmdLine[i];
                if ((c == '"' || c == '\'') && (!inQuotes || c == quoteChar)) {
                    if (inQuotes) {
                        inQuotes = false;
                        quoteChar = '\0';
                    } else {
                        inQuotes = true;
                        quoteChar = c;
                    }
                    continue;
                }
                if (!inQuotes && (c == ' ' || c == '\t')) {
                    if (!current.empty()) {
                        rawTokens.push_back(std::move(current));
                        current.clear();
                    }
                    continue;
                }
                current.push_back(c);
            }
            if (!current.empty()) {
                rawTokens.push_back(std::move(current));
            }

            if (rawTokens.size() == 1 && rawTokens[0].find(':') != std::string::npos && rawTokens[0].rfind("--", 0) != 0) {
                size_t colonPos = rawTokens[0].find(':');
                std::string head = rawTokens[0].substr(0, colonPos);
                std::string tail = rawTokens[0].substr(colonPos + 1);
                rawTokens.clear();
                rawTokens.push_back(std::move(head));
                if (!tail.empty()) {
                    rawTokens.push_back(std::move(tail));
                }
            }

            for (size_t i = 0; i < rawTokens.size(); ++i) {
                const std::string& tok = rawTokens[i];
                if (tok.rfind("--", 0) == 0 && tok.size() > 2) {
                    std::string body = tok.substr(2);
                    size_t eqPos = body.find('=');
                    if (eqPos != std::string::npos) {
                        outFlags[body.substr(0, eqPos)] = body.substr(eqPos + 1);
                    } else if (i + 1 < rawTokens.size() && rawTokens[i + 1].rfind("--", 0) != 0 &&
                               (body == "name" || body == "pos" || body == "rot" || body == "scale" ||
                                body == "components" || body == "component" || body == "frames" ||
                                body == "dt" || body == "revision" || body == "entity" ||
                                body == "distance" || body == "limit" || body == "target")) {
                        outFlags[body] = rawTokens[i + 1];
                        ++i;
                    } else {
                        outFlags[body] = "true";
                    }
                } else {
                    outPositional.push_back(tok);
                }
            }
        }

        const char* PluginStateToString(Runtime::PluginState state) {
            switch (state) {
                case Runtime::PluginState::Unloaded: return "Unloaded";
                case Runtime::PluginState::Preloaded: return "Preloaded";
                case Runtime::PluginState::Loaded: return "Loaded";
                case Runtime::PluginState::Initialized: return "Initialized";
                case Runtime::PluginState::Failed: return "Failed";
                default: return "Unknown";
            }
        }

        const char* UserCodeStateToString(Runtime::UserCodeState state) {
            switch (state) {
                case Runtime::UserCodeState::Unloaded: return "Unloaded";
                case Runtime::UserCodeState::Preloaded: return "Preloaded";
                case Runtime::UserCodeState::Loaded: return "Loaded";
                case Runtime::UserCodeState::Initialized: return "Initialized";
                case Runtime::UserCodeState::Failed: return "Failed";
                default: return "Unknown";
            }
        }

        ConfigNode ComponentValueToConfigNode(const ComponentValue& val) {
            if (std::holds_alternative<bool>(val)) {
                return ConfigNode(std::get<bool>(val));
            }
            if (std::holds_alternative<int>(val)) {
                return ConfigNode(static_cast<int64_t>(std::get<int>(val)));
            }
            if (std::holds_alternative<float>(val)) {
                return ConfigNode(static_cast<double>(std::get<float>(val)));
            }
            if (std::holds_alternative<double>(val)) {
                return ConfigNode(std::get<double>(val));
            }
            if (std::holds_alternative<std::string>(val)) {
                return ConfigNode(std::get<std::string>(val));
            }
            if (std::holds_alternative<Vec2>(val)) {
                const auto& v = std::get<Vec2>(val);
                return ConfigNode(ConfigArray{ConfigNode(static_cast<double>(v.x)),
                                              ConfigNode(static_cast<double>(v.y))});
            }
            if (std::holds_alternative<Vec3<>>(val)) {
                const auto& v = std::get<Vec3<>>(val);
                return ConfigNode(ConfigArray{ConfigNode(static_cast<double>(v.x)),
                                              ConfigNode(static_cast<double>(v.y)),
                                              ConfigNode(static_cast<double>(v.z))});
            }
            if (std::holds_alternative<Vec4>(val)) {
                const auto& v = std::get<Vec4>(val);
                return ConfigNode(ConfigArray{ConfigNode(static_cast<double>(v.x)),
                                              ConfigNode(static_cast<double>(v.y)),
                                              ConfigNode(static_cast<double>(v.z)),
                                              ConfigNode(static_cast<double>(v.w))});
            }
            if (std::holds_alternative<std::vector<std::string>>(val)) {
                const auto& vec = std::get<std::vector<std::string>>(val);
                ConfigArray arr;
                for (const auto& s : vec) {
                    arr.push_back(ConfigNode(s));
                }
                return ConfigNode(std::move(arr));
            }
            return ConfigNode();
        }

        bool ParseVec3String(const std::string& text, FCT::Vec3<>& outVec) {
            std::string cleaned = text;
            cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '['), cleaned.end());
            cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ']'), cleaned.end());
            auto parts = SplitByChar(cleaned, ',');
            if (parts.size() != 3) {
                return false;
            }
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!absl::SimpleAtof(parts[0], &x) ||
                !absl::SimpleAtof(parts[1], &y) ||
                !absl::SimpleAtof(parts[2], &z)) {
                return false;
            }
            outVec = FCT::Vec3<>(x, y, z);
            return true;
        }

        bool ParseComponentFieldValue(const std::string& typeStr, const std::string& rawStr, ComponentValue& outVal) {
            if (typeStr == "bool") {
                bool b = (rawStr == "true" || rawStr == "1" || rawStr == "on" || rawStr == "yes");
                if (!b && rawStr != "false" && rawStr != "0" && rawStr != "off" && rawStr != "no") {
                    return false;
                }
                outVal = b;
                return true;
            }
            if (typeStr == "int") {
                int v = 0;
                if (!absl::SimpleAtoi(rawStr, &v)) {
                    return false;
                }
                outVal = v;
                return true;
            }
            if (typeStr == "float") {
                float v = 0.0f;
                if (!absl::SimpleAtof(rawStr, &v)) {
                    return false;
                }
                outVal = v;
                return true;
            }
            if (typeStr == "double") {
                double v = 0.0;
                if (!absl::SimpleAtod(rawStr, &v)) {
                    return false;
                }
                outVal = v;
                return true;
            }
            if (typeStr == "string") {
                outVal = rawStr;
                return true;
            }
            if (typeStr == "Vec2") {
                std::string cleaned = rawStr;
                cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '['), cleaned.end());
                cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ']'), cleaned.end());
                auto parts = SplitByChar(cleaned, ',');
                if (parts.size() != 2) {
                    return false;
                }
                float x = 0.0f;
                float y = 0.0f;
                if (!absl::SimpleAtof(parts[0], &x) || !absl::SimpleAtof(parts[1], &y)) {
                    return false;
                }
                outVal = Vec2(x, y);
                return true;
            }
            if (typeStr == "Vec3") {
                FCT::Vec3<> v{};
                if (!ParseVec3String(rawStr, v)) {
                    return false;
                }
                outVal = v;
                return true;
            }
            if (typeStr == "Vec4") {
                std::string cleaned = rawStr;
                cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '['), cleaned.end());
                cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), ']'), cleaned.end());
                auto parts = SplitByChar(cleaned, ',');
                if (parts.size() != 4) {
                    return false;
                }
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                float w = 0.0f;
                if (!absl::SimpleAtof(parts[0], &x) || !absl::SimpleAtof(parts[1], &y) ||
                    !absl::SimpleAtof(parts[2], &z) || !absl::SimpleAtof(parts[3], &w)) {
                    return false;
                }
                outVal = Vec4(x, y, z, w);
                return true;
            }
            if (typeStr == "string[]") {
                outVal = SplitByChar(rawStr, ',');
                return true;
            }
            return false;
        }

    } // namespace

    ConfigNode CliCommandOutcome::toConfigNode() const {
        ConfigNode node(ConfigTable{});
        node.setBool("ok", ok);
        node.setString("command", command);
        node.setInt("revision", static_cast<int64_t>(revision));
        node.setInt("frame", static_cast<int64_t>(frame));
        if (ok) {
            node.setNode("data", data);
        } else {
            ConfigNode errNode(ConfigTable{});
            errNode.setString("code", error.code);
            errNode.setString("message", error.message);
            errNode.setString("expected", error.expected);
            errNode.setString("hint", error.hint);
            node.setNode("error", std::move(errNode));
        }
        return node;
    }

    EditorCliApplication::EditorCliApplication() = default;

    void EditorCliApplication::registerInfo() {
        registerInfoImpl();
        Runtime::Runtime::instance().setHeadless(true);
        applicationRegisterInfo = ApplicationRegisterInfo{
            .platformRequire = {
                .target = RenderTarget::Window,
                .windowTitle = "MQEngine EditorCli",
            },
            .defaultProjectPath = "UserCode/Projects/DemoProject/project.mq",
            .tags = {"Runtime", "Editor"},
            .cliOptions = {
                { .name = "json", .takesValue = false, .description = "启用纯净 JSON-RPC 模式并重定向日志至 stderr" },
                { .name = "mcp", .takesValue = false, .description = "启用 MCP (Model Context Protocol) 模式" },
                { .name = "headed", .takesValue = false, .description = "开启窗口渲染模式（默认无头模式）" },
                { .name = "cmd", .takesValue = true, .description = "执行单条 CLI 指令并立即退出" },
                { .name = "script", .takesValue = true, .description = "执行脚本文件" },
                { .name = "json-out", .takesValue = true, .description = "JSON 输出文件路径" },
                { .name = "revision", .takesValue = true, .description = "乐观并发控制版本号" },
                { .name = "detail", .takesValue = false, .description = "输出详细调试信息" },
            },
        };
    }

    EditorCliApplication::~EditorCliApplication() {
        m_running.store(false, std::memory_order_release);
        stopStdinReaderThread();
        cleanupFallbackRegistry();
    }

    void EditorCliApplication::ensureFallbackRegistry() {
        auto* engine = Runtime::Engine::getInstance();
        DataManager* dm = (engine && engine->scene) ? engine->scene->getDataManager() : nullptr;
        if (dm && dm->currentRegistries().empty() && g_engineGlobal.registriesManager) {
            if (!m_fallbackRegistry) {
                m_fallbackRegistry = makeUnique<entt::registry>();
            }
            dm->appendRegistry(m_fallbackRegistry.get());
            g_engineGlobal.registriesManager->syncTicker();
        }
    }

    void EditorCliApplication::cleanupFallbackRegistry() {
        if (m_fallbackRegistry) {
            if (DataManager* dm = g_engineGlobal.dataManager) {
                dm->removeRegistry(m_fallbackRegistry.get());
            }
            if (g_engineGlobal.registriesManager) {
                g_engineGlobal.registriesManager->syncTicker();
            }
            m_fallbackRegistry.reset();
        }
    }

    void EditorCliApplication::init() {
        m_running.store(true, std::memory_order_release);
        parseStartupOptions();

        ensureFallbackRegistry();
        renderCallBackDispatcher.subscribe<RenderCallBack::WindowClose>([this](const RenderCallBack::WindowClose&) {
            cleanupFallbackRegistry();
        });

        if (!m_jsonMode && !m_mcpMode) {
            spdlog::info("[EditorCli] MQEngine AI-Native EditorCli 已启动 (Project: '{}', Revision: {})",
                         Runtime::Runtime::instance().getProjectName(), m_revision);
        }

        if (m_interactiveStdin || m_mcpMode) {
            startStdinReaderThread();
        }
    }

    void EditorCliApplication::logicTick() {
        ++m_tickCount;
        drainCommandQueue();

        if (!m_initCommandsExecuted) {
            m_initCommandsExecuted = true;
            if (m_autoExitAfterInitCmds && !m_mcpMode) {
                emitBatchJsonIfNeeded();
                cleanupFallbackRegistry();
                Runtime::Runtime::instance().requestExit();
            }
        }
    }


    void EditorCliApplication::parseStartupOptions() {
        auto& runtime = Runtime::Runtime::instance();
        m_jsonMode = runtime.hasOption("json");
        m_detailMode = runtime.hasOption("detail");
        m_mcpMode = runtime.hasOption("mcp");
        if (!m_mcpMode && runtime.getEngine() && runtime.getEngine()->foundation) {
            const auto& posArgs = runtime.getEngine()->foundation->cliOption.getPositionalArgs();
            if (std::find(posArgs.begin(), posArgs.end(), "mcp") != posArgs.end()) {
                m_mcpMode = true;
            }
        }
        if (runtime.hasOption("headed")) {
            runtime.setHeadless(false);
        }
        m_jsonOutputPath = runtime.getOption("json-out", "");

        if (m_jsonMode || m_mcpMode) {
            RedirectStdoutToStderrForJsonMode();
        }

        std::string cmdBatch = runtime.getOption("cmd", "");
        if (!cmdBatch.empty()) {
            auto cmds = SplitByChar(cmdBatch, ';');
            for (const auto& c : cmds) {
                enqueueCommand(c);
            }
        }

        std::string scriptPath = runtime.getOption("script", "");
        if (!scriptPath.empty()) {
            auto* engine = Runtime::Engine::getInstance();
            ResourceLoader* rl = (engine && engine->resource) ? engine->resource->getResourceLoader() : nullptr;
            if (rl && rl->exists(scriptPath)) {
                auto textRes = rl->loadFile(scriptPath);
                if (textRes.ok()) {
                    std::istringstream iss(*textRes);
                    std::string line;
                    while (std::getline(iss, line)) {
                        std::string trimmed = TrimString(line);
                        if (!trimmed.empty() && trimmed[0] != '#') {
                            enqueueCommand(trimmed);
                        }
                    }
                }
            }
        }

        // 支持直接无前缀位置参数作为单条指令：如 `EditorCli help --tree --json` 或 `EditorCli dev status --json`
        const auto& rawArgs = runtime.getRawArgs();
        std::vector<std::string> directCommandParts;
        for (size_t i = 1; i < rawArgs.size(); ++i) {
            const std::string& arg = rawArgs[i];
            if (arg == "--json" || arg == "--detail" || arg == "--headless" || arg == "--headed" ||
                arg == "--interactive" || arg == "--no-interactive" || arg == "--auto-exit" ||
                arg.rfind("--project=", 0) == 0 || arg.rfind("--scene=", 0) == 0 ||
                arg.rfind("--cmd=", 0) == 0 || arg.rfind("--script=", 0) == 0 ||
                arg.rfind("--json-out=", 0) == 0) {
                continue;
            }
            if (arg == "mcp" || arg == "--mcp") {
                m_mcpMode = true;
                RedirectStdoutToStderrForJsonMode();
                continue;
            }
            directCommandParts.push_back(arg);
        }

        if (!directCommandParts.empty() && cmdBatch.empty() && scriptPath.empty()) {
            std::string singleCmd;
            for (size_t i = 0; i < directCommandParts.size(); ++i) {
                if (i > 0) {
                    singleCmd += " ";
                }
                if (directCommandParts[i].find(' ') != std::string::npos) {
                    singleCmd += "\"" + directCommandParts[i] + "\"";
                } else {
                    singleCmd += directCommandParts[i];
                }
            }
            enqueueCommand(singleCmd);
        }

        bool hasInitialCommands = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            hasInitialCommands = !m_pendingCommands.empty();
        }

        if (runtime.hasOption("auto-exit")) {
            m_autoExitAfterInitCmds = true;
        } else if (hasInitialCommands && !runtime.hasOption("interactive")) {
            m_autoExitAfterInitCmds = true;
        }

        if (runtime.hasOption("no-interactive") || m_autoExitAfterInitCmds) {
            m_interactiveStdin = false;
        } else if (!m_mcpMode) {
            m_interactiveStdin = true;
        }
    }

    void EditorCliApplication::startStdinReaderThread() {
        if (m_stdinThread.joinable()) {
            return;
        }
        m_stdinThread = std::thread([this]() {
            std::string line;
            while (m_running.load(std::memory_order_acquire) && std::getline(std::cin, line)) {
                std::string trimmed = TrimString(line);
                if (trimmed.empty()) {
                    continue;
                }
                enqueueCommand(trimmed);
                if (trimmed == "exit" || trimmed == "quit") {
                    break;
                }
            }
        });
    }

    void EditorCliApplication::stopStdinReaderThread() {
        if (m_stdinThread.joinable()) {
            m_stdinThread.detach();
        }
    }

    void EditorCliApplication::enqueueCommand(const std::string& commandLine) {
        std::string trimmed = TrimString(commandLine);
        if (trimmed.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingCommands.push_back(std::move(trimmed));
    }

    void EditorCliApplication::drainCommandQueue() {
        std::deque<std::string> localQueue;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            localQueue.swap(m_pendingCommands);
        }

        while (!localQueue.empty()) {
            std::string cmd = std::move(localQueue.front());
            localQueue.pop_front();

            if (m_mcpMode && !cmd.empty() && cmd.front() == '{') {
                handleMcpStdioLine(cmd);
                continue;
            }

            CliCommandOutcome outcome = executeCommand(cmd);
            if (m_autoExitAfterInitCmds) {
                m_batchOutcomes.push_back(std::move(outcome));
            } else if (m_jsonMode) {
                emitJsonStdout(outcome.toConfigNode());
            } else {
                if (outcome.ok) {
                    spdlog::info("[EditorCli] {}", SerializeNodeToJson(outcome.toConfigNode()));
                } else {
                    spdlog::error("[EditorCli] [{}] {} (expected: {}, hint: {})",
                                  outcome.error.code, outcome.error.message,
                                  outcome.error.expected, outcome.error.hint);
                }
            }
        }
    }

    void EditorCliApplication::emitBatchJsonIfNeeded() {
        if (m_batchOutcomes.empty()) {
            return;
        }

        ConfigNode outputNode;
        if (m_batchOutcomes.size() == 1) {
            outputNode = m_batchOutcomes.front().toConfigNode();
        } else {
            outputNode = ConfigNode(ConfigTable{});
            bool allOk = true;
            ConfigArray resultsArr;
            for (const auto& item : m_batchOutcomes) {
                if (!item.ok) {
                    allOk = false;
                }
                resultsArr.push_back(item.toConfigNode());
            }
            outputNode.setBool("ok", allOk);
            outputNode.setInt("count", static_cast<int64_t>(m_batchOutcomes.size()));
            outputNode.setInt("revision", static_cast<int64_t>(m_revision));
            outputNode.setInt("frame", static_cast<int64_t>(m_tickCount));
            SetNodeArray(outputNode, "results", std::move(resultsArr));
        }

        if (!m_jsonOutputPath.empty()) {
            auto* engine = Runtime::Engine::getInstance();
            ResourceLoader* rl = (engine && engine->resource) ? engine->resource->getResourceLoader() : nullptr;
            if (rl) {
                (void)rl->saveFile(m_jsonOutputPath, SerializeNodeToJson(outputNode));
            }
        }

        if (m_jsonMode) {
            emitJsonStdout(outputNode);
        } else {
            for (const auto& item : m_batchOutcomes) {
                if (item.ok) {
                    spdlog::info("[EditorCli] [OK] {} (rev={})\n{}",
                                 item.command, item.revision, SerializeNodeToJson(item.data));
                } else {
                    spdlog::error("[EditorCli] [ERR:{}] {} | expected: {} | hint: {}",
                                  item.error.code, item.error.message, item.error.expected, item.error.hint);
                }
            }
        }
    }

    void EditorCliApplication::emitJsonStdout(const ConfigNode& rootNode) const {
        WritePureStdoutLine(SerializeNodeToJson(rootNode));
    }

    void EditorCliApplication::handleMcpStdioLine(const std::string& line) {
        auto reqOr = ConfigDocument::parse(line, ConfigFormat::Json);
        if (!reqOr.ok()) {
            return;
        }
        const ConfigNode& req = reqOr->root();
        std::string method = req.getString("method", "");
        int64_t id = req.getInt("id", -1);
        std::string idStr = req.getString("id", "");

        ConfigNode resp(ConfigTable{});
        resp.setString("jsonrpc", "2.0");
        if (!idStr.empty()) {
            resp.setString("id", idStr);
        } else if (id >= 0) {
            resp.setInt("id", id);
        }

        if (method == "initialize") {
            ConfigNode result(ConfigTable{});
            result.setString("protocolVersion", "2024-11-05");
            ConfigNode serverInfo(ConfigTable{});
            serverInfo.setString("name", "mqengine-editor-cli");
            serverInfo.setString("version", "1.0.0");
            result.setNode("serverInfo", std::move(serverInfo));
            ConfigNode capabilities(ConfigTable{});
            capabilities.setNode("tools", ConfigNode(ConfigTable{}));
            result.setNode("capabilities", std::move(capabilities));
            resp.setNode("result", std::move(result));
            emitJsonStdout(resp);
            return;
        }

        if (method == "notifications/initialized") {
            return;
        }

        if (method == "tools/list") {
            ConfigArray tools;
            auto addTool = [&tools](const char* name, const char* desc) {
                ConfigNode t(ConfigTable{});
                t.setString("name", name);
                t.setString("description", desc);
                ConfigNode schema(ConfigTable{});
                schema.setString("type", "object");
                t.setNode("inputSchema", std::move(schema));
                tools.push_back(std::move(t));
            };
            addTool("mqengine_exec", "Execute any MQEngine EditorCli command (e.g. 'dev status', 'entity inspect Player', 'usercode reload UserCode').");
            addTool("mqengine_status", "Inspect engine runtime state, revision, loaded plugins, and usercode modules.");
            addTool("mqengine_reload", "Hot-reload UserCode and/or Plugins while preserving ECS component state.");

            ConfigNode result(ConfigTable{});
            SetNodeArray(result, "tools", std::move(tools));
            resp.setNode("result", std::move(result));
            emitJsonStdout(resp);
            return;
        }

        if (method == "tools/call") {
            const ConfigNode* params = req.findNode("params");
            std::string toolName = params ? params->getString("name", "") : "";
            const ConfigNode* args = params ? params->findNode("arguments") : nullptr;

            std::string cliCmd = "dev status";
            if (toolName == "mqengine_exec" && args) {
                cliCmd = args->getString("command", "dev status");
            } else if (toolName == "mqengine_status") {
                cliCmd = "dev status";
            } else if (toolName == "mqengine_reload") {
                std::string target = args ? args->getString("target", "all") : "all";
                cliCmd = "dev reload --target=" + target;
            }

            CliCommandOutcome outcome = executeCommand(cliCmd);
            std::string outJson = SerializeNodeToJson(outcome.toConfigNode());

            ConfigNode contentItem(ConfigTable{});
            contentItem.setString("type", "text");
            contentItem.setString("text", outJson);

            ConfigNode result(ConfigTable{});
            SetNodeArray(result, "content", ConfigArray{std::move(contentItem)});
            result.setBool("isError", !outcome.ok);
            resp.setNode("result", std::move(result));
            emitJsonStdout(resp);
            return;
        }

        if (method == "shutdown" || method == "exit") {
            Runtime::Runtime::instance().requestExit();
            return;
        }
    }

    CliCommandOutcome EditorCliApplication::executeCommand(const std::string& rawCommandLine) {
        std::string trimmed = TrimString(rawCommandLine);
        std::vector<std::string> positional;
        std::unordered_map<std::string, std::string> flags;
        TokenizeCommand(trimmed, positional, flags);

        if (positional.empty()) {
            if (flags.count("help") > 0 || flags.count("tree") > 0) {
                positional.push_back("help");
            } else {
                return makeError(trimmed, "empty-command", "未提供可执行的指令",
                                 "EditorCli <domain> <action> [args] [--flags]",
                                 "运行 'EditorCli help --tree --json' 查看完整指令树");
            }
        }

        // 乐观并发 Revision 栅栏校验（对齐 ForgeAX --revision=<rev> 机制）
        auto revIt = flags.find("revision");
        if (revIt != flags.end()) {
            uint64_t expectedRev = 0;
            if (!absl::SimpleAtoi(revIt->second, &expectedRev)) {
                return makeError(trimmed, "invalid-revision-flag",
                                 "无效的 --revision 参数: " + revIt->second,
                                 "--revision=<uint64>",
                                 "请先调用 'dev status' 获取当前最新 revision");
            }
            if (expectedRev != m_revision) {
                return makeError(trimmed, "live-revision-stale",
                                 "乐观并发版本不匹配：当前状态已被更新 (expected=" +
                                     std::to_string(expectedRev) + ", actual=" + std::to_string(m_revision) + ")",
                                 "revision == " + std::to_string(m_revision),
                                 "请重新执行 'dev status' 或 'entity inspect' 获取最新快照与 revision 后重试");
            }
        }

        return dispatchParsedTokens(trimmed, positional, flags);
    }

    CliCommandOutcome EditorCliApplication::dispatchParsedTokens(
        const std::string& fullCmd,
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& flags) {

        const std::string& domain = positional[0];

        if (domain == "help" || domain == "--help" || domain == "-h") {
            return handleHelpCommand(positional, flags);
        }
        if (domain == "project") {
            return handleProjectCommand(positional, flags);
        }
        if (domain == "dev") {
            return handleDevCommand(positional, flags);
        }
        if (domain == "scene") {
            return handleSceneCommand(positional, flags);
        }
        if (domain == "entity") {
            return handleEntityCommand(positional, flags);
        }
        if (domain == "component") {
            return handleComponentCommand(positional, flags);
        }
        if (domain == "plugin" || domain == "plugins") {
            return handlePluginCommand(positional, flags);
        }
        if (domain == "usercode") {
            return handleUserCodeCommand(positional, flags);
        }

        // 兼容简写别名（如 status / reload_all / reload_usercode / reload_plugin / exit）
        if (domain == "status") {
            return handleDevCommand({"dev", "status"}, flags);
        }
        if (domain == "reload_all") {
            return handleDevCommand({"dev", "reload"}, flags);
        }
        if (domain == "reload_usercode") {
            std::vector<std::string> mapped = {"usercode", "reload"};
            if (positional.size() > 1) {
                mapped.push_back(positional[1]);
            }
            return handleUserCodeCommand(mapped, flags);
        }
        if (domain == "reload_plugin") {
            std::vector<std::string> mapped = {"plugin", "reload"};
            if (positional.size() > 1) {
                mapped.push_back(positional[1]);
            }
            return handlePluginCommand(mapped, flags);
        }
        if (domain == "load_plugin") {
            std::vector<std::string> mapped = {"plugin", "load"};
            if (positional.size() > 1) {
                mapped.push_back(positional[1]);
            }
            return handlePluginCommand(mapped, flags);
        }
        if (domain == "unload_plugin") {
            std::vector<std::string> mapped = {"plugin", "unload"};
            if (positional.size() > 1) {
                mapped.push_back(positional[1]);
            }
            return handlePluginCommand(mapped, flags);
        }
        if (domain == "exit" || domain == "quit") {
            Runtime::Runtime::instance().requestExit();
            ConfigNode data(ConfigTable{});
            data.setBool("exiting", true);
            return makeSuccess(fullCmd, std::move(data));
        }

        return makeError(fullCmd, "unknown-command-domain",
                         "未知指令域: '" + domain + "'",
                         "help | project | dev | scene | entity | component | plugin | usercode | exit",
                         "运行 'EditorCli help --tree --json' 浏览所有支持的 AI 领域指令与参数说明");
    }

    CliCommandOutcome EditorCliApplication::handleHelpCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& flags) const {

        std::string filterDomain;
        if (positional.size() > 1) {
            filterDomain = positional[1];
        }
        ConfigNode treeNode = buildCommandTreeNode(filterDomain);
        treeNode.setBool("tree", flags.count("tree") > 0 || filterDomain.empty());
        return makeSuccess("help", std::move(treeNode));
    }

    CliCommandOutcome EditorCliApplication::handleProjectCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& /*flags*/) {

        std::string sub = positional.size() > 1 ? positional[1] : "check";
        auto* engine = Runtime::Engine::getInstance();
        auto& runtime = Runtime::Runtime::instance();

        if (sub == "check" || sub == "info") {
            ConfigNode data(ConfigTable{});
            std::string projDir = (engine && engine->foundation) ? engine->foundation->getProjectDirectory() : "";
            std::string projFile = (engine && engine->foundation) ? engine->foundation->getCurrentConfig().projectFilePath : "";
            std::string assetRoot = (engine && engine->resource) ? engine->resource->getAssetRoot() : "";
            bool projOpen = (engine && engine->foundation) ? engine->foundation->isProjectOpen() : false;

            data.setString("projectName", runtime.getProjectName());
            data.setString("projectPath", projDir);
            data.setString("projectFilePath", projFile);
            data.setString("assetRoot", assetRoot);
            data.setBool("headless", runtime.isHeadless());
            data.setBool("valid", projOpen || !projDir.empty());
            data.setBool("autoHotReload", runtime.isAutoHotReloadEnabled());

            std::vector<std::string> sceneNames;
            if (engine && engine->scene && engine->scene->getDataManager()) {
                for (const auto& [uuid, name] : engine->scene->getDataManager()->getSceneUuidToNameMap()) {
                    sceneNames.push_back(name);
                }
            }
            std::sort(sceneNames.begin(), sceneNames.end());
            data.setStringArray("scenes", sceneNames);

            if (engine && engine->host && engine->host->getPluginManager()) {
                data.setInt("pluginCount", static_cast<int64_t>(engine->host->getPluginManager()->getAllPluginNames().size()));
            }
            if (engine && engine->host && engine->host->getUserCodeManager()) {
                data.setInt("userCodeCount", static_cast<int64_t>(engine->host->getUserCodeManager()->getAllUserCodeNames().size()));
            }
            if (engine && engine->foundation && engine->foundation->getComponentReflection()) {
                data.setStringArray("registeredComponents", engine->foundation->getComponentReflection()->getRegisteredComponentNames());
            }
            return makeSuccess("project " + sub, std::move(data));
        }

        return makeError("project " + sub, "unknown-project-subcommand",
                         "未知 project 子指令: '" + sub + "'",
                         "project check | project info",
                         "使用 'project check --json' 验证当前工程配置、场景与模块状态");
    }

    CliCommandOutcome EditorCliApplication::handleDevCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& flags) {

        std::string sub = positional.size() > 1 ? positional[1] : "status";
        auto* engine = Runtime::Engine::getInstance();
        auto& runtime = Runtime::Runtime::instance();

        if (sub == "status") {
            return makeSuccess("dev status", buildDevStatusNode());
        }

        if (sub == "verify") {
            ConfigNode data(ConfigTable{});
            ConfigArray diagnostics;
            bool passed = true;

            if (engine && engine->host && engine->host->getPluginManager()) {
                auto* pm = engine->host->getPluginManager();
                for (const auto& name : pm->getAllPluginNames()) {
                    const auto* p = pm->getPluginEntry(name);
                    if (p && p->state == Runtime::PluginState::Failed) {
                        passed = false;
                        ConfigNode diag(ConfigTable{});
                        diag.setString("severity", "error");
                        diag.setString("source", "Plugin:" + p->name);
                        diag.setString("message", p->errorMessage);
                        diagnostics.push_back(std::move(diag));
                    }
                }
            }
            if (engine && engine->host && engine->host->getUserCodeManager()) {
                auto* ucm = engine->host->getUserCodeManager();
                for (const auto& name : ucm->getAllUserCodeNames()) {
                    const auto* m = ucm->getUserCodeEntry(name);
                    if (m && m->state == Runtime::UserCodeState::Failed) {
                        passed = false;
                        ConfigNode diag(ConfigTable{});
                        diag.setString("severity", "error");
                        diag.setString("source", "UserCode:" + m->name);
                        diag.setString("message", m->errorMessage);
                        diagnostics.push_back(std::move(diag));
                    }
                }
            }

            size_t totalEntities = 0;
            if (DataManager* dm = (engine && engine->scene) ? engine->scene->getDataManager() : nullptr) {
                for (entt::registry* reg : dm->currentRegistries()) {
                    if (!reg) {
                        continue;
                    }
                    const auto& storage = reg->storage<entt::entity>();
                    for (entt::entity ent : storage) {
                        if (reg->valid(ent)) {
                            ++totalEntities;
                        }
                    }
                }
            }

            data.setBool("passed", passed);
            data.setInt("entityCount", static_cast<int64_t>(totalEntities));
            data.setInt("diagnosticCount", static_cast<int64_t>(diagnostics.size()));
            SetNodeArray(data, "diagnostics", std::move(diagnostics));
            return makeSuccess("dev verify", std::move(data));
        }

        if (sub == "find") {
            std::string pattern = positional.size() > 2 ? positional[2] : "";
            std::string compFilter;
            auto compIt = flags.find("component");
            if (compIt != flags.end()) {
                compFilter = compIt->second;
            }
            int limit = 50;
            auto limIt = flags.find("limit");
            if (limIt != flags.end()) {
                absl::SimpleAtoi(limIt->second, &limit);
            }

            ConfigArray matches;
            DataManager* dm = (engine && engine->scene) ? engine->scene->getDataManager() : nullptr;
            ComponentReflection* refl = (engine && engine->foundation) ? engine->foundation->getComponentReflection() : nullptr;
            if (dm) {
                const auto regs = dm->currentRegistries();
                for (size_t rIdx = 0; rIdx < regs.size(); ++rIdx) {
                    entt::registry* reg = regs[rIdx];
                    if (!reg) {
                        continue;
                    }
                    const auto& storage = reg->storage<entt::entity>();
                    for (entt::entity ent : storage) {
                        if (!reg->valid(ent)) {
                            continue;
                        }
                        std::string entName;
                        if (reg->all_of<NameTag>(ent)) {
                            entName = reg->get<NameTag>(ent).name;
                        }
                        std::string idStr = std::to_string(static_cast<uint32_t>(entt::to_integral(ent)));
                        if (!pattern.empty() &&
                            entName.find(pattern) == std::string::npos &&
                            idStr != pattern) {
                            continue;
                        }
                        if (!compFilter.empty() && refl) {
                            if (!refl->hasComponent(*reg, ent, compFilter)) {
                                continue;
                            }
                        }
                        matches.push_back(buildEntityInspectNode(*reg, ent, rIdx));
                        if (static_cast<int>(matches.size()) >= limit) {
                            break;
                        }
                    }
                }
            }

            ConfigNode data(ConfigTable{});
            data.setString("query", pattern);
            data.setString("componentFilter", compFilter);
            data.setInt("count", static_cast<int64_t>(matches.size()));
            SetNodeArray(data, "entities", std::move(matches));
            return makeSuccess("dev find", std::move(data));
        }

        if (sub == "focus" || sub == "camera") {
            std::string targetId = positional.size() > 2 ? positional[2] : "";
            auto entFlag = flags.find("entity");
            if (entFlag != flags.end()) {
                targetId = entFlag->second;
            }
            float distance = 5.0f;
            auto distIt = flags.find("distance");
            if (distIt != flags.end()) {
                absl::SimpleAtof(distIt->second, &distance);
            }

            FCT::Vec3<> targetPos(0.0f, 0.0f, 0.0f);
            if (!targetId.empty()) {
                entt::registry* reg = nullptr;
                entt::entity ent = entt::null;
                size_t rIdx = 0;
                if (!resolveEntityInCurrentScene(targetId, reg, ent, rIdx)) {
                    return makeError("dev " + sub, "entity-not-found",
                                     "未找到聚焦目标实体: '" + targetId + "'",
                                     "有效的实体名称、UUID 或实体数值 ID",
                                     "运行 'entity list --json' 查看当前场景实体");
                }
                if (reg->all_of<PositionComponent>(ent)) {
                    targetPos = reg->get<PositionComponent>(ent).position;
                }
            }

            FCT::Vec3<> camPos(targetPos.x, targetPos.y + distance * 0.5f, targetPos.z + distance);
            auto posFlag = flags.find("pos");
            if (posFlag != flags.end()) {
                ParseVec3String(posFlag->second, camPos);
            }

            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setString("targetEntity", targetId);
            SetNodeArray(data, "cameraPosition", ConfigArray{
                ConfigNode(static_cast<double>(camPos.x)),
                ConfigNode(static_cast<double>(camPos.y)),
                ConfigNode(static_cast<double>(camPos.z))
            });
            SetNodeArray(data, "lookAt", ConfigArray{
                ConfigNode(static_cast<double>(targetPos.x)),
                ConfigNode(static_cast<double>(targetPos.y)),
                ConfigNode(static_cast<double>(targetPos.z))
            });
            return makeSuccess("dev " + sub, std::move(data));
        }

        if (sub == "advance" || sub == "step") {
            int frames = 1;
            float dt = 0.016f;
            auto fIt = flags.find("frames");
            if (fIt != flags.end()) {
                absl::SimpleAtoi(fIt->second, &frames);
            } else if (positional.size() > 2) {
                absl::SimpleAtoi(positional[2], &frames);
            }
            auto dtIt = flags.find("dt");
            if (dtIt != flags.end()) {
                absl::SimpleAtof(dtIt->second, &dt);
            }
            frames = std::clamp(frames, 1, 3600);

            runtime.setPlayMode(true);
            runtime.getSystemManager().flushRequests();
            for (int i = 0; i < frames; ++i) {
                if (engine && engine->host && engine->host->getPluginManager()) {
                    engine->host->getPluginManager()->logicTick(dt);
                }
                if (engine && engine->host && engine->host->getUserCodeManager()) {
                    engine->host->getUserCodeManager()->logicTick(dt);
                }
                for (const auto& [sysName, sysCfg] : runtime.getSystemManager().getConfigs()) {
                    if (!sysCfg.enabled || !sysCfg.system) {
                        continue;
                    }
                    if (sysName == "MatrixCacheSystem" || sysName == "TextureRenderSystem" ||
                        sysName == "ShininessSystem" || sysName == "MeshRenderSystem" ||
                        sysName == "SkyboxCacheSystem" || sysName == "ResourceActiveSystem" ||
                        sysName == "TextureSamplerSystem" || sysName == "CameraSystem" ||
                        sysName == "LightingSystem") {
                        continue;
                    }
                    sysCfg.system->updateLogic();
                }
                ++m_tickCount;
            }
            bumpRevision();

            ConfigNode data(ConfigTable{});
            data.setInt("advancedFrames", frames);
            data.setFloat("deltaTime", static_cast<double>(dt));
            data.setInt("totalFrames", static_cast<int64_t>(m_tickCount));
            return makeSuccess("dev advance", std::move(data));
        }

        if (sub == "play") {
            runtime.setPlayMode(true);
            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setBool("isPlaying", true);
            return makeSuccess("dev play", std::move(data));
        }

        if (sub == "stop" || sub == "pause") {
            runtime.setPlayMode(false);
            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setBool("isPlaying", false);
            return makeSuccess("dev stop", std::move(data));
        }

        if (sub == "reload") {
            std::string target = "all";
            auto tIt = flags.find("target");
            if (tIt != flags.end()) {
                target = tIt->second;
            } else if (positional.size() > 2) {
                target = positional[2];
            }

            ConfigArray reloadedModules;
            if (target == "all") {
                runtime.hotReloadAll();
                if (engine && engine->host && engine->host->getUserCodeManager()) {
                    for (const auto& name : engine->host->getUserCodeManager()->getAllUserCodeNames()) {
                        reloadedModules.push_back(ConfigNode("usercode:" + name));
                    }
                }
                if (engine && engine->host && engine->host->getPluginManager()) {
                    auto* pm = engine->host->getPluginManager();
                    for (const auto& name : pm->getAllPluginNames()) {
                        const auto* entry = pm->getPluginEntry(name);
                        if (entry && (entry->isDynamic || entry->isScript)) {
                            reloadedModules.push_back(ConfigNode("plugin:" + name));
                        }
                    }
                }
            } else if (target == "usercode") {
                if (engine && engine->host && engine->host->getUserCodeManager()) {
                    for (const auto& name : engine->host->getUserCodeManager()->getAllUserCodeNames()) {
                        Status st = runtime.reloadUserCode(name);
                        if (!st.ok()) {
                            return makeError("dev reload", "usercode-reload-failed",
                                             std::string(st.message()),
                                             "UserCode.dll 编译通过且导出符号完整",
                                             "检查 UserCode 编译输出并重试 'usercode reload'");
                        }
                        reloadedModules.push_back(ConfigNode("usercode:" + name));
                    }
                }
            } else if (target == "plugins" || target == "plugin") {
                if (engine && engine->host && engine->host->getPluginManager()) {
                    auto* pm = engine->host->getPluginManager();
                    for (const auto& name : pm->getAllPluginNames()) {
                        const auto* entry = pm->getPluginEntry(name);
                        if (entry && (entry->isDynamic || entry->isScript)) {
                            Status st = runtime.reloadPlugin(name);
                            if (!st.ok()) {
                                return makeError("dev reload", "plugin-reload-failed",
                                                 std::string(st.message()),
                                                 "插件 DLL 处于可重编译/重加载状态",
                                                 "检查插件名与 DLL 路径后重试 'plugin reload <name>'");
                            }
                            reloadedModules.push_back(ConfigNode("plugin:" + name));
                        }
                    }
                }
            }

            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setString("target", target);
            SetNodeArray(data, "reloaded", std::move(reloadedModules));
            return makeSuccess("dev reload", std::move(data));
        }

        return makeError("dev " + sub, "unknown-dev-subcommand",
                         "未知 dev 子指令: '" + sub + "'",
                         "dev status | dev verify | dev find | dev focus | dev camera | dev advance | dev play | dev stop | dev reload",
                         "运行 'EditorCli help dev --json' 查看 dev 域指令用法");
    }

    CliCommandOutcome EditorCliApplication::handleSceneCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& /*flags*/) {

        std::string sub = positional.size() > 1 ? positional[1] : "info";
        auto* engine = Runtime::Engine::getInstance();
        DataManager* dm = (engine && engine->scene) ? engine->scene->getDataManager() : nullptr;
        if (!dm) {
            return makeError("scene " + sub, "data-manager-unavailable",
                             "DataManager 尚未初始化",
                             "有效工程与 DataManager 实例",
                             "使用 --project=<path> 指定有效工程路径");
        }

        auto getCurrentSceneUuid = [dm]() -> std::string {
            return dm->getCurrentSceneUuid();
        };
        auto getCurrentSceneName = [dm]() -> std::string {
            std::string curUuid = dm->getCurrentSceneUuid();
            return curUuid.empty() ? "" : dm->getSceneNameByUuid(curUuid);
        };

        if (sub == "list") {
            std::vector<std::string> sceneNames;
            for (const auto& [uuid, name] : dm->getSceneUuidToNameMap()) {
                sceneNames.push_back(name);
            }
            std::sort(sceneNames.begin(), sceneNames.end());
            ConfigNode data(ConfigTable{});
            data.setStringArray("scenes", sceneNames);
            data.setInt("count", static_cast<int64_t>(sceneNames.size()));
            return makeSuccess("scene list", std::move(data));
        }

        if (sub == "info") {
            ConfigNode data(ConfigTable{});
            const auto regs = dm->currentRegistries();
            data.setInt("registryCount", static_cast<int64_t>(regs.size()));

            int64_t entityCount = 0;
            for (entt::registry* reg : regs) {
                if (!reg) {
                    continue;
                }
                const auto& storage = reg->storage<entt::entity>();
                for (entt::entity ent : storage) {
                    if (reg->valid(ent)) {
                        ++entityCount;
                    }
                }
            }
            data.setInt("entityCount", entityCount);
            data.setString("sceneUuid", getCurrentSceneUuid());
            data.setString("sceneName", getCurrentSceneName());
            return makeSuccess("scene info", std::move(data));
        }

        if (sub == "open" || sub == "load") {
            if (positional.size() < 3) {
                return makeError("scene open", "missing-scene-name",
                                 "缺少要打开的场景名称或 UUID",
                                 "scene open <sceneNameOrUuid>",
                                 "先运行 'scene list --json' 获取可用场景列表");
            }
            std::string target = positional[2];
            std::string resolvedUuid = target;
            for (const auto& [uuid, name] : dm->getSceneUuidToNameMap()) {
                if (name == target || uuid == target) {
                    resolvedUuid = uuid;
                    break;
                }
            }
            dm->openScene(resolvedUuid);
            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setString("openedScene", target);
            data.setString("sceneUuid", getCurrentSceneUuid());
            return makeSuccess("scene open", std::move(data));
        }

        if (sub == "new" || sub == "create") {
            std::string sceneName = positional.size() > 2 ? positional[2] : "NewScene";
            dm->newScene(sceneName);
            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setString("sceneName", sceneName);
            data.setString("sceneUuid", getCurrentSceneUuid());
            return makeSuccess("scene new", std::move(data));
        }

        if (sub == "save") {
            std::string curUuid = getCurrentSceneUuid();
            if (curUuid.empty()) {
                return makeError("scene save", "no-active-scene",
                                 "当前没有打开的持久化场景 UUID",
                                 "已打开的持久化场景",
                                 "先执行 'scene open <name>' 或 'scene new <name>' 后再保存");
            }
            bool saved = dm->saveScene(curUuid);
            if (!saved) {
                return makeError("scene save", "scene-save-failed",
                                 "保存场景失败: " + curUuid,
                                 "可写的场景文件路径",
                                 "检查工程目录写权限");
            }
            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setBool("saved", true);
            data.setString("sceneUuid", curUuid);
            return makeSuccess("scene save", std::move(data));
        }

        return makeError("scene " + sub, "unknown-scene-subcommand",
                         "未知 scene 子指令: '" + sub + "'",
                         "scene list | scene info | scene open <name> | scene new <name> | scene save",
                         "运行 'EditorCli help scene --json' 查看详情");
    }


    CliCommandOutcome EditorCliApplication::handleEntityCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& flags) {

        std::string sub = positional.size() > 1 ? positional[1] : "list";
        auto* engine = Runtime::Engine::getInstance();
        DataManager* dm = (engine && engine->scene) ? engine->scene->getDataManager() : nullptr;
        if (!dm) {
            return makeError("entity " + sub, "data-manager-unavailable",
                             "DataManager 未就绪", "有效工程", "检查 --project 参数");
        }

        if (sub == "list") {
            ConfigArray entities;
            const auto regs = dm->currentRegistries();
            for (size_t rIdx = 0; rIdx < regs.size(); ++rIdx) {
                entt::registry* reg = regs[rIdx];
                if (!reg) {
                    continue;
                }
                const auto& storage = reg->storage<entt::entity>();
                for (entt::entity ent : storage) {
                    if (!reg->valid(ent)) {
                        continue;
                    }
                    entities.push_back(buildEntityInspectNode(*reg, ent, rIdx));
                }
            }
            ConfigNode data(ConfigTable{});
            data.setInt("count", static_cast<int64_t>(entities.size()));
            SetNodeArray(data, "entities", std::move(entities));
            return makeSuccess("entity list", std::move(data));
        }

        if (sub == "inspect" || sub == "get") {
            if (positional.size() < 3) {
                return makeError("entity inspect", "missing-entity-identifier",
                                 "缺少实体名称、UUID 或实体 ID",
                                 "entity inspect <name|uuid|id>",
                                 "先运行 'entity list --json' 获取当前场景中的实体标识符");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("entity inspect", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "当前场景中存在的实体名称、UUID 或数值 ID",
                                 "运行 'entity list --json' 或 'dev find <pattern> --json' 查找实体");
            }
            return makeSuccess("entity inspect", buildEntityInspectNode(*reg, ent, rIdx));
        }

        if (sub == "spawn" || sub == "create") {
            const auto regs = dm->currentRegistries();
            if (regs.empty() || !regs[0]) {
                return makeError("entity spawn", "no-active-registry",
                                 "当前没有可写入的 ECS Registry",
                                 "至少打开一个场景或初始化默认 Registry",
                                 "运行 'scene open <name>'");
            }
            entt::registry& reg = *regs[0];
            entt::entity ent = reg.create();

            std::string entName = "Entity_" + std::to_string(static_cast<uint32_t>(entt::to_integral(ent)));
            auto nameFlag = flags.find("name");
            if (nameFlag != flags.end() && !nameFlag->second.empty()) {
                entName = nameFlag->second;
            } else if (positional.size() > 2) {
                entName = positional[2];
            }

            NameTag tag;
            tag.name = entName;
            reg.emplace_or_replace<NameTag>(ent, std::move(tag));

            FCT::Vec3<> pos(0.0f, 0.0f, 0.0f);
            auto posFlag = flags.find("pos");
            if (posFlag != flags.end()) {
                ParseVec3String(posFlag->second, pos);
            }
            reg.emplace_or_replace<PositionComponent>(ent, PositionComponent{pos});

            FCT::Vec3<> rot(0.0f, 0.0f, 0.0f);
            auto rotFlag = flags.find("rot");
            if (rotFlag != flags.end()) {
                ParseVec3String(rotFlag->second, rot);
            }
            reg.emplace_or_replace<RotationComponent>(ent, RotationComponent{rot});

            FCT::Vec3<> scale(1.0f, 1.0f, 1.0f);
            auto scaleFlag = flags.find("scale");
            if (scaleFlag != flags.end()) {
                ParseVec3String(scaleFlag->second, scale);
            }
            reg.emplace_or_replace<ScaleComponent>(ent, ScaleComponent{scale});
            reg.emplace_or_replace<CacheRotationMatrix>(ent);

            auto compsFlag = flags.find("components");
            if (compsFlag != flags.end() && engine && engine->foundation && engine->foundation->getComponentReflection()) {
                auto compNames = SplitByChar(compsFlag->second, ',');
                for (const auto& cName : compNames) {
                    engine->foundation->getComponentReflection()->addComponent(reg, ent, cName);
                }
            }

            bumpRevision();
            return makeSuccess("entity spawn", buildEntityInspectNode(reg, ent, 0));
        }

        if (sub == "despawn" || sub == "destroy" || sub == "delete") {
            if (positional.size() < 3) {
                return makeError("entity despawn", "missing-entity-identifier",
                                 "缺少要销毁的实体名称、UUID 或 ID",
                                 "entity despawn <name|uuid|id>",
                                 "运行 'entity list --json' 查看可销毁实体");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("entity despawn", "entity-not-found",
                                 "未找到要销毁的实体: '" + positional[2] + "'",
                                 "当前场景中存在的实体标识符",
                                 "检查名称或 ID 是否拼写正确");
            }
            uint32_t rawId = static_cast<uint32_t>(entt::to_integral(ent));
            std::string entName = reg->all_of<NameTag>(ent) ? reg->get<NameTag>(ent).name : "";
            reg->destroy(ent);
            bumpRevision();

            ConfigNode data(ConfigTable{});
            data.setBool("despawned", true);
            data.setInt("entityId", static_cast<int64_t>(rawId));
            data.setString("name", entName);
            return makeSuccess("entity despawn", std::move(data));
        }

        if (sub == "rename") {
            if (positional.size() < 4) {
                return makeError("entity rename", "missing-arguments",
                                 "重命名实体需要提供原标识符与新名称",
                                 "entity rename <entity> <newName>",
                                 "示例: 'entity rename 1 HeroSpinner'");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("entity rename", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "当前场景中存在的实体标识符",
                                 "运行 'entity list --json' 查看实体列表");
            }
            if (reg->all_of<NameTag>(ent)) {
                reg->get<NameTag>(ent).name = positional[3];
            } else {
                NameTag tag;
                tag.name = positional[3];
                reg->emplace<NameTag>(ent, std::move(tag));
            }
            bumpRevision();
            return makeSuccess("entity rename", buildEntityInspectNode(*reg, ent, rIdx));
        }

        return makeError("entity " + sub, "unknown-entity-subcommand",
                         "未知 entity 子指令: '" + sub + "'",
                         "entity list | entity inspect <entity> | entity spawn [--name=...] [--pos=x,y,z] [--components=A,B] | entity despawn <entity> | entity rename <entity> <newName>",
                         "运行 'EditorCli help entity --json' 查看详情");
    }

    CliCommandOutcome EditorCliApplication::handleComponentCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& /*flags*/) {

        std::string sub = positional.size() > 1 ? positional[1] : "list";
        auto* engine = Runtime::Engine::getInstance();
        ComponentReflection* refl = (engine && engine->foundation) ? engine->foundation->getComponentReflection() : nullptr;
        if (!refl) {
            return makeError("component " + sub, "reflection-unavailable",
                             "ComponentReflection 子系统尚未初始化",
                             "Runtime::Engine::getInstance().getComponentReflection() != nullptr",
                             "检查引擎初始化状态");
        }

        if (sub == "list") {
            auto names = refl->getRegisteredComponentNames();
            std::sort(names.begin(), names.end());
            ConfigArray schemas;
            for (const auto& name : names) {
                if (const auto* info = refl->findComponent(name)) {
                    schemas.push_back(buildComponentSchemaNode(*info));
                }
            }
            ConfigNode data(ConfigTable{});
            data.setInt("count", static_cast<int64_t>(names.size()));
            data.setStringArray("names", names);
            SetNodeArray(data, "components", std::move(schemas));
            return makeSuccess("component list", std::move(data));
        }

        if (sub == "schema") {
            if (positional.size() < 3) {
                return makeError("component schema", "missing-component-name",
                                 "缺少组件名称",
                                 "component schema <ComponentName>",
                                 "先运行 'component list --json' 获取所有已注册的反射组件名");
            }
            const auto* info = refl->findComponent(positional[2]);
            if (!info) {
                return makeError("component schema", "component-not-registered",
                                 "未找到已注册组件: '" + positional[2] + "'",
                                 "已通过 REGISTER_COMPONENT_PFR 注册的组件名",
                                 "运行 'component list --json' 查看全部可用组件");
            }
            return makeSuccess("component schema", buildComponentSchemaNode(*info));
        }

        if (sub == "get") {
            if (positional.size() < 4) {
                return makeError("component get", "missing-arguments",
                                 "缺少实体标识或组件名称",
                                 "component get <entity> <ComponentName>",
                                 "示例: 'component get AgentSpinner UserRotateComponent'");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("component get", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "有效实体名称、UUID 或 ID",
                                 "运行 'entity list --json' 查看实体");
            }
            const std::string& compName = positional[3];
            const auto* info = refl->findComponent(compName);
            if (!info) {
                return makeError("component get", "component-not-registered",
                                 "未注册的组件类型: '" + compName + "'",
                                 "已注册组件名称",
                                 "运行 'component list --json'");
            }
            if (!refl->hasComponent(*reg, ent, compName)) {
                return makeError("component get", "entity-missing-component",
                                 "实体 '" + positional[2] + "' 未挂载组件 '" + compName + "'",
                                 "实体已包含该组件",
                                 "先运行 'component add " + positional[2] + " " + compName + "'");
            }

            ConfigNode fieldsNode(ConfigTable{});
            for (const auto& f : info->fields) {
                ComponentValue val = refl->getComponentField(*reg, ent, compName, f.name);
                fieldsNode.setNode(f.name, ComponentValueToConfigNode(val));
            }
            ConfigNode data(ConfigTable{});
            data.setString("entity", positional[2]);
            data.setString("component", compName);
            data.setNode("fields", std::move(fieldsNode));
            return makeSuccess("component get", std::move(data));
        }

        if (sub == "add") {
            if (positional.size() < 4) {
                return makeError("component add", "missing-arguments",
                                 "缺少实体标识或组件名",
                                 "component add <entity> <ComponentName>",
                                 "示例: 'component add AgentSpinner UserRotateComponent'");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("component add", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "有效实体标识",
                                 "运行 'entity list --json'");
            }
            const std::string& compName = positional[3];
            if (!refl->findComponent(compName)) {
                return makeError("component add", "component-not-registered",
                                 "未知组件类型: '" + compName + "'",
                                 "已注册的组件名称",
                                 "运行 'component list --json' 查看所有支持反射添加的组件");
            }
            refl->addComponent(*reg, ent, compName);
            if (!refl->hasComponent(*reg, ent, compName)) {
                return makeError("component add", "component-add-failed",
                                 "向实体添加组件 '" + compName + "' 失败",
                                 "组件支持默认构造与反射添加",
                                 "检查 ComponentReflection 注册信息");
            }
            bumpRevision();
            return makeSuccess("component add", buildEntityInspectNode(*reg, ent, rIdx));
        }

        if (sub == "remove") {
            if (positional.size() < 4) {
                return makeError("component remove", "missing-arguments",
                                 "缺少实体标识或组件名",
                                 "component remove <entity> <ComponentName>",
                                 "示例: 'component remove AgentSpinner UserRotateComponent'");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("component remove", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "有效实体标识",
                                 "运行 'entity list --json'");
            }
            const std::string& compName = positional[3];
            if (!refl->hasComponent(*reg, ent, compName)) {
                return makeError("component remove", "component-remove-failed",
                                 "移除组件 '" + compName + "' 失败或实体未挂载该组件",
                                 "实体已挂载该组件",
                                 "运行 'entity inspect " + positional[2] + " --json' 确认已挂载组件");
            }
            refl->removeComponent(*reg, ent, compName);
            bumpRevision();
            return makeSuccess("component remove", buildEntityInspectNode(*reg, ent, rIdx));
        }

        if (sub == "set") {
            if (positional.size() < 5) {
                return makeError("component set", "missing-arguments",
                                 "缺少实体标识、组件名称或字段赋值表达式",
                                 "component set <entity> <ComponentName> <field>=<value> [<field2>=<value2> ...]",
                                 "示例: 'component set AgentSpinner UserRotateComponent speed=180 axis=0,1,0 enabled=true'");
            }
            entt::registry* reg = nullptr;
            entt::entity ent = entt::null;
            size_t rIdx = 0;
            if (!resolveEntityInCurrentScene(positional[2], reg, ent, rIdx)) {
                return makeError("component set", "entity-not-found",
                                 "未找到目标实体: '" + positional[2] + "'",
                                 "有效实体名称、UUID 或数值 ID",
                                 "运行 'entity list --json' 查看当前场景实体");
            }

            const std::string& compName = positional[3];
            const auto* info = refl->findComponent(compName);
            if (!info) {
                return makeError("component set", "component-not-registered",
                                 "组件 '" + compName + "' 未注册到反射表",
                                 "已注册的组件名称",
                                 "运行 'component list --json' 获取支持的组件列表");
            }
            if (!refl->hasComponent(*reg, ent, compName)) {
                refl->addComponent(*reg, ent, compName);
            }

            // 支持 `field=value` 形式，以及 `component set <entity> <Comp> <field> <value>` 形式
            std::vector<std::pair<std::string, std::string>> assignments;
            for (size_t i = 4; i < positional.size(); ++i) {
                const std::string& expr = positional[i];
                size_t eqPos = expr.find('=');
                if (eqPos != std::string::npos) {
                    assignments.emplace_back(expr.substr(0, eqPos), expr.substr(eqPos + 1));
                } else if (i + 1 < positional.size() && positional[i + 1].find('=') == std::string::npos) {
                    assignments.emplace_back(expr, positional[i + 1]);
                    ++i;
                } else {
                    return makeError("component set", "invalid-field-assignment",
                                     "无效的字段赋值语法: '" + expr + "'",
                                     "<field>=<value>",
                                     "运行 'component schema " + compName + " --json' 查看该组件的所有字段名称与类型");
                }
            }

            ConfigNode updatedFields(ConfigTable{});
            for (const auto& [fieldName, rawVal] : assignments) {
                const ComponentFieldInfo* targetField = nullptr;
                for (const auto& f : info->fields) {
                    if (f.name == fieldName) {
                        targetField = &f;
                        break;
                    }
                }
                if (!targetField) {
                    std::string expectedFields;
                    for (size_t k = 0; k < info->fields.size(); ++k) {
                        if (k > 0) {
                            expectedFields += ", ";
                        }
                        expectedFields += info->fields[k].name + ":" + info->fields[k].type;
                    }
                    return makeError("component set", "unknown-component-field",
                                     "组件 '" + compName + "' 不存在字段 '" + fieldName + "'",
                                     expectedFields,
                                     "运行 'component schema " + compName + " --json' 核对字段名");
                }

                ComponentValue parsedValue;
                if (!ParseComponentFieldValue(targetField->type, rawVal, parsedValue)) {
                    return makeError("component set", "field-type-parse-error",
                                     "无法将 '" + rawVal + "' 解析为字段 '" + fieldName + "' 的目标类型 '" + targetField->type + "'",
                                     targetField->type,
                                     "Vec3 格式示例: 0,1,0；float 示例: 90.0；bool 示例: true");
                }

                refl->setComponentField(*reg, ent, compName, fieldName, parsedValue);
                updatedFields.setNode(fieldName, ComponentValueToConfigNode(parsedValue));
            }

            bumpRevision();
            ConfigNode data(ConfigTable{});
            data.setString("entity", positional[2]);
            data.setString("component", compName);
            data.setNode("updatedFields", std::move(updatedFields));
            data.setNode("entitySnapshot", buildEntityInspectNode(*reg, ent, rIdx));
            return makeSuccess("component set", std::move(data));
        }

        return makeError("component " + sub, "unknown-component-subcommand",
                         "未知 component 子指令: '" + sub + "'",
                         "component list | component schema <Comp> | component get <ent> <Comp> | component add <ent> <Comp> | component remove <ent> <Comp> | component set <ent> <Comp> <field>=<val>",
                         "运行 'EditorCli help component --json' 查看详情");
    }

    CliCommandOutcome EditorCliApplication::handlePluginCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& /*flags*/) {

        std::string sub = positional.size() > 1 ? positional[1] : "list";
        auto* engine = Runtime::Engine::getInstance();
        auto& runtime = Runtime::Runtime::instance();
        Runtime::PluginManager* pm = engine->host->pluginManager.get();
        if (!pm) {
            return makeError("plugin " + sub, "plugin-manager-unavailable",
                             "PluginManager 未初始化", "有效 PluginManager", "检查引擎初始化");
        }

        if (sub == "list") {
            return makeSuccess("plugin list", buildPluginListNode());
        }

        if (sub == "inspect") {
            if (positional.size() < 3) {
                return makeError("plugin inspect", "missing-plugin-name",
                                 "缺少插件名称", "plugin inspect <PluginName>",
                                 "运行 'plugin list --json' 查看所有插件名称");
            }
            const auto* p = pm->getPluginEntry(positional[2]);
            if (!p) {
                return makeError("plugin inspect", "plugin-not-found",
                                 "未找到插件: '" + positional[2] + "'",
                                 "已发现的插件名",
                                 "运行 'plugin list --json'");
            }
            ConfigNode node(ConfigTable{});
            node.setString("name", p->name);
            node.setString("version", p->plugin ? p->plugin->getVersion() : "");
            node.setString("dllPath", p->sourcePath.empty() ? p->dynamicLibraryPath : p->sourcePath);
            node.setString("loadedDllPath", p->shadowLibraryPath.empty() ? p->dynamicLibraryPath : p->shadowLibraryPath);
            node.setString("state", PluginStateToString(p->state));
            node.setInt("reloadGeneration", static_cast<int64_t>(p->reloadCount));
            node.setString("lastError", p->errorMessage);
            node.setStringArray("registeredSystems", p->registeredSystems);
            node.setStringArray("registeredComponents", p->registeredComponents);
            return makeSuccess("plugin inspect", std::move(node));
        }

        if (sub == "load") {
            if (positional.size() < 3) {
                return makeError("plugin load", "missing-plugin-name",
                                 "缺少要加载的插件名称或路径",
                                 "plugin load <PluginNameOrPath>",
                                 "运行 'plugin list --json' 查看可加载插件");
            }
            Status st = runtime.loadPlugin(positional[2]);
            if (!st.ok()) {
                return makeError("plugin load", "plugin-load-failed",
                                 std::string(st.message()),
                                 "有效插件名称、DLL 路径或脚本插件路径",
                                 "检查插件编译输出与依赖项");
            }
            bumpRevision();
            return makeSuccess("plugin load", buildPluginListNode());
        }

        if (sub == "unload") {
            if (positional.size() < 3) {
                return makeError("plugin unload", "missing-plugin-name",
                                 "缺少要卸载的插件名称",
                                 "plugin unload <PluginName>",
                                 "运行 'plugin list --json'");
            }
            Status st = runtime.unloadPlugin(positional[2]);
            if (!st.ok()) {
                return makeError("plugin unload", "plugin-unload-failed",
                                 std::string(st.message()),
                                 "已加载的插件名称",
                                 "运行 'plugin list --json' 确认插件当前状态");
            }
            bumpRevision();
            return makeSuccess("plugin unload", buildPluginListNode());
        }

        if (sub == "reload") {
            if (positional.size() < 3) {
                for (const auto& name : pm->getAllPluginNames()) {
                    const auto* p = pm->getPluginEntry(name);
                    if (p && p->state == Runtime::PluginState::Initialized && (p->isDynamic || p->isScript)) {
                        Status st = runtime.reloadPlugin(name);
                        if (!st.ok()) {
                            return makeError("plugin reload", "plugin-reload-failed",
                                             std::string(st.message()),
                                             "插件可正常卸载并从 Shadow Copy 重新加载",
                                             "检查插件编译状态");
                        }
                    }
                }
            } else {
                Status st = runtime.reloadPlugin(positional[2]);
                if (!st.ok()) {
                    return makeError("plugin reload", "plugin-reload-failed",
                                     std::string(st.message()),
                                     "已发现的插件名称",
                                     "运行 'plugin list --json' 查看插件名称");
                }
            }
            bumpRevision();
            return makeSuccess("plugin reload", buildPluginListNode());
        }

        return makeError("plugin " + sub, "unknown-plugin-subcommand",
                         "未知 plugin 子指令: '" + sub + "'",
                         "plugin list | plugin inspect <name> | plugin load <name|path> | plugin unload <name> | plugin reload [<name>]",
                         "运行 'EditorCli help plugin --json' 查看详情");
    }

    CliCommandOutcome EditorCliApplication::handleUserCodeCommand(
        const std::vector<std::string>& positional,
        const std::unordered_map<std::string, std::string>& /*flags*/) {

        std::string sub = positional.size() > 1 ? positional[1] : "list";
        auto* engine = Runtime::Engine::getInstance();
        auto& runtime = Runtime::Runtime::instance();
        Runtime::UserCodeManager* ucm = engine->host->userCodeManager.get();
        if (!ucm) {
            return makeError("usercode " + sub, "usercode-manager-unavailable",
                             "UserCodeManager 未初始化", "有效 UserCodeManager", "检查引擎初始化");
        }

        if (sub == "list") {
            return makeSuccess("usercode list", buildUserCodeListNode());
        }

        if (sub == "inspect") {
            std::string target = positional.size() > 2 ? positional[2] : "UserCode";
            const auto* m = ucm->getUserCodeEntry(target);
            if (!m) {
                auto allNames = ucm->getAllUserCodeNames();
                if (!allNames.empty()) {
                    m = ucm->getUserCodeEntry(allNames.front());
                }
            }
            if (!m) {
                return makeError("usercode inspect", "usercode-not-found",
                                 "未找到 UserCode 模块: '" + target + "'",
                                 "已发现的 UserCode 模块名",
                                 "运行 'usercode list --json' 查看已发现模块");
            }
            ConfigNode node(ConfigTable{});
            node.setString("name", m->name);
            node.setString("version", m->userCode ? m->userCode->getVersion() : "");
            node.setString("dllPath", m->sourcePath.empty() ? m->modulePath : m->sourcePath);
            node.setString("loadedDllPath", m->shadowLibraryPath.empty() ? m->modulePath : m->shadowLibraryPath);
            node.setString("state", UserCodeStateToString(m->state));
            node.setInt("reloadGeneration", static_cast<int64_t>(m->reloadCount));
            node.setString("lastError", m->errorMessage);
            node.setStringArray("registeredSystems", m->registeredSystems);
            node.setStringArray("registeredComponents", m->registeredComponents);
            return makeSuccess("usercode inspect", std::move(node));
        }

        if (sub == "load") {
            std::string target = positional.size() > 2 ? positional[2] : "UserCode";
            Status st = runtime.loadUserCode(target);
            if (!st.ok()) {
                return makeError("usercode load", "usercode-load-failed",
                                 std::string(st.message()),
                                 "已编译的 UserCode.dll",
                                 "先构建 UserCode 目标后再执行 'usercode load'");
            }
            bumpRevision();
            return makeSuccess("usercode load", buildUserCodeListNode());
        }

        if (sub == "unload") {
            std::string target = positional.size() > 2 ? positional[2] : "UserCode";
            Status st = ucm->unloadUserCode(target, engine);
            if (!st.ok()) {
                return makeError("usercode unload", "usercode-unload-failed",
                                 std::string(st.message()),
                                 "处于 Initialized 状态的 UserCode 模块",
                                 "运行 'usercode list --json' 查看模块状态");
            }
            bumpRevision();
            return makeSuccess("usercode unload", buildUserCodeListNode());
        }

        if (sub == "reload") {
            if (positional.size() > 2) {
                Status st = runtime.reloadUserCode(positional[2]);
                if (!st.ok()) {
                    return makeError("usercode reload", "usercode-reload-failed",
                                     std::string(st.message()),
                                     "有效 UserCode 模块名且 DLL 可正常加载",
                                     "运行 'usercode list --json' 查看模块名称");
                }
            } else {
                Status st = runtime.reloadUserCode("");
                if (!st.ok()) {
                    return makeError("usercode reload", "usercode-reload-failed",
                                     std::string(st.message()),
                                     "UserCode.dll 编译通过",
                                     "检查 UserCode 编译日志");
                }
            }
            bumpRevision();
            return makeSuccess("usercode reload", buildUserCodeListNode());
        }

        return makeError("usercode " + sub, "unknown-usercode-subcommand",
                         "未知 usercode 子指令: '" + sub + "'",
                         "usercode list | usercode inspect [<name>] | usercode load [<name>] | usercode unload [<name>] | usercode reload [<name>]",
                         "运行 'EditorCli help usercode --json' 查看详情");
    }

    ConfigNode EditorCliApplication::buildCommandTreeNode(const std::string& filterDomain) const {
        ConfigNode root(ConfigTable{});
        root.setString("cli", "MQEngine EditorCli (AI-Native Workflow & Hot-Reload Host)");
        root.setString("version", "1.0.0");
        root.setInt("revision", static_cast<int64_t>(m_revision));

        ConfigArray globalFlags{
            ConfigNode("--json"),
            ConfigNode("--detail"),
            ConfigNode("--revision=<uint64>"),
            ConfigNode("--project=<path>"),
            ConfigNode("--scene=<nameOrUuid>"),
            ConfigNode("--cmd=\"<cmd1>;<cmd2>\""),
            ConfigNode("--script=<path>"),
            ConfigNode("--json-out=<path>"),
            ConfigNode("--mcp")
        };
        SetNodeArray(root, "globalFlags", std::move(globalFlags));

        auto makeCmdEntry = [](const char* usage, const char* summary) {
            ConfigNode c(ConfigTable{});
            c.setString("usage", usage);
            c.setString("summary", summary);
            return c;
        };

        ConfigNode domainsNode(ConfigTable{});

        auto addDomainIfMatch = [&filterDomain, &domainsNode](const std::string& name, ConfigNode domainObj) {
            if (filterDomain.empty() || filterDomain == name) {
                domainsNode.setNode(name, std::move(domainObj));
            }
        };

        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "工程健康检查与全局元数据自省");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("project check [--json]", "验证工程配置、场景列表、已注册组件与热重载开关"),
                makeCmdEntry("project info [--json]", "输出工程路径、名称及子系统概览")
            });
            addDomainIfMatch("project", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "AI 闭环运行时控制、状态快照、搜索、相机聚焦、帧推进、热重载与验证");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("dev status [--json]", "返回引擎运行态、当前 revision、已加载插件/UserCode 与场景实体概览"),
                makeCmdEntry("dev verify [--json]", "执行场景与模块健康诊断，验证所有插件与 UserCode 无错误"),
                makeCmdEntry("dev find [<pattern>] [--component=<Comp>] [--limit=<N>] [--json]", "按名称/ID/组件过滤搜索场景中的实体"),
                makeCmdEntry("dev focus <entity> [--distance=<d>] [--json]", "将观察视角聚焦到目标实体坐标"),
                makeCmdEntry("dev camera [--pos=x,y,z] [--entity=<target>] [--json]", "设置或查询观察相机位置与朝向目标"),
                makeCmdEntry("dev advance [--frames=<N>] [--dt=<sec>] [--json]", "确定性推进 N 帧 ECS 逻辑 Tick（用于测试 UserCode 系统与物理行为）"),
                makeCmdEntry("dev play [--json]", "切换至 Play 运行态"),
                makeCmdEntry("dev stop [--json]", "停止 Play 运行态"),
                makeCmdEntry("dev reload [--target=all|usercode|plugins] [--json]", "一键热重载 UserCode 与插件并保留 ECS 组件数据")
            });
            addDomainIfMatch("dev", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "场景枚举、打开、新建与保存");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("scene list [--json]", "列出当前工程中的所有场景名称"),
                makeCmdEntry("scene info [--json]", "查看当前活动场景的 UUID 与实体总数"),
                makeCmdEntry("scene open <nameOrUuid> [--json]", "打开指定场景并同步 ECS Registry"),
                makeCmdEntry("scene new <name> [--json]", "创建并打开新场景"),
                makeCmdEntry("scene save [--json]", "将当前活动场景持久化保存到磁盘")
            });
            addDomainIfMatch("scene", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "ECS 实体生命周期与完整组件树检视");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("entity list [--json]", "列出当前场景中的所有实体及其挂载组件"),
                makeCmdEntry("entity inspect <name|uuid|id> [--json]", "深度检视单个实体的所有反射组件与字段值"),
                makeCmdEntry("entity spawn [--name=<Name>] [--pos=x,y,z] [--rot=x,y,z] [--scale=x,y,z] [--components=CompA,CompB] [--json]", "生成新实体并自动挂载指定组件"),
                makeCmdEntry("entity despawn <name|uuid|id> [--revision=<rev>] [--json]", "销毁指定实体"),
                makeCmdEntry("entity rename <name|uuid|id> <newName> [--revision=<rev>] [--json]", "修改实体名称")
            });
            addDomainIfMatch("entity", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "基于 Boost.PFR 零宏反射的组件 Schema 自发现与字段读写");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("component list [--json]", "列出所有已注册的反射组件及其字段定义"),
                makeCmdEntry("component schema <ComponentName> [--json]", "查询单个组件的字段名称与类型 Schema"),
                makeCmdEntry("component get <entity> <ComponentName> [--json]", "读取实体上指定组件的全部字段值"),
                makeCmdEntry("component add <entity> <ComponentName> [--revision=<rev>] [--json]", "为实体动态挂载组件"),
                makeCmdEntry("component remove <entity> <ComponentName> [--revision=<rev>] [--json]", "移除实体上的指定组件"),
                makeCmdEntry("component set <entity> <ComponentName> <field>=<val> [...] [--revision=<rev>] [--json]", "修改实体组件的单个或多个字段值")
            });
            addDomainIfMatch("component", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "C++ 插件动态热加载、卸载、Shadow-Copy 热重载与自省");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("plugin list [--json]", "列出所有插件及其状态、热重载代数与注册项"),
                makeCmdEntry("plugin inspect <PluginName> [--json]", "查看单个插件的详细元数据与已注册系统/组件"),
                makeCmdEntry("plugin load <PluginNameOrDir> [--json]", "热加载指定插件"),
                makeCmdEntry("plugin unload <PluginName> [--json]", "安全卸载指定插件并回收其 ECS 组件池与系统"),
                makeCmdEntry("plugin reload [<PluginName>] [--json]", "Shadow-Copy 热重载指定插件（或全部已加载插件）并自动恢复 ECS 组件状态")
            });
            addDomainIfMatch("plugin", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "UserCode 游戏业务逻辑 DLL 热更新、状态快照保留与检视");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("usercode list [--json]", "列出所有 UserCode 模块及其状态与热重载代数"),
                makeCmdEntry("usercode inspect [<ModuleName>] [--json]", "查看 UserCode 模块详情及已注册的自定义组件与系统"),
                makeCmdEntry("usercode load [<ModuleName>] [--json]", "加载 UserCode 模块"),
                makeCmdEntry("usercode unload [<ModuleName>] [--json]", "卸载 UserCode 模块并清理自定义类型池"),
                makeCmdEntry("usercode reload [<ModuleName>] [--json]", "热更新 UserCode.dll（自动快照并在新 DLL 中无损恢复自定义组件字段数据）")
            });
            addDomainIfMatch("usercode", std::move(d));
        }
        {
            ConfigNode d(ConfigTable{});
            d.setString("summary", "Model Context Protocol (MCP) JSON-RPC 2.0 Stdio 服务端模式");
            SetNodeArray(d, "commands", ConfigArray{
                makeCmdEntry("mcp", "启动 MCP JSON-RPC 2.0 标准输入输出长连接服务，向 AI Agent 暴露 mqengine_exec / mqengine_status / mqengine_reload 工具")
            });
            addDomainIfMatch("mcp", std::move(d));
        }

        root.setNode("domains", std::move(domainsNode));
        return root;
    }

    ConfigNode EditorCliApplication::buildDevStatusNode() const {
        auto* engine = Runtime::Engine::getInstance();
        auto& runtime = Runtime::Runtime::instance();
        ConfigNode status(ConfigTable{});
        status.setString("projectName", engine->foundation->getCurrentConfig().projectName);
        status.setString("projectPath", engine->foundation->getProjectDirectory());
        status.setBool("headless", runtime.isHeadless());
        status.setBool("isPlaying", runtime.isPlaying());
        status.setInt("revision", static_cast<int64_t>(m_revision));
        status.setInt("frame", static_cast<int64_t>(m_tickCount));

        int64_t entityCount = 0;
        if (DataManager* dm = engine->scene->dataManager.get()) {
            for (entt::registry* reg : dm->currentRegistries()) {
                if (!reg) {
                    continue;
                }
                const auto& storage = reg->storage<entt::entity>();
                for (entt::entity ent : storage) {
                    if (reg->valid(ent)) {
                        ++entityCount;
                    }
                }
            }
        }
        status.setInt("entityCount", entityCount);

        if (ComponentReflection* refl = engine->foundation->componentReflection.get()) {
            status.setStringArray("registeredComponents", refl->getRegisteredComponentNames());
        }

        status.setNode("plugins", buildPluginListNode());
        status.setNode("userCode", buildUserCodeListNode());
        return status;
    }

    ConfigNode EditorCliApplication::buildPluginListNode() const {
        auto* engine = Runtime::Engine::getInstance();
        ConfigNode out(ConfigTable{});
        out.setBool("autoHotReload", Runtime::Runtime::instance().isAutoHotReloadEnabled());
        ConfigArray arr;
        if (auto* pm = engine->host->pluginManager.get()) {
            for (const auto& name : pm->getAllPluginNames()) {
                const auto* p = pm->getPluginEntry(name);
                if (!p) {
                    continue;
                }
                ConfigNode item(ConfigTable{});
                item.setString("name", p->name);
                item.setString("version", p->plugin ? p->plugin->getVersion() : "");
                item.setString("state", PluginStateToString(p->state));
                item.setInt("reloadGeneration", static_cast<int64_t>(p->reloadCount));
                item.setString("dllPath", p->sourcePath.empty() ? p->dynamicLibraryPath : p->sourcePath);
                item.setString("loadedDllPath", p->shadowLibraryPath.empty() ? p->dynamicLibraryPath : p->shadowLibraryPath);
                item.setString("lastError", p->errorMessage);
                item.setStringArray("registeredSystems", p->registeredSystems);
                item.setStringArray("registeredComponents", p->registeredComponents);
                arr.push_back(std::move(item));
            }
        }
        out.setInt("count", static_cast<int64_t>(arr.size()));
        SetNodeArray(out, "items", std::move(arr));
        return out;
    }

    ConfigNode EditorCliApplication::buildUserCodeListNode() const {
        auto* engine = Runtime::Engine::getInstance();
        ConfigNode out(ConfigTable{});
        out.setBool("autoHotReload", Runtime::Runtime::instance().isAutoHotReloadEnabled());
        ConfigArray arr;
        if (auto* ucm = engine->host->userCodeManager.get()) {
            for (const auto& name : ucm->getAllUserCodeNames()) {
                const auto* m = ucm->getUserCodeEntry(name);
                if (!m) {
                    continue;
                }
                ConfigNode item(ConfigTable{});
                item.setString("name", m->name);
                item.setString("version", m->userCode ? m->userCode->getVersion() : "");
                item.setString("state", UserCodeStateToString(m->state));
                item.setInt("reloadGeneration", static_cast<int64_t>(m->reloadCount));
                item.setString("dllPath", m->sourcePath.empty() ? m->modulePath : m->sourcePath);
                item.setString("loadedDllPath", m->shadowLibraryPath.empty() ? m->modulePath : m->shadowLibraryPath);
                item.setString("lastError", m->errorMessage);
                item.setStringArray("registeredSystems", m->registeredSystems);
                item.setStringArray("registeredComponents", m->registeredComponents);
                arr.push_back(std::move(item));
            }
        }
        out.setInt("count", static_cast<int64_t>(arr.size()));
        SetNodeArray(out, "items", std::move(arr));
        return out;
    }

    ConfigNode EditorCliApplication::buildComponentSchemaNode(const ComponentInfo& info) const {
        ConfigNode schema(ConfigTable{});
        schema.setString("name", info.name);
        schema.setInt("typeId", static_cast<int64_t>(info.typeId));
        ConfigArray fieldsArr;
        for (const auto& f : info.fields) {
            ConfigNode fNode(ConfigTable{});
            fNode.setString("name", f.name);
            fNode.setString("type", f.type);
            fieldsArr.push_back(std::move(fNode));
        }
        schema.setInt("fieldCount", static_cast<int64_t>(fieldsArr.size()));
        SetNodeArray(schema, "fields", std::move(fieldsArr));
        return schema;
    }

    ConfigNode EditorCliApplication::buildEntityInspectNode(
        entt::registry& reg, entt::entity ent, size_t regIndex) const {

        ConfigNode node(ConfigTable{});
        uint32_t rawId = static_cast<uint32_t>(entt::to_integral(ent));
        node.setInt("id", static_cast<int64_t>(rawId));
        node.setInt("registryIndex", static_cast<int64_t>(regIndex));

        if (reg.all_of<NameTag>(ent)) {
            const auto& tag = reg.get<NameTag>(ent);
            node.setString("name", tag.name);
            node.setString("uuid", tag.uuid);
        } else {
            node.setString("name", "");
            node.setString("uuid", "");
        }

        ConfigNode compsNode(ConfigTable{});
        ConfigArray compNamesArr;
        ComponentReflection* refl = Runtime::Engine::getInstance()->foundation->componentReflection.get();
        if (refl) {
            std::vector<std::string> attachedNames;
            for (const auto& [compName, _] : refl->getComponentInfos()) {
                if (refl->hasComponent(reg, ent, compName)) {
                    attachedNames.push_back(compName);
                }
            }
            std::sort(attachedNames.begin(), attachedNames.end());

            for (const auto& compName : attachedNames) {
                compNamesArr.push_back(ConfigNode(compName));
                const auto* info = refl->findComponent(compName);
                if (!info) {
                    continue;
                }
                ConfigNode cFields(ConfigTable{});
                for (const auto& f : info->fields) {
                    ComponentValue val = refl->getComponentField(reg, ent, compName, f.name);
                    cFields.setNode(f.name, ComponentValueToConfigNode(val));
                }
                compsNode.setNode(compName, std::move(cFields));
            }
        }
        SetNodeArray(node, "componentNames", std::move(compNamesArr));
        node.setNode("components", std::move(compsNode));
        return node;
    }

    bool EditorCliApplication::resolveEntityInCurrentScene(
        const std::string& identifier,
        entt::registry*& outReg,
        entt::entity& outEntity,
        size_t& outRegIndex) const {

        outReg = nullptr;
        outEntity = entt::null;
        outRegIndex = 0;

        DataManager* dm = Runtime::Engine::getInstance()->scene->getDataManager();
        if (!dm) {
            return false;
        }

        uint32_t numericId = 0;
        bool isNumeric = absl::SimpleAtoi(identifier, &numericId);

        const auto regs = dm->currentRegistries();
        for (size_t rIdx = 0; rIdx < regs.size(); ++rIdx) {
            entt::registry* reg = regs[rIdx];
            if (!reg) {
                continue;
            }
            const auto& storage = reg->storage<entt::entity>();
            for (entt::entity ent : storage) {
                if (!reg->valid(ent)) {
                    continue;
                }
                if (reg->all_of<NameTag>(ent)) {
                    const auto& tag = reg->get<NameTag>(ent);
                    if (tag.name == identifier || tag.uuid == identifier) {
                        outReg = reg;
                        outEntity = ent;
                        outRegIndex = rIdx;
                        return true;
                    }
                }
                if (isNumeric && static_cast<uint32_t>(entt::to_integral(ent)) == numericId) {
                    outReg = reg;
                    outEntity = ent;
                    outRegIndex = rIdx;
                    return true;
                }
            }
        }
        return false;
    }

    CliCommandOutcome EditorCliApplication::makeSuccess(const std::string& cmd, ConfigNode data) const {
        CliCommandOutcome out;
        out.ok = true;
        out.command = cmd;
        out.revision = m_revision;
        out.frame = m_tickCount;
        out.data = std::move(data);
        return out;
    }

    CliCommandOutcome EditorCliApplication::makeError(
        const std::string& cmd,
        const std::string& code,
        const std::string& message,
        const std::string& expected,
        const std::string& hint) const {

        CliCommandOutcome out;
        out.ok = false;
        out.command = cmd;
        out.revision = m_revision;
        out.frame = m_tickCount;
        out.error.code = code;
        out.error.message = message;
        out.error.expected = expected;
        out.error.hint = hint;
        return out;
    }

    uint64_t EditorCliApplication::bumpRevision() {
        return ++m_revision;
    }

    static Runtime::Application* CreateEditorCliApplication() {
        return new EditorCliApplication();
    }

    namespace {
        class EditorCliApplicationRegistrar {
        public:
            EditorCliApplicationRegistrar() {
                Runtime::Runtime::RegisterApplicationFactory(CreateEditorCliApplication);
            }
        };
        static EditorCliApplicationRegistrar g_editorCliRegistrar;
    } // namespace

} // namespace MQEngine
