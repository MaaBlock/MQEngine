//
// Created by MaaBlock on 2026/10/18.
//

#include "ApplicationRegistrar.h"
#include "LauncherApplication.h"


namespace MQEngine
{

    static Runtime::Application* CreateLauncherApplication()
    {
        return new LauncherApplication();
    }

    class LauncherApplicationRegistrar
    {
    public:
        LauncherApplicationRegistrar()
        {
            Runtime::Runtime::RegisterApplicationFactory(CreateLauncherApplication);
        }
    };
    static LauncherApplicationRegistrar g_launcherRegistrar;
}