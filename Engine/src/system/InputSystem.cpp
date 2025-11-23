#include "InputSystem.h"
#include "../core/EngineGlobal.h"

namespace MQEngine {

    InputSystem::InputSystem() {
    }

    InputSystem::~InputSystem() {
        onDeactivate();
    }

    void InputSystem::updateLogic() {}
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
        m_keyState[key] = true;
    }

    void InputSystem::onKeyUp(FCT::Window* wnd, int key) {
        m_keyState[key] = false;
    }

    void InputSystem::onMouseMove(FCT::Window* wnd, int xpos, int ypos) {
        FCT::Vec2 pos(static_cast<float>(xpos), static_cast<float>(ypos));
        
        if (m_transformCallback) {
            m_transformCallback(pos);
        }
        
        m_mousePos = pos;
    }

    bool InputSystem::isKeyPressed(int key) const {
        auto it = m_keyState.find(key);
        return it != m_keyState.end() && it->second;
    }

    FCT::Vec2 InputSystem::getMousePosition() const {
        return m_mousePos;
    }

    void InputSystem::setMouseTransformCallback(std::function<void(FCT::Vec2&)> cb) {
        m_transformCallback = cb;
    }

} // MQEngine
