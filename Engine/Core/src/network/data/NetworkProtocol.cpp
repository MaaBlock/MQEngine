#include "NetworkProtocol.h"

namespace MQEngine {

    namespace {
        enum class WireComponentValueTag : uint8_t {
            Bool = 0,
            Int = 1,
            Float = 2,
            Double = 3,
            String = 4,
            Vec2 = 5,
            Vec3 = 6,
            Vec4 = 7,
            StringArray = 8
        };
    }

    NetworkByteWriter::NetworkByteWriter(size_t reserveBytes) {
        m_buffer.reserve(reserveBytes);
    }

    void NetworkByteWriter::writeBytes(const void* src, size_t len) {
        if (!src || len == 0) {
            return;
        }
        const auto* bytes = static_cast<const uint8_t*>(src);
        m_buffer.insert(m_buffer.end(), bytes, bytes + len);
    }

    void NetworkByteWriter::writeU8(uint8_t val) {
        m_buffer.push_back(val);
    }

    void NetworkByteWriter::writeBool(bool val) {
        m_buffer.push_back(val ? 1u : 0u);
    }

    void NetworkByteWriter::writeU16(uint16_t val) {
        m_buffer.push_back(static_cast<uint8_t>(val & 0xFFu));
        m_buffer.push_back(static_cast<uint8_t>((val >> 8u) & 0xFFu));
    }

    void NetworkByteWriter::writeU32(uint32_t val) {
        m_buffer.push_back(static_cast<uint8_t>(val & 0xFFu));
        m_buffer.push_back(static_cast<uint8_t>((val >> 8u) & 0xFFu));
        m_buffer.push_back(static_cast<uint8_t>((val >> 16u) & 0xFFu));
        m_buffer.push_back(static_cast<uint8_t>((val >> 24u) & 0xFFu));
    }

    void NetworkByteWriter::writeI32(int32_t val) {
        writeU32(static_cast<uint32_t>(val));
    }

    void NetworkByteWriter::writeU64(uint64_t val) {
        writeU32(static_cast<uint32_t>(val & 0xFFFFFFFFull));
        writeU32(static_cast<uint32_t>((val >> 32ull) & 0xFFFFFFFFull));
    }

    void NetworkByteWriter::writeF32(float val) {
        uint32_t bits = 0;
        std::memcpy(&bits, &val, sizeof(uint32_t));
        writeU32(bits);
    }

    void NetworkByteWriter::writeF64(double val) {
        uint64_t bits = 0;
        std::memcpy(&bits, &val, sizeof(uint64_t));
        writeU64(bits);
    }

    void NetworkByteWriter::writeString(std::string_view str) {
        const uint32_t len = static_cast<uint32_t>(str.size());
        writeU32(len);
        writeBytes(str.data(), len);
    }

    void NetworkByteWriter::writeVec2(const FCT::Vec2& v) {
        writeF32(v.x);
        writeF32(v.y);
    }

    void NetworkByteWriter::writeVec3(const FCT::Vec3<>& v) {
        writeF32(v.x);
        writeF32(v.y);
        writeF32(v.z);
    }

    void NetworkByteWriter::writeVec4(const FCT::Vec4& v) {
        writeF32(v.x);
        writeF32(v.y);
        writeF32(v.z);
        writeF32(v.w);
    }

    void NetworkByteWriter::writeStringArray(const std::vector<std::string>& arr) {
        writeU32(static_cast<uint32_t>(arr.size()));
        for (const auto& item : arr) {
            writeString(item);
        }
    }

    void NetworkByteWriter::writeComponentValue(const ComponentValue& val) {
        std::visit([this](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Bool));
                writeBool(v);
            } else if constexpr (std::is_same_v<T, int>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Int));
                writeI32(static_cast<int32_t>(v));
            } else if constexpr (std::is_same_v<T, float>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Float));
                writeF32(v);
            } else if constexpr (std::is_same_v<T, double>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Double));
                writeF64(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::String));
                writeString(v);
            } else if constexpr (std::is_same_v<T, FCT::Vec2>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Vec2));
                writeVec2(v);
            } else if constexpr (std::is_same_v<T, FCT::Vec3<>>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Vec3));
                writeVec3(v);
            } else if constexpr (std::is_same_v<T, FCT::Vec4>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::Vec4));
                writeVec4(v);
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                writeU8(static_cast<uint8_t>(WireComponentValueTag::StringArray));
                writeStringArray(v);
            }
        }, val);
    }

    void NetworkByteWriter::writeHeader(const NetworkPacketHeader& header) {
        writeU32(header.magic);
        writeU16(header.version);
        writeU8(static_cast<uint8_t>(header.type));
        writeU8(header.flags);
        writeU32(header.sequence);
        writeU32(header.senderClientId);
        writeU32(header.payloadSize);
    }

    void NetworkByteWriter::patchU32(size_t offset, uint32_t val) {
        if (offset + sizeof(uint32_t) > m_buffer.size()) {
            return;
        }
        m_buffer[offset] = static_cast<uint8_t>(val & 0xFFu);
        m_buffer[offset + 1] = static_cast<uint8_t>((val >> 8u) & 0xFFu);
        m_buffer[offset + 2] = static_cast<uint8_t>((val >> 16u) & 0xFFu);
        m_buffer[offset + 3] = static_cast<uint8_t>((val >> 24u) & 0xFFu);
    }

    NetworkByteReader::NetworkByteReader(const uint8_t* data, size_t size) noexcept
        : m_data(data), m_size(size), m_offset(0), m_failed(!data && size > 0) {}

    NetworkByteReader::NetworkByteReader(const std::vector<uint8_t>& buffer) noexcept
        : m_data(buffer.data()), m_size(buffer.size()), m_offset(0), m_failed(false) {}

    bool NetworkByteReader::readBytes(void* dst, size_t len) noexcept {
        if (m_failed || remaining() < len) {
            m_failed = true;
            return false;
        }
        if (len > 0 && dst) {
            std::memcpy(dst, m_data + m_offset, len);
        }
        m_offset += len;
        return true;
    }

    bool NetworkByteReader::readU8(uint8_t& out) noexcept {
        if (m_failed || remaining() < 1) {
            m_failed = true;
            return false;
        }
        out = m_data[m_offset++];
        return true;
    }

    bool NetworkByteReader::readBool(bool& out) noexcept {
        uint8_t raw = 0;
        if (!readU8(raw)) {
            return false;
        }
        out = (raw != 0);
        return true;
    }

    bool NetworkByteReader::readU16(uint16_t& out) noexcept {
        if (m_failed || remaining() < 2) {
            m_failed = true;
            return false;
        }
        out = static_cast<uint16_t>(m_data[m_offset]) |
              static_cast<uint16_t>(static_cast<uint16_t>(m_data[m_offset + 1]) << 8u);
        m_offset += 2;
        return true;
    }

    bool NetworkByteReader::readU32(uint32_t& out) noexcept {
        if (m_failed || remaining() < 4) {
            m_failed = true;
            return false;
        }
        out = static_cast<uint32_t>(m_data[m_offset]) |
              (static_cast<uint32_t>(m_data[m_offset + 1]) << 8u) |
              (static_cast<uint32_t>(m_data[m_offset + 2]) << 16u) |
              (static_cast<uint32_t>(m_data[m_offset + 3]) << 24u);
        m_offset += 4;
        return true;
    }

    bool NetworkByteReader::readI32(int32_t& out) noexcept {
        uint32_t raw = 0;
        if (!readU32(raw)) {
            return false;
        }
        out = static_cast<int32_t>(raw);
        return true;
    }

    bool NetworkByteReader::readU64(uint64_t& out) noexcept {
        uint32_t lo = 0;
        uint32_t hi = 0;
        if (!readU32(lo) || !readU32(hi)) {
            return false;
        }
        out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32ull);
        return true;
    }

    bool NetworkByteReader::readF32(float& out) noexcept {
        uint32_t bits = 0;
        if (!readU32(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(float));
        return true;
    }

    bool NetworkByteReader::readF64(double& out) noexcept {
        uint64_t bits = 0;
        if (!readU64(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(double));
        return true;
    }

    bool NetworkByteReader::readString(std::string& out, size_t maxLen) {
        uint32_t len = 0;
        if (!readU32(len)) {
            return false;
        }
        if (len > maxLen || remaining() < len) {
            m_failed = true;
            return false;
        }
        out.assign(reinterpret_cast<const char*>(m_data + m_offset), len);
        m_offset += len;
        return true;
    }

    bool NetworkByteReader::readVec2(FCT::Vec2& out) noexcept {
        return readF32(out.x) && readF32(out.y);
    }

    bool NetworkByteReader::readVec3(FCT::Vec3<>& out) noexcept {
        return readF32(out.x) && readF32(out.y) && readF32(out.z);
    }

    bool NetworkByteReader::readVec4(FCT::Vec4& out) noexcept {
        return readF32(out.x) && readF32(out.y) && readF32(out.z) && readF32(out.w);
    }

    bool NetworkByteReader::readStringArray(std::vector<std::string>& out, size_t maxCount) {
        uint32_t count = 0;
        if (!readU32(count) || count > maxCount) {
            m_failed = true;
            return false;
        }
        out.clear();
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            std::string item;
            if (!readString(item)) {
                return false;
            }
            out.push_back(std::move(item));
        }
        return true;
    }

    bool NetworkByteReader::readComponentValue(ComponentValue& out) {
        uint8_t rawTag = 0;
        if (!readU8(rawTag)) {
            return false;
        }
        switch (static_cast<WireComponentValueTag>(rawTag)) {
            case WireComponentValueTag::Bool: {
                bool v = false;
                if (!readBool(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::Int: {
                int32_t v = 0;
                if (!readI32(v)) return false;
                out = static_cast<int>(v);
                return true;
            }
            case WireComponentValueTag::Float: {
                float v = 0.0f;
                if (!readF32(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::Double: {
                double v = 0.0;
                if (!readF64(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::String: {
                std::string v;
                if (!readString(v)) return false;
                out = std::move(v);
                return true;
            }
            case WireComponentValueTag::Vec2: {
                FCT::Vec2 v;
                if (!readVec2(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::Vec3: {
                FCT::Vec3<> v;
                if (!readVec3(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::Vec4: {
                FCT::Vec4 v;
                if (!readVec4(v)) return false;
                out = v;
                return true;
            }
            case WireComponentValueTag::StringArray: {
                std::vector<std::string> v;
                if (!readStringArray(v)) return false;
                out = std::move(v);
                return true;
            }
            default:
                m_failed = true;
                return false;
        }
    }

    bool NetworkByteReader::readHeader(NetworkPacketHeader& out) noexcept {
        uint8_t rawType = 0;
        if (!readU32(out.magic) ||
            !readU16(out.version) ||
            !readU8(rawType) ||
            !readU8(out.flags) ||
            !readU32(out.sequence) ||
            !readU32(out.senderClientId) ||
            !readU32(out.payloadSize)) {
            return false;
        }
        out.type = static_cast<NetworkPacketType>(rawType);
        if (out.magic != NetworkWireConstants::kMagic ||
            out.version != NetworkWireConstants::kProtocolVersion) {
            m_failed = true;
            return false;
        }
        return true;
    }

} // namespace MQEngine
