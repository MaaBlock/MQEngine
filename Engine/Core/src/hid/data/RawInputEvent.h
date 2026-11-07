#ifndef MQENGINE_RAWINPUTEVENT_H
#define MQENGINE_RAWINPUTEVENT_H

#include <cstdint>
#include "KeyCode.h"

namespace MQEngine {

    enum class RawInputEventType : uint8_t {
        None = 0,
        KeyDown,
        KeyUp,
        MouseMove,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel
    };

    /**
     * @brief 原始输入事件紧凑结构体（平凡可复制 POD，适于无锁并发队列）
     */
    struct RawInputEvent {
        RawInputEventType type = RawInputEventType::None;
        uint8_t mouseButton = 0; // MouseButton 映射 (0: Left, 1: Right, 2: Middle, etc.)
        uint16_t keyCode = 0;    // KeyCode 或 Win32 VK 码
        int32_t x = 0;           // 鼠标绝对坐标 X
        int32_t y = 0;           // 鼠标绝对坐标 Y
        float wheelDelta = 0.0f; // 鼠标滚轮增量
    };

} // namespace MQEngine

#endif // MQENGINE_RAWINPUTEVENT_H
