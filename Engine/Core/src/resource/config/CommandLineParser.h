#ifndef MQENGINE_RESOURCE_CONFIG_COMMANDLINEPARSER_H
#define MQENGINE_RESOURCE_CONFIG_COMMANDLINEPARSER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 解析器拆解出的单条命令行参数数据项
         */
        struct ENGINE_API CommandLineArg
        {
            std::string rawToken;
            std::string key;
            std::string value;
            bool isPositional = false;
            bool hasExplicitValue = false;

            bool matchesKey(std::string_view expectedKey) const noexcept;
            bool asBool(bool defaultValue = true) const noexcept;
            int32_t asInt(int32_t defaultValue = 0) const noexcept;
        };

        /**
         * @brief 命令行参数处理职责链节点基类：各上层模块在 registerInfo 阶段将处理器挂载至职责链，依次消费解析后的参数
         */
        class ENGINE_API ICommandLineHandler
        {
        public:
            virtual ~ICommandLineHandler() = default;

            void setNext(ICommandLineHandler* next) noexcept { m_next = next; }
            ICommandLineHandler* getNext() const noexcept { return m_next; }

            bool handle(const CommandLineArg& arg)
            {
                if (onHandle(arg)) {
                    return true;
                }
                if (m_next) {
                    return m_next->handle(arg);
                }
                return false;
            }

        protected:
            virtual bool onHandle(const CommandLineArg& arg) = 0;

        private:
            ICommandLineHandler* m_next = nullptr;
        };

        /**
         * @brief Resource 层命令行解析器：负责将原始 argc/argv 解析为结构化参数项，并交由职责链处理器链式消费
         */
        class ENGINE_API CommandLineParser : public CoreEngineManager<CommandLineParser>
        {
        public:
            using HandleCallback = std::function<bool(const CommandLineArg& arg)>;

            CommandLineParser() = default;
            ~CommandLineParser() = default;

            CommandLineParser(const CommandLineParser&) = delete;
            CommandLineParser& operator=(const CommandLineParser&) = delete;

            void registerInfo() {}

            void addHandler(UniquePtr<ICommandLineHandler> handler);
            void addExternalHandler(ICommandLineHandler* handler);
            void registerHandler(HandleCallback callback);
            void clearHandlers();

            void setArgs(int argc, char* argv[]);
            void setArgs(const std::vector<std::string>& args);

            void parse(int argc, char* argv[]);
            void parse(const std::vector<std::string>& args);
            void parse();

            const std::vector<std::string>& getRawArgs() const noexcept { return m_rawArgs; }
            const std::vector<CommandLineArg>& getParsedArgs() const noexcept { return m_parsedArgs; }
            ICommandLineHandler* getHeadHandler() const noexcept { return m_headHandler; }

            static std::string normalizeKey(std::string_view key);

        private:
            void appendHandlerToChain(ICommandLineHandler* handler);
            void tokenizeArgs(const std::vector<std::string>& rawArgs, std::vector<CommandLineArg>& outArgs) const;
            void parseSingleOptionToken(const std::vector<std::string>& rawArgs,
                                        size_t& index,
                                        size_t prefixLen,
                                        std::vector<CommandLineArg>& outArgs) const;
            void dispatchToHandlers() const;

        private:
            std::vector<UniquePtr<ICommandLineHandler>> m_ownedHandlers;
            std::vector<ICommandLineHandler*> m_chainNodes;
            ICommandLineHandler* m_headHandler = nullptr;
            ICommandLineHandler* m_tailHandler = nullptr;

            std::vector<std::string> m_rawArgs;
            std::vector<CommandLineArg> m_parsedArgs;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CONFIG_COMMANDLINEPARSER_H
