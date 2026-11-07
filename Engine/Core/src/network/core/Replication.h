#ifndef MQENGINE_NETWORK_CORE_REPLICATION_H
#define MQENGINE_NETWORK_CORE_REPLICATION_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../manager/NetworkManager.h"
#include "../system/NetworkSyncSystem.h"

namespace MQEngine
{
    namespace Network
    {
        class ENGINE_API Replication : public CoreEngineModule<Replication>
        {
        public:
            Replication();
            ~Replication();

            Replication(const Replication&) = delete;
            Replication& operator=(const Replication&) = delete;

            void registerInfo();
            Status init();
            void term();

            NetworkManager* getNetworkManager() const noexcept { return const_cast<NetworkManager*>(&networkManager); }
            NetworkSyncSystem* getNetworkSyncSystem() const noexcept { return networkSyncSystem.get(); }

        public:
            NetworkManager networkManager;
            UniquePtr<NetworkSyncSystem> networkSyncSystem;

        private:
            bool m_initialized = false;
        };

    } // namespace Network
} // namespace MQEngine

#endif // MQENGINE_NETWORK_CORE_REPLICATION_H
