#include "AndroidApplication.h"

namespace MQEngine
{
    static Runtime::Application* CreateAndroidApplication()
    {
        return new AndroidApplication();
    }

    class AndroidApplicationRegistrar
    {
    public:
        AndroidApplicationRegistrar()
        {
            Runtime::Runtime::RegisterApplicationFactory(CreateAndroidApplication);
        }
    };

    static AndroidApplicationRegistrar g_androidRegistrar;
}
