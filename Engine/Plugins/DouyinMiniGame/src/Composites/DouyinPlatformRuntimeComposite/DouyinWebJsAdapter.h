/**
 * @file DouyinWebJsAdapter.h
 * @brief 抖音 tt.* 宿主 API 抽象接口与 FCT_WebJS 零泄漏桥接器（所有平台 API 严格收敛于插件内部）
 */
#ifndef MQENGINE_DOUYIN_WEBJS_ADAPTER_H
#define MQENGINE_DOUYIN_WEBJS_ADAPTER_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief tt.onShow 宿主生命周期参数快照
     */
    struct DouyinShowEvent
    {
        std::string scene;
        std::string launchFrom;
        std::string location;
    };

    /**
     * @brief tt.getMenuButtonBoundingClientRect 官方右上角胶囊几何信息（逻辑像素）
     */
    struct DouyinCapsuleRect
    {
        float left{0.0f};
        float top{0.0f};
        float right{0.0f};
        float bottom{0.0f};
        float width{0.0f};
        float height{0.0f};
    };

    /**
     * @brief tt.getSystemInfoSync 屏幕尺寸与安全区信息（逻辑像素）
     */
    struct DouyinSystemSafeArea
    {
        float windowWidth{390.0f};
        float windowHeight{844.0f};
        float pixelRatio{2.0f};
        float safeLeft{0.0f};
        float safeTop{0.0f};
        float safeRight{390.0f};
        float safeBottom{844.0f};
    };

    /**
     * @brief tt.shareAppMessage 拍抖音短视频挂载请求
     */
    struct DouyinShareVideoRequest
    {
        std::string videoPath;
        std::string title;
        std::vector<std::string> videoTopics;
        std::string anchorTitle;
        std::string anchorPath;
    };

    /**
     * @brief 抖音小游戏宿主能力纯 C++ 抽象桥接契约（默认由 FCT_WebJS 句柄表驱动，亦支持测试注入）
     */
    class DOUYIN_PLATFORM_RUNTIME_API IDouyinHostApi
    {
    public:
        virtual ~IDouyinHostApi() = default;

        virtual void bindOnShowListener(std::function<void(const DouyinShowEvent&)> listener) = 0;
        virtual void bindOnMemoryWarningListener(std::function<void(int32_t level)> listener) = 0;

        virtual void checkSidebarScene(std::function<void(StatusOr<bool>)> callback) = 0;
        virtual void navigateToSidebarScene(std::function<void(Status)> callback) = 0;

        virtual Status createRewardedVideoAd(const std::string& adUnitId) = 0;
        virtual Status loadRewardedVideoAd() = 0;
        virtual void showRewardedVideoAd(
            std::function<void(bool isEnded)> onClose,
            std::function<void(int32_t errCode, const std::string& errMsg)> onError) = 0;

        virtual Status recorderStart(int32_t durationSec) = 0;
        virtual Status recorderPause() = 0;
        virtual Status recorderResume() = 0;
        virtual StatusOr<std::string> recorderStop() = 0;
        virtual Status recorderRecordClip(double beforeSec, double afterSec) = 0;
        virtual void shareVideoMessage(
            const DouyinShareVideoRequest& req,
            std::function<void(Status)> callback) = 0;

        virtual DouyinSystemSafeArea querySystemSafeArea() = 0;
        virtual DouyinCapsuleRect queryMenuButtonCapsuleRect() = 0;

        virtual Status postOpenDataMessage(const std::string& jsonPayload) = 0;
        virtual bool readSharedRankCanvasRgba(
            uint32_t& outWidth,
            uint32_t& outHeight,
            std::vector<uint8_t>& outRgbaPixels) = 0;

        virtual void dismissBootSplash() = 0;
        virtual void pumpMicrotasks() = 0;
    };

    /**
     * @brief 创建基于 FCT_WebJS (globalThis.tt) 的真实抖音宿主桥接适配器实例
     */
    DOUYIN_PLATFORM_RUNTIME_API UniquePtr<IDouyinHostApi> createWebJsDouyinHostApi();
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_WEBJS_ADAPTER_H
