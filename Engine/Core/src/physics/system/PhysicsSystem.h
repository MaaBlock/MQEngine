/**
 * @file PhysicsSystem.h
 * @brief MQEngine 物理系统实现，管理 ECS 物理组件与 PhysX 场景双向同步
 */
#ifndef ENGINE_PHYSICS_SYSTEM_H
#define ENGINE_PHYSICS_SYSTEM_H

#include "../../runtime/system/ISystem.h"
#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../../scene/data/Component.h"
#include "../manager/PhysicsScene.h"
#include <functional>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>

namespace MQEngine {

    class DataManager;

    class ENGINE_API PhysicsSystem : public ISystem {
    public:
        explicit PhysicsSystem(DataManager* dataManager = nullptr);
        virtual ~PhysicsSystem() override;

        // ISystem 接口契约
        virtual void updateLogic() override;
        virtual void updateRender() override {}
        virtual void onActivate() override;
        virtual void onDeactivate() override;
        virtual std::vector<std::string> getSuccessors() override { return {"MatrixCacheSystem"}; }
        virtual std::vector<std::string> getPredecessors() override { return {}; }

        /**
         * @brief 初始化物理系统与底层物理场景
         * @param gravity 重力加速度
         */
        void init(const FCT::Vec3<>& gravity = {0.0f, -9.81f, 0.0f});

        /**
         * @brief 彻底释放物理系统与场景资源
         */
        void shutdown();

        /**
         * @brief 物理固定步长步进循环
         * @param fixedDeltaTime 固定步长时间 (默认 1/60s)
         * @param registry 指定的 ECS 注册表 (若为空则从 DataManager 获取主场景)
         */
        void fixedTick(float fixedDeltaTime, entt::registry* registry = nullptr);

        /**
         * @brief 单次世界射线检测
         */
        bool raycast(const FCT::Vec3<>& origin, const FCT::Vec3<>& dir, float maxDistance, RaycastHit& outHit, uint32_t layerMask = 0xFFFFFFFF);

        /**
         * @brief 多目标穿透世界射线检测 (按距离升序排序)
         */
        std::vector<RaycastHit> raycastAll(const FCT::Vec3<>& origin, const FCT::Vec3<>& dir, float maxDistance, uint32_t layerMask = 0xFFFFFFFF);

        // 事件回调订阅
        using CollisionCallback = std::function<void(const CollisionEvent&)>;
        using TriggerCallback = std::function<void(const TriggerEvent&)>;
        void registerCollisionCallback(CollisionCallback cb);
        void registerTriggerCallback(TriggerCallback cb);

        /**
         * @brief 绑定并监听指定注册表的组件销毁事件
         */
        void bindRegistry(entt::registry* registry);

        /**
         * @brief 解绑指定注册表
         */
        void unbindRegistry(entt::registry* registry);

        /**
         * @brief 门面方法：设置动态刚体线性速度
         */
        void setLinearVelocity(entt::registry* registry, entt::entity entity, const FCT::Vec3<>& velocity);

        /**
         * @brief 门面方法：获取刚体线性速度
         */
        FCT::Vec3<> getLinearVelocity(entt::registry* registry, entt::entity entity) const;

        /**
         * @brief 门面方法：施加外力或冲量
         */
        void addForce(entt::registry* registry, entt::entity entity, const FCT::Vec3<>& force);

        /**
         * @brief 获取物理场景指针
         */
        PhysicsScene* getScene() const { return m_physicsScene.get(); }

    private:
        void onActorComponentDestroyed(entt::registry& registry, entt::entity entity);
        void syncNewEntities(entt::registry* registry);
        void cleanupRemovedEntities(entt::registry* registry);
        void pushKinematicTransforms(entt::registry* registry);
        void pullDynamicTransforms(entt::registry* registry);
        void dispatchEvents();

        physx::PxShape* createShapeForEntity(entt::registry* registry, entt::entity entity, physx::PxRigidActor* actor);
        physx::PxMaterial* resolveMaterial(entt::registry* registry, entt::entity entity);

        DataManager* m_dataManager = nullptr;
        UniquePtr<PhysicsScene> m_physicsScene;
        std::vector<CollisionCallback> m_collisionCallbacks;
        std::vector<TriggerCallback> m_triggerCallbacks;
        std::unordered_set<entt::registry*> m_boundRegistries;
        std::map<std::pair<entt::registry*, entt::entity>, physx::PxRigidActor*> m_trackedActors;
        bool m_initialized = false;
    };

} // namespace MQEngine

#endif // ENGINE_PHYSICS_SYSTEM_H
