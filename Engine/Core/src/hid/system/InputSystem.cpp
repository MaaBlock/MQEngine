#include "InputSystem.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine {

    InputSystem::InputSystem()
        : m_actionManager(makeUnique<InputActionManager>()) {
    }

    InputSystem::~InputSystem() {
        onDeactivate();
    }

    void InputSystem::updateLogic() {
        m_actionManager->beginFrameSnapshot();

        bool hasMouseMove = false;
        FCT::Vec2 latestMousePos = m_mousePos;
        float accumulatedWheelDelta = 0.0f;

        RawInputEvent evt;
        while (m_eventQueue.try_dequeue(evt)) {
            switch (evt.type) {
                case RawInputEventType::KeyDown:
                    m_actionManager->setKey(static_cast<KeyCode>(evt.keyCode), true);
                    break;
                case RawInputEventType::KeyUp:
                    m_actionManager->setKey(static_cast<KeyCode>(evt.keyCode), false);
                    break;
                case RawInputEventType::MouseMove: {
                    FCT::Vec2 pos(static_cast<float>(evt.x), static_cast<float>(evt.y));
                    if (m_transformCallback) {
                        m_transformCallback(pos);
                    }
                    latestMousePos = pos;
                    hasMouseMove = true;
                    if (!m_hasInitialMousePos) {
                        m_lastMousePos = latestMousePos;
                        m_hasInitialMousePos = true;
                    }
                    break;
                }
                case RawInputEventType::MouseButtonDown: {
                    auto btn = static_cast<MouseButton>(evt.mouseButton);
                    m_actionManager->setMouseButton(btn, true);
                    break;
                }
                case RawInputEventType::MouseButtonUp: {
                    auto btn = static_cast<MouseButton>(evt.mouseButton);
                    m_actionManager->setMouseButton(btn, false);
                    break;
                }
                case RawInputEventType::MouseWheel:
                    accumulatedWheelDelta += evt.wheelDelta;
                    break;
                default:
                    break;
            }
        }

        if (hasMouseMove) {
            m_mouseDelta = FCT::Vec2(latestMousePos.x - m_lastMousePos.x, latestMousePos.y - m_lastMousePos.y);
            m_lastMousePos = latestMousePos;
            m_mousePos = latestMousePos;
        } else {
            m_mouseDelta = FCT::Vec2(0.0f, 0.0f);
        }

        m_wheelDelta = accumulatedWheelDelta;

        m_actionManager->setMousePosition(m_mousePos);
        m_actionManager->setMouseDelta(m_mouseDelta);
        m_actionManager->setMouseWheelDelta(m_wheelDelta);
    }

    void InputSystem::updateRender() {}

    void InputSystem::onActivate() {
        if (g_engineGlobal.ctx) {
            const auto& windows = g_engineGlobal.ctx->getBindWindows();
            for (auto* wnd : windows) {
                wnd->registerHandler(this);
            }
        }
    }

    void InputSystem::onDeactivate() {
        if (g_engineGlobal.ctx) {
            const auto& windows = g_engineGlobal.ctx->getBindWindows();
            for (auto* wnd : windows) {
                wnd->unregisterHandler(this);
            }
        }
    }

    void InputSystem::onKeyDown(FCT::Window* wnd, int key) {
        KeyCode kc = win32VirtualKeyToKeyCode(key);
        if (kc == KeyCode::Unknown) {
            return;
        }
        pushRawKeyEvent(key, true);
    }

    void InputSystem::onKeyUp(FCT::Window* wnd, int key) {
        KeyCode kc = win32VirtualKeyToKeyCode(key);
        if (kc == KeyCode::Unknown) {
            return;
        }
        pushRawKeyEvent(key, false);
    }

    void InputSystem::onMouseMove(FCT::Window* wnd, int xpos, int ypos) {
        pushRawMouseMoveEvent(xpos, ypos);
    }

    void InputSystem::onMouseWheel(FCT::Window* wnd, int delta) {
        pushRawMouseWheelEvent(delta);
    }

    void InputSystem::onLButtonDown(FCT::Window* wnd, int xpos, int ypos) {
        pushRawMouseButtonEvent(0, true, xpos, ypos);
    }

    void InputSystem::onLButtonUp(FCT::Window* wnd, int xpos, int ypos) {
        pushRawMouseButtonEvent(0, false, xpos, ypos);
    }

    void InputSystem::onRButtonDown(FCT::Window* wnd, int xpos, int ypos) {
        pushRawMouseButtonEvent(1, true, xpos, ypos);
    }

    void InputSystem::onRButtonUp(FCT::Window* wnd, int xpos, int ypos) {
        pushRawMouseButtonEvent(1, false, xpos, ypos);
    }

    void InputSystem::injectKeyEvent(KeyCode key, bool isDown) {
        if (key == KeyCode::Unknown) {
            return;
        }
        RawInputEvent evt;
        evt.type = isDown ? RawInputEventType::KeyDown : RawInputEventType::KeyUp;
        evt.keyCode = static_cast<uint16_t>(key);
        m_eventQueue.enqueue(evt);
    }

    void InputSystem::injectRawKeyEvent(int vkCode, bool isDown) {
        KeyCode key = win32VirtualKeyToKeyCode(vkCode);
        if (key == KeyCode::Unknown) {
            return;
        }
        injectKeyEvent(key, isDown);
    }

    void InputSystem::injectMouseMove(int x, int y) {
        RawInputEvent evt;
        evt.type = RawInputEventType::MouseMove;
        evt.x = x;
        evt.y = y;
        m_eventQueue.enqueue(evt);
    }

    void InputSystem::injectMouseButton(MouseButton button, bool isDown) {
        RawInputEvent evt;
        evt.type = isDown ? RawInputEventType::MouseButtonDown : RawInputEventType::MouseButtonUp;
        evt.mouseButton = static_cast<uint8_t>(button);
        evt.keyCode = static_cast<uint16_t>(mouseButtonToKeyCode(button));
        m_eventQueue.enqueue(evt);
    }

    void InputSystem::injectMouseWheel(float delta) {
        RawInputEvent evt;
        evt.type = RawInputEventType::MouseWheel;
        evt.wheelDelta = delta;
        m_eventQueue.enqueue(evt);
    }

    void InputSystem::pushRawKeyEvent(int vkCode, bool isDown) {
        injectRawKeyEvent(vkCode, isDown);
    }

    void InputSystem::pushRawMouseMoveEvent(int x, int y) {
        injectMouseMove(x, y);
    }

    void InputSystem::pushRawMouseButtonEvent(int button, bool isDown, int x, int y) {
        injectMouseMove(x, y);
        injectMouseButton(static_cast<MouseButton>(button), isDown);
    }

    void InputSystem::pushRawMouseWheelEvent(int delta) {
        injectMouseWheel(static_cast<float>(delta));
    }

    bool InputSystem::isKeyPressed(KeyCode key) const {
        return m_actionManager->isKeyPressed(key);
    }

    bool InputSystem::isKeyJustPressed(KeyCode key) const {
        return m_actionManager->isKeyJustPressed(key);
    }

    bool InputSystem::isKeyJustReleased(KeyCode key) const {
        return m_actionManager->isKeyJustReleased(key);
    }

    bool InputSystem::isKeyPressed(int key) const {
        return isKeyPressed(win32VirtualKeyToKeyCode(key));
    }

    bool InputSystem::isJustPressed(int key) const {
        return isKeyJustPressed(win32VirtualKeyToKeyCode(key));
    }

    bool InputSystem::isJustReleased(int key) const {
        return isKeyJustReleased(win32VirtualKeyToKeyCode(key));
    }

    bool InputSystem::isMouseButtonPressed(MouseButton button) const {
        return m_actionManager->isMouseButtonPressed(button);
    }

    bool InputSystem::isMouseButtonJustPressed(MouseButton button) const {
        return m_actionManager->isMouseButtonJustPressed(button);
    }

    bool InputSystem::isMouseButtonJustReleased(MouseButton button) const {
        return m_actionManager->isMouseButtonJustReleased(button);
    }

    bool InputSystem::isMouseButtonPressed(int button) const {
        return isMouseButtonPressed(static_cast<MouseButton>(button));
    }

    bool InputSystem::isLeftMousePressed() const {
        return isMouseButtonPressed(MouseButton::Left);
    }

    bool InputSystem::isRightMousePressed() const {
        return isMouseButtonPressed(MouseButton::Right);
    }

    FCT::Vec2 InputSystem::getMousePosition() const {
        return m_actionManager->getMousePosition();
    }

    FCT::Vec2 InputSystem::getMouseDelta() const {
        return m_actionManager->getMouseDelta();
    }

    float InputSystem::getMouseWheelDelta() const {
        return m_actionManager->getMouseWheelDelta();
    }

    void InputSystem::setMouseTransformCallback(std::function<void(FCT::Vec2&)> cb) {
        m_transformCallback = cb;
    }

    void InputSystem::addActionMapping(const std::string& actionName, KeyCode key) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->addActionMapping(actionName, key);
        }
    }

    void InputSystem::addActionMapping(const std::string& actionName, MouseButton button) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->addActionMapping(actionName, button);
        }
    }

    void InputSystem::removeActionMapping(const std::string& actionName, KeyCode key) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->removeActionMapping(actionName, key);
        }
    }

    void InputSystem::clearActionMappings(const std::string& actionName) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->clearActionMappings(actionName);
        }
    }

    bool InputSystem::isActionPressed(const std::string& actionName) const {
        return m_actionManager ? m_actionManager->isActionPressed(actionName) : false;
    }

    bool InputSystem::isActionJustPressed(const std::string& actionName) const {
        return m_actionManager ? m_actionManager->isActionJustPressed(actionName) : false;
    }

    bool InputSystem::isActionJustReleased(const std::string& actionName) const {
        return m_actionManager ? m_actionManager->isActionJustReleased(actionName) : false;
    }

    void InputSystem::addAxisMapping(const std::string& axisName, KeyCode key, float scale) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->addAxisMapping(axisName, key, scale);
        }
    }

    void InputSystem::addAxisMapping(const std::string& axisName, MouseAxis mouseAxis, float scale) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->addAxisMapping(axisName, mouseAxis, scale);
        }
    }

    void InputSystem::removeAxisMapping(const std::string& axisName, KeyCode key) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->removeAxisMapping(axisName, key);
        }
    }

    void InputSystem::removeAxisMapping(const std::string& axisName, MouseAxis mouseAxis) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->removeAxisMapping(axisName, mouseAxis);
        }
    }

    void InputSystem::clearAxisMappings(const std::string& axisName) {
        if (m_actionManager && m_actionManager->getInputMapping()) {
            m_actionManager->getInputMapping()->clearAxisMappings(axisName);
        }
    }

    float InputSystem::getAxis(const std::string& axisName) const {
        return m_actionManager ? m_actionManager->getAxis(axisName) : 0.0f;
    }

    InputActionManager* InputSystem::getActionManager() {
        return m_actionManager.get();
    }

    const InputActionManager* InputSystem::getActionManager() const {
        return m_actionManager.get();
    }

    InputMapping* InputSystem::getInputMapping() {
        return m_actionManager ? m_actionManager->getInputMapping() : nullptr;
    }

    const InputMapping* InputSystem::getInputMapping() const {
        return m_actionManager ? m_actionManager->getInputMapping() : nullptr;
    }

    void InputSystem::setInputMapping(UniquePtr<InputMapping> mapping) {
        if (m_actionManager) {
            m_actionManager->setInputMapping(std::move(mapping));
        }
    }

} // namespace MQEngine
