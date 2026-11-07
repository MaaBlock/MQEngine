#include "ScriptSystem.h"
#include "../../runtime/core/Engine.h"
#include "../../scene/data/Component.h"
#include "../../scene/manager/DataManager.h"
#include <iostream>

namespace MQEngine {

    ScriptSystem::ScriptSystem()
        : m_dataManager(g_engineGlobal.dataManager)
        , m_jsTsBackend(makeUnique<NodeJsScriptBackend>(m_dataManager))
        , m_pybindBackend(makeUnique<PybindScriptBackend>(m_dataManager)) {
    }

    void ScriptSystem::init() {
        if (!m_dataManager) {
            m_dataManager = g_engineGlobal.dataManager;
        }
        if (m_jsTsBackend) {
            Status st = m_jsTsBackend->initialize();
            if (!st.ok()) {
                std::cerr << "Failed to initialize NodeJsScriptBackend: " << st.message() << std::endl;
            }
        }
        if (m_pybindBackend) {
            (void)m_pybindBackend->initialize();
        }
    }

    ScriptSystem::~ScriptSystem() {
        cleanUp();
    }

    std::string ScriptSystem::resolveScriptsRoot() const {
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        std::string assetRoot = dm ? dm->getAssetRoot() : "./Assets";
        if (assetRoot.empty()) {
            assetRoot = "./Assets";
        }
        while (assetRoot.size() > 1 && (assetRoot.back() == '/' || assetRoot.back() == '\\')) {
            assetRoot.pop_back();
        }
        return assetRoot + "/Scripts";
    }

    FCT::JS::NodeEnvironment* ScriptSystem::getNodeEnvironment() const {
        return m_jsTsBackend ? m_jsTsBackend->getNodeEnvironment() : nullptr;
    }

    FCT::NB::Package* ScriptSystem::getEnginePackage() const {
        return m_jsTsBackend ? m_jsTsBackend->getEnginePackage() : nullptr;
    }

    FCT::PyBri::PyEnvironment* ScriptSystem::getPyEnvironment() const {
        return m_pybindBackend ? m_pybindBackend->getPyEnvironment() : nullptr;
    }

    FCT::PyBri::Package* ScriptSystem::getPyEnginePackage() const {
        return m_pybindBackend ? m_pybindBackend->getEnginePackage() : nullptr;
    }

    void ScriptSystem::loadScripts() {
        ZoneScoped();
        if (m_isScriptsLoaded) {
            return;
        }

        const std::string scriptsRoot = resolveScriptsRoot();
        if (m_jsTsBackend) {
            Status st = m_jsTsBackend->loadScriptsFromRoot(scriptsRoot);
            if (!st.ok()) {
                std::cerr << "NodeJsScriptBackend loadScripts error: " << st.message() << std::endl;
            }
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            (void)m_pybindBackend->loadScriptsFromRoot(scriptsRoot);
        }
        m_isScriptsLoaded = true;
    }

    void ScriptSystem::reloadScripts() {
        m_isScriptsLoaded = false;
        m_startedEntities.clear();
        loadScripts();
    }

    std::vector<std::string> ScriptSystem::getFunctionNames() const {
        std::vector<std::string> names = m_jsTsBackend ? m_jsTsBackend->getFunctionNames() : std::vector<std::string>{};
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            for (const auto& pyFn : m_pybindBackend->getFunctionNames()) {
                if (std::find(names.begin(), names.end(), pyFn) == names.end()) {
                    names.push_back(pyFn);
                }
            }
        }
        return names;
    }

    std::vector<std::string> ScriptSystem::getClassNames() const {
        std::vector<std::string> names = m_jsTsBackend ? m_jsTsBackend->getClassNames() : std::vector<std::string>{};
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            for (const auto& pyCls : m_pybindBackend->getClassNames()) {
                if (std::find(names.begin(), names.end(), pyCls) == names.end()) {
                    names.push_back(pyCls);
                }
            }
        }
        return names;
    }

    void ScriptSystem::setLogicDeltaTime(float deltaTime) {
        m_logicDeltaTime = deltaTime;
        if (m_jsTsBackend) {
            m_jsTsBackend->setLogicDeltaTime(deltaTime);
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            m_pybindBackend->setLogicDeltaTime(deltaTime);
        }
    }

    void ScriptSystem::start() {
        ZoneScoped();
        FCT::JS::NodeEnvironment* nodeEnv = getNodeEnvironment();
        FCT::PyBri::PyEnvironment* pyEnv = getPyEnvironment();
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if ((!nodeEnv && !pyEnv) || !dm) {
            return;
        }

        auto registries = dm->currentRegistries();
        for (auto& registry : registries) {
            auto view = registry->view<OnStartScriptComponent>();
            for (auto entity : view) {
                if (m_startedEntities.find(entity) != m_startedEntities.end()) {
                    continue;
                }
                const auto& scriptComponent = view.get<OnStartScriptComponent>(entity);
                if (scriptComponent.functionName.empty()) {
                    continue;
                }
                uint64_t registryPtr = reinterpret_cast<uint64_t>(registry);
                if (m_pybindBackend && m_pybindBackend->isAvailable() &&
                    m_pybindBackend->hasFunction(scriptComponent.functionName)) {
                    Status st = m_pybindBackend->executeGlobalEntityFunction(
                        scriptComponent.functionName, registryPtr, static_cast<uint32_t>(entity));
                    if (st.ok()) {
                        m_startedEntities.insert(entity);
                    } else {
                        std::cerr << "Error executing Python OnStart script '" << scriptComponent.functionName << "': " << st.message() << std::endl;
                    }
                    continue;
                }
                if (nodeEnv) {
                    try {
                        std::string setupCode = "globalThis.entityInfo.registry_ptr = " + std::to_string(registryPtr) +
                                                "; globalThis.entityInfo.entity_id = " + std::to_string(static_cast<uint32_t>(entity)) + ";";
                        nodeEnv->excuteScript(setupCode);

                        std::string callCode = scriptComponent.functionName + "();";
                        nodeEnv->excuteScript(callCode);
                        m_startedEntities.insert(entity);
                    } catch (const std::exception& e) {
                        std::cerr << "Error executing OnStart script '" << scriptComponent.functionName << "': " << e.what() << std::endl;
                    }
                }
            }
        }
    }

    void ScriptSystem::update() {
        ZoneScoped();
        FCT::JS::NodeEnvironment* nodeEnv = getNodeEnvironment();
        FCT::PyBri::PyEnvironment* pyEnv = getPyEnvironment();
        DataManager* dm = m_dataManager ? m_dataManager : g_engineGlobal.dataManager;
        if ((!nodeEnv && !pyEnv) || !dm) {
            return;
        }

        auto registries = dm->currentRegistries();
        for (auto& registry : registries) {
            auto view = registry->view<TickerScriptComponent>();
            for (auto entity : view) {
                auto& scriptComponent = view.get<TickerScriptComponent>(entity);
                if (!scriptComponent.functionName.empty()) {
                    uint64_t registryPtr = reinterpret_cast<uint64_t>(registry);
                    if (m_pybindBackend && m_pybindBackend->isAvailable() &&
                        m_pybindBackend->hasFunction(scriptComponent.functionName)) {
                        Status st = m_pybindBackend->executeGlobalEntityFunction(
                            scriptComponent.functionName, registryPtr, static_cast<uint32_t>(entity));
                        if (!st.ok()) {
                            std::cerr << "Error executing Python script function '" << scriptComponent.functionName << "': " << st.message() << std::endl;
                        }
                        continue;
                    }
                    if (nodeEnv) {
                        try {
                            std::string setupCode = "globalThis.entityInfo.registry_ptr = " + std::to_string(registryPtr) +
                                                    "; globalThis.entityInfo.entity_id = " + std::to_string(static_cast<uint32_t>(entity)) + ";";
                            nodeEnv->excuteScript(setupCode);

                            std::string callCode = scriptComponent.functionName + "();";
                            nodeEnv->excuteScript(callCode);
                        } catch (const std::exception& e) {
                            std::cerr << "Error executing script function '" << scriptComponent.functionName << "': " << e.what() << std::endl;
                        }
                    }
                }
            }

            auto cacheView = registry->view<CacheScriptComponent>();
            for (auto entity : cacheView) {
                auto& cache = cacheView.get<CacheScriptComponent>(entity);
                bool isFirstStart = (m_startedEntities.find(entity) == m_startedEntities.end());
                if (isFirstStart && (cache.m_jsObject || cache.m_pyObject)) {
                    m_startedEntities.insert(entity);
                }

                if (cache.m_jsObject) {
                    try {
                        if (isFirstStart && cache.m_jsObject->hasProperty("onStart")) {
                            cache.m_jsObject->call("onStart");
                        }
                        if (cache.m_jsObject->hasProperty("onUpdate")) {
                            cache.m_jsObject->call("onUpdate", m_logicDeltaTime);
                        }
                        if (cache.m_jsObject->hasProperty("onTicker")) {
                            cache.m_jsObject->call("onTicker");
                        }
                        if (cache.m_jsObject->hasProperty("_onTicker")) {
                            cache.m_jsObject->call("_onTicker");
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Error calling script lifecycle: " << e.what() << std::endl;
                    }
                }

                if (cache.m_pyObject && pyEnv) {
                    try {
                        if (isFirstStart) {
                            pyEnv->callFunction<void>("invokePythonEntityStart", *cache.m_pyObject);
                        }
                        pyEnv->callFunction<void>("invokePythonEntityUpdate", *cache.m_pyObject, m_logicDeltaTime);
                    } catch (const std::exception& e) {
                        std::cerr << "Error calling Python script lifecycle: " << e.what() << std::endl;
                    }
                }
            }
        }

        if (m_jsTsBackend) {
            m_jsTsBackend->tick();
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            m_pybindBackend->tick();
        }
    }

    void ScriptSystem::cleanUp() {
        m_startedEntities.clear();
        if (m_pybindBackend) {
            m_pybindBackend->shutdown();
        }
        if (m_jsTsBackend) {
            m_jsTsBackend->shutdown();
        }
    }

    std::string ScriptSystem::transpileTypeScript(const std::string& tsSource) {
        return NodeJsScriptBackend::transpileTypeScript(tsSource);
    }

    void ScriptSystem::initializeCoreEngineBridge() {
        const std::string scriptsRoot = resolveScriptsRoot();
        if (m_jsTsBackend) {
            m_jsTsBackend->initializeCoreBridge(scriptsRoot);
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            m_pybindBackend->initializeCoreBridge(scriptsRoot);
        }
    }

    void ScriptSystem::exportNbrTypeDefinitions() {
        const std::string scriptsRoot = resolveScriptsRoot();
        if (m_jsTsBackend) {
            m_jsTsBackend->exportTypeDefinitions(scriptsRoot);
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            m_pybindBackend->exportTypeDefinitions(scriptsRoot);
        }
    }

    Status ScriptSystem::executePluginScript(const std::string& pluginName,
                                             const std::string& pluginVersion,
                                             const std::string& entryPath,
                                             const std::string& pluginDir,
                                             ResourceLoader* resourceLoader) {
        if (SAI::Path::hasExtension(entryPath, ".py")) {
            if (!m_pybindBackend || !m_pybindBackend->isAvailable()) {
                return absl::FailedPreconditionError("FCT_PyBri Python 后端未初始化");
            }
            m_pythonPluginNames.insert(pluginName);
            return m_pybindBackend->executeModuleScript(pluginName, pluginVersion, entryPath, pluginDir, resourceLoader);
        }
        m_pythonPluginNames.erase(pluginName);
        return m_jsTsBackend
            ? m_jsTsBackend->executeModuleScript(pluginName, pluginVersion, entryPath, pluginDir, resourceLoader)
            : absl::FailedPreconditionError("NodeJsScriptBackend 未初始化");
    }

    Status ScriptSystem::invokePluginLifecycle(const std::string& pluginName,
                                               const std::string& methodName,
                                               const std::string& argsJson) {
        if (m_pythonPluginNames.find(pluginName) != m_pythonPluginNames.end()) {
            return m_pybindBackend
                ? m_pybindBackend->invokeModuleLifecycle(pluginName, methodName, argsJson)
                : absl::FailedPreconditionError("FCT_PyBri Python 后端未初始化");
        }
        return m_jsTsBackend
            ? m_jsTsBackend->invokeModuleLifecycle(pluginName, methodName, argsJson)
            : absl::FailedPreconditionError("NodeJsScriptBackend 未初始化");
    }

    void ScriptSystem::unloadPluginScript(const std::string& pluginName) {
        if (m_pythonPluginNames.find(pluginName) != m_pythonPluginNames.end()) {
            m_pythonPluginNames.erase(pluginName);
            if (m_pybindBackend && m_pybindBackend->isAvailable()) {
                m_pybindBackend->unloadModuleScript(pluginName);
            }
            return;
        }
        if (m_jsTsBackend) {
            m_jsTsBackend->unloadModuleScript(pluginName);
        }
        if (m_pybindBackend && m_pybindBackend->isAvailable()) {
            m_pybindBackend->unloadModuleScript(pluginName);
        }
    }

} // namespace MQEngine