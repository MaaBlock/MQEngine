//
// Created by Administrator on 2025/8/31.
//

#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../core/EngineGlobal.h"
#include "ComponentReflection.h"
#include <boost/json.hpp>
#include <memory>
#include <vector>
#include <string>

#include <unordered_set>

namespace MQEngine {
    class ResourceLoader;
    class ENGINE_API ScriptSystem {
    public:
        ScriptSystem();
        ~ScriptSystem();
        
        /**
         * @brief 加载并执行JavaScript脚本文件
         * @details 从./res/scripts/dist/和./res/scripts/js目录下获取所有js文件，
         *          然后使用NodeEnvironment执行所有的js文件
         */
        void loadScripts();

        /**
         * @brief 强制重新加载脚本（会重置JS环境）
         */
        void reloadScripts();
        
        /**
         * @brief 获取当前JavaScript环境中的所有函数名
         * @return JavaScript函数名列表
         */
        std::vector<std::string> getFunctionNames() const;

        /**
         * @brief 获取当前JavaScript环境中的所有类名
         * @return JavaScript类名列表
         */
        std::vector<std::string> getClassNames() const;
        
        /**
         * @brief 执行所有OnStart类型的脚本
         */
        void start();

        /**
         * @brief 更新系统，执行所有Ticker类型的脚本
         */
        void update();
        
        /**
         * @brief 清理已启动的实体记录
         */
        void cleanUp();

        /**
         * @brief 设置逻辑帧时间间隔
         * @param deltaTime 帧时间间隔（秒）
         */
        void setLogicDeltaTime(float deltaTime);
        
        FCT::JS::NodeEnvironment* getNodeEnvironment() const { return m_nodeEnv.get(); }

        /**
         * @brief 将 TypeScript 源码转译为可执行 JavaScript（引擎核心 TS 支持）
         */
        static std::string transpileTypeScript(const std::string& tsSource);

        /**
         * @brief 初始化引擎核心 JS/TS 运行时环境与 globalThis.MQEngine 桥接接口
         */
        void initializeCoreEngineBridge();

        /**
         * @brief 处理来自 JS/TS 环境的引擎控制与查询请求
         */
        std::string dispatchEngineBridgeCall(const std::string& action, const std::string& payloadJson);

        /**
         * @brief 加载并执行以 JS/TS 编写的插件入口脚本
         */
        Status executePluginScript(const std::string& pluginName,
                                   const std::string& pluginVersion,
                                   const std::string& entryPath,
                                   const std::string& pluginDir,
                                   ResourceLoader* resourceLoader);

        /**
         * @brief 调用已加载 JS/TS 插件的生命周期钩子函数
         */
        Status invokePluginLifecycle(const std::string& pluginName,
                                     const std::string& methodName,
                                     const std::string& argsJson = "{}");

        /**
         * @brief 卸载并清理 JS/TS 插件对象
         */
        void unloadPluginScript(const std::string& pluginName);
        
    private:
        DataManager* m_dataManager;
        std::unique_ptr<FCT::JS::NodeEnvironment> m_nodeEnv;
        std::unique_ptr<ComponentReflection> m_componentReflection;
        float m_logicDeltaTime = 0.0f;
        bool m_isScriptsLoaded = false;
        bool m_isCoreBridgeInitialized = false;
        std::unordered_set<entt::entity> m_startedEntities;
        
        /**
         * 从指定目录加载所有脚本文件 (.js / .ts)
         * @param directory 目录路径
         * @return 文件路径列表
         */
        std::vector<std::string> loadJSFilesFromDirectory(const std::string& directory);
        std::vector<std::string> loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext);

        /**
         * 注册实体成员函数到JavaScript环境
         */
        void registerEntityFunctions();
        void registerInputFunctions();
        void executeLoadedScriptFiles(const std::vector<std::string>& scriptFiles);
        
        /**
         * @brief 通用类型转换函数：从JSObject转换为ComponentValue
         * @param fieldType 字段类型
         * @param jsObject JavaScript对象
         * @param fieldName 字段名称（同时用于错误输出）
         * @return 转换后的ComponentValue
         */
        ComponentValue convertJSObjectToComponentValue(const std::string& fieldType, FCT::JS::Any& jsAny, const std::string& fieldName);
        
        ComponentValue convertJSObjectToComponentValue(const std::string& fieldType, const FCT::JS::Any& fieldValue);
        std::pair<entt::registry*, entt::entity> getEntityFromJS(FCT::JS::NodeEnvironment& env);
        v8::Local<v8::Value> convertComponentValueToJS(const ComponentValue& value);
        
        /**
         * @brief 读取文件内容
         * @param filePath 文件路径
         * @return 文件内容字符串
         */
        std::string readFileContent(const std::string& filePath);

        boost::json::object handleGetEngineStatus();
        boost::json::object handleListPlugins();
        boost::json::object handleSetPlayMode(const boost::json::object& params);
        boost::json::object handleListScenes();
        boost::json::object handleOpenScene(const boost::json::object& params);
        boost::json::object handleSaveScene(const boost::json::object& params);
        boost::json::object handleNewScene(const boost::json::object& params);
        boost::json::object handleListEntities(const boost::json::object& params);
        boost::json::object handleGetEntity(const boost::json::object& params);
        boost::json::object handleCreateEntity(const boost::json::object& params);
        boost::json::object handleDestroyEntity(const boost::json::object& params);
        boost::json::object handleListComponentTypes();
        boost::json::object handleGetComponent(const boost::json::object& params);
        boost::json::object handleSetComponent(const boost::json::object& params);
        boost::json::object handleAddComponent(const boost::json::object& params);
        boost::json::object handleRemoveComponent(const boost::json::object& params);

        entt::registry* resolveTargetRegistry(const std::string& trunkName, std::string& outResolvedTrunk) const;
        boost::json::object serializeEntityComponents(entt::registry& registry, entt::entity entity) const;
        boost::json::object serializeComponentFields(entt::registry& registry,
                                                     entt::entity entity,
                                                     const std::string& componentName) const;
        Status applyComponentFieldsFromJson(entt::registry& registry,
                                            entt::entity entity,
                                            const std::string& componentName,
                                            const boost::json::object& fieldsObj);
        static boost::json::value componentValueToJson(const ComponentValue& val);
        static StatusOr<ComponentValue> jsonToComponentValue(const std::string& fieldType,
                                                             const boost::json::value& jv);
    };
} // MQEngine

#endif //SCRIPTSYSTEM_H
