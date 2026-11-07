//
// Created by Administrator on 2025/1/21.
//

#ifndef MATRIXCACHESYSTEM_H
#define MATRIXCACHESYSTEM_H
#include "../../scene/manager/DataManager.h"
#include "../data/Camera.h"
#include "../../runtime/system/ISystem.h"
namespace MQEngine {
    /**
     * @brief 负责局部变换与场景图（SceneGraph）父子层级世界矩阵的缓存计算与渲染绑定
     */
    class ENGINE_API MatrixCacheSystem : public ISystem {
    public:
        MatrixCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        void updateLogic();
        void updateRender();
        void bindModelMatrix(entt::registry* registry, entt::entity entity, FCT::Layout* layout);

        FCT::Mat4 computeEntityLocalMatrix(entt::registry* registry, entt::entity entity);
        FCT::Mat4 computeEntityWorldMatrix(entt::registry* registry, entt::entity entity);
        FCT::Mat4 computeEntityWorldRotationMatrix(entt::registry* registry, entt::entity entity);
        FCT::Vec3<> computeEntityWorldPosition(entt::registry* registry, entt::entity entity);
        static FCT::Mat4 calculateRotationMatrix(const RotationComponent& rotation);
        static FCT::Mat4 calculateModelMatrix(const PositionComponent& position, const RotationComponent& rotation, const ScaleComponent& scale);
        
    private:
        void syncHierarchyLinks(entt::registry* registry);
        void collectTransformEntities(entt::registry* registry, std::set<entt::entity>& outEntities);
        void processEntity(entt::registry* registry, entt::entity entity);
        entt::entity resolveParentEntity(entt::registry* registry, entt::entity entity);
        FCT::Mat4 computeWorldMatrixRecursive(entt::registry* registry, entt::entity entity, int depth);
        FCT::Mat4 computeWorldRotationRecursive(entt::registry* registry, entt::entity entity, int depth);
        FCT::Mat4 calculateModelInverseTransposeMatrix(const FCT::Mat4& modelMatrix);
        void cleanupCacheComponents(entt::registry* registry);
        
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::Uniform m_defaultModelUniform;  // 默认模型矩阵uniform，用于没有矩阵cache的情况
    };
}

#endif //MATRIXCACHESYSTEM_H
