/**
 * @file PhysicsScene.cpp
 * @brief PhysX 5.5.0 场景模拟与查询实现
 */
#include "PhysicsScene.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace MQEngine {

    void PhysicsSceneSimulationCallback::onContact(
        const physx::PxContactPairHeader& pairHeader,
        const physx::PxContactPair* pairs,
        physx::PxU32 nbPairs)
    {
        for (physx::PxU32 i = 0; i < nbPairs; ++i) {
            const auto& cp = pairs[i];
            CollisionEvent event;
            event.actorA = pairHeader.actors[0];
            event.actorB = pairHeader.actors[1];

            if (event.actorA && event.actorA->userData) {
                event.entityA = decodePhysicsUserData(event.actorA->userData);
            }
            if (event.actorB && event.actorB->userData) {
                event.entityB = decodePhysicsUserData(event.actorB->userData);
            }

            event.isTouchFound = cp.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_FOUND);
            event.isTouchLost = cp.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_LOST);

            if (cp.contactCount > 0) {
                std::vector<physx::PxContactPairPoint> pts(cp.contactCount);
                cp.extractContacts(pts.data(), cp.contactCount);
                for (const auto& pt : pts) {
                    ContactPointInfo ptInfo;
                    ptInfo.position = FCT::Vec3<>(pt.position.x, pt.position.y, pt.position.z);
                    ptInfo.normal = FCT::Vec3<>(pt.normal.x, pt.normal.y, pt.normal.z);
                    ptInfo.separation = pt.separation;
                    ptInfo.impulse = pt.impulse.magnitude();
                    event.contacts.push_back(ptInfo);
                }
                if (!event.contacts.empty()) {
                    event.contactPoint = event.contacts.front().position;
                    event.contactNormal = event.contacts.front().normal;
                    event.impulse = event.contacts.front().impulse;
                }
            }
            m_collisionEvents.push_back(std::move(event));
        }
    }

    void PhysicsSceneSimulationCallback::onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count)
    {
        for (physx::PxU32 i = 0; i < count; ++i) {
            const auto& tp = pairs[i];
            TriggerEvent event;
            event.triggerActor = tp.triggerActor;
            event.otherActor = tp.otherActor;

            if (event.triggerActor && event.triggerActor->userData) {
                event.triggerEntity = decodePhysicsUserData(event.triggerActor->userData);
            }
            if (event.otherActor && event.otherActor->userData) {
                event.otherEntity = decodePhysicsUserData(event.otherActor->userData);
            }

            event.isEnter = (tp.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND);
            m_triggerEvents.push_back(std::move(event));
        }
    }

    void PhysicsSceneSimulationCallback::clearEvents()
    {
        m_collisionEvents.clear();
        m_triggerEvents.clear();
    }

    // -------------------------------------------------------------
    // PhysicsScene
    // -------------------------------------------------------------

    PhysicsScene::PhysicsScene() = default;

    PhysicsScene::~PhysicsScene()
    {
        shutdown();
    }

    PhysicsScene::PhysicsScene(PhysicsScene&& other) noexcept
        : m_scene(other.m_scene),
          m_dispatcher(other.m_dispatcher),
          m_eventCallback(std::move(other.m_eventCallback)),
          m_isSimulating(other.m_isSimulating),
          m_gravity(other.m_gravity)
    {
        other.m_scene = nullptr;
        other.m_dispatcher = nullptr;
        other.m_isSimulating = false;
    }

    PhysicsScene& PhysicsScene::operator=(PhysicsScene&& other) noexcept
    {
        if (this != &other) {
            shutdown();
            m_scene = other.m_scene;
            m_dispatcher = other.m_dispatcher;
            m_eventCallback = std::move(other.m_eventCallback);
            m_isSimulating = other.m_isSimulating;
            m_gravity = other.m_gravity;

            other.m_scene = nullptr;
            other.m_dispatcher = nullptr;
            other.m_isSimulating = false;
        }
        return *this;
    }

    namespace {
        physx::PxFilterFlags EngineSimulationFilterShader(
            physx::PxFilterObjectAttributes attributes0,
            physx::PxFilterData filterData0,
            physx::PxFilterObjectAttributes attributes1,
            physx::PxFilterData filterData1,
            physx::PxPairFlags& pairFlags,
            const void* constantBlock,
            physx::PxU32 constantBlockSize)
        {
            if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1)) {
                pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
                return physx::PxFilterFlag::eDEFAULT;
            }

            pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT
                      | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
                      | physx::PxPairFlag::eNOTIFY_TOUCH_LOST
                      | physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS
                      | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS
                      | physx::PxPairFlag::eDETECT_CCD_CONTACT
                      | physx::PxPairFlag::eSOLVE_CONTACT;

            return physx::PxFilterFlag::eDEFAULT;
        }
    }

    bool PhysicsScene::init(const PhysicsSceneDesc& desc)
    {
        if (m_scene) {
            return true;
        }

        PhysicsCommon& common = PhysicsCommon::get();
        if (!common.isInitialized()) {
            if (!common.init()) {
                spdlog::error("[PhysicsScene] 初始化 PhysicsCommon 失败");
                return false;
            }
        }

        physx::PxPhysics* physics = common.getPhysics();
        if (!physics) {
            spdlog::error("[PhysicsScene] PxPhysics 实例为空");
            return false;
        }

        uint32_t threads = desc.numWorkerThreads;
        if (threads == 0) {
            threads = 1;
        }
        m_dispatcher = physx::PxDefaultCpuDispatcherCreate(threads);
        if (!m_dispatcher) {
            spdlog::error("[PhysicsScene] 创建 PxDefaultCpuDispatcher ({} 线程) 失败", threads);
            return false;
        }

        physx::PxSceneDesc sceneDesc(common.getTolerancesScale());
        m_gravity = desc.gravity;
        sceneDesc.gravity = physx::PxVec3(m_gravity.x, m_gravity.y, m_gravity.z);
        sceneDesc.cpuDispatcher = m_dispatcher;
        sceneDesc.filterShader = EngineSimulationFilterShader;

        m_eventCallback = makeUnique<PhysicsSceneSimulationCallback>();
        sceneDesc.simulationEventCallback = m_eventCallback.get();

        if (desc.enablePCM) {
            sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
        }
        if (desc.enableCCD) {
            sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
        }
        if (desc.enableActiveActors) {
            sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
        }

        if (!sceneDesc.isValid()) {
            spdlog::error("[PhysicsScene] PxSceneDesc 无效");
            m_dispatcher->release();
            m_dispatcher = nullptr;
            m_eventCallback.reset();
            return false;
        }

        m_scene = physics->createScene(sceneDesc);
        if (!m_scene) {
            spdlog::error("[PhysicsScene] 创建 PxScene 实例失败");
            m_dispatcher->release();
            m_dispatcher = nullptr;
            m_eventCallback.reset();
            return false;
        }

        spdlog::info("[PhysicsScene] PhysicsScene 初始化成功 (重力: [{}, {}, {}])",
                     m_gravity.x, m_gravity.y, m_gravity.z);
        return true;
    }

    void PhysicsScene::shutdown()
    {
        if (m_isSimulating && m_scene) {
            m_scene->fetchResults(true);
            m_isSimulating = false;
        }

        if (m_scene) {
            m_scene->release();
            m_scene = nullptr;
        }

        if (m_dispatcher) {
            m_dispatcher->release();
            m_dispatcher = nullptr;
        }

        m_eventCallback.reset();
    }

    void PhysicsScene::simulate(float dt)
    {
        if (!m_scene || dt <= 0.0f) {
            return;
        }

        if (m_isSimulating) {
            fetchResults(true);
        }

        if (m_eventCallback) {
            m_eventCallback->clearEvents();
        }

        m_scene->simulate(dt);
        m_isSimulating = true;
    }

    bool PhysicsScene::fetchResults(bool block)
    {
        if (!m_scene || !m_isSimulating) {
            return false;
        }

        bool completed = m_scene->fetchResults(block);
        if (completed) {
            m_isSimulating = false;
        }
        return completed;
    }

    void PhysicsScene::setGravity(const FCT::Vec3<>& gravity)
    {
        m_gravity = gravity;
        if (m_scene) {
            m_scene->setGravity(physx::PxVec3(m_gravity.x, m_gravity.y, m_gravity.z));
        }
    }

    FCT::Vec3<> PhysicsScene::getGravity() const
    {
        if (m_scene) {
            physx::PxVec3 g = m_scene->getGravity();
            return FCT::Vec3<>(g.x, g.y, g.z);
        }
        return m_gravity;
    }

    void PhysicsScene::addActor(physx::PxActor& actor)
    {
        if (m_scene) {
            m_scene->addActor(actor);
        }
    }

    void PhysicsScene::removeActor(physx::PxActor& actor)
    {
        if (m_scene) {
            m_scene->removeActor(actor);
        }
    }

    bool PhysicsScene::raycast(
        const FCT::Vec3<>& origin,
        const FCT::Vec3<>& unitDir,
        float maxDistance,
        RaycastHit& outHit,
        uint32_t layerMask)
    {
        if (!m_scene || maxDistance <= 0.0f) {
            outHit.hasHit = false;
            return false;
        }

        physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
        physx::PxVec3 pxDir(unitDir.x, unitDir.y, unitDir.z);
        float len = pxDir.magnitude();
        if (len > 1e-5f) {
            pxDir /= len;
        } else {
            outHit.hasHit = false;
            return false;
        }

        physx::PxRaycastBuffer hit;
        physx::PxQueryFilterData filterData;
        filterData.data.word0 = layerMask;

        bool status = m_scene->raycast(pxOrigin, pxDir, maxDistance, hit, physx::PxHitFlag::eDEFAULT, filterData);
        if (status && hit.hasBlock) {
            outHit.hasHit = true;
            outHit.distance = hit.block.distance;
            outHit.position = FCT::Vec3<>(hit.block.position.x, hit.block.position.y, hit.block.position.z);
            outHit.point = outHit.position;
            outHit.normal = FCT::Vec3<>(hit.block.normal.x, hit.block.normal.y, hit.block.normal.z);
            outHit.actor = hit.block.actor;
            outHit.shape = hit.block.shape;

            if (hit.block.actor && hit.block.actor->userData) {
                outHit.entity = decodePhysicsUserData(hit.block.actor->userData);
            } else {
                outHit.entity = entt::null;
            }
            return true;
        }

        outHit.hasHit = false;
        return false;
    }

    std::vector<RaycastHit> PhysicsScene::raycastAll(
        const FCT::Vec3<>& origin,
        const FCT::Vec3<>& unitDir,
        float maxDistance,
        uint32_t layerMask)
    {
        std::vector<RaycastHit> results;
        if (!m_scene || maxDistance <= 0.0f) {
            return results;
        }

        physx::PxVec3 pxOrigin(origin.x, origin.y, origin.z);
        physx::PxVec3 pxDir(unitDir.x, unitDir.y, unitDir.z);
        float len = pxDir.magnitude();
        if (len > 1e-5f) {
            pxDir /= len;
        } else {
            return results;
        }

        constexpr physx::PxU32 maxHits = 64;
        physx::PxRaycastHit hitBuffer[maxHits];
        physx::PxRaycastBuffer hits(hitBuffer, maxHits);
        physx::PxQueryFilterData filterData;
        filterData.data.word0 = layerMask;

        bool status = m_scene->raycast(pxOrigin, pxDir, maxDistance, hits, physx::PxHitFlag::eDEFAULT, filterData);
        if (status && hits.hasAnyHits()) {
            for (physx::PxU32 i = 0; i < hits.nbTouches; ++i) {
                const auto& touch = hits.touches[i];
                RaycastHit hitInfo;
                hitInfo.hasHit = true;
                hitInfo.distance = touch.distance;
                hitInfo.position = FCT::Vec3<>(touch.position.x, touch.position.y, touch.position.z);
                hitInfo.point = hitInfo.position;
                hitInfo.normal = FCT::Vec3<>(touch.normal.x, touch.normal.y, touch.normal.z);
                hitInfo.actor = touch.actor;
                hitInfo.shape = touch.shape;
                if (touch.actor && touch.actor->userData) {
                    hitInfo.entity = decodePhysicsUserData(touch.actor->userData);
                }
                results.push_back(hitInfo);
            }
            if (hits.hasBlock) {
                bool alreadyIn = false;
                for (const auto& r : results) {
                    if (r.actor == hits.block.actor && r.shape == hits.block.shape && std::abs(r.distance - hits.block.distance) < 1e-4f) {
                        alreadyIn = true;
                        break;
                    }
                }
                if (!alreadyIn) {
                    RaycastHit hitInfo;
                    hitInfo.hasHit = true;
                    hitInfo.distance = hits.block.distance;
                    hitInfo.position = FCT::Vec3<>(hits.block.position.x, hits.block.position.y, hits.block.position.z);
                    hitInfo.point = hitInfo.position;
                    hitInfo.normal = FCT::Vec3<>(hits.block.normal.x, hits.block.normal.y, hits.block.normal.z);
                    hitInfo.actor = hits.block.actor;
                    hitInfo.shape = hits.block.shape;
                    if (hits.block.actor && hits.block.actor->userData) {
                        hitInfo.entity = decodePhysicsUserData(hits.block.actor->userData);
                    }
                    results.push_back(hitInfo);
                }
            }
            std::sort(results.begin(), results.end(), [](const RaycastHit& a, const RaycastHit& b) {
                return a.distance < b.distance;
            });
        }
        return results;
    }

    physx::PxRigidDynamic* PhysicsScene::createRigidDynamic(const FCT::Vec3<>& position, const FCT::Vec3<>& rotationEuler)
    {
        if (!m_scene) {
            return nullptr;
        }
        physx::PxPhysics* physics = PhysicsCommon::get().getPhysics();
        if (!physics) {
            return nullptr;
        }

        physx::PxQuat qx(rotationEuler.x, physx::PxVec3(1.0f, 0.0f, 0.0f));
        physx::PxQuat qy(rotationEuler.y, physx::PxVec3(0.0f, 1.0f, 0.0f));
        physx::PxQuat qz(rotationEuler.z, physx::PxVec3(0.0f, 0.0f, 1.0f));
        physx::PxQuat rot = qx * qy * qz;
        physx::PxTransform transform(physx::PxVec3(position.x, position.y, position.z), rot);

        physx::PxRigidDynamic* actor = physics->createRigidDynamic(transform);
        if (actor) {
            m_scene->addActor(*actor);
        }
        return actor;
    }

    physx::PxRigidStatic* PhysicsScene::createRigidStatic(const FCT::Vec3<>& position, const FCT::Vec3<>& rotationEuler)
    {
        if (!m_scene) {
            return nullptr;
        }
        physx::PxPhysics* physics = PhysicsCommon::get().getPhysics();
        if (!physics) {
            return nullptr;
        }

        physx::PxQuat qx(rotationEuler.x, physx::PxVec3(1.0f, 0.0f, 0.0f));
        physx::PxQuat qy(rotationEuler.y, physx::PxVec3(0.0f, 1.0f, 0.0f));
        physx::PxQuat qz(rotationEuler.z, physx::PxVec3(0.0f, 0.0f, 1.0f));
        physx::PxQuat rot = qx * qy * qz;
        physx::PxTransform transform(physx::PxVec3(position.x, position.y, position.z), rot);

        physx::PxRigidStatic* actor = physics->createRigidStatic(transform);
        if (actor) {
            m_scene->addActor(*actor);
        }
        return actor;
    }

    const std::vector<CollisionEvent>& PhysicsScene::getCollisionEvents() const
    {
        static const std::vector<CollisionEvent> s_empty;
        return m_eventCallback ? m_eventCallback->getCollisionEvents() : s_empty;
    }

    const std::vector<TriggerEvent>& PhysicsScene::getTriggerEvents() const
    {
        static const std::vector<TriggerEvent> s_empty;
        return m_eventCallback ? m_eventCallback->getTriggerEvents() : s_empty;
    }

    void PhysicsScene::clearEvents()
    {
        if (m_eventCallback) {
            m_eventCallback->clearEvents();
        }
    }

} // namespace MQEngine
