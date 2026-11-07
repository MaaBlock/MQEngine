#include "Input.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine
{
    namespace HID
    {
        Input::Input() = default;

        Input::~Input()
        {
            term();
        }

        void Input::registerInfo()
        {
            inputSystem = FCT_NEW(InputSystem);
            g_engineGlobal.inputSystem = inputSystem;
        }

        Status Input::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            assert(inputSystem != nullptr && "InputSystem 不能为空");
            g_engineGlobal.inputSystem = inputSystem;
            inputSystem->onActivate();

            m_initialized = true;
            return OkStatus();
        }

        void Input::term()
        {
            if (!m_initialized && !inputSystem) {
                return;
            }
            m_initialized = false;

            if (inputSystem) {
                inputSystem->onDeactivate();
                if (g_engineGlobal.inputSystem == inputSystem) {
                    g_engineGlobal.inputSystem = nullptr;
                }
                inputSystem->release();
                inputSystem = nullptr;
            }
        }

    } // namespace HID
} // namespace MQEngine
