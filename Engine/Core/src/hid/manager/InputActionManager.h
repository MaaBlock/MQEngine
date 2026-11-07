#ifndef MQENGINE_INPUTACTIONMANAGER_H
#define MQENGINE_INPUTACTIONMANAGER_H

#include <bitset>
#include <array>
#include "../data/KeyCode.h"
#include "../data/InputMapping.h"
#include "../../thirdparty/thirdparty.h"

namespace MQEngine {

    struct InputFrameSnapshot {
        std::bitset<512> keysDown;
        std::bitset<512> keysJustPressed;
        std::bitset<512> keysJustReleased;

        std::array<bool, 5> mouseButtonsDown{false};
        std::array<bool, 5> mouseButtonsJustPressed{false};
        std::array<bool, 5> mouseButtonsJustReleased{false};

        FCT::Vec2 mousePosition{0.0f, 0.0f};
        FCT::Vec2 mouseDelta{0.0f, 0.0f};
        float mouseWheelDelta = 0.0f;

        void resetTransients() {
            keysJustPressed.reset();
            keysJustReleased.reset();
            mouseButtonsJustPressed.fill(false);
            mouseButtonsJustReleased.fill(false);
            mouseDelta = FCT::Vec2(0.0f, 0.0f);
            mouseWheelDelta = 0.0f;
        }
    };

    class ENGINE_API InputActionManager {
    public:
        InputActionManager();
        explicit InputActionManager(UniquePtr<InputMapping> mapping);
        ~InputActionManager() = default;

        // 帧快照翻转与状态更新
        void beginFrameSnapshot();
        void setKey(KeyCode key, bool isDown);
        void setMouseButton(MouseButton button, bool isDown);
        void setMousePosition(const FCT::Vec2& pos);
        void setMouseDelta(const FCT::Vec2& delta);
        void addMouseDelta(const FCT::Vec2& delta);
        void setMouseWheelDelta(float delta);
        void addMouseWheelDelta(float delta);

        // 动作状态查询 (Action Queries)
        bool isActionPressed(const std::string& actionName) const;
        bool isActionJustPressed(const std::string& actionName) const;
        bool isActionJustReleased(const std::string& actionName) const;

        // 轴向数值计算 (Axis Queries)
        float getAxis(const std::string& axisName) const;

        // 键码与鼠标直接状态查询
        bool isKeyPressed(KeyCode key) const;
        bool isKeyJustPressed(KeyCode key) const;
        bool isKeyJustReleased(KeyCode key) const;

        bool isMouseButtonPressed(MouseButton button) const;
        bool isMouseButtonJustPressed(MouseButton button) const;
        bool isMouseButtonJustReleased(MouseButton button) const;

        FCT::Vec2 getMousePosition() const;
        FCT::Vec2 getMouseDelta() const;
        float getMouseWheelDelta() const;

        InputMapping* getInputMapping();
        const InputMapping* getInputMapping() const;
        void setInputMapping(UniquePtr<InputMapping> mapping);

    private:
        UniquePtr<InputMapping> m_mapping;
        InputFrameSnapshot m_currentSnapshot;
        InputFrameSnapshot m_previousSnapshot;
    };

} // namespace MQEngine

#endif // MQENGINE_INPUTACTIONMANAGER_H
