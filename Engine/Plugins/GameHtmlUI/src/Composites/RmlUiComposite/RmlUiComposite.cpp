/**
 * @file RmlUiComposite.cpp
 * @brief 基于 RmlUi 6.0 的轻量级运行时 HTML/CSS UI Composite 实现（集成 FEAT-47 ~ FEAT-54）
 */
#include "RmlUiComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/renderer/BindlessSet0Manager.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <Font/Composites/FontRuntimeComposite/FontRuntimeComposite.h>
#include <MQEngine/runtime/core/Engine.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>

#if defined(MQ_HTMLUI_DEBUGGER_ENABLED) && MQ_HTMLUI_DEBUGGER_ENABLED
#include <RmlUi/Debugger.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_set>

namespace MQEngine::GameHtmlUI
{
    namespace
    {
        std::atomic<uint32_t> s_rmlCoreActiveRefCount{0};
        std::atomic<uint32_t> s_contextInstanceSerial{0};
        std::atomic<RmlUiComposite*> s_activeCompositeInstance{nullptr};

        constexpr const char* kRmlOverlayExtName = "RmlUiSwapchainOverlay";

        std::string resolveProjectUiAssetPath(const std::string& virtualOrDiskPath)
        {
            if (virtualOrDiskPath.empty())
            {
                return virtualOrDiskPath;
            }
            auto* resModule = Resource::Resource::InstancePtr();
            auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
            if (resModule && loader && !resModule->getProjectDir().empty())
            {
                const std::string projCandidate = FCT::SAI::Path::join(resModule->getProjectDir(), virtualOrDiskPath);
                if (loader->exists(projCandidate))
                {
                    return projCandidate;
                }
            }
            return virtualOrDiskPath;
        }

        constexpr const char* kRmlOverlayVertexShaderHlsl = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.position = sIn.position;
    sOut.color = sIn.color;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.tangent = sIn.tangent;
    sOut.bitangent = sIn.bitangent;
    sOut.boneIds = sIn.boneIds;
    sOut.boneWeights = sIn.boneWeights;
    sOut.worldPos = sIn.position;
    sOut.shadowPos = float4(0.0f, 0.0f, 0.0f, 1.0f);
    sOut.instanceId = 0;
    return sOut;
}
)";

        constexpr const char* kRmlOverlayPixelShaderHlsl = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    if (sIn.bitangent.z > 0.5f) {
        float px = sIn.normal.x;
        float py = sIn.normal.y;
        if (px < sIn.tangent.y || px > sIn.bitangent.x ||
            py < sIn.tangent.z || py > sIn.bitangent.y) {
            discard;
        }
    }

    float texMode = sIn.tangent.x;
    uint slot = (uint)(sIn.normal.z + 0.5f);
    float4 outCol = sIn.color;

    if (texMode > 0.5f && texMode < 1.5f) {
        float cov = g_Textures[NonUniformResourceIndex(slot)].SampleLevel(g_LinearSampler, sIn.texCoord, 0.0f).r;
        outCol = float4(sIn.color.rgb, sIn.color.a * cov);
    } else if (texMode >= 1.5f) {
        float4 texel = g_Textures[NonUniformResourceIndex(slot)].SampleLevel(g_LinearSampler, sIn.texCoord, 0.0f);
        float3 texLinear = SrgbToLinearExact(saturate(texel.rgb));
        outCol = float4(sIn.color.rgb * texLinear, sIn.color.a * texel.a);
    }

    if (outCol.a <= 0.001f) {
        discard;
    }
    sOut.target0 = outCol;
    return sOut;
}
)";

        float rmlSrgbToLinear(float c) noexcept
        {
            const float v = std::clamp(c, 0.0f, 1.0f);
            return (v <= 0.04045f) ? (v / 12.92f) : std::pow((v + 0.055f) / 1.055f, 2.4f);
        }

        FCT::Vec4 rmlPremulVertexColorToLinearStraight(const Rml::ColourbPremultiplied& col) noexcept
        {
            const uint32_t pa = col.alpha;
            if (pa == 0u)
            {
                return FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }
            const float invPa = 1.0f / static_cast<float>(pa);
            const float sr = std::clamp(static_cast<float>(col.red) * invPa, 0.0f, 1.0f);
            const float sg = std::clamp(static_cast<float>(col.green) * invPa, 0.0f, 1.0f);
            const float sb = std::clamp(static_cast<float>(col.blue) * invPa, 0.0f, 1.0f);
            const float sa = std::clamp(static_cast<float>(pa) / 255.0f, 0.0f, 1.0f);
            return FCT::Vec4(rmlSrgbToLinear(sr), rmlSrgbToLinear(sg), rmlSrgbToLinear(sb), sa);
        }
    } // namespace

    class MQRmlDomEventDispatcher : public Rml::EventListener
    {
    public:
        explicit MQRmlDomEventDispatcher(RmlUiComposite* owner) noexcept
            : m_owner(owner)
        {
        }

        ~MQRmlDomEventDispatcher() override = default;

        void ensureEventAttached(Rml::Context* ctx, const std::string& eventType)
        {
            if (!ctx || eventType.empty())
            {
                return;
            }
            if (m_attachedEventTypes.insert(eventType).second)
            {
                ctx->AddEventListener(eventType.c_str(), this, false);
            }
        }

        void detachAllFromContext(Rml::Context* ctx)
        {
            if (ctx)
            {
                for (const std::string& evtType : m_attachedEventTypes)
                {
                    ctx->RemoveEventListener(evtType.c_str(), this, false);
                }
            }
            m_attachedEventTypes.clear();
        }

        void ProcessEvent(Rml::Event& event) override
        {
            if (!m_owner)
            {
                return;
            }
            const Rml::String& typeStr = event.GetType();
            m_owner->dispatchDomEvent(
                std::string(typeStr.c_str(), typeStr.size()),
                event.GetTargetElement());
        }

    private:
        RmlUiComposite* m_owner = nullptr;
        std::unordered_set<std::string> m_attachedEventTypes;
    };

    RmlUiComposite* RmlUiComposite::getActiveInstance() noexcept
    {
        return s_activeCompositeInstance.load(std::memory_order_acquire);
    }

    RmlUiComposite::RmlUiComposite(Rml::FileInterface* customFileInterface)
        : m_customFileInterface(customFileInterface)
    {
        m_touchController = makeUnique<VirtualTouchController>();
        m_capsuleInjector = makeUnique<CapsuleCssVariableInjector>();
        m_keyboardBridge = makeUnique<RmlUiKeyboardBridge>();
        m_splashCoordinator = makeUnique<SplashRelayCoordinator>();
        m_leaderboardController = makeUnique<OpenDataLeaderboardController>();
        m_domEventDispatcher = makeUnique<MQRmlDomEventDispatcher>(this);
    }

    RmlUiComposite::~RmlUiComposite()
    {
        onUnload();
    }

    bool RmlUiComposite::isDebuggerEnabled() const noexcept
    {
#if defined(MQ_HTMLUI_DEBUGGER_ENABLED) && MQ_HTMLUI_DEBUGGER_ENABLED
        return true;
#else
        return false;
#endif
    }

    Status RmlUiComposite::onLoad(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onLoad(engine));
        return OkStatus();
    }

    Status RmlUiComposite::onInit(Runtime::Engine* engine)
    {
        if (engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
        }

        if (auto* hostModule = Host::Host::InstancePtr())
        {
            if (auto* wnd = hostModule->uiHost.getWindow())
            {
                const int width = wnd->getWidth();
                const int height = wnd->getHeight();
                if (width > 0 && height > 0)
                {
                    m_viewportWidth = width;
                    m_viewportHeight = height;
                }
            }
        }

        ensureContextReady();
        initFontSubsystem();
        s_activeCompositeInstance.store(this, std::memory_order_release);

        if (auto* domain = Domain::Domain::InstancePtr())
        {
            if (auto* bindless = domain->renderer.getBindlessManager();
                bindless && bindless->isInitialized())
            {
                m_useVulkanSwapchainOverlay = true;
                if (m_renderInterface)
                {
                    m_renderInterface->setDeferOverlayMode(true);
                }
                registerOverlayRenderExtension();
            }
            else
            {
                Domain::Renderer::RenderGraphExtensionDesc ext;
                ext.name = "RmlUiPass";
                ext.syncNodes.push_back(Domain::Renderer::RenderSyncNodeDesc{
                    .name = "SyncRmlUiDrawList",
                    .callback = [this]() {
                        submitUiDrawBuckets();
                    },
                    .predecessors = {},
                    .successors = {}
                });
                ext.passSubscribers.push_back(Domain::Renderer::NamedPassSubscription{
                    .passName = "RmlUiPass",
                    .callback = [this](FCT::PassSubmitEvent env) {
                        (void)env;
                        if (m_splashCoordinator)
                        {
                            m_splashCoordinator->notifyFirstFrameRendered();
                        }
                    }
                });
                domain->renderer.registerRenderGraphExtension(std::move(ext));
            }
        }

        registerEventListener("btn_pause", "click", [](const std::string&, const std::string&) {
            if (auto* domain = Domain::Domain::InstancePtr())
            {
                domain->gameplay.pauseGame();
            }
        });

        registerEventListener("tactical_scan", "click", [](const std::string&, const std::string&) {
            spdlog::info("[GameHUD] Tactical scan initiated");
        });

        auto* resModule = Resource::Resource::InstancePtr();
        auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
        const std::string viewerHudDisk = resolveProjectUiAssetPath("Assets/UI/ViewerHUD.rml");
        if (loader && loader->exists(viewerHudDisk))
        {
            return loadDocument("Assets/UI/ViewerHUD.rml", "GameHUD");
        }
        return OkStatus();
    }

    void RmlUiComposite::initFontSubsystem()
    {
        initRmlSystem();
        if (m_fontsInitialized)
        {
            return;
        }

        // 优先尝试工程打包虚拟路径字体，最终统一收敛至 @system-ui 虚拟字体族，杜绝任何本地硬编码路径
        auto* resModule = Resource::Resource::InstancePtr();
        auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
        const std::string robotoDisk = resolveProjectUiAssetPath("Assets/Fonts/Roboto.ttf");
        if (loader && loader->exists(robotoDisk))
        {
            (void)Rml::LoadFontFace("Assets/Fonts/Roboto.ttf", false);
        }
        (void)Rml::LoadFontFace("@system-ui", true);
        m_fontsInitialized = true;
    }

    void RmlUiComposite::onTick(float deltaTime)
    {
        ZoneScopedN("RmlUiComposite::onTick");
        (void)deltaTime;

        if (!m_rmlContext)
        {
            return;
        }

        forwardEngineHidSnapshot();

        m_rmlContext->Update();

        if (m_renderInterface)
        {
            m_renderInterface->clearDrawCommands();
        }
        m_rmlContext->Render();

        if (m_leaderboardController)
        {
            m_leaderboardController->syncVisibilityFromDocument(m_activeDocument);
            m_leaderboardController->onFrameUpdate();
        }

        if (m_useVulkanSwapchainOverlay)
        {
            bool allowOverlay = hasAnyVisibleDocument();
            if (allowOverlay)
            {
                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    const std::string primaryTarget = domain->renderer.getPrimaryPresentTargetName();
                    if (!primaryTarget.empty() && primaryTarget != "MainWindow")
                    {
                        allowOverlay = false;
                    }
                }
            }

            if (allowOverlay && m_renderInterface)
            {
                m_stagedDrawCommands = m_renderInterface->getDrawCommands();
            }
            else
            {
                m_stagedDrawCommands.clear();
            }
            m_stagedViewportW = static_cast<float>(std::max(1, m_viewportWidth));
            m_stagedViewportH = static_cast<float>(std::max(1, m_viewportHeight));
            m_overlayStagedDirty = true;
        }
        else
        {
            submitUiDrawBuckets();
        }

        if (m_splashCoordinator)
        {
            m_splashCoordinator->notifyFirstFrameRendered();
        }

        if (m_liveElementsDirty && m_activeDocument)
        {
            m_liveElementsDirty = false;
            for (auto& [docId, docDesc] : m_documents)
            {
                (void)docId;
                if (docDesc.rawDocumentHandle == m_activeDocument)
                {
                    syncLiveElementsFromDoc(m_activeDocument, docDesc);
                    break;
                }
            }
        }
    }

    void RmlUiComposite::forwardEngineHidSnapshot()
    {
        auto* domain = Domain::Domain::InstancePtr();
        if (!domain || !m_rmlContext)
        {
            return;
        }
        const std::string primaryTarget = domain->renderer.getPrimaryPresentTargetName();
        if (!primaryTarget.empty() && primaryTarget != "MainWindow")
        {
            if (m_pointerInsideViewport)
            {
                m_pointerInsideViewport = false;
                m_rmlContext->ProcessMouseLeave();
            }
            return;
        }

        int vpX = 0;
        int vpY = 0;
        int vpW = m_viewportWidth;
        int vpH = m_viewportHeight;
        if (const auto binding = domain->renderer.getRenderTargetBinding("MainWindow");
            binding.has_value() && binding->width > 0 && binding->height > 0)
        {
            vpX = binding->viewportX;
            vpY = binding->viewportY;
            vpW = static_cast<int>(binding->width);
            vpH = static_cast<int>(binding->height);
        }
        else if (auto* hostModule = Host::Host::InstancePtr())
        {
            if (auto* wnd = hostModule->uiHost.getWindow())
            {
                if (wnd->getWidth() > 0 && wnd->getHeight() > 0)
                {
                    vpW = wnd->getWidth();
                    vpH = wnd->getHeight();
                }
            }
        }

        if (vpW > 0 && vpH > 0 && (vpW != m_viewportWidth || vpH != m_viewportHeight))
        {
            onWindowResize(vpW, vpH);
        }

        auto& hid = domain->hid;
        const FCT::Vec2 mousePos = hid.getMousePosition();
        const int localX = static_cast<int>(std::floor(mousePos.x)) - vpX;
        const int localY = static_cast<int>(std::floor(mousePos.y)) - vpY;
        const bool insideViewport = (localX >= 0 && localX < m_viewportWidth && localY >= 0 && localY < m_viewportHeight);

        if (!insideViewport)
        {
            if (m_pointerInsideViewport)
            {
                m_pointerInsideViewport = false;
                m_rmlContext->ProcessMouseLeave();
            }
            if (hid.isMouseButtonJustReleased(Domain::HID::MouseButton::Left))
            {
                m_rmlContext->ProcessMouseButtonUp(0, 0);
            }
            if (hid.isMouseButtonJustReleased(Domain::HID::MouseButton::Right))
            {
                m_rmlContext->ProcessMouseButtonUp(1, 0);
            }
            return;
        }

        m_pointerInsideViewport = true;
        m_rmlContext->ProcessMouseMove(localX, localY, 0);

        if (hid.isMouseButtonJustPressed(Domain::HID::MouseButton::Left))
        {
            m_rmlContext->ProcessMouseButtonDown(0, 0);
        }
        if (hid.isMouseButtonJustReleased(Domain::HID::MouseButton::Left))
        {
            m_rmlContext->ProcessMouseButtonUp(0, 0);
        }
        if (hid.isMouseButtonJustPressed(Domain::HID::MouseButton::Right))
        {
            m_rmlContext->ProcessMouseButtonDown(1, 0);
        }
        if (hid.isMouseButtonJustReleased(Domain::HID::MouseButton::Right))
        {
            m_rmlContext->ProcessMouseButtonUp(1, 0);
        }

        const float wheel = hid.getMouseWheelDelta();
        if (std::abs(wheel) > 0.001f)
        {
            m_rmlContext->ProcessMouseWheel(wheel, 0);
            if (m_leaderboardController && m_leaderboardController->isVisible())
            {
                m_leaderboardController->onScroll(0.0f, wheel * 24.0f);
            }
        }

        if (hid.isKeyJustPressed(Domain::HID::KeyCode::Space))
        {
            m_rmlContext->ProcessKeyDown(Rml::Input::KI_SPACE, 0);
        }
        if (hid.isKeyJustReleased(Domain::HID::KeyCode::Space))
        {
            m_rmlContext->ProcessKeyUp(Rml::Input::KI_SPACE, 0);
        }
        if (hid.isKeyJustPressed(Domain::HID::KeyCode::Escape))
        {
            m_rmlContext->ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
        }
        if (hid.isKeyJustReleased(Domain::HID::KeyCode::Escape))
        {
            m_rmlContext->ProcessKeyUp(Rml::Input::KI_ESCAPE, 0);
        }
        if (hid.isKeyJustPressed(Domain::HID::KeyCode::Q))
        {
            m_rmlContext->ProcessKeyDown(Rml::Input::KI_Q, 0);
        }
        if (hid.isKeyJustReleased(Domain::HID::KeyCode::Q))
        {
            m_rmlContext->ProcessKeyUp(Rml::Input::KI_Q, 0);
        }
    }

    void RmlUiComposite::onUnload()
    {
        RmlUiComposite* expected = this;
        (void)s_activeCompositeInstance.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);

        if (auto* domain = Domain::Domain::InstancePtr())
        {
            auto& renderer = domain->renderer;
            if (m_overlaySyncRegistered)
            {
                renderer.unregisterSyncTicker("SyncRmlUiOverlay");
                m_overlaySyncRegistered = false;
            }
            if (m_hasAaPassSub)
            {
                if (FCT::RenderGraph* graph = renderer.getRenderGraph())
                {
                    graph->unsubscribe(m_aaPassSubId);
                }
                m_aaPassSubId = {};
                m_hasAaPassSub = false;
            }
            if (m_overlayExtensionRegistered)
            {
                renderer.unregisterRenderGraphExtension(kRmlOverlayExtName);
                m_overlayExtensionRegistered = false;
            }
            renderer.unregisterRenderGraphExtension("RmlUiPass");
        }
        else
        {
            m_overlaySyncRegistered = false;
            m_hasAaPassSub = false;
            m_overlayExtensionRegistered = false;
        }

        releaseOverlayGpuResources();
        m_stagedDrawCommands.clear();
        m_overlayStagedDirty = false;
        m_useVulkanSwapchainOverlay = false;

        m_eventListeners.clear();
        m_dataModels.clear();

        shutdownRmlSystem();
        Asset::Composite::onUnload();
    }

    void RmlUiComposite::initRmlSystem()
    {
        if (m_rmlInitialized)
        {
            return;
        }

        m_systemInterface = makeUnique<MQRmlSystemInterface>();
        m_fileInterface = makeUnique<MQRmlFileInterface>();
        m_fontEngine = makeUnique<MQRmlFontEngine>();
        m_renderInterface = makeUnique<MQRmlRenderInterface>();

        if (auto* globalFontSys = Font::FontRuntimeComposite::getActiveFontSystem())
        {
            m_renderInterface->getTextureRouter().bindFontSystem(globalFontSys);
        }

        if (auto* hostMod = Host::Host::InstancePtr())
        {
            if (auto* rhiCtx = hostMod->renderHost.getContext())
            {
                m_renderInterface->bindRhiContext(rhiCtx);
                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    if (auto* bindless = domain->renderer.getBindlessManager();
                        bindless && bindless->isInitialized())
                    {
                        m_renderInterface->setDeferOverlayMode(true);
                    }
                }
                m_leaderboardController->bindRhiContext(rhiCtx);
            }
        }
        m_leaderboardController->bindTextureRouter(&m_renderInterface->getTextureRouter());

        if (s_rmlCoreActiveRefCount.fetch_add(1, std::memory_order_acq_rel) == 0)
        {
            Rml::SetSystemInterface(m_systemInterface.get());
            Rml::SetFileInterface(m_customFileInterface ? m_customFileInterface : m_fileInterface.get());
            Rml::SetFontEngineInterface(m_fontEngine.get());
            Rml::SetRenderInterface(m_renderInterface.get());

            const bool ok = Rml::Initialise();
            assert(ok && "Rml::Initialise 初始化失败");
        }
        else if (m_customFileInterface)
        {
            Rml::SetFileInterface(m_customFileInterface);
        }

        CapsuleCssVariableInjector::registerCustomPropertiesOnce();
        m_rmlInitialized = true;
    }

    void RmlUiComposite::ensureContextReady()
    {
        initRmlSystem();
        if (m_rmlContext)
        {
            return;
        }

        if (!m_fontsInitialized)
        {
            initFontSubsystem();
        }

        std::string candidateName = "GameHUD";
        if (Rml::GetContext(candidateName.c_str()) != nullptr)
        {
            const uint32_t serial = s_contextInstanceSerial.fetch_add(1, std::memory_order_relaxed) + 1u;
            candidateName = StrCat("GameHUD_", serial);
        }
        m_contextName = std::move(candidateName);

        m_rmlContext = Rml::CreateContext(
            m_contextName.c_str(),
            Rml::Vector2i(m_viewportWidth, m_viewportHeight),
            m_renderInterface.get());
        assert(m_rmlContext != nullptr && "RmlUi Context 创建失败");

#if defined(MQ_HTMLUI_DEBUGGER_ENABLED) && MQ_HTMLUI_DEBUGGER_ENABLED
        Rml::Debugger::Initialise(m_rmlContext);
#endif

        if (m_touchController)
        {
            m_touchController->bindRmlContext(m_rmlContext);
        }
        if (m_capsuleInjector)
        {
            m_capsuleInjector->bindRmlContext(m_rmlContext);
            m_capsuleInjector->onWindowResize(m_viewportWidth, m_viewportHeight);
        }
        if (m_keyboardBridge)
        {
            m_keyboardBridge->attachToContext(m_rmlContext);
        }
        if (m_domEventDispatcher)
        {
            m_domEventDispatcher->ensureEventAttached(m_rmlContext, "click");
            for (const auto& [key, listeners] : m_eventListeners)
            {
                (void)listeners;
                const size_t colonPos = key.rfind(':');
                if (colonPos != std::string::npos && colonPos + 1u < key.size())
                {
                    m_domEventDispatcher->ensureEventAttached(m_rmlContext, key.substr(colonPos + 1u));
                }
            }
        }
    }

    void RmlUiComposite::shutdownRmlSystem()
    {
        if (!m_rmlInitialized)
        {
            m_activeDocument = nullptr;
            m_documents.clear();
            return;
        }

        if (m_domEventDispatcher)
        {
            m_domEventDispatcher->detachAllFromContext(m_rmlContext);
        }
        if (m_keyboardBridge)
        {
            m_keyboardBridge->detachFromContext();
        }
        if (m_capsuleInjector)
        {
            m_capsuleInjector->bindRmlContext(nullptr);
        }
        if (m_touchController)
        {
            m_touchController->bindRmlContext(nullptr);
            m_touchController->clearBindings();
        }

        if (m_rmlContext)
        {
            m_activeDocument = nullptr;
            m_documents.clear();
            m_rmlContext->UnloadAllDocuments();
            m_rmlContext->Update();
            if (!m_contextName.empty())
            {
                Rml::RemoveContext(m_contextName.c_str());
            }
            m_rmlContext = nullptr;
        }
        else
        {
            m_activeDocument = nullptr;
            m_documents.clear();
        }

        if (s_rmlCoreActiveRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            Rml::Shutdown();
            Rml::SetRenderInterface(nullptr);
            Rml::SetFontEngineInterface(nullptr);
            Rml::SetFileInterface(nullptr);
            Rml::SetSystemInterface(nullptr);
        }

        m_renderInterface.reset();
        m_fontEngine.reset();
        m_fileInterface.reset();
        m_systemInterface.reset();
        m_rmlInitialized = false;
        m_fontsInitialized = false;
    }

    void RmlUiComposite::registerDocumentPostLoadHooks(Rml::ElementDocument* doc, RmlDocumentDesc& desc)
    {
        if (!doc)
        {
            return;
        }

        if (m_capsuleInjector)
        {
            m_capsuleInjector->applyToDocument(doc);
        }
        doc->Show();
        if (m_rmlContext)
        {
            m_rmlContext->Update();
        }

        m_activeDocument = doc;
        if (m_keyboardBridge)
        {
            m_keyboardBridge->bindActiveDocument(doc);
        }
        if (m_touchController)
        {
            m_touchController->syncFromDocument(doc);
        }
        if (m_leaderboardController)
        {
            m_leaderboardController->syncVisibilityFromDocument(doc);
        }

        syncLiveElementsFromDoc(doc, desc);
    }

    void RmlUiComposite::syncLiveElementsFromDoc(Rml::ElementDocument* doc, RmlDocumentDesc& desc)
    {
        if (!doc)
        {
            return;
        }

        desc.elements.clear();

        std::function<void(Rml::Element*, int)> traverse = [&](Rml::Element* elem, int depth) {
            if (!elem)
            {
                return;
            }

            const std::string tag = elem->GetTagName();
            if (tag != "head" && tag != "style" && tag != "script" && tag != "title" && tag != "link")
            {
                RmlDomElement domElem;
                domElem.tag = tag;
                domElem.id = elem->GetId();
                domElem.text = elem->GetInnerRML();
                domElem.x = elem->GetAbsoluteLeft();
                domElem.y = elem->GetAbsoluteTop();
                domElem.width = elem->GetBox().GetSize(Rml::BoxArea::Border).x;
                if (domElem.width <= 0.0f)
                {
                    domElem.width = static_cast<float>(elem->GetClientWidth());
                }
                domElem.height = elem->GetBox().GetSize(Rml::BoxArea::Border).y;
                if (domElem.height <= 0.0f)
                {
                    domElem.height = static_cast<float>(elem->GetClientHeight());
                }

                if (!domElem.id.empty() || !domElem.text.empty() ||
                    tag == "div" || tag == "button" || tag == "progress" || tag == "img" || tag == "p")
                {
                    desc.elements.push_back(std::move(domElem));
                }
            }

            const int childCount = elem->GetNumChildren();
            for (int i = 0; i < childCount; ++i)
            {
                traverse(elem->GetChild(i), depth + 1);
            }
        };

        traverse(doc, 0);
    }

    Status RmlUiComposite::loadDocument(const std::string& path, const std::string& documentId)
    {
        if (path.empty())
        {
            return InvalidArgumentError("文档路径不能为空");
        }

        const std::string id = documentId.empty() ? path : documentId;
        if (m_documents.contains(id))
        {
            return AlreadyExistsError(StrCat("文档已存在: ", id));
        }

        ensureContextReady();
        if (!m_rmlContext)
        {
            return FailedPreconditionError("RmlUi Context 尚未初始化");
        }

        auto* resModule = Resource::Resource::InstancePtr();
        auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
        const std::string resolvedDiskPath = resolveProjectUiAssetPath(path);

        std::string rmlContent;
        if (loader && loader->exists(resolvedDiskPath))
        {
            auto loadRes = loader->loadFile(resolvedDiskPath);
            if (loadRes.ok())
            {
                rmlContent = *loadRes;
            }
        }
        else if (FCT::SAI::FileSystem::exists(resolvedDiskPath))
        {
            auto saiRes = FCT::SAI::FileSystem::readTextFile(resolvedDiskPath);
            if (saiRes.ok())
            {
                rmlContent = *saiRes;
            }
        }

        Rml::ElementDocument* doc = nullptr;
        if (!rmlContent.empty())
        {
            doc = m_rmlContext->LoadDocument(path);
        }

        if (!doc)
        {
            if (rmlContent.empty())
            {
                return NotFoundError(StrCat("未找到 RML 文档: ", path));
            }
            return InternalError(StrCat("无法加载 RML 文档: ", path));
        }

        RmlDocumentDesc desc;
        desc.id = id;
        desc.sourcePath = path;
        desc.rmlContent = std::move(rmlContent);
        desc.isVisible = true;
        desc.rawDocumentHandle = doc;

        registerDocumentPostLoadHooks(doc, desc);
        m_documents.emplace(id, std::move(desc));
        return OkStatus();
    }

    Status RmlUiComposite::loadDocumentFromMemory(const std::string& rmlContent, const std::string& documentId)
    {
        if (documentId.empty())
        {
            return InvalidArgumentError("文档 ID 不能为空");
        }

        ensureContextReady();
        if (!m_rmlContext)
        {
            return FailedPreconditionError("RmlUi Context 尚未初始化");
        }

        Rml::ElementDocument* doc = m_rmlContext->LoadDocumentFromMemory(rmlContent, documentId);
        if (!doc)
        {
            return InternalError(StrCat("从内存加载 RML 文档失败: ", documentId));
        }

        RmlDocumentDesc desc;
        desc.id = documentId;
        desc.sourcePath = "Memory://" + documentId;
        desc.rmlContent = rmlContent;
        desc.isVisible = true;
        desc.rawDocumentHandle = doc;

        registerDocumentPostLoadHooks(doc, desc);
        m_documents[documentId] = std::move(desc);
        return OkStatus();
    }

    Status RmlUiComposite::updateDocumentContent(
        const std::string& documentId,
        const std::string& rmlContent,
        const std::string& rcssContent)
    {
        ensureContextReady();

        auto it = m_documents.find(documentId);
        if (it != m_documents.end() && it->second.rawDocumentHandle)
        {
            auto* oldDoc = static_cast<Rml::ElementDocument*>(it->second.rawDocumentHandle);
            oldDoc->Close();
            it->second.rawDocumentHandle = nullptr;
        }

        Rml::ElementDocument* newDoc = nullptr;
        if (m_rmlContext)
        {
            newDoc = m_rmlContext->LoadDocumentFromMemory(rmlContent, documentId);
        }

        if (it == m_documents.end())
        {
            RmlDocumentDesc desc;
            desc.id = documentId;
            desc.sourcePath = "Assets/UI/" + documentId + ".rml";
            desc.rmlContent = rmlContent;
            desc.rcssContent = rcssContent;
            desc.isVisible = true;
            desc.rawDocumentHandle = newDoc;
            registerDocumentPostLoadHooks(newDoc, desc);
            m_documents.emplace(documentId, std::move(desc));
            return OkStatus();
        }

        it->second.rmlContent = rmlContent;
        it->second.rcssContent = rcssContent;
        it->second.rawDocumentHandle = newDoc;
        registerDocumentPostLoadHooks(newDoc, it->second);
        m_liveElementsDirty = true;
        return OkStatus();
    }

    Status RmlUiComposite::reloadDocumentFromDisk(const std::string& documentId)
    {
        ensureContextReady();
        if (!m_rmlContext)
        {
            return FailedPreconditionError("RmlUi Context 尚未初始化");
        }

        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        auto reloadSingleEntry = [this](RmlDocumentDesc& desc) -> Status {
            if (desc.sourcePath.empty() || desc.sourcePath.starts_with("Memory://"))
            {
                return InvalidArgumentError("内存文档不支持从磁盘重载");
            }

            const bool wasVisible = desc.isVisible;
            if (desc.rawDocumentHandle)
            {
                auto* oldDoc = static_cast<Rml::ElementDocument*>(desc.rawDocumentHandle);
                if (m_activeDocument == oldDoc)
                {
                    m_activeDocument = nullptr;
                }
                oldDoc->Close();
                desc.rawDocumentHandle = nullptr;
            }

            auto* resModule = Resource::Resource::InstancePtr();
            auto* loader = resModule ? resModule->getResourceLoader() : nullptr;
            const std::string resolvedDiskPath = resolveProjectUiAssetPath(desc.sourcePath);
            if (loader && loader->exists(resolvedDiskPath))
            {
                if (auto loadRes = loader->loadFile(resolvedDiskPath); loadRes.ok())
                {
                    desc.rmlContent = *loadRes;
                }
            }
            else if (FCT::SAI::FileSystem::exists(resolvedDiskPath))
            {
                if (auto saiRes = FCT::SAI::FileSystem::readTextFile(resolvedDiskPath); saiRes.ok())
                {
                    desc.rmlContent = *saiRes;
                }
            }

            Rml::ElementDocument* reloadedDoc = m_rmlContext->LoadDocument(desc.sourcePath);
            if (!reloadedDoc)
            {
                return InternalError(StrCat("重载 RML 文档失败: ", desc.sourcePath));
            }

            desc.rawDocumentHandle = reloadedDoc;
            desc.isVisible = wasVisible;
            registerDocumentPostLoadHooks(reloadedDoc, desc);
            if (!wasVisible)
            {
                reloadedDoc->Hide();
            }
            m_liveElementsDirty = true;
            return OkStatus();
        };

        if (!documentId.empty())
        {
            auto it = m_documents.find(documentId);
            if (it == m_documents.end())
            {
                return NotFoundError(StrCat("未找到待重载文档: ", documentId));
            }
            return reloadSingleEntry(it->second);
        }

        for (auto& [id, desc] : m_documents)
        {
            (void)id;
            if (!desc.sourcePath.empty() && !desc.sourcePath.starts_with("Memory://"))
            {
                CHECK_STATUS(reloadSingleEntry(desc));
            }
        }
        return OkStatus();
    }

    const RmlDocumentDesc* RmlUiComposite::getDocument(const std::string& documentId) const
    {
        auto it = m_documents.find(documentId);
        return (it != m_documents.end()) ? &it->second : nullptr;
    }

    RmlDocumentDesc* RmlUiComposite::getDocumentMutable(const std::string& documentId)
    {
        auto it = m_documents.find(documentId);
        return (it != m_documents.end()) ? &it->second : nullptr;
    }

    Status RmlUiComposite::showDocument(const std::string& documentId)
    {
        auto it = m_documents.find(documentId);
        if (it == m_documents.end())
        {
            return NotFoundError(StrCat("未找到文档: ", documentId));
        }
        it->second.isVisible = true;
        if (auto* doc = static_cast<Rml::ElementDocument*>(it->second.rawDocumentHandle))
        {
            doc->Show();
            m_activeDocument = doc;
            if (m_leaderboardController)
            {
                m_leaderboardController->syncVisibilityFromDocument(doc);
            }
        }
        return OkStatus();
    }

    Status RmlUiComposite::hideDocument(const std::string& documentId)
    {
        auto it = m_documents.find(documentId);
        if (it == m_documents.end())
        {
            return NotFoundError(StrCat("未找到文档: ", documentId));
        }
        it->second.isVisible = false;
        if (auto* doc = static_cast<Rml::ElementDocument*>(it->second.rawDocumentHandle))
        {
            doc->Hide();
            if (m_leaderboardController)
            {
                m_leaderboardController->syncVisibilityFromDocument(doc);
            }
        }
        return OkStatus();
    }

    Status RmlUiComposite::closeDocument(const std::string& documentId)
    {
        auto it = m_documents.find(documentId);
        if (it == m_documents.end())
        {
            return NotFoundError(StrCat("未找到文档: ", documentId));
        }

        if (auto* doc = static_cast<Rml::ElementDocument*>(it->second.rawDocumentHandle))
        {
            if (m_activeDocument == doc)
            {
                m_activeDocument = nullptr;
            }
            doc->Close();
        }
        m_documents.erase(it);
        if (!m_activeDocument && !m_documents.empty())
        {
            m_activeDocument = static_cast<Rml::ElementDocument*>(m_documents.begin()->second.rawDocumentHandle);
        }
        return OkStatus();
    }

    void RmlUiComposite::unloadAllDocuments()
    {
        m_activeDocument = nullptr;
        m_documents.clear();
        if (m_keyboardBridge)
        {
            m_keyboardBridge->bindActiveDocument(nullptr);
        }
        if (m_touchController)
        {
            m_touchController->clearBindings();
        }
        if (m_rmlContext)
        {
            m_rmlContext->UnloadAllDocuments();
            m_rmlContext->Update();
        }
    }

    bool RmlUiComposite::isDocumentVisible(const std::string& documentId) const
    {
        auto it = m_documents.find(documentId);
        return (it != m_documents.end()) ? it->second.isVisible : false;
    }

    void RmlUiComposite::processMouseMove(int x, int y, int keyModifier)
    {
        if (m_rmlContext)
        {
            m_rmlContext->ProcessMouseMove(x, y, keyModifier);
        }
    }

    void RmlUiComposite::processMouseButton(int button, bool isDown, int keyModifier)
    {
        ZoneScopedN("RmlUiComposite::processMouseButton");
        if (!m_rmlContext)
        {
            return;
        }

        if (isDown)
        {
            m_rmlContext->ProcessMouseButtonDown(button, keyModifier);
        }
        else
        {
            m_rmlContext->ProcessMouseButtonUp(button, keyModifier);
        }
    }

    void RmlUiComposite::processMouseWheel(float delta, int keyModifier)
    {
        if (m_rmlContext)
        {
            m_rmlContext->ProcessMouseWheel(delta, keyModifier);
        }
    }

    void RmlUiComposite::processKeyDown(int key, int keyModifier)
    {
        if (m_rmlContext)
        {
            m_rmlContext->ProcessKeyDown(static_cast<Rml::Input::KeyIdentifier>(key), keyModifier);
        }
    }

    void RmlUiComposite::processKeyUp(int key, int keyModifier)
    {
        if (m_rmlContext)
        {
            m_rmlContext->ProcessKeyUp(static_cast<Rml::Input::KeyIdentifier>(key), keyModifier);
        }
    }

    void RmlUiComposite::processTextInput(const std::string& text)
    {
        if (m_rmlContext && !text.empty())
        {
            m_rmlContext->ProcessTextInput(Rml::String(text.c_str(), text.size()));
        }
    }

    bool RmlUiComposite::processTouchDown(int pointerId, float x, float y)
    {
        return m_touchController ? m_touchController->onTouchDown(pointerId, x, y) : false;
    }

    bool RmlUiComposite::processTouchMove(int pointerId, float x, float y)
    {
        return m_touchController ? m_touchController->onTouchMove(pointerId, x, y) : false;
    }

    bool RmlUiComposite::processTouchUp(int pointerId, float x, float y)
    {
        return m_touchController ? m_touchController->onTouchUp(pointerId, x, y) : false;
    }

    void RmlUiComposite::onWindowResize(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        if (width == m_viewportWidth && height == m_viewportHeight &&
            (!m_rmlContext || m_rmlContext->GetDimensions() == Rml::Vector2i(width, height)))
        {
            return;
        }
        m_viewportWidth = width;
        m_viewportHeight = height;
        if (m_capsuleInjector)
        {
            m_capsuleInjector->onWindowResize(width, height);
        }
        else if (m_rmlContext)
        {
            m_rmlContext->SetDimensions(Rml::Vector2i(width, height));
        }
    }

    RmlGeneratedDrawBucket RmlUiComposite::generateDrawBucket(uint32_t boundTechId) const
    {
        if (m_renderInterface)
        {
            return RmlGeneratedDrawBucket(m_renderInterface->buildAggregateDrawBucket(boundTechId));
        }
        return RmlGeneratedDrawBucket(
            boundTechId,
            Domain::Renderer::InputTags::UI | Domain::Renderer::InputTags::RmlUi,
            1u);
    }

    void RmlUiComposite::submitUiDrawBuckets()
    {
        if (!m_renderInterface)
        {
            return;
        }
        auto* domain = Domain::Domain::InstancePtr();
        if (!domain)
        {
            return;
        }
        const std::string primaryTarget = domain->renderer.getPrimaryPresentTargetName();
        if (!primaryTarget.empty() && primaryTarget != "MainWindow")
        {
            return;
        }

        std::vector<Domain::Renderer::DrawBucket> buckets;
        m_renderInterface->collectDrawBuckets(buckets);
        for (const auto& bucket : buckets)
        {
            domain->renderer.submitDrawBucket(bucket);
        }
    }

    VirtualTextureRouter* RmlUiComposite::getTextureRouter() noexcept
    {
        return m_renderInterface ? &m_renderInterface->getTextureRouter() : nullptr;
    }

    void RmlUiComposite::registerEventListener(
        const std::string& elementId,
        const std::string& eventType,
        ElementEventListener listener)
    {
        assert(!elementId.empty() && !eventType.empty() && listener != nullptr && "事件监听器参数不能为空");
        const std::string key = elementId + ":" + eventType;
        m_eventListeners[key].push_back(std::move(listener));
        if (m_domEventDispatcher && m_rmlContext)
        {
            m_domEventDispatcher->ensureEventAttached(m_rmlContext, eventType);
        }
    }

    void RmlUiComposite::bindDataModel(const std::string& modelName, void* modelData)
    {
        assert(!modelName.empty() && "数据模型名称不能为空");
        m_dataModels[modelName] = modelData;
    }

    Rml::Element* RmlUiComposite::findDomElementById(const std::string& elementId) const
    {
        if (elementId.empty())
        {
            return nullptr;
        }
        if (m_activeDocument)
        {
            if (Rml::Element* elem = m_activeDocument->GetElementById(elementId.c_str()))
            {
                return elem;
            }
        }
        for (const auto& [docId, desc] : m_documents)
        {
            (void)docId;
            if (!desc.isVisible || !desc.rawDocumentHandle)
            {
                continue;
            }
            auto* doc = static_cast<Rml::ElementDocument*>(desc.rawDocumentHandle);
            if (doc != m_activeDocument)
            {
                if (Rml::Element* elem = doc->GetElementById(elementId.c_str()))
                {
                    return elem;
                }
            }
        }
        for (const auto& [docId, desc] : m_documents)
        {
            (void)docId;
            if (desc.isVisible || !desc.rawDocumentHandle)
            {
                continue;
            }
            auto* doc = static_cast<Rml::ElementDocument*>(desc.rawDocumentHandle);
            if (Rml::Element* elem = doc->GetElementById(elementId.c_str()))
            {
                return elem;
            }
        }
        return nullptr;
    }

    bool RmlUiComposite::hasAnyVisibleDocument() const noexcept
    {
        for (const auto& [docId, desc] : m_documents)
        {
            (void)docId;
            if (desc.isVisible && desc.rawDocumentHandle != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void RmlUiComposite::dispatchDomEvent(const std::string& eventType, Rml::Element* targetElement)
    {
        if (eventType.empty() || !targetElement)
        {
            return;
        }
        for (Rml::Element* cur = targetElement; cur != nullptr; cur = cur->GetParentNode())
        {
            const Rml::String& idStr = cur->GetId();
            if (idStr.empty())
            {
                continue;
            }
            const std::string elemId(idStr.c_str(), idStr.size());
            const std::string key = elemId + ":" + eventType;
            auto it = m_eventListeners.find(key);
            if (it != m_eventListeners.end() && !it->second.empty())
            {
                const auto callbacksCopy = it->second;
                for (const auto& cb : callbacksCopy)
                {
                    if (cb)
                    {
                        cb(eventType, elemId);
                    }
                }
                break;
            }
        }
    }

    bool RmlUiComposite::setElementInnerText(const std::string& elementId, const std::string& text)
    {
        if (Rml::Element* elem = findDomElementById(elementId))
        {
            elem->SetInnerRML(text.c_str());
            return true;
        }
        return false;
    }

    bool RmlUiComposite::setElementStyleProperty(
        const std::string& elementId,
        const std::string& property,
        const std::string& value)
    {
        if (Rml::Element* elem = findDomElementById(elementId))
        {
            return elem->SetProperty(property.c_str(), value.c_str());
        }
        return false;
    }

    bool RmlUiComposite::toggleElementClass(const std::string& elementId, const std::string& className)
    {
        if (Rml::Element* elem = findDomElementById(elementId))
        {
            const bool hasClass = elem->IsClassSet(className.c_str());
            elem->SetClass(className.c_str(), !hasClass);
            return !hasClass;
        }
        return false;
    }

    bool RmlUiComposite::setElementClass(const std::string& elementId, const std::string& className, bool enabled)
    {
        if (Rml::Element* elem = findDomElementById(elementId))
        {
            if (elem->IsClassSet(className.c_str()) != enabled)
            {
                elem->SetClass(className.c_str(), enabled);
            }
            return true;
        }
        return false;
    }

    bool RmlUiComposite::isPointerOverUi() const noexcept
    {
        if (!m_rmlContext || !m_pointerInsideViewport || !hasAnyVisibleDocument())
        {
            return false;
        }
        Rml::Element* hover = m_rmlContext->GetHoverElement();
        if (!hover || hover == m_rmlContext->GetRootElement())
        {
            return false;
        }
        const Rml::String& tag = hover->GetTagName();
        if (tag == "body" || tag == "rml")
        {
            return false;
        }
        for (Rml::Element* cur = hover; cur != nullptr; cur = cur->GetParentNode())
        {
            if (cur->GetId() == "toast_banner")
            {
                return false;
            }
        }
        return true;
    }

    void RmlUiComposite::initOverlayGpuStates(FCT::Context* ctx)
    {
        if (!ctx)
        {
            return;
        }

        if (!m_overlayRasterState)
        {
            m_overlayRasterState = ctx->createResource<FCT::RasterizationState>();
            if (m_overlayRasterState)
            {
                m_overlayRasterState->polygonMode(FCT::PolygonMode::Fill);
                m_overlayRasterState->cullMode(FCT::CullMode::None);
                m_overlayRasterState->frontFace(FCT::FrontFace::CounterClockwise);
                m_overlayRasterState->multisampleEnable(false);
                m_overlayRasterState->sampleCount(1u);
                m_overlayRasterState->create();
            }
        }

        if (!m_overlayDepthState)
        {
            m_overlayDepthState = ctx->createResource<FCT::DepthStencilState>();
            if (m_overlayDepthState)
            {
                m_overlayDepthState->depthTestEnable(false);
                m_overlayDepthState->depthWriteEnable(false);
                m_overlayDepthState->depthCompareOp(FCT::CompareOp::Always);
                m_overlayDepthState->stencilTestEnable(false);
                m_overlayDepthState->create();
            }
        }

        if (!m_overlayBlendState)
        {
            m_overlayBlendState = ctx->createResource<FCT::BlendState>();
            if (m_overlayBlendState)
            {
                m_overlayBlendState->enable(true, 0)
                    .colorFactors(FCT::BlendFactor::SrcAlpha, FCT::BlendFactor::OneMinusSrcAlpha, 0)
                    .alphaFactors(FCT::BlendFactor::One, FCT::BlendFactor::OneMinusSrcAlpha, 0)
                    .op(FCT::BlendOp::Add, 0)
                    .mask(0xFu, 0);
                m_overlayBlendState->create();
            }
        }
    }

    void RmlUiComposite::ensureOverlayPipelineInitialized(FCT::Context* ctx)
    {
        if (!ctx)
        {
            return;
        }
        if (!m_overlayLayout)
        {
            m_overlayLayout = makeUnique<FCT::Layout>(
                ctx,
                FCT::PassName("AntiAliasingPass"),
                Domain::Renderer::StandardMeshVertexLayout,
                Domain::Renderer::StandardPixelLayout);
        }
        if (!m_overlayRasterState || !m_overlayDepthState || !m_overlayBlendState)
        {
            initOverlayGpuStates(ctx);
        }
        if (m_overlayLayout)
        {
            m_overlayVsRef = m_overlayLayout->cacheVertexShader(kRmlOverlayVertexShaderHlsl);
            m_overlayPsRef = m_overlayLayout->cachePixelShader(kRmlOverlayPixelShaderHlsl);
        }
    }

    void RmlUiComposite::registerOverlayRenderExtension()
    {
        if (m_overlayExtensionRegistered)
        {
            return;
        }
        auto* domain = Domain::Domain::InstancePtr();
        if (!domain)
        {
            return;
        }

        Domain::Renderer::RenderGraphExtensionDesc ext;
        ext.name = kRmlOverlayExtName;

        ext.onSetupPass = [this](FCT::RenderGraph*) {
            auto* renderHost = Host::RenderHost::InstancePtr();
            FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
            if (!ctx)
            {
                return;
            }
            if (!m_overlayLayout)
            {
                m_overlayLayout = makeUnique<FCT::Layout>(
                    ctx,
                    FCT::PassName("AntiAliasingPass"),
                    Domain::Renderer::StandardMeshVertexLayout,
                    Domain::Renderer::StandardPixelLayout);
            }
            initOverlayGpuStates(ctx);
        };

        ext.onPostCompile = [this](FCT::RenderGraph* graph) {
            auto* renderHost = Host::RenderHost::InstancePtr();
            FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
            ensureOverlayPipelineInitialized(ctx);

            if (graph && !m_hasAaPassSub)
            {
                m_aaPassSubId = graph->subscribe("AntiAliasingPass", [this](FCT::PassSubmitEvent env) {
                    recordRmlOverlayOnSwapchain(env);
                });
                m_hasAaPassSub = true;
            }

            if (auto* curRenderer = Domain::Renderer::RendererSubsystem::InstancePtr();
                curRenderer && !m_overlaySyncRegistered)
            {
                curRenderer->requestSyncTicker("SyncRmlUiOverlay", [this]() {
                    syncRmlOverlayOnBarrier();
                });
                m_overlaySyncRegistered = true;
            }
            m_overlayStagedDirty = true;
        };

        domain->renderer.registerRenderGraphExtension(std::move(ext));
        m_overlayExtensionRegistered = true;
    }

    uint32_t RmlUiComposite::acquireOverlayBindlessSlot(FCT::Image* image)
    {
        if (!image)
        {
            return 0u;
        }
        auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr();
        auto* bindless = renderer ? renderer->getBindlessManager() : nullptr;
        if (!bindless || !bindless->isInitialized())
        {
            return 0u;
        }

        for (uint32_t i = 0; i < kOverlayBindlessSlotCapacity; ++i)
        {
            if (m_overlaySlotImages[i] == image)
            {
                return kOverlayBindlessBaseSlot + i;
            }
        }

        uint32_t chosenIdx = kOverlayBindlessSlotCapacity;
        for (uint32_t i = 0; i < kOverlayBindlessSlotCapacity; ++i)
        {
            if (m_overlaySlotImages[i] == nullptr)
            {
                chosenIdx = i;
                break;
            }
        }
        if (chosenIdx >= kOverlayBindlessSlotCapacity)
        {
            chosenIdx = m_nextOverlaySlotCursor % kOverlayBindlessSlotCapacity;
            m_nextOverlaySlotCursor = (m_nextOverlaySlotCursor + 1u) % kOverlayBindlessSlotCapacity;
        }

        m_overlaySlotImages[chosenIdx] = image;
        const uint32_t slot = kOverlayBindlessBaseSlot + chosenIdx;
        bindless->updateTextureSlot(slot, image);
        return slot;
    }

    void RmlUiComposite::syncRmlOverlayOnBarrier()
    {
        ZoneScopedN("RmlUiComposite::syncRmlOverlayOnBarrier");
        auto* renderHost = Host::RenderHost::InstancePtr();
        FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
        if (!ctx)
        {
            return;
        }

        auto* fontSys = Font::FontRuntimeComposite::getActiveFontSystem();
        if (fontSys)
        {
            (void)fontSys->flushDirtyPagesToGpu(ctx);
        }

        if (m_renderInterface)
        {
            m_renderInterface->getTextureRouter().flushDirtyAssetTexturesToGpu(ctx);
            m_renderInterface->flushGeneratedTexturesToGpu(ctx);

            auto retiredImages = m_renderInterface->takeRetiredGpuImages();
            if (!retiredImages.empty())
            {
                auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr();
                auto* bindless = renderer ? renderer->getBindlessManager() : nullptr;
                auto* gpuMem = Resource::GpuMemoryManager::InstancePtr();
                const uint64_t retiredFrame = renderHost->getRenderFrameCount() + 2u;

                for (auto& imgPtr : retiredImages)
                {
                    FCT::Image* rawImg = imgPtr.get();
                    if (!rawImg)
                    {
                        continue;
                    }
                    for (uint32_t i = 0; i < kOverlayBindlessSlotCapacity; ++i)
                    {
                        if (m_overlaySlotImages[i] == rawImg)
                        {
                            m_overlaySlotImages[i] = nullptr;
                            if (bindless && bindless->isInitialized())
                            {
                                bindless->updateTextureSlot(kOverlayBindlessBaseSlot + i, nullptr);
                            }
                        }
                    }
                    if (gpuMem)
                    {
                        rawImg->addRef();
                        imgPtr = nullptr;
                        gpuMem->deferRelease(
                            rawImg,
                            [](void* p) {
                                if (auto* im = static_cast<FCT::Image*>(p))
                                {
                                    im->release();
                                }
                            },
                            retiredFrame);
                    }
                }
            }
        }

        if (!m_overlayStagedDirty)
        {
            return;
        }
        m_overlayStagedDirty = false;

        if (m_stagedDrawCommands.empty())
        {
            m_hasValidOverlayMesh = false;
            return;
        }

        const float vpW = std::max(m_stagedViewportW, 1.0f);
        const float vpH = std::max(m_stagedViewportH, 1.0f);

        size_t totalIndicesCount = 0;
        for (const auto& cmd : m_stagedDrawCommands)
        {
            if (!cmd.vertices.empty() && !cmd.indices.empty())
            {
                totalIndicesCount += cmd.indices.size();
            }
        }
        if (totalIndicesCount == 0)
        {
            m_hasValidOverlayMesh = false;
            return;
        }

        const uint32_t nextSlot = (m_activeOverlayMeshSlot + 1u) % static_cast<uint32_t>(m_overlayMeshes.size());
        if (FCT::StaticMesh<uint32_t>* oldMesh = m_overlayMeshes[nextSlot])
        {
            auto* gpuTable = Resource::GpuResourceTable::InstancePtr();
            auto* gpuMem = Resource::GpuMemoryManager::InstancePtr();
            if (gpuMem)
            {
                const uint64_t retiredFrame = renderHost->getRenderFrameCount() + 1u;
                gpuMem->deferReleaseResource(
                    oldMesh,
                    [](const void* ptr) {
                        auto* m = const_cast<FCT::StaticMesh<uint32_t>*>(static_cast<const FCT::StaticMesh<uint32_t>*>(ptr));
                        if (auto* tbl = Resource::GpuResourceTable::InstancePtr())
                        {
                            tbl->unregisterResource(m);
                        }
                        FCT_DELETE(m);
                    },
                    retiredFrame);
            }
            else
            {
                ctx->waitGpu();
                if (gpuTable)
                {
                    gpuTable->unregisterResource(oldMesh);
                }
                FCT_DELETE(oldMesh);
            }
            m_overlayMeshes[nextSlot] = nullptr;
        }

        auto* mesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, Domain::Renderer::StandardMeshVertexLayout);
        std::vector<uint32_t> indices;
        indices.reserve(totalIndicesCount);
        uint32_t baseVertex = 0;

        for (const auto& cmd : m_stagedDrawCommands)
        {
            if (cmd.vertices.empty() || cmd.indices.empty())
            {
                continue;
            }

            float texMode = 0.0f;
            uint32_t bindlessSlot = 0u;
            if (cmd.texture != 0 && m_renderInterface)
            {
                auto& router = m_renderInterface->getTextureRouter();
                uint32_t atlasPageId = 0;
                if (router.tryResolveAtlasPageId(cmd.texture, atlasPageId) && fontSys)
                {
                    if (FCT::Image* atlasImg = fontSys->getAtlasPageTexture(atlasPageId))
                    {
                        bindlessSlot = acquireOverlayBindlessSlot(atlasImg);
                        if (bindlessSlot != 0u)
                        {
                            const auto fmt = fontSys->getAtlasManager().getAtlasPageFormat(atlasPageId);
                            texMode = (fmt == Resource::Font::TextureFormat::R8_UNORM) ? 1.0f : 2.0f;
                        }
                    }
                }
                else if (FCT::Image* genImg = m_renderInterface->resolveGeneratedGpuImage(cmd.texture))
                {
                    bindlessSlot = acquireOverlayBindlessSlot(genImg);
                    if (bindlessSlot != 0u)
                    {
                        texMode = 2.0f;
                    }
                }
                else if (FCT::Image* boundImg = router.resolveBoundGpuImage(cmd.texture))
                {
                    bindlessSlot = acquireOverlayBindlessSlot(boundImg);
                    if (bindlessSlot != 0u)
                    {
                        texMode = 2.0f;
                    }
                }
            }

            const float scLeft = static_cast<float>(cmd.scissor.Left());
            const float scTop = static_cast<float>(cmd.scissor.Top());
            const float scRight = static_cast<float>(cmd.scissor.Right());
            const float scBottom = static_cast<float>(cmd.scissor.Bottom());
            const float scFlag = cmd.scissorEnabled ? 1.0f : 0.0f;

            const uint32_t cmdVtxCount = static_cast<uint32_t>(cmd.vertices.size());
            for (const Rml::Vertex& v : cmd.vertices)
            {
                const float px = v.position.x + cmd.translation.x;
                const float py = v.position.y + cmd.translation.y;
                const float ndcX = (px / vpW) * 2.0f - 1.0f;
                const float ndcY = 1.0f - (py / vpH) * 2.0f;
                const FCT::Vec4 linearColor = rmlPremulVertexColorToLinearStraight(v.colour);

                mesh->addVertex(
                    linearColor,
                    FCT::Vec4(ndcX, ndcY, 0.0f, 1.0f),
                    FCT::Vec2(v.tex_coord.x, v.tex_coord.y),
                    FCT::Vec3<>(px, py, static_cast<float>(bindlessSlot)),
                    FCT::Vec3<>(texMode, scLeft, scTop),
                    FCT::Vec3<>(scRight, scBottom, scFlag));
            }

            for (int rawIdx : cmd.indices)
            {
                if (rawIdx >= 0 && static_cast<uint32_t>(rawIdx) < cmdVtxCount)
                {
                    indices.push_back(baseVertex + static_cast<uint32_t>(rawIdx));
                }
            }
            baseVertex += cmdVtxCount;
        }

        if (indices.empty())
        {
            FCT_DELETE(mesh);
            m_hasValidOverlayMesh = false;
            return;
        }

        mesh->setIndices(indices);
        mesh->create();

        if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr())
        {
            gpuTable->registerResource(
                mesh,
                Resource::GpuResourceType::Buffer,
                baseVertex * 96u + indices.size() * sizeof(uint32_t),
                "RmlUi_SwapchainOverlayMesh");
        }

        m_overlayMeshes[nextSlot] = mesh;
        m_activeOverlayMeshSlot = nextSlot;
        m_hasValidOverlayMesh = true;
    }

    void RmlUiComposite::recordRmlOverlayOnSwapchain(FCT::PassSubmitEvent env)
    {
        ZoneScopedN("RmlUiComposite::recordRmlOverlayOnSwapchain");
        if (!m_useVulkanSwapchainOverlay || !m_hasValidOverlayMesh || !m_overlayLayout || !env.cmdBuf)
        {
            return;
        }

        auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr();
        if (renderer)
        {
            const std::string primaryTarget = renderer->getPrimaryPresentTargetName();
            if (!primaryTarget.empty() && primaryTarget != "MainWindow")
            {
                return;
            }
        }

        FCT::StaticMesh<uint32_t>* mesh = m_overlayMeshes[m_activeOverlayMeshSlot];
        if (!mesh)
        {
            return;
        }

        auto* cmdBuf = env.cmdBuf;
        m_overlayLayout->begin();
        if (m_overlayRasterState) m_overlayLayout->bindRasterizationState(m_overlayRasterState);
        if (m_overlayDepthState) m_overlayLayout->bindDepthStencilState(m_overlayDepthState);
        if (m_overlayBlendState) m_overlayLayout->bindBlendState(m_overlayBlendState);
        m_overlayLayout->bindVertexShader(m_overlayVsRef);
        m_overlayLayout->bindPixelShader(m_overlayPsRef);

        auto* pipeline = m_overlayLayout->getCurrentPipeline();
        if (!pipeline || pipeline->getNativeHandle() == 0)
        {
            m_overlayLayout->end();
            return;
        }

        pipeline->bind(cmdBuf);

        if (renderer)
        {
            if (auto* bindless = renderer->getBindlessManager(); bindless && bindless->isInitialized())
            {
                uint32_t submitFrameIdx = 0;
                if (auto* renderHost = Host::RenderHost::InstancePtr())
                {
                    if (FCT::Context* ctx = renderHost->getContext())
                    {
                        submitFrameIdx = ctx->currentSubmitFrameIndex();
                    }
                }
                void* nativeLayout = reinterpret_cast<void*>(pipeline->getNativeLayoutHandle());
                bindless->bind(cmdBuf, submitFrameIdx, nativeLayout);
            }
        }

        if (auto* passRes = m_overlayLayout->getCurrentPassResource())
        {
            passRes->bind(cmdBuf, pipeline);
        }

        mesh->bind(cmdBuf);
        mesh->draw(cmdBuf);
        m_overlayLayout->end();
    }

    void RmlUiComposite::releaseOverlayGpuResources()
    {
        if (auto* renderHost = Host::RenderHost::InstancePtr())
        {
            if (FCT::Context* ctx = renderHost->getContext())
            {
                ctx->waitGpu();
            }
        }

        if (auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr())
        {
            if (auto* bindless = renderer->getBindlessManager(); bindless && bindless->isInitialized())
            {
                for (uint32_t i = 0; i < kOverlayBindlessSlotCapacity; ++i)
                {
                    if (m_overlaySlotImages[i] != nullptr)
                    {
                        bindless->updateTextureSlot(kOverlayBindlessBaseSlot + i, nullptr);
                        m_overlaySlotImages[i] = nullptr;
                    }
                }
            }
            else
            {
                m_overlaySlotImages.fill(nullptr);
            }
        }
        else
        {
            m_overlaySlotImages.fill(nullptr);
        }

        if (m_renderInterface)
        {
            m_renderInterface->releaseAllGpuTextures();
        }

        m_overlayLayout.reset();

        auto* gpuTable = Resource::GpuResourceTable::InstancePtr();
        for (auto*& mesh : m_overlayMeshes)
        {
            if (mesh)
            {
                if (gpuTable)
                {
                    gpuTable->unregisterResource(mesh);
                }
                FCT_DELETE(mesh);
                mesh = nullptr;
            }
        }
        m_hasValidOverlayMesh = false;

        if (m_overlayRasterState)
        {
            m_overlayRasterState->release();
            m_overlayRasterState = nullptr;
        }
        if (m_overlayDepthState)
        {
            m_overlayDepthState->release();
            m_overlayDepthState = nullptr;
        }
        if (m_overlayBlendState)
        {
            m_overlayBlendState->release();
            m_overlayBlendState = nullptr;
        }
    }
} // namespace MQEngine::GameHtmlUI

MQ_EXPORT_COMPOSITE(MQEngine::GameHtmlUI::RmlUiComposite)
