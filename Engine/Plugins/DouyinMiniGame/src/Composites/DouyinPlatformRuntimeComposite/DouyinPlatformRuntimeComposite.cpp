/**
 * @file DouyinPlatformRuntimeComposite.cpp
 * @brief 抖音小游戏运行时宿主生态服务 Composite 生命周期与子服务编排实现
 */
#include "DouyinPlatformRuntimeComposite.h"

#include <MQEngine/asset/core/Asset.h>
#include <Composites/RmlUiComposite/RmlUiComposite.h>

namespace MQEngine::DouyinMiniGame
{
    DouyinPlatformRuntimeComposite::DouyinPlatformRuntimeComposite()
        : m_hostApi(createWebJsDouyinHostApi())
        , m_sidebarService(makeUnique<SidebarRetentionService>(m_hostApi.get()))
        , m_rewardedAdService(makeUnique<RewardedVideoAdService>(m_hostApi.get()))
        , m_recorderService(makeUnique<GameRecorderService>(m_hostApi.get()))
        , m_capsuleBridge(makeUnique<CapsuleAvoidanceBridge>(m_hostApi.get()))
        , m_openDataBridge(makeUnique<OpenDataDomainBridge>(m_hostApi.get()))
        , m_splashRelay(makeUnique<SplashRelayCoordinator>(m_hostApi.get()))
        , m_memoryCoordinator(makeUnique<MemoryWarningCoordinator>(m_hostApi.get()))
    {
    }

    DouyinPlatformRuntimeComposite::~DouyinPlatformRuntimeComposite()
    {
        onUnload();
    }

    Status DouyinPlatformRuntimeComposite::onLoad(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onLoad(engine));
        bindHostLifecycleCallbacks();
        return OkStatus();
    }

    Status DouyinPlatformRuntimeComposite::onInit(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onInit(engine));

        if (auto* assetMod = Asset::Asset::InstancePtr())
        {
            if (auto* rmlComp = assetMod->compositeManager.getComposite("RmlUiComposite"))
            {
                m_capsuleBridge->setRmlUiComposite(static_cast<GameHtmlUI::RmlUiComposite*>(rmlComp));
            }
        }
        (void)m_capsuleBridge->refreshAndInjectCssVariables();
        return OkStatus();
    }

    void DouyinPlatformRuntimeComposite::onTick(float deltaTime)
    {
        if (!m_firstRuntimeFrameDispatched)
        {
            m_firstRuntimeFrameDispatched = true;
            m_splashRelay->notifyFirstFrameRendered();
        }

        if (m_hostApi)
        {
            m_hostApi->pumpMicrotasks();
        }
        m_openDataBridge->tick(deltaTime);
    }

    void DouyinPlatformRuntimeComposite::onUnload()
    {
        if (m_capsuleBridge)
        {
            m_capsuleBridge->setRmlUiComposite(nullptr);
        }
        Asset::Composite::onUnload();
    }

    void DouyinPlatformRuntimeComposite::setCustomHostApi(UniquePtr<IDouyinHostApi> customHostApi)
    {
        assert(customHostApi != nullptr && "注入的 IDouyinHostApi 不能为空指针");
        m_hostApi = std::move(customHostApi);
        rebindAllServicesToHostApi();
        bindHostLifecycleCallbacks();
    }

    void DouyinPlatformRuntimeComposite::rebindAllServicesToHostApi()
    {
        IDouyinHostApi* raw = m_hostApi.get();
        m_sidebarService->setHostApi(raw);
        m_rewardedAdService->setHostApi(raw);
        m_recorderService->setHostApi(raw);
        m_capsuleBridge->setHostApi(raw);
        m_openDataBridge->setHostApi(raw);
        m_splashRelay->setHostApi(raw);
        m_memoryCoordinator->setHostApi(raw);
    }

    void DouyinPlatformRuntimeComposite::bindHostLifecycleCallbacks()
    {
        if (!m_hostApi)
        {
            return;
        }
        m_hostApi->bindOnShowListener([this](const DouyinShowEvent& evt) {
            m_sidebarService->handleOnShow(evt);
        });
        m_hostApi->bindOnMemoryWarningListener([this](int32_t level) {
            (void)m_memoryCoordinator->handleMemoryWarning(level);
        });
    }
} // namespace MQEngine::DouyinMiniGame

MQ_EXPORT_COMPOSITE(MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite)
