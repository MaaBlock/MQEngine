/**
 * @file SpatialGraph.h
 * @brief 空间加速图 (SpatialGraph)：
 *        动态松散 BVH (Loose Bounding Volume Hierarchy) 空间划分加速结构，
 *        支持 O(log N) 快速视锥/AABB 范围裁剪与射线求交，无锁只读快照发布
 */
#ifndef MQENGINE_DOMAIN_SCENE_SPATIALGRAPH_H
#define MQENGINE_DOMAIN_SCENE_SPATIALGRAPH_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 轴对齐包围盒 (AABB)
             */
            struct ENGINE_API AABB
            {
                FCT::Vec3<> min{-0.5f, -0.5f, -0.5f};
                FCT::Vec3<> max{ 0.5f,  0.5f,  0.5f};

                bool intersects(const AABB& other) const noexcept
                {
                    return (min.x <= other.max.x && max.x >= other.min.x) &&
                           (min.y <= other.max.y && max.y >= other.min.y) &&
                           (min.z <= other.max.z && max.z >= other.min.z);
                }

                bool contains(const FCT::Vec3<>& pt) const noexcept
                {
                    return (pt.x >= min.x && pt.x <= max.x) &&
                           (pt.y >= min.y && pt.y <= max.y) &&
                           (pt.z >= min.z && pt.z <= max.z);
                }

                bool contains(const AABB& other) const noexcept
                {
                    return (min.x <= other.min.x && max.x >= other.max.x) &&
                           (min.y <= other.min.y && max.y >= other.max.y) &&
                           (min.z <= other.min.z && max.z >= other.max.z);
                }

                static AABB merge(const AABB& a, const AABB& b) noexcept
                {
                    return AABB{
                        FCT::Vec3<>(
                            std::min(a.min.x, b.min.x),
                            std::min(a.min.y, b.min.y),
                            std::min(a.min.z, b.min.z)),
                        FCT::Vec3<>(
                            std::max(a.max.x, b.max.x),
                            std::max(a.max.y, b.max.y),
                            std::max(a.max.z, b.max.z))
                    };
                }

                float surfaceArea() const noexcept
                {
                    FCT::Vec3<> d = max - min;
                    if (d.x < 0.0f || d.y < 0.0f || d.z < 0.0f) {
                        return 0.0f;
                    }
                    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
                }
            };

            /**
             * @brief 松散 BVH 节点定义
             */
            struct BvhNode
            {
                AABB tightBox;
                AABB looseBox;
                int32_t leftChild = -1;
                int32_t rightChild = -1;
                int32_t parent = -1;
                FCT::ECS::entity entity = FCT::ECS::null;
                bool isLeaf = false;
            };

            /**
             * @brief 双缓冲 BVH 快照，供并发读取与零锁遍历
             */
            struct BvhSnapshot
            {
                std::vector<BvhNode> nodes;
                int32_t root = -1;
                size_t count = 0;
            };

            /**
             * @brief 动态松散 BVH 空间加速图核心类
             */
            class ENGINE_API SpatialGraph
            {
            public:
                SpatialGraph();
                ~SpatialGraph() = default;

                SpatialGraph(const SpatialGraph&) = delete;
                SpatialGraph& operator=(const SpatialGraph&) = delete;

                /**
                 * @brief 插入或更新实体的空间包围盒
                 */
                void updateEntity(FCT::ECS::entity entity, const FCT::Vec3<>& aabbMin, const FCT::Vec3<>& aabbMax);

                /**
                 * @brief 移除实体空间索引
                 */
                void removeEntity(FCT::ECS::entity entity);

                /**
                 * @brief AABB 范围重叠查询 (层次剪枝快速求交)
                 */
                std::vector<FCT::ECS::entity> queryAABB(const AABB& range) const;

                /**
                 * @brief 射线求交检测 (层次遍历与距离排序)
                 */
                std::vector<FCT::ECS::entity> raycast(const FCT::Vec3<>& origin, const FCT::Vec3<>& dir, float maxDist) const;

                /**
                 * @brief 发布最新修改为无锁只读快照
                 */
                void publishSnapshot();

                /**
                 * @brief 清空空间图
                 */
                void clear();

                size_t size() const noexcept;

            private:
                int32_t allocateNode();
                void freeNode(int32_t nodeIdx);
                void insertLeaf(int32_t leafIdx);
                void removeLeaf(int32_t leafIdx);
                void refitHierarchy(int32_t nodeIdx);

                static AABB computeLooseBox(const AABB& tight, float factor = 0.25f, float margin = 0.2f) noexcept;

            private:
                std::vector<BvhNode> m_nodes;
                std::vector<int32_t> m_freeNodes;
                int32_t m_root = -1;
                std::unordered_map<uint32_t, int32_t> m_entityToNode;
                std::atomic<size_t> m_itemCount{0};

                // 双缓冲无锁只读快照
                BvhSnapshot m_snapshots[2];
                std::atomic<uint32_t> m_activeSnapshotIndex{0};
                bool m_dirty = false;
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SPATIALGRAPH_H
