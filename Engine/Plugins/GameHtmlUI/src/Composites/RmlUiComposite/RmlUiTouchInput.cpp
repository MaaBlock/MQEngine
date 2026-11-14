/**
 * @file RmlUiTouchInput.cpp
 * @brief 声明式 RmlUi 虚拟触控解析与 Enhanced Input 直注实现 (FEAT-51)
 */
#include "RmlUiTouchInput.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>

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

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace MQEngine::GameHtmlUI
{
    namespace
    {
        template <typename THid>
        void notifyHidTouchDown(THid& hid, int pointerId, float x, float y, bool consumedByUi)
        {
            if constexpr (requires { hid.onTouchDown(static_cast<uint32_t>(pointerId), x, y, consumedByUi); })
            {
                if (pointerId >= 0)
                {
                    hid.onTouchDown(static_cast<uint32_t>(pointerId), x, y, consumedByUi);
                }
            }
        }

        template <typename THid>
        void notifyHidTouchMove(THid& hid, int pointerId, float x, float y)
        {
            if constexpr (requires { hid.onTouchMove(static_cast<uint32_t>(pointerId), x, y); })
            {
                if (pointerId >= 0)
                {
                    hid.onTouchMove(static_cast<uint32_t>(pointerId), x, y);
                }
            }
        }

        template <typename THid>
        void notifyHidTouchUp(THid& hid, int pointerId, float x, float y)
        {
            if constexpr (requires { hid.onTouchUp(static_cast<uint32_t>(pointerId), x, y); })
            {
                if (pointerId >= 0)
                {
                    hid.onTouchUp(static_cast<uint32_t>(pointerId), x, y);
                }
            }
        }

        bool parseNonThrowingFloat(std::string_view text, float& outVal) noexcept
        {
            if (text.empty())
            {
                return false;
            }
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), outVal);
            return ec == std::errc{} && ptr == text.data() + text.size();
        }

        VirtualControlType parseControlTypeAttr(std::string_view attrVal) noexcept
        {
            if (attrVal == "joystick" || attrVal == "Joystick")
            {
                return VirtualControlType::Joystick;
            }
            if (attrVal == "aim-pad" || attrVal == "aimpad" || attrVal == "AimPad")
            {
                return VirtualControlType::AimPad;
            }
            return VirtualControlType::Button;
        }
    } // namespace

    VirtualTouchController::VirtualTouchController(Domain::HID::HIDSubsystem* hidSubsystem)
        : m_hidSubsystem(hidSubsystem)
    {
    }

    Domain::HID::HIDSubsystem* VirtualTouchController::resolveHidSubsystem() const noexcept
    {
        if (m_hidSubsystem)
        {
            return m_hidSubsystem;
        }
        if (auto* domain = Domain::Domain::InstancePtr())
        {
            return &domain->hid;
        }
        return nullptr;
    }

    void VirtualTouchController::ensureHidActionRegistered(const ControlBinding& binding)
    {
        auto* hid = resolveHidSubsystem();
        if (!hid || binding.actionName.empty())
        {
            return;
        }

        if (!hid->isActionValid(binding.actionName))
        {
            const auto valueType = (binding.type == VirtualControlType::Button)
                ? Domain::HID::EInputActionValueType::Digital
                : Domain::HID::EInputActionValueType::Axis2D;
            (void)hid->registerAction(
                binding.actionName,
                valueType,
                Domain::HID::EInputAccumulationPolicy::MaxMagnitude);
        }
    }

    void VirtualTouchController::clearBindings() noexcept
    {
        m_controls.clear();
        m_controlOrder.clear();
    }

    void VirtualTouchController::syncFromDocument(Rml::ElementDocument* document)
    {
        if (!document)
        {
            return;
        }
        scanDomElementRecursive(document);
    }

    void VirtualTouchController::scanDomElementRecursive(Rml::Element* element)
    {
        if (!element)
        {
            return;
        }

        if (const Rml::Variant* actionAttr = element->GetAttribute("data-input-action"))
        {
            const Rml::String actionStr = actionAttr->Get<Rml::String>();
            if (!actionStr.empty())
            {
                std::string controlId = element->GetId();
                if (controlId.empty())
                {
                    controlId = std::string(actionStr.c_str(), actionStr.size());
                }

                VirtualControlType ctrlType = VirtualControlType::Button;
                if (const Rml::Variant* ctrlAttr = element->GetAttribute("data-control"))
                {
                    const Rml::String ctrlStr = ctrlAttr->Get<Rml::String>();
                    ctrlType = parseControlTypeAttr(std::string_view(ctrlStr.c_str(), ctrlStr.size()));
                }

                float deadzone = 0.0f;
                if (const Rml::Variant* dzAttr = element->GetAttribute("data-deadzone"))
                {
                    const Rml::String dzStr = dzAttr->Get<Rml::String>();
                    (void)parseNonThrowingFloat(std::string_view(dzStr.c_str(), dzStr.size()), deadzone);
                }

                float w = element->GetBox().GetSize(Rml::BoxArea::Border).x;
                if (w <= 0.0f)
                {
                    w = static_cast<float>(element->GetClientWidth());
                }
                float h = element->GetBox().GetSize(Rml::BoxArea::Border).y;
                if (h <= 0.0f)
                {
                    h = static_cast<float>(element->GetClientHeight());
                }

                VirtualControlRect rect{
                    .x = element->GetAbsoluteLeft(),
                    .y = element->GetAbsoluteTop(),
                    .width = std::max(1.0f, w),
                    .height = std::max(1.0f, h)
                };

                bindControl(
                    controlId,
                    std::string_view(actionStr.c_str(), actionStr.size()),
                    ctrlType,
                    rect,
                    deadzone);
            }
        }

        const int childCount = element->GetNumChildren();
        for (int i = 0; i < childCount; ++i)
        {
            scanDomElementRecursive(element->GetChild(i));
        }
    }

    void VirtualTouchController::bindControl(
        std::string_view controlId,
        std::string_view actionName,
        VirtualControlType type,
        const VirtualControlRect& rect,
        float deadzone)
    {
        if (controlId.empty() || actionName.empty())
        {
            return;
        }

        const std::string idKey(controlId);
        ControlBinding binding;
        binding.controlId = idKey;
        binding.actionName = std::string(actionName);
        binding.type = type;
        binding.rect = rect;
        binding.centerX = rect.x + rect.width * 0.5f;
        binding.centerY = rect.y + rect.height * 0.5f;
        binding.radius = std::max(1.0f, std::min(rect.width, rect.height) * 0.5f);
        binding.deadzone = std::clamp(deadzone, 0.0f, 0.95f);

        ensureHidActionRegistered(binding);

        if (!m_controls.contains(idKey))
        {
            m_controlOrder.push_back(idKey);
        }
        m_controls[idKey] = std::move(binding);
    }

    void VirtualTouchController::bindButton(
        std::string_view actionName,
        float x,
        float y,
        float width,
        float height)
    {
        bindControl(actionName, actionName, VirtualControlType::Button, VirtualControlRect{x, y, width, height});
    }

    void VirtualTouchController::bindJoystick(
        std::string_view actionName,
        float centerX,
        float centerY,
        float radius)
    {
        const float safeRadius = std::max(1.0f, radius);
        VirtualControlRect rect{
            .x = centerX - safeRadius,
            .y = centerY - safeRadius,
            .width = safeRadius * 2.0f,
            .height = safeRadius * 2.0f
        };
        bindControl(actionName, actionName, VirtualControlType::Joystick, rect);
    }

    bool VirtualTouchController::checkRmlContextInteractiveHit(float x, float y) const
    {
        if (!m_rmlContext || x < 0.0f || y < 0.0f)
        {
            return false;
        }

        Rml::Element* hitElem = m_rmlContext->GetElementAtPoint(
            Rml::Vector2f(x, y));
        if (!hitElem)
        {
            return false;
        }

        const Rml::String& tag = hitElem->GetTagName();
        if (tag == "body" || tag == "rml" || tag == "#root")
        {
            return false;
        }
        return true;
    }

    bool VirtualTouchController::onTouchDown(int pointerId, float x, float y)
    {
        ActivePointerState ptrState;
        ptrState.pointerId = pointerId;
        ptrState.startX = x;
        ptrState.startY = y;
        ptrState.currentX = x;
        ptrState.currentY = y;

        // 逆序匹配顶层声明式虚拟控件
        if (x >= 0.0f && y >= 0.0f)
        {
            for (auto it = m_controlOrder.rbegin(); it != m_controlOrder.rend(); ++it)
            {
                auto ctrlIt = m_controls.find(*it);
                if (ctrlIt != m_controls.end() && ctrlIt->second.rect.contains(x, y))
                {
                    ptrState.consumedByUi = true;
                    ptrState.boundControlId = ctrlIt->first;
                    applyControlTouchDown(ctrlIt->second, x, y);
                    break;
                }
            }
        }

        if (!ptrState.consumedByUi && checkRmlContextInteractiveHit(x, y))
        {
            ptrState.consumedByUi = true;
            m_rmlContext->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
            m_rmlContext->ProcessMouseButtonDown(0, 0);
        }

        const bool consumed = ptrState.consumedByUi;
        m_activePointers[pointerId] = std::move(ptrState);

        if (auto* hid = resolveHidSubsystem())
        {
            notifyHidTouchDown(*hid, pointerId, x, y, consumed);
        }
        if (!consumed)
        {
            forwardUnconsumedSceneGesture(false);
        }

        return consumed;
    }

    bool VirtualTouchController::onTouchMove(int pointerId, float x, float y)
    {
        auto it = m_activePointers.find(pointerId);
        if (it == m_activePointers.end())
        {
            return false;
        }

        auto& ptrState = it->second;
        if (!ptrState.boundControlId.empty())
        {
            auto ctrlIt = m_controls.find(ptrState.boundControlId);
            if (ctrlIt != m_controls.end())
            {
                applyControlTouchMove(ctrlIt->second, ptrState, x, y);
            }
        }
        else if (ptrState.consumedByUi && m_rmlContext)
        {
            m_rmlContext->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
        }

        ptrState.currentX = x;
        ptrState.currentY = y;

        if (auto* hid = resolveHidSubsystem())
        {
            notifyHidTouchMove(*hid, pointerId, x, y);
        }
        if (!ptrState.consumedByUi)
        {
            forwardUnconsumedSceneGesture(false);
        }

        return ptrState.consumedByUi;
    }

    bool VirtualTouchController::onTouchUp(int pointerId, float x, float y)
    {
        auto it = m_activePointers.find(pointerId);
        if (it == m_activePointers.end())
        {
            return false;
        }

        const ActivePointerState releasedState = it->second;
        const float finalX = (x != 0.0f || y != 0.0f) ? x : releasedState.currentX;
        const float finalY = (x != 0.0f || y != 0.0f) ? y : releasedState.currentY;

        m_activePointers.erase(it);

        if (!releasedState.boundControlId.empty())
        {
            auto ctrlIt = m_controls.find(releasedState.boundControlId);
            if (ctrlIt != m_controls.end())
            {
                applyControlTouchUp(ctrlIt->second, pointerId);
            }
        }
        else if (releasedState.consumedByUi && m_rmlContext)
        {
            m_rmlContext->ProcessMouseMove(static_cast<int>(finalX), static_cast<int>(finalY), 0);
            m_rmlContext->ProcessMouseButtonUp(0, 0);
        }

        if (auto* hid = resolveHidSubsystem())
        {
            notifyHidTouchUp(*hid, pointerId, finalX, finalY);
            if (!releasedState.consumedByUi)
            {
                hid->processTouchSwipe(
                    releasedState.startX,
                    releasedState.startY,
                    finalX,
                    finalY,
                    true);
            }
        }

        if (!releasedState.consumedByUi)
        {
            m_initialPinchDistance = 0.0f;
        }

        return releasedState.consumedByUi;
    }

    void VirtualTouchController::onTouchCancel(int pointerId)
    {
        (void)onTouchUp(pointerId, 0.0f, 0.0f);
    }

    void VirtualTouchController::applyControlTouchDown(ControlBinding& binding, float x, float y)
    {
        binding.isPressed = true;
        auto* hid = resolveHidSubsystem();

        if (binding.type == VirtualControlType::Button)
        {
            if (hid)
            {
                (void)hid->injectInputForAction(binding.actionName, Domain::HID::InputValue(true));
            }
            return;
        }

        if (binding.type == VirtualControlType::Joystick)
        {
            float dx = (x - binding.centerX) / binding.radius;
            float dy = (y - binding.centerY) / binding.radius;
            const float len = std::hypot(dx, dy);
            if (len < binding.deadzone)
            {
                dx = 0.0f;
                dy = 0.0f;
            }
            else if (len > 1.0f)
            {
                dx /= len;
                dy /= len;
            }
            binding.deflectionX = dx;
            binding.deflectionY = dy;
            if (hid)
            {
                (void)hid->injectInputForAction(
                    binding.actionName,
                    Domain::HID::InputValue(FCT::Vec2(dx, dy)));
            }
        }
        else if (binding.type == VirtualControlType::AimPad)
        {
            binding.deflectionX = 0.0f;
            binding.deflectionY = 0.0f;
            if (hid)
            {
                (void)hid->injectInputForAction(
                    binding.actionName,
                    Domain::HID::InputValue(FCT::Vec2(0.0f, 0.0f)));
            }
        }
    }

    void VirtualTouchController::applyControlTouchMove(
        ControlBinding& binding,
        ActivePointerState& ptrState,
        float x,
        float y)
    {
        auto* hid = resolveHidSubsystem();
        if (binding.type == VirtualControlType::Joystick)
        {
            float dx = (x - binding.centerX) / binding.radius;
            float dy = (y - binding.centerY) / binding.radius;
            const float len = std::hypot(dx, dy);
            if (len < binding.deadzone)
            {
                dx = 0.0f;
                dy = 0.0f;
            }
            else if (len > 1.0f)
            {
                dx /= len;
                dy /= len;
            }
            binding.deflectionX = dx;
            binding.deflectionY = dy;
            if (hid)
            {
                (void)hid->injectInputForAction(
                    binding.actionName,
                    Domain::HID::InputValue(FCT::Vec2(dx, dy)));
            }
        }
        else if (binding.type == VirtualControlType::AimPad)
        {
            const float normScaleX = std::max(1.0f, binding.rect.width * 0.5f);
            const float normScaleY = std::max(1.0f, binding.rect.height * 0.5f);
            const float deltaX = std::clamp((x - ptrState.currentX) / normScaleX, -1.0f, 1.0f);
            const float deltaY = std::clamp((y - ptrState.currentY) / normScaleY, -1.0f, 1.0f);
            binding.deflectionX = deltaX;
            binding.deflectionY = deltaY;
            if (hid)
            {
                (void)hid->injectInputForAction(
                    binding.actionName,
                    Domain::HID::InputValue(FCT::Vec2(deltaX, deltaY)));
            }
        }
    }

    void VirtualTouchController::applyControlTouchUp(ControlBinding& binding, int releasingPointerId)
    {
        for (const auto& [otherId, otherState] : m_activePointers)
        {
            if (otherId != releasingPointerId && otherState.boundControlId == binding.controlId)
            {
                return;
            }
        }

        binding.isPressed = false;
        binding.deflectionX = 0.0f;
        binding.deflectionY = 0.0f;

        if (auto* hid = resolveHidSubsystem())
        {
            if (binding.type == VirtualControlType::Button)
            {
                (void)hid->injectInputForAction(binding.actionName, Domain::HID::InputValue(false));
            }
            else
            {
                (void)hid->injectInputForAction(
                    binding.actionName,
                    Domain::HID::InputValue(FCT::Vec2(0.0f, 0.0f)));
            }
        }
    }

    void VirtualTouchController::forwardUnconsumedSceneGesture(bool touchEnded)
    {
        auto* hid = resolveHidSubsystem();
        if (!hid)
        {
            return;
        }

        std::vector<const ActivePointerState*> idleFingers;
        idleFingers.reserve(m_activePointers.size());
        for (const auto& [_, ptr] : m_activePointers)
        {
            if (!ptr.consumedByUi)
            {
                idleFingers.push_back(&ptr);
            }
        }

        if (idleFingers.size() == 1)
        {
            const auto* p0 = idleFingers[0];
            hid->processTouchSwipe(p0->startX, p0->startY, p0->currentX, p0->currentY, touchEnded);
            m_initialPinchDistance = 0.0f;
        }
        else if (idleFingers.size() >= 2)
        {
            const auto* p0 = idleFingers[0];
            const auto* p1 = idleFingers[1];
            const float currDist = std::hypot(p1->currentX - p0->currentX, p1->currentY - p0->currentY);
            if (m_initialPinchDistance <= 0.001f)
            {
                m_initialPinchDistance = std::max(
                    1.0f,
                    std::hypot(p1->startX - p0->startX, p1->startY - p0->startY));
            }
            hid->processTouchPinch(m_initialPinchDistance, currDist, touchEnded);
        }
    }

    size_t VirtualTouchController::getConsumedPointerCount() const noexcept
    {
        size_t count = 0;
        for (const auto& [_, ptr] : m_activePointers)
        {
            if (ptr.consumedByUi)
            {
                ++count;
            }
        }
        return count;
    }

    bool VirtualTouchController::isPointerConsumedByUi(int pointerId) const noexcept
    {
        auto it = m_activePointers.find(pointerId);
        return (it != m_activePointers.end()) ? it->second.consumedByUi : false;
    }

    bool VirtualTouchController::getButtonState(std::string_view actionName) const
    {
        for (const auto& [_, ctrl] : m_controls)
        {
            if (ctrl.actionName == actionName)
            {
                return ctrl.isPressed;
            }
        }
        return false;
    }

    std::pair<float, float> VirtualTouchController::getJoystickDeflection(std::string_view actionName) const
    {
        for (const auto& [_, ctrl] : m_controls)
        {
            if (ctrl.actionName == actionName)
            {
                return {ctrl.deflectionX, ctrl.deflectionY};
            }
        }
        return {0.0f, 0.0f};
    }
} // namespace MQEngine::GameHtmlUI
