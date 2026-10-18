#include "AndroidApplication.h"

namespace MQEngine
{
    static Application* CreateAndroidApplication()
    {
        return new AndroidApplication();
    }

    class AndroidApplicationRegistrar
    {
    public:
        AndroidApplicationRegistrar()
        {
            Engine::RegisterApplicationFactory(CreateAndroidApplication);
        }
    };

    static AndroidApplicationRegistrar g_androidRegistrar;
}
