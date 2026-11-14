/**
 * @file RmlUiMiniGameAdapters.h
 * @brief 小游戏宿主适配组件：异形屏胶囊 CSS 变量注入 (FEAT-52)、软键盘桥接 (FEAT-53) 与首屏秒开/共享画布控制 (FEAT-54)
 */
#ifndef MQENGINE_GAME_HTML_UI_RML_MINIGAME_ADAPTERS_H
#define MQENGINE_GAME_HTML_UI_RML_MINIGAME_ADAPTERS_H

#include "../../GameHtmlUiApi.h"
#include "RmlUiBackendInterfaces.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetPreviousSibling
#undef GetPreviousSibling
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <RmlUi/Core/EventListener.h>

#include <cstdint>
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
}

namespace MQEngine::GameHtmlUI
{
    /**
     * @brief 异形屏安全区与抖音右上角官方胶囊避让 CSS 变量注入器 (FEAT-52)
     */
    class RML_UI_COMPOSITE_API CapsuleCssVariableInjector
    {
    public:
        explicit CapsuleCssVariableInjector(Rml::Context* rmlContext = nullptr);
        ~CapsuleCssVariableInjector() = default;

        static void registerCustomPropertiesOnce();

        void bindRmlContext(Rml::Context* rmlContext) noexcept;

        void setDpr(float dpr);
        void onDprChanged(float dpr) { setDpr(dpr); }

        void updateCapsule(
            float top,
            float right,
            float width,
            float height,
            float screenWidth);

        void updateSafeArea(
            float top,
            float bottom,
            float left,
            float right);

        void onWindowResize(int width, int height);
        void reset();
        void resetCapsule() { reset(); }

        void applyToDocument(Rml::ElementDocument* document) const;

        [[nodiscard]] float getDpr() const noexcept { return m_dpr; }
        [[nodiscard]] float getAvoidTop() const noexcept { return m_avoidTop; }
        [[nodiscard]] float getAvoidRight() const noexcept { return m_avoidRight; }
        [[nodiscard]] int getWindowWidth() const noexcept { return m_windowWidth; }
        [[nodiscard]] int getWindowHeight() const noexcept { return m_windowHeight; }
        [[nodiscard]] uint32_t getResizeCount() const noexcept { return m_resizeCount; }
        [[nodiscard]] bool isCssDirty() const noexcept { return m_cssDirty; }
        void clearCssDirtyFlag() noexcept { m_cssDirty = false; }

        [[nodiscard]] std::string getCssVariable(std::string_view name) const;
        [[nodiscard]] const std::unordered_map<std::string, std::string>& getAllCssVariables() const noexcept
        {
            return m_cssVariables;
        }

    private:
        void recalculateAndInjectAll();
        void injectIntoContextDocuments() const;

    private:
        Rml::Context* m_rmlContext = nullptr;

        float m_dpr = 1.0f;
        float m_capsuleTop = 0.0f;
        float m_capsuleRight = 0.0f;
        float m_capsuleWidth = 0.0f;
        float m_capsuleHeight = 0.0f;
        float m_screenWidth = 0.0f;

        float m_safeTop = 0.0f;
        float m_safeBottom = 0.0f;
        float m_safeLeft = 0.0f;
        float m_safeRight = 0.0f;

        float m_avoidTop = 0.0f;
        float m_avoidRight = 0.0f;

        int m_windowWidth = 0;
        int m_windowHeight = 0;
        uint32_t m_resizeCount = 0;
        bool m_hasCapsuleData = false;
        bool m_cssDirty = false;

        std::unordered_map<std::string, std::string> m_cssVariables;
    };

    /**
     * @brief RmlUi 表单控件原生软键盘桥接器：监听 focus/blur 并联动 FCT::SAI::VirtualKeyboard (FEAT-53)
     */
    class RML_UI_COMPOSITE_API RmlUiKeyboardBridge : public Rml::EventListener
    {
    public:
        explicit RmlUiKeyboardBridge(Rml::ElementDocument* activeDocument = nullptr);
        ~RmlUiKeyboardBridge() override;

        void attachToContext(Rml::Context* rmlContext);
        void detachFromContext();
        void bindActiveDocument(Rml::ElementDocument* document) noexcept { m_activeDocument = document; }

        void ProcessEvent(Rml::Event& event) override;

        void onElementFocus(
            std::string_view elementId,
            std::string_view inputTypeOrTag,
            std::string_view defaultValue,
            int32_t maxLengthOverride = -1);

        void onElementBlur(std::string_view elementId);

        void setElementMaxLength(std::string_view elementId, int32_t maxLength);
        [[nodiscard]] std::string getElementValue(std::string_view elementId) const;
        [[nodiscard]] const std::string& getFocusedElementId() const noexcept { return m_focusedElementId; }
        [[nodiscard]] bool isKeyboardActive() const noexcept;

        static std::string truncateToMaxLength(std::string_view rawText, int32_t maxLength);

    private:
        void applyBackfillText(std::string_view text, bool isConfirm);
        static bool isEditableTextControl(Rml::Element* element, std::string& outTypeOrTag);
        static int32_t extractMaxLengthFromElement(Rml::Element* element);
        static std::string extractCurrentValueFromElement(Rml::Element* element);

    private:
        Rml::Context* m_attachedContext = nullptr;
        Rml::ElementDocument* m_activeDocument = nullptr;
        Rml::Element* m_focusedElement = nullptr;

        std::string m_focusedElementId;
        int32_t m_activeMaxLength = 140;
        bool m_activeMultiline = false;

        std::unordered_map<std::string, int32_t> m_configuredMaxLengths;
        std::unordered_map<std::string, std::string> m_elementValues;
    };

    /**
     * @brief 首屏 50ms 秒开 Splash 接力协调器：首帧 RmlUiPass 渲染落盘后幂等通知移除宿主封面 (FEAT-54)
     */
    class RML_UI_COMPOSITE_API SplashRelayCoordinator
    {
    public:
        using DismissSplashCallback = std::function<void()>;

        SplashRelayCoordinator() = default;
        explicit SplashRelayCoordinator(DismissSplashCallback onDismiss);
        ~SplashRelayCoordinator() = default;

        void setDismissCallback(DismissSplashCallback callback) { m_onDismiss = std::move(callback); }
        void notifyFirstFrameRendered();
        void reset() noexcept;

        [[nodiscard]] bool isSplashDismissed() const noexcept { return m_splashDismissed; }
        [[nodiscard]] uint32_t getDismissCount() const noexcept { return m_dismissCount; }

    private:
        DismissSplashCallback m_onDismiss;
        bool m_splashDismissed = false;
        uint32_t m_dismissCount = 0;
    };

    /**
     * @brief 抖音开放数据域 SharedCanvas 排行榜按需刷新控制器 (FEAT-54)
     *        弹窗隐藏时强制 0 FPS 零开销休眠，可见或滚动时调用 Image::updateSubRegion 局部刷新。
     */
    class RML_UI_COMPOSITE_API OpenDataLeaderboardController
    {
    public:
        using SubRegionUploadCallback = std::function<void(uint32_t x, uint32_t y, uint32_t width, uint32_t height)>;

        explicit OpenDataLeaderboardController(
            FCT::Context* rhiContext = nullptr,
            VirtualTextureRouter* textureRouter = nullptr);
        ~OpenDataLeaderboardController() = default;

        void bindRhiContext(FCT::Context* rhiContext) noexcept { m_rhiContext = rhiContext; }
        void bindTextureRouter(VirtualTextureRouter* router) noexcept { m_textureRouter = router; }
        void setSubRegionUploadObserver(SubRegionUploadCallback observer) { m_uploadObserver = std::move(observer); }

        Rml::TextureHandle initializeSharedCanvas(uint32_t width, uint32_t height);
        Rml::TextureHandle initSharedCanvas(uint32_t width, uint32_t height)
        {
            return initializeSharedCanvas(width, height);
        }

        void setVisible(bool visible) noexcept { m_visible = visible; }
        void setLeaderboardVisible(bool visible) noexcept { m_visible = visible; }
        void syncVisibilityFromDocument(Rml::ElementDocument* document);

        [[nodiscard]] bool isVisible() const noexcept { return m_visible; }
        [[nodiscard]] bool isLeaderboardVisible() const noexcept { return m_visible; }
        [[nodiscard]] uint32_t getTargetRefreshRateFps() const noexcept { return m_visible ? 60u : 0u; }

        void onFrameUpdate();
        void onScroll(float deltaX, float deltaY);

        [[nodiscard]] uint32_t getCanvasWidth() const noexcept { return m_canvasWidth; }
        [[nodiscard]] uint32_t getSharedCanvasWidth() const noexcept { return m_canvasWidth; }
        [[nodiscard]] uint32_t getCanvasHeight() const noexcept { return m_canvasHeight; }
        [[nodiscard]] uint32_t getSharedCanvasHeight() const noexcept { return m_canvasHeight; }
        [[nodiscard]] Rml::TextureHandle getSharedCanvasTextureHandle() const noexcept { return m_textureHandle; }
        [[nodiscard]] FCT::Image* getSharedCanvasImage() const noexcept { return m_sharedCanvasImage.get(); }
        [[nodiscard]] FCT::Vec2 getScrollOffset() const noexcept { return m_scrollOffset; }
        [[nodiscard]] uint32_t getRefreshCount() const noexcept { return m_refreshCount; }

    private:
        void performSubRegionUpload();
        static bool detectSharedCanvasVisibleRecursive(Rml::Element* element);

    private:
        FCT::Context* m_rhiContext = nullptr;
        VirtualTextureRouter* m_textureRouter = nullptr;
        Ptr<FCT::Image> m_sharedCanvasImage;
        SubRegionUploadCallback m_uploadObserver;

        std::vector<uint8_t> m_stagingRgbaBuffer;
        uint32_t m_canvasWidth = 0;
        uint32_t m_canvasHeight = 0;
        Rml::TextureHandle m_textureHandle = 0;
        FCT::Vec2 m_scrollOffset{0.0f, 0.0f};

        bool m_visible = false;
        uint32_t m_refreshCount = 0;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_RML_MINIGAME_ADAPTERS_H
