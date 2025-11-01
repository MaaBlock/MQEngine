//
// Created by MQ on 2025/9/15.
//

#ifndef SHININESSSYSTEM_H
#define SHININESSSYSTEM_H
#include "../data/DataManager.h"
#include "../data/Camera.h"

namespace MQEngine {
    class ENGINE_API ShininessSystem {
    public:
        ShininessSystem(FCT::Context* ctx, DataManager* dataManager);
        void update();
        void updateUniforms();
        void bindShininess(entt::registry* registry, entt::entity entity, FCT::Layout* layout);

    private:
        void processEntity(entt::registry* registry, entt::entity entity);
        void cleanupCacheComponents(entt::registry* registry);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::Uniform m_defaultShininessUniform;
    };
}

#endif //SHININESSSYSTEM_H
