//
// Created by MaaBlock on 2026/10/18.
//

#include "ApplicationRegistrar.h"
#include "LauncherApplication.h"


namespace MQEngine
{

    static Application* CreateLauncherApplication()
    {
        return new LauncherApplication();
    }

    class LauncherApplicationRegistrar
    {
    public:
        LauncherApplicationRegistrar()
        {
            Engine::RegisterApplicationFactory(CreateLauncherApplication);
        }
    };
    static LauncherApplicationRegistrar g_launcherRegistrar;
}