#ifndef MQENGINE_HOST_MANAGER_RAWINPUTEVENT_H
#define MQENGINE_HOST_MANAGER_RAWINPUTEVENT_H

#include <MQEngine/EnginePCH.h>
#include <cstdint>

namespace MQEngine
{
    namespace Host
    {
        enum class RawInputEventType : uint8_t
        {
            KeyDown,
            KeyUp,
            MouseMove,
            MouseDown,
            MouseUp,
            MouseWheel,
            WindowResize,
            FileDrop
        };

        struct RawInputEvent
        {
            RawInputEventType type = RawInputEventType::KeyDown;
            uint32_t keyCode = 0;
            int32_t mouseX = 0;
            int32_t mouseY = 0;
            int32_t delta = 0;
            uint32_t mouseButton = 0;
            uint64_t timestamp = 0;
        };

        using InputEvent = RawInputEvent;

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_RAWINPUTEVENT_H
