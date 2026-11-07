//
// Created by MaaBlock on 2025/11/18.
//

#include "ScriptCacheSystem.h"
#include "ScriptSystem.h"
#include "../../scene/manager/DataManager.h"
#include "../../scene/data/Component.h"
#include "../../scene/manager/RegistriesManager.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine {

    ScriptCacheSystem::ScriptCacheSystem(DataManager* dataManager, ScriptSystem* scriptSystem)
        : m_dataManager(dataManager), m_scriptSystem(scriptSystem) {}

    ScriptCacheSystem::~ScriptCacheSystem() = default;

    void ScriptCacheSystem::updateLogic()
    {
        ZoneScoped();
        if (!m_scriptSystem || !m_dataManager) {
            return;
        }

        auto* nodeEnv = m_scriptSystem->getNodeEnvironment();
        auto* pyBackend = m_scriptSystem->getPybindBackend();
        auto* pyEnv = (pyBackend && pyBackend->isAvailable()) ? pyBackend->getPyEnvironment() : nullptr;
        if (!nodeEnv && !pyEnv) {
            return;
        }

        auto registries = m_dataManager->currentRegistries();
        for (auto* registry : registries)
        {
            if (!registry) continue;

            auto cleanupView = registry->view<CacheScriptComponent>();
            for (auto entity : cleanupView)
            {
                if (!registry->all_of<ScriptFunctionTableComponent>(entity))
                {
                    auto& cache = cleanupView.get<CacheScriptComponent>(entity);
                    if (cache.m_jsObject)
                    {
                        delete cache.m_jsObject;
                        cache.m_jsObject = nullptr;
                    }
                    if (cache.m_pyObject)
                    {
                        delete cache.m_pyObject;
                        cache.m_pyObject = nullptr;
                    }
                    g_engineGlobal.registriesManager->requestRemoveComponent<CacheScriptComponent>(registry, entity);
                }
            }

            auto creationView = registry->view<ScriptFunctionTableComponent>(entt::exclude<CacheScriptComponent>);
            for (auto entity : creationView)
            {
                auto& table = registry->get<ScriptFunctionTableComponent>(entity);
                FCT::JS::Object* jsObject = nullptr;
                FCT::PyBri::Object* pyObject = nullptr;

                if (!table.className.empty()) {
                    if (pyEnv && pyBackend->hasClass(table.className)) {
                        try {
                            FCT::PyBri::Object pyInst = pyEnv->callFunction<FCT::PyBri::Object>(
                                "createPythonScriptInstance",
                                table.className,
                                static_cast<uint32_t>(entity),
                                reinterpret_cast<uint64_t>(registry));
                            if (pyInst.isValid() && !pyInst.isNull()) {
                                pyObject = new FCT::PyBri::Object(std::move(pyInst));
                            }
                        } catch (const std::exception&) {
                        }
                    }

                    if (!pyObject && nodeEnv) {
                        try {
                            FCT::JS::Object result = nodeEnv->callFunction<FCT::JS::Object>("createScriptClassInstance",
                                table.className,
                                static_cast<uint32_t>(entity),
                                reinterpret_cast<uint64_t>(registry)
                            );
                            if (!result.isNull()) {
                                jsObject = new FCT::JS::Object(std::move(result));
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }

                for (const auto& funcName : table.onTicker) {
                    if (pyEnv && pyBackend->hasFunction(funcName)) {
                        try {
                            if (!pyObject) {
                                pyObject = new FCT::PyBri::Object(
                                    pyEnv->callFunction<FCT::PyBri::Object>(
                                        "createPythonEntityObject",
                                        static_cast<uint32_t>(entity),
                                        reinterpret_cast<uint64_t>(registry)));
                            }
                            pyEnv->callFunction<void>("assignPythonGlobalFunction", *pyObject, funcName);
                        } catch (const std::exception&) {
                        }
                    } else if (nodeEnv) {
                        if (!jsObject) {
                            jsObject = new FCT::JS::Object(
                                nodeEnv->callFunction<FCT::JS::Object>("createEntityObject",
                                    static_cast<uint32_t>(entity),
                                    reinterpret_cast<uint64_t>(registry)));
                        }
                        nodeEnv->callFunction("assignGlobalFunction", *jsObject, funcName);
                    }
                }

                if (!jsObject && !pyObject && nodeEnv) {
                    jsObject = new FCT::JS::Object(
                        nodeEnv->callFunction<FCT::JS::Object>("createEntityObject",
                            static_cast<uint32_t>(entity),
                            reinterpret_cast<uint64_t>(registry)));
                }

                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheScriptComponent>(registry, entity, jsObject, pyObject);
            }
        }
    }

    void ScriptCacheSystem::updateRender()
    {
    }

    void ScriptCacheSystem::onDeactivate()
    {
        if (!m_dataManager) return;
        auto registries = m_dataManager->currentRegistries();
        for (auto* registry : registries)
        {
            if (!registry) continue;
            auto view = registry->view<CacheScriptComponent>();
            for (auto entity : view)
            {
                auto& cache = view.get<CacheScriptComponent>(entity);
                if (cache.m_jsObject) {
                    delete cache.m_jsObject;
                    cache.m_jsObject = nullptr;
                }
                if (cache.m_pyObject) {
                    delete cache.m_pyObject;
                    cache.m_pyObject = nullptr;
                }
            }
            g_engineGlobal.registriesManager->requestClearComponent<CacheScriptComponent>(registry);
        }
    }

} // MQEngine
