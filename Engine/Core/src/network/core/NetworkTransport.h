#ifndef ENGINE_NETWORK_TRANSPORT_H
#define ENGINE_NETWORK_TRANSPORT_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../data/NetworkProtocol.h"

namespace MQEngine {

    /**
     * @brief 基于 Boost.Asio 的 TCP + UDP 双通道异步网络传输层（PIMPL 隔离 Asio 头文件）
     */
    class ENGINE_API NetworkTransport {
    public:
        NetworkTransport();
        ~NetworkTransport();

        NetworkTransport(const NetworkTransport&) = delete;
        NetworkTransport& operator=(const NetworkTransport&) = delete;

        Status startServer(uint16_t port, bool isHostMode);
        Status connectClient(const std::string& host, uint16_t port);
        void stop();

        NetworkRole getRole() const noexcept;
        bool isConnected() const noexcept;
        bool isUdpReady() const noexcept;
        int getLocalClientId() const noexcept;

        std::vector<int> getConnectedClientIds() const;
        size_t getConnectedClientCount() const;

        Status sendToServer(
            NetworkPacketType type,
            NetworkChannel channel,
            const uint8_t* payload,
            size_t payloadSize);

        Status sendToClient(
            int clientId,
            NetworkPacketType type,
            NetworkChannel channel,
            const uint8_t* payload,
            size_t payloadSize);

        Status broadcast(
            NetworkPacketType type,
            NetworkChannel channel,
            const uint8_t* payload,
            size_t payloadSize,
            int excludeClientId = -1);

        void pollPackets(std::vector<InboundNetworkPacket>& outPackets);
        void pollEvents(std::vector<NetworkConnectionEvent>& outEvents);

        double getLocalTimeSec() const noexcept;
        double getEstimatedServerTimeSec() const noexcept;
        float getRttMs() const noexcept;
        uint64_t getBytesSentPerSec() const noexcept;
        uint64_t getBytesReceivedPerSec() const noexcept;
        uint64_t getTotalBytesSent() const noexcept;
        uint64_t getTotalBytesReceived() const noexcept;

    private:
        struct Impl;
        UniquePtr<Impl> m_impl;
    };

} // namespace MQEngine

#endif // ENGINE_NETWORK_TRANSPORT_H
