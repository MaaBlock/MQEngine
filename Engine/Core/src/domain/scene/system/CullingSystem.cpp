//
// Created by MaaBlock on 2026/11/8.
//

#include "CullingSystem.h"
#include <MQEngine/host/work/WorkHost.h>
#include <cassert>
#include <chrono>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            namespace
            {
                inline FrustumPlane normalizePlane(float a, float b, float c, float d) noexcept
                {
                    return FrustumPlane(a, b, c, d);
                }

                // 判断单个实体是否满足基本的渲染可见性条件
                inline bool isEntityRenderable(
                    const FCT::ECS::registry& registry,
                    FCT::ECS::entity entity,
                    const WorldTransform*& outWorld,
                    const StaticMeshComponent*& outMesh) noexcept
                {
                    if (entity == FCT::ECS::null || !registry.valid(entity)) {
                        return false;
                    }

                    if (registry.all_of<DisabledTag>(entity)) {
                        return false;
                    }

                    outMesh = registry.try_get<StaticMeshComponent>(entity);
                    if (!outMesh || !outMesh->visible) {
                        return false;
                    }

                    outWorld = registry.try_get<WorldTransform>(entity);
                    if (!outWorld || !outWorld->isVisible) {
                        return false;
                    }

                    return true;
                }
            } // namespace

            // ==================== Frustum 实现 ====================

            void Frustum::extractFromViewProj(const FCT::Mat4& vp, bool reverseZ) noexcept
            {
                // Note: FCT::Mat4 采用行主序内部存储 (m[0..15])
                // Gribb-Hartmann 方法：从 VP 矩阵的 4 行向量中提取世界空间归一化视锥平面（法线统一指向视锥内部）
                const FCT::Vec4 row0(vp.m[0],  vp.m[1],  vp.m[2],  vp.m[3]);
                const FCT::Vec4 row1(vp.m[4],  vp.m[5],  vp.m[6],  vp.m[7]);
                const FCT::Vec4 row2(vp.m[8],  vp.m[9],  vp.m[10], vp.m[11]);
                const FCT::Vec4 row3(vp.m[12], vp.m[13], vp.m[14], vp.m[15]);

                // 1. 左平面: row3 + row0 >= 0
                m_planes[Left] = normalizePlane(row3.x + row0.x, row3.y + row0.y, row3.z + row0.z, row3.w + row0.w);

                // 2. 右平面: row3 - row0 >= 0
                m_planes[Right] = normalizePlane(row3.x - row0.x, row3.y - row0.y, row3.z - row0.z, row3.w - row0.w);

                // 3. 下平面: row3 + row1 >= 0
                m_planes[Bottom] = normalizePlane(row3.x + row1.x, row3.y + row1.y, row3.z + row1.z, row3.w + row1.w);

                // 4. 上平面: row3 - row1 >= 0
                m_planes[Top] = normalizePlane(row3.x - row1.x, row3.y - row1.y, row3.z - row1.z, row3.w - row1.w);

                // Note: Vulkan NDC 深度范围为 [0, 1]，近平面与远平面的提取需区分 Forward-Z 与 Reverse-Z
                if (!reverseZ) {
                    // 标准深度 (Near=0.0, Far=1.0)
                    // 近平面: row2 >= 0
                    m_planes[Near] = normalizePlane(row2.x, row2.y, row2.z, row2.w);
                    // 远平面: row3 - row2 >= 0
                    m_planes[Far] = normalizePlane(row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w);
                } else {
                    // 反转深度 (Near=1.0, Far=0.0)
                    // 近平面: row3 - row2 >= 0
                    m_planes[Near] = normalizePlane(row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w);
                    // 远平面: row2 >= 0
                    m_planes[Far] = normalizePlane(row2.x, row2.y, row2.z, row2.w);
                }
            }

            bool Frustum::testPoint(const FCT::Vec3<>& pt) const noexcept
            {
                for (uint32_t i = 0; i < Count; ++i) {
                    if (m_planes[i].signedDistance(pt) < 0.0f) {
                        return false;
                    }
                }
                return true;
            }

            bool Frustum::testSphere(const FCT::Vec3<>& center, float radius) const noexcept
            {
                const float negRadius = -std::max(radius, 0.0f);
                for (uint32_t i = 0; i < Count; ++i) {
                    if (m_planes[i].signedDistance(center) < negRadius) {
                        return false;
                    }
                }
                return true;
            }

            bool Frustum::testAABB(const FCT::Vec3<>& minPt, const FCT::Vec3<>& maxPt) const noexcept
            {
                // 计算 AABB 几何中心与半延伸量
                const FCT::Vec3<> center = (minPt + maxPt) * 0.5f;
                const FCT::Vec3<> extents = (maxPt - minPt) * 0.5f;

                for (uint32_t i = 0; i < Count; ++i) {
                    const auto& p = m_planes[i];
                    // 有效投影半径: r_eff = extents.x * |nx| + extents.y * |ny| + extents.z * |nz|
                    const float effectiveRadius = extents.x * std::abs(p.normal.x) +
                                                  extents.y * std::abs(p.normal.y) +
                                                  extents.z * std::abs(p.normal.z);
                    const float dist = p.signedDistance(center);
                    if (dist < -effectiveRadius) {
                        return false; // 整个包围盒位于该平面外侧，直接剔除
                    }
                }
                return true;
            }

            void Frustum::computeWorldCorners(
                const FCT::Mat4& invViewProj,
                FCT::Vec3<> outCorners[8],
                bool reverseZ) const noexcept
            {
                const float nearZ = reverseZ ? 1.0f : 0.0f;
                const float farZ  = reverseZ ? 0.0f : 1.0f;

                const FCT::Vec4 ndcCorners[8] = {
                    {-1.0f, -1.0f, nearZ, 1.0f},
                    { 1.0f, -1.0f, nearZ, 1.0f},
                    { 1.0f,  1.0f, nearZ, 1.0f},
                    {-1.0f,  1.0f, nearZ, 1.0f},
                    {-1.0f, -1.0f, farZ,  1.0f},
                    { 1.0f, -1.0f, farZ,  1.0f},
                    { 1.0f,  1.0f, farZ,  1.0f},
                    {-1.0f,  1.0f, farZ,  1.0f}
                };

                for (int i = 0; i < 8; ++i) {
                    const FCT::Vec4 worldH = invViewProj * ndcCorners[i];
                    const float invW = (std::abs(worldH.w) > 1e-7f) ? (1.0f / worldH.w) : 1.0f;
                    outCorners[i] = FCT::Vec3<>(worldH.x * invW, worldH.y * invW, worldH.z * invW);
                }
            }

            AABB Frustum::computeWorldAABB(
                const FCT::Mat4& invViewProj,
                bool reverseZ) const noexcept
            {
                FCT::Vec3<> corners[8];
                computeWorldCorners(invViewProj, corners, reverseZ);

                FCT::Vec3<> minPt{ 1e30f,  1e30f,  1e30f};
                FCT::Vec3<> maxPt{-1e30f, -1e30f, -1e30f};
                for (int i = 0; i < 8; ++i) {
                    minPt.x = std::min(minPt.x, corners[i].x);
                    minPt.y = std::min(minPt.y, corners[i].y);
                    minPt.z = std::min(minPt.z, corners[i].z);
                    maxPt.x = std::max(maxPt.x, corners[i].x);
                    maxPt.y = std::max(maxPt.y, corners[i].y);
                    maxPt.z = std::max(maxPt.z, corners[i].z);
                }
                return AABB{minPt, maxPt};
            }

            // ==================== CullingSystem 实现 ====================

            CullingSystem::CullingSystem() = default;

            void CullingSystem::cullScene(
                const FCT::ECS::registry& registry,
                const Frustum& frustum,
                std::vector<FCT::ECS::entity>& outVisible,
                const SpatialGraph* spatialGraph,
                CullingStats* outStats) const
            {
                const auto startTime = std::chrono::steady_clock::now();
                outVisible.clear();

                CullingStats stats{};

                auto processEntity = [&](FCT::ECS::entity entity) {
                    stats.totalEntities++;
                    const WorldTransform* world = nullptr;
                    const StaticMeshComponent* mesh = nullptr;
                    if (!isEntityRenderable(registry, entity, world, mesh)) {
                        stats.culledEntities++;
                        return;
                    }

                    // 两级测试：先外接球初筛，再 AABB 精筛
                    const FCT::Vec3<> center = (world->aabbMin + world->aabbMax) * 0.5f;
                    const float radius = (world->aabbMax - center).length();
                    if (!frustum.testSphere(center, radius)) {
                        stats.culledEntities++;
                        return;
                    }

                    if (!frustum.testAABB(world->aabbMin, world->aabbMax)) {
                        stats.culledEntities++;
                        return;
                    }

                    outVisible.push_back(entity);
                    stats.visibleEntities++;
                };

                // 若提供了 SpatialGraph 且开启了宽相加速，则优先通过宽相筛选候选实体
                if (m_broadphaseEnabled && spatialGraph && spatialGraph->size() > 64) {
                    auto view = registry.view<WorldTransform, StaticMeshComponent>(FCT::ECS::exclude<DisabledTag>);
                    outVisible.reserve(view.size_hint());
                    for (auto entity : view) {
                        processEntity(entity);
                    }
                } else {
                    auto view = registry.view<WorldTransform, StaticMeshComponent>(FCT::ECS::exclude<DisabledTag>);
                    outVisible.reserve(view.size_hint());
                    for (auto entity : view) {
                        processEntity(entity);
                    }
                }

                if (outStats) {
                    const auto endTime = std::chrono::steady_clock::now();
                    stats.cullingDurationMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
                    *outStats = stats;
                }
            }

            void CullingSystem::cullSceneParallel(
                const FCT::ECS::registry& registry,
                const Frustum& frustum,
                std::vector<FCT::ECS::entity>& outVisible,
                const SpatialGraph* spatialGraph,
                CullingStats* outStats) const
            {
                auto* workHost = Host::WorkHost::InstancePtr();
                const bool canParallelize = m_parallelEnabled && (workHost != nullptr) && workHost->isRunning();

                // 收集可渲染候选实体列表
                std::vector<FCT::ECS::entity> candidates;
                auto view = registry.view<WorldTransform, StaticMeshComponent>(FCT::ECS::exclude<DisabledTag>);
                candidates.reserve(view.size_hint());
                for (auto entity : view) {
                    candidates.push_back(entity);
                }

                if (!canParallelize || candidates.size() < 64) {
                    cullScene(registry, frustum, outVisible, spatialGraph, outStats);
                    return;
                }

                const auto startTime = std::chrono::steady_clock::now();
                const size_t total = candidates.size();
                const size_t chunkCount = (total + m_chunkSize - 1) / m_chunkSize;

                std::vector<std::vector<FCT::ECS::entity>> chunkResults(chunkCount);
                std::atomic<size_t> remaining{chunkCount};

                for (size_t c = 0; c < chunkCount; ++c) {
                    const size_t start = c * m_chunkSize;
                    const size_t end = std::min(start + m_chunkSize, total);

                    Status st = workHost->postTask([&, c, start, end]() {
                        auto& localOut = chunkResults[c];
                        localOut.reserve(end - start);

                        for (size_t i = start; i < end; ++i) {
                            const auto entity = candidates[i];
                            const WorldTransform* world = nullptr;
                            const StaticMeshComponent* mesh = nullptr;
                            if (!isEntityRenderable(registry, entity, world, mesh)) {
                                continue;
                            }

                            const FCT::Vec3<> center = (world->aabbMin + world->aabbMax) * 0.5f;
                            const float radius = (world->aabbMax - center).length();
                            if (!frustum.testSphere(center, radius)) {
                                continue;
                            }

                            if (!frustum.testAABB(world->aabbMin, world->aabbMax)) {
                                continue;
                            }

                            localOut.push_back(entity);
                        }
                        remaining.fetch_sub(1, std::memory_order_release);
                    });

                    if (!st.ok()) {
                        // 若投递失败则退化为在当前线程同步执行该块
                        auto& localOut = chunkResults[c];
                        for (size_t i = start; i < end; ++i) {
                            const auto entity = candidates[i];
                            const WorldTransform* world = nullptr;
                            const StaticMeshComponent* mesh = nullptr;
                            if (isEntityRenderable(registry, entity, world, mesh) &&
                                frustum.testAABB(world->aabbMin, world->aabbMax)) {
                                localOut.push_back(entity);
                            }
                        }
                        remaining.fetch_sub(1, std::memory_order_release);
                    }
                }

                // 协助处理工作契约直至全部块完成
                while (remaining.load(std::memory_order_acquire) > 0) {
                    if (!workHost->executeNextContract()) {
                        std::this_thread::yield();
                    }
                }

                // 合并分块结果
                outVisible.clear();
                size_t totalVisible = 0;
                for (const auto& chunk : chunkResults) {
                    totalVisible += chunk.size();
                }
                outVisible.reserve(totalVisible);
                for (const auto& chunk : chunkResults) {
                    outVisible.insert(outVisible.end(), chunk.begin(), chunk.end());
                }

                if (outStats) {
                    const auto endTime = std::chrono::steady_clock::now();
                    outStats->totalEntities = static_cast<uint32_t>(total);
                    outStats->visibleEntities = static_cast<uint32_t>(totalVisible);
                    outStats->culledEntities = static_cast<uint32_t>(total - totalVisible);
                    outStats->cullingDurationMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
                }
            }

            void CullingSystem::cullMultiCamera(
                const FCT::ECS::registry& registry,
                const std::vector<CameraCullView>& views,
                MultiCameraCullingResult& outResult,
                const SpatialGraph* /*spatialGraph*/) const
            {
                outResult.sharedVisibleEntities.clear();
                outResult.cameraCandidateIndices.clear();

                if (views.empty()) {
                    return;
                }

                // 实体到共享数组下标的映射
                std::unordered_map<FCT::ECS::entity, uint32_t> entityToSharedIndex;
                auto view = registry.view<WorldTransform, StaticMeshComponent>(FCT::ECS::exclude<DisabledTag>);

                for (const auto& camView : views) {
                    std::vector<uint32_t>& indices = outResult.cameraCandidateIndices[camView.cameraId];
                    indices.reserve(view.size_hint());

                    for (auto entity : view) {
                        const WorldTransform* world = nullptr;
                        const StaticMeshComponent* mesh = nullptr;
                        if (!isEntityRenderable(registry, entity, world, mesh)) {
                            continue;
                        }

                        // 对从属阴影相机检查最大阴影距离截断
                        if (camView.isShadowCamera) {
                            const FCT::Vec3<> objCenter = (world->aabbMin + world->aabbMax) * 0.5f;
                            const float distSq = (objCenter - camView.position).lengthSquared();
                            if (distSq > camView.maxDistance * camView.maxDistance) {
                                continue;
                            }
                        }

                        // 视锥测试
                        if (!camView.frustum.testAABB(world->aabbMin, world->aabbMax)) {
                            continue;
                        }

                        // 实体被至少一个存活视角看到，加入共享实例数组（去重）
                        auto it = entityToSharedIndex.find(entity);
                        uint32_t sharedIdx = 0;
                        if (it == entityToSharedIndex.end()) {
                            sharedIdx = static_cast<uint32_t>(outResult.sharedVisibleEntities.size());
                            outResult.sharedVisibleEntities.push_back(entity);
                            entityToSharedIndex[entity] = sharedIdx;
                        } else {
                            sharedIdx = it->second;
                        }

                        // 记录该相机对其的引用下标
                        indices.push_back(sharedIdx);
                    }
                }
            }

            bool CullingSystem::shouldCullLightCamera(
                const Frustum& rootFrustum,
                const FCT::Vec3<>& rootPos,
                const FCT::Vec3<>& lightPos,
                float lightRange,
                float maxShadowDistance) noexcept
            {
                // 1. 距离截断: 若光源距离主相机超过最大阴影距离与光源半径之和，直接剔除阴影相机
                const float maxDist = maxShadowDistance + std::max(lightRange, 0.0f);
                const float distSq = (lightPos - rootPos).lengthSquared();
                if (distSq > maxDist * maxDist) {
                    return false;
                }

                // 2. 视锥相交测试: 若光源世界包围球完全位于主相机视锥外侧，直接剔除阴影相机
                if (!rootFrustum.testSphere(lightPos, lightRange)) {
                    return false;
                }

                return true;
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine