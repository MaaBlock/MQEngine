/**
 * @file RmlUiMiniGameAdapters.cpp
 * @brief 小游戏宿主适配组件实现：异形屏胶囊 CSS 注入 (FEAT-52)、软键盘联动 (FEAT-53) 与首屏秒开/共享画布 (FEAT-54)
 */
#include "RmlUiMiniGameAdapters.h"

#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetPreviousSibling
#undef GetPreviousSibling
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace MQEngine::GameHtmlUI
{
    namespace
    {
        constexpr Rml::TextureHandle kHeadlessSharedCanvasHandle = 2001u;

        bool parseNonThrowingInt32(std::string_view text, int32_t& outVal) noexcept
        {
            if (text.empty())
            {
                return false;
            }
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), outVal);
            return ec == std::errc{} && ptr == text.data() + text.size();
        }

        std::string formatPx(float value)
        {
            const int rounded = static_cast<int>(std::lround(std::max(0.0f, value)));
            return StrCat(rounded, "px");
        }

        std::string formatOneDecimal(float value)
        {
            char buf[32]{};
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
            return std::string(buf);
        }
    } // namespace

    // ==============================================================================
    // CapsuleCssVariableInjector 实现 (FEAT-52)
    // ==============================================================================

    CapsuleCssVariableInjector::CapsuleCssVariableInjector(Rml::Context* rmlContext)
        : m_rmlContext(rmlContext)
    {
        reset();
    }

    void CapsuleCssVariableInjector::registerCustomPropertiesOnce()
    {
        if (Rml::StyleSheetSpecification::GetProperty("--mq-capsule-avoid-top") != nullptr)
        {
            return;
        }

        const char* lengthProps[] = {
            "--mq-capsule-avoid-top",
            "--mq-capsule-avoid-right",
            "--mq-capsule-avoid-width",
            "--mq-capsule-avoid-height",
            "--mq-safe-top",
            "--mq-safe-bottom",
            "--mq-safe-left",
            "--mq-safe-right",
            "--mq-safe-area-top",
            "--mq-safe-area-bottom",
            "--mq-safe-area-left",
            "--mq-safe-area-right",
            "--mq-window-width",
            "--mq-window-height"
        };

        for (const char* propName : lengthProps)
        {
            if (!Rml::StyleSheetSpecification::GetProperty(propName))
            {
                Rml::StyleSheetSpecification::RegisterProperty(propName, "0px", true, false)
                    .AddParser("length")
                    .AddParser("number")
                    .AddParser("keyword");
            }
        }

        if (!Rml::StyleSheetSpecification::GetProperty("--mq-device-pixel-ratio"))
        {
            Rml::StyleSheetSpecification::RegisterProperty("--mq-device-pixel-ratio", "1.0", true, false)
                .AddParser("number")
                .AddParser("keyword");
        }
    }

    void CapsuleCssVariableInjector::bindRmlContext(Rml::Context* rmlContext) noexcept
    {
        m_rmlContext = rmlContext;
        if (m_rmlContext)
        {
            m_rmlContext->SetDensityIndependentPixelRatio(m_dpr);
            injectIntoContextDocuments();
        }
    }

    void CapsuleCssVariableInjector::setDpr(float dpr)
    {
        m_dpr = std::clamp(dpr, 0.1f, 4.0f);
        if (m_rmlContext)
        {
            m_rmlContext->SetDensityIndependentPixelRatio(m_dpr);
        }
        recalculateAndInjectAll();
    }

    void CapsuleCssVariableInjector::updateCapsule(
        float top,
        float right,
        float width,
        float height,
        float screenWidth)
    {
        m_capsuleTop = top;
        m_capsuleRight = right;
        m_capsuleWidth = std::max(0.0f, width);
        m_capsuleHeight = std::max(0.0f, height);
        m_screenWidth = std::max(0.0f, screenWidth);
        m_hasCapsuleData = (m_capsuleWidth > 0.0f || m_capsuleRight > 0.0f || m_capsuleTop > 0.0f);
        recalculateAndInjectAll();
    }

    void CapsuleCssVariableInjector::updateSafeArea(
        float top,
        float bottom,
        float left,
        float right)
    {
        m_safeTop = std::max(0.0f, top);
        m_safeBottom = std::max(0.0f, bottom);
        m_safeLeft = std::max(0.0f, left);
        m_safeRight = std::max(0.0f, right);
        recalculateAndInjectAll();
    }

    void CapsuleCssVariableInjector::onWindowResize(int width, int height)
    {
        m_windowWidth = std::max(0, width);
        m_windowHeight = std::max(0, height);
        m_screenWidth = static_cast<float>(m_windowWidth);
        ++m_resizeCount;

        if (m_rmlContext && m_windowWidth > 0 && m_windowHeight > 0)
        {
            m_rmlContext->SetDimensions(Rml::Vector2i(m_windowWidth, m_windowHeight));
        }

        recalculateAndInjectAll();
    }

    void CapsuleCssVariableInjector::reset()
    {
        m_capsuleTop = 0.0f;
        m_capsuleRight = 0.0f;
        m_capsuleWidth = 0.0f;
        m_capsuleHeight = 0.0f;
        m_avoidTop = 0.0f;
        m_avoidRight = 0.0f;
        m_hasCapsuleData = false;
        recalculateAndInjectAll();
    }

    void CapsuleCssVariableInjector::recalculateAndInjectAll()
    {
        if (m_hasCapsuleData)
        {
            m_avoidTop = std::max(0.0f, m_capsuleTop);
            const float capsuleLeftEdge = m_capsuleRight - m_capsuleWidth;
            m_avoidRight = std::max(0.0f, m_screenWidth - capsuleLeftEdge);
        }
        else
        {
            m_avoidTop = 0.0f;
            m_avoidRight = 0.0f;
        }

        m_cssVariables["--mq-capsule-avoid-top"] = formatPx(m_avoidTop);
        m_cssVariables["--mq-capsule-avoid-right"] = formatPx(m_avoidRight);
        m_cssVariables["--mq-capsule-avoid-width"] = formatPx(m_capsuleWidth);
        m_cssVariables["--mq-capsule-avoid-height"] = formatPx(m_capsuleHeight);

        const std::string safeTopStr = formatPx(m_safeTop);
        const std::string safeBottomStr = formatPx(m_safeBottom);
        const std::string safeLeftStr = formatPx(m_safeLeft);
        const std::string safeRightStr = formatPx(m_safeRight);

        m_cssVariables["--mq-safe-top"] = safeTopStr;
        m_cssVariables["--mq-safe-bottom"] = safeBottomStr;
        m_cssVariables["--mq-safe-left"] = safeLeftStr;
        m_cssVariables["--mq-safe-right"] = safeRightStr;

        m_cssVariables["--mq-safe-area-top"] = safeTopStr;
        m_cssVariables["--mq-safe-area-bottom"] = safeBottomStr;
        m_cssVariables["--mq-safe-area-left"] = safeLeftStr;
        m_cssVariables["--mq-safe-area-right"] = safeRightStr;

        m_cssVariables["--mq-window-width"] = StrCat(m_windowWidth, "px");
        m_cssVariables["--mq-window-height"] = StrCat(m_windowHeight, "px");
        m_cssVariables["--mq-device-pixel-ratio"] = formatOneDecimal(m_dpr);

        m_cssDirty = true;
        injectIntoContextDocuments();
    }

    void CapsuleCssVariableInjector::applyToDocument(Rml::ElementDocument* document) const
    {
        if (!document)
        {
            return;
        }

        for (const auto& [propName, propVal] : m_cssVariables)
        {
            (void)document->SetProperty(propName.c_str(), propVal.c_str());
            if (propName.starts_with("--"))
            {
                const std::string dataAttrName = StrCat("data-", propName.substr(2));
                document->SetAttribute(dataAttrName.c_str(), propVal.c_str());
            }
        }
    }

    void CapsuleCssVariableInjector::injectIntoContextDocuments() const
    {
        if (!m_rmlContext)
        {
            return;
        }

        if (Rml::Element* root = m_rmlContext->GetRootElement())
        {
            for (const auto& [propName, propVal] : m_cssVariables)
            {
                (void)root->SetProperty(propName.c_str(), propVal.c_str());
            }
        }

        const int numDocs = m_rmlContext->GetNumDocuments();
        for (int i = 0; i < numDocs; ++i)
        {
            applyToDocument(m_rmlContext->GetDocument(i));
        }
    }

    std::string CapsuleCssVariableInjector::getCssVariable(std::string_view name) const
    {
        auto it = m_cssVariables.find(std::string(name));
        return (it != m_cssVariables.end()) ? it->second : std::string{};
    }

    // ==============================================================================
    // RmlUiKeyboardBridge 实现 (FEAT-53)
    // ==============================================================================

    RmlUiKeyboardBridge::RmlUiKeyboardBridge(Rml::ElementDocument* activeDocument)
        : m_activeDocument(activeDocument)
    {
    }

    RmlUiKeyboardBridge::~RmlUiKeyboardBridge()
    {
        detachFromContext();
    }

    void RmlUiKeyboardBridge::attachToContext(Rml::Context* rmlContext)
    {
        if (m_attachedContext == rmlContext)
        {
            return;
        }
        detachFromContext();
        m_attachedContext = rmlContext;

        if (m_attachedContext && m_attachedContext->GetRootElement())
        {
            auto* root = m_attachedContext->GetRootElement();
            root->AddEventListener(Rml::EventId::Focus, this, true);
            root->AddEventListener(Rml::EventId::Blur, this, true);
        }
    }

    void RmlUiKeyboardBridge::detachFromContext()
    {
        if (m_attachedContext && m_attachedContext->GetRootElement())
        {
            auto* root = m_attachedContext->GetRootElement();
            root->RemoveEventListener(Rml::EventId::Focus, this, true);
            root->RemoveEventListener(Rml::EventId::Blur, this, true);
        }
        m_attachedContext = nullptr;
        m_focusedElement = nullptr;
        m_focusedElementId.clear();
    }

    void RmlUiKeyboardBridge::ProcessEvent(Rml::Event& event)
    {
        Rml::Element* target = event.GetTargetElement();
        if (!target)
        {
            return;
        }

        std::string typeOrTag;
        if (!isEditableTextControl(target, typeOrTag))
        {
            return;
        }

        std::string elemId = target->GetId();
        if (elemId.empty())
        {
            elemId = "__anonymous_input__";
        }

        if (event.GetId() == Rml::EventId::Focus)
        {
            m_focusedElement = target;
            const std::string curValue = extractCurrentValueFromElement(target);
            const int32_t maxLen = extractMaxLengthFromElement(target);
            onElementFocus(elemId, typeOrTag, curValue, maxLen);
        }
        else if (event.GetId() == Rml::EventId::Blur)
        {
            if (m_focusedElement == target || m_focusedElementId == elemId)
            {
                m_focusedElement = nullptr;
                onElementBlur(elemId);
            }
        }
    }

    void RmlUiKeyboardBridge::onElementFocus(
        std::string_view elementId,
        std::string_view inputTypeOrTag,
        std::string_view defaultValue,
        int32_t maxLengthOverride)
    {
        m_focusedElementId = std::string(elementId);
        m_activeMultiline = (inputTypeOrTag == "textarea");

        int32_t resolvedMaxLen = 140;
        if (maxLengthOverride >= 0)
        {
            resolvedMaxLen = maxLengthOverride;
        }
        else
        {
            auto cfgIt = m_configuredMaxLengths.find(m_focusedElementId);
            if (cfgIt != m_configuredMaxLengths.end())
            {
                resolvedMaxLen = cfgIt->second;
            }
        }
        m_activeMaxLength = resolvedMaxLen;

        const std::string clampedDefault = truncateToMaxLength(defaultValue, m_activeMaxLength);
        m_elementValues[m_focusedElementId] = clampedDefault;

        FCT::SAI::VirtualKeyboardConfig cfg;
        cfg.defaultValue = clampedDefault;
        cfg.maxLength = m_activeMaxLength;
        cfg.multiple = m_activeMultiline;
        cfg.confirmHold = false;
        cfg.confirmType = (inputTypeOrTag == "search")
            ? FCT::SAI::KeyboardConfirmType::Search
            : (m_activeMultiline ? FCT::SAI::KeyboardConfirmType::Send : FCT::SAI::KeyboardConfirmType::Done);

        FCT::SAI::VirtualKeyboard::setOnInput([this](std::string_view text) {
            applyBackfillText(text, false);
        });

        FCT::SAI::VirtualKeyboard::setOnConfirm([this](std::string_view text) {
            applyBackfillText(text, true);
        });

        (void)FCT::SAI::VirtualKeyboard::show(cfg);
    }

    void RmlUiKeyboardBridge::onElementBlur(std::string_view elementId)
    {
        if (!m_focusedElementId.empty() && (elementId.empty() || m_focusedElementId == elementId))
        {
            m_focusedElementId.clear();
            m_focusedElement = nullptr;
            (void)FCT::SAI::VirtualKeyboard::hide();
        }
    }

    void RmlUiKeyboardBridge::setElementMaxLength(std::string_view elementId, int32_t maxLength)
    {
        if (!elementId.empty())
        {
            m_configuredMaxLengths[std::string(elementId)] = maxLength;
        }
    }

    std::string RmlUiKeyboardBridge::getElementValue(std::string_view elementId) const
    {
        const std::string idKey(elementId);
        if (m_activeDocument)
        {
            if (Rml::Element* elem = m_activeDocument->GetElementById(idKey.c_str()))
            {
                return extractCurrentValueFromElement(elem);
            }
        }
        auto it = m_elementValues.find(idKey);
        return (it != m_elementValues.end()) ? it->second : std::string{};
    }

    bool RmlUiKeyboardBridge::isKeyboardActive() const noexcept
    {
        return FCT::SAI::VirtualKeyboard::isShowing();
    }

    std::string RmlUiKeyboardBridge::truncateToMaxLength(std::string_view rawText, int32_t maxLength)
    {
        if (maxLength < 0 || rawText.empty())
        {
            return std::string(rawText);
        }
        if (maxLength == 0)
        {
            return std::string{};
        }

        const auto* bytes = reinterpret_cast<const uint8_t*>(rawText.data());
        const size_t len = rawText.size();
        size_t bytePos = 0;
        int32_t codePointCount = 0;

        while (bytePos < len && codePointCount < maxLength)
        {
            const uint8_t lead = bytes[bytePos];
            size_t seqLen = 1;
            if ((lead & 0x80u) == 0u)
            {
                seqLen = 1;
            }
            else if ((lead & 0xE0u) == 0xC0u)
            {
                seqLen = 2;
            }
            else if ((lead & 0xF0u) == 0xE0u)
            {
                seqLen = 3;
            }
            else if ((lead & 0xF8u) == 0xF0u)
            {
                seqLen = 4;
            }
            if (bytePos + seqLen > len)
            {
                break;
            }
            bytePos += seqLen;
            ++codePointCount;
        }
        return std::string(rawText.substr(0, bytePos));
    }

    void RmlUiKeyboardBridge::applyBackfillText(std::string_view text, bool isConfirm)
    {
        if (m_focusedElementId.empty())
        {
            return;
        }

        const std::string sanitized = truncateToMaxLength(text, m_activeMaxLength);
        m_elementValues[m_focusedElementId] = sanitized;

        Rml::Element* targetElem = m_focusedElement;
        if (!targetElem && m_activeDocument)
        {
            targetElem = m_activeDocument->GetElementById(m_focusedElementId.c_str());
        }

        if (targetElem)
        {
            if (auto* formCtrl = rmlui_dynamic_cast<Rml::ElementFormControl*>(targetElem))
            {
                formCtrl->SetValue(Rml::String(sanitized.c_str(), sanitized.size()));
            }
            else
            {
                targetElem->SetAttribute("value", sanitized.c_str());
                targetElem->SetInnerRML(sanitized.c_str());
            }
        }

        if (isConfirm)
        {
            m_focusedElementId.clear();
            m_focusedElement = nullptr;
        }
    }

    bool RmlUiKeyboardBridge::isEditableTextControl(Rml::Element* element, std::string& outTypeOrTag)
    {
        if (!element)
        {
            return false;
        }

        const Rml::String& tag = element->GetTagName();
        if (tag == "textarea")
        {
            outTypeOrTag = "textarea";
            return true;
        }
        if (tag == "input")
        {
            std::string typeVal = "text";
            if (const Rml::Variant* typeAttr = element->GetAttribute("type"))
            {
                const Rml::String s = typeAttr->Get<Rml::String>();
                if (!s.empty())
                {
                    typeVal.assign(s.c_str(), s.size());
                }
            }
            if (typeVal == "text" || typeVal == "password" || typeVal == "search" ||
                typeVal == "email" || typeVal == "number" || typeVal == "tel")
            {
                outTypeOrTag = typeVal;
                return true;
            }
        }
        return false;
    }

    int32_t RmlUiKeyboardBridge::extractMaxLengthFromElement(Rml::Element* element)
    {
        if (!element)
        {
            return 140;
        }
        if (const Rml::Variant* maxAttr = element->GetAttribute("maxlength"))
        {
            const Rml::String s = maxAttr->Get<Rml::String>();
            int32_t parsed = 140;
            if (parseNonThrowingInt32(std::string_view(s.c_str(), s.size()), parsed) && parsed > 0)
            {
                return parsed;
            }
        }
        return 140;
    }

    std::string RmlUiKeyboardBridge::extractCurrentValueFromElement(Rml::Element* element)
    {
        if (!element)
        {
            return std::string{};
        }
        if (const auto* formCtrl = rmlui_dynamic_cast<const Rml::ElementFormControl*>(element))
        {
            const Rml::String v = formCtrl->GetValue();
            return std::string(v.c_str(), v.size());
        }
        if (const Rml::Variant* valAttr = element->GetAttribute("value"))
        {
            const Rml::String v = valAttr->Get<Rml::String>();
            return std::string(v.c_str(), v.size());
        }
        const Rml::String inner = element->GetInnerRML();
        return std::string(inner.c_str(), inner.size());
    }

    // ==============================================================================
    // SplashRelayCoordinator 实现 (FEAT-54)
    // ==============================================================================

    SplashRelayCoordinator::SplashRelayCoordinator(DismissSplashCallback onDismiss)
        : m_onDismiss(std::move(onDismiss))
    {
    }

    void SplashRelayCoordinator::notifyFirstFrameRendered()
    {
        if (m_splashDismissed)
        {
            return;
        }
        m_splashDismissed = true;
        ++m_dismissCount;
        if (m_onDismiss)
        {
            m_onDismiss();
        }
    }

    void SplashRelayCoordinator::reset() noexcept
    {
        m_splashDismissed = false;
        m_dismissCount = 0;
    }

    // ==============================================================================
    // OpenDataLeaderboardController 实现 (FEAT-54)
    // ==============================================================================

    OpenDataLeaderboardController::OpenDataLeaderboardController(
        FCT::Context* rhiContext,
        VirtualTextureRouter* textureRouter)
        : m_rhiContext(rhiContext),
          m_textureRouter(textureRouter)
    {
    }

    Rml::TextureHandle OpenDataLeaderboardController::initializeSharedCanvas(
        uint32_t width,
        uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return 0;
        }

        m_canvasWidth = width;
        m_canvasHeight = height;
        m_stagingRgbaBuffer.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);

        if (m_rhiContext && m_sharedCanvasImage)
        {
            FCT::Image* oldImg = m_sharedCanvasImage.get();
            oldImg->addRef();
            m_sharedCanvasImage.reset();

            auto* renderHost = Host::RenderHost::InstancePtr();
            auto* gpuMem = Resource::GpuMemoryManager::InstancePtr();
            if (renderHost && gpuMem && renderHost->getContext() == m_rhiContext)
            {
                const uint64_t retiredFrame = renderHost->getRenderFrameCount() + 2u;
                gpuMem->deferRelease(
                    oldImg,
                    [](void* ptr) {
                        if (auto* img = static_cast<FCT::Image*>(ptr))
                        {
                            img->release();
                        }
                    },
                    retiredFrame);
            }
            else
            {
                m_rhiContext->deferRelease(oldImg);
            }
        }

        if (m_rhiContext)
        {
            auto* rawImg = FCT_NEW(FCT::SingleBufferImage, m_rhiContext);
            if (rawImg)
            {
                rawImg->width(static_cast<int>(width));
                rawImg->height(static_cast<int>(height));
                rawImg->arrayLayers(1);
                rawImg->format(FCT::Format::R8G8B8A8_UNORM);
                rawImg->as(FCT::ImageUsage::Texture);
                rawImg->create();
                m_sharedCanvasImage = Ptr<FCT::Image>(rawImg);
                rawImg->release();
                m_textureHandle = reinterpret_cast<Rml::TextureHandle>(rawImg);
            }
        }

        if (m_textureHandle == 0)
        {
            m_textureHandle = kHeadlessSharedCanvasHandle;
        }

        if (m_textureRouter)
        {
            m_textureRouter->registerSharedCanvas(
                "rank",
                m_textureHandle,
                Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)),
                m_sharedCanvasImage.get());
        }

        return m_textureHandle;
    }

    void OpenDataLeaderboardController::syncVisibilityFromDocument(Rml::ElementDocument* document)
    {
        if (!document || !document->IsVisible())
        {
            m_visible = false;
            return;
        }
        m_visible = detectSharedCanvasVisibleRecursive(document);
    }

    bool OpenDataLeaderboardController::detectSharedCanvasVisibleRecursive(Rml::Element* element)
    {
        if (!element || !element->IsVisible())
        {
            return false;
        }

        if (element->GetTagName() == "img")
        {
            if (const Rml::Variant* srcAttr = element->GetAttribute("src"))
            {
                const Rml::String srcStr = srcAttr->Get<Rml::String>();
                if (std::string_view(srcStr.c_str(), srcStr.size()).starts_with("@mq_shared_canvas/"))
                {
                    return true;
                }
            }
        }

        const int childCount = element->GetNumChildren();
        for (int i = 0; i < childCount; ++i)
        {
            if (detectSharedCanvasVisibleRecursive(element->GetChild(i)))
            {
                return true;
            }
        }
        return false;
    }

    void OpenDataLeaderboardController::onFrameUpdate()
    {
        if (!m_visible || m_canvasWidth == 0 || m_canvasHeight == 0)
        {
            return;
        }
        performSubRegionUpload();
    }

    void OpenDataLeaderboardController::onScroll(float deltaX, float deltaY)
    {
        m_scrollOffset.x += deltaX;
        m_scrollOffset.y += deltaY;

        if (!m_visible || m_canvasWidth == 0 || m_canvasHeight == 0)
        {
            return;
        }
        performSubRegionUpload();
    }

    void OpenDataLeaderboardController::performSubRegionUpload()
    {
        ++m_refreshCount;

        if (m_sharedCanvasImage && !m_stagingRgbaBuffer.empty())
        {
            m_sharedCanvasImage->updateSubRegion(
                0u,
                0u,
                m_canvasWidth,
                m_canvasHeight,
                0u,
                0u,
                m_stagingRgbaBuffer.data(),
                static_cast<size_t>(m_canvasWidth) * 4u);
        }

        if (m_uploadObserver)
        {
            m_uploadObserver(0u, 0u, m_canvasWidth, m_canvasHeight);
        }
    }
} // namespace MQEngine::GameHtmlUI
