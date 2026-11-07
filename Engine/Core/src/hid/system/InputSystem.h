#ifndef INPUTSYSTEM_H
#define INPUTSYSTEM_H

#include "../../runtime/system/ISystem.h"
#include "../data/KeyCode.h"
#include "../data/RawInputEvent.h"
#include "../data/InputMapping.h"
#include "../manager/InputActionManager.h"
#include <FCT_Node/concurrentqueue.h>
#include <functional>
#include <string>

namespace MQEngine {

    class ENGINE_API InputSystem : public ISystem, public FCT::EventHandler {
    public:
        InputSystem();
        ~InputSystem() override;

        void updateLogic() override;
        void updateRender() override;
        void onActivate() override;
        void onDeactivate() override;

        // FCT::EventHandler 窗口回调
        void onResize(FCT::Window* wnd, int width, int height) override {}
        void onMouseMove(FCT::Window* wnd, int xpos, int ypos) override;
        void onMouseWheel(FCT::Window* wnd, int delta) override;
        void onLButtonDown(FCT::Window* wnd, int xpos, int ypos) override;
        void onLButtonUp(FCT::Window* wnd, int xpos, int ypos) override;
        void onRButtonDown(FCT::Window* wnd, int xpos, int ypos) override;
        void onRButtonUp(FCT::Window* wnd, int xpos, int ypos) override;
        void onKeyDown(FCT::Window* wnd, int key) override;
        void onKeyUp(FCT::Window* wnd, int key) override;
        void onFileDrop(FCT::Window* wnd, const std::vector<std::string>& files) override {}

        // 虚拟输入注入接口（用于无头测试、自动化及跨线程模拟输入）
        void injectKeyEvent(KeyCode key, bool isDown);
        void injectRawKeyEvent(int vkCode, bool isDown);
        void injectMouseMove(int x, int y);
        void injectMouseButton(MouseButton button, bool isDown);
        void injectMouseWheel(float delta);

        // Win32 事件无锁入队接口
        void pushRawKeyEvent(int vkCode, bool isDown);
        void pushRawMouseMoveEvent(int x, int y);
        void pushRawMouseButtonEvent(int button, bool isDown, int x, int y);
        void pushRawMouseWheelEvent(int delta);

        // 按键状态查询 (KeyCode 与别名)
        bool isKeyPressed(KeyCode key) const;
        bool isPressed(KeyCode key) const { return isKeyPressed(key); }
        bool isKeyJustPressed(KeyCode key) const;
        bool isJustPressed(KeyCode key) const { return isKeyJustPressed(key); }
        bool isKeyJustReleased(KeyCode key) const;
        bool isJustReleased(KeyCode key) const { return isKeyJustReleased(key); }

        // Win32 虚拟键码整型兼容查询
        bool isKeyPressed(int key) const;
        bool isPressed(int key) const { return isKeyPressed(key); }
        bool isJustPressed(int key) const;
        bool isJustReleased(int key) const;

        // 鼠标按键状态查询
        bool isMouseButtonPressed(MouseButton button) const;
        bool isMouseButtonJustPressed(MouseButton button) const;
        bool isMouseButtonJustReleased(MouseButton button) const;
        bool isMouseButtonPressed(int button) const;
        bool isLeftMousePressed() const;
        bool isRightMousePressed() const;

        // 鼠标位移与滚轮查询
        FCT::Vec2 getMousePosition() const;
        FCT::Vec2 getMouseDelta() const;
        float getMouseWheelDelta() const;
        void setMouseTransformCallback(std::function<void(FCT::Vec2&)> cb);

        // 动作映射门面方法 (Action Mapping Facade)
        void addActionMapping(const std::string& actionName, KeyCode key);
        void addActionMapping(const std::string& actionName, MouseButton button);
        void removeActionMapping(const std::string& actionName, KeyCode key);
        void clearActionMappings(const std::string& actionName);
        bool isActionPressed(const std::string& actionName) const;
        bool isActionJustPressed(const std::string& actionName) const;
        bool isActionJustReleased(const std::string& actionName) const;

        // 轴向映射门面方法 (Axis Mapping Facade)
        void addAxisMapping(const std::string& axisName, KeyCode key, float scale = 1.0f);
        void addAxisMapping(const std::string& axisName, MouseAxis mouseAxis, float scale = 1.0f);
        void removeAxisMapping(const std::string& axisName, KeyCode key);
        void removeAxisMapping(const std::string& axisName, MouseAxis mouseAxis);
        void clearAxisMappings(const std::string& axisName);
        float getAxis(const std::string& axisName) const;

        // 映射管理器与配置访问
        InputActionManager* getActionManager();
        const InputActionManager* getActionManager() const;
        InputMapping* getInputMapping();
        const InputMapping* getInputMapping() const;
        void setInputMapping(UniquePtr<InputMapping> mapping);

    private:
        UniquePtr<InputActionManager> m_actionManager;
        moodycamel::ConcurrentQueue<RawInputEvent> m_eventQueue;

        bool m_hasInitialMousePos = false;
        FCT::Vec2 m_mousePos{0.0f, 0.0f};
        FCT::Vec2 m_lastMousePos{0.0f, 0.0f};
        FCT::Vec2 m_mouseDelta{0.0f, 0.0f};
        float m_wheelDelta = 0.0f;
        std::function<void(FCT::Vec2&)> m_transformCallback;
    };

} // namespace MQEngine

#endif // INPUTSYSTEM_H
