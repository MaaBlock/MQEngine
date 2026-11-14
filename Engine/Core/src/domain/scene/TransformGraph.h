/**
 * @file TransformGraph.h
 * @brief 变换传播图 (TransformGraph)：
 *        零堆分配侵入式四指针 HierarchyRelation + depth 分桶 WorkHost 并行求值
 */
#ifndef MQENGINE_DOMAIN_SCENE_TRANSFORMGRAPH_H
#define MQENGINE_DOMAIN_SCENE_TRANSFORMGRAPH_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/resource/table/EntityKey.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 零堆分配侵入式四指针层级关系组件
             *        完全基于 Entity 主键，内部无任何 std::vector 分配
             */
            struct ENGINE_API HierarchyRelation
            {
                FCT::ECS::entity parent = FCT::ECS::null;
                FCT::ECS::entity firstChild = FCT::ECS::null;
                FCT::ECS::entity prevSibling = FCT::ECS::null;
                FCT::ECS::entity nextSibling = FCT::ECS::null;
                uint32_t depth = 0;
            };

            /**
             * @brief 本地变换组件
             */
            struct ENGINE_API LocalTransform
            {
                FCT::Vec3<> position{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> rotation{0.0f, 0.0f, 0.0f}; // 欧拉角 (度数或弧度)
                FCT::Vec3<> scale{1.0f, 1.0f, 1.0f};
                uint32_t dirtyVersion = 0;
                bool isDirty = true;
            };

            /**
             * @brief 世界变换与包围盒组件
             *        GPU 资源直接以 FCT 裸指针作为天然主键与引用键
             */
            struct ENGINE_API WorldTransform
            {
                FCT::Mat4 worldMatrix;
                FCT::Vec3<> aabbMin{-0.5f, -0.5f, -0.5f};
                FCT::Vec3<> aabbMax{0.5f, 0.5f, 0.5f};
                FCT::StaticMesh<uint32_t>* mesh = nullptr;
                FCT::Image* texture = nullptr;
                bool isVisible = true;
            };

            /**
             * @brief 变换传播图管理器
             */
            class ENGINE_API TransformGraph
            {
            public:
                TransformGraph() = default;
                ~TransformGraph() = default;

                /**
                 * @brief 建立父子层级关系
                 */
                void attachChild(FCT::ECS::registry& registry, FCT::ECS::entity parent, FCT::ECS::entity child);

                /**
                 * @brief 断开与父节点的关联
                 */
                void detachChild(FCT::ECS::registry& registry, FCT::ECS::entity child);

                /**
                 * @brief 标记指定实体及其整棵子树的变换为脏
                 */
                void markDirty(FCT::ECS::registry& registry, FCT::ECS::entity entity);

                /**
                 * @brief 按深度分桶，利用 WorkHost 多线程并行计算所有 Dirty 节点的 WorldMatrix
                 */
                void evaluateParallel(FCT::ECS::registry& registry);

            private:
                void updateDepthRecursive(FCT::ECS::registry& registry, FCT::ECS::entity entity, uint32_t depth);
                void markSubtreeDirty(FCT::ECS::registry& registry, FCT::ECS::entity entity);

            private:
                static constexpr uint32_t kMaxDepth = 64;
                std::vector<FCT::ECS::entity> m_depthBuckets[kMaxDepth];
                uint32_t m_currentMaxDepth = 0;
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_TRANSFORMGRAPH_H
