//
// Created by MQ on 2025/9/15.
//

#include "ShininessSystem.h"
#include "../core/UniformSlots.h"
#include <set>

namespace MQEngine {
    ShininessSystem::ShininessSystem(FCT::Context* ctx, DataManager* dataManager)
        : m_ctx(ctx), m_dataManager(dataManager), m_defaultShininessUniform(ctx, ShininessUniformSlot)
    {
        m_defaultShininessUniform.setValue("shininess", 32.0f);
        m_defaultShininessUniform.update();
    }

    void ShininessSystem::update()
    {
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            cleanupCacheComponents(registry);
            
            auto view = registry->view<ShininessComponent>();
            for (auto entity : view)
            {
                processEntity(registry, entity);
            }
        }
    }

    void ShininessSystem::processEntity(entt::registry* registry, entt::entity entity)
    {
        const auto* shininess = registry->try_get<ShininessComponent>(entity);
        if (!shininess) return;

        auto* cache = registry->try_get<CacheShininess>(entity);
        if (!cache)
        {
            cache = &registry->emplace<CacheShininess>(entity, m_ctx);
        }

        cache->uniform->setValue("shininess", shininess->shininess);
        cache->init = true;
    }

    void ShininessSystem::updateUniforms()
    {
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<CacheShininess>();
            for (auto entity : view)
            {
                auto& cache = view.get<CacheShininess>(entity);
                if (cache.init)
                {
                    cache.uniform->update();
                }
            }
        }
    }

    void ShininessSystem::bindShininess(entt::registry* registry, entt::entity entity, FCT::Layout* layout)
    {
        const auto* cache = registry->try_get<CacheShininess>(entity);
        if (cache && cache->init)
        {
            layout->bindUniform(*cache->uniform);
        }
        else
        {
            layout->bindUniform(m_defaultShininessUniform);
        }
    }

    void ShininessSystem::cleanupCacheComponents(entt::registry* registry)
    {
        auto view = registry->view<CacheShininess>();
        for (auto entity : view)
        {
            if (!registry->all_of<ShininessComponent>(entity))
            {
                registry->remove<CacheShininess>(entity);
            }
        }
    }
}
