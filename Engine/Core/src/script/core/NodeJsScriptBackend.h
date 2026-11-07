#ifndef MQENGINE_NODEJSSCRIPTBACKEND_H
#define MQENGINE_NODEJSSCRIPTBACKEND_H

#include "IScriptBackend.h"
#include "../../scene/system/ComponentReflection.h"
#include <memory>
#include <string>
#include <vector>

namespace MQEngine {

    class DataManager;

    /**
     * @brief 基于 FCT_Node 与 FCT_NBR 的 JavaScript / TypeScript 脚本后端实现
     */
    class ENGINE_API NodeJsScriptBackend : public IScriptBackend {
    public:
        explicit NodeJsScriptBackend(DataManager* dataManager);
        ~NodeJsScriptBackend() override;

        ScriptBackendType getType() const override { return ScriptBackendType::JsTs; }
        const char* getName() const override { return "NodeJS_NBR"; }
        bool isAvailable() const override { return m_nodeEnv != nullptr; }

        Status initialize() override;
        void shutdown() override;

        Status loadScriptsFromRoot(const std::string& scriptsRoot) override;
        Status reloadScriptsFromRoot(const std::string& scriptsRoot) override;
        void exportTypeDefinitions(const std::string& scriptsRoot) override;

        void setLogicDeltaTime(float deltaTime) override;
        void tick() override;

        std::vector<std::string> getFunctionNames() const override;
        std::vector<std::string> getClassNames() const override;

        Status executeModuleScript(const std::string& moduleName,
                                   const std::string& moduleVersion,
                                   const std::string& entryPath,
                                   const std::string& moduleDir,
                                   ResourceLoader* resourceLoader) override;

        Status invokeModuleLifecycle(const std::string& moduleName,
                                     const std::string& methodName,
                                     const std::string& argsJson = "{}") override;

        void unloadModuleScript(const std::string& moduleName) override;

        FCT::JS::NodeEnvironment* getNodeEnvironment() const { return m_nodeEnv.get(); }
        FCT::NB::Package* getEnginePackage() const { return m_enginePackage.get(); }
        ComponentReflection* getActiveReflection() const;

        void initializeCoreBridge(const std::string& scriptsRoot);
        static std::string transpileTypeScript(const std::string& tsSource);

        std::vector<std::string> loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext);
        void executeLoadedScriptFiles(const std::vector<std::string>& scriptFiles);
        std::string readFileContent(const std::string& filePath);

    private:
        DataManager* m_dataManager = nullptr;
        UniquePtr<ComponentReflection> m_fallbackReflection;
        UniquePtr<FCT::JS::NodeEnvironment> m_nodeEnv;
        UniquePtr<FCT::NB::Package> m_enginePackage;
        float m_logicDeltaTime = 0.0f;
        bool m_isCoreBridgeInitialized = false;

        void setupNbrEnginePackage();
        void ensureScriptsWorkspaceConfig(const std::string& scriptsRoot, ResourceLoader* loader);
        void injectEntityRuntimeBootstrap();
        void registerEntityFunctions();
        void registerInputFunctions();
        void executeSingleScriptFile(const std::string& filePath, const std::string& rawCode);

        ComponentValue convertJSObjectToComponentValue(const std::string& fieldType, FCT::JS::Any& jsAny, const std::string& fieldName);
        v8::Local<v8::Value> convertComponentValueToJS(const ComponentValue& value);
    };

} // namespace MQEngine

#endif // MQENGINE_NODEJSSCRIPTBACKEND_H
