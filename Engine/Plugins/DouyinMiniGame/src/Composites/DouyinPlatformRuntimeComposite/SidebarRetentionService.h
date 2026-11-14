/**
 * @file SidebarRetentionService.h
 * @brief FEAT-04：抖音首页侧边栏复访场景值（021036）识别、可用性探测、一键跳转与防重领奖服务
 */
#ifndef MQENGINE_DOUYIN_SIDEBAR_RETENTION_SERVICE_H
#define MQENGINE_DOUYIN_SIDEBAR_RETENTION_SERVICE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"

#include <unordered_set>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 抖音首页侧边栏复访留存服务（FEAT-04）
     */
    class DOUYIN_PLATFORM_RUNTIME_API SidebarRetentionService
    {
    public:
        static constexpr const char* kSidebarSceneCode = "021036";

        explicit SidebarRetentionService(IDouyinHostApi* hostApi = nullptr);
        ~SidebarRetentionService() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }

        void handleOnShow(const DouyinShowEvent& event);
        void checkSidebarAvailable(std::function<void(StatusOr<bool>)> callback);
        void navigateToSidebar(std::function<void(Status)> callback);

        void setSidebarSupported(bool supported) noexcept { m_sidebarSupported = supported; }
        bool isSidebarSupported() const noexcept { return m_sidebarSupported; }
        bool isLaunchedFromSidebar() const noexcept { return m_launchedFromSidebar; }
        const std::string& getLastSceneCode() const noexcept { return m_lastSceneCode; }

        StatusOr<bool> claimReward(const std::string& rewardCycleId = "daily_default");
        bool hasClaimedReward(const std::string& rewardCycleId = "daily_default") const;
        void resetClaimedRewards() noexcept { m_claimedCycles.clear(); }

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        bool m_sidebarSupported{true};
        bool m_launchedFromSidebar{false};
        std::string m_lastSceneCode;
        std::unordered_set<std::string> m_claimedCycles;
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_SIDEBAR_RETENTION_SERVICE_H
