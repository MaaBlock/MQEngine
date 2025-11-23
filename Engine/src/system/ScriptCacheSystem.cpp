//
// Created by MaaBlock on 2025/11/18.
//

#include "ScriptCacheSystem.h"
#include "ScriptSystem.h"
#include "../data/DataManager.h"
#include "../data/Component.h"
#include "../manager/RegistriesManager.h"
#include "../core/EngineGlobal.h"
#include <FCT_Node/NodeEnvironment.h>
#include <FCT_Node/JSObject.h>

namespace MQEngine {

    ScriptCacheSystem::ScriptCacheSystem(DataManager* dataManager, ScriptSystem* scriptSystem)
        : m_dataManager(dataManager), m_scriptSystem(scriptSystem) {}

    ScriptCacheSystem::~ScriptCacheSystem() = default;

    void ScriptCacheSystem::updateLogic()
    {
        if (!m_scriptSystem || !m_dataManager) {
            return;
        }

        auto* nodeEnv = m_scriptSystem->getNodeEnvironment();
        if (!nodeEnv) {
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
                    g_engineGlobal.registriesManager->requestRemoveComponent<CacheScriptComponent>(registry, entity);
                }
            }

            auto creationView = registry->view<ScriptFunctionTableComponent>(entt::exclude<CacheScriptComponent>);
            for (auto entity : creationView)
            {
                auto& table = registry->get<ScriptFunctionTableComponent>(entity);
                FCT::JSObject* jsObject = nullptr;

                                if (!table.className.empty()) {
                                    try {
                                        FCT::JSObject result = nodeEnv->callFunction<FCT::JSObject>("createScriptClassInstance", 
                                            table.className,
                                            static_cast<uint32_t>(entity),
                                            reinterpret_cast<uint64_t>(registry)
                                        );
                                        
                                        if (!result.isNull()) {
                                            jsObject = new FCT::JSObject(std::move(result));
                                        } else {
                                        }
                                    } catch (const std::exception& e) {
                                    }
                                }
                if (!jsObject) {
                     jsObject = new FCT::JSObject(
                            nodeEnv->callFunction<FCT::JSObject>("createEntityObject",
                            static_cast<uint32_t>(entity),
                            reinterpret_cast<uint64_t>(registry)));
                }

                for (const auto& funcName : table.onTicker) {
                    nodeEnv->callFunction("assignGlobalFunction", *jsObject, funcName);
                }
                
                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheScriptComponent>(registry, entity, jsObject);
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
            }
            g_engineGlobal.registriesManager->requestClearComponent<CacheScriptComponent>(registry);
        }
    }

} // MQEngine
