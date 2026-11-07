#include "InputActionManager.h"
#include <cmath>
#include <algorithm>

namespace MQEngine {

    InputActionManager::InputActionManager()
        : m_mapping(makeUnique<InputMapping>()) {
    }

    InputActionManager::InputActionManager(UniquePtr<InputMapping> mapping)
        : m_mapping(mapping ? std::move(mapping) : makeUnique<InputMapping>()) {
    }

    void InputActionManager::beginFrameSnapshot() {
        m_previousSnapshot = m_currentSnapshot;
        m_currentSnapshot.resetTransients();
    }

    void InputActionManager::setKey(KeyCode key, bool isDown) {
        uint16_t idx = static_cast<uint16_t>(key);
        if (idx >= 512) {
            return;
        }

        if (isDown) {
            if (!m_currentSnapshot.keysDown.test(idx)) {
                m_currentSnapshot.keysJustPressed.set(idx);
            }
            m_currentSnapshot.keysDown.set(idx);
        } else {
            if (m_currentSnapshot.keysDown.test(idx) || m_currentSnapshot.keysJustPressed.test(idx)) {
                m_currentSnapshot.keysJustReleased.set(idx);
            }
            m_currentSnapshot.keysDown.reset(idx);
        }
    }

    void InputActionManager::setMouseButton(MouseButton button, bool isDown) {
        uint8_t btnIdx = static_cast<uint8_t>(button);
        if (btnIdx >= 5) {
            return;
        }

        if (isDown) {
            if (!m_currentSnapshot.mouseButtonsDown[btnIdx]) {
                m_currentSnapshot.mouseButtonsJustPressed[btnIdx] = true;
            }
            m_currentSnapshot.mouseButtonsDown[btnIdx] = true;
        } else {
            if (m_currentSnapshot.mouseButtonsDown[btnIdx] || m_currentSnapshot.mouseButtonsJustPressed[btnIdx]) {
                m_currentSnapshot.mouseButtonsJustReleased[btnIdx] = true;
            }
            m_currentSnapshot.mouseButtonsDown[btnIdx] = false;
        }

        KeyCode key = mouseButtonToKeyCode(button);
        if (key != KeyCode::Unknown) {
            setKey(key, isDown);
        }
    }

    void InputActionManager::setMousePosition(const FCT::Vec2& pos) {
        m_currentSnapshot.mousePosition = pos;
    }

    void InputActionManager::setMouseDelta(const FCT::Vec2& delta) {
        m_currentSnapshot.mouseDelta = delta;
    }

    void InputActionManager::addMouseDelta(const FCT::Vec2& delta) {
        m_currentSnapshot.mouseDelta.x += delta.x;
        m_currentSnapshot.mouseDelta.y += delta.y;
    }

    void InputActionManager::setMouseWheelDelta(float delta) {
        m_currentSnapshot.mouseWheelDelta = delta;
    }

    void InputActionManager::addMouseWheelDelta(float delta) {
        m_currentSnapshot.mouseWheelDelta += delta;
    }

    bool InputActionManager::isActionPressed(const std::string& actionName) const {
        if (!m_mapping) {
            return false;
        }
        const ActionMapping* mapping = m_mapping->findActionMapping(actionName);
        if (!mapping) {
            return false;
        }
        for (KeyCode key : mapping->keys) {
            if (isKeyPressed(key)) {
                return true;
            }
        }
        return false;
    }

    bool InputActionManager::isActionJustPressed(const std::string& actionName) const {
        if (!m_mapping) {
            return false;
        }
        const ActionMapping* mapping = m_mapping->findActionMapping(actionName);
        if (!mapping) {
            return false;
        }

        // 检查前一帧是否已被激活（若前一帧已激活，动作保持激活，不触发上升沿跃迁）
        bool wasActiveInPreviousFrame = false;
        for (KeyCode key : mapping->keys) {
            uint16_t idx = static_cast<uint16_t>(key);
            if (idx < 512 && m_previousSnapshot.keysDown.test(idx)) {
                wasActiveInPreviousFrame = true;
                break;
            }
        }
        if (wasActiveInPreviousFrame) {
            return false;
        }

        // 前一帧未激活，当前帧若有按键被按下或发生刚按下事件，则触发刚按下
        for (KeyCode key : mapping->keys) {
            uint16_t idx = static_cast<uint16_t>(key);
            if (idx < 512 && (m_currentSnapshot.keysDown.test(idx) || m_currentSnapshot.keysJustPressed.test(idx))) {
                return true;
            }
        }
        return false;
    }

    bool InputActionManager::isActionJustReleased(const std::string& actionName) const {
        if (!m_mapping) {
            return false;
        }
        const ActionMapping* mapping = m_mapping->findActionMapping(actionName);
        if (!mapping) {
            return false;
        }

        // 若当前动作仍处于按下状态（有其他绑定键维持），则动作尚未释放
        if (isActionPressed(actionName)) {
            return false;
        }

        // 检查前一帧是否处于激活状态
        bool wasActiveInPreviousFrame = false;
        for (KeyCode key : mapping->keys) {
            uint16_t idx = static_cast<uint16_t>(key);
            if (idx < 512 && m_previousSnapshot.keysDown.test(idx)) {
                wasActiveInPreviousFrame = true;
                break;
            }
        }
        if (wasActiveInPreviousFrame) {
            return true;
        }

        // 针对单帧瞬态按下又释放的情况，检查是否有按键刚被释放
        for (KeyCode key : mapping->keys) {
            uint16_t idx = static_cast<uint16_t>(key);
            if (idx < 512 && m_currentSnapshot.keysJustReleased.test(idx)) {
                return true;
            }
        }
        return false;
    }

    float InputActionManager::getAxis(const std::string& axisName) const {
        if (!m_mapping) {
            return 0.0f;
        }
        const AxisMapping* mapping = m_mapping->findAxisMapping(axisName);
        if (!mapping) {
            return 0.0f;
        }

        float rawValue = 0.0f;

        // 1. 累加按键绑定贡献
        for (const auto& binding : mapping->keyBindings) {
            if (isKeyPressed(binding.key)) {
                rawValue += binding.scale;
            }
        }

        // 2. 累加鼠标轴绑定贡献
        for (const auto& binding : mapping->mouseBindings) {
            if (binding.axis == MouseAxis::MouseX) {
                rawValue += m_currentSnapshot.mouseDelta.x * binding.scale;
            } else if (binding.axis == MouseAxis::MouseY) {
                rawValue += m_currentSnapshot.mouseDelta.y * binding.scale;
            } else if (binding.axis == MouseAxis::MouseWheel) {
                rawValue += m_currentSnapshot.mouseWheelDelta * binding.scale;
            }
        }

        // 3. 死区过滤
        if (mapping->deadzone > 0.0f && std::fabs(rawValue) < mapping->deadzone) {
            return 0.0f;
        }

        // 4. 灵敏度放缩
        rawValue *= mapping->sensitivity;

        // 5. 范围限制截断
        if (mapping->clamp) {
            rawValue = std::clamp(rawValue, mapping->clampMin, mapping->clampMax);
        }

        return rawValue;
    }

    bool InputActionManager::isKeyPressed(KeyCode key) const {
        uint16_t idx = static_cast<uint16_t>(key);
        return idx < 512 && m_currentSnapshot.keysDown.test(idx);
    }

    bool InputActionManager::isKeyJustPressed(KeyCode key) const {
        uint16_t idx = static_cast<uint16_t>(key);
        return idx < 512 && m_currentSnapshot.keysJustPressed.test(idx);
    }

    bool InputActionManager::isKeyJustReleased(KeyCode key) const {
        uint16_t idx = static_cast<uint16_t>(key);
        return idx < 512 && m_currentSnapshot.keysJustReleased.test(idx);
    }

    bool InputActionManager::isMouseButtonPressed(MouseButton button) const {
        uint8_t idx = static_cast<uint8_t>(button);
        return idx < 5 && m_currentSnapshot.mouseButtonsDown[idx];
    }

    bool InputActionManager::isMouseButtonJustPressed(MouseButton button) const {
        uint8_t idx = static_cast<uint8_t>(button);
        return idx < 5 && m_currentSnapshot.mouseButtonsJustPressed[idx];
    }

    bool InputActionManager::isMouseButtonJustReleased(MouseButton button) const {
        uint8_t idx = static_cast<uint8_t>(button);
        return idx < 5 && m_currentSnapshot.mouseButtonsJustReleased[idx];
    }

    FCT::Vec2 InputActionManager::getMousePosition() const {
        return m_currentSnapshot.mousePosition;
    }

    FCT::Vec2 InputActionManager::getMouseDelta() const {
        return m_currentSnapshot.mouseDelta;
    }

    float InputActionManager::getMouseWheelDelta() const {
        return m_currentSnapshot.mouseWheelDelta;
    }

    InputMapping* InputActionManager::getInputMapping() {
        return m_mapping.get();
    }

    const InputMapping* InputActionManager::getInputMapping() const {
        return m_mapping.get();
    }

    void InputActionManager::setInputMapping(UniquePtr<InputMapping> mapping) {
        m_mapping = mapping ? std::move(mapping) : makeUnique<InputMapping>();
    }

} // namespace MQEngine
