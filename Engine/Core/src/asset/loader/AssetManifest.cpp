/**
 * @file AssetManifest.cpp
 * @brief 三级资产寻址清单协议与 FIPS 180-4 SHA-256 校验实现
 */
#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>

namespace MQEngine::Asset
{
    namespace
    {
        constexpr uint32_t kBinaryManifestMagic = 0x424D514Du; // "MQMB"

        constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        constexpr uint32_t rotr32(uint32_t x, uint32_t n) noexcept
        {
            return (x >> n) | (x << (32u - n));
        }

        void sha256TransformBlock(std::array<uint32_t, 8>& state, const uint8_t block[64]) noexcept
        {
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24u) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16u) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8u) |
                       (static_cast<uint32_t>(block[i * 4 + 3]));
            }
            for (int i = 16; i < 64; ++i)
            {
                const uint32_t s0 = rotr32(w[i - 15], 7u) ^ rotr32(w[i - 15], 18u) ^ (w[i - 15] >> 3u);
                const uint32_t s1 = rotr32(w[i - 2], 17u) ^ rotr32(w[i - 2], 19u) ^ (w[i - 2] >> 10u);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }

            uint32_t a = state[0];
            uint32_t b = state[1];
            uint32_t c = state[2];
            uint32_t d = state[3];
            uint32_t e = state[4];
            uint32_t f = state[5];
            uint32_t g = state[6];
            uint32_t h = state[7];

            for (int i = 0; i < 64; ++i)
            {
                const uint32_t S1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
                const uint32_t ch = (e & f) ^ ((~e) & g);
                const uint32_t temp1 = h + S1 + ch + kSha256RoundConstants[i] + w[i];
                const uint32_t S0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t temp2 = S0 + maj;

                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        bool parseUint32Field(std::string_view token, uint32_t& outValue) noexcept
        {
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            {
                token.remove_prefix(1);
            }
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
            {
                token.remove_suffix(1);
            }
            if (token.empty())
            {
                return false;
            }
            const char* begin = token.data();
            const char* end = token.data() + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, outValue, 10);
            return ec == std::errc{} && ptr == end;
        }

        bool hasPathTraversal(std::string_view virtualPath) noexcept
        {
            return virtualPath.find("..") != std::string_view::npos ||
                   virtualPath.find('\\') != std::string_view::npos;
        }
    } // namespace

    AssetManifest& AssetManifest::getInstance() noexcept
    {
        static AssetManifest s_instance;
        return s_instance;
    }

    void AssetManifest::setCdnBaseUrl(std::string_view url)
    {
        m_cdnBaseUrl = std::string(url);
        while (!m_cdnBaseUrl.empty() && m_cdnBaseUrl.back() == '/')
        {
            m_cdnBaseUrl.pop_back();
        }
    }

    void AssetManifest::registerRecord(ManifestRecord record)
    {
        if (record.virtualPath.empty())
        {
            return;
        }
        m_records[record.virtualPath] = std::move(record);
    }

    void AssetManifest::clear() noexcept
    {
        m_records.clear();
    }

    bool AssetManifest::hasEntry(std::string_view virtualPath) const noexcept
    {
        return m_records.find(std::string(virtualPath)) != m_records.end();
    }

    const ManifestRecord* AssetManifest::findRecord(std::string_view virtualPath) const noexcept
    {
        auto it = m_records.find(std::string(virtualPath));
        if (it != m_records.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    Status AssetManifest::loadFromBuffer(std::span<const uint8_t> buffer)
    {
        if (buffer.size() < 4)
        {
            return InvalidArgumentError("AssetManifest 缓冲区为空或长度不足");
        }

        // 支持二进制 MQMB 封装格式
        uint32_t magic = 0;
        std::memcpy(&magic, buffer.data(), sizeof(uint32_t));
        if (magic == kBinaryManifestMagic)
        {
            if (buffer.size() < 8)
            {
                return InvalidArgumentError("AssetManifest 二进制头部残缺");
            }
            uint32_t recordCount = 0;
            std::memcpy(&recordCount, buffer.data() + 4, sizeof(uint32_t));

            size_t offset = 8;
            for (uint32_t i = 0; i < recordCount; ++i)
            {
                if (offset + 12 > buffer.size())
                {
                    return InvalidArgumentError("AssetManifest 二进制记录截断");
                }
                uint16_t pathLen = 0;
                uint16_t hashLen = 0;
                uint32_t fileSize = 0;
                uint16_t assetFmt = 0;
                uint8_t bundleByte = 0;

                std::memcpy(&pathLen, buffer.data() + offset, 2);
                std::memcpy(&hashLen, buffer.data() + offset + 2, 2);
                std::memcpy(&fileSize, buffer.data() + offset + 4, 4);
                std::memcpy(&assetFmt, buffer.data() + offset + 8, 2);
                bundleByte = buffer[offset + 10];
                offset += 12;

                if (offset + pathLen + hashLen > buffer.size())
                {
                    return InvalidArgumentError("AssetManifest 字符串载荷越界");
                }

                ManifestRecord rec;
                rec.virtualPath.assign(reinterpret_cast<const char*>(buffer.data() + offset), pathLen);
                offset += pathLen;
                rec.sha256Hex.assign(reinterpret_cast<const char*>(buffer.data() + offset), hashLen);
                offset += hashLen;
                rec.fileSize = fileSize;
                rec.assetFormat = assetFmt;
                rec.bundleType = (bundleByte == 0) ? BundleType::FirstPackage : BundleType::RemoteCache;
                registerRecord(std::move(rec));
            }
            return OkStatus();
        }

        // 解析紧凑行协议 (# MQMF_V1 / <virtualPath>|<sha256Hex>|<size>|<bundleType>[|<assetFormat>])
        std::string_view text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        size_t parsedLines = 0;
        bool hasHeaderComment = false;
        size_t start = 0;

        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }
            std::string_view line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            if (line.starts_with("#"))
            {
                if (line.find("MQMF") != std::string_view::npos)
                {
                    hasHeaderComment = true;
                }
            }
            else if (!line.empty())
            {
                const size_t p1 = line.find('|');
                const size_t p2 = (p1 != std::string_view::npos) ? line.find('|', p1 + 1) : std::string_view::npos;
                if (p1 == std::string_view::npos || p2 == std::string_view::npos)
                {
                    return InvalidArgumentError("AssetManifest 包含非法记录行");
                }

                const size_t p3 = line.find('|', p2 + 1);
                const size_t p4 = (p3 != std::string_view::npos) ? line.find('|', p3 + 1) : std::string_view::npos;

                ManifestRecord rec;
                rec.virtualPath = std::string(line.substr(0, p1));
                rec.sha256Hex = std::string(line.substr(p1 + 1, p2 - p1 - 1));
                if (rec.virtualPath.empty() || hasPathTraversal(rec.virtualPath))
                {
                    return InvalidArgumentError("AssetManifest 包含非法虚拟路径");
                }

                const std::string_view sizeToken = (p3 != std::string_view::npos)
                    ? line.substr(p2 + 1, p3 - p2 - 1)
                    : line.substr(p2 + 1);
                if (!parseUint32Field(sizeToken, rec.fileSize))
                {
                    return InvalidArgumentError("AssetManifest 文件大小字段非合法无符号整数");
                }

                if (p3 != std::string_view::npos)
                {
                    const std::string_view bundleToken = (p4 != std::string_view::npos)
                        ? line.substr(p3 + 1, p4 - p3 - 1)
                        : line.substr(p3 + 1);
                    uint32_t bt = 0;
                    if (!parseUint32Field(bundleToken, bt) || bt > 1u)
                    {
                        return InvalidArgumentError("AssetManifest BundleType 字段越界");
                    }
                    rec.bundleType = static_cast<BundleType>(bt);
                }

                if (p4 != std::string_view::npos)
                {
                    uint32_t fmtVal = 0;
                    if (parseUint32Field(line.substr(p4 + 1), fmtVal))
                    {
                        rec.assetFormat = static_cast<uint16_t>(fmtVal);
                    }
                }

                registerRecord(std::move(rec));
                ++parsedLines;
            }
            start = end + 1;
        }

        if (parsedLines == 0 && !hasHeaderComment)
        {
            return InvalidArgumentError("AssetManifest 未解析到任何有效清单条目");
        }

        return OkStatus();
    }

    std::vector<uint8_t> AssetManifest::saveToBuffer() const
    {
        std::vector<const ManifestRecord*> sorted;
        sorted.reserve(m_records.size());
        for (const auto& [_, rec] : m_records)
        {
            sorted.push_back(&rec);
        }
        std::sort(sorted.begin(), sorted.end(), [](const ManifestRecord* a, const ManifestRecord* b) {
            return a->virtualPath < b->virtualPath;
        });

        std::string serialized = "# MQMF_V1\n";
        for (const ManifestRecord* rec : sorted)
        {
            serialized += StrCat(
                rec->virtualPath, "|",
                rec->sha256Hex, "|",
                rec->fileSize, "|",
                static_cast<uint32_t>(rec->bundleType), "|",
                static_cast<uint32_t>(rec->assetFormat), "\n");
        }

        return std::vector<uint8_t>(serialized.begin(), serialized.end());
    }

    Status AssetManifest::loadFromFile(std::string_view manifestPath)
    {
        if (manifestPath.empty())
        {
            return InvalidArgumentError("清单路径不能为空");
        }

        const std::string pathStr(manifestPath);
        if (auto* res = Resource::Resource::InstancePtr())
        {
            if (auto* loader = res->getResourceLoader())
            {
                auto contentOr = loader->loadFile(pathStr);
                if (contentOr.ok())
                {
                    std::span<const uint8_t> span(
                        reinterpret_cast<const uint8_t*>(contentOr->data()),
                        contentOr->size());
                    return loadFromBuffer(span);
                }
            }
        }

        if (!FCT::SAI::FileSystem::exists(pathStr))
        {
            return NotFoundError(StrCat("清单文件不存在: ", pathStr));
        }

        auto bytesOr = FCT::SAI::FileSystem::readBinaryFile(pathStr);
        if (!bytesOr.ok())
        {
            return bytesOr.status();
        }
        return loadFromBuffer(*bytesOr);
    }

    StatusOr<AssetResolveResult> AssetManifest::resolveAsset(std::string_view virtualPath) const
    {
        if (virtualPath.empty() || hasPathTraversal(virtualPath))
        {
            return InvalidArgumentError(StrCat("禁止目录穿越或空虚拟资产路径: ", virtualPath));
        }

        const ManifestRecord* rec = findRecord(virtualPath);
        if (!rec)
        {
            return NotFoundError(StrCat("资产未在 AssetManifest 登记: ", virtualPath));
        }

        AssetResolveResult result;
        if (rec->bundleType == BundleType::FirstPackage)
        {
            result.location = AssetResolveResult::Location::FirstPackagePkg;
            if (rec->virtualPath.starts_with("/pkg/"))
            {
                result.resolvedPath = rec->virtualPath;
            }
            else if (!rec->virtualPath.empty() && rec->virtualPath.front() == '/')
            {
                result.resolvedPath = StrCat("/pkg", rec->virtualPath);
            }
            else
            {
                result.resolvedPath = StrCat("/pkg/", rec->virtualPath);
            }
            return result;
        }

        const std::string cachePath = StrCat("/cache/", rec->sha256Hex, ".bin");
        if (FCT::SAI::FileSystem::exists(cachePath))
        {
            result.location = AssetResolveResult::Location::LocalCache;
            result.resolvedPath = cachePath;
            result.cacheDestPath = cachePath;
            return result;
        }

        result.location = AssetResolveResult::Location::RemoteCdn;
        result.resolvedPath = cachePath;
        result.downloadUrl = StrCat(m_cdnBaseUrl, "/", rec->sha256Hex, ".bin");
        result.cacheDestPath = cachePath;
        return result;
    }

    std::string AssetManifest::computeSha256Hex(std::span<const uint8_t> data)
    {
        std::array<uint32_t, 8> state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };

        const size_t fullBlocks = data.size() / 64u;
        for (size_t b = 0; b < fullBlocks; ++b)
        {
            sha256TransformBlock(state, data.data() + b * 64u);
        }

        uint8_t tail[128]{};
        const size_t rem = data.size() - fullBlocks * 64u;
        if (rem > 0)
        {
            std::memcpy(tail, data.data() + fullBlocks * 64u, rem);
        }
        tail[rem] = 0x80u;

        const size_t totalTailLen = (rem < 56u) ? 64u : 128u;
        const uint64_t totalBits = static_cast<uint64_t>(data.size()) * 8ULL;
        for (int i = 0; i < 8; ++i)
        {
            tail[totalTailLen - 1u - static_cast<size_t>(i)] =
                static_cast<uint8_t>((totalBits >> (static_cast<uint32_t>(i) * 8u)) & 0xFFu);
        }

        sha256TransformBlock(state, tail);
        if (totalTailLen == 128u)
        {
            sha256TransformBlock(state, tail + 64u);
        }

        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (uint32_t word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                hex.push_back(kHexDigits[(word >> static_cast<uint32_t>(shift)) & 0xFu]);
            }
        }
        return hex;
    }

    bool AssetManifest::verifySha256(std::span<const uint8_t> data, std::string_view expectedHex)
    {
        if (expectedHex.size() != 64u)
        {
            return false;
        }
        const std::string computed = computeSha256Hex(data);
        for (size_t i = 0; i < 64u; ++i)
        {
            const char a = computed[i];
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(expectedHex[i])));
            if (a != b)
            {
                return false;
            }
        }
        return true;
    }
}
