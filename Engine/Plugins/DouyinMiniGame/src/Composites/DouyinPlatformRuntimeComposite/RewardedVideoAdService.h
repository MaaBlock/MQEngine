/**
 * @file RewardedVideoAdService.h
 * @brief FEAT-05：抖音激励视频广告生命周期、音频自动静音恢复与严格完播校验防误发服务
 */
#ifndef MQENGINE_DOUYIN_REWARDED_VIDEO_AD_SERVICE_H
#define MQENGINE_DOUYIN_REWARDED_VIDEO_AD_SERVICE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 激励视频广告播放结算结果
     */
    struct RewardedAdCloseResult
    {
        bool rewardGranted{false};
        bool isEnded{false};
        int32_t errorCode{0};
        std::string errorMessage;
    };

    /**
     * @brief 抖音激励视频广告服务（FEAT-05）
     */
    class DOUYIN_PLATFORM_RUNTIME_API RewardedVideoAdService
    {
    public:
        using RewardResultCallback = std::function<void(const RewardedAdCloseResult&)>;
        using AudioMuteCallback = std::function<void(bool shouldMute)>;

        explicit RewardedVideoAdService(IDouyinHostApi* hostApi = nullptr);
        ~RewardedVideoAdService() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }
        void setAudioMuteCallback(AudioMuteCallback callback) { m_audioMuteCallback = std::move(callback); }

        Status initialize(const std::string& adUnitId);
        Status preload();
        Status show(RewardResultCallback onResult);

        void notifyAdClosed(bool isEnded);
        void notifyAdError(int32_t errCode, const std::string& errMsg);

        bool isInitialized() const noexcept { return m_initialized; }
        bool isShowing() const noexcept { return m_isShowing; }
        bool isPreloaded() const noexcept { return m_isPreloaded; }
        bool isEngineAudioMuted() const noexcept { return m_engineAudioMuted; }
        uint32_t getGrantedRewardCount() const noexcept { return m_grantedRewardCount; }
        const std::string& getAdUnitId() const noexcept { return m_adUnitId; }

    private:
        void applyAudioMuteState(bool mute);

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        AudioMuteCallback m_audioMuteCallback;
        RewardResultCallback m_pendingCallback;
        std::string m_adUnitId;
        bool m_initialized{false};
        bool m_isPreloaded{false};
        bool m_isShowing{false};
        bool m_engineAudioMuted{false};
        uint32_t m_grantedRewardCount{0};
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_REWARDED_VIDEO_AD_SERVICE_H
