/**
 * @file DouyinPlatformRuntimeComposite.h
 * @brief 抖音小游戏运行时宿主生态服务聚合 Composite（FEAT-01, FEAT-04~08, FEAT-54）
 */
#ifndef MQENGINE_DOUYIN_PLATFORM_RUNTIME_COMPOSITE_H
#define MQENGINE_DOUYIN_PLATFORM_RUNTIME_COMPOSITE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"
#include "SidebarRetentionService.h"
#include "RewardedVideoAdService.h"
#include "GameRecorderService.h"
#include "CapsuleAndOpenDataService.h"
#include "MemoryWarningCoordinator.h"

#include <MQEngine/asset/interface/Composite.h>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 抖音宿主生态运行时总控 Composite（仅匹配 Runtime + MiniGame + Douyin 标签时激活，桌面游戏零侵入）
     */
    class DOUYIN_PLATFORM_RUNTIME_API DouyinPlatformRuntimeComposite : public Asset::Composite
    {
    public:
        DouyinPlatformRuntimeComposite();
        ~DouyinPlatformRuntimeComposite() override;

        Status onLoad(Runtime::Engine* engine) override;
        Status onInit(Runtime::Engine* engine) override;
        void onTick(float deltaTime) override;
        void onUnload() override;

        std::string getName() const override { return "DouyinPlatformRuntimeComposite"; }
        std::string getPluginName() const override { return "DouyinMiniGame"; }
        std::vector<std::string> getDependencies() const override { return {}; }
        std::vector<std::string> getRequiredTags() const override { return {"Runtime", "MiniGame", "Douyin"}; }

        void setCustomHostApi(UniquePtr<IDouyinHostApi> customHostApi);
        IDouyinHostApi* getHostApi() const noexcept { return m_hostApi.get(); }

        SidebarRetentionService& getSidebarService() noexcept { return *m_sidebarService; }
        const SidebarRetentionService& getSidebarService() const noexcept { return *m_sidebarService; }

        RewardedVideoAdService& getRewardedAdService() noexcept { return *m_rewardedAdService; }
        const RewardedVideoAdService& getRewardedAdService() const noexcept { return *m_rewardedAdService; }

        GameRecorderService& getRecorderService() noexcept { return *m_recorderService; }
        const GameRecorderService& getRecorderService() const noexcept { return *m_recorderService; }

        CapsuleAvoidanceBridge& getCapsuleBridge() noexcept { return *m_capsuleBridge; }
        const CapsuleAvoidanceBridge& getCapsuleBridge() const noexcept { return *m_capsuleBridge; }

        OpenDataDomainBridge& getOpenDataBridge() noexcept { return *m_openDataBridge; }
        const OpenDataDomainBridge& getOpenDataBridge() const noexcept { return *m_openDataBridge; }

        SplashRelayCoordinator& getSplashRelay() noexcept { return *m_splashRelay; }
        const SplashRelayCoordinator& getSplashRelay() const noexcept { return *m_splashRelay; }

        MemoryWarningCoordinator& getMemoryCoordinator() noexcept { return *m_memoryCoordinator; }
        const MemoryWarningCoordinator& getMemoryCoordinator() const noexcept { return *m_memoryCoordinator; }

    private:
        void rebindAllServicesToHostApi();
        void bindHostLifecycleCallbacks();

    private:
        UniquePtr<IDouyinHostApi> m_hostApi;
        UniquePtr<SidebarRetentionService> m_sidebarService;
        UniquePtr<RewardedVideoAdService> m_rewardedAdService;
        UniquePtr<GameRecorderService> m_recorderService;
        UniquePtr<CapsuleAvoidanceBridge> m_capsuleBridge;
        UniquePtr<OpenDataDomainBridge> m_openDataBridge;
        UniquePtr<SplashRelayCoordinator> m_splashRelay;
        UniquePtr<MemoryWarningCoordinator> m_memoryCoordinator;
        bool m_firstRuntimeFrameDispatched{false};
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_PLATFORM_RUNTIME_COMPOSITE_H
