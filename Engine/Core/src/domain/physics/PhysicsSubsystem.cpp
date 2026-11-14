/**
 * @file PhysicsSubsystem.cpp
 * @brief 物理子系统实现：动力学积分、窄相碰撞检测、冲量响应求解与无锁事件发布
 */
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/physics/PhysicsHost.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace MQEngine
{
    namespace Domain
    {
        namespace Physics
        {
            namespace
            {
                // 射线与 AABB 求交
                inline bool intersectRayAABB(
                    const FCT::Vec3<>& origin,
                    const FCT::Vec3<>& dir,
                    float maxDist,
                    const FCT::Vec3<>& boxMin,
                    const FCT::Vec3<>& boxMax,
                    float& outDist,
                    FCT::Vec3<>& outNormal)
                {
                    float tmin = 0.0f;
                    float tmax = maxDist;
                    FCT::Vec3<> hitNormal(0.0f, 1.0f, 0.0f);

                    for (int i = 0; i < 3; ++i) {
                        float o = (i == 0) ? origin.x : (i == 1 ? origin.y : origin.z);
                        float d = (i == 0) ? dir.x : (i == 1 ? dir.y : dir.z);
                        float bmin = (i == 0) ? boxMin.x : (i == 1 ? boxMin.y : boxMin.z);
                        float bmax = (i == 0) ? boxMax.x : (i == 1 ? boxMax.y : boxMax.z);

                        float axisSign = 1.0f;
                        if (std::abs(d) < 1e-6f) {
                            if (o < bmin || o > bmax) return false;
                        } else {
                            float invD = 1.0f / d;
                            float t1 = (bmin - o) * invD;
                            float t2 = (bmax - o) * invD;
                            float sign = -1.0f;
                            if (t1 > t2) {
                                std::swap(t1, t2);
                                sign = 1.0f;
                            }
                            if (t1 > tmin) {
                                tmin = t1;
                                axisSign = sign;
                                hitNormal = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
                                if (i == 0) hitNormal.x = axisSign;
                                else if (i == 1) hitNormal.y = axisSign;
                                else hitNormal.z = axisSign;
                            }
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) return false;
                        }
                    }
                    if (tmax < 0.0f || tmin > maxDist) {
                        return false;
                    }
                    outDist = tmin;
                    outNormal = hitNormal;
                    return true;
                }

                // 射线与球体求交
                inline bool intersectRaySphere(
                    const FCT::Vec3<>& origin,
                    const FCT::Vec3<>& dir,
                    float maxDist,
                    const FCT::Vec3<>& center,
                    float radius,
                    float& outDist,
                    FCT::Vec3<>& outNormal)
                {
                    FCT::Vec3<> m = origin - center;
                    float b = m.dot(dir);
                    float c = m.dot(m) - radius * radius;

                    if (c > 0.0f && b > 0.0f) return false;

                    float discr = b * b - c;
                    if (discr < 0.0f) return false;

                    float t = -b - std::sqrt(discr);
                    if (t < 0.0f) t = 0.0f;
                    if (t > maxDist) return false;

                    outDist = t;
                    FCT::Vec3<> hitPoint = origin + dir * t;
                    outNormal = (hitPoint - center).normalize();
                    return true;
                }
            } // namespace

            PhysicsSubsystem::PhysicsSubsystem()
            {
                m_collisionEventBuffers[0].reserve(128);
                m_collisionEventBuffers[1].reserve(128);
                m_triggerEventBuffers[0].reserve(64);
                m_triggerEventBuffers[1].reserve(64);
            }

            PhysicsSubsystem::~PhysicsSubsystem()
            {
                term();
            }

            void PhysicsSubsystem::registerInfo()
            {
            }

            Status PhysicsSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                m_accumulatedTime = 0.0f;
                m_runtimeTickerActive.store(false, std::memory_order_release);
                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void PhysicsSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed)) {
                    return;
                }

                deactivateRuntimeTicker();
                clearEvents();
                m_initialized.store(false, std::memory_order_release);
            }

            void PhysicsSubsystem::activateRuntimeTicker()
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                auto* host = Host::PhysicsHost::InstancePtr();
                if (!host) {
                    return;
                }

                if (!host->tickers().hasNode("PhysicsSubsystem_Step")) {
                    host->tickers().addNode("PhysicsSubsystem_Step", [this]() {
                        step(1.0f / 60.0f);
                    });
                }
                m_runtimeTickerActive.store(true, std::memory_order_release);
            }

            void PhysicsSubsystem::deactivateRuntimeTicker() noexcept
            {
                auto* host = Host::PhysicsHost::InstancePtr();
                if (host) {
                    host->tickers().removeNode("PhysicsSubsystem_Step");
                }
                m_runtimeTickerActive.store(false, std::memory_order_release);
            }

            void PhysicsSubsystem::step(float dt)
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                m_accumulatedTime += dt;
                while (m_accumulatedTime >= m_fixedTimestep) {
                    solveRigidBodyDynamics(m_fixedTimestep);
                    solveNarrowphaseCollisions(m_fixedTimestep);
                    m_accumulatedTime -= m_fixedTimestep;
                }

                // 原子发布本帧碰撞事件至活跃双缓冲
                uint32_t publishedIdx = m_writeEventBufferIndex;
                m_activeEventBufferIndex.store(publishedIdx, std::memory_order_release);
                m_writeEventBufferIndex = 1 - m_writeEventBufferIndex;
                m_collisionEventBuffers[m_writeEventBufferIndex].clear();
                m_triggerEventBuffers[m_writeEventBufferIndex].clear();
            }

            void PhysicsSubsystem::solveRigidBodyDynamics(float dt)
            {
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return;
                }

                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();

                for (auto entity : view) {
                    auto& local = view.get<Scene::LocalTransform>(entity);
                    auto& rb = view.get<Scene::RigidBodyComponent>(entity);

                    // 0: Static, 1: Dynamic, 2: Kinematic
                    if (rb.bodyType != 1) {
                        continue;
                    }

                    // 重力积分
                    if (rb.useGravity) {
                        rb.linearVelocity = rb.linearVelocity + m_gravity * dt;
                    }

                    // 阻尼线性衰减
                    const float linearDamp = std::clamp(1.0f - rb.linearDamping * dt, 0.0f, 1.0f);
                    const float angularDamp = std::clamp(1.0f - rb.angularDamping * dt, 0.0f, 1.0f);
                    rb.linearVelocity = rb.linearVelocity * linearDamp;
                    rb.angularVelocity = rb.angularVelocity * angularDamp;

                    // 位置与角姿态积分
                    local.position = local.position + rb.linearVelocity * dt;
                    local.rotation = local.rotation + rb.angularVelocity * (dt * (180.0f / 3.14159265f));
                    local.isDirty = true;
                }
            }

            void PhysicsSubsystem::solveNarrowphaseCollisions(float dt)
            {
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return;
                }

                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();

                struct ColliderSnapshot
                {
                    FCT::ECS::entity entity;
                    int bodyType;
                    int colliderType; // 0: Box, 1: Plane/Ground, 2: Sphere
                    FCT::Vec3<> pos;
                    FCT::Vec3<> halfExtents;
                    float mass;
                    float restitution;
                    float friction;
                    bool isTrigger;
                };

                std::vector<ColliderSnapshot> colliders;
                colliders.reserve(view.size_hint());

                for (auto entity : view) {
                    const auto& local = view.get<Scene::LocalTransform>(entity);
                    const auto& rb = view.get<Scene::RigidBodyComponent>(entity);
                    colliders.push_back(ColliderSnapshot{
                        .entity = entity,
                        .bodyType = rb.bodyType,
                        .colliderType = rb.colliderType,
                        .pos = local.position,
                        .halfExtents = rb.colliderHalfExtents,
                        .mass = std::max(rb.mass, 0.001f),
                        .restitution = std::clamp(rb.restitution, 0.0f, 1.0f),
                        .friction = std::clamp(rb.friction, 0.0f, 2.0f),
                        .isTrigger = rb.isTrigger
                    });
                }

                auto& currentCollisionEvents = m_collisionEventBuffers[m_writeEventBufferIndex];
                auto& currentTriggerEvents = m_triggerEventBuffers[m_writeEventBufferIndex];

                // 实体与静态地面碰撞响应
                for (size_t i = 0; i < colliders.size(); ++i) {
                    auto& col = colliders[i];
                    if (col.bodyType != 1) continue; // 仅动态刚体受地面对抗

                    const float floorHeight = 0.0f;
                    const float bottomY = col.pos.y - col.halfExtents.y;
                    if (bottomY < floorHeight) {
                        float penetration = floorHeight - bottomY;
                        auto& local = reg.get<Scene::LocalTransform>(col.entity);
                        auto& rb = reg.get<Scene::RigidBodyComponent>(col.entity);

                        local.position.y += penetration;
                        local.isDirty = true;

                        float vy = rb.linearVelocity.y;
                        if (vy < 0.0f) {
                            float impulse = -(1.0f + col.restitution) * vy * col.mass;
                            rb.linearVelocity.y = -vy * col.restitution;
                            if (std::abs(rb.linearVelocity.y) < 0.08f) {
                                rb.linearVelocity.y = 0.0f;
                            }
                            // 地面摩擦减速
                            rb.linearVelocity.x *= std::max(0.0f, 1.0f - col.friction * dt * 5.0f);
                            rb.linearVelocity.z *= std::max(0.0f, 1.0f - col.friction * dt * 5.0f);

                            currentCollisionEvents.push_back(CollisionEvent{
                                .entityA = col.entity,
                                .entityB = FCT::ECS::null,
                                .contactPoint = FCT::Vec3<>(col.pos.x, floorHeight, col.pos.z),
                                .contactNormal = FCT::Vec3<>(0.0f, 1.0f, 0.0f),
                                .impulse = impulse,
                                .isTouchFound = true,
                                .isTouchLost = false
                            });
                        }
                    }
                }

                // 双实体间碰撞检测与冲量求解 (Box-Box / Sphere-Box / Sphere-Sphere)
                for (size_t i = 0; i < colliders.size(); ++i) {
                    for (size_t j = i + 1; j < colliders.size(); ++j) {
                        auto& a = colliders[i];
                        auto& b = colliders[j];

                        if (a.bodyType == 0 && b.bodyType == 0) {
                            continue; // 静态物体之间忽略碰撞
                        }

                        // AABB 重叠检测
                        FCT::Vec3<> delta = b.pos - a.pos;
                        FCT::Vec3<> totalHalf = a.halfExtents + b.halfExtents;
                        float overlapX = totalHalf.x - std::abs(delta.x);
                        float overlapY = totalHalf.y - std::abs(delta.y);
                        float overlapZ = totalHalf.z - std::abs(delta.z);

                        if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f) {
                            // 触发器检测分支
                            if (a.isTrigger || b.isTrigger) {
                                currentTriggerEvents.push_back(TriggerEvent{
                                    .triggerEntity = a.isTrigger ? a.entity : b.entity,
                                    .otherEntity = a.isTrigger ? b.entity : a.entity,
                                    .isEnter = true
                                });
                                continue;
                            }

                            // 寻找最小穿透轴
                            FCT::Vec3<> normal(0.0f, 1.0f, 0.0f);
                            float minOverlap = overlapX;
                            normal = FCT::Vec3<>(delta.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);

                            if (overlapY < minOverlap) {
                                minOverlap = overlapY;
                                normal = FCT::Vec3<>(0.0f, delta.y > 0.0f ? 1.0f : -1.0f, 0.0f);
                            }
                            if (overlapZ < minOverlap) {
                                minOverlap = overlapZ;
                                normal = FCT::Vec3<>(0.0f, 0.0f, delta.z > 0.0f ? 1.0f : -1.0f);
                            }

                            auto* rbA = reg.try_get<Scene::RigidBodyComponent>(a.entity);
                            auto* rbB = reg.try_get<Scene::RigidBodyComponent>(b.entity);
                            auto* tfA = reg.try_get<Scene::LocalTransform>(a.entity);
                            auto* tfB = reg.try_get<Scene::LocalTransform>(b.entity);

                            if (!rbA || !rbB || !tfA || !tfB) continue;

                            // 质量加权位置分离
                            float invMassA = (a.bodyType == 1) ? (1.0f / a.mass) : 0.0f;
                            float invMassB = (b.bodyType == 1) ? (1.0f / b.mass) : 0.0f;
                            float totalInvMass = invMassA + invMassB;
                            if (totalInvMass < 1e-6f) continue;

                            FCT::Vec3<> separation = normal * (minOverlap / totalInvMass);
                            if (a.bodyType == 1) {
                                tfA->position = tfA->position - separation * invMassA;
                                tfA->isDirty = true;
                            }
                            if (b.bodyType == 1) {
                                tfB->position = tfB->position + separation * invMassB;
                                tfB->isDirty = true;
                            }

                            // 相对速度与冲量响应
                            FCT::Vec3<> relVel = rbB->linearVelocity - rbA->linearVelocity;
                            float velAlongNormal = relVel.dot(normal);
                            if (velAlongNormal < 0.0f) {
                                float e = std::min(a.restitution, b.restitution);
                                float impulseMag = -(1.0f + e) * velAlongNormal / totalInvMass;
                                FCT::Vec3<> impulse = normal * impulseMag;

                                if (a.bodyType == 1) {
                                    rbA->linearVelocity = rbA->linearVelocity - impulse * invMassA;
                                }
                                if (b.bodyType == 1) {
                                    rbB->linearVelocity = rbB->linearVelocity + impulse * invMassB;
                                }

                                FCT::Vec3<> contactPoint = a.pos + normal * (a.halfExtents.x);
                                currentCollisionEvents.push_back(CollisionEvent{
                                    .entityA = a.entity,
                                    .entityB = b.entity,
                                    .contactPoint = contactPoint,
                                    .contactNormal = normal,
                                    .impulse = impulseMag,
                                    .isTouchFound = true,
                                    .isTouchLost = false
                                });
                            }
                        }
                    }
                }
            }

            bool PhysicsSubsystem::raycast(
                const FCT::Vec3<>& origin,
                const FCT::Vec3<>& direction,
                float maxDistance,
                RaycastHit& outHit)
            {
                auto hits = raycastAll(origin, direction, maxDistance);
                if (!hits.empty()) {
                    outHit = hits.front();
                    return true;
                }
                outHit.hasHit = false;
                return false;
            }

            std::vector<RaycastHit> PhysicsSubsystem::raycastAll(
                const FCT::Vec3<>& origin,
                const FCT::Vec3<>& direction,
                float maxDistance)
            {
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return {};
                }

                FCT::Vec3<> dir = direction.normalize();
                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();

                std::vector<RaycastHit> hits;
                hits.reserve(16);

                if (hasHeightfieldCollider()) {
                    const float step = 0.5f;
                    float curDist = 0.0f;
                    while (curDist < maxDistance) {
                        float px = origin.x + dir.x * curDist;
                        float py = origin.y + dir.y * curDist;
                        float pz = origin.z + dir.z * curDist;
                        float terrainH = getHeightfieldHeightAt(px, pz);
                        if (py <= terrainH) {
                            const float eps = m_heightfieldCollider.gridSpacing;
                            float hL = getHeightfieldHeightAt(px - eps, pz);
                            float hR = getHeightfieldHeightAt(px + eps, pz);
                            float hD = getHeightfieldHeightAt(px, pz - eps);
                            float hU = getHeightfieldHeightAt(px, pz + eps);
                            FCT::Vec3<> n(hL - hR, 2.0f * eps, hD - hU);
                            hits.push_back(RaycastHit{
                                .hasHit = true,
                                .point = FCT::Vec3<>(px, terrainH, pz),
                                .normal = n.normalize(),
                                .distance = curDist,
                                .entity = FCT::ECS::null
                            });
                            break;
                        }
                        curDist += step;
                    }
                }

                for (auto entity : view) {
                    const auto& local = view.get<Scene::LocalTransform>(entity);
                    const auto& rb = view.get<Scene::RigidBodyComponent>(entity);

                    float dist = 0.0f;
                    FCT::Vec3<> hitNormal(0.0f, 1.0f, 0.0f);
                    bool hasHit = false;

                    if (rb.colliderType == 2) { // 球体
                        float radius = std::max({rb.colliderHalfExtents.x, rb.colliderHalfExtents.y, rb.colliderHalfExtents.z});
                        hasHit = intersectRaySphere(origin, dir, maxDistance, local.position, radius, dist, hitNormal);
                    } else { // 盒体或地面
                        FCT::Vec3<> bmin = local.position - rb.colliderHalfExtents;
                        FCT::Vec3<> bmax = local.position + rb.colliderHalfExtents;
                        hasHit = intersectRayAABB(origin, dir, maxDistance, bmin, bmax, dist, hitNormal);
                    }

                    if (hasHit) {
                        hits.push_back(RaycastHit{
                            .hasHit = true,
                            .point = origin + dir * dist,
                            .normal = hitNormal,
                            .distance = dist,
                            .entity = entity
                        });
                    }
                }

                std::sort(hits.begin(), hits.end(), [](const RaycastHit& a, const RaycastHit& b) {
                    return a.distance < b.distance;
                });

                return hits;
            }

            bool PhysicsSubsystem::sphereSweep(
                const FCT::Vec3<>& origin,
                float radius,
                const FCT::Vec3<>& direction,
                float maxDistance,
                RaycastHit& outHit)
            {
                // 将球体扫掠等价转换为主体 AABB 外扩膨胀的射线投射 (Minkowski Sum)
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    outHit.hasHit = false;
                    return false;
                }

                FCT::Vec3<> dir = direction.normalize();
                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();

                float closestDist = maxDistance + 1.0f;
                FCT::Vec3<> closestNormal(0.0f, 1.0f, 0.0f);
                FCT::ECS::entity hitEntity = FCT::ECS::null;

                for (auto entity : view) {
                    const auto& local = view.get<Scene::LocalTransform>(entity);
                    const auto& rb = view.get<Scene::RigidBodyComponent>(entity);

                    FCT::Vec3<> expandedHalf = rb.colliderHalfExtents + FCT::Vec3<>(radius, radius, radius);
                    FCT::Vec3<> bmin = local.position - expandedHalf;
                    FCT::Vec3<> bmax = local.position + expandedHalf;

                    float dist = 0.0f;
                    FCT::Vec3<> normal(0.0f, 1.0f, 0.0f);
                    if (intersectRayAABB(origin, dir, maxDistance, bmin, bmax, dist, normal)) {
                        if (dist < closestDist) {
                            closestDist = dist;
                            closestNormal = normal;
                            hitEntity = entity;
                        }
                    }
                }

                if (closestDist <= maxDistance) {
                    outHit.hasHit = true;
                    outHit.distance = closestDist;
                    outHit.point = origin + dir * closestDist;
                    outHit.normal = closestNormal;
                    outHit.entity = hitEntity;
                    return true;
                }

                outHit.hasHit = false;
                return false;
            }

            std::vector<OverlapResult> PhysicsSubsystem::overlapSphere(const FCT::Vec3<>& center, float radius)
            {
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return {};
                }

                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();
                std::vector<OverlapResult> results;
                results.reserve(8);

                const float radiusSq = radius * radius;
                for (auto entity : view) {
                    const auto& local = view.get<Scene::LocalTransform>(entity);
                    const auto& rb = view.get<Scene::RigidBodyComponent>(entity);

                    // 计算球心到 AABB 最近点的距离
                    FCT::Vec3<> bmin = local.position - rb.colliderHalfExtents;
                    FCT::Vec3<> bmax = local.position + rb.colliderHalfExtents;
                    FCT::Vec3<> closest(
                        std::clamp(center.x, bmin.x, bmax.x),
                        std::clamp(center.y, bmin.y, bmax.y),
                        std::clamp(center.z, bmin.z, bmax.z)
                    );

                    FCT::Vec3<> diff = center - closest;
                    float distSq = diff.dot(diff);
                    if (distSq <= radiusSq) {
                        results.push_back(OverlapResult{
                            .entity = entity,
                            .nearestPoint = closest,
                            .distance = std::sqrt(distSq)
                        });
                    }
                }

                return results;
            }

            std::vector<OverlapResult> PhysicsSubsystem::overlapBox(const FCT::Vec3<>& center, const FCT::Vec3<>& halfExtents)
            {
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return {};
                }

                auto& reg = scene->getRegistry();
                auto view = reg.view<Scene::LocalTransform, Scene::RigidBodyComponent>();
                std::vector<OverlapResult> results;
                results.reserve(8);

                FCT::Vec3<> qMin = center - halfExtents;
                FCT::Vec3<> qMax = center + halfExtents;

                for (auto entity : view) {
                    const auto& local = view.get<Scene::LocalTransform>(entity);
                    const auto& rb = view.get<Scene::RigidBodyComponent>(entity);

                    FCT::Vec3<> bmin = local.position - rb.colliderHalfExtents;
                    FCT::Vec3<> bmax = local.position + rb.colliderHalfExtents;

                    bool overlap = (qMin.x <= bmax.x && qMax.x >= bmin.x) &&
                                   (qMin.y <= bmax.y && qMax.y >= bmin.y) &&
                                   (qMin.z <= bmax.z && qMax.z >= bmin.z);

                    if (overlap) {
                        results.push_back(OverlapResult{
                            .entity = entity,
                            .nearestPoint = local.position,
                            .distance = (local.position - center).length()
                        });
                    }
                }

                return results;
            }

            const std::vector<CollisionEvent>& PhysicsSubsystem::getCollisionEvents() const noexcept
            {
                uint32_t activeIdx = m_activeEventBufferIndex.load(std::memory_order_acquire);
                return m_collisionEventBuffers[activeIdx];
            }

            const std::vector<TriggerEvent>& PhysicsSubsystem::getTriggerEvents() const noexcept
            {
                uint32_t activeIdx = m_activeEventBufferIndex.load(std::memory_order_acquire);
                return m_triggerEventBuffers[activeIdx];
            }

            void PhysicsSubsystem::clearEvents()
            {
                m_collisionEventBuffers[0].clear();
                m_collisionEventBuffers[1].clear();
                m_triggerEventBuffers[0].clear();
                m_triggerEventBuffers[1].clear();
            }

            void PhysicsSubsystem::setHeightfieldCollider(
                uint32_t resolution,
                float spacing,
                float originX,
                float originZ,
                const std::vector<float>& heights)
            {
                m_heightfieldCollider.resolution = resolution;
                m_heightfieldCollider.gridSpacing = spacing;
                m_heightfieldCollider.originX = originX;
                m_heightfieldCollider.originZ = originZ;
                m_heightfieldCollider.heights = heights;
                m_heightfieldCollider.active = true;
            }

            void PhysicsSubsystem::removeHeightfieldCollider() noexcept
            {
                m_heightfieldCollider.active = false;
                m_heightfieldCollider.heights.clear();
            }

            float PhysicsSubsystem::getHeightfieldHeightAt(float worldX, float worldZ) const noexcept
            {
                if (!m_heightfieldCollider.active || m_heightfieldCollider.heights.empty() || m_heightfieldCollider.resolution < 2)
                    return 0.0f;

                float gx = (worldX - m_heightfieldCollider.originX) / m_heightfieldCollider.gridSpacing;
                float gz = (worldZ - m_heightfieldCollider.originZ) / m_heightfieldCollider.gridSpacing;

                if (gx < 0.0f || gz < 0.0f || gx >= static_cast<float>(m_heightfieldCollider.resolution - 1) || gz >= static_cast<float>(m_heightfieldCollider.resolution - 1))
                    return 0.0f;

                uint32_t x0 = static_cast<uint32_t>(gx);
                uint32_t z0 = static_cast<uint32_t>(gz);
                uint32_t x1 = std::min(x0 + 1, m_heightfieldCollider.resolution - 1);
                uint32_t z1 = std::min(z0 + 1, m_heightfieldCollider.resolution - 1);

                float fx = gx - static_cast<float>(x0);
                float fz = gz - static_cast<float>(z0);

                const auto& hf = m_heightfieldCollider.heights;
                uint32_t res = m_heightfieldCollider.resolution;
                float h00 = hf[z0 * res + x0];
                float h10 = hf[z0 * res + x1];
                float h01 = hf[z1 * res + x0];
                float h11 = hf[z1 * res + x1];

                float h0 = h00 * (1.0f - fx) + h10 * fx;
                float h1 = h01 * (1.0f - fx) + h11 * fx;
                return h0 * (1.0f - fz) + h1 * fz;
            }

        } // namespace Physics
    } // namespace Domain
} // namespace MQEngine
