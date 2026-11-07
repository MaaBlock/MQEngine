#include "Host.h"
#include "Engine.h"
#include "EngineGlobal.h"
#include "../interface/Application.h"

namespace MQEngine
{
    namespace Runtime
    {
        Host::Host() = default;

        Host::~Host()
        {
            term();
        }

        void Host::registerInfo()
        {
            systemManager.registerInfo();
            pluginManager.registerInfo();
            userCodeManager.registerInfo();
            logicTickManager.registerInfo();

            g_engineGlobal.systemManager = &systemManager;
        }

        Status Host::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            logicTickManager.resetExitRequest();
            systemManager.init();
            g_engineGlobal.systemManager = &systemManager;

            m_initialized = true;
            return OkStatus();
        }

        void Host::term()
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;

            userCodeManager.unloadAll();
            pluginManager.unloadAll();
            systemManager.term();

            if (g_engineGlobal.systemManager == &systemManager) {
                g_engineGlobal.systemManager = nullptr;
            }
        }

    } // namespace Runtime
} // namespace MQEngine
