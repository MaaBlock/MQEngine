#ifndef INPUTSYSTEM_H
#define INPUTSYSTEM_H

#include "ISystem.h"
#include <unordered_map>
#include <functional>

namespace MQEngine {

    class ENGINE_API InputSystem : public ISystem, public FCT::EventHandler {
    public:
        InputSystem();
        ~InputSystem() override;

        void updateLogic() override;
        void updateRender() override;
        void onActivate() override;
        void onDeactivate() override;

        // FCT::EventHandler overrides
        void onResize(FCT::Window* wnd, int width, int height) override {}
        void onMouseMove(FCT::Window* wnd, int xpos, int ypos) override;
        void onMouseWheel(FCT::Window* wnd, int delta) override {}
        void onLButtonDown(FCT::Window* wnd, int xpos, int ypos) override {}
        void onLButtonUp(FCT::Window* wnd, int xpos, int ypos) override {}
        void onRButtonDown(FCT::Window* wnd, int xpos, int ypos) override {}
        void onRButtonUp(FCT::Window* wnd, int xpos, int ypos) override {}
        void onKeyDown(FCT::Window* wnd, int key) override;
        void onKeyUp(FCT::Window* wnd, int key) override;
        void onFileDrop(FCT::Window* wnd, const std::vector<std::string>& files) override {}

        bool isKeyPressed(int key) const;
        FCT::Vec2 getMousePosition() const;
        
        /**
         * @brief 设置鼠标位置变换回调
         * @param cb 回调函数，接收坐标引用，可修改为变换后的坐标
         */
        void setMouseTransformCallback(std::function<void(FCT::Vec2&)> cb);

    private:
        std::unordered_map<int, bool> m_keyState;
        FCT::Vec2 m_mousePos;
        std::function<void(FCT::Vec2&)> m_transformCallback;
    };

} // MQEngine

#endif //INPUTSYSTEM_H
