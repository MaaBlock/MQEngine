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
                auto* jsObject = new FCT::JSObject(nodeEnv->createJSObject());
                g_engineGlobal.registriesManager->requestEmplaceComponent<CacheScriptComponent>(registry, entity, jsObject);
            }
        }
    }

    void ScriptCacheSystem::updateRender()
    {
    }

} // MQEngine
