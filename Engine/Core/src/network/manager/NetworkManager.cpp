#include "NetworkManager.h"

namespace MQEngine {

    NetworkManager::NetworkManager()
        : m_transport(makeUnique<NetworkTransport>()) {}

    NetworkManager::~NetworkManager() {
        disconnect();
    }

    bool NetworkManager::startServer(int port) {
        if (port <= 0 || port > 65535) {
            spdlog::error("NetworkManager::startServer 端口无效: {}", port);
            return false;
        }
        m_pendingSyncPackets.clear();
        m_newClientIds.clear();
        m_disconnectedClientIds.clear();
        m_scriptRpcQueue.clear();

        Status st = m_transport->startServer(static_cast<uint16_t>(port), false);
        if (!st.ok()) {
            spdlog::error("启动 Server 失败: {}", st.message());
            return false;
        }
        return true;
    }

    bool NetworkManager::startHost(int port) {
        if (port <= 0 || port > 65535) {
            spdlog::error("NetworkManager::startHost 端口无效: {}", port);
            return false;
        }
        m_pendingSyncPackets.clear();
        m_newClientIds.clear();
        m_disconnectedClientIds.clear();
        m_scriptRpcQueue.clear();

        Status st = m_transport->startServer(static_cast<uint16_t>(port), true);
        if (!st.ok()) {
            spdlog::error("启动 Host 失败: {}", st.message());
            return false;
        }
        return true;
    }

    bool NetworkManager::connectClient(const std::string& host, int port) {
        if (host.empty() || port <= 0 || port > 65535) {
            spdlog::error("NetworkManager::connectClient 参数无效 [{}:{}]", host, port);
            return false;
        }
        m_pendingSyncPackets.clear();
        m_newClientIds.clear();
        m_disconnectedClientIds.clear();
        m_scriptRpcQueue.clear();

        Status st = m_transport->connectClient(host, static_cast<uint16_t>(port));
        if (!st.ok()) {
            spdlog::error("连接服务器失败: {}", st.message());
            return false;
        }
        return true;
    }

    void NetworkManager::disconnect() {
        if (m_transport) {
            m_transport->stop();
        }
        m_pendingSyncPackets.clear();
        m_newClientIds.clear();
        m_disconnectedClientIds.clear();
    }

    void NetworkManager::tick(float deltaTime) {
        (void)deltaTime;
        ZoneScopedN("NetworkManager::tick");
        if (!m_transport || getRole() == NetworkRole::Offline) {
            return;
        }

        processConnectionEvents();
        processInboundPackets();

        TracyPlot("Net Sent (B/s)", static_cast<int64_t>(getBytesSentPerSec()));
        TracyPlot("Net Recv (B/s)", static_cast<int64_t>(getBytesReceivedPerSec()));
        if (isPureRemoteClient()) {
            TracyPlot("Net RTT (ms)", static_cast<double>(getRttMs()));
        }
    }

    NetworkRole NetworkManager::getRole() const noexcept {
        return m_transport ? m_transport->getRole() : NetworkRole::Offline;
    }

    std::string NetworkManager::getRoleName() const {
        switch (getRole()) {
            case NetworkRole::Server: return "Server";
            case NetworkRole::Host: return "Host";
            case NetworkRole::Client: return "Client";
            default: return "Offline";
        }
    }

    bool NetworkManager::isConnected() const noexcept {
        return m_transport && m_transport->isConnected();
    }

    bool NetworkManager::isServer() const noexcept {
        const NetworkRole r = getRole();
        return r == NetworkRole::Server || r == NetworkRole::Host;
    }

    bool NetworkManager::isClient() const noexcept {
        const NetworkRole r = getRole();
        return r == NetworkRole::Client || r == NetworkRole::Host;
    }

    bool NetworkManager::isHost() const noexcept {
        return getRole() == NetworkRole::Host;
    }

    bool NetworkManager::isServerAuthorityHost() const noexcept {
        return isServer();
    }

    bool NetworkManager::isPureRemoteClient() const noexcept {
        return getRole() == NetworkRole::Client;
    }

    int NetworkManager::getLocalClientId() const noexcept {
        return m_transport ? m_transport->getLocalClientId() : 0;
    }

    int NetworkManager::getConnectedClientCount() const {
        return m_transport ? static_cast<int>(m_transport->getConnectedClientCount()) : 0;
    }

    std::vector<int> NetworkManager::getConnectedClientIds() const {
        return m_transport ? m_transport->getConnectedClientIds() : std::vector<int>{};
    }

    float NetworkManager::getRttMs() const noexcept {
        return m_transport ? m_transport->getRttMs() : 0.0f;
    }

    double NetworkManager::getLocalTimeSec() const noexcept {
        return m_transport ? m_transport->getLocalTimeSec() : 0.0;
    }

    double NetworkManager::getEstimatedServerTimeSec() const noexcept {
        return m_transport ? m_transport->getEstimatedServerTimeSec() : 0.0;
    }

    uint64_t NetworkManager::getBytesSentPerSec() const noexcept {
        return m_transport ? m_transport->getBytesSentPerSec() : 0;
    }

    uint64_t NetworkManager::getBytesReceivedPerSec() const noexcept {
        return m_transport ? m_transport->getBytesReceivedPerSec() : 0;
    }

    void NetworkManager::registerRpcHandler(const std::string& rpcName, RpcHandler handler) {
        if (rpcName.empty() || !handler) {
            return;
        }
        m_rpcHandlers[rpcName] = std::move(handler);
    }

    void NetworkManager::unregisterRpcHandler(const std::string& rpcName) {
        m_rpcHandlers.erase(rpcName);
    }

    uint64_t NetworkManager::addConnectionListener(ConnectionCallback callback) {
        if (!callback) {
            return 0;
        }
        const uint64_t id = m_nextListenerId++;
        m_connectionListeners.push_back(ListenerEntry{id, std::move(callback)});
        return id;
    }

    void NetworkManager::removeConnectionListener(uint64_t listenerId) {
        m_connectionListeners.erase(
            std::remove_if(
                m_connectionListeners.begin(),
                m_connectionListeners.end(),
                [listenerId](const ListenerEntry& entry) { return entry.id == listenerId; }),
            m_connectionListeners.end());
    }

    bool NetworkManager::sendRpcToServer(
        const std::string& rpcName,
        const std::string& targetEntityUuid,
        const std::string& payloadJson,
        bool reliable) {
        if (rpcName.empty()) {
            return false;
        }
        if (isHost() || getRole() == NetworkRole::Server) {
            dispatchLocalRpc(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
            return true;
        }
        if (!isPureRemoteClient() || !isConnected()) {
            return false;
        }

        const auto encoded = encodeRpcPayload(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
        const NetworkChannel ch = reliable ? NetworkChannel::ReliableTcp : NetworkChannel::UnreliableUdp;
        return m_transport->sendToServer(NetworkPacketType::RpcMessage, ch, encoded.data(), encoded.size()).ok();
    }

    bool NetworkManager::broadcastRpc(
        const std::string& rpcName,
        const std::string& targetEntityUuid,
        const std::string& payloadJson,
        bool reliable) {
        if (rpcName.empty() || !isServer()) {
            return false;
        }

        if (isHost()) {
            dispatchLocalRpc(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
        }

        const auto encoded = encodeRpcPayload(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
        const NetworkChannel ch = reliable ? NetworkChannel::ReliableTcp : NetworkChannel::UnreliableUdp;
        return m_transport->broadcast(NetworkPacketType::RpcMessage, ch, encoded.data(), encoded.size()).ok();
    }

    bool NetworkManager::sendRpcToClient(
        int clientId,
        const std::string& rpcName,
        const std::string& targetEntityUuid,
        const std::string& payloadJson,
        bool reliable) {
        if (rpcName.empty() || !isServer()) {
            return false;
        }
        if (isHost() && clientId == getLocalClientId()) {
            dispatchLocalRpc(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
            return true;
        }

        const auto encoded = encodeRpcPayload(getLocalClientId(), rpcName, targetEntityUuid, payloadJson);
        const NetworkChannel ch = reliable ? NetworkChannel::ReliableTcp : NetworkChannel::UnreliableUdp;
        return m_transport->sendToClient(
            clientId,
            NetworkPacketType::RpcMessage,
            ch,
            encoded.data(),
            encoded.size()).ok();
    }

    std::string NetworkManager::pollRpcEventsJson() {
        if (m_scriptRpcQueue.empty()) {
            return "[]";
        }
        boost::json::array arr;
        arr.reserve(m_scriptRpcQueue.size());
        for (const auto& ev : m_scriptRpcQueue) {
            boost::json::object obj;
            obj["senderClientId"] = ev.senderClientId;
            obj["rpcName"] = ev.rpcName;
            obj["targetEntityUuid"] = ev.targetEntityUuid;
            obj["payloadJson"] = ev.payloadJson;
            arr.push_back(std::move(obj));
        }
        m_scriptRpcQueue.clear();
        return boost::json::serialize(arr);
    }

    Status NetworkManager::sendSyncPacketToServer(
        NetworkPacketType type,
        NetworkChannel channel,
        const std::vector<uint8_t>& payload) {
        return m_transport->sendToServer(type, channel, payload.data(), payload.size());
    }

    Status NetworkManager::sendSyncPacketToClient(
        int clientId,
        NetworkPacketType type,
        NetworkChannel channel,
        const std::vector<uint8_t>& payload) {
        return m_transport->sendToClient(clientId, type, channel, payload.data(), payload.size());
    }

    Status NetworkManager::broadcastSyncPacket(
        NetworkPacketType type,
        NetworkChannel channel,
        const std::vector<uint8_t>& payload,
        int excludeClientId) {
        return m_transport->broadcast(type, channel, payload.data(), payload.size(), excludeClientId);
    }

    void NetworkManager::drainSyncPackets(std::vector<InboundNetworkPacket>& outPackets) {
        outPackets.clear();
        outPackets.swap(m_pendingSyncPackets);
    }

    void NetworkManager::drainNewClientIds(std::vector<int>& outClientIds) {
        outClientIds.clear();
        outClientIds.swap(m_newClientIds);
    }

    void NetworkManager::drainDisconnectedClientIds(std::vector<int>& outClientIds) {
        outClientIds.clear();
        outClientIds.swap(m_disconnectedClientIds);
    }

    void NetworkManager::processConnectionEvents() {
        m_polledEvents.clear();
        m_transport->pollEvents(m_polledEvents);
        for (const auto& ev : m_polledEvents) {
            switch (ev.type) {
                case NetworkConnectionEventType::ConnectedToServer:
                    spdlog::info("已成功连接服务器，分配 ClientId = {}", ev.clientId);
                    break;
                case NetworkConnectionEventType::DisconnectedFromServer:
                    spdlog::info("与服务器断开连接: {}", ev.reason);
                    break;
                case NetworkConnectionEventType::RemoteClientConnected:
                    spdlog::info("远端客户端已连接: ClientId = {}", ev.clientId);
                    m_newClientIds.push_back(ev.clientId);
                    break;
                case NetworkConnectionEventType::RemoteClientDisconnected:
                    spdlog::info("远端客户端已断开: ClientId = {} ({})", ev.clientId, ev.reason);
                    m_disconnectedClientIds.push_back(ev.clientId);
                    break;
            }
            for (const auto& listener : m_connectionListeners) {
                if (listener.callback) {
                    listener.callback(ev);
                }
            }
        }
    }

    void NetworkManager::processInboundPackets() {
        m_polledPackets.clear();
        m_transport->pollPackets(m_polledPackets);
        for (auto& pkt : m_polledPackets) {
            if (pkt.header.type == NetworkPacketType::RpcMessage) {
                handleInboundRpcPacket(pkt);
            } else {
                m_pendingSyncPackets.push_back(std::move(pkt));
            }
        }
    }

    void NetworkManager::handleInboundRpcPacket(const InboundNetworkPacket& packet) {
        NetworkByteReader reader(packet.payload);
        int32_t encodedSender = 0;
        std::string rpcName;
        std::string targetEntityUuid;
        std::string payloadJson;
        if (!reader.readI32(encodedSender) ||
            !reader.readString(rpcName, 256) ||
            !reader.readString(targetEntityUuid, 256) ||
            !reader.readString(payloadJson, 512 * 1024)) {
            return;
        }
        const int actualSender = isServer() ? packet.sourceClientId : static_cast<int>(encodedSender);
        dispatchLocalRpc(actualSender, rpcName, targetEntityUuid, payloadJson);
    }

    void NetworkManager::dispatchLocalRpc(
        int senderClientId,
        const std::string& rpcName,
        const std::string& targetEntityUuid,
        const std::string& payloadJson) {
        auto it = m_rpcHandlers.find(rpcName);
        if (it != m_rpcHandlers.end() && it->second) {
            it->second(senderClientId, targetEntityUuid, payloadJson);
        }

        if (m_scriptRpcQueue.size() >= kMaxQueuedScriptRpcEvents) {
            m_scriptRpcQueue.pop_front();
        }
        m_scriptRpcQueue.push_back(ScriptRpcEvent{
            senderClientId,
            rpcName,
            targetEntityUuid,
            payloadJson
        });
    }

    std::vector<uint8_t> NetworkManager::encodeRpcPayload(
        int originClientId,
        const std::string& rpcName,
        const std::string& targetEntityUuid,
        const std::string& payloadJson) const {
        NetworkByteWriter writer(32 + rpcName.size() + targetEntityUuid.size() + payloadJson.size());
        writer.writeI32(originClientId);
        writer.writeString(rpcName);
        writer.writeString(targetEntityUuid);
        writer.writeString(payloadJson);
        return writer.takeBuffer();
    }

} // namespace MQEngine
