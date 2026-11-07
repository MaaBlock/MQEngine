#ifndef ENGINE_NETWORK_PROTOCOL_H
#define ENGINE_NETWORK_PROTOCOL_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../../scene/system/ComponentReflection.h"

namespace MQEngine {

    enum class NetworkRole : uint8_t {
        Offline = 0,
        Server = 1,
        Host = 2,
        Client = 3
    };

    enum class NetworkChannel : uint8_t {
        ReliableTcp = 0,
        UnreliableUdp = 1
    };

    enum class NetworkPacketType : uint8_t {
        HandshakeReq = 1,
        HandshakeAck = 2,
        UdpBind = 3,
        UdpBindAck = 4,
        Heartbeat = 5,
        HeartbeatAck = 6,
        Disconnect = 7,
        RpcMessage = 8,
        EntitySpawnBatch = 9,
        EntityDespawnBatch = 10,
        StateDeltaBatch = 11,
        ClientAuthorityDelta = 12,
        ReflectedFieldsDelta = 13
    };

    namespace NetworkWireConstants {
        inline constexpr uint32_t kMagic = 0x4D514E54u; // "MQNT"
        inline constexpr uint16_t kProtocolVersion = 1u;
        inline constexpr size_t kHeaderBytes = 20u;
        inline constexpr size_t kMaxTcpPayloadBytes = 4u * 1024u * 1024u;
        inline constexpr size_t kMaxUdpPayloadBytes = 1360u;
        inline constexpr size_t kMaxUdpDatagramBytes = kHeaderBytes + kMaxUdpPayloadBytes;
        inline constexpr int kServerClientId = 0;
        inline constexpr int kHostLocalClientId = 1;
        inline constexpr int kFirstRemoteClientId = 2;
    }

    namespace NetworkTransformDirtyFlags {
        inline constexpr uint8_t kPosition = 1u << 0;
        inline constexpr uint8_t kRotation = 1u << 1;
        inline constexpr uint8_t kScale = 1u << 2;
        inline constexpr uint8_t kAnimation = 1u << 3;
        inline constexpr uint8_t kCrossfade = 1u << 4;
        inline constexpr uint8_t kLayerAnim = 1u << 5;
    }

    struct NetworkPacketHeader {
        uint32_t magic = NetworkWireConstants::kMagic;
        uint16_t version = NetworkWireConstants::kProtocolVersion;
        NetworkPacketType type = NetworkPacketType::Heartbeat;
        uint8_t flags = 0;
        uint32_t sequence = 0;
        uint32_t senderClientId = 0;
        uint32_t payloadSize = 0;
    };

    struct InboundNetworkPacket {
        NetworkPacketHeader header;
        NetworkChannel channel = NetworkChannel::ReliableTcp;
        int sourceClientId = 0;
        double receiveLocalTime = 0.0;
        std::vector<uint8_t> payload;
    };

    enum class NetworkConnectionEventType : uint8_t {
        ConnectedToServer = 0,
        DisconnectedFromServer = 1,
        RemoteClientConnected = 2,
        RemoteClientDisconnected = 3
    };

    struct NetworkConnectionEvent {
        NetworkConnectionEventType type = NetworkConnectionEventType::DisconnectedFromServer;
        int clientId = 0;
        std::string reason;
    };

    /**
     * @brief 小端序网络二进制序列化写入器
     */
    class ENGINE_API NetworkByteWriter {
    public:
        NetworkByteWriter() = default;
        explicit NetworkByteWriter(size_t reserveBytes);

        void clear() noexcept { m_buffer.clear(); }
        void reserve(size_t bytes) { m_buffer.reserve(bytes); }

        size_t size() const noexcept { return m_buffer.size(); }
        const uint8_t* data() const noexcept { return m_buffer.data(); }
        const std::vector<uint8_t>& buffer() const noexcept { return m_buffer; }
        std::vector<uint8_t> takeBuffer() noexcept { return std::move(m_buffer); }

        void writeBytes(const void* src, size_t len);
        void writeU8(uint8_t val);
        void writeBool(bool val);
        void writeU16(uint16_t val);
        void writeU32(uint32_t val);
        void writeI32(int32_t val);
        void writeU64(uint64_t val);
        void writeF32(float val);
        void writeF64(double val);
        void writeString(std::string_view str);
        void writeVec2(const FCT::Vec2& v);
        void writeVec3(const FCT::Vec3<>& v);
        void writeVec4(const FCT::Vec4& v);
        void writeStringArray(const std::vector<std::string>& arr);
        void writeComponentValue(const ComponentValue& val);
        void writeHeader(const NetworkPacketHeader& header);
        void patchU32(size_t offset, uint32_t val);

    private:
        std::vector<uint8_t> m_buffer;
    };

    /**
     * @brief 带边界校验的小端序网络二进制反序列化读取器
     */
    class ENGINE_API NetworkByteReader {
    public:
        NetworkByteReader(const uint8_t* data, size_t size) noexcept;
        explicit NetworkByteReader(const std::vector<uint8_t>& buffer) noexcept;

        bool ok() const noexcept { return !m_failed; }
        size_t offset() const noexcept { return m_offset; }
        size_t remaining() const noexcept { return m_size >= m_offset ? (m_size - m_offset) : 0; }

        bool readBytes(void* dst, size_t len) noexcept;
        bool readU8(uint8_t& out) noexcept;
        bool readBool(bool& out) noexcept;
        bool readU16(uint16_t& out) noexcept;
        bool readU32(uint32_t& out) noexcept;
        bool readI32(int32_t& out) noexcept;
        bool readU64(uint64_t& out) noexcept;
        bool readF32(float& out) noexcept;
        bool readF64(double& out) noexcept;
        bool readString(std::string& out, size_t maxLen = 65536);
        bool readVec2(FCT::Vec2& out) noexcept;
        bool readVec3(FCT::Vec3<>& out) noexcept;
        bool readVec4(FCT::Vec4& out) noexcept;
        bool readStringArray(std::vector<std::string>& out, size_t maxCount = 1024);
        bool readComponentValue(ComponentValue& out);
        bool readHeader(NetworkPacketHeader& out) noexcept;

    private:
        const uint8_t* m_data = nullptr;
        size_t m_size = 0;
        size_t m_offset = 0;
        bool m_failed = false;
    };

} // namespace MQEngine

#endif // ENGINE_NETWORK_PROTOCOL_H
