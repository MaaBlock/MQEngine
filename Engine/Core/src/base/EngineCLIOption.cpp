#include "EngineCLIOption.h"
#include <algorithm>
#include <cctype>
#include <iostream>

namespace MQEngine
{
    namespace
    {
        /**
         * @brief 基于函数回调的职责链节点实现
         */
        class CallbackCLIOptionHandler : public ICLIOptionHandler
        {
        public:
            CallbackCLIOptionHandler(std::string name,
                                     EngineCLIOption::HandleCallback onHandle,
                                     EngineCLIOption::CompleteCallback onComplete)
                : m_name(std::move(name)),
                  m_onHandle(std::move(onHandle)),
                  m_onComplete(std::move(onComplete))
            {
            }

            std::string getHandlerName() const override { return m_name; }

        protected:
            bool onHandleOption(const CLIOptionItem& item, EngineCLIOption& cli) override
            {
                return m_onHandle ? m_onHandle(item, cli) : false;
            }

            void onParseComplete(EngineCLIOption& cli) override
            {
                if (m_onComplete) {
                    m_onComplete(cli);
                }
            }

        private:
            std::string m_name;
            EngineCLIOption::HandleCallback m_onHandle;
            EngineCLIOption::CompleteCallback m_onComplete;
        };

        /**
         * @brief Abseil 全局 Flag 职责链节点：自动匹配 ENGINE_CLI_OPTION 声明的全局参数
         */
        class AbseilFlagCLIHandler : public ICLIOptionHandler
        {
        public:
            std::string getHandlerName() const override { return "AbseilFlagCLIHandler"; }

        protected:
            bool onHandleOption(const CLIOptionItem& item, EngineCLIOption& cli) override
            {
                if (item.isPositional || item.key.empty()) {
                    return false;
                }

                auto* flag = FindCommandLineFlag(item.key);
                if (!flag) {
                    const std::string normalized = EngineCLIOption::normalizeKeyToUnderscore(item.key);
                    if (normalized != item.key) {
                        flag = FindCommandLineFlag(normalized);
                    }
                }
                if (!flag) {
                    return false;
                }

                std::string error;
                if (flag->ParseFrom(item.value, &error)) {
                    cli.storeExtraOption(item.key, item.value);
                    return true;
                }
                return false;
            }
        };

        /**
         * @brief 职责链末端兜底节点：收集所有未被前置模块消费的扩展选项与位置参数
         */
        class FallbackStoreCLIHandler : public ICLIOptionHandler
        {
        public:
            std::string getHandlerName() const override { return "FallbackStoreCLIHandler"; }

        protected:
            bool onHandleOption(const CLIOptionItem& item, EngineCLIOption& cli) override
            {
                if (item.isPositional) {
                    cli.storePositionalArg(item.rawToken);
                    return true;
                }
                if (!item.key.empty()) {
                    cli.storeExtraOption(item.key, item.value);
                    return true;
                }
                return false;
            }
        };

        bool parseBoolString(const std::string& val, bool defaultVal)
        {
            if (val.empty() || val == "true" || val == "1" || val == "yes" || val == "on") {
                return true;
            }
            if (val == "false" || val == "0" || val == "no" || val == "off") {
                return false;
            }
            return defaultVal;
        }
    } // namespace

    EngineCLIOption::EngineCLIOption()
        : m_abseilHandler(makeUnique<AbseilFlagCLIHandler>()),
          m_fallbackHandler(makeUnique<FallbackStoreCLIHandler>())
    {
        // 预注册标准声明式选项元数据描述
        registerOptionDesc({ .name = "help", .takesValue = false, .description = "显示命令行参数帮助信息" });
        registerOptionDesc({ .name = "h", .takesValue = false, .description = "显示命令行参数帮助信息 (简写)" });
        registerOptionDesc({ .name = "project", .takesValue = true, .description = "指定工程文件路径 (*.mq)" });
        registerOptionDesc({ .name = "p", .takesValue = true, .description = "指定工程文件路径 (简写)" });
        registerOptionDesc({ .name = "headless", .takesValue = false, .description = "无窗口无头模式运行" });
        registerOptionDesc({ .name = "no-window", .takesValue = false, .description = "无窗口运行 (同 --headless)" });
        registerOptionDesc({ .name = "test-frames", .takesValue = true, .description = "自动化测试运行总逻辑帧数" });
        registerOptionDesc({ .name = "test_frames", .takesValue = true, .description = "自动化测试运行总逻辑帧数" });
        registerOptionDesc({ .name = "frames", .takesValue = true, .description = "自动化测试运行总逻辑帧数 (同 --test-frames)" });
        registerOptionDesc({ .name = "screenshot", .takesValue = true, .description = "截屏保存输出路径" });
        registerOptionDesc({ .name = "capture_output", .takesValue = true, .description = "截屏保存输出路径 (同 --screenshot)" });
        registerOptionDesc({ .name = "capture-frame", .takesValue = true, .description = "截屏触发的目标帧号" });
        registerOptionDesc({ .name = "capture_frame", .takesValue = true, .description = "截屏触发的目标帧号" });
        registerOptionDesc({ .name = "capture-target", .takesValue = true, .description = "截屏渲染目标名称" });
        registerOptionDesc({ .name = "capture_target", .takesValue = true, .description = "截屏渲染目标名称" });
        registerOptionDesc({ .name = "local", .takesValue = true, .description = "设置本地化语言代码 (zh_CN / en_US)" });

        rebuildChainLinks();
    }

    EngineCLIOption::~EngineCLIOption() = default;

    void EngineCLIOption::addHandler(UniquePtr<ICLIOptionHandler> handler)
    {
        if (!handler) {
            return;
        }
        ICLIOptionHandler* rawPtr = handler.get();
        m_ownedHandlers.push_back(std::move(handler));
        m_customChainOrder.push_back(rawPtr);
        rebuildChainLinks();
    }

    void EngineCLIOption::addExternalHandler(ICLIOptionHandler* handler)
    {
        if (!handler) {
            return;
        }
        if (std::find(m_customChainOrder.begin(), m_customChainOrder.end(), handler) == m_customChainOrder.end()) {
            m_customChainOrder.push_back(handler);
            rebuildChainLinks();
        }
    }

    void EngineCLIOption::addCallbackHandler(const std::string& name,
                                             HandleCallback onHandle,
                                             CompleteCallback onComplete)
    {
        addHandler(makeUnique<CallbackCLIOptionHandler>(
            name, std::move(onHandle), std::move(onComplete)));
    }

    void EngineCLIOption::registerOptionDesc(const CLIOptionDesc& desc)
    {
        if (desc.name.empty()) {
            return;
        }
        m_optionDescs.push_back(desc);
        m_registeredOptionNames.insert(desc.name);
        m_registeredOptionNames.insert(normalizeKeyToUnderscore(desc.name));
        if (desc.takesValue) {
            registerValueOptionKey(desc.name);
        }
    }

    void EngineCLIOption::printHelp() const
    {
        std::cout << "\nMQEngine 命令行参数列表 (Available CLI Options):\n\n";
        for (const auto& desc : m_optionDescs) {
            std::cout << "  --" << desc.name;
            if (desc.takesValue) {
                std::cout << " <value>";
            }
            std::cout << "\n      " << desc.description << "\n";
        }
        std::cout << std::endl;
    }

    void EngineCLIOption::validateOptions(const std::vector<std::string>& rawArgs)
    {
        std::vector<CLIOptionItem> items;
        tokenizeArgs(rawArgs, items);

        for (const auto& item : items) {
            if (item.isPositional || item.key.empty()) {
                continue;
            }
            if (item.key == "help" || item.key == "h") {
                printHelp();
                std::exit(0);
            }
            const std::string normalized = normalizeKeyToUnderscore(item.key);
            bool isKnown = (m_registeredOptionNames.find(item.key) != m_registeredOptionNames.end()) ||
                           (m_registeredOptionNames.find(normalized) != m_registeredOptionNames.end()) ||
                           (FindCommandLineFlag(item.key) != nullptr) ||
                           (FindCommandLineFlag(normalized) != nullptr);

            if (!isKnown) {
                spdlog::error("未知或未注册的命令行参数: --{}", item.key);
                std::cerr << "错误: 未知或未注册的命令行参数: --" << item.key << "\n"
                          << "输入 --help 查看可用命令行参数列表。" << std::endl;
                std::exit(1);
            }
        }
    }

    void EngineCLIOption::registerValueOptionKey(const std::string& key)
    {
        if (!key.empty()) {
            m_valueOptionKeys.insert(key);
            m_valueOptionKeys.insert(normalizeKeyToUnderscore(key));
        }
    }

    void EngineCLIOption::rebuildChainLinks()
    {
        // 职责链顺序：[各模块/Application 自定义节点...] -> [AbseilFlagCLIHandler] -> [FallbackStoreCLIHandler]
        std::vector<ICLIOptionHandler*> fullChain;
        fullChain.reserve(m_customChainOrder.size() + 2);
        for (ICLIOptionHandler* custom : m_customChainOrder) {
            if (custom) {
                fullChain.push_back(custom);
            }
        }
        if (m_abseilHandler) {
            fullChain.push_back(m_abseilHandler.get());
        }
        if (m_fallbackHandler) {
            fullChain.push_back(m_fallbackHandler.get());
        }

        for (size_t i = 0; i < fullChain.size(); ++i) {
            ICLIOptionHandler* nextNode = (i + 1 < fullChain.size()) ? fullChain[i + 1] : nullptr;
            fullChain[i]->setNext(nextNode);
        }
        m_headHandler = fullChain.empty() ? nullptr : fullChain.front();
    }

    std::string EngineCLIOption::normalizeKeyToUnderscore(const std::string& key)
    {
        std::string result = key;
        std::replace(result.begin(), result.end(), '-', '_');
        return result;
    }

    bool EngineCLIOption::isValueTakingOption(const std::string& key) const
    {
        if (m_valueOptionKeys.find(key) != m_valueOptionKeys.end()) {
            return true;
        }
        const std::string normalized = normalizeKeyToUnderscore(key);
        if (m_valueOptionKeys.find(normalized) != m_valueOptionKeys.end()) {
            return true;
        }
        if (auto* flag = FindCommandLineFlag(key)) {
            return !flag->IsOfType<bool>();
        }
        if (auto* flag = FindCommandLineFlag(normalized)) {
            return !flag->IsOfType<bool>();
        }
        return false;
    }

    void EngineCLIOption::tokenizeArgs(const std::vector<std::string>& args,
                                       std::vector<CLIOptionItem>& outItems) const
    {
        outItems.clear();
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg.empty()) {
                continue;
            }

            if (arg == "--") {
                for (size_t j = i + 1; j < args.size(); ++j) {
                    CLIOptionItem posItem;
                    posItem.rawToken = args[j];
                    posItem.value = args[j];
                    posItem.isPositional = true;
                    outItems.push_back(std::move(posItem));
                }
                break;
            }

            size_t prefixLen = 0;
            if (arg.rfind("--", 0) == 0) {
                prefixLen = 2;
            } else if (arg.rfind("-", 0) == 0 && arg.size() > 1) {
                prefixLen = 1;
            }

            if (prefixLen == 0) {
                CLIOptionItem posItem;
                posItem.rawToken = arg;
                posItem.value = arg;
                posItem.isPositional = true;
                outItems.push_back(std::move(posItem));
                continue;
            }

            std::string body = arg.substr(prefixLen);
            size_t eqPos = body.find('=');
            if (eqPos != std::string::npos) {
                CLIOptionItem optItem;
                optItem.rawToken = arg;
                optItem.key = body.substr(0, eqPos);
                optItem.value = body.substr(eqPos + 1);
                optItem.hasExplicitValue = true;
                outItems.push_back(std::move(optItem));
                continue;
            }

            if (isValueTakingOption(body) && (i + 1 < args.size()) && args[i + 1].rfind("-", 0) != 0) {
                CLIOptionItem optItem;
                optItem.rawToken = arg;
                optItem.key = body;
                optItem.value = args[++i];
                optItem.hasExplicitValue = true;
                outItems.push_back(std::move(optItem));
                continue;
            }

            CLIOptionItem flagItem;
            flagItem.rawToken = arg;
            flagItem.key = body;
            flagItem.value = "true";
            flagItem.hasExplicitValue = false;
            outItems.push_back(std::move(flagItem));
        }
    }

    void EngineCLIOption::parse(int argc, char* argv[])
    {
        std::vector<std::string> args;
        if (argc > 0 && argv) {
            args.reserve(static_cast<size_t>(argc));
            for (int i = 0; i < argc; ++i) {
                if (argv[i]) {
                    args.emplace_back(argv[i]);
                }
            }
        }
        parse(args);
    }

    void EngineCLIOption::parse(const std::vector<std::string>& args)
    {
        m_rawArgs = args;
        m_positionalArgs.clear();
        m_extraOptions.clear();

        validateOptions(m_rawArgs);

        if (!m_rawArgs.empty()) {
            std::vector<std::string> mutableStrings = m_rawArgs;
            std::vector<char*> argvPtrs;
            argvPtrs.reserve(mutableStrings.size());
            for (auto& s : mutableStrings) {
                argvPtrs.push_back(s.data());
            }

            std::vector<char*> positionalArgs;
            std::vector<absl::UnrecognizedFlag> unrecognizedFlags;
            absl::ParseAbseilFlagsOnly(
                static_cast<int>(argvPtrs.size()),
                argvPtrs.data(),
                positionalArgs,
                unrecognizedFlags);
        }

        std::vector<CLIOptionItem> items;
        tokenizeArgs(m_rawArgs, items);

        if (m_headHandler) {
            for (const auto& item : items) {
                (void)m_headHandler->handle(item, *this);
            }
            m_headHandler->notifyParseComplete(*this);
        }
    }

    void EngineCLIOption::storeExtraOption(const std::string& key, const std::string& value)
    {
        if (!key.empty()) {
            m_extraOptions[key] = value;
        }
    }

    void EngineCLIOption::storePositionalArg(const std::string& arg)
    {
        m_positionalArgs.push_back(arg);
    }

    bool EngineCLIOption::hasOption(const std::string& key) const
    {
        if (m_extraOptions.find(key) != m_extraOptions.end()) {
            return true;
        }
        const std::string normalized = normalizeKeyToUnderscore(key);
        if (m_extraOptions.find(normalized) != m_extraOptions.end()) {
            return true;
        }
        if (auto* flag = FindCommandLineFlag(key)) {
            return !flag->CurrentValue().empty() && flag->CurrentValue() != "false" && flag->CurrentValue() != "0";
        }
        if (auto* flag = FindCommandLineFlag(normalized)) {
            return !flag->CurrentValue().empty() && flag->CurrentValue() != "false" && flag->CurrentValue() != "0";
        }
        return false;
    }

    std::string EngineCLIOption::getOption(const std::string& key, const std::string& defaultVal) const
    {
        auto it = m_extraOptions.find(key);
        if (it != m_extraOptions.end()) {
            return it->second;
        }
        const std::string normalized = normalizeKeyToUnderscore(key);
        auto normIt = m_extraOptions.find(normalized);
        if (normIt != m_extraOptions.end()) {
            return normIt->second;
        }
        if (auto* flag = FindCommandLineFlag(key)) {
            std::string val = flag->CurrentValue();
            return val.empty() ? defaultVal : val;
        }
        if (auto* flag = FindCommandLineFlag(normalized)) {
            std::string val = flag->CurrentValue();
            return val.empty() ? defaultVal : val;
        }
        return defaultVal;
    }

    bool EngineCLIOption::getBoolOption(const std::string& key, bool defaultVal) const
    {
        if (!hasOption(key)) {
            return defaultVal;
        }
        return parseBoolString(getOption(key), defaultVal);
    }

    int32_t EngineCLIOption::getIntOption(const std::string& key, int32_t defaultVal) const
    {
        std::string val = getOption(key);
        if (val.empty()) {
            return defaultVal;
        }
        int32_t parsed = defaultVal;
        if (absl::SimpleAtoi(val, &parsed)) {
            return parsed;
        }
        return defaultVal;
    }

    void EngineCLIOption::setOption(const std::string& key, const std::string& value)
    {
        m_extraOptions[key] = value;

        auto* flag = FindCommandLineFlag(key);
        if (!flag) {
            flag = FindCommandLineFlag(normalizeKeyToUnderscore(key));
        }
        if (flag) {
            std::string error;
            (void)flag->ParseFrom(value, &error);
        }
    }

} // namespace MQEngine
