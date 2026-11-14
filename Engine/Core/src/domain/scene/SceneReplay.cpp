/**
 * @file SceneReplay.cpp
 * @brief ECS 场景回放系统实现 (严格无锁设计，由 LogicHost 线程单一所有权调度)
 */
#include <MQEngine/domain/scene/SceneReplay.h>
#include <algorithm>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            void SceneReplay::startRecording()
            {
                m_recording = true;
            }

            void SceneReplay::stopRecording()
            {
                m_recording = false;
            }

            void SceneReplay::captureKeyframe(uint64_t frameIndex, FCT::ECS::registry& registry)
            {
                KeyframeSnapshot snapshot;
                snapshot.frameIndex = frameIndex;

                auto view = registry.view<LocalTransform, WorldTransform>();
                for (auto entity : view) {
                    EntitySnapshotRecord record;
                    record.entityId = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                    record.localTransform = view.get<LocalTransform>(entity);
                    record.worldTransform = view.get<WorldTransform>(entity);

                    auto* rel = registry.try_get<HierarchyRelation>(entity);
                    if (rel) {
                        record.hierarchy = *rel;
                    }
                    snapshot.entities.push_back(record);
                }

                m_keyframes[frameIndex] = std::move(snapshot);
            }

            void SceneReplay::recordDelta(
                uint64_t frameIndex,
                uint32_t entityId,
                const LocalTransform& local)
            {
                if (!m_recording) return;

                m_deltaLogs.push_back({frameIndex, entityId, local});
            }

            Status SceneReplay::scrubToFrame(uint64_t targetFrame, FCT::ECS::registry& registry)
            {
                if (m_keyframes.empty()) {
                    return NotFoundError("未找到任何关键帧快照");
                }

                // 寻找小于等于 targetFrame 的最大关键帧
                auto it = m_keyframes.upper_bound(targetFrame);
                if (it != m_keyframes.begin()) {
                    --it;
                } else {
                    it = m_keyframes.begin();
                }

                const auto& baseSnapshot = it->second;

                // 1. 恢复基础关键帧中的所有实体组件
                for (const auto& record : baseSnapshot.entities) {
                    auto entity = static_cast<FCT::ECS::entity>(record.entityId);
                    if (registry.valid(entity)) {
                        registry.emplace_or_replace<LocalTransform>(entity, record.localTransform);
                        registry.emplace_or_replace<WorldTransform>(entity, record.worldTransform);
                        registry.emplace_or_replace<HierarchyRelation>(entity, record.hierarchy);
                    }
                }

                // 2. 从关键帧起始重放增量日志至目标帧
                uint64_t startFrame = baseSnapshot.frameIndex;
                for (const auto& delta : m_deltaLogs) {
                    if (delta.frameIndex > startFrame && delta.frameIndex <= targetFrame) {
                        auto entity = static_cast<FCT::ECS::entity>(delta.entityId);
                        if (registry.valid(entity)) {
                            auto* local = registry.try_get<LocalTransform>(entity);
                            if (local) {
                                *local = delta.newLocalTransform;
                                local->isDirty = true;
                            }
                        }
                    }
                }

                return OkStatus();
            }

            void SceneReplay::savePieSnapshot(FCT::ECS::registry& registry)
            {
                m_pieBaselineSnapshot.frameIndex = 0;
                m_pieBaselineSnapshot.entities.clear();

                auto view = registry.view<LocalTransform, WorldTransform>();
                for (auto entity : view) {
                    EntitySnapshotRecord record;
                    record.entityId = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                    record.localTransform = view.get<LocalTransform>(entity);
                    record.worldTransform = view.get<WorldTransform>(entity);

                    auto* rel = registry.try_get<HierarchyRelation>(entity);
                    if (rel) {
                        record.hierarchy = *rel;
                    }
                    m_pieBaselineSnapshot.entities.push_back(record);
                }
            }

            Status SceneReplay::restorePieSnapshot(FCT::ECS::registry& registry)
            {
                if (m_pieBaselineSnapshot.entities.empty()) {
                    return NotFoundError("未找到 PIE 基线快照");
                }

                for (const auto& record : m_pieBaselineSnapshot.entities) {
                    auto entity = static_cast<FCT::ECS::entity>(record.entityId);
                    if (registry.valid(entity)) {
                        registry.emplace_or_replace<LocalTransform>(entity, record.localTransform);
                        registry.emplace_or_replace<WorldTransform>(entity, record.worldTransform);
                        registry.emplace_or_replace<HierarchyRelation>(entity, record.hierarchy);
                    }
                }

                return OkStatus();
            }

            void SceneReplay::clear()
            {
                m_keyframes.clear();
                m_deltaLogs.clear();
                m_pieBaselineSnapshot.entities.clear();
                m_recording = false;
            }

            uint64_t SceneReplay::getRecordedFrameCount() const noexcept
            {
                if (m_deltaLogs.empty()) return 0;
                return m_deltaLogs.back().frameIndex;
            }

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine
