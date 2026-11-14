/**
 * @file HIDSubsystem.h
 * @brief 人机交互接口 (HID) 子域：消费 UIHost::drainEvents() 原始输入事件，
 *        驱动 Enhanced Input 优先级 IMC 栈、顺序修饰器、五态触发器与零泄漏触控手势源
 */
#ifndef MQENGINE_DOMAIN_HID_HIDSUBSYSTEM_H
#define MQENGINE_DOMAIN_HID_HIDSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/hid/InputAction.h>
#include <MQEngine/domain/hid/InputModifier.h>
#include <MQEngine/domain/hid/InputTrigger.h>
#include <MQEngine/domain/hid/InputMappingContext.h>
#include <MQEngine/host/ui/RawInputEvent.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace HID
        {
            enum class KeyCode : uint32_t
            {
                Unknown = 0,
                Space = 32,
                A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
                Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
                Escape = 256, Enter, Tab, Backspace, Insert, Delete,
                Right, Left, Down, Up,
                PageUp, PageDown, Home, End,
                LeftShift = 340, LeftControl, LeftAlt,
                RightShift = 344, RightControl, RightAlt,
                Count = 512
            };

            enum class MouseButton : uint32_t
            {
                Left = 0,
                Right = 1,
                Middle = 2,
                Button4 = 3,
                Button5 = 4,
                Count = 5
            };

            struct ActionBinding
            {
                std::string actionName;
                std::vector<KeyCode> keys;
                std::vector<MouseButton> mouseButtons;
            };

            struct AxisKeyEntry
            {
                KeyCode key = KeyCode::Unknown;
                float scale = 1.0f;
            };

            struct AxisBinding
            {
                std::string axisName;
                std::vector<AxisKeyEntry> keyEntries;
            };

            struct ResolvedKeyMapping
            {
                std::string actionName;
                std::string contextName;
                bool triggered{false};
            };

            /**
             * @brief HID 输入子系统核心类
             */
            class ENGINE_API HIDSubsystem
            {
            public:
                using ActionEventCallback = std::function<void(const InputValue&, ETriggerState)>;

                struct TimestampedAction
                {
                    uint64_t timestampMs{0};
                    InputActionHandle handle{0};
                    InputValue value;
                };

                HIDSubsystem();
                ~HIDSubsystem();

                HIDSubsystem(const HIDSubsystem&) = delete;
                HIDSubsystem& operator=(const HIDSubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 每逻辑帧排空 UIHost 原始输入事件并推进 Enhanced Input 优先级上下文栈
                 */
                void update();
                void update(float deltaTime);

                // 传统兼容动作/轴状态查询
                bool isActionPressed(const std::string& actionName) const;
                bool isActionJustPressed(const std::string& actionName) const;
                bool isActionJustReleased(const std::string& actionName) const;
                float getAxis(const std::string& axisName) const;

                bool isKeyPressed(KeyCode key) const;
                bool isKeyJustPressed(KeyCode key) const;
                bool isKeyJustReleased(KeyCode key) const;

                bool isMouseButtonPressed(MouseButton button) const;
                bool isMouseButtonJustPressed(MouseButton button) const;
                bool isMouseButtonJustReleased(MouseButton button) const;

                FCT::Vec2 getMousePosition() const noexcept;
                FCT::Vec2 getMouseDelta() const noexcept;
                float getMouseWheelDelta() const noexcept;

                void registerAction(const std::string& actionName, KeyCode key);
                void registerAction(const std::string& actionName, MouseButton button);
                void registerAxis(const std::string& axisName, KeyCode positiveKey, KeyCode negativeKey);
                void registerAxis(const std::string& axisName, KeyCode key, float scale);

                // ==================== Enhanced Input (FEAT-60 ~ FEAT-66) ====================
                InputActionHandle registerAction(
                    const std::string& actionName,
                    EInputActionValueType type,
                    EInputAccumulationPolicy policy = EInputAccumulationPolicy::Cumulative);
                void registerAction(UniquePtr<InputAction> action);

                [[nodiscard]] bool isActionValid(std::string_view actionName) const noexcept;
                [[nodiscard]] InputActionHandle getActionHandle(std::string_view actionName) const noexcept;
                [[nodiscard]] InputAction* findAction(InputActionHandle handle) noexcept;
                [[nodiscard]] const InputAction* findAction(InputActionHandle handle) const noexcept;

                void addMappingContext(UniquePtr<InputMappingContext> imc, int32_t priority);
                void removeMappingContext(std::string_view contextName);
                [[nodiscard]] bool hasActiveContext(std::string_view contextName) const noexcept;
                [[nodiscard]] const std::vector<UniquePtr<InputMappingContext>>& getContextStack() const noexcept { return m_imcStack; }
                [[nodiscard]] ResolvedKeyMapping resolveKeyInput(std::string_view sourceKey) const;

                void setSourceInputValue(std::string_view sourceKey, const InputValue& value);
                void clearSourceInputValues() noexcept;

                [[nodiscard]] bool isActionPressed(std::string_view actionName) const noexcept;
                [[nodiscard]] InputValue getActionValue(std::string_view actionName) const noexcept;
                [[nodiscard]] InputValue getActionValue(InputActionHandle handle) const noexcept;
                [[nodiscard]] ETriggerState getActionTriggerState(InputActionHandle handle) const noexcept;
                [[nodiscard]] ETriggerState getActionTriggerState(std::string_view actionName) const noexcept;

                uint64_t bindActionEvent(InputActionHandle handle, ETriggerState triggerEvent, ActionEventCallback callback);
                void unbindActionEvent(uint64_t callbackId);

                Status injectInputForAction(InputActionHandle handle, const InputValue& rawValue, float deltaTime = 1.0f / 60.0f);
                Status injectInputForAction(std::string_view actionName, const InputValue& rawValue, float deltaTime = 1.0f / 60.0f);
                Status replayActionSequence(const std::vector<TimestampedAction>& sequence, uint32_t& outProcessedCount);

                // 触控虚拟手势源（被 UI 消费的触点绝不泄漏至手势源与 GamePlay）
                void onTouchDown(uint32_t pointerId, float x, float y, bool consumedByUi = false);
                void onTouchMove(uint32_t pointerId, float x, float y, bool consumedByUi = false);
                void onTouchUp(uint32_t pointerId, float x = 0.0f, float y = 0.0f);
                void resetTouches() noexcept;

                void processTouchSwipe(float startX, float startY, float currX, float currY, bool touchEnded);
                void processTouchPinch(float dist0, float dist1, bool touchEnded);

                [[nodiscard]] FCT::Vec2 getTouchSwipeDeltaForPointer(uint32_t pointerId) const noexcept;
                [[nodiscard]] bool isTouchPinchActive() const noexcept;
                [[nodiscard]] const FCT::Vec2& getTouchSwipeDelta() const noexcept { return m_touchSwipeDelta; }
                [[nodiscard]] float getTouchPinchDelta() const noexcept { return m_touchPinchDelta; }

            private:
                struct TouchPointerTrack
                {
                    uint32_t pointerId{0};
                    float prevX{0.0f};
                    float prevY{0.0f};
                    float currX{0.0f};
                    float currY{0.0f};
                    bool consumedByUi{false};
                };

                struct ActionCallbackEntry
                {
                    uint64_t id{0};
                    InputActionHandle handle{0};
                    ETriggerState triggerEvent{ETriggerState::Triggered};
                    ActionEventCallback callback;
                };

                void registerTickerWithLogicHost();
                void unregisterTickerFromLogicHost() noexcept;
                void processRawEvent(const Host::RawInputEvent& event);
                void evaluateMappingContexts(float deltaTime);
                InputValue samplePhysicalOrVirtualSource(std::string_view sourceKey) const noexcept;
                void updateTouchVirtualSources();
                void notifyActionCallbacks(InputActionHandle handle, const InputValue& val, ETriggerState state);

            private:
                std::atomic<bool> m_initialized{false};

                std::bitset<512> m_keysDown;
                std::bitset<512> m_keysJustPressed;
                std::bitset<512> m_keysJustReleased;

                std::array<bool, 5> m_mouseButtonsDown{false};
                std::array<bool, 5> m_mouseButtonsJustPressed{false};
                std::array<bool, 5> m_mouseButtonsJustReleased{false};

                FCT::Vec2 m_mousePosition{0.0f, 0.0f};
                FCT::Vec2 m_mouseDelta{0.0f, 0.0f};
                float m_mouseWheelDelta = 0.0f;

                std::unordered_map<std::string, ActionBinding> m_actionBindings;
                std::unordered_map<std::string, AxisBinding> m_axisBindings;

                std::vector<UniquePtr<InputAction>> m_enhancedActions;
                std::unordered_map<std::string, InputActionHandle> m_actionNameToHandle;
                std::vector<UniquePtr<InputMappingContext>> m_imcStack;
                std::unordered_map<std::string, InputValue> m_customSourceValues;
                std::unordered_map<InputActionHandle, InputValue> m_actionValues;
                std::unordered_map<InputActionHandle, ETriggerState> m_actionTriggerStates;
                std::vector<ActionCallbackEntry> m_actionCallbacks;
                uint64_t m_nextCallbackId{1};

                std::unordered_map<uint32_t, TouchPointerTrack> m_activeTouches;
                FCT::Vec2 m_touchSwipeDelta{0.0f, 0.0f};
                float m_touchPinchDelta{0.0f};
                InputActionHandle m_nextActionHandle{1};
            };

        } // namespace HID
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_HID_HIDSUBSYSTEM_H
