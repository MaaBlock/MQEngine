#ifndef ENGINE_NETWORK_MANAGER_H
#define ENGINE_NETWORK_MANAGER_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../thirdparty/thirdparty.h"
#include "../data/NetworkProtocol.h"
#include "../core/NetworkTransport.h"

namespace MQEngine {

    struct ScriptRpcEvent {
        int senderClientId = 0;
        std::string rpcName;
        std::string targetEntityUuid;
        std::string payloadJson;
    };

    /**
     * @brief 引擎级网络管理器，封装连接生命周期、双向 RPC 分发、时钟同步与脚本接口
     */
    class ENGINE_API NetworkManager : public CoreEngineManager<NetworkManager> {
    public:
        static constexpr size_t kMaxQueuedScriptRpcEvents = 512;

        using RpcHandler = std::function<void(
            int senderClientId,
            const std::string& targetEntityUuid,
            const std::string& payloadJson)>;

        using ConnectionCallback = std::function<void(const NetworkConnectionEvent&)>;

        NetworkManager();
        ~NetworkManager();

        NetworkManager(const NetworkManager&) = delete;
        NetworkManager& operator=(const NetworkManager&) = delete;

        void registerInfo() {}

        bool startServer(int port);
        bool startHost(int port);
        bool connectClient(const std::string& host, int port);
        void disconnect();
        void tick(float deltaTime);

        NetworkRole getRole() const noexcept;
        std::string getRoleName() const;
        bool isConnected() const noexcept;
        bool isServer() const noexcept;
        bool isClient() const noexcept;
        bool isHost() const noexcept;
        bool isServerAuthorityHost() const noexcept;
        bool isPureRemoteClient() const noexcept;

        int getLocalClientId() const noexcept;
        int getConnectedClientCount() const;
        std::vector<int> getConnectedClientIds() const;

        float getRttMs() const noexcept;
        double getLocalTimeSec() const noexcept;
        double getEstimatedServerTimeSec() const noexcept;
        uint64_t getBytesSentPerSec() const noexcept;
        uint64_t getBytesReceivedPerSec() const noexcept;

        void registerRpcHandler(const std::string& rpcName, RpcHandler handler);
        void unregisterRpcHandler(const std::string& rpcName);
        uint64_t addConnectionListener(ConnectionCallback callback);
        void removeConnectionListener(uint64_t listenerId);

        bool sendRpcToServer(
            const std::string& rpcName,
            const std::string& targetEntityUuid,
            const std::string& payloadJson,
            bool reliable = true);

        bool broadcastRpc(
            const std::string& rpcName,
            const std::string& targetEntityUuid,
            const std::string& payloadJson,
            bool reliable = true);

        bool sendRpcToClient(
            int clientId,
            const std::string& rpcName,
            const std::string& targetEntityUuid,
            const std::string& payloadJson,
            bool reliable = true);

        std::string pollRpcEventsJson();

        Status sendSyncPacketToServer(
            NetworkPacketType type,
            NetworkChannel channel,
            const std::vector<uint8_t>& payload);

        Status sendSyncPacketToClient(
            int clientId,
            NetworkPacketType type,
            NetworkChannel channel,
            const std::vector<uint8_t>& payload);

        Status broadcastSyncPacket(
            NetworkPacketType type,
            NetworkChannel channel,
            const std::vector<uint8_t>& payload,
            int excludeClientId = -1);

        void drainSyncPackets(std::vector<InboundNetworkPacket>& outPackets);
        void drainNewClientIds(std::vector<int>& outClientIds);
        void drainDisconnectedClientIds(std::vector<int>& outClientIds);

    private:
        void processConnectionEvents();
        void processInboundPackets();
        void handleInboundRpcPacket(const InboundNetworkPacket& packet);
        void dispatchLocalRpc(
            int senderClientId,
            const std::string& rpcName,
            const std::string& targetEntityUuid,
            const std::string& payloadJson);
        std::vector<uint8_t> encodeRpcPayload(
            int originClientId,
            const std::string& rpcName,
            const std::string& targetEntityUuid,
            const std::string& payloadJson) const;

        struct ListenerEntry {
            uint64_t id = 0;
            ConnectionCallback callback;
        };

        UniquePtr<NetworkTransport> m_transport;
        std::unordered_map<std::string, RpcHandler> m_rpcHandlers;
        std::deque<ScriptRpcEvent> m_scriptRpcQueue;

        uint64_t m_nextListenerId = 1;
        std::vector<ListenerEntry> m_connectionListeners;

        std::vector<NetworkConnectionEvent> m_polledEvents;
        std::vector<InboundNetworkPacket> m_polledPackets;
        std::vector<InboundNetworkPacket> m_pendingSyncPackets;
        std::vector<int> m_newClientIds;
        std::vector<int> m_disconnectedClientIds;
    };

} // namespace MQEngine

#endif // ENGINE_NETWORK_MANAGER_H
