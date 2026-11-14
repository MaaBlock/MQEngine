/**
 * @file SceneReplay.h
 * @brief ECS 内存数据库回放系统 (SceneReplay)：
 *        支持全量关键帧快照 (Keyframe Snapshot) + 帧间增量日志 (Frame Delta Log)，
 *        实现时间旅行调试 (Time-Travel)、录像回放与编辑器 PIE 秒级快照回滚
 */
#ifndef MQENGINE_DOMAIN_SCENE_SCENEREPLAY_H
#define MQENGINE_DOMAIN_SCENE_SCENEREPLAY_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/scene/TransformGraph.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 实体快照行记录
             */
            struct EntitySnapshotRecord
            {
                uint32_t entityId = 0;
                LocalTransform localTransform;
                WorldTransform worldTransform;
                HierarchyRelation hierarchy;
            };

            /**
             * @brief 全量关键帧快照
             */
            struct KeyframeSnapshot
            {
                uint64_t frameIndex = 0;
                std::vector<EntitySnapshotRecord> entities;
            };

            /**
             * @brief 帧间增量变更项
             */
            struct DeltaLogEntry
            {
                uint64_t frameIndex = 0;
                uint32_t entityId = 0;
                LocalTransform newLocalTransform;
            };

            /**
             * @brief ECS 场景回放核心类
             */
            class ENGINE_API SceneReplay
            {
            public:
                SceneReplay() = default;
                ~SceneReplay() = default;

                /**
                 * @brief 开始录制
                 */
                void startRecording();

                /**
                 * @brief 停止录制
                 */
                void stopRecording();

                bool isRecording() const noexcept { return m_recording; }

                /**
                 * @brief 捕获全量关键帧快照
                 */
                void captureKeyframe(uint64_t frameIndex, FCT::ECS::registry& registry);

                /**
                 * @brief 记录帧间增量变更
                 */
                void recordDelta(uint64_t frameIndex, uint32_t entityId, const LocalTransform& local);

                /**
                 * @brief 时间轴跳转与回放：恢复至指定帧编号
                 */
                Status scrubToFrame(uint64_t targetFrame, FCT::ECS::registry& registry);

                /**
                 * @brief 编辑器 PIE 进入时保存第 0 帧基线快照
                 */
                void savePieSnapshot(FCT::ECS::registry& registry);

                /**
                 * @brief 编辑器 PIE 退出时一键极速回滚至第 0 帧快照
                 */
                Status restorePieSnapshot(FCT::ECS::registry& registry);

                /**
                 * @brief 清空全部录像缓存
                 */
                void clear();

                uint64_t getRecordedFrameCount() const noexcept;

            private:
                bool m_recording = false;
                std::map<uint64_t, KeyframeSnapshot> m_keyframes;
                std::vector<DeltaLogEntry> m_deltaLogs;
                KeyframeSnapshot m_pieBaselineSnapshot;
            };

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SCENEREPLAY_H
