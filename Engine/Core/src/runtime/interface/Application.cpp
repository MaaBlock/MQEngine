#include <MQEngine/runtime/interface/Application.h>
#include <MQEngine/runtime/core/Engine.h>

namespace MQEngine
{
    namespace Runtime
    {
        void Application::registerInfoImpl()
        {
            m_engine = Engine::getInstance();
        }
    } // namespace Runtime
} // namespace MQEngine
