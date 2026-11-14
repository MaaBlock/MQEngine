/**
 * @file RmlUiComposite.h
 * @brief 基于 RmlUi 6.0 的轻量级游戏运行时 HTML/CSS UI Composite（集成 FEAT-47 ~ FEAT-54）
 */
#ifndef MQENGINE_GAME_HTML_UI_RML_UI_COMPOSITE_H
#define MQENGINE_GAME_HTML_UI_RML_UI_COMPOSITE_H

#include "../../GameHtmlUiApi.h"
#include "../../IRmlUiHotReloadBridge.h"
#include "MQRmlFontEngine.h"
#include "RmlUiBackendInterfaces.h"
#include "RmlUiMiniGameAdapters.h"
#include "RmlUiTouchInput.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/domain/renderer/DrawBucket.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
    class FileInterface;
}

namespace MQEngine::GameHtmlUI
{
    class MQRmlDomEventDispatcher;
    struct RmlDomElement
    {
        std::string tag;
        std::string id;
        std::string text;
        float x = 24.0f;
        float y = 24.0f;
        float width = 220.0f;
        float height = 36.0f;
        uint32_t bgColor = 0xAA71CC2Eu;
        uint32_t textColor = 0xFFFFFFFFu;
        bool hovered = false;
    };

    struct RmlDocumentDesc
    {
        std::string id;
        std::string sourcePath;
        std::string rmlContent;
        std::string rcssContent;
        bool isVisible = true;
        void* rawDocumentHandle = nullptr;
        std::vector<RmlDomElement> elements;
    };

    /**
     * @brief 携带输入标签语义查询辅助的 RmlUi 绘制桶封装 (FEAT-49)
     */
    struct RmlGeneratedDrawBucket : public Domain::Renderer::DrawBucket
    {
        using Domain::Renderer::DrawBucket::DrawBucket;

        explicit RmlGeneratedDrawBucket(const Domain::Renderer::DrawBucket& base)
            : Domain::Renderer::DrawBucket(base)
        {
        }

        [[nodiscard]] bool hasInputTag(std::string_view tagName) const noexcept
        {
            const uint64_t bit = Domain::Renderer::TagRegistry::StringToInputTag(tagName);
            return bit != 0ULL && ((inputTagsMask & bit) == bit);
        }
    };

    /**
     * @brief 运行时 HTML/CSS 游戏界面 Composite：
     *        剥离内置 FreeType 与调试层，直连微内核 FontSystem、Backbuffer 直出渲染、
     *        声明式虚拟触控直注 Enhanced Input、异形屏胶囊 CSS 避让与软键盘/开放数据域闭环。
     */
    class RML_UI_COMPOSITE_API RmlUiComposite : public Asset::Composite, public IRmlUiHotReloadBridge
    {
    public:
        using ElementEventListener = std::function<void(const std::string& eventType, const std::string& targetId)>;

        explicit RmlUiComposite(Rml::FileInterface* customFileInterface = nullptr);
        ~RmlUiComposite() override;

        RmlUiComposite(const RmlUiComposite&) = delete;
        RmlUiComposite& operator=(const RmlUiComposite&) = delete;

        static RmlUiComposite* getActiveInstance() noexcept;

        // ==================== Composite 生命周期实现 ====================

        Status onLoad(Runtime::Engine* engine) override;
        Status onInit(Runtime::Engine* engine) override;
        void onTick(float deltaTime) override;
        void onUnload() override;

        std::string getName() const override { return "RmlUiComposite"; }
        std::string getPluginName() const override { return "GameHtmlUI"; }
        std::vector<std::string> getDependencies() const override { return {}; }
        std::vector<std::string> getRequiredTags() const override { return {"Runtime", "Game"}; }

        // ==================== 运行期裁剪与字体初始化契约 (FEAT-47 / FEAT-48) ====================

        void initFontSubsystem();
        [[nodiscard]] bool isCustomFontEngineActive() const noexcept { return true; }
        [[nodiscard]] bool isDefaultFontEngineActive() const noexcept { return false; }
        [[nodiscard]] bool isDebuggerEnabled() const noexcept;

        // ==================== 文档生命周期管理接口 ====================

        virtual Status loadDocument(const std::string& path, const std::string& documentId = "");
        virtual Status loadDocumentFromMemory(const std::string& rmlContent, const std::string& documentId);
        virtual Status updateDocumentContent(
            const std::string& documentId,
            const std::string& rmlContent,
            const std::string& rcssContent);
        virtual Status reloadDocumentFromDisk(const std::string& documentId = "") override;
        virtual Status showDocument(const std::string& documentId);
        virtual Status hideDocument(const std::string& documentId);
        virtual Status closeDocument(const std::string& documentId);
        void unloadAllDocuments();
        void unloadAll() { unloadAllDocuments(); }

        virtual bool isDocumentVisible(const std::string& documentId) const;
        virtual const RmlDocumentDesc* getDocument(const std::string& documentId) const;
        virtual RmlDocumentDesc* getDocumentMutable(const std::string& documentId);

        // ==================== 键鼠与多指触控事件路由 (FEAT-51) ====================

        void processMouseMove(int x, int y, int keyModifier = 0);
        void processMouseButton(int button, bool isDown, int keyModifier = 0);
        void processMouseWheel(float delta, int keyModifier = 0);
        void processKeyDown(int key, int keyModifier = 0);
        void processKeyUp(int key, int keyModifier = 0);
        void processTextInput(const std::string& text);

        bool processTouchDown(int pointerId, float x, float y);
        bool processTouchMove(int pointerId, float x, float y);
        bool processTouchUp(int pointerId, float x = 0.0f, float y = 0.0f);
        void onWindowResize(int width, int height);

        // ==================== 数据绑定与事件监听 ====================

        void registerEventListener(
            const std::string& elementId,
            const std::string& eventType,
            ElementEventListener listener);

        void bindDataModel(const std::string& modelName, void* modelData);

        // ==================== Backbuffer 直绘与子组件访问器 (FEAT-49 ~ FEAT-54) ====================

        [[nodiscard]] bool isBackbufferTarget() const noexcept { return true; }
        [[nodiscard]] bool allocatesOffscreenRt() const noexcept { return false; }
        [[nodiscard]] MQRmlPassDescriptor getPassDescriptor() const { return MQRmlPassDescriptor{}; }
        [[nodiscard]] MQRmlPipelineRenderState getPipelineRenderState() const { return MQRmlPipelineRenderState{}; }
        [[nodiscard]] RmlGeneratedDrawBucket generateDrawBucket(uint32_t boundTechId = 1) const;
        void submitUiDrawBuckets();

        [[nodiscard]] Rml::Context* getRmlContext() const noexcept { return m_rmlContext; }
        [[nodiscard]] Rml::ElementDocument* getActiveDocument() const noexcept { return m_activeDocument; }
        [[nodiscard]] uint32_t getActiveDocumentCount() const noexcept { return static_cast<uint32_t>(m_documents.size()); }
        [[nodiscard]] uint32_t getDocCount() const noexcept { return getActiveDocumentCount(); }

        [[nodiscard]] MQRmlFontEngine* getFontEngine() const noexcept { return m_fontEngine.get(); }
        [[nodiscard]] MQRmlRenderInterface* getRenderInterface() const noexcept { return m_renderInterface.get(); }
        [[nodiscard]] MQRmlFileInterface* getFileInterface() const noexcept { return m_fileInterface.get(); }
        [[nodiscard]] VirtualTextureRouter* getTextureRouter() noexcept;
        [[nodiscard]] VirtualTouchController* getTouchController() const noexcept { return m_touchController.get(); }
        [[nodiscard]] CapsuleCssVariableInjector* getCapsuleInjector() const noexcept { return m_capsuleInjector.get(); }
        [[nodiscard]] RmlUiKeyboardBridge* getKeyboardBridge() const noexcept { return m_keyboardBridge.get(); }
        [[nodiscard]] SplashRelayCoordinator* getSplashCoordinator() const noexcept { return m_splashCoordinator.get(); }
        [[nodiscard]] OpenDataLeaderboardController* getLeaderboardController() const noexcept { return m_leaderboardController.get(); }

        void syncLiveElementsFromDoc(Rml::ElementDocument* doc, RmlDocumentDesc& desc);

        // ==================== DOM 元素动态操作接口 ====================

        bool setElementInnerText(const std::string& elementId, const std::string& text);
        bool setElementStyleProperty(const std::string& elementId, const std::string& property, const std::string& value);
        bool toggleElementClass(const std::string& elementId, const std::string& className);
        bool setElementClass(const std::string& elementId, const std::string& className, bool enabled);
        [[nodiscard]] virtual bool isPointerOverUi() const noexcept;

    private:
        friend class MQRmlDomEventDispatcher;

        void initRmlSystem();
        void ensureContextReady();
        void shutdownRmlSystem();
        void forwardEngineHidSnapshot();
        void registerDocumentPostLoadHooks(Rml::ElementDocument* doc, RmlDocumentDesc& desc);
        Rml::Element* findDomElementById(const std::string& elementId) const;
        bool hasAnyVisibleDocument() const noexcept;
        void dispatchDomEvent(const std::string& eventType, Rml::Element* targetElement);

        void registerOverlayRenderExtension();
        void ensureOverlayPipelineInitialized(FCT::Context* ctx);
        void initOverlayGpuStates(FCT::Context* ctx);
        void syncRmlOverlayOnBarrier();
        void recordRmlOverlayOnSwapchain(FCT::PassSubmitEvent env);
        void releaseOverlayGpuResources();
        uint32_t acquireOverlayBindlessSlot(FCT::Image* image);

    private:
        static constexpr size_t kOverlayMeshSlotCount = 4;
        static constexpr uint32_t kOverlayBindlessBaseSlot = 3900;
        static constexpr uint32_t kOverlayBindlessSlotCapacity = 64;

        Rml::FileInterface* m_customFileInterface = nullptr;
        Rml::Context* m_rmlContext = nullptr;
        Rml::ElementDocument* m_activeDocument = nullptr;
        std::string m_contextName;

        UniquePtr<MQRmlSystemInterface> m_systemInterface;
        UniquePtr<MQRmlFileInterface> m_fileInterface;
        UniquePtr<MQRmlFontEngine> m_fontEngine;
        UniquePtr<MQRmlRenderInterface> m_renderInterface;
        UniquePtr<MQRmlDomEventDispatcher> m_domEventDispatcher;

        UniquePtr<VirtualTouchController> m_touchController;
        UniquePtr<CapsuleCssVariableInjector> m_capsuleInjector;
        UniquePtr<RmlUiKeyboardBridge> m_keyboardBridge;
        UniquePtr<SplashRelayCoordinator> m_splashCoordinator;
        UniquePtr<OpenDataLeaderboardController> m_leaderboardController;

        std::unordered_map<std::string, RmlDocumentDesc> m_documents;
        std::unordered_map<std::string, std::vector<ElementEventListener>> m_eventListeners;
        std::unordered_map<std::string, void*> m_dataModels;

        int m_viewportWidth = 1920;
        int m_viewportHeight = 1080;
        bool m_rmlInitialized = false;
        bool m_fontsInitialized = false;
        bool m_pointerInsideViewport = true;
        bool m_liveElementsDirty = false;

        bool m_useVulkanSwapchainOverlay = false;
        bool m_overlayExtensionRegistered = false;
        bool m_overlaySyncRegistered = false;
        bool m_hasAaPassSub = false;
        FCT::SubscribeId m_aaPassSubId{};

        std::vector<MQRmlDrawCommand> m_stagedDrawCommands;
        float m_stagedViewportW = 1920.0f;
        float m_stagedViewportH = 1080.0f;
        bool m_overlayStagedDirty = false;

        UniquePtr<FCT::Layout> m_overlayLayout;
        FCT::ShaderRef m_overlayVsRef;
        FCT::ShaderRef m_overlayPsRef;
        FCT::RasterizationState* m_overlayRasterState = nullptr;
        FCT::DepthStencilState* m_overlayDepthState = nullptr;
        FCT::BlendState* m_overlayBlendState = nullptr;
        std::array<FCT::StaticMesh<uint32_t>*, kOverlayMeshSlotCount> m_overlayMeshes{};
        uint32_t m_activeOverlayMeshSlot = 0;
        bool m_hasValidOverlayMesh = false;
        std::array<FCT::Image*, kOverlayBindlessSlotCapacity> m_overlaySlotImages{};
        uint32_t m_nextOverlaySlotCursor = 0;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_RML_UI_COMPOSITE_H
