#include "Replication.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include <cassert>

namespace MQEngine
{
    namespace Network
    {
        Replication::Replication() = default;

        Replication::~Replication()
        {
            term();
        }

        void Replication::registerInfo()
        {
            networkManager.registerInfo();
            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            networkSyncSystem = makeUnique<NetworkSyncSystem>(engine->scene->getDataManager(), &networkManager);

            g_engineGlobal.networkManager = &networkManager;
            g_engineGlobal.networkSyncSystem = networkSyncSystem.get();
        }

        Status Replication::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            assert(networkSyncSystem != nullptr && "NetworkSyncSystem 不能为空");

            g_engineGlobal.networkManager = &networkManager;
            g_engineGlobal.networkSyncSystem = networkSyncSystem.get();

            engine->host->getSystemManager()->requestAddSystem("NetworkSyncSystem", networkSyncSystem.get());

            m_initialized = true;
            return OkStatus();
        }

        void Replication::term()
        {
            if (!m_initialized && !networkSyncSystem) {
                return;
            }
            m_initialized = false;

            if (g_engineGlobal.networkSyncSystem == networkSyncSystem.get()) {
                g_engineGlobal.networkSyncSystem = nullptr;
            }
            networkSyncSystem.reset();

            networkManager.disconnect();
            if (g_engineGlobal.networkManager == &networkManager) {
                g_engineGlobal.networkManager = nullptr;
            }
        }

    } // namespace Network
} // namespace MQEngine
