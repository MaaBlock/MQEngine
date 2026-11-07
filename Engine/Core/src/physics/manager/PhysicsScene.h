/**
 * @file PhysicsScene.h
 * @brief PhysX 5.5.0 场景模拟、多线程调度与射线检测封装
 */
#ifndef ENGINE_PHYSICS_SCENE_H
#define ENGINE_PHYSICS_SCENE_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../data/PhysicsCommon.h"

namespace MQEngine {

    /**
     * @brief 射线命中结果
     */
    struct ENGINE_API RaycastHit {
        bool hasHit = false;
        FCT::Vec3<> position{0.0f, 0.0f, 0.0f};
        FCT::Vec3<> point{0.0f, 0.0f, 0.0f}; // 别名支持
        FCT::Vec3<> normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
        physx::PxRigidActor* actor = nullptr;
        physx::PxShape* shape = nullptr;
        entt::entity entity = entt::null;
    };

    /**
     * @brief 触点几何数据
     */
    struct ContactPointInfo {
        FCT::Vec3<> position{0.0f, 0.0f, 0.0f};
        FCT::Vec3<> normal{0.0f, 1.0f, 0.0f};
        float separation = 0.0f;
        float impulse = 0.0f;
    };

    /**
     * @brief 碰撞事件记录
     */
    struct CollisionEvent {
        entt::entity entityA = entt::null;
        entt::entity entityB = entt::null;
        physx::PxActor* actorA = nullptr;
        physx::PxActor* actorB = nullptr;
        FCT::Vec3<> contactPoint{0.0f, 0.0f, 0.0f};
        FCT::Vec3<> contactNormal{0.0f, 1.0f, 0.0f};
        float impulse = 0.0f;
        bool isTouchFound = false;
        bool isTouchLost = false;
        std::vector<ContactPointInfo> contacts;
    };

    /**
     * @brief 触发器事件记录
     */
    struct TriggerEvent {
        entt::entity triggerEntity = entt::null;
        entt::entity otherEntity = entt::null;
        physx::PxActor* triggerActor = nullptr;
        physx::PxActor* otherActor = nullptr;
        bool isEnter = false;
    };

    /**
     * @brief 物理场景创建描述符
     */
    struct PhysicsSceneDesc {
        FCT::Vec3<> gravity{0.0f, -9.81f, 0.0f};
        uint32_t numWorkerThreads = 2;
        bool enableCCD = true;
        bool enablePCM = true;
        bool enableActiveActors = true;
    };

    /**
     * @brief 模拟事件侦听转发器
     */
    class PhysicsSceneSimulationCallback : public physx::PxSimulationEventCallback {
    public:
        virtual ~PhysicsSceneSimulationCallback() = default;
        virtual void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override {}
        virtual void onWake(physx::PxActor** actors, physx::PxU32 count) override {}
        virtual void onSleep(physx::PxActor** actors, physx::PxU32 count) override {}
        virtual void onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, physx::PxU32 nbPairs) override;
        virtual void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;
        virtual void onAdvance(const physx::PxRigidBody* const* bodyBuffer, const physx::PxTransform* poseBuffer, const physx::PxU32 count) override {}

        void clearEvents();
        const std::vector<CollisionEvent>& getCollisionEvents() const { return m_collisionEvents; }
        const std::vector<TriggerEvent>& getTriggerEvents() const { return m_triggerEvents; }

    private:
        std::vector<CollisionEvent> m_collisionEvents;
        std::vector<TriggerEvent> m_triggerEvents;
    };

    /**
     * @brief 物理场景核心类
     */
    class ENGINE_API PhysicsScene {
    public:
        PhysicsScene();
        ~PhysicsScene();

        PhysicsScene(const PhysicsScene&) = delete;
        PhysicsScene& operator=(const PhysicsScene&) = delete;
        PhysicsScene(PhysicsScene&& other) noexcept;
        PhysicsScene& operator=(PhysicsScene&& other) noexcept;

        /**
         * @brief 初始化物理场景
         * @param desc 场景描述参数
         * @return 成功返回 true
         */
        bool init(const PhysicsSceneDesc& desc = PhysicsSceneDesc{});

        /**
         * @brief 彻底释放物理场景及 CPU 调度器
         */
        void shutdown();

        /**
         * @brief 场景是否有效
         */
        bool isValid() const { return m_scene != nullptr; }

        /**
         * @brief 推进物理模拟步进
         * @param dt 时间增量
         */
        void simulate(float dt);

        /**
         * @brief 同步/非阻塞提取模拟结果
         * @param block 是否阻塞等待
         * @return 结果已就绪返回 true
         */
        bool fetchResults(bool block = true);

        /**
         * @brief 是否正在执行物理仿真
         */
        bool isSimulating() const { return m_isSimulating; }

        /**
         * @brief 设置场景重力加速度
         */
        void setGravity(const FCT::Vec3<>& gravity);

        /**
         * @brief 获取场景重力加速度
         */
        FCT::Vec3<> getGravity() const;

        /**
         * @brief 向场景添加 Actor
         */
        void addActor(physx::PxActor& actor);

        /**
         * @brief 从场景移除 Actor
         */
        void removeActor(physx::PxActor& actor);

        /**
         * @brief 单次射线检测
         */
        bool raycast(const FCT::Vec3<>& origin, const FCT::Vec3<>& unitDir, float maxDistance, RaycastHit& outHit, uint32_t layerMask = 0xFFFFFFFF);

        /**
         * @brief 多目标穿透射线检测，按距离升序排列
         */
        std::vector<RaycastHit> raycastAll(const FCT::Vec3<>& origin, const FCT::Vec3<>& unitDir, float maxDistance, uint32_t layerMask = 0xFFFFFFFF);

        /**
         * @brief 动态刚体快速创建
         */
        physx::PxRigidDynamic* createRigidDynamic(const FCT::Vec3<>& position, const FCT::Vec3<>& rotationEuler = FCT::Vec3<>(0.0f, 0.0f, 0.0f));

        /**
         * @brief 静态刚体快速创建
         */
        physx::PxRigidStatic* createRigidStatic(const FCT::Vec3<>& position, const FCT::Vec3<>& rotationEuler = FCT::Vec3<>(0.0f, 0.0f, 0.0f));

        /**
         * @brief 获取底层 PxScene
         */
        physx::PxScene* getPxScene() const { return m_scene; }

        /**
         * @brief 获取本帧发生的所有碰撞事件
         */
        const std::vector<CollisionEvent>& getCollisionEvents() const;

        /**
         * @brief 获取本帧发生的所有触发器事件
         */
        const std::vector<TriggerEvent>& getTriggerEvents() const;

        /**
         * @brief 清空事件缓存
         */
        void clearEvents();

    private:
        physx::PxScene* m_scene = nullptr;
        physx::PxDefaultCpuDispatcher* m_dispatcher = nullptr;
        UniquePtr<PhysicsSceneSimulationCallback> m_eventCallback;
        bool m_isSimulating = false;
        FCT::Vec3<> m_gravity{0.0f, -9.81f, 0.0f};
    };

} // namespace MQEngine

#endif // ENGINE_PHYSICS_SCENE_H
