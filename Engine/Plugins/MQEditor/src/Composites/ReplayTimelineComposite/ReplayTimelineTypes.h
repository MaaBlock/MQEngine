/**
 * @file ReplayTimelineTypes.h
 * @brief ECS 内存数据库时间旅行回放实体变换快照、帧记录、关键帧书签与播放状态定义
 */
#ifndef MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_TYPES_H
#define MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_TYPES_H

#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 单个 ECS 实体的局部变换与可见性快照
         */
        struct EntityTransformSnapshot
        {
            uint32_t entityId = 0;
            FCT::Vec3<float> position{0.0f, 0.0f, 0.0f};
            FCT::Vec3<float> rotation{0.0f, 0.0f, 0.0f};
            FCT::Vec3<float> scale{1.0f, 1.0f, 1.0f};
            bool isVisible = true;
        };

        using EntitySnapshot = EntityTransformSnapshot;

        /**
         * @brief 单帧全场景 ECS 实体快照记录
         */
        struct ReplayFrameRecord
        {
            uint32_t frameIndex = 0;
            float timestampSec = 0.0f;
            std::vector<EntityTransformSnapshot> entities;
        };

        using FrameSnapshot = ReplayFrameRecord;

        /**
         * @brief 时间轴关键帧书签标记
         */
        struct ReplayBookmark
        {
            uint32_t frameIndex = 0;
            std::string label = "Keyframe";
        };

        /**
         * @brief 回放时间轴播放状态枚举
         */
        enum class ReplayPlaybackState : uint8_t
        {
            Stopped = 0,
            Recording,
            Playing,
            Paused
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_TYPES_H
