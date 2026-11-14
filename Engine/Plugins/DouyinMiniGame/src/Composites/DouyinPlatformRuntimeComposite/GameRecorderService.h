/**
 * @file GameRecorderService.h
 * @brief FEAT-06：抖音游戏画面环形录屏（上限 300s）、高光片段裁剪与一键拍抖音话题挂载服务
 */
#ifndef MQENGINE_DOUYIN_GAME_RECORDER_SERVICE_H
#define MQENGINE_DOUYIN_GAME_RECORDER_SERVICE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"

namespace MQEngine::DouyinMiniGame
{
    enum class RecorderState : uint8_t
    {
        Idle = 0,
        Recording = 1,
        Paused = 2
    };

    /**
     * @brief 抖音短视频录屏与高光分享服务（FEAT-06）
     */
    class DOUYIN_PLATFORM_RUNTIME_API GameRecorderService
    {
    public:
        static constexpr int32_t kMaxDurationSeconds = 300;
        static constexpr double kMinValidRecordDurationSec = 3.0;

        explicit GameRecorderService(IDouyinHostApi* hostApi = nullptr);
        ~GameRecorderService() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }

        Status start(int32_t requestedDurationSec = kMaxDurationSeconds);
        Status pause();
        Status resume();
        StatusOr<std::string> stop(double recordedElapsedSec = 5.0);

        Status recordHighlightClip(double beforeSec, double afterSec);

        void shareVideo(
            const DouyinShareVideoRequest& request,
            std::function<void(Status)> callback);

        RecorderState getState() const noexcept { return m_state; }
        int32_t getActiveDurationLimitSec() const noexcept { return m_activeDurationSec; }
        const std::string& getLastVideoPath() const noexcept { return m_lastVideoPath; }
        uint32_t getRecordedClipCount() const noexcept { return m_clipCount; }

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        RecorderState m_state{RecorderState::Idle};
        int32_t m_activeDurationSec{kMaxDurationSeconds};
        std::string m_lastVideoPath;
        uint32_t m_clipCount{0};
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_GAME_RECORDER_SERVICE_H
