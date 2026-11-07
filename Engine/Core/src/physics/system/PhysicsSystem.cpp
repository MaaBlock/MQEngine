/**
 * @file PhysicsSystem.cpp
 * @brief MQEngine 物理系统实现，管理 ECS 物理组件与 PhysX 场景双向同步
 */
#include "PhysicsSystem.h"
#include "../../scene/manager/DataManager.h"
#include "../../scene/core/Scene.h"
#include "../../render/data/Camera.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace MQEngine {

    PhysicsSystem::PhysicsSystem(DataManager* dataManager)
        : m_dataManager(dataManager)
    {
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    void PhysicsSystem::updateLogic()
    {
    }

    void PhysicsSystem::onActivate()
    {
    }

    void PhysicsSystem::onDeactivate()
    {
    }

    void PhysicsSystem::init(const FCT::Vec3<>& gravity)
    {
        if (m_initialized) {
            return;
        }

        if (!PhysicsCommon::get().init()) {
            spdlog::error("[PhysicsSystem] 初始化 PhysicsCommon 失败");
            return;
        }

        PhysicsSceneDesc desc;
        desc.gravity = gravity;
        m_physicsScene = makeUnique<PhysicsScene>();
        if (!m_physicsScene->init(desc)) {
            spdlog::error("[PhysicsSystem] 初始化 PhysicsScene 失败");
            m_physicsScene.reset();
            return;
        }

        m_initialized = true;
        spdlog::info("[PhysicsSystem] 物理系统初始化成功");
    }

    void PhysicsSystem::bindRegistry(entt::registry* registry)
    {
        if (!registry || m_boundRegistries.find(registry) != m_boundRegistries.end()) {
            return;
        }
        registry->on_destroy<CachePhysicsActor>().connect<&PhysicsSystem::onActorComponentDestroyed>(this);
        m_boundRegistries.insert(registry);
    }

    void PhysicsSystem::unbindRegistry(entt::registry* registry)
    {
        if (!registry || m_boundRegistries.find(registry) == m_boundRegistries.end()) {
            return;
        }
        registry->on_destroy<CachePhysicsActor>().disconnect<&PhysicsSystem::onActorComponentDestroyed>(this);
        m_boundRegistries.erase(registry);
    }

    void PhysicsSystem::onActorComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        auto* cache = registry.try_get<CachePhysicsActor>(entity);
        if (cache && cache->actor) {
            if (m_physicsScene) {
                m_physicsScene->removeActor(*cache->actor);
            }
            cache->actor->release();
            cache->actor = nullptr;
            cache->shape = nullptr;
        }
        m_trackedActors.erase({&registry, entity});
    }

    void PhysicsSystem::shutdown()
    {
        if (!m_initialized) {
            return;
        }

        for (auto* reg : m_boundRegistries) {
            if (reg) {
                reg->on_destroy<CachePhysicsActor>().disconnect<&PhysicsSystem::onActorComponentDestroyed>(this);
            }
        }
        m_boundRegistries.clear();

        for (auto& [key, actor] : m_trackedActors) {
            if (actor) {
                if (m_physicsScene) {
                    m_physicsScene->removeActor(*actor);
                }
                actor->release();
            }
        }
        m_trackedActors.clear();

        if (m_physicsScene) {
            m_physicsScene->shutdown();
            m_physicsScene.reset();
        }

        m_collisionCallbacks.clear();
        m_triggerCallbacks.clear();
        m_initialized = false;
        spdlog::info("[PhysicsSystem] 物理系统释放完成");
    }

    void PhysicsSystem::fixedTick(float fixedDeltaTime, entt::registry* registry)
    {
        if (!m_initialized || !m_physicsScene || !m_physicsScene->isValid() || fixedDeltaTime <= 0.0f) {
            return;
        }

        std::vector<entt::registry*> registriesToTick;
        if (registry) {
            registriesToTick.push_back(registry);
        } else if (m_dataManager) {
            registriesToTick = m_dataManager->currentRegistries();
        }

        if (registriesToTick.empty()) {
            return;
        }

        for (auto* reg : registriesToTick) {
            if (!reg) continue;
            bindRegistry(reg);
            cleanupRemovedEntities(reg);
            syncNewEntities(reg);
            pushKinematicTransforms(reg);
        }

        m_physicsScene->simulate(fixedDeltaTime);
        m_physicsScene->fetchResults(true);

        for (auto* reg : registriesToTick) {
            if (!reg) continue;
            pullDynamicTransforms(reg);
        }
        dispatchEvents();
    }

    void PhysicsSystem::syncNewEntities(entt::registry* registry)
    {
        physx::PxPhysics* physics = PhysicsCommon::get().getPhysics();
        if (!physics) {
            return;
        }

        auto processEntity = [&](entt::entity entity) {
            if (registry->all_of<CachePhysicsActor>(entity)) {
                return;
            }
            if (!registry->all_of<PositionComponent>(entity)) {
                return;
            }

            const auto& pos = registry->get<PositionComponent>(entity);
            FCT::Vec3<> rotEuler{0.0f, 0.0f, 0.0f};
            if (registry->all_of<RotationComponent>(entity)) {
                rotEuler = registry->get<RotationComponent>(entity).rotation;
            }

            physx::PxQuat qx(rotEuler.x, physx::PxVec3(1.0f, 0.0f, 0.0f));
            physx::PxQuat qy(rotEuler.y, physx::PxVec3(0.0f, 1.0f, 0.0f));
            physx::PxQuat qz(rotEuler.z, physx::PxVec3(0.0f, 0.0f, 1.0f));
            physx::PxQuat rot = qx * qy * qz;
            physx::PxTransform transform(physx::PxVec3(pos.position.x, pos.position.y, pos.position.z), rot);

            physx::PxRigidActor* actor = nullptr;
            RigidBodyType bodyType = RigidBodyType::Static;
            bool isKinematic = false;

            if (registry->all_of<RigidBodyComponent>(entity)) {
                const auto& rb = registry->get<RigidBodyComponent>(entity);
                bodyType = rb.bodyType;
                if (bodyType == RigidBodyType::Static) {
                    actor = physics->createRigidStatic(transform);
                } else if (bodyType == RigidBodyType::Dynamic) {
                    auto* dynamicActor = physics->createRigidDynamic(transform);
                    if (dynamicActor) {
                        dynamicActor->setLinearDamping(rb.linearDamping);
                        dynamicActor->setAngularDamping(rb.angularDamping);
                        dynamicActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !rb.useGravity);
                        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
                        dynamicActor->setSolverIterationCounts(8, 2);

                        physx::PxRigidDynamicLockFlags lockFlags;
                        if (rb.freezePositionX) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
                        if (rb.freezePositionY) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
                        if (rb.freezePositionZ) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
                        if (rb.freezeRotationX) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
                        if (rb.freezeRotationY) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
                        if (rb.freezeRotationZ) lockFlags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
                        dynamicActor->setRigidDynamicLockFlags(lockFlags);

                        if (rb.linearVelocity.length() > 1e-4f) {
                            dynamicActor->setLinearVelocity(physx::PxVec3(rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z));
                        }
                        if (rb.angularVelocity.length() > 1e-4f) {
                            dynamicActor->setAngularVelocity(physx::PxVec3(rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z));
                        }
                    }
                    actor = dynamicActor;
                } else if (bodyType == RigidBodyType::Kinematic) {
                    auto* dynamicActor = physics->createRigidDynamic(transform);
                    if (dynamicActor) {
                        dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
                    }
                    actor = dynamicActor;
                    isKinematic = true;
                }
            } else {
                actor = physics->createRigidStatic(transform);
                bodyType = RigidBodyType::Static;
            }

            if (!actor) {
                return;
            }

            physx::PxShape* shape = createShapeForEntity(registry, entity, actor);

            if (bodyType == RigidBodyType::Dynamic && actor->is<physx::PxRigidDynamic>() && shape) {
                float mass = 1.0f;
                if (registry->all_of<RigidBodyComponent>(entity)) {
                    mass = registry->get<RigidBodyComponent>(entity).mass;
                    if (mass <= 0.0f) {
                        mass = 1.0f;
                    }
                }
                physx::PxRigidBodyExt::updateMassAndInertia(*actor->is<physx::PxRigidDynamic>(), mass);
            }

            actor->userData = encodePhysicsUserData(entity);
            if (shape) {
                shape->userData = encodePhysicsUserData(entity);
            }

            m_physicsScene->addActor(*actor);
            registry->emplace<CachePhysicsActor>(entity, CachePhysicsActor{actor, shape, bodyType, isKinematic, 0});
            m_trackedActors[{registry, entity}] = actor;
        };

        for (auto e : registry->view<BoxColliderComponent>()) {
            processEntity(e);
        }
        for (auto e : registry->view<SphereColliderComponent>()) {
            processEntity(e);
        }
        for (auto e : registry->view<CapsuleColliderComponent>()) {
            processEntity(e);
        }
        for (auto e : registry->view<RigidBodyComponent>()) {
            processEntity(e);
        }
    }

    physx::PxShape* PhysicsSystem::createShapeForEntity(entt::registry* registry, entt::entity entity, physx::PxRigidActor* actor)
    {
        physx::PxMaterial* material = resolveMaterial(registry, entity);
        if (!material) {
            material = PhysicsCommon::get().getDefaultMaterial();
        }
        if (!material) {
            return nullptr;
        }

        physx::PxShape* shape = nullptr;

        if (registry->all_of<BoxColliderComponent>(entity)) {
            const auto& box = registry->get<BoxColliderComponent>(entity);
            FCT::Vec3<> ext = box.halfExtents;
            if (registry->all_of<ScaleComponent>(entity)) {
                const auto& scale = registry->get<ScaleComponent>(entity).scale;
                ext.x *= std::abs(scale.x);
                ext.y *= std::abs(scale.y);
                ext.z *= std::abs(scale.z);
            }
            float hx = (std::max)(0.001f, ext.x);
            float hy = (std::max)(0.001f, ext.y);
            float hz = (std::max)(0.001f, ext.z);
            physx::PxBoxGeometry geom(hx, hy, hz);
            shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
            if (shape) {
                shape->setLocalPose(physx::PxTransform(physx::PxVec3(box.center.x, box.center.y, box.center.z)));
                physx::PxFilterData fd(0xFFFFFFFF, 0, 0, 0);
                shape->setSimulationFilterData(fd);
                shape->setQueryFilterData(fd);
                if (box.isTrigger) {
                    shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
                    shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
                }
            }
        } else if (registry->all_of<SphereColliderComponent>(entity)) {
            const auto& sphere = registry->get<SphereColliderComponent>(entity);
            float r = sphere.radius;
            if (registry->all_of<ScaleComponent>(entity)) {
                const auto& scale = registry->get<ScaleComponent>(entity).scale;
                float maxScale = (std::max)({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
                r *= maxScale;
            }
            r = (std::max)(0.001f, r);
            physx::PxSphereGeometry geom(r);
            shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
            if (shape) {
                shape->setLocalPose(physx::PxTransform(physx::PxVec3(sphere.center.x, sphere.center.y, sphere.center.z)));
                physx::PxFilterData fd(0xFFFFFFFF, 0, 0, 0);
                shape->setSimulationFilterData(fd);
                shape->setQueryFilterData(fd);
                if (sphere.isTrigger) {
                    shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
                    shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
                }
            }
        } else if (registry->all_of<CapsuleColliderComponent>(entity)) {
            const auto& capsule = registry->get<CapsuleColliderComponent>(entity);
            float r = capsule.radius;
            float hh = capsule.halfHeight;
            if (registry->all_of<ScaleComponent>(entity)) {
                const auto& scale = registry->get<ScaleComponent>(entity).scale;
                r *= (std::max)(std::abs(scale.x), std::abs(scale.z));
                hh *= std::abs(scale.y);
            }
            r = (std::max)(0.001f, r);
            hh = (std::max)(0.001f, hh);
            physx::PxCapsuleGeometry geom(r, hh);
            shape = physx::PxRigidActorExt::createExclusiveShape(*actor, geom, *material);
            if (shape) {
                // Note: PhysX 胶囊体沿局部 X 轴建立，MQEngine 为 Y 轴朝上，需绕 Z 轴旋转 90 度修正
                physx::PxQuat rotZ90(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f));
                shape->setLocalPose(physx::PxTransform(physx::PxVec3(capsule.center.x, capsule.center.y, capsule.center.z), rotZ90));
                physx::PxFilterData fd(0xFFFFFFFF, 0, 0, 0);
                shape->setSimulationFilterData(fd);
                shape->setQueryFilterData(fd);
                if (capsule.isTrigger) {
                    shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
                    shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
                }
            }
        }
        return shape;
    }

    physx::PxMaterial* PhysicsSystem::resolveMaterial(entt::registry* registry, entt::entity entity)
    {
        if (registry->all_of<PhysicsMaterialComponent>(entity)) {
            const auto& mat = registry->get<PhysicsMaterialComponent>(entity);
            return PhysicsCommon::get().createMaterial(mat.staticFriction, mat.dynamicFriction, mat.restitution);
        }
        return PhysicsCommon::get().getDefaultMaterial();
    }

    void PhysicsSystem::cleanupRemovedEntities(entt::registry* registry)
    {
        if (!registry) {
            return;
        }

        std::vector<entt::entity> toRemove;
        for (const auto& [key, actor] : m_trackedActors) {
            if (key.first != registry) {
                continue;
            }
            entt::entity entity = key.second;
            if (!registry->valid(entity)) {
                toRemove.push_back(entity);
            } else {
                bool hasColliderOrRb = registry->any_of<BoxColliderComponent,
                                                        SphereColliderComponent,
                                                        CapsuleColliderComponent,
                                                        RigidBodyComponent>(entity);
                if (!hasColliderOrRb) {
                    toRemove.push_back(entity);
                }
            }
        }

        for (auto entity : toRemove) {
            auto it = m_trackedActors.find({registry, entity});
            physx::PxRigidActor* actor = (it != m_trackedActors.end()) ? it->second : nullptr;
            if (it != m_trackedActors.end()) {
                m_trackedActors.erase(it);
            }

            if (registry->valid(entity) && registry->all_of<CachePhysicsActor>(entity)) {
                auto& cache = registry->get<CachePhysicsActor>(entity);
                cache.actor = nullptr;
                cache.shape = nullptr;
                registry->remove<CachePhysicsActor>(entity);
            }

            if (actor) {
                if (m_physicsScene) {
                    m_physicsScene->removeActor(*actor);
                }
                actor->release();
            }
        }
    }

    void PhysicsSystem::pushKinematicTransforms(entt::registry* registry)
    {
        auto view = registry->view<CachePhysicsActor, PositionComponent, RotationComponent>();
        for (auto entity : view) {
            const auto& cache = view.get<CachePhysicsActor>(entity);
            if (!cache.isKinematic || !cache.actor) {
                continue;
            }
            auto* dynamicActor = cache.actor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) {
                continue;
            }

            const auto& pos = view.get<PositionComponent>(entity);
            const auto& rot = view.get<RotationComponent>(entity);

            physx::PxQuat qx(rot.rotation.x, physx::PxVec3(1.0f, 0.0f, 0.0f));
            physx::PxQuat qy(rot.rotation.y, physx::PxVec3(0.0f, 1.0f, 0.0f));
            physx::PxQuat qz(rot.rotation.z, physx::PxVec3(0.0f, 0.0f, 1.0f));
            physx::PxQuat q = qx * qy * qz;
            physx::PxTransform targetPose(physx::PxVec3(pos.position.x, pos.position.y, pos.position.z), q);

            dynamicActor->setKinematicTarget(targetPose);
        }
    }

    void PhysicsSystem::pullDynamicTransforms(entt::registry* registry)
    {
        auto view = registry->view<CachePhysicsActor, PositionComponent, RotationComponent>();
        for (auto entity : view) {
            const auto& cache = view.get<CachePhysicsActor>(entity);
            if (cache.isKinematic || cache.bodyType != RigidBodyType::Dynamic || !cache.actor) {
                continue;
            }
            auto* dynamicActor = cache.actor->is<physx::PxRigidDynamic>();
            if (!dynamicActor) {
                continue;
            }

            physx::PxTransform pose = dynamicActor->getGlobalPose();
            auto& pos = view.get<PositionComponent>(entity);
            auto& rot = view.get<RotationComponent>(entity);

            pos.position = FCT::Vec3<>(pose.p.x, pose.p.y, pose.p.z);

            // Note: 将四元数转化为欧拉角，严格匹配 MatrixCacheSystem 内部旋转约定 R = Rx * Ry * Rz
            physx::PxMat33 mat(pose.q);
            float rotY = std::asin(std::clamp(mat.column2.x, -1.0f, 1.0f));
            float rotX = 0.0f;
            float rotZ = 0.0f;
            if (std::abs(std::cos(rotY)) > 1e-4f) {
                rotX = std::atan2(-mat.column2.y, mat.column2.z);
                rotZ = std::atan2(-mat.column1.x, mat.column0.x);
            } else {
                rotX = std::atan2(mat.column1.z, mat.column1.y);
                rotZ = 0.0f;
            }
            rot.rotation = FCT::Vec3<>(rotX, rotY, rotZ);

            if (registry->all_of<RigidBodyComponent>(entity)) {
                auto& rb = registry->get<RigidBodyComponent>(entity);
                physx::PxVec3 linVel = dynamicActor->getLinearVelocity();
                rb.linearVelocity = FCT::Vec3<>(linVel.x, linVel.y, linVel.z);
                physx::PxVec3 angVel = dynamicActor->getAngularVelocity();
                rb.angularVelocity = FCT::Vec3<>(angVel.x, angVel.y, angVel.z);
            }
        }
    }

    void PhysicsSystem::dispatchEvents()
    {
        if (!m_physicsScene) {
            return;
        }

        const auto& collisionEvents = m_physicsScene->getCollisionEvents();
        if (!m_collisionCallbacks.empty()) {
            for (const auto& evt : collisionEvents) {
                for (const auto& cb : m_collisionCallbacks) {
                    cb(evt);
                }
            }
        }

        const auto& triggerEvents = m_physicsScene->getTriggerEvents();
        if (!m_triggerCallbacks.empty()) {
            for (const auto& evt : triggerEvents) {
                for (const auto& cb : m_triggerCallbacks) {
                    cb(evt);
                }
            }
        }
    }

    bool PhysicsSystem::raycast(const FCT::Vec3<>& origin, const FCT::Vec3<>& dir, float maxDistance, RaycastHit& outHit, uint32_t layerMask)
    {
        if (!m_physicsScene) {
            outHit.hasHit = false;
            return false;
        }
        return m_physicsScene->raycast(origin, dir, maxDistance, outHit, layerMask);
    }

    std::vector<RaycastHit> PhysicsSystem::raycastAll(const FCT::Vec3<>& origin, const FCT::Vec3<>& dir, float maxDistance, uint32_t layerMask)
    {
        if (!m_physicsScene) {
            return {};
        }
        return m_physicsScene->raycastAll(origin, dir, maxDistance, layerMask);
    }

    void PhysicsSystem::registerCollisionCallback(CollisionCallback cb)
    {
        m_collisionCallbacks.push_back(std::move(cb));
    }

    void PhysicsSystem::registerTriggerCallback(TriggerCallback cb)
    {
        m_triggerCallbacks.push_back(std::move(cb));
    }

    void PhysicsSystem::setLinearVelocity(entt::registry* registry, entt::entity entity, const FCT::Vec3<>& velocity)
    {
        if (!registry || !registry->valid(entity) || !registry->all_of<CachePhysicsActor>(entity)) {
            return;
        }
        auto& cache = registry->get<CachePhysicsActor>(entity);
        if (cache.actor) {
            if (auto* dynamicActor = cache.actor->is<physx::PxRigidDynamic>()) {
                dynamicActor->setLinearVelocity(physx::PxVec3(velocity.x, velocity.y, velocity.z));
            }
        }
    }

    FCT::Vec3<> PhysicsSystem::getLinearVelocity(entt::registry* registry, entt::entity entity) const
    {
        if (!registry || !registry->valid(entity) || !registry->all_of<CachePhysicsActor>(entity)) {
            return {0.0f, 0.0f, 0.0f};
        }
        const auto& cache = registry->get<CachePhysicsActor>(entity);
        if (cache.actor) {
            if (auto* dynamicActor = cache.actor->is<physx::PxRigidDynamic>()) {
                auto v = dynamicActor->getLinearVelocity();
                return {v.x, v.y, v.z};
            }
        }
        return {0.0f, 0.0f, 0.0f};
    }

    void PhysicsSystem::addForce(entt::registry* registry, entt::entity entity, const FCT::Vec3<>& force)
    {
        if (!registry || !registry->valid(entity) || !registry->all_of<CachePhysicsActor>(entity)) {
            return;
        }
        auto& cache = registry->get<CachePhysicsActor>(entity);
        if (cache.actor) {
            if (auto* dynamicActor = cache.actor->is<physx::PxRigidDynamic>()) {
                dynamicActor->addForce(physx::PxVec3(force.x, force.y, force.z));
            }
        }
    }

} // namespace MQEngine
