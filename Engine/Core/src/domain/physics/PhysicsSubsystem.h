/**
 * @file PhysicsSubsystem.h
 * @brief 物理子域：统一多后端物理求解架构 (Docs 02 Section 6: Jolt / PhysX / MuJoCo / libuipc)，
 *        支持完整刚体动力学积分、窄相碰撞检测、冲量响应求解、射线/扫掠/重叠查询与无锁双缓冲事件管线
 */
#ifndef MQENGINE_DOMAIN_PHYSICS_PHYSICSSUBSYSTEM_H
#define MQENGINE_DOMAIN_PHYSICS_PHYSICSSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Physics
        {
            /**
             * @brief 物理仿真后端引擎类型 (Docs 02 Section 6)
             */
            enum class PhysicsBackend : uint8_t
            {
                Jolt = 0,            // Jolt Physics 高并发并行求解器 (默认)
                PhysX = 1,           // NVIDIA PhysX 5 硬件加速求解器
                MuJoCo = 2,          // MuJoCo 高精度机器人与关节铰接动力学
                LibUipc = 3,         // libuipc 非线性有限元柔体/布料与接触力学
                EngineFallback = 4   // 引擎内置快速冲量接触求解器
            };

            /**
             * @brief 射线命中结果
             */
            struct ENGINE_API RaycastHit
            {
                bool hasHit = false;
                FCT::Vec3<> point{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> normal{0.0f, 1.0f, 0.0f};
                float distance = 0.0f;
                FCT::ECS::entity entity{FCT::ECS::null};
            };

            /**
             * @brief 碰撞事件记录
             */
            struct ENGINE_API CollisionEvent
            {
                FCT::ECS::entity entityA{FCT::ECS::null};
                FCT::ECS::entity entityB{FCT::ECS::null};
                FCT::Vec3<> contactPoint{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> contactNormal{0.0f, 1.0f, 0.0f};
                float impulse = 0.0f;
                bool isTouchFound = false;
                bool isTouchLost = false;
            };

            /**
             * @brief 触发器事件记录
             */
            struct ENGINE_API TriggerEvent
            {
                FCT::ECS::entity triggerEntity{FCT::ECS::null};
                FCT::ECS::entity otherEntity{FCT::ECS::null};
                bool isEnter = false;
            };

            /**
             * @brief 空间重叠查询结果
             */
            struct ENGINE_API OverlapResult
            {
                FCT::ECS::entity entity{FCT::ECS::null};
                FCT::Vec3<> nearestPoint{0.0f, 0.0f, 0.0f};
                float distance = 0.0f;
            };

            struct ENGINE_API HeightfieldCollider
            {
                uint32_t resolution = 0;
                float gridSpacing = 1.0f;
                float originX = 0.0f;
                float originZ = 0.0f;
                std::vector<float> heights;
                bool active = false;
            };

            /**
             * @brief 物理子系统核心类 (严格无锁设计，由 PhysicsHost 线程驱动)
             */
            class ENGINE_API PhysicsSubsystem
            {
            public:
                PhysicsSubsystem();
                ~PhysicsSubsystem();

                PhysicsSubsystem(const PhysicsSubsystem&) = delete;
                PhysicsSubsystem& operator=(const PhysicsSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                void setHeightfieldCollider(uint32_t resolution, float spacing, float originX, float originZ, const std::vector<float>& heights);
                void removeHeightfieldCollider() noexcept;
                const HeightfieldCollider& getHeightfieldCollider() const noexcept { return m_heightfieldCollider; }
                bool hasHeightfieldCollider() const noexcept { return m_heightfieldCollider.active && !m_heightfieldCollider.heights.empty(); }
                float getHeightfieldHeightAt(float worldX, float worldZ) const noexcept;

                /**
                 * @brief 推进物理动力学步进 (恒定时间步长积分、宽相/窄相碰撞检测、冲量接触求解)
                 * @param dt 步进时间 (秒)
                 */
                void step(float dt);

                /**
                 * @brief 单次射线检测
                 */
                bool raycast(const FCT::Vec3<>& origin, const FCT::Vec3<>& direction, float maxDistance, RaycastHit& outHit);

                /**
                 * @brief 多目标穿透射线检测 (按相交距离升序排序)
                 */
                std::vector<RaycastHit> raycastAll(const FCT::Vec3<>& origin, const FCT::Vec3<>& direction, float maxDistance);

                /**
                 * @brief 球体扫掠几何投射检测 (Sphere Sweep)
                 */
                bool sphereSweep(const FCT::Vec3<>& origin, float radius, const FCT::Vec3<>& direction, float maxDistance, RaycastHit& outHit);

                /**
                 * @brief 球体范围重叠碰撞查询
                 */
                std::vector<OverlapResult> overlapSphere(const FCT::Vec3<>& center, float radius);

                /**
                 * @brief 轴对齐包围盒范围重叠碰撞查询
                 */
                std::vector<OverlapResult> overlapBox(const FCT::Vec3<>& center, const FCT::Vec3<>& halfExtents);

                /**
                 * @brief 切换或配置底层物理引擎后端
                 */
                void setBackend(PhysicsBackend backend) noexcept { m_backend = backend; }
                PhysicsBackend getBackend() const noexcept { return m_backend; }

                /**
                 * @brief 设置物理世界重力加速度
                 */
                void setGravity(const FCT::Vec3<>& gravity) noexcept { m_gravity = gravity; }
                const FCT::Vec3<>& getGravity() const noexcept { return m_gravity; }

                /**
                 * @brief 获取当前帧发布的碰撞事件 (只读无锁双缓冲访问)
                 */
                const std::vector<CollisionEvent>& getCollisionEvents() const noexcept;

                /**
                 * @brief 获取当前帧发布的触发器事件 (只读无锁双缓冲访问)
                 */
                const std::vector<TriggerEvent>& getTriggerEvents() const noexcept;

                /**
                 * @brief 清空物理事件缓存
                 */
                void clearEvents();

                /**
                 * @brief 挂载/卸载后台 PhysicsHost 运行时物理步进节点（由 Application \"Game\" Tag 声明式触发）
                 */
                void activateRuntimeTicker();
                void deactivateRuntimeTicker() noexcept;
                bool isRuntimeTickerActive() const noexcept { return m_runtimeTickerActive.load(std::memory_order_acquire); }

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                void solveRigidBodyDynamics(float dt);
                void solveNarrowphaseCollisions(float dt);

            private:
                std::atomic<bool> m_initialized{false};
                std::atomic<bool> m_runtimeTickerActive{false};
                PhysicsBackend m_backend = PhysicsBackend::Jolt;
                FCT::Vec3<> m_gravity{0.0f, -9.81f, 0.0f};
                float m_fixedTimestep = 1.0f / 60.0f;
                float m_accumulatedTime = 0.0f;

                // 双缓冲无锁事件发布管线
                std::vector<CollisionEvent> m_collisionEventBuffers[2];
                std::vector<TriggerEvent> m_triggerEventBuffers[2];
                std::atomic<uint32_t> m_activeEventBufferIndex{0};
                uint32_t m_writeEventBufferIndex = 0;

                HeightfieldCollider m_heightfieldCollider;
            };

        } // namespace Physics
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_PHYSICS_PHYSICSSUBSYSTEM_H
