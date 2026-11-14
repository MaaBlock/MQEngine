#include <MQEngine/resource/config/CommandLineParser.h>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            class CallbackCommandLineHandler : public ICommandLineHandler
            {
            public:
                explicit CallbackCommandLineHandler(CommandLineParser::HandleCallback callback)
                    : m_callback(std::move(callback))
                {
                }

            protected:
                bool onHandle(const CommandLineArg& arg) override
                {
                    return m_callback ? m_callback(arg) : false;
                }

            private:
                CommandLineParser::HandleCallback m_callback;
            };

            bool isBoolLiteral(std::string_view val) noexcept
            {
                return val == "true" || val == "false" ||
                       val == "1" || val == "0" ||
                       val == "yes" || val == "no" ||
                       val == "on" || val == "off";
            }

            std::string stripOuterQuotesAndWhitespace(std::string_view raw)
            {
                while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) {
                    raw.remove_prefix(1);
                }
                while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) {
                    raw.remove_suffix(1);
                }
                if (raw.size() >= 2 &&
                    ((raw.front() == '"' && raw.back() == '"') ||
                     (raw.front() == '\'' && raw.back() == '\''))) {
                    raw.remove_prefix(1);
                    raw.remove_suffix(1);
                }
                return std::string(raw);
            }
        } // namespace

        bool CommandLineArg::matchesKey(std::string_view expectedKey) const noexcept
        {
            if (isPositional || key.empty() || expectedKey.empty()) {
                return false;
            }
            if (key == expectedKey) {
                return true;
            }
            return CommandLineParser::normalizeKey(key) == CommandLineParser::normalizeKey(expectedKey);
        }

        bool CommandLineArg::asBool(bool defaultValue) const noexcept
        {
            if (value.empty() || value == "true" || value == "1" || value == "yes" || value == "on") {
                return true;
            }
            if (value == "false" || value == "0" || value == "no" || value == "off") {
                return false;
            }
            return defaultValue;
        }

        int32_t CommandLineArg::asInt(int32_t defaultValue) const noexcept
        {
            if (value.empty()) {
                return defaultValue;
            }
            int32_t parsed = defaultValue;
            if (absl::SimpleAtoi(value, &parsed)) {
                return parsed;
            }
            return defaultValue;
        }

        std::string CommandLineParser::normalizeKey(std::string_view key)
        {
            std::string normalized(key);
            std::replace(normalized.begin(), normalized.end(), '-', '_');
            return normalized;
        }

        void CommandLineParser::addHandler(UniquePtr<ICommandLineHandler> handler)
        {
            if (!handler) {
                return;
            }
            ICommandLineHandler* rawPtr = handler.get();
            m_ownedHandlers.push_back(std::move(handler));
            appendHandlerToChain(rawPtr);
        }

        void CommandLineParser::addExternalHandler(ICommandLineHandler* handler)
        {
            if (!handler) {
                return;
            }
            if (std::find(m_chainNodes.begin(), m_chainNodes.end(), handler) != m_chainNodes.end()) {
                return;
            }
            appendHandlerToChain(handler);
        }

        void CommandLineParser::registerHandler(HandleCallback callback)
        {
            if (!callback) {
                return;
            }
            addHandler(makeUnique<CallbackCommandLineHandler>(std::move(callback)));
        }

        void CommandLineParser::clearHandlers()
        {
            for (ICommandLineHandler* node : m_chainNodes) {
                if (node) {
                    node->setNext(nullptr);
                }
            }
            m_chainNodes.clear();
            m_ownedHandlers.clear();
            m_headHandler = nullptr;
            m_tailHandler = nullptr;
        }

        void CommandLineParser::appendHandlerToChain(ICommandLineHandler* handler)
        {
            handler->setNext(nullptr);
            if (!m_headHandler) {
                m_headHandler = handler;
                m_tailHandler = handler;
            } else {
                m_tailHandler->setNext(handler);
                m_tailHandler = handler;
            }
            m_chainNodes.push_back(handler);
        }

        void CommandLineParser::setArgs(int argc, char* argv[])
        {
            std::vector<std::string> args;
            if (argc > 0 && argv != nullptr) {
                args.reserve(static_cast<size_t>(argc));
                for (int i = 0; i < argc; ++i) {
                    if (argv[i] != nullptr) {
                        args.emplace_back(argv[i]);
                    }
                }
            }
            setArgs(args);
        }

        void CommandLineParser::setArgs(const std::vector<std::string>& args)
        {
            m_rawArgs = args;
        }

        void CommandLineParser::parse(int argc, char* argv[])
        {
            setArgs(argc, argv);
            parse();
        }

        void CommandLineParser::parse(const std::vector<std::string>& args)
        {
            setArgs(args);
            parse();
        }

        void CommandLineParser::parse()
        {
            tokenizeArgs(m_rawArgs, m_parsedArgs);
            dispatchToHandlers();
        }

        void CommandLineParser::tokenizeArgs(const std::vector<std::string>& rawArgs,
                                             std::vector<CommandLineArg>& outArgs) const
        {
            outArgs.clear();
            for (size_t i = 1; i < rawArgs.size(); ++i) {
                const std::string& token = rawArgs[i];
                if (token.empty()) {
                    continue;
                }

                if (token == "--") {
                    for (size_t j = i + 1; j < rawArgs.size(); ++j) {
                        CommandLineArg posArg;
                        posArg.rawToken = rawArgs[j];
                        posArg.value = stripOuterQuotesAndWhitespace(rawArgs[j]);
                        posArg.isPositional = true;
                        outArgs.push_back(std::move(posArg));
                    }
                    break;
                }

                size_t prefixLen = 0;
                if (token.rfind("--", 0) == 0) {
                    prefixLen = 2;
                } else if (token.rfind("-", 0) == 0 && token.size() > 1) {
                    prefixLen = 1;
                }

                if (prefixLen == 0) {
                    CommandLineArg posArg;
                    posArg.rawToken = token;
                    posArg.value = stripOuterQuotesAndWhitespace(token);
                    posArg.isPositional = true;
                    outArgs.push_back(std::move(posArg));
                    continue;
                }

                parseSingleOptionToken(rawArgs, i, prefixLen, outArgs);
            }
        }

        void CommandLineParser::parseSingleOptionToken(const std::vector<std::string>& rawArgs,
                                                       size_t& index,
                                                       size_t prefixLen,
                                                       std::vector<CommandLineArg>& outArgs) const
        {
            const std::string& token = rawArgs[index];
            const std::string body = token.substr(prefixLen);
            const size_t eqPos = body.find('=');

            if (eqPos != std::string::npos) {
                CommandLineArg optArg;
                optArg.rawToken = token;
                optArg.key = body.substr(0, eqPos);
                optArg.value = stripOuterQuotesAndWhitespace(body.substr(eqPos + 1));
                optArg.hasExplicitValue = true;
                outArgs.push_back(std::move(optArg));
                return;
            }

            if ((index + 1 < rawArgs.size()) && !rawArgs[index + 1].empty() &&
                rawArgs[index + 1].rfind("-", 0) != 0) {
                const std::string& nextToken = rawArgs[index + 1];
                const bool isKnownFlag = (body == "headless" || body == "no-window");
                if (isBoolLiteral(nextToken) || !isKnownFlag) {
                    CommandLineArg optArg;
                    optArg.rawToken = token;
                    optArg.key = body;
                    optArg.value = stripOuterQuotesAndWhitespace(nextToken);
                    optArg.hasExplicitValue = true;
                    ++index;
                    outArgs.push_back(std::move(optArg));
                    return;
                }
            }

            CommandLineArg flagArg;
            flagArg.rawToken = token;
            flagArg.key = body;
            flagArg.value = "true";
            flagArg.hasExplicitValue = false;
            outArgs.push_back(std::move(flagArg));
        }

        void CommandLineParser::dispatchToHandlers() const
        {
            if (!m_headHandler) {
                return;
            }
            for (const auto& arg : m_parsedArgs) {
                (void)m_headHandler->handle(arg);
            }
        }

    } // namespace Resource
} // namespace MQEngine
