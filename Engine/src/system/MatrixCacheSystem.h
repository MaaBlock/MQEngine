//
// Created by Administrator on 2025/1/21.
//

#ifndef MATRIXCACHESYSTEM_H
#define MATRIXCACHESYSTEM_H
#include "../data/DataManager.h"
#include "../data/Camera.h"
#include "./ISystem.h"
namespace MQEngine {
    class ENGINE_API MatrixCacheSystem : public ISystem {
    public:
        MatrixCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        void updateLogic();
        void updateRender();
        void bindModelMatrix(entt::registry* registry, entt::entity entity, FCT::Layout* layout);
        
    private:
        void processEntity(entt::registry* registry, entt::entity entity);
        void cleanupCacheComponents(entt::registry* registry);
        FCT::Mat4 calculateRotationMatrix(const RotationComponent& rotation);
        FCT::Mat4 calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::Uniform m_defaultModelUniform;  // 默认模型矩阵uniform，用于没有矩阵cache的情况
    };
}

#endif //MATRIXCACHESYSTEM_H