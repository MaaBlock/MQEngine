//
// Created by Administrator on 2025/8/31.
//

#ifndef SCRIPTSYSTEM_H
#define SCRIPTSYSTEM_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../core/IScriptBackend.h"
#include "../core/NodeJsScriptBackend.h"
#include "../core/PybindScriptBackend.h"
#include "../../scene/system/ComponentReflection.h"
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace MQEngine {

    class ResourceLoader;

    /**
     * @brief 引擎脚本系统（协调 Engine/src/script 下的 Node/NBR JS/TS 后端与 Pybind11 Python 后端）
     */
    class ENGINE_API ScriptSystem {
    public:
        ScriptSystem();
        ~ScriptSystem();

        void init();
        void loadScripts();
        void reloadScripts();

        std::vector<std::string> getFunctionNames() const;
        std::vector<std::string> getClassNames() const;

        void start();
        void update();
        void cleanUp();

        void setLogicDeltaTime(float deltaTime);

        FCT::JS::NodeEnvironment* getNodeEnvironment() const;
        FCT::NB::Package* getEnginePackage() const;
        FCT::PyBri::PyEnvironment* getPyEnvironment() const;
        FCT::PyBri::Package* getPyEnginePackage() const;

        NodeJsScriptBackend* getJsTsBackend() const { return m_jsTsBackend.get(); }
        PybindScriptBackend* getPybindBackend() const { return m_pybindBackend.get(); }

        static std::string transpileTypeScript(const std::string& tsSource);

        void initializeCoreEngineBridge();
        void exportNbrTypeDefinitions();

        template<typename ServiceClass>
        void registerScriptService(const std::string& serviceName, ServiceClass* instance) {
            if (!instance) {
                return;
            }
            if (getEnginePackage() && getNodeEnvironment()) {
                getEnginePackage()->bindDescribedService(*getNodeEnvironment(), serviceName, instance);
                getEnginePackage()->mountToEnvironment(*getNodeEnvironment(), "MQEngine");
            }
            if (getPyEnginePackage() && getPyEnvironment()) {
                getPyEnginePackage()->bindDescribedService(*getPyEnvironment(), serviceName, instance);
                getPyEnginePackage()->mountToEnvironment(*getPyEnvironment(), "MQEngine");
            }
            exportNbrTypeDefinitions();
        }

        void addTypeScriptDeclaration(const std::string& tsCode) {
            if (tsCode.empty() || !getEnginePackage()) {
                return;
            }
            getEnginePackage()->addTypeScriptDeclaration(tsCode);
            exportNbrTypeDefinitions();
        }

        Status executePluginScript(const std::string& pluginName,
                                   const std::string& pluginVersion,
                                   const std::string& entryPath,
                                   const std::string& pluginDir,
                                   ResourceLoader* resourceLoader);

        Status invokePluginLifecycle(const std::string& pluginName,
                                     const std::string& methodName,
                                     const std::string& argsJson = "{}");

        void unloadPluginScript(const std::string& pluginName);

        std::string resolveScriptsRoot() const;

    private:
        DataManager* m_dataManager = nullptr;
        UniquePtr<NodeJsScriptBackend> m_jsTsBackend;
        UniquePtr<PybindScriptBackend> m_pybindBackend;
        float m_logicDeltaTime = 0.0f;
        bool m_isScriptsLoaded = false;
        std::unordered_set<entt::entity> m_startedEntities;
        std::unordered_set<std::string> m_pythonPluginNames;
    };

} // namespace MQEngine

#endif // SCRIPTSYSTEM_H
