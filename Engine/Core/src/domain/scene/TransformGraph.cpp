/**
 * @file TransformGraph.cpp
 * @brief 变换传播图实现
 */
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/host/work/WorkHost.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            namespace
            {
                inline FCT::Mat4 computeLocalMatrix(const LocalTransform& local)
                {
                    // 平移 * 旋转 (Z * Y * X) * 缩放矩阵合成
                    FCT::Mat4 rot = FCT::Mat4::RotateZ(local.rotation.z) *
                                    FCT::Mat4::RotateY(local.rotation.y) *
                                    FCT::Mat4::RotateX(local.rotation.x);
                    return FCT::Mat4::Translate(local.position.x, local.position.y, local.position.z) *
                           rot *
                           FCT::Mat4::Scale(local.scale.x, local.scale.y, local.scale.z);
                }
            } // namespace

            void TransformGraph::attachChild(
                FCT::ECS::registry& registry,
                FCT::ECS::entity parent,
                FCT::ECS::entity child)
            {
                assert(parent != FCT::ECS::null && child != FCT::ECS::null && parent != child && "非法实体节点");

                detachChild(registry, child);

                auto& parentRel = registry.get_or_emplace<HierarchyRelation>(parent);
                auto& childRel = registry.get_or_emplace<HierarchyRelation>(child);

                childRel.parent = parent;
                childRel.prevSibling = FCT::ECS::null;
                childRel.nextSibling = parentRel.firstChild;

                if (parentRel.firstChild != FCT::ECS::null) {
                    auto* oldFirst = registry.try_get<HierarchyRelation>(parentRel.firstChild);
                    if (oldFirst) {
                        oldFirst->prevSibling = child;
                    }
                }
                parentRel.firstChild = child;

                updateDepthRecursive(registry, child, parentRel.depth + 1);
                markDirty(registry, child);
            }

            void TransformGraph::detachChild(
                FCT::ECS::registry& registry,
                FCT::ECS::entity child)
            {
                if (child == FCT::ECS::null) return;

                auto* childRel = registry.try_get<HierarchyRelation>(child);
                if (!childRel || childRel->parent == FCT::ECS::null) {
                    return;
                }

                auto* parentRel = registry.try_get<HierarchyRelation>(childRel->parent);
                if (parentRel && parentRel->firstChild == child) {
                    parentRel->firstChild = childRel->nextSibling;
                }

                if (childRel->prevSibling != FCT::ECS::null) {
                    auto* prev = registry.try_get<HierarchyRelation>(childRel->prevSibling);
                    if (prev) {
                        prev->nextSibling = childRel->nextSibling;
                    }
                }

                if (childRel->nextSibling != FCT::ECS::null) {
                    auto* next = registry.try_get<HierarchyRelation>(childRel->nextSibling);
                    if (next) {
                        next->prevSibling = childRel->prevSibling;
                    }
                }

                childRel->parent = FCT::ECS::null;
                childRel->prevSibling = FCT::ECS::null;
                childRel->nextSibling = FCT::ECS::null;

                updateDepthRecursive(registry, child, 0);
                markDirty(registry, child);
            }

            void TransformGraph::markDirty(
                FCT::ECS::registry& registry,
                FCT::ECS::entity entity)
            {
                markSubtreeDirty(registry, entity);
            }

            void TransformGraph::markSubtreeDirty(
                FCT::ECS::registry& registry,
                FCT::ECS::entity entity)
            {
                if (entity == FCT::ECS::null) return;

                auto* local = registry.try_get<LocalTransform>(entity);
                if (local) {
                    local->isDirty = true;
                    local->dirtyVersion++;
                }

                auto* rel = registry.try_get<HierarchyRelation>(entity);
                if (rel) {
                    auto cur = rel->firstChild;
                    while (cur != FCT::ECS::null) {
                        markSubtreeDirty(registry, cur);
                        auto* siblingRel = registry.try_get<HierarchyRelation>(cur);
                        cur = siblingRel ? siblingRel->nextSibling : FCT::ECS::null;
                    }
                }
            }

            void TransformGraph::updateDepthRecursive(
                FCT::ECS::registry& registry,
                FCT::ECS::entity entity,
                uint32_t depth)
            {
                if (entity == FCT::ECS::null) return;

                auto* rel = registry.try_get<HierarchyRelation>(entity);
                if (!rel) return;

                rel->depth = depth;
                if (depth > m_currentMaxDepth && depth < kMaxDepth) {
                    m_currentMaxDepth = depth;
                }

                auto cur = rel->firstChild;
                while (cur != FCT::ECS::null) {
                    updateDepthRecursive(registry, cur, depth + 1);
                    auto* siblingRel = registry.try_get<HierarchyRelation>(cur);
                    cur = siblingRel ? siblingRel->nextSibling : FCT::ECS::null;
                }
            }

            void TransformGraph::evaluateParallel(FCT::ECS::registry& registry)
            {
                ZoneScopedN("TransformGraph::evaluateParallel");
                for (uint32_t d = 0; d < kMaxDepth; ++d) {
                    m_depthBuckets[d].clear();
                }

                uint32_t maxDepthFound = 0;

                // 收集包含变换组件的脏实体并分桶
                auto view = registry.view<LocalTransform, WorldTransform>();
                for (auto entity : view) {
                    auto& local = view.get<LocalTransform>(entity);
                    if (local.isDirty) {
                        uint32_t depth = 0;
                        auto* rel = registry.try_get<HierarchyRelation>(entity);
                        if (rel) {
                            depth = std::min(rel->depth, kMaxDepth - 1);
                        }
                        m_depthBuckets[depth].push_back(entity);
                        if (depth > maxDepthFound) {
                            maxDepthFound = depth;
                        }
                    }
                }

                auto evaluateEntityRange = [&registry](const std::vector<FCT::ECS::entity>& entities, size_t beginIdx, size_t endIdx) {
                    for (size_t idx = beginIdx; idx < endIdx; ++idx) {
                        auto entity = entities[idx];
                        auto& local = registry.get<LocalTransform>(entity);
                        auto& world = registry.get<WorldTransform>(entity);

                        FCT::Mat4 localMat = computeLocalMatrix(local);

                        auto* rel = registry.try_get<HierarchyRelation>(entity);
                        if (rel && rel->parent != FCT::ECS::null) {
                            auto* parentWorld = registry.try_get<WorldTransform>(rel->parent);
                            if (parentWorld) {
                                world.worldMatrix = parentWorld->worldMatrix * localMat;
                            } else {
                                world.worldMatrix = localMat;
                            }
                        } else {
                            world.worldMatrix = localMat;
                        }

                        float minY = -0.5f;
                        float maxY =  0.5f;
                        float halfXZ = 0.5f;
                        if (const auto* meshComp = registry.try_get<StaticMeshComponent>(entity)) {
                            if (meshComp->primitiveType == 1) {
                                minY = -0.02f;
                                maxY =  0.02f;
                            } else if (meshComp->customMesh ||
                                       (!meshComp->meshAssetPath.empty() &&
                                        meshComp->meshAssetPath.find("Builtin_") != 0 &&
                                        meshComp->meshAssetPath != "Cube" &&
                                        meshComp->meshAssetPath != "Sphere" &&
                                        meshComp->meshAssetPath != "Plane")) {
                                minY = -0.5f;
                                maxY =  2.5f;
                                halfXZ = 1.2f;
                            }
                        }
                        const float corners[8][3] = {
                            {-halfXZ, minY, -halfXZ},
                            { halfXZ, minY, -halfXZ},
                            {-halfXZ, maxY, -halfXZ},
                            { halfXZ, maxY, -halfXZ},
                            {-halfXZ, minY,  halfXZ},
                            { halfXZ, minY,  halfXZ},
                            {-halfXZ, maxY,  halfXZ},
                            { halfXZ, maxY,  halfXZ}
                        };

                        FCT::Vec3<> minPt{ 1e30f,  1e30f,  1e30f};
                        FCT::Vec3<> maxPt{-1e30f, -1e30f, -1e30f};
                        for (int c = 0; c < 8; ++c) {
                            FCT::Vec4 transformed = world.worldMatrix * FCT::Vec4(
                                corners[c][0], corners[c][1], corners[c][2], 1.0f);
                            minPt.x = std::min(minPt.x, transformed.x);
                            minPt.y = std::min(minPt.y, transformed.y);
                            minPt.z = std::min(minPt.z, transformed.z);
                            maxPt.x = std::max(maxPt.x, transformed.x);
                            maxPt.y = std::max(maxPt.y, transformed.y);
                            maxPt.z = std::max(maxPt.z, transformed.z);
                        }
                        world.aabbMin = minPt;
                        world.aabbMax = maxPt;

                        local.isDirty = false;
                    }
                };

                auto* workHost = Host::WorkHost::InstancePtr();
                const bool canParallelize = (workHost != nullptr && workHost->isRunning());

                // 逐层推进 WorldMatrix 与世界 AABB 计算（同层各节点无依赖，无锁并行）
                for (uint32_t d = 0; d <= maxDepthFound; ++d) {
                    auto& bucket = m_depthBuckets[d];
                    if (bucket.empty()) continue;

                    if (canParallelize && bucket.size() >= 32) {
                        constexpr size_t kBatchSize = 16;
                        const size_t batchCount = (bucket.size() + kBatchSize - 1) / kBatchSize;
                        std::atomic<size_t> remaining{batchCount};

                        for (size_t b = 0; b < batchCount; ++b) {
                            const size_t start = b * kBatchSize;
                            const size_t end = std::min(start + kBatchSize, bucket.size());
                            Status st = workHost->postTask([&bucket, &evaluateEntityRange, &remaining, start, end]() {
                                evaluateEntityRange(bucket, start, end);
                                remaining.fetch_sub(1, std::memory_order_release);
                            });
                            if (!st.ok()) {
                                evaluateEntityRange(bucket, start, end);
                                remaining.fetch_sub(1, std::memory_order_release);
                            }
                        }

                        while (remaining.load(std::memory_order_acquire) > 0) {
                            if (!workHost->executeNextContract()) {
                                std::this_thread::yield();
                            }
                        }
                    } else {
                        evaluateEntityRange(bucket, 0, bucket.size());
                    }
                }
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine
