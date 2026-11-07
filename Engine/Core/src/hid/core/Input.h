#ifndef MQENGINE_HID_CORE_INPUT_H
#define MQENGINE_HID_CORE_INPUT_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../system/InputSystem.h"

namespace MQEngine
{
    namespace HID
    {
        class ENGINE_API Input : public CoreEngineModule<Input>
        {
        public:
            Input();
            ~Input();

            Input(const Input&) = delete;
            Input& operator=(const Input&) = delete;

            void registerInfo();
            Status init();
            void term();

            InputSystem* getInputSystem() const noexcept { return inputSystem; }

        public:
            InputSystem* inputSystem = nullptr;

        private:
            bool m_initialized = false;
        };

    } // namespace HID
} // namespace MQEngine

#endif // MQENGINE_HID_CORE_INPUT_H
