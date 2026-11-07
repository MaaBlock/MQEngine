#ifndef MQENGINE_BASE_ENGINECLIOPTION_H
#define MQENGINE_BASE_ENGINECLIOPTION_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "./CoreEngineManager.h"
#include "./classdef.h"

#ifndef ENGINE_CLI_OPTION
#define ENGINE_CLI_OPTION(Type, name, default_value, help) \
    ABSL_FLAG(Type, name, default_value, help)
#endif

namespace MQEngine
{
    class EngineCLIOption;

    /**
     * @brief 声明式命令行选项描述元数据（支持各层声明式注入与统一帮助/未知校验）
     */
    struct CLIOptionDesc
    {
        std::string name;        ///< 参数名称（如 "headless", "scene", "test-frames"）
        bool takesValue = false; ///< 是否携带后续参数值
        std::string description; ///< 描述信息（供 --help 格式化输出）
    };

    /**
     * @brief 单个命令行参数解析条目（由分词器生成并沿职责链传递）
     */
    struct CLIOptionItem
    {
        std::string rawToken;           ///< 原始命令行参数字符串
        std::string key;                ///< 剥离 '-' / '--' 前缀后的参数名（位置参数为空）
        std::string value;              ///< 参数值（独立布尔开关默认为 "true"，--no-xxx 为 "false"）
        bool isPositional = false;      ///< 是否为位置参数（非 '-' 开头）
        bool hasExplicitValue = false;  ///< 是否包含显式赋值（如 --key=val 或 --key val）
    };

    /**
     * @brief 命令行参数职责链节点抽象基类：各模块或 Application 可实现该接口并挂载至 EngineCLIOption 职责链
     */
    class ENGINE_API ICLIOptionHandler
    {
    public:
        virtual ~ICLIOptionHandler() = default;

        void setNext(ICLIOptionHandler* next) noexcept { m_next = next; }
        ICLIOptionHandler* getNext() const noexcept { return m_next; }

        /**
         * @brief 沿职责链处理单个命令行参数：若当前节点识别并消费则返回 true，否则传递至下一节点
         */
        bool handle(const CLIOptionItem& item, EngineCLIOption& cli)
        {
            if (onHandleOption(item, cli)) {
                return true;
            }
            if (m_next) {
                return m_next->handle(item, cli);
            }
            return false;
        }

        /**
         * @brief 沿职责链广播命令行解析完成事件，供各节点同步最终状态
         */
        void notifyParseComplete(EngineCLIOption& cli)
        {
            onParseComplete(cli);
            if (m_next) {
                m_next->notifyParseComplete(cli);
            }
        }

        virtual std::string getHandlerName() const { return "ICLIOptionHandler"; }

    protected:
        virtual bool onHandleOption(const CLIOptionItem& item, EngineCLIOption& cli) = 0;
        virtual void onParseComplete(EngineCLIOption& cli) { (void)cli; }

    private:
        ICLIOptionHandler* m_next = nullptr;
    };

    /**
     * @brief 基于职责链模式的引擎命令行解析器（Base 层）
     * @note 职责链结构：
     *   [AbseilFlagCLIHandler] -> [Module / Runtime Handlers...] -> [Application Handler] -> [FallbackStoreCLIHandler]
     */
    class ENGINE_API EngineCLIOption : public CoreEngineManager<EngineCLIOption>
    {
    public:
        using HandleCallback = std::function<bool(const CLIOptionItem& item, EngineCLIOption& cli)>;
        using CompleteCallback = std::function<void(EngineCLIOption& cli)>;

        EngineCLIOption();
        ~EngineCLIOption();

        EngineCLIOption(const EngineCLIOption&) = delete;
        EngineCLIOption& operator=(const EngineCLIOption&) = delete;

        void registerInfo() {}

        /**
         * @brief 向职责链尾部（兜底节点之前）挂载自定义处理器节点（转移所有权）
         */
        void addHandler(UniquePtr<ICLIOptionHandler> handler);

        /**
         * @brief 向职责链尾部（兜底节点之前）挂载外部持有的处理器节点（不转移所有权）
         */
        void addExternalHandler(ICLIOptionHandler* handler);

        /**
         * @brief 向职责链尾部（兜底节点之前）挂载基于 Lambda 回调的处理器节点
         */
        void addCallbackHandler(const std::string& name,
                                HandleCallback onHandle,
                                CompleteCallback onComplete = nullptr);

        /**
         * @brief 注册声明式命令行选项描述
         */
        void registerOptionDesc(const CLIOptionDesc& desc);

        /**
         * @brief 获取所有已注册的声明式选项描述列表
         */
        const std::vector<CLIOptionDesc>& getRegisteredOptionDescs() const noexcept { return m_optionDescs; }

        /**
         * @brief 格式化输出命令行帮助说明并退出
         */
        void printHelp() const;

        /**
         * @brief 校验原始命令行参数，拦截未注册的未知参数或 --help 请求
         */
        void validateOptions(const std::vector<std::string>& rawArgs);

        /**
         * @brief 注册需要携带后续参数值的选项名（用于区分 `--key value` 与独立布尔开关 `--flag positional`）
         */
        void registerValueOptionKey(const std::string& key);

        /**
         * @brief 解析命令行参数并通过职责链依次分发
         */
        void parse(int argc, char* argv[]);
        void parse(const std::vector<std::string>& args);

        /**
         * @brief 查询与设置选项键值
         */
        bool hasOption(const std::string& key) const;
        std::string getOption(const std::string& key, const std::string& defaultVal = "") const;
        bool getBoolOption(const std::string& key, bool defaultVal = false) const;
        int32_t getIntOption(const std::string& key, int32_t defaultVal = 0) const;
        void setOption(const std::string& key, const std::string& value);

        /**
         * @brief 存储未被特定模块拦截的扩展选项或位置参数（由兜底节点或自定义节点调用）
         */
        void storeExtraOption(const std::string& key, const std::string& value);
        void storePositionalArg(const std::string& arg);

        const std::vector<std::string>& getRawArgs() const noexcept { return m_rawArgs; }
        const std::vector<std::string>& getPositionalArgs() const noexcept { return m_positionalArgs; }
        const std::unordered_map<std::string, std::string>& getExtraOptions() const noexcept { return m_extraOptions; }

        /**
         * @brief 将连字符参数名规范化为下划线形式（如 "test-frames" -> "test_frames"）
         */
        static std::string normalizeKeyToUnderscore(const std::string& key);

    private:
        void rebuildChainLinks();
        void tokenizeArgs(const std::vector<std::string>& args, std::vector<CLIOptionItem>& outItems) const;
        bool isValueTakingOption(const std::string& key) const;

    private:
        UniquePtr<ICLIOptionHandler> m_abseilHandler;
        UniquePtr<ICLIOptionHandler> m_fallbackHandler;
        std::vector<UniquePtr<ICLIOptionHandler>> m_ownedHandlers;
        std::vector<ICLIOptionHandler*> m_customChainOrder;
        ICLIOptionHandler* m_headHandler = nullptr;

        std::unordered_set<std::string> m_valueOptionKeys;
        std::vector<CLIOptionDesc> m_optionDescs;
        std::unordered_set<std::string> m_registeredOptionNames;
        std::vector<std::string> m_rawArgs;
        std::vector<std::string> m_positionalArgs;
        std::unordered_map<std::string, std::string> m_extraOptions;
    };

} // namespace MQEngine

#endif // MQENGINE_BASE_ENGINECLIOPTION_H
