/**
 * @file RmlUiTouchInput.h
 * @brief 声明式 RmlUi 虚拟触控控件解析、多指状态机与 Enhanced Input 直注控制器 (FEAT-51)
 */
#ifndef MQENGINE_GAME_HTML_UI_RML_TOUCH_INPUT_H
#define MQENGINE_GAME_HTML_UI_RML_TOUCH_INPUT_H

#include "../../GameHtmlUiApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
}

namespace MQEngine::Domain::HID
{
    class HIDSubsystem;
}

namespace MQEngine::GameHtmlUI
{
    /**
     * @brief 声明式虚拟触控控件语义类型
     */
    enum class VirtualControlType : uint8_t
    {
        Button   = 0,
        Joystick = 1,
        AimPad   = 2
    };

    /**
     * @brief 虚拟控件屏幕空间碰撞框
     */
    struct VirtualControlRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        [[nodiscard]] bool contains(float px, float py) const noexcept
        {
            return width > 0.0f && height > 0.0f &&
                   px >= x && px <= (x + width) &&
                   py >= y && py <= (y + height);
        }
    };

    /**
     * @brief 声明式虚拟触控直注控制器 (FEAT-51)
     *        消化 UI 内部触控事件并向 HIDSubsystem::injectInputForAction 注入语义值，阻断底层场景误触。
     */
    class RML_UI_COMPOSITE_API VirtualTouchController
    {
    public:
        explicit VirtualTouchController(Domain::HID::HIDSubsystem* hidSubsystem = nullptr);
        ~VirtualTouchController() = default;

        void bindHidSubsystem(Domain::HID::HIDSubsystem* hidSubsystem) noexcept { m_hidSubsystem = hidSubsystem; }
        void bindRmlContext(Rml::Context* rmlContext) noexcept { m_rmlContext = rmlContext; }

        // ==================== DOM 声明式扫描与显式绑定 ====================

        void syncFromDocument(Rml::ElementDocument* document);
        void clearBindings() noexcept;

        void bindControl(
            std::string_view controlId,
            std::string_view actionName,
            VirtualControlType type,
            const VirtualControlRect& rect,
            float deadzone = 0.0f);

        void bindButton(
            std::string_view actionName,
            float x,
            float y,
            float width,
            float height);

        void bindJoystick(
            std::string_view actionName,
            float centerX,
            float centerY,
            float radius);

        // ==================== 多点触控事件入口 ====================

        bool onTouchDown(int pointerId, float x, float y);
        bool onTouchMove(int pointerId, float x, float y);
        bool onTouchUp(int pointerId, float x = 0.0f, float y = 0.0f);
        void onTouchCancel(int pointerId);

        // ==================== 状态查询接口 ====================

        [[nodiscard]] size_t getActivePointerCount() const noexcept { return m_activePointers.size(); }
        [[nodiscard]] size_t getConsumedPointerCount() const noexcept;
        [[nodiscard]] bool isPointerConsumedByUi(int pointerId) const noexcept;
        [[nodiscard]] bool getButtonState(std::string_view actionName) const;
        [[nodiscard]] std::pair<float, float> getJoystickDeflection(std::string_view actionName) const;

    private:
        struct ControlBinding
        {
            std::string controlId;
            std::string actionName;
            VirtualControlType type = VirtualControlType::Button;
            VirtualControlRect rect{};
            float centerX = 0.0f;
            float centerY = 0.0f;
            float radius = 50.0f;
            float deadzone = 0.0f;
            bool isPressed = false;
            float deflectionX = 0.0f;
            float deflectionY = 0.0f;
        };

        struct ActivePointerState
        {
            int pointerId = 0;
            float startX = 0.0f;
            float startY = 0.0f;
            float currentX = 0.0f;
            float currentY = 0.0f;
            bool consumedByUi = false;
            std::string boundControlId;
        };

    private:
        Domain::HID::HIDSubsystem* resolveHidSubsystem() const noexcept;
        void ensureHidActionRegistered(const ControlBinding& binding);
        void scanDomElementRecursive(Rml::Element* element);
        bool checkRmlContextInteractiveHit(float x, float y) const;

        void applyControlTouchDown(ControlBinding& binding, float x, float y);
        void applyControlTouchMove(ControlBinding& binding, ActivePointerState& ptrState, float x, float y);
        void applyControlTouchUp(ControlBinding& binding, int releasingPointerId);

        void forwardUnconsumedSceneGesture(bool touchEnded);

    private:
        Domain::HID::HIDSubsystem* m_hidSubsystem = nullptr;
        Rml::Context* m_rmlContext = nullptr;

        std::vector<std::string> m_controlOrder;
        std::unordered_map<std::string, ControlBinding> m_controls;
        std::unordered_map<int, ActivePointerState> m_activePointers;
        float m_initialPinchDistance = 0.0f;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_RML_TOUCH_INPUT_H
