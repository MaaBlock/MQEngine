/**
 * @file CullingSystem.h
 * @brief 生产级视锥剔除与可见实例索引生成系统：
 *        基于 Gribb-Hartmann 平面提取、有效半径 AABB 投影求值与 WorkHost 分块并行剔除，
 *        严格对齐 Docs 05 (Bindless/GPU Driven) 与 07 (Host 数据流与两级剔除) 架构规范。
 */
#ifndef MQENGINE_DOMAIN_SCENE_SYSTEM_CULLINGSYSTEM_H
#define MQENGINE_DOMAIN_SCENE_SYSTEM_CULLINGSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SpatialGraph.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 归一化三维视锥平面 (Hessian Normal Form: ax + by + cz + d = 0)
             */
            struct alignas(16) FrustumPlane
            {
                FCT::Vec3<> normal{0.0f, 1.0f, 0.0f};
                float distance = 0.0f;

                FrustumPlane() = default;

                FrustumPlane(float a, float b, float c, float d) noexcept
                {
                    const float len = std::sqrt(a * a + b * b + c * c);
                    if (len > 1e-7f) {
                        const float invLen = 1.0f / len;
                        normal = FCT::Vec3<>(a * invLen, b * invLen, c * invLen);
                        distance = d * invLen;
                    } else {
                        normal = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
                        distance = 0.0f;
                    }
                }

                FrustumPlane(const FCT::Vec3<>& n, float d) noexcept
                {
                    const float len = n.length();
                    if (len > 1e-7f) {
                        const float invLen = 1.0f / len;
                        normal = n * invLen;
                        distance = d * invLen;
                    } else {
                        normal = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
                        distance = 0.0f;
                    }
                }

                explicit FrustumPlane(const FCT::Vec4& raw) noexcept
                    : FrustumPlane(raw.x, raw.y, raw.z, raw.w)
                {
                }

                [[nodiscard]] float signedDistance(const FCT::Vec3<>& pt) const noexcept
                {
                    return normal.dot(pt) + distance;
                }
            };

            /**
             * @brief 6 平面相机视锥体几何与测试器
             */
            class ENGINE_API Frustum
            {
            public:
                enum PlaneIndex : uint32_t
                {
                    Left = 0,
                    Right = 1,
                    Bottom = 2,
                    Top = 3,
                    Near = 4,
                    Far = 5,
                    Count = 6
                };

                Frustum() = default;

                /**
                 * @brief 从行主序 View-Projection 矩阵提取归一化 6 平面（Gribb-Hartmann 方法）
                 * @param vp 行主序 View-Projection 矩阵 (FCT::Mat4)
                 * @param reverseZ 是否为 Vulkan 反转 Z 深度缓冲模式 (Near=1.0, Far=0.0)
                 */
                void extractFromViewProj(const FCT::Mat4& vp, bool reverseZ = false) noexcept;

                /**
                 * @brief 点与视锥测试
                 */
                [[nodiscard]] bool testPoint(const FCT::Vec3<>& pt) const noexcept;

                /**
                 * @brief 球体与视锥测试
                 */
                [[nodiscard]] bool testSphere(const FCT::Vec3<>& center, float radius) const noexcept;

                /**
                 * @brief AABB 与视锥中心-半延伸量投影测试
                 */
                [[nodiscard]] bool testAABB(const FCT::Vec3<>& minPt, const FCT::Vec3<>& maxPt) const noexcept;
                [[nodiscard]] bool testAABB(const AABB& box) const noexcept
                {
                    return testAABB(box.min, box.max);
                }

                /**
                 * @brief 计算视锥体的 8 个世界空间角点
                 */
                void computeWorldCorners(
                    const FCT::Mat4& invViewProj,
                    FCT::Vec3<> outCorners[8],
                    bool reverseZ = false) const noexcept;

                /**
                 * @brief 计算视锥体在世界空间的紧凑轴对齐包围盒 (用于 SpatialGraph 宽相查询)
                 */
                [[nodiscard]] AABB computeWorldAABB(
                    const FCT::Mat4& invViewProj,
                    bool reverseZ = false) const noexcept;

                [[nodiscard]] const FrustumPlane& getPlane(PlaneIndex idx) const noexcept
                {
                    return m_planes[idx];
                }

                [[nodiscard]] const FrustumPlane* getPlanes() const noexcept
                {
                    return m_planes;
                }

            private:
                FrustumPlane m_planes[Count];
            };

            /**
             * @brief 剔除性能诊断与统计数据
             */
            struct CullingStats
            {
                uint32_t totalEntities = 0;
                uint32_t broadphasePassed = 0;
                uint32_t visibleEntities = 0;
                uint32_t culledEntities = 0;
                float cullingDurationMs = 0.0f;
            };

            /**
             * @brief 多相机剔除输入视角快照
             */
            struct CameraCullView
            {
                uint32_t cameraId = 0;
                Frustum frustum;
                FCT::Vec3<> position{0.0f, 0.0f, 0.0f};
                float maxDistance = 1000.0f;
                bool isShadowCamera = false;
            };

            /**
             * @brief 多相机剔除紧凑结果 (对齐 Doc 05 & 07 共享实例表规范)
             */
            struct MultiCameraCullingResult
            {
                std::vector<FCT::ECS::entity> sharedVisibleEntities;
                std::unordered_map<uint32_t, std::vector<uint32_t>> cameraCandidateIndices;
                CullingStats stats;
            };

            /**
             * @brief 生产级场景剔除系统核心类
             */
            class ENGINE_API CullingSystem
            {
            public:
                CullingSystem();
                ~CullingSystem() = default;

                CullingSystem(const CullingSystem&) = delete;
                CullingSystem& operator=(const CullingSystem&) = delete;

                // ==================== 1. 单相机场景剔除 ====================

                /**
                 * @brief 对指定 ECS 注册表执行单相机视锥剔除
                 * @param registry ECS 注册表 (包含 StaticMeshComponent, WorldTransform，按需过滤 DisabledTag)
                 * @param frustum 相机提取的视锥体
                 * @param outVisible 输出通过剔除的可见实体集合
                 * @param spatialGraph 可选空间加速图 (非空时启用宽相查询加速)
                 * @param outStats 可选诊断统计结构
                 */
                void cullScene(
                    const FCT::ECS::registry& registry,
                    const Frustum& frustum,
                    std::vector<FCT::ECS::entity>& outVisible,
                    const SpatialGraph* spatialGraph = nullptr,
                    CullingStats* outStats = nullptr) const;

                /**
                 * @brief 并发批量剔除（利用 WorkHost 分块并行）
                 */
                void cullSceneParallel(
                    const FCT::ECS::registry& registry,
                    const Frustum& frustum,
                    std::vector<FCT::ECS::entity>& outVisible,
                    const SpatialGraph* spatialGraph = nullptr,
                    CullingStats* outStats = nullptr) const;

                // ==================== 2. 多相机联合剔除 (对齐 Docs 05/07) ====================

                /**
                 * @brief 多相机视锥联合剔除与共享紧凑索引生成
                 * @param registry ECS 注册表
                 * @param views 存活相机视锥列表 (主相机、多级 CSM 阴影相机、反射相机等)
                 * @param outResult 输出去重的共享实体数组与各相机的候选下标映射
                 * @param spatialGraph 可选空间加速图
                 */
                void cullMultiCamera(
                    const FCT::ECS::registry& registry,
                    const std::vector<CameraCullView>& views,
                    MultiCameraCullingResult& outResult,
                    const SpatialGraph* spatialGraph = nullptr) const;

                // ==================== 3. 相机自身剔除 (Stage 1: Camera Culling) ====================

                /**
                 * @brief 以根相机 (SceneCamera) 视锥剔除光源从属相机 (LightCamera)
                 * @param rootFrustum 主场景相机视锥体
                 * @param rootPos 主相机世界坐标
                 * @param lightPos 光源世界坐标
                 * @param lightRange 光源有效影响半径 (Spot/Point Light)
                 * @param maxShadowDistance 最大阴影投射有效距离
                 * @return true 表示光源相机存活，false 表示被剔除无需绘制 ShadowMap
                 */
                static bool shouldCullLightCamera(
                    const Frustum& rootFrustum,
                    const FCT::Vec3<>& rootPos,
                    const FCT::Vec3<>& lightPos,
                    float lightRange,
                    float maxShadowDistance = 50.0f) noexcept;

                // ==================== 4. 配置与开关 ====================

                void setParallelEnabled(bool enabled) noexcept { m_parallelEnabled = enabled; }
                [[nodiscard]] bool isParallelEnabled() const noexcept { return m_parallelEnabled; }

                void setBroadphaseEnabled(bool enabled) noexcept { m_broadphaseEnabled = enabled; }
                [[nodiscard]] bool isBroadphaseEnabled() const noexcept { return m_broadphaseEnabled; }

                void setChunkSize(size_t size) noexcept { m_chunkSize = std::max(size, size_t{16}); }
                [[nodiscard]] size_t getChunkSize() const noexcept { return m_chunkSize; }

            private:
                bool m_parallelEnabled = true;
                bool m_broadphaseEnabled = true;
                size_t m_chunkSize = 32;
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SYSTEM_CULLINGSYSTEM_H
