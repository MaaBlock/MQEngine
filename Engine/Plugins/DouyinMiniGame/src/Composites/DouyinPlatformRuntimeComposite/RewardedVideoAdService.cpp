/**
 * @file RewardedVideoAdService.cpp
 * @brief FEAT-05 抖音激励视频广告静音联动与 res.isEnded === true 严格发奖实现
 */
#include "RewardedVideoAdService.h"

namespace MQEngine::DouyinMiniGame
{
    RewardedVideoAdService::RewardedVideoAdService(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    Status RewardedVideoAdService::initialize(const std::string& adUnitId)
    {
        if (adUnitId.empty())
        {
            return InvalidArgumentError("激励视频广告位 ID (adUnitId) 不能为空");
        }
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->createRewardedVideoAd(adUnitId));
        }
        m_adUnitId = adUnitId;
        m_initialized = true;
        return preload();
    }

    Status RewardedVideoAdService::preload()
    {
        if (!m_initialized || m_adUnitId.empty())
        {
            return FailedPreconditionError("激励视频广告尚未调用 initialize 初始化广告位 ID");
        }
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->loadRewardedVideoAd());
        }
        m_isPreloaded = true;
        return OkStatus();
    }

    Status RewardedVideoAdService::show(RewardResultCallback onResult)
    {
        if (!m_initialized || m_adUnitId.empty())
        {
            return FailedPreconditionError("激励视频广告尚未初始化，无法拉起播放");
        }
        if (m_isShowing)
        {
            return FailedPreconditionError("激励视频广告正在播放中，严禁并发重入 show()");
        }

        m_isShowing = true;
        m_isPreloaded = false;
        m_pendingCallback = std::move(onResult);

        // 播放前强制静音引擎背景音乐与音效，防止音轨冲突违规
        applyAudioMuteState(true);

        if (m_hostApi)
        {
            m_hostApi->showRewardedVideoAd(
                [this](bool isEnded) { notifyAdClosed(isEnded); },
                [this](int32_t errCode, const std::string& errMsg) { notifyAdError(errCode, errMsg); });
        }
        return OkStatus();
    }

    void RewardedVideoAdService::notifyAdClosed(bool isEnded)
    {
        if (!m_isShowing)
        {
            return;
        }

        m_isShowing = false;
        applyAudioMuteState(false);

        // 关闭后自动异步预加载下一条广告实例
        (void)preload();

        RewardedAdCloseResult result;
        result.isEnded = isEnded;
        // Note: 唯一合法发奖红线：仅当宿主回调 isEnded === true 时发放奖励，中途退出一律为 false
        result.rewardGranted = (isEnded == true);
        if (result.rewardGranted)
        {
            ++m_grantedRewardCount;
        }

        auto cb = std::move(m_pendingCallback);
        m_pendingCallback = nullptr;
        if (cb)
        {
            cb(result);
        }
    }

    void RewardedVideoAdService::notifyAdError(int32_t errCode, const std::string& errMsg)
    {
        const bool wasShowing = m_isShowing;
        m_isShowing = false;
        m_isPreloaded = false;
        if (wasShowing)
        {
            applyAudioMuteState(false);
        }

        RewardedAdCloseResult result;
        result.rewardGranted = false;
        result.isEnded = false;
        result.errorCode = (errCode != 0) ? errCode : -1;
        result.errorMessage = errMsg.empty() ? "广告无填充或网络请求超时" : errMsg;

        auto cb = std::move(m_pendingCallback);
        m_pendingCallback = nullptr;
        if (cb)
        {
            cb(result);
        }
    }

    void RewardedVideoAdService::applyAudioMuteState(bool mute)
    {
        m_engineAudioMuted = mute;
        if (m_audioMuteCallback)
        {
            m_audioMuteCallback(mute);
        }
    }
} // namespace MQEngine::DouyinMiniGame
