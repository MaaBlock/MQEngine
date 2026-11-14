/**
 * @file HIDSubsystem.cpp
 * @brief 人机交互接口 (HID) 子域实现
 */
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_set>

namespace MQEngine
{
    namespace Domain
    {
        namespace HID
        {
            InputAction::~InputAction() = default;
            InputAction::InputAction(InputAction&&) noexcept = default;
            InputAction& InputAction::operator=(InputAction&&) noexcept = default;

            void InputAction::addModifier(UniquePtr<InputModifier> modifier)
            {
                if (modifier)
                {
                    m_modifiers.push_back(std::move(modifier));
                }
            }

            void InputAction::addTrigger(UniquePtr<InputTrigger> trigger)
            {
                if (trigger)
                {
                    m_triggers.push_back(std::move(trigger));
                }
            }

            namespace
            {
                bool parseSingleKeySource(std::string_view keyName, KeyCode& outCode) noexcept
                {
                    if (keyName.rfind("Key_", 0) != 0)
                    {
                        return false;
                    }
                    const std::string_view suffix = keyName.substr(4);
                    if (suffix.size() == 1)
                    {
                        const char c = suffix[0];
                        if (c >= 'A' && c <= 'Z')
                        {
                            outCode = static_cast<KeyCode>(static_cast<uint32_t>(KeyCode::A) + static_cast<uint32_t>(c - 'A'));
                            return true;
                        }
                        if (c >= '0' && c <= '9')
                        {
                            outCode = static_cast<KeyCode>(static_cast<uint32_t>(KeyCode::Num0) + static_cast<uint32_t>(c - '0'));
                            return true;
                        }
                    }
                    if (suffix == "Space") { outCode = KeyCode::Space; return true; }
                    if (suffix == "Escape") { outCode = KeyCode::Escape; return true; }
                    if (suffix == "Enter") { outCode = KeyCode::Enter; return true; }
                    if (suffix == "Tab") { outCode = KeyCode::Tab; return true; }
                    if (suffix == "LeftShift") { outCode = KeyCode::LeftShift; return true; }
                    if (suffix == "LeftControl") { outCode = KeyCode::LeftControl; return true; }
                    if (suffix == "LeftAlt") { outCode = KeyCode::LeftAlt; return true; }
                    if (suffix == "Up") { outCode = KeyCode::Up; return true; }
                    if (suffix == "Down") { outCode = KeyCode::Down; return true; }
                    if (suffix == "Left") { outCode = KeyCode::Left; return true; }
                    if (suffix == "Right") { outCode = KeyCode::Right; return true; }
                    return false;
                }
            } // namespace

            HIDSubsystem::HIDSubsystem() = default;

            HIDSubsystem::~HIDSubsystem()
            {
                term();
            }

            void HIDSubsystem::registerInfo()
            {
            }

            Status HIDSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed))
                {
                    return OkStatus();
                }

                registerAxis("MoveForward", KeyCode::W, KeyCode::S);
                registerAxis("MoveRight", KeyCode::D, KeyCode::A);
                registerAction("Jump", KeyCode::Space);
                registerAction("Fire", MouseButton::Left);

                registerTickerWithLogicHost();

                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void HIDSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed))
                {
                    return;
                }

                unregisterTickerFromLogicHost();
                m_initialized.store(false, std::memory_order_release);
            }

            void HIDSubsystem::update()
            {
                update(1.0f / 60.0f);
            }

            void HIDSubsystem::update(float deltaTime)
            {
                if (m_initialized.load(std::memory_order_acquire))
                {
                    m_keysJustPressed.reset();
                    m_keysJustReleased.reset();
                    m_mouseButtonsJustPressed.fill(false);
                    m_mouseButtonsJustReleased.fill(false);
                    m_mouseDelta = FCT::Vec2(0.0f, 0.0f);
                    m_mouseWheelDelta = 0.0f;

                    if (auto* uiHost = Host::UIHost::InstancePtr())
                    {
                        std::vector<Host::RawInputEvent> events = uiHost->drainEvents();
                        for (const auto& ev : events)
                        {
                            processRawEvent(ev);
                        }
                    }
                }

                if (!m_imcStack.empty())
                {
                    evaluateMappingContexts(deltaTime);
                }
            }

            void HIDSubsystem::processRawEvent(const Host::RawInputEvent& event)
            {
                switch (event.type)
                {
                case Host::RawInputEventType::KeyDown:
                {
                    const uint32_t code = event.keyCode;
                    if (code < 512)
                    {
                        if (!m_keysDown.test(code))
                        {
                            m_keysJustPressed.set(code);
                        }
                        m_keysDown.set(code);
                    }
                    break;
                }
                case Host::RawInputEventType::KeyUp:
                {
                    const uint32_t code = event.keyCode;
                    if (code < 512)
                    {
                        if (m_keysDown.test(code))
                        {
                            m_keysJustReleased.set(code);
                        }
                        m_keysDown.reset(code);
                    }
                    break;
                }
                case Host::RawInputEventType::MouseDown:
                {
                    const uint32_t btn = event.mouseButton;
                    if (btn < 5)
                    {
                        if (!m_mouseButtonsDown[btn])
                        {
                            m_mouseButtonsJustPressed[btn] = true;
                        }
                        m_mouseButtonsDown[btn] = true;
                    }
                    break;
                }
                case Host::RawInputEventType::MouseUp:
                {
                    const uint32_t btn = event.mouseButton;
                    if (btn < 5)
                    {
                        if (m_mouseButtonsDown[btn])
                        {
                            m_mouseButtonsJustReleased[btn] = true;
                        }
                        m_mouseButtonsDown[btn] = false;
                    }
                    break;
                }
                case Host::RawInputEventType::MouseMove:
                {
                    const float newX = static_cast<float>(event.mouseX);
                    const float newY = static_cast<float>(event.mouseY);
                    m_mouseDelta.x += (newX - m_mousePosition.x);
                    m_mouseDelta.y += (newY - m_mousePosition.y);
                    m_mousePosition = FCT::Vec2(newX, newY);
                    break;
                }
                case Host::RawInputEventType::MouseWheel:
                {
                    m_mouseWheelDelta += static_cast<float>(event.delta);
                    break;
                }
                default:
                    break;
                }
            }

            InputValue HIDSubsystem::samplePhysicalOrVirtualSource(std::string_view sourceKey) const noexcept
            {
                auto customIt = m_customSourceValues.find(std::string(sourceKey));
                if (customIt != m_customSourceValues.end())
                {
                    return customIt->second;
                }

                if (sourceKey == "TouchSwipeDelta2D")
                {
                    return InputValue(m_touchSwipeDelta);
                }
                if (sourceKey == "TouchPinchDelta1D")
                {
                    return InputValue(m_touchPinchDelta);
                }
                if (sourceKey == "Mouse_Delta2D")
                {
                    return InputValue(m_mouseDelta);
                }
                if (sourceKey == "Mouse_Wheel1D")
                {
                    return InputValue(m_mouseWheelDelta);
                }
                if (sourceKey == "Mouse_Left")
                {
                    return InputValue(isMouseButtonPressed(MouseButton::Left));
                }
                if (sourceKey == "Mouse_Right")
                {
                    return InputValue(isMouseButtonPressed(MouseButton::Right));
                }
                if (sourceKey == "Mouse_Middle")
                {
                    return InputValue(isMouseButtonPressed(MouseButton::Middle));
                }

                KeyCode kc = KeyCode::Unknown;
                if (parseSingleKeySource(sourceKey, kc))
                {
                    return InputValue(isKeyPressed(kc) ? 1.0f : 0.0f);
                }
                return InputValue(false);
            }

            void HIDSubsystem::evaluateMappingContexts(float deltaTime)
            {
                std::unordered_map<InputActionHandle, InputValue> frameAccumulated;
                std::unordered_map<InputActionHandle, bool> hasContribution;
                for (const auto& action : m_enhancedActions)
                {
                    frameAccumulated[action->getHandle()] = InputValue::zeroForType(action->getValueType());
                    hasContribution[action->getHandle()] = false;
                }

                std::unordered_set<std::string> consumedSources;
                std::unordered_map<InputActionHandle, ETriggerState> bindingTriggerOverride;
                std::unordered_map<InputActionHandle, bool> hasBindingTriggers;

                for (auto& imc : m_imcStack)
                {
                    for (auto& binding : imc->getMutableBindings())
                    {
                        InputActionHandle handle = binding.actionHandle;
                        if (handle == 0 && !binding.actionName.empty())
                        {
                            handle = getActionHandle(binding.actionName);
                            binding.actionHandle = handle;
                        }
                        InputAction* action = findAction(handle);
                        if (!action)
                        {
                            continue;
                        }

                        const bool isConsumed = (consumedSources.find(binding.inputSourceKey) != consumedSources.end());
                        InputValue val = isConsumed
                            ? InputValue::zeroForType(action->getValueType())
                            : samplePhysicalOrVirtualSource(binding.inputSourceKey);

                        if (!isConsumed && binding.consumesInput && val.magnitude() > 0.0f)
                        {
                            consumedSources.insert(binding.inputSourceKey);
                        }

                        for (const auto& mod : binding.modifiers)
                        {
                            val = mod->modify(val, deltaTime);
                        }

                        if (!binding.triggers.empty())
                        {
                            hasBindingTriggers[handle] = true;
                            for (const auto& trig : binding.triggers)
                            {
                                if (auto* chord = dynamic_cast<InputTriggerChordedAction*>(trig.get()))
                                {
                                    bool active = false;
                                    if (chord->getChordHandle() != 0)
                                    {
                                        active = getActionValue(chord->getChordHandle()).magnitude() > 0.0f ||
                                                 frameAccumulated[chord->getChordHandle()].magnitude() > 0.0f;
                                    }
                                    else if (!chord->getChordActionName().empty())
                                    {
                                        active = isActionPressed(std::string_view(chord->getChordActionName()));
                                    }
                                    chord->setChordActive(active);
                                }
                                const ETriggerState st = trig->update(val, deltaTime);
                                if (st != ETriggerState::None)
                                {
                                    bindingTriggerOverride[handle] = st;
                                }
                            }
                        }

                        if (!hasContribution[handle])
                        {
                            frameAccumulated[handle] = val.convertTo(action->getValueType());
                            hasContribution[handle] = true;
                        }
                        else
                        {
                            frameAccumulated[handle].accumulate(val, action->getAccumulationPolicy());
                        }
                    }
                }

                for (auto& action : m_enhancedActions)
                {
                    const InputActionHandle handle = action->getHandle();
                    if (!hasContribution[handle])
                    {
                        continue;
                    }

                    InputValue val = frameAccumulated[handle];
                    for (const auto& mod : action->getModifiers())
                    {
                        val = mod->modify(val, deltaTime);
                    }
                    val = val.convertTo(action->getValueType());
                    m_actionValues[handle] = val;

                    ETriggerState finalState = ETriggerState::None;
                    auto ovIt = bindingTriggerOverride.find(handle);
                    if (ovIt != bindingTriggerOverride.end())
                    {
                        finalState = ovIt->second;
                    }
                    else if (action->getTriggers().empty() && !hasBindingTriggers[handle])
                    {
                        finalState = (val.magnitude() > 0.0f) ? ETriggerState::Triggered : ETriggerState::None;
                    }
                    else
                    {
                        for (const auto& trig : action->getTriggers())
                        {
                            if (auto* chord = dynamic_cast<InputTriggerChordedAction*>(trig.get()))
                            {
                                if (chord->getChordHandle() != 0)
                                {
                                    chord->setChordActive(getActionValue(chord->getChordHandle()).magnitude() > 0.0f);
                                }
                                else if (!chord->getChordActionName().empty())
                                {
                                    chord->setChordActive(isActionPressed(std::string_view(chord->getChordActionName())));
                                }
                            }
                            const ETriggerState st = trig->update(val, deltaTime);
                            if (st != ETriggerState::None)
                            {
                                finalState = st;
                            }
                        }
                    }

                    m_actionTriggerStates[handle] = finalState;
                    if (finalState != ETriggerState::None)
                    {
                        notifyActionCallbacks(handle, val, finalState);
                    }
                }
            }

            bool HIDSubsystem::isActionPressed(const std::string& actionName) const
            {
                auto enhIt = m_actionNameToHandle.find(actionName);
                if (enhIt != m_actionNameToHandle.end())
                {
                    auto valIt = m_actionValues.find(enhIt->second);
                    if (valIt != m_actionValues.end() && valIt->second.magnitude() > 0.0f)
                    {
                        return true;
                    }
                }

                auto it = m_actionBindings.find(actionName);
                if (it == m_actionBindings.end())
                {
                    return false;
                }
                for (KeyCode key : it->second.keys)
                {
                    if (isKeyPressed(key)) return true;
                }
                for (MouseButton btn : it->second.mouseButtons)
                {
                    if (isMouseButtonPressed(btn)) return true;
                }
                return false;
            }

            bool HIDSubsystem::isActionJustPressed(const std::string& actionName) const
            {
                auto it = m_actionBindings.find(actionName);
                if (it == m_actionBindings.end())
                {
                    return false;
                }
                for (KeyCode key : it->second.keys)
                {
                    if (isKeyJustPressed(key)) return true;
                }
                for (MouseButton btn : it->second.mouseButtons)
                {
                    if (isMouseButtonJustPressed(btn)) return true;
                }
                return false;
            }

            bool HIDSubsystem::isActionJustReleased(const std::string& actionName) const
            {
                auto it = m_actionBindings.find(actionName);
                if (it == m_actionBindings.end())
                {
                    return false;
                }
                for (KeyCode key : it->second.keys)
                {
                    if (isKeyJustReleased(key)) return true;
                }
                for (MouseButton btn : it->second.mouseButtons)
                {
                    if (isMouseButtonJustReleased(btn)) return true;
                }
                return false;
            }

            float HIDSubsystem::getAxis(const std::string& axisName) const
            {
                auto it = m_axisBindings.find(axisName);
                if (it == m_axisBindings.end())
                {
                    return 0.0f;
                }
                float value = 0.0f;
                for (const auto& entry : it->second.keyEntries)
                {
                    if (isKeyPressed(entry.key))
                    {
                        value += entry.scale;
                    }
                }
                return value;
            }

            bool HIDSubsystem::isKeyPressed(KeyCode key) const
            {
                const uint32_t idx = static_cast<uint32_t>(key);
                return (idx < 512) ? m_keysDown.test(idx) : false;
            }

            bool HIDSubsystem::isKeyJustPressed(KeyCode key) const
            {
                const uint32_t idx = static_cast<uint32_t>(key);
                return (idx < 512) ? m_keysJustPressed.test(idx) : false;
            }

            bool HIDSubsystem::isKeyJustReleased(KeyCode key) const
            {
                const uint32_t idx = static_cast<uint32_t>(key);
                return (idx < 512) ? m_keysJustReleased.test(idx) : false;
            }

            bool HIDSubsystem::isMouseButtonPressed(MouseButton button) const
            {
                const uint32_t idx = static_cast<uint32_t>(button);
                return (idx < 5) ? m_mouseButtonsDown[idx] : false;
            }

            bool HIDSubsystem::isMouseButtonJustPressed(MouseButton button) const
            {
                const uint32_t idx = static_cast<uint32_t>(button);
                return (idx < 5) ? m_mouseButtonsJustPressed[idx] : false;
            }

            bool HIDSubsystem::isMouseButtonJustReleased(MouseButton button) const
            {
                const uint32_t idx = static_cast<uint32_t>(button);
                return (idx < 5) ? m_mouseButtonsJustReleased[idx] : false;
            }

            FCT::Vec2 HIDSubsystem::getMousePosition() const noexcept
            {
                return m_mousePosition;
            }

            FCT::Vec2 HIDSubsystem::getMouseDelta() const noexcept
            {
                return m_mouseDelta;
            }

            float HIDSubsystem::getMouseWheelDelta() const noexcept
            {
                return m_mouseWheelDelta;
            }

            void HIDSubsystem::registerAction(const std::string& actionName, KeyCode key)
            {
                m_actionBindings[actionName].actionName = actionName;
                m_actionBindings[actionName].keys.push_back(key);
            }

            void HIDSubsystem::registerAction(const std::string& actionName, MouseButton button)
            {
                m_actionBindings[actionName].actionName = actionName;
                m_actionBindings[actionName].mouseButtons.push_back(button);
            }

            void HIDSubsystem::registerAxis(const std::string& axisName, KeyCode positiveKey, KeyCode negativeKey)
            {
                auto& binding = m_axisBindings[axisName];
                binding.axisName = axisName;
                binding.keyEntries.push_back({positiveKey, 1.0f});
                binding.keyEntries.push_back({negativeKey, -1.0f});
            }

            void HIDSubsystem::registerAxis(const std::string& axisName, KeyCode key, float scale)
            {
                auto& binding = m_axisBindings[axisName];
                binding.axisName = axisName;
                binding.keyEntries.push_back({key, scale});
            }

            void HIDSubsystem::registerTickerWithLogicHost()
            {
                if (auto* logicHost = Host::LogicHost::InstancePtr())
                {
                    logicHost->tickers().addNode("Domain_HID_DrainEvents", [this]() {
                        update();
                    });
                }
            }

            void HIDSubsystem::unregisterTickerFromLogicHost() noexcept
            {
                if (auto* logicHost = Host::LogicHost::InstancePtr())
                {
                    logicHost->tickers().removeNode("Domain_HID_DrainEvents");
                }
            }

            InputActionHandle HIDSubsystem::registerAction(
                const std::string& actionName,
                EInputActionValueType type,
                EInputAccumulationPolicy policy)
            {
                auto existingIt = m_actionNameToHandle.find(actionName);
                if (existingIt != m_actionNameToHandle.end())
                {
                    return existingIt->second;
                }

                const InputActionHandle handle = m_nextActionHandle++;
                auto action = makeUnique<InputAction>(actionName, handle, type, policy);
                m_actionNameToHandle[actionName] = handle;
                m_actionValues[handle] = InputValue::zeroForType(type);
                m_actionTriggerStates[handle] = ETriggerState::None;
                m_enhancedActions.push_back(std::move(action));
                return handle;
            }

            void HIDSubsystem::registerAction(UniquePtr<InputAction> action)
            {
                if (!action)
                {
                    return;
                }
                InputActionHandle handle = action->getHandle();
                if (handle == 0)
                {
                    handle = m_nextActionHandle++;
                    action->setHandle(handle);
                }
                else if (handle >= m_nextActionHandle)
                {
                    m_nextActionHandle = handle + 1;
                }
                m_actionNameToHandle[action->getName()] = handle;
                m_actionValues[handle] = InputValue::zeroForType(action->getValueType());
                m_actionTriggerStates[handle] = ETriggerState::None;
                m_enhancedActions.push_back(std::move(action));
            }

            bool HIDSubsystem::isActionValid(std::string_view actionName) const noexcept
            {
                if (m_actionNameToHandle.find(std::string(actionName)) != m_actionNameToHandle.end())
                {
                    return true;
                }
                return m_actionBindings.find(std::string(actionName)) != m_actionBindings.end();
            }

            InputActionHandle HIDSubsystem::getActionHandle(std::string_view actionName) const noexcept
            {
                auto it = m_actionNameToHandle.find(std::string(actionName));
                return (it != m_actionNameToHandle.end()) ? it->second : 0u;
            }

            InputAction* HIDSubsystem::findAction(InputActionHandle handle) noexcept
            {
                for (auto& a : m_enhancedActions)
                {
                    if (a->getHandle() == handle)
                    {
                        return a.get();
                    }
                }
                return nullptr;
            }

            const InputAction* HIDSubsystem::findAction(InputActionHandle handle) const noexcept
            {
                for (const auto& a : m_enhancedActions)
                {
                    if (a->getHandle() == handle)
                    {
                        return a.get();
                    }
                }
                return nullptr;
            }

            void HIDSubsystem::addMappingContext(UniquePtr<InputMappingContext> imc, int32_t priority)
            {
                if (!imc)
                {
                    return;
                }
                removeMappingContext(imc->getName());
                imc->setPriority(priority);
                m_imcStack.push_back(std::move(imc));
                std::stable_sort(
                    m_imcStack.begin(),
                    m_imcStack.end(),
                    [](const UniquePtr<InputMappingContext>& a, const UniquePtr<InputMappingContext>& b) {
                        return a->getPriority() > b->getPriority();
                    });
            }

            void HIDSubsystem::removeMappingContext(std::string_view contextName)
            {
                m_imcStack.erase(
                    std::remove_if(
                        m_imcStack.begin(),
                        m_imcStack.end(),
                        [contextName](const UniquePtr<InputMappingContext>& ctx) {
                            return ctx && ctx->getName() == contextName;
                        }),
                    m_imcStack.end());
            }

            bool HIDSubsystem::hasActiveContext(std::string_view contextName) const noexcept
            {
                for (const auto& ctx : m_imcStack)
                {
                    if (ctx && ctx->getName() == contextName)
                    {
                        return true;
                    }
                }
                return false;
            }

            ResolvedKeyMapping HIDSubsystem::resolveKeyInput(std::string_view sourceKey) const
            {
                for (const auto& ctx : m_imcStack)
                {
                    if (!ctx)
                    {
                        continue;
                    }
                    for (const auto& binding : ctx->getBindings())
                    {
                        if (binding.inputSourceKey == sourceKey)
                        {
                            std::string actName = binding.actionName;
                            if (actName.empty() && binding.actionHandle != 0)
                            {
                                if (const auto* act = findAction(binding.actionHandle))
                                {
                                    actName = act->getName();
                                }
                            }
                            return ResolvedKeyMapping{actName, ctx->getName(), true};
                        }
                    }
                }
                return ResolvedKeyMapping{"", "", false};
            }

            void HIDSubsystem::setSourceInputValue(std::string_view sourceKey, const InputValue& value)
            {
                m_customSourceValues[std::string(sourceKey)] = value;
            }

            void HIDSubsystem::clearSourceInputValues() noexcept
            {
                m_customSourceValues.clear();
            }

            bool HIDSubsystem::isActionPressed(std::string_view actionName) const noexcept
            {
                auto it = m_actionNameToHandle.find(std::string(actionName));
                if (it != m_actionNameToHandle.end())
                {
                    auto valIt = m_actionValues.find(it->second);
                    if (valIt != m_actionValues.end())
                    {
                        return valIt->second.magnitude() > 0.0f;
                    }
                }
                return isActionPressed(std::string(actionName));
            }

            InputValue HIDSubsystem::getActionValue(std::string_view actionName) const noexcept
            {
                auto it = m_actionNameToHandle.find(std::string(actionName));
                if (it != m_actionNameToHandle.end())
                {
                    return getActionValue(it->second);
                }
                return InputValue(0.0f);
            }

            InputValue HIDSubsystem::getActionValue(InputActionHandle handle) const noexcept
            {
                auto it = m_actionValues.find(handle);
                if (it != m_actionValues.end())
                {
                    return it->second;
                }
                return InputValue(0.0f);
            }

            ETriggerState HIDSubsystem::getActionTriggerState(InputActionHandle handle) const noexcept
            {
                auto it = m_actionTriggerStates.find(handle);
                return (it != m_actionTriggerStates.end()) ? it->second : ETriggerState::None;
            }

            ETriggerState HIDSubsystem::getActionTriggerState(std::string_view actionName) const noexcept
            {
                const InputActionHandle handle = getActionHandle(actionName);
                return (handle != 0) ? getActionTriggerState(handle) : ETriggerState::None;
            }

            uint64_t HIDSubsystem::bindActionEvent(
                InputActionHandle handle,
                ETriggerState triggerEvent,
                ActionEventCallback callback)
            {
                if (handle == 0 || !callback)
                {
                    return 0;
                }
                const uint64_t id = m_nextCallbackId++;
                m_actionCallbacks.push_back(ActionCallbackEntry{id, handle, triggerEvent, std::move(callback)});
                return id;
            }

            void HIDSubsystem::unbindActionEvent(uint64_t callbackId)
            {
                m_actionCallbacks.erase(
                    std::remove_if(
                        m_actionCallbacks.begin(),
                        m_actionCallbacks.end(),
                        [callbackId](const ActionCallbackEntry& item) { return item.id == callbackId; }),
                    m_actionCallbacks.end());
            }

            void HIDSubsystem::notifyActionCallbacks(
                InputActionHandle handle,
                const InputValue& val,
                ETriggerState state)
            {
                for (const auto& entry : m_actionCallbacks)
                {
                    if (entry.handle == handle && entry.triggerEvent == state && entry.callback)
                    {
                        entry.callback(val, state);
                    }
                }
            }

            Status HIDSubsystem::injectInputForAction(
                InputActionHandle handle,
                const InputValue& rawValue,
                float deltaTime)
            {
                if (handle == 0)
                {
                    return InvalidArgumentError("无效动作句柄 0");
                }

                InputAction* targetAction = findAction(handle);
                if (!targetAction)
                {
                    return NotFoundError(StrCat("未找到句柄为 ", handle, " 的动作"));
                }

                InputValue val = rawValue;
                for (const auto& mod : targetAction->getModifiers())
                {
                    val = mod->modify(val, deltaTime);
                }
                val = val.convertTo(targetAction->getValueType());
                m_actionValues[handle] = val;

                ETriggerState finalState = ETriggerState::None;
                if (targetAction->getTriggers().empty())
                {
                    finalState = (val.magnitude() > 0.0f) ? ETriggerState::Triggered : ETriggerState::Completed;
                }
                else
                {
                    for (const auto& trig : targetAction->getTriggers())
                    {
                        if (auto* chord = dynamic_cast<InputTriggerChordedAction*>(trig.get()))
                        {
                            if (chord->getChordHandle() != 0)
                            {
                                chord->setChordActive(getActionValue(chord->getChordHandle()).magnitude() > 0.0f);
                            }
                            else if (!chord->getChordActionName().empty())
                            {
                                chord->setChordActive(isActionPressed(std::string_view(chord->getChordActionName())));
                            }
                        }
                        const ETriggerState st = trig->update(val, deltaTime);
                        if (st != ETriggerState::None)
                        {
                            finalState = st;
                        }
                    }
                }
                m_actionTriggerStates[handle] = finalState;
                if (finalState != ETriggerState::None)
                {
                    notifyActionCallbacks(handle, val, finalState);
                }
                return OkStatus();
            }

            Status HIDSubsystem::injectInputForAction(
                std::string_view actionName,
                const InputValue& rawValue,
                float deltaTime)
            {
                const InputActionHandle handle = getActionHandle(actionName);
                if (handle == 0)
                {
                    return NotFoundError(StrCat("未找到名为 ", actionName, " 的动作"));
                }
                return injectInputForAction(handle, rawValue, deltaTime);
            }

            Status HIDSubsystem::replayActionSequence(
                const std::vector<TimestampedAction>& sequence,
                uint32_t& outProcessedCount)
            {
                outProcessedCount = 0;
                uint64_t prevTs = sequence.empty() ? 0ull : sequence.front().timestampMs;
                for (const auto& item : sequence)
                {
                    const float dt = (item.timestampMs > prevTs)
                        ? (static_cast<float>(item.timestampMs - prevTs) * 0.001f)
                        : (1.0f / 60.0f);
                    prevTs = item.timestampMs;
                    CHECK_STATUS(injectInputForAction(item.handle, item.value, dt));
                    outProcessedCount++;
                }
                return OkStatus();
            }

            void HIDSubsystem::onTouchDown(uint32_t pointerId, float x, float y, bool consumedByUi)
            {
                m_activeTouches[pointerId] = TouchPointerTrack{pointerId, x, y, x, y, consumedByUi};
                updateTouchVirtualSources();
            }

            void HIDSubsystem::onTouchMove(uint32_t pointerId, float x, float y, bool consumedByUi)
            {
                auto it = m_activeTouches.find(pointerId);
                if (it == m_activeTouches.end())
                {
                    m_activeTouches[pointerId] = TouchPointerTrack{pointerId, x, y, x, y, consumedByUi};
                }
                else
                {
                    it->second.prevX = it->second.currX;
                    it->second.prevY = it->second.currY;
                    it->second.currX = x;
                    it->second.currY = y;
                    if (consumedByUi)
                    {
                        it->second.consumedByUi = true;
                    }
                }
                updateTouchVirtualSources();
            }

            void HIDSubsystem::onTouchUp(uint32_t pointerId, float /*x*/, float /*y*/)
            {
                m_activeTouches.erase(pointerId);
                updateTouchVirtualSources();
            }

            void HIDSubsystem::resetTouches() noexcept
            {
                m_activeTouches.clear();
                m_touchSwipeDelta = FCT::Vec2{0.0f, 0.0f};
                m_touchPinchDelta = 0.0f;
            }

            FCT::Vec2 HIDSubsystem::getTouchSwipeDeltaForPointer(uint32_t pointerId) const noexcept
            {
                auto it = m_activeTouches.find(pointerId);
                if (it == m_activeTouches.end() || it->second.consumedByUi)
                {
                    return FCT::Vec2{0.0f, 0.0f};
                }
                return FCT::Vec2{it->second.currX - it->second.prevX, it->second.currY - it->second.prevY};
            }

            bool HIDSubsystem::isTouchPinchActive() const noexcept
            {
                uint32_t unconsumedCount = 0;
                for (const auto& [id, pt] : m_activeTouches)
                {
                    (void)id;
                    if (!pt.consumedByUi)
                    {
                        ++unconsumedCount;
                    }
                }
                return unconsumedCount >= 2u;
            }

            void HIDSubsystem::updateTouchVirtualSources()
            {
                std::vector<const TouchPointerTrack*> freeTouches;
                freeTouches.reserve(m_activeTouches.size());
                for (const auto& [id, pt] : m_activeTouches)
                {
                    (void)id;
                    if (!pt.consumedByUi)
                    {
                        freeTouches.push_back(&pt);
                    }
                }

                if (freeTouches.empty())
                {
                    m_touchSwipeDelta = FCT::Vec2{0.0f, 0.0f};
                    m_touchPinchDelta = 0.0f;
                }
                else if (freeTouches.size() == 1u)
                {
                    const auto* t0 = freeTouches[0];
                    m_touchSwipeDelta = FCT::Vec2{t0->currX - t0->prevX, t0->currY - t0->prevY};
                    m_touchPinchDelta = 0.0f;
                }
                else
                {
                    std::sort(freeTouches.begin(), freeTouches.end(), [](const auto* a, const auto* b) {
                        return a->pointerId < b->pointerId;
                    });
                    const auto* p0 = freeTouches[0];
                    const auto* p1 = freeTouches[1];
                    const float curDx = p0->currX - p1->currX;
                    const float curDy = p0->currY - p1->currY;
                    const float prevDx = p0->prevX - p1->prevX;
                    const float prevDy = p0->prevY - p1->prevY;
                    const float curDist = std::sqrt(curDx * curDx + curDy * curDy);
                    const float prevDist = std::sqrt(prevDx * prevDx + prevDy * prevDy);
                    m_touchPinchDelta = curDist - prevDist;
                    m_touchSwipeDelta = FCT::Vec2{
                        0.5f * ((p0->currX - p0->prevX) + (p1->currX - p1->prevX)),
                        0.5f * ((p0->currY - p0->prevY) + (p1->currY - p1->prevY))};
                }

                auto swipeIt = m_actionNameToHandle.find("TouchSwipeDelta2D");
                if (swipeIt != m_actionNameToHandle.end())
                {
                    (void)injectInputForAction(swipeIt->second, InputValue(m_touchSwipeDelta));
                }
                auto pinchIt = m_actionNameToHandle.find("TouchPinchDelta1D");
                if (pinchIt != m_actionNameToHandle.end())
                {
                    (void)injectInputForAction(pinchIt->second, InputValue(m_touchPinchDelta));
                }
            }

            void HIDSubsystem::processTouchSwipe(float startX, float startY, float currX, float currY, bool touchEnded)
            {
                if (touchEnded)
                {
                    m_touchSwipeDelta = FCT::Vec2{0.0f, 0.0f};
                }
                else
                {
                    m_touchSwipeDelta = FCT::Vec2{currX - startX, currY - startY};
                }

                auto it = m_actionNameToHandle.find("TouchSwipeDelta2D");
                if (it != m_actionNameToHandle.end())
                {
                    (void)injectInputForAction(it->second, InputValue(m_touchSwipeDelta));
                }
            }

            void HIDSubsystem::processTouchPinch(float dist0, float dist1, bool touchEnded)
            {
                if (touchEnded)
                {
                    m_touchPinchDelta = 0.0f;
                }
                else
                {
                    m_touchPinchDelta = dist1 - dist0;
                }

                auto it = m_actionNameToHandle.find("TouchPinchDelta1D");
                if (it != m_actionNameToHandle.end())
                {
                    (void)injectInputForAction(it->second, InputValue(m_touchPinchDelta));
                }
            }

        } // namespace HID
    } // namespace Domain
} // namespace MQEngine
