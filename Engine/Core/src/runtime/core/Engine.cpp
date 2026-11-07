#include "Engine.h"
#include "CreateApplication.h"
#include "EngineGlobal.h"

namespace MQEngine
{
    CreateApplicationFn CreateApplication = nullptr;
    EngineGlobal g_engineGlobal;
}

namespace FCT
{
    std::string LoadStringFromStringResource(const unsigned char* resource, size_t size)
    {
        return std::string(reinterpret_cast<const char*>(resource), size);
    }
}

namespace MQEngine
{
    namespace Runtime
    {
        Engine* Engine::s_instance = nullptr;

        Engine::Engine() = default;
        Engine::~Engine() = default;
    } // namespace Runtime
} // namespace MQEngine
