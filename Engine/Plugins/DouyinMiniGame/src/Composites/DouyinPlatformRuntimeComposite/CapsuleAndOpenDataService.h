/**
 * @file CapsuleAndOpenDataService.h
 * @brief FEAT-07 & FEAT-54：官方右上角胶囊安全区 GameHtmlUI CSS 注入、开放数据域社交排行榜共享画布同步与首屏 Splash 卸载桥接
 */
#ifndef MQENGINE_DOUYIN_CAPSULE_AND_OPEN_DATA_SERVICE_H
#define MQENGINE_DOUYIN_CAPSULE_AND_OPEN_DATA_SERVICE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"

#include <unordered_map>

namespace MQEngine::GameHtmlUI
{
    class RmlUiComposite;
}

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 归一化并钳制后的胶囊避让与屏幕安全区 CSS 像素度量
     */
    struct CapsuleSafeAreaCssMetrics
    {
        float capsuleAvoidTopPx{0.0f};
        float capsuleAvoidRightPx{0.0f};
        float capsuleAvoidWidthPx{0.0f};
        float capsuleAvoidHeightPx{0.0f};
        float safeTopPx{0.0f};
        float safeBottomPx{0.0f};
        float safeLeftPx{0.0f};
        float safeRightPx{0.0f};
    };

    /**
     * @brief 官方右上角胶囊安全区采集与 GameHtmlUI (--mq-capsule-avoid-*) 自动注入桥接器（FEAT-07）
     */
    class DOUYIN_PLATFORM_RUNTIME_API CapsuleAvoidanceBridge
    {
    public:
        explicit CapsuleAvoidanceBridge(IDouyinHostApi* hostApi = nullptr);
        ~CapsuleAvoidanceBridge() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }
        void setRmlUiComposite(GameHtmlUI::RmlUiComposite* rmlUi) noexcept { m_rmlUi = rmlUi; }
        void setTargetRootElementId(std::string elementId) { m_rootElementId = std::move(elementId); }

        static CapsuleSafeAreaCssMetrics computeSanitizedMetrics(
            const DouyinSystemSafeArea& sysArea,
            const DouyinCapsuleRect& rawCapsule);

        CapsuleSafeAreaCssMetrics refreshAndInjectCssVariables();
        CapsuleSafeAreaCssMetrics applyExplicitRects(
            const DouyinSystemSafeArea& sysArea,
            const DouyinCapsuleRect& rawCapsule);

        const CapsuleSafeAreaCssMetrics& getLastMetrics() const noexcept { return m_lastMetrics; }
        const std::unordered_map<std::string, std::string>& getInjectedCssVars() const noexcept { return m_injectedCssVars; }

    private:
        void pushMetricsToRmlUi(const CapsuleSafeAreaCssMetrics& metrics);

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        GameHtmlUI::RmlUiComposite* m_rmlUi{nullptr};
        std::string m_rootElementId{"body"};
        CapsuleSafeAreaCssMetrics m_lastMetrics;
        std::unordered_map<std::string, std::string> m_injectedCssVars;
    };

    /**
     * @brief 抖音开放数据域 (OpenDataContext) 单向消息投递与好友排行榜离屏纹理 (@mq_shared_canvas/rank) 按需同步桥接器（FEAT-07）
     */
    class DOUYIN_PLATFORM_RUNTIME_API OpenDataDomainBridge
    {
    public:
        static constexpr const char* kSharedRankTextureUri = "@mq_shared_canvas/rank";

        explicit OpenDataDomainBridge(IDouyinHostApi* hostApi = nullptr);
        ~OpenDataDomainBridge() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }
        void bindTargetRankTexture(FCT::Image* targetImage) noexcept { m_targetRankImage = targetImage; }

        Status postMessage(const std::string& jsonPayload);

        void setLeaderboardVisible(bool visible) noexcept;
        bool isLeaderboardVisible() const noexcept { return m_leaderboardVisible; }

        void setSyncIntervalSeconds(float intervalSec) noexcept;
        float getSyncIntervalSeconds() const noexcept { return m_syncIntervalSec; }

        void tick(float deltaTime);

        uint32_t getPostedMessageCount() const noexcept { return m_postedMessageCount; }
        uint32_t getSharedCanvasSyncCount() const noexcept { return m_sharedCanvasSyncCount; }
        const std::string& getLastPostedJson() const noexcept { return m_lastPostedJson; }

    private:
        static bool isBalancedJsonObject(std::string_view jsonText) noexcept;
        void performSharedCanvasUpload();

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        FCT::Image* m_targetRankImage{nullptr};
        bool m_leaderboardVisible{false};
        float m_syncIntervalSec{0.1f};
        float m_accumulatedSec{0.0f};
        uint32_t m_postedMessageCount{0};
        uint32_t m_sharedCanvasSyncCount{0};
        std::string m_lastPostedJson;
    };

    /**
     * @brief 首屏 game.js Splash 进度条与引擎首帧无缝卸载接力协调器（FEAT-54）
     */
    class DOUYIN_PLATFORM_RUNTIME_API SplashRelayCoordinator
    {
    public:
        explicit SplashRelayCoordinator(IDouyinHostApi* hostApi = nullptr)
            : m_hostApi(hostApi) {}

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }

        void notifyFirstFrameRendered();
        bool isSplashDismissed() const noexcept { return m_splashDismissed; }

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        bool m_splashDismissed{false};
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_CAPSULE_AND_OPEN_DATA_SERVICE_H
