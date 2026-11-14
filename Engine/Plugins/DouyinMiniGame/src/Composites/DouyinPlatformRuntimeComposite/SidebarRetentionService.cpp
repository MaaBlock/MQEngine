/**
 * @file SidebarRetentionService.cpp
 * @brief FEAT-04 抖音侧边栏复访场景 021036 监听、跳转与幂等领奖防刷实现
 */
#include "SidebarRetentionService.h"

namespace MQEngine::DouyinMiniGame
{
    SidebarRetentionService::SidebarRetentionService(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    void SidebarRetentionService::handleOnShow(const DouyinShowEvent& event)
    {
        m_lastSceneCode = event.scene;
        // 严格匹配抖音官方侧边栏复访场景值 "021036"（或显式 homepage + sidebar_card 组合且带 021036）
        m_launchedFromSidebar = (event.scene == kSidebarSceneCode);
    }

    void SidebarRetentionService::checkSidebarAvailable(std::function<void(StatusOr<bool>)> callback)
    {
        if (!m_hostApi)
        {
            if (callback)
            {
                callback(m_sidebarSupported);
            }
            return;
        }

        m_hostApi->checkSidebarScene([this, cb = std::move(callback)](StatusOr<bool> availableOr) {
            if (availableOr.ok())
            {
                m_sidebarSupported = *availableOr;
            }
            else
            {
                m_sidebarSupported = false;
            }
            if (cb)
            {
                cb(availableOr);
            }
        });
    }

    void SidebarRetentionService::navigateToSidebar(std::function<void(Status)> callback)
    {
        if (!m_sidebarSupported)
        {
            if (callback)
            {
                callback(FailedPreconditionError("当前抖音宿主版本不支持首页侧边栏入口"));
            }
            return;
        }

        if (!m_hostApi)
        {
            if (callback)
            {
                callback(FailedPreconditionError("未绑定抖音宿主桥接实例"));
            }
            return;
        }

        m_hostApi->navigateToSidebarScene(std::move(callback));
    }

    StatusOr<bool> SidebarRetentionService::claimReward(const std::string& rewardCycleId)
    {
        if (rewardCycleId.empty())
        {
            return InvalidArgumentError("rewardCycleId 领奖周期标识不能为空");
        }
        if (!m_sidebarSupported)
        {
            return FailedPreconditionError("当前宿主不支持侧边栏复访能力");
        }
        if (!m_launchedFromSidebar)
        {
            return PermissionDeniedError("非侧边栏场景启动 (需要 scene == 021036)，拒绝发放复访奖励");
        }
        if (m_claimedCycles.find(rewardCycleId) != m_claimedCycles.end())
        {
            return false;
        }

        m_claimedCycles.insert(rewardCycleId);
        return true;
    }

    bool SidebarRetentionService::hasClaimedReward(const std::string& rewardCycleId) const
    {
        return m_claimedCycles.find(rewardCycleId) != m_claimedCycles.end();
    }
} // namespace MQEngine::DouyinMiniGame
