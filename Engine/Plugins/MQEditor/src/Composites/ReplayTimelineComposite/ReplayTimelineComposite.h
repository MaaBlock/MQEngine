/**
 * @file ReplayTimelineComposite.h
 * @brief ECS 内存数据库时间旅行回放与 PIE 时间轴 Composite（tags: [Editor, ImGui]，独立编译为 ReplayTimelineComposite.dll）
 */
#ifndef MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "ReplayTimelineTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief ECS 内存数据库时间旅行回放 Composite：
         *        作为全引擎唯一的 ECS 回放时间轴 ("ECS 回放时间轴 (ECS Replay Timeline)") 权威持有者，
         *        提供环形缓冲区快照录制、时间轴滑块拖拽穿梭、关键帧书签标记、实体状态差异检视与 .mqreplay 序列化
         */
        class REPLAY_TIMELINE_COMPOSITE_API ReplayTimelineComposite : public Asset::Composite
        {
        public:
            ReplayTimelineComposite();
            ~ReplayTimelineComposite() override;

            ReplayTimelineComposite(const ReplayTimelineComposite&) = delete;
            ReplayTimelineComposite& operator=(const ReplayTimelineComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "ReplayTimelineComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 时间旅行控制与 .mqreplay 序列化 ====================

            void startRecording();
            void stopAndRewindToStart();
            void play();
            void pause();
            void scrubToFrame(uint32_t targetFrame);
            void stepForward();
            void stepBackward();
            void addKeyframeMarker(uint32_t frameIdx);
            void removeKeyframeMarker(uint32_t frameIdx);

            Status saveReplayToFile(const std::string& filePath);
            Status loadReplayFromFile(const std::string& filePath);

            ReplayPlaybackState getPlaybackState() const noexcept { return m_state; }
            uint32_t getCurrentFrame() const noexcept { return m_currentFrame; }
            uint32_t getTotalRecordedFrames() const noexcept { return static_cast<uint32_t>(m_history.size()); }

        private:
            // 环形快照录制与场景还原 (ReplayTimelineComposite.cpp)
            void recordCurrentSceneSnapshot();
            void restoreSnapshotToScene(const ReplayFrameRecord& snapshot);

            // 时间轴拖拽 UI、关键帧书签与实体状态差异检视面板 (ReplayTimelinePanel.cpp)
            void renderTimelineWindow();
            void renderPlaybackControls();
            void renderTimelineScrubberAndBookmarks();
            void renderEntityStateDiffInspector();

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;

            std::vector<ReplayFrameRecord> m_history;
            std::vector<uint32_t> m_keyframeMarkers;
            std::vector<ReplayBookmark> m_bookmarks;

            ReplayPlaybackState m_state = ReplayPlaybackState::Stopped;
            uint32_t m_currentFrame = 0;
            float m_accumulatedTime = 0.0f;
            float m_playbackSpeed = 1.0f;
            bool m_loopPlayback = true;
            char m_replayFilePath[128] = "Saved/Replays/Session.mqreplay";

            // 第 0 帧快照基底（用于 Stop 一键回退与状态 Diff 对比）
            ReplayFrameRecord m_initialSnapshot;
            bool m_hasInitialSnapshot = false;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_REPLAY_TIMELINE_COMPOSITE_H
