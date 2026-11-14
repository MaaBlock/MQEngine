#include "GameApplication.h"
#include <MQEngine/runtime/core/Runtime.h>

namespace MQEngine
{
    static Runtime::Application* CreateGameApplication()
    {
        BindModuleSharedMemoryTracker();
        return FCT_NEW(GameApplication);
    }

    namespace
    {
        class GameApplicationRegistrar
        {
        public:
            GameApplicationRegistrar()
            {
                Runtime::Runtime::RegisterApplicationFactory(CreateGameApplication);
            }
        };
        static GameApplicationRegistrar g_gameRegistrar;
    } // namespace
} // namespace MQEngine
