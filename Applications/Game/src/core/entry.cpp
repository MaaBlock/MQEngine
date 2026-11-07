#include "GameApplication.h"
#include "Engine/runtime/core/Runtime.h"

namespace MQEngine
{
    static Runtime::Application* CreateGameApplication()
    {
        return new GameApplication();
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
    }
}
