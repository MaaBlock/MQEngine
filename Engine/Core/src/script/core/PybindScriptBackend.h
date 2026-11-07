#ifndef MQENGINE_PYBINDSCRIPTBACKEND_H
#define MQENGINE_PYBINDSCRIPTBACKEND_H

#include "IScriptBackend.h"
#include "../../scene/system/ComponentReflection.h"
#include <memory>
#include <string>
#include <vector>

namespace MQEngine {

    class DataManager;

    /**
     * @brief 基于 FCT_PyBri (嵌入式 CPython 双向反射桥接) 的 Python 脚本与插件运行时后端
     */
    class ENGINE_API PybindScriptBackend : public IScriptBackend {
    public:
        explicit PybindScriptBackend(DataManager* dataManager);
        ~PybindScriptBackend() override;

        ScriptBackendType getType() const override { return ScriptBackendType::Pybind; }
        const char* getName() const override { return "FCT_PyBri"; }
        bool isAvailable() const override { return m_isAvailable; }

        Status initialize() override;
        void shutdown() override;

        Status loadScriptsFromRoot(const std::string& scriptsRoot) override;
        Status reloadScriptsFromRoot(const std::string& scriptsRoot) override;
        void exportTypeDefinitions(const std::string& scriptsRoot) override;

        void setLogicDeltaTime(float deltaTime) override;
        void tick() override;

        std::vector<std::string> getFunctionNames() const override;
        std::vector<std::string> getClassNames() const override;

        bool hasFunction(const std::string& funcName) const;
        bool hasClass(const std::string& className) const;

        Status executeGlobalEntityFunction(const std::string& funcName,
                                           uint64_t registryPtr,
                                           uint32_t entityId);

        Status executeModuleScript(const std::string& moduleName,
                                   const std::string& moduleVersion,
                                   const std::string& entryPath,
                                   const std::string& moduleDir,
                                   ResourceLoader* resourceLoader) override;

        Status invokeModuleLifecycle(const std::string& moduleName,
                                     const std::string& methodName,
                                     const std::string& argsJson = "{}") override;

        void unloadModuleScript(const std::string& moduleName) override;

#ifndef __ANDROID__
        FCT::PyBri::PyEnvironment* getPyEnvironment() const { return m_pyEnv.get(); }
        FCT::PyBri::Package* getEnginePackage() const { return m_enginePackage.get(); }
#endif
        ComponentReflection* getActiveReflection() const;

        void initializeCoreBridge(const std::string& scriptsRoot);

    private:
        DataManager* m_dataManager = nullptr;
        UniquePtr<ComponentReflection> m_fallbackReflection;
#ifndef __ANDROID__
        UniquePtr<FCT::PyBri::PyEnvironment> m_pyEnv;
        UniquePtr<FCT::PyBri::Package> m_enginePackage;
#endif
        float m_logicDeltaTime = 0.0f;
        bool m_isAvailable = false;
        bool m_isCoreBridgeInitialized = false;
        mutable std::mutex m_lifecycleCacheMutex;
        std::unordered_map<std::string, std::unordered_set<std::string>> m_moduleLifecycleMethods;

        void setupPyBriEnginePackage();
        void cacheModuleLifecycleMethods(const std::string& moduleName);
        bool hasCachedModuleLifecycleMethod(const std::string& moduleName, const std::string& methodName) const;
        void ensurePythonWorkspaceConfig(const std::string& scriptsRoot, ResourceLoader* loader);
        void injectEntityRuntimeBootstrap();
        void registerEntityFunctions();
        void registerInputFunctions();
        void executeSinglePythonFile(const std::string& filePath, const std::string& rawCode);

        std::vector<std::string> loadScriptFilesFromDirectory(const std::string& directory, const std::string& ext);
        std::string readFileContent(const std::string& filePath);

#ifndef __ANDROID__
        ComponentValue convertPyObjectToComponentValue(const std::string& fieldType,
                                                       const FCT::PyBri::Object& pyObj,
                                                       const std::string& fieldName);
        FCT::PyBri::Object convertComponentValueToPy(const ComponentValue& value);
#endif
    };

} // namespace MQEngine

#endif // MQENGINE_PYBINDSCRIPTBACKEND_H
