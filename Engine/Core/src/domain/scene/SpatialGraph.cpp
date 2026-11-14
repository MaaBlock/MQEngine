/**
 * @file SpatialGraph.cpp
 * @brief 动态松散 BVH 空间划分加速结构实现
 */
#include <MQEngine/domain/scene/SpatialGraph.h>
#include <algorithm>
#include <array>
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
                // AABB 射线求交测试 (Slab Method)，计算相交最近距离
                inline bool rayIntersectsAABB(
                    const FCT::Vec3<>& origin,
                    const FCT::Vec3<>& dir,
                    float maxDist,
                    const AABB& box,
                    float& outDist)
                {
                    float tmin = 0.0f;
                    float tmax = maxDist;

                    for (int i = 0; i < 3; ++i) {
                        float o = (i == 0) ? origin.x : (i == 1 ? origin.y : origin.z);
                        float d = (i == 0) ? dir.x : (i == 1 ? dir.y : dir.z);
                        float bmin = (i == 0) ? box.min.x : (i == 1 ? box.min.y : box.min.z);
                        float bmax = (i == 0) ? box.max.x : (i == 1 ? box.max.y : box.max.z);

                        if (std::abs(d) < 1e-6f) {
                            if (o < bmin || o > bmax) return false;
                        } else {
                            float invD = 1.0f / d;
                            float t1 = (bmin - o) * invD;
                            float t2 = (bmax - o) * invD;
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) return false;
                        }
                    }
                    if (tmax < 0.0f || tmin > maxDist) {
                        return false;
                    }
                    outDist = (tmin >= 0.0f) ? tmin : tmax;
                    return true;
                }
            } // namespace

            SpatialGraph::SpatialGraph()
            {
                m_nodes.reserve(512);
                m_snapshots[0].nodes.reserve(512);
                m_snapshots[1].nodes.reserve(512);
            }

            AABB SpatialGraph::computeLooseBox(const AABB& tight, float factor, float margin) noexcept
            {
                FCT::Vec3<> ext = (tight.max - tight.min) * (factor * 0.5f) + FCT::Vec3<>(margin, margin, margin);
                return AABB{tight.min - ext, tight.max + ext};
            }

            int32_t SpatialGraph::allocateNode()
            {
                if (!m_freeNodes.empty()) {
                    int32_t idx = m_freeNodes.back();
                    m_freeNodes.pop_back();
                    m_nodes[idx] = BvhNode{};
                    return idx;
                }
                int32_t idx = static_cast<int32_t>(m_nodes.size());
                m_nodes.emplace_back();
                return idx;
            }

            void SpatialGraph::freeNode(int32_t nodeIdx)
            {
                assert(nodeIdx >= 0 && nodeIdx < static_cast<int32_t>(m_nodes.size()));
                m_nodes[nodeIdx] = BvhNode{};
                m_freeNodes.push_back(nodeIdx);
            }

            void SpatialGraph::insertLeaf(int32_t leafIdx)
            {
                if (m_root == -1) {
                    m_root = leafIdx;
                    m_nodes[leafIdx].parent = -1;
                    return;
                }

                const AABB& leafAABB = m_nodes[leafIdx].tightBox;
                int32_t cur = m_root;

                // 启发式向下寻找最优插入分支
                while (!m_nodes[cur].isLeaf) {
                    int32_t left = m_nodes[cur].leftChild;
                    int32_t right = m_nodes[cur].rightChild;

                    float area = m_nodes[cur].tightBox.surfaceArea();
                    AABB combined = AABB::merge(m_nodes[cur].tightBox, leafAABB);
                    float combinedArea = combined.surfaceArea();

                    float costInherited = 2.0f * (combinedArea - area);

                    // 计算左子树插入代价
                    float costLeft;
                    if (m_nodes[left].isLeaf) {
                        costLeft = AABB::merge(m_nodes[left].tightBox, leafAABB).surfaceArea() + costInherited;
                    } else {
                        float oldArea = m_nodes[left].tightBox.surfaceArea();
                        float newArea = AABB::merge(m_nodes[left].tightBox, leafAABB).surfaceArea();
                        costLeft = (newArea - oldArea) + costInherited;
                    }

                    // 计算右子树插入代价
                    float costRight;
                    if (m_nodes[right].isLeaf) {
                        costRight = AABB::merge(m_nodes[right].tightBox, leafAABB).surfaceArea() + costInherited;
                    } else {
                        float oldArea = m_nodes[right].tightBox.surfaceArea();
                        float newArea = AABB::merge(m_nodes[right].tightBox, leafAABB).surfaceArea();
                        costRight = (newArea - oldArea) + costInherited;
                    }

                    if (combinedArea < costLeft && combinedArea < costRight) {
                        break;
                    }

                    cur = (costLeft < costRight) ? left : right;
                }

                int32_t oldParent = m_nodes[cur].parent;
                int32_t newParent = allocateNode();
                m_nodes[newParent].parent = oldParent;
                m_nodes[newParent].tightBox = AABB::merge(leafAABB, m_nodes[cur].tightBox);
                m_nodes[newParent].looseBox = computeLooseBox(m_nodes[newParent].tightBox);
                m_nodes[newParent].isLeaf = false;

                if (oldParent != -1) {
                    if (m_nodes[oldParent].leftChild == cur) {
                        m_nodes[oldParent].leftChild = newParent;
                    } else {
                        m_nodes[oldParent].rightChild = newParent;
                    }
                } else {
                    m_root = newParent;
                }

                m_nodes[newParent].leftChild = cur;
                m_nodes[newParent].rightChild = leafIdx;
                m_nodes[cur].parent = newParent;
                m_nodes[leafIdx].parent = newParent;

                refitHierarchy(newParent);
            }

            void SpatialGraph::removeLeaf(int32_t leafIdx)
            {
                if (leafIdx == m_root) {
                    m_root = -1;
                    return;
                }

                int32_t parent = m_nodes[leafIdx].parent;
                int32_t grandParent = m_nodes[parent].parent;
                int32_t sibling = (m_nodes[parent].leftChild == leafIdx)
                                      ? m_nodes[parent].rightChild
                                      : m_nodes[parent].leftChild;

                if (grandParent != -1) {
                    if (m_nodes[grandParent].leftChild == parent) {
                        m_nodes[grandParent].leftChild = sibling;
                    } else {
                        m_nodes[grandParent].rightChild = sibling;
                    }
                    m_nodes[sibling].parent = grandParent;
                    freeNode(parent);
                    refitHierarchy(grandParent);
                } else {
                    m_root = sibling;
                    m_nodes[sibling].parent = -1;
                    freeNode(parent);
                }
            }

            void SpatialGraph::refitHierarchy(int32_t nodeIdx)
            {
                int32_t cur = nodeIdx;
                while (cur != -1) {
                    int32_t left = m_nodes[cur].leftChild;
                    int32_t right = m_nodes[cur].rightChild;

                    assert(left != -1 && right != -1);
                    m_nodes[cur].tightBox = AABB::merge(m_nodes[left].tightBox, m_nodes[right].tightBox);
                    m_nodes[cur].looseBox = computeLooseBox(m_nodes[cur].tightBox);

                    cur = m_nodes[cur].parent;
                }
            }

            void SpatialGraph::updateEntity(
                FCT::ECS::entity entity,
                const FCT::Vec3<>& aabbMin,
                const FCT::Vec3<>& aabbMax)
            {
                if (entity == FCT::ECS::null) {
                    return;
                }

                uint32_t id = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                AABB tight{aabbMin, aabbMax};

                auto it = m_entityToNode.find(id);
                if (it != m_entityToNode.end()) {
                    int32_t leafIdx = it->second;
                    // 若新包围盒仍在松散包围盒内，则仅更新紧致包围盒，避免频繁重构子树
                    if (m_nodes[leafIdx].looseBox.contains(tight)) {
                        m_nodes[leafIdx].tightBox = tight;
                        m_dirty = true;
                        publishSnapshot();
                        return;
                    }

                    removeLeaf(leafIdx);
                    m_nodes[leafIdx].tightBox = tight;
                    m_nodes[leafIdx].looseBox = computeLooseBox(tight);
                    insertLeaf(leafIdx);
                } else {
                    int32_t leafIdx = allocateNode();
                    m_nodes[leafIdx].entity = entity;
                    m_nodes[leafIdx].tightBox = tight;
                    m_nodes[leafIdx].looseBox = computeLooseBox(tight);
                    m_nodes[leafIdx].isLeaf = true;
                    m_nodes[leafIdx].leftChild = -1;
                    m_nodes[leafIdx].rightChild = -1;

                    insertLeaf(leafIdx);
                    m_entityToNode[id] = leafIdx;
                    m_itemCount.fetch_add(1, std::memory_order_relaxed);
                }

                m_dirty = true;
                publishSnapshot();
            }

            void SpatialGraph::removeEntity(FCT::ECS::entity entity)
            {
                if (entity == FCT::ECS::null) {
                    return;
                }

                uint32_t id = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                auto it = m_entityToNode.find(id);
                if (it != m_entityToNode.end()) {
                    int32_t leafIdx = it->second;
                    removeLeaf(leafIdx);
                    freeNode(leafIdx);
                    m_entityToNode.erase(it);
                    m_itemCount.fetch_sub(1, std::memory_order_relaxed);
                    m_dirty = true;
                    publishSnapshot();
                }
            }

            void SpatialGraph::publishSnapshot()
            {
                if (!m_dirty) {
                    return;
                }

                uint32_t currentActive = m_activeSnapshotIndex.load(std::memory_order_relaxed);
                uint32_t nextWrite = 1 - currentActive;

                m_snapshots[nextWrite].nodes = m_nodes;
                m_snapshots[nextWrite].root = m_root;
                m_snapshots[nextWrite].count = m_itemCount.load(std::memory_order_relaxed);

                m_activeSnapshotIndex.store(nextWrite, std::memory_order_release);
                m_dirty = false;
            }

            std::vector<FCT::ECS::entity> SpatialGraph::queryAABB(const AABB& range) const
            {
                std::vector<FCT::ECS::entity> hits;
                uint32_t activeIdx = m_activeSnapshotIndex.load(std::memory_order_acquire);
                const auto& snapshot = m_snapshots[activeIdx];

                if (snapshot.root == -1 || snapshot.nodes.empty()) {
                    return hits;
                }

                hits.reserve(std::min<size_t>(snapshot.count, 64));

                // 栈式非递归层次求交，栈深度最大 128
                std::array<int32_t, 128> stack;
                int32_t stackTop = 0;
                stack[stackTop++] = snapshot.root;

                while (stackTop > 0) {
                    int32_t nodeIdx = stack[--stackTop];
                    if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(snapshot.nodes.size())) {
                        continue;
                    }
                    const auto& node = snapshot.nodes[nodeIdx];

                    // 松散包围盒初筛：若松散包围盒不相交，直接裁剪整棵子树
                    if (!range.intersects(node.looseBox)) {
                        continue;
                    }

                    if (node.isLeaf) {
                        // 叶子节点验证精确紧致包围盒
                        if (range.intersects(node.tightBox)) {
                            hits.push_back(node.entity);
                        }
                    } else {
                        if (node.leftChild != -1 && stackTop < 127) {
                            stack[stackTop++] = node.leftChild;
                        }
                        if (node.rightChild != -1 && stackTop < 127) {
                            stack[stackTop++] = node.rightChild;
                        }
                    }
                }

                return hits;
            }

            std::vector<FCT::ECS::entity> SpatialGraph::raycast(
                const FCT::Vec3<>& origin,
                const FCT::Vec3<>& dir,
                float maxDist) const
            {
                struct HitPair
                {
                    float dist;
                    FCT::ECS::entity entity;
                };

                std::vector<HitPair> hitPairs;
                uint32_t activeIdx = m_activeSnapshotIndex.load(std::memory_order_acquire);
                const auto& snapshot = m_snapshots[activeIdx];

                if (snapshot.root == -1 || snapshot.nodes.empty()) {
                    return {};
                }

                hitPairs.reserve(std::min<size_t>(snapshot.count, 32));

                std::array<int32_t, 128> stack;
                int32_t stackTop = 0;
                stack[stackTop++] = snapshot.root;

                while (stackTop > 0) {
                    int32_t nodeIdx = stack[--stackTop];
                    if (nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(snapshot.nodes.size())) {
                        continue;
                    }
                    const auto& node = snapshot.nodes[nodeIdx];

                    float looseDist = 0.0f;
                    if (!rayIntersectsAABB(origin, dir, maxDist, node.looseBox, looseDist)) {
                        continue;
                    }

                    if (node.isLeaf) {
                        float exactDist = 0.0f;
                        if (rayIntersectsAABB(origin, dir, maxDist, node.tightBox, exactDist)) {
                            hitPairs.push_back({exactDist, node.entity});
                        }
                    } else {
                        if (node.leftChild != -1 && stackTop < 127) {
                            stack[stackTop++] = node.leftChild;
                        }
                        if (node.rightChild != -1 && stackTop < 127) {
                            stack[stackTop++] = node.rightChild;
                        }
                    }
                }

                std::sort(hitPairs.begin(), hitPairs.end(), [](const HitPair& a, const HitPair& b) {
                    return a.dist < b.dist;
                });

                std::vector<FCT::ECS::entity> hits;
                hits.reserve(hitPairs.size());
                for (const auto& hp : hitPairs) {
                    hits.push_back(hp.entity);
                }
                return hits;
            }

            void SpatialGraph::clear()
            {
                m_nodes.clear();
                m_freeNodes.clear();
                m_root = -1;
                m_entityToNode.clear();
                m_itemCount.store(0, std::memory_order_relaxed);
                m_dirty = true;
                publishSnapshot();
            }

            size_t SpatialGraph::size() const noexcept
            {
                return m_itemCount.load(std::memory_order_relaxed);
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine
