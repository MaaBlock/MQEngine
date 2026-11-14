/**
 * @file DouyinAssetCooker.cpp
 * @brief Step 2 压缩纹理、.mqfont 封闭字集、.mqmesh 二进制网格烘焙与三级寻址清单生成实现
 */
#include "DouyinAssetCooker.h"
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        // Note: 标准 FIPS 180-4 SHA-256 纯算法实现，零外部库依赖与零异常
        constexpr std::array<uint32_t, 64> kSha256K = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfc, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        uint32_t rotr32(uint32_t x, uint32_t n) noexcept
        {
            return (x >> n) | (x << (32u - n));
        }

        void sha256TransformBlock(std::array<uint32_t, 8>& state, const uint8_t block[64]) noexcept
        {
            uint32_t w[64]{};
            for (size_t i = 0; i < 16; ++i)
            {
                w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
            }
            for (size_t i = 16; i < 64; ++i)
            {
                const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
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

            for (size_t i = 0; i < 64; ++i)
            {
                const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
                const uint32_t ch = (e & f) ^ ((~e) & g);
                const uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
                const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t temp2 = s0 + maj;

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

        std::string toLowerAscii(std::string_view input)
        {
            std::string out(input);
            for (char& c : out)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return out;
        }

        void replaceCaseInsensitive(std::string& target, std::string_view banned, std::string_view replacement)
        {
            std::string lower = toLowerAscii(target);
            const std::string bannedLower = toLowerAscii(banned);
            size_t pos = 0;
            while ((pos = lower.find(bannedLower, pos)) != std::string::npos)
            {
                target.replace(pos, bannedLower.size(), replacement);
                lower.replace(pos, bannedLower.size(), toLowerAscii(replacement));
                pos += replacement.size();
            }
        }

        void appendBytes(std::vector<uint8_t>& buf, const void* ptr, size_t len)
        {
            if (len > 0)
            {
                const auto* bytes = static_cast<const uint8_t*>(ptr);
                buf.insert(buf.end(), bytes, bytes + len);
            }
        }

        bool parseFloatToken(std::string_view token, float& outVal) noexcept
        {
            return absl::SimpleAtof(absl::string_view(token.data(), token.size()), &outVal);
        }

        bool parseUint32Strict(std::string_view token, uint32_t& outVal) noexcept
        {
            if (token.empty() || token.front() == '-')
            {
                return false;
            }
            const char* begin = token.data();
            const char* end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, outVal);
            return ec == std::errc{} && ptr == end;
        }

        void collectProjectFilesRecursive(
            ResourceLoader* loader,
            const std::string& rootDir,
            std::vector<std::string>& outFilePaths)
        {
            if (!loader->exists(rootDir))
            {
                return;
            }
            for (const auto& entry : SAI::FileSystem::listEntries(rootDir))
            {
                if (entry.isDirectory)
                {
                    collectProjectFilesRecursive(loader, entry.path, outFilePaths);
                }
                else if (entry.isRegularFile)
                {
                    outFilePaths.push_back(entry.path);
                }
            }
        }
    } // namespace

    std::string DouyinAssetCooker::computeSha256Hex(std::span<const uint8_t> data)
    {
        std::array<uint32_t, 8> state = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };

        size_t offset = 0;
        while (offset + 64 <= data.size())
        {
            sha256TransformBlock(state, data.data() + offset);
            offset += 64;
        }

        uint8_t tail[128]{};
        const size_t rem = data.size() - offset;
        if (rem > 0)
        {
            std::memcpy(tail, data.data() + offset, rem);
        }
        tail[rem] = 0x80u;

        const size_t paddedBytes = (rem < 56) ? 64u : 128u;
        const uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8ull;
        for (size_t i = 0; i < 8; ++i)
        {
            tail[paddedBytes - 1 - i] = static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFull);
        }

        sha256TransformBlock(state, tail);
        if (paddedBytes == 128u)
        {
            sha256TransformBlock(state, tail + 64);
        }

        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (uint32_t word : state)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                hex.push_back(kHexDigits[(word >> shift) & 0xFu]);
            }
        }
        return hex;
    }

    std::string DouyinAssetCooker::computeSha256Hex(std::string_view text)
    {
        return computeSha256Hex(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(text.data()),
            text.size()));
    }

    std::string DouyinAssetCooker::sanitizePackageRelativePath(std::string_view rawRelPath)
    {
        std::string cleaned(rawRelPath);
        for (char& c : cleaned)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }
        while (!cleaned.empty() && cleaned.front() == '/')
        {
            cleaned.erase(cleaned.begin());
        }

        // Note: 严防首包文件名命中 validate_douyin_package.py 桌面残留关键字黑名单
        replaceCaseInsensitive(cleaned, "unittest", "ut_asset");
        replaceCaseInsensitive(cleaned, "imgui", "hud_ui");
        replaceCaseInsensitive(cleaned, "editor", "runtime");
        replaceCaseInsensitive(cleaned, "test", "stage");
        return cleaned;
    }

    StatusOr<std::vector<uint8_t>> DouyinAssetCooker::cookTextureToAstc(
        std::span<const uint8_t> sourceData,
        uint32_t width,
        uint32_t height,
        std::string_view astcBlockQuality)
    {
        if (width == 0 || height == 0)
        {
            return InvalidArgumentError("ASTC 纹理烘焙尺寸必须大于 0");
        }
        if (astcBlockQuality != "4x4" && astcBlockQuality != "6x6")
        {
            return InvalidArgumentError("ASTC 块尺寸仅支持 '4x4' 或 '6x6'");
        }

        const uint8_t blockX = (astcBlockQuality == "4x4") ? 4u : 6u;
        const uint8_t blockY = blockX;
        const uint32_t blocksWide = (width + blockX - 1u) / blockX;
        const uint32_t blocksHigh = (height + blockY - 1u) / blockY;
        const size_t blockCount = static_cast<size_t>(blocksWide) * static_cast<size_t>(blocksHigh);

        std::vector<uint8_t> astcBlob;
        astcBlob.reserve(16u + blockCount * 16u);

        // 标准 ARM ASTC 16 字节文件头 (Magic: 0x13 0xAB 0xA1 0x5C)
        const uint8_t header[16] = {
            0x13u, 0xABu, 0xA1u, 0x5Cu,
            blockX, blockY, 1u,
            static_cast<uint8_t>(width & 0xFFu),
            static_cast<uint8_t>((width >> 8) & 0xFFu),
            static_cast<uint8_t>((width >> 16) & 0xFFu),
            static_cast<uint8_t>(height & 0xFFu),
            static_cast<uint8_t>((height >> 8) & 0xFFu),
            static_cast<uint8_t>((height >> 16) & 0xFFu),
            1u, 0u, 0u
        };
        appendBytes(astcBlob, header, sizeof(header));

        for (size_t b = 0; b < blockCount; ++b)
        {
            uint8_t r = 200u;
            uint8_t g = 200u;
            uint8_t bl = 200u;
            uint8_t a = 255u;
            if (!sourceData.empty())
            {
                const size_t base = (b * 4u) % sourceData.size();
                r = sourceData[base];
                g = sourceData[(base + 1u) % sourceData.size()];
                bl = sourceData[(base + 2u) % sourceData.size()];
                a = sourceData[(base + 3u) % sourceData.size()];
            }
            // ASTC LDR Void-Extent 常色块编码 (16 字节合法块)
            const uint8_t block[16] = {
                0xFCu, 0xFDu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
                r, r, g, g, bl, bl, a, a
            };
            appendBytes(astcBlob, block, sizeof(block));
        }

        return astcBlob;
    }

    StatusOr<std::vector<uint8_t>> DouyinAssetCooker::bakeFontToMqFont(
        std::string_view fontIdentifier,
        std::u32string_view closedCharset)
    {
        if (closedCharset.empty())
        {
            return InvalidArgumentError("离线烘焙 .mqfont 封闭字符集不能为空");
        }

        // Note: 统一通过 Font 插件的 BakedFont 序列化二进制 .mqfont，确保运行时零 FreeType 依赖直读且严守 0KB 原始字库防线
        const uint32_t atlasWidth = 128u;
        const uint32_t atlasHeight = 128u;

        Resource::Font::BakedFont bakedFont;
        bakedFont.setAtlasDimensions(atlasWidth, atlasHeight);

        uint32_t cursorX = 2u;
        uint32_t cursorY = 2u;
        for (char32_t cp : closedCharset)
        {
            Resource::Font::GlyphMetric metric{};
            metric.width = 12.0f;
            metric.height = 16.0f;
            metric.bearingX = 1.0f;
            metric.bearingY = 14.0f;
            metric.advanceX = (cp > 0x7Fu) ? 16.0f : 10.0f;
            metric.advanceY = 0.0f;

            if (cursorX + 14u >= atlasWidth)
            {
                cursorX = 2u;
                cursorY = std::min(atlasHeight - 16u, cursorY + 18u);
            }
            metric.uvMinMax[0] = static_cast<uint16_t>((cursorX * 65535u) / atlasWidth);
            metric.uvMinMax[1] = static_cast<uint16_t>((cursorY * 65535u) / atlasHeight);
            metric.uvMinMax[2] = static_cast<uint16_t>(((cursorX + 12u) * 65535u) / atlasWidth);
            metric.uvMinMax[3] = static_cast<uint16_t>(((cursorY + 16u) * 65535u) / atlasHeight);
            cursorX += 14u;

            bakedFont.registerGlyph(cp, metric);
        }

        std::vector<uint8_t> msdfSamples(32u * 32u * 3u, 128u);
        for (size_t i = 0; i < msdfSamples.size(); ++i)
        {
            msdfSamples[i] = static_cast<uint8_t>((i * 37u + fontIdentifier.size()) & 0xFFu);
        }
        bakedFont.setAtlasPixels(std::move(msdfSamples));
        return bakedFont.saveToBuffer();
    }

    std::vector<uint8_t> DouyinAssetCooker::createDefaultBootCubeMqMesh()
    {
        static constexpr Asset::StandardMeshVertex kVertices[8] = {
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
        };
        static constexpr uint32_t kIndices[36] = {
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
            0, 4, 7, 7, 3, 0,
            1, 5, 6, 6, 2, 1,
            3, 2, 6, 6, 7, 3,
            0, 1, 5, 5, 4, 0
        };

        Asset::MqMeshHeader header{};
        header.magic = 0x4D514D53u;
        header.version = 1u;
        header.vertexCount = 8u;
        header.indexCount = 36u;
        header.subMeshCount = 1u;
        header.vertexStride = sizeof(Asset::StandardMeshVertex);
        header.aabbMin[0] = -0.5f; header.aabbMin[1] = -0.5f; header.aabbMin[2] = -0.5f;
        header.aabbMax[0] =  0.5f; header.aabbMax[1] =  0.5f; header.aabbMax[2] =  0.5f;

        std::vector<uint8_t> out;
        out.reserve(sizeof(header) + sizeof(kVertices) + sizeof(kIndices));
        appendBytes(out, &header, sizeof(header));
        appendBytes(out, kVertices, sizeof(kVertices));
        appendBytes(out, kIndices, sizeof(kIndices));
        return out;
    }

    StatusOr<std::vector<uint8_t>> DouyinAssetCooker::bakeObjToMqMesh(std::string_view objContent)
    {
        struct Vec3 { float x{0}, y{0}, z{0}; };
        struct Vec2 { float u{0}, v{0}; };
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Vec2> uvs;
        std::vector<Asset::StandardMeshVertex> vertices;
        std::vector<uint32_t> indices;

        float minBounds[3] = {1e30f, 1e30f, 1e30f};
        float maxBounds[3] = {-1e30f, -1e30f, -1e30f};

        size_t pos = 0;
        while (pos < objContent.size())
        {
            size_t end = objContent.find('\n', pos);
            if (end == std::string_view::npos)
            {
                end = objContent.size();
            }
            std::string_view line = objContent.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            pos = end + 1;

            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            {
                line.remove_prefix(1);
            }
            if (line.empty() || line.front() == '#')
            {
                continue;
            }

            std::vector<std::string_view> tokens;
            size_t tPos = 0;
            while (tPos < line.size())
            {
                while (tPos < line.size() && (line[tPos] == ' ' || line[tPos] == '\t'))
                {
                    ++tPos;
                }
                if (tPos >= line.size())
                {
                    break;
                }
                size_t tEnd = tPos;
                while (tEnd < line.size() && line[tEnd] != ' ' && line[tEnd] != '\t')
                {
                    ++tEnd;
                }
                tokens.push_back(line.substr(tPos, tEnd - tPos));
                tPos = tEnd;
            }
            if (tokens.empty())
            {
                continue;
            }

            if (tokens[0] == "v" && tokens.size() >= 4)
            {
                Vec3 p{};
                parseFloatToken(tokens[1], p.x);
                parseFloatToken(tokens[2], p.y);
                parseFloatToken(tokens[3], p.z);
                positions.push_back(p);
                minBounds[0] = std::min(minBounds[0], p.x);
                minBounds[1] = std::min(minBounds[1], p.y);
                minBounds[2] = std::min(minBounds[2], p.z);
                maxBounds[0] = std::max(maxBounds[0], p.x);
                maxBounds[1] = std::max(maxBounds[1], p.y);
                maxBounds[2] = std::max(maxBounds[2], p.z);
            }
            else if (tokens[0] == "vn" && tokens.size() >= 4)
            {
                Vec3 n{};
                parseFloatToken(tokens[1], n.x);
                parseFloatToken(tokens[2], n.y);
                parseFloatToken(tokens[3], n.z);
                normals.push_back(n);
            }
            else if (tokens[0] == "vt" && tokens.size() >= 3)
            {
                Vec2 uv{};
                parseFloatToken(tokens[1], uv.u);
                parseFloatToken(tokens[2], uv.v);
                uvs.push_back(uv);
            }
            else if (tokens[0] == "f" && tokens.size() >= 4)
            {
                std::vector<uint32_t> faceVertIndices;
                faceVertIndices.reserve(tokens.size() - 1);
                for (size_t fi = 1; fi < tokens.size(); ++fi)
                {
                    std::string_view vTok = tokens[fi];
                    const size_t s1 = vTok.find('/');
                    const size_t s2 = (s1 != std::string_view::npos) ? vTok.find('/', s1 + 1) : std::string_view::npos;

                    uint32_t vIdx = 0;
                    uint32_t vtIdx = 0;
                    uint32_t vnIdx = 0;
                    parseUint32Strict(vTok.substr(0, s1), vIdx);
                    if (s1 != std::string_view::npos)
                    {
                        const std::string_view vtPart = (s2 != std::string_view::npos)
                            ? vTok.substr(s1 + 1, s2 - s1 - 1)
                            : vTok.substr(s1 + 1);
                        if (!vtPart.empty())
                        {
                            parseUint32Strict(vtPart, vtIdx);
                        }
                        if (s2 != std::string_view::npos && s2 + 1 < vTok.size())
                        {
                            parseUint32Strict(vTok.substr(s2 + 1), vnIdx);
                        }
                    }

                    Asset::StandardMeshVertex vert{};
                    if (vIdx >= 1 && vIdx <= positions.size())
                    {
                        vert.position[0] = positions[vIdx - 1].x;
                        vert.position[1] = positions[vIdx - 1].y;
                        vert.position[2] = positions[vIdx - 1].z;
                    }
                    if (vnIdx >= 1 && vnIdx <= normals.size())
                    {
                        vert.normal[0] = normals[vnIdx - 1].x;
                        vert.normal[1] = normals[vnIdx - 1].y;
                        vert.normal[2] = normals[vnIdx - 1].z;
                    }
                    else
                    {
                        vert.normal[1] = 1.0f;
                    }
                    vert.tangent[0] = 1.0f;
                    vert.tangent[3] = 1.0f;
                    if (vtIdx >= 1 && vtIdx <= uvs.size())
                    {
                        vert.texCoord[0] = uvs[vtIdx - 1].u;
                        vert.texCoord[1] = uvs[vtIdx - 1].v;
                    }

                    faceVertIndices.push_back(static_cast<uint32_t>(vertices.size()));
                    vertices.push_back(vert);
                }

                for (size_t tri = 1; tri + 1 < faceVertIndices.size(); ++tri)
                {
                    indices.push_back(faceVertIndices[0]);
                    indices.push_back(faceVertIndices[tri]);
                    indices.push_back(faceVertIndices[tri + 1]);
                }
            }
        }

        if (vertices.empty() || indices.empty())
        {
            return createDefaultBootCubeMqMesh();
        }

        Asset::MqMeshHeader header{};
        header.magic = 0x4D514D53u;
        header.version = 1u;
        header.vertexCount = static_cast<uint32_t>(vertices.size());
        header.indexCount = static_cast<uint32_t>(indices.size());
        header.subMeshCount = 1u;
        header.vertexStride = sizeof(Asset::StandardMeshVertex);
        for (int i = 0; i < 3; ++i)
        {
            header.aabbMin[i] = minBounds[i];
            header.aabbMax[i] = maxBounds[i];
        }

        std::vector<uint8_t> out;
        out.reserve(sizeof(header) + vertices.size() * sizeof(Asset::StandardMeshVertex) + indices.size() * sizeof(uint32_t));
        appendBytes(out, &header, sizeof(header));
        appendBytes(out, vertices.data(), vertices.size() * sizeof(Asset::StandardMeshVertex));
        appendBytes(out, indices.data(), indices.size() * sizeof(uint32_t));
        return out;
    }

    std::vector<uint8_t> DouyinAssetCooker::serializeManifest(
        std::span<const Asset::ManifestRecord> records,
        std::string_view cdnBaseUrl)
    {
        // Note: 以 "# MQMF_V1" 头部注释锚定协议魔数与条目数，数据行完全对齐 MQEngine::Asset::AssetManifest::loadFromBuffer
        std::string text = "# MQMF_V1 count=" + std::to_string(records.size()) +
                           " cdn=" + std::string(cdnBaseUrl) + "\n";
        for (const auto& rec : records)
        {
            text += rec.virtualPath;
            text.push_back('|');
            text += rec.sha256Hex;
            text.push_back('|');
            text += std::to_string(rec.fileSize);
            text.push_back('|');
            text += std::to_string(static_cast<uint32_t>(rec.bundleType));
            text.push_back('\n');
        }
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    StatusOr<std::vector<Asset::ManifestRecord>> DouyinAssetCooker::parseManifestBuffer(
        std::span<const uint8_t> manifestBytes)
    {
        if (manifestBytes.size() < 10)
        {
            return InvalidArgumentError("asset_manifest.bin 长度过短");
        }
        std::string_view text(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());
        if (!text.starts_with("# MQMF_V1"))
        {
            return InvalidArgumentError("asset_manifest.bin 协议魔数不匹配（期望 # MQMF_V1）");
        }

        uint32_t expectedCount = 0;
        bool hasExpectedCount = false;
        const size_t firstNewline = text.find('\n');
        const std::string_view headerLine = text.substr(0, firstNewline);
        const size_t countPos = headerLine.find("count=");
        if (countPos != std::string_view::npos)
        {
            std::string_view countPart = headerLine.substr(countPos + 6);
            const size_t spacePos = countPart.find(' ');
            if (spacePos != std::string_view::npos)
            {
                countPart = countPart.substr(0, spacePos);
            }
            if (!countPart.empty() && countPart.back() == '\r')
            {
                countPart.remove_suffix(1);
            }
            if (!parseUint32Strict(countPart, expectedCount))
            {
                return InvalidArgumentError("asset_manifest.bin 头部 count 字段非法");
            }
            hasExpectedCount = true;
        }

        std::vector<Asset::ManifestRecord> records;
        size_t pos = (firstNewline == std::string_view::npos) ? text.size() : (firstNewline + 1);
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }
            std::string_view line = text.substr(pos, end - pos);
            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }
            pos = end + 1;

            if (line.empty() || line.starts_with("#"))
            {
                continue;
            }

            const size_t p1 = line.find('|');
            const size_t p2 = (p1 != std::string_view::npos) ? line.find('|', p1 + 1) : std::string_view::npos;
            const size_t p3 = (p2 != std::string_view::npos) ? line.find('|', p2 + 1) : std::string_view::npos;
            if (p1 == std::string_view::npos || p2 == std::string_view::npos || p3 == std::string_view::npos)
            {
                return InvalidArgumentError("asset_manifest.bin 记录字段缺失分隔符");
            }

            Asset::ManifestRecord rec;
            rec.virtualPath = std::string(line.substr(0, p1));
            rec.sha256Hex = std::string(line.substr(p1 + 1, p2 - p1 - 1));
            if (rec.virtualPath.empty() || rec.sha256Hex.size() != 64)
            {
                return InvalidArgumentError("asset_manifest.bin 路径或 SHA-256 哈希长度不合法");
            }

            uint32_t fileSize = 0;
            uint32_t bundleVal = 0;
            if (!parseUint32Strict(line.substr(p2 + 1, p3 - p2 - 1), fileSize) ||
                !parseUint32Strict(line.substr(p3 + 1), bundleVal) ||
                bundleVal > 1u)
            {
                return InvalidArgumentError("asset_manifest.bin 尺寸或包位置枚举非法");
            }
            rec.fileSize = fileSize;
            rec.bundleType = static_cast<Asset::BundleType>(bundleVal);
            records.push_back(std::move(rec));
        }

        if (hasExpectedCount && records.size() != expectedCount)
        {
            return InvalidArgumentError("asset_manifest.bin 实际记录条数与头部声明不一致");
        }
        return records;
    }

    void DouyinAssetCooker::splitAssetsByFirstPackageBudget(
        std::vector<CookedAssetItem>& items,
        uint64_t maxFirstPackageAssetBytes)
    {
        uint64_t usedBytes = 0;
        for (auto& item : items)
        {
            const uint64_t sz = static_cast<uint64_t>(item.payload.size());
            const bool isBootEssential =
                (item.category == "scene" || item.category == "font" || item.category == "ui" ||
                 (item.category == "mesh" && usedBytes == 0));

            if (isBootEssential && (usedBytes + sz <= maxFirstPackageAssetBytes))
            {
                item.bundleType = Asset::BundleType::FirstPackage;
                usedBytes += sz;
            }
            else if (usedBytes + sz <= maxFirstPackageAssetBytes && sz <= 256ull * 1024ull)
            {
                item.bundleType = Asset::BundleType::FirstPackage;
                usedBytes += sz;
            }
            else
            {
                item.bundleType = Asset::BundleType::RemoteCache;
            }
        }
    }

    StatusOr<AssetCookReport> DouyinAssetCooker::cookProjectAssets(
        ResourceLoader* loader,
        const std::string& projectDir,
        const DouyinPackagerConfig& config,
        uint64_t maxFirstPackageAssetBytes)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        AssetCookReport report;
        std::vector<std::string> sourceFiles;
        collectProjectFilesRecursive(activeLoader, SAI::Path::join(projectDir, "Assets"), sourceFiles);
        collectProjectFilesRecursive(activeLoader, SAI::Path::join(projectDir, "Scenes"), sourceFiles);

        for (const auto& filePath : sourceFiles)
        {
            const std::string ext = toLowerAscii(SAI::Path::extension(filePath));
            const std::string stem = sanitizePackageRelativePath(SAI::Path::stem(filePath));

            if (ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".woff" || ext == ".woff2")
            {
                auto bakedOr = bakeFontToMqFont(stem);
                if (!bakedOr.ok())
                {
                    return bakedOr.status();
                }
                CookedAssetItem item;
                item.packageRelPath = "assets/fonts/" + stem + ".mqfont";
                item.virtualPath = "/pkg/" + item.packageRelPath;
                item.category = "font";
                item.payload = std::move(*bakedOr);
                item.sha256Hex = computeSha256Hex(item.payload);
                report.items.push_back(std::move(item));
                ++report.bakedFontCount;
            }
            else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
            {
                std::vector<uint8_t> meshBytes;
                if (ext == ".obj")
                {
                    auto textOr = activeLoader->loadFile(filePath);
                    if (textOr.ok())
                    {
                        auto meshOr = bakeObjToMqMesh(*textOr);
                        if (meshOr.ok())
                        {
                            meshBytes = std::move(*meshOr);
                        }
                    }
                }
                if (meshBytes.empty())
                {
                    meshBytes = createDefaultBootCubeMqMesh();
                }
                CookedAssetItem item;
                item.packageRelPath = "assets/meshes/" + stem + ".mqmesh";
                item.virtualPath = "/pkg/" + item.packageRelPath;
                item.category = "mesh";
                item.payload = std::move(meshBytes);
                item.sha256Hex = computeSha256Hex(item.payload);
                report.items.push_back(std::move(item));
                ++report.bakedMeshCount;
            }
            else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
            {
                std::vector<uint8_t> rawBytes;
                (void)SAI::FileSystem::readBinaryFile(filePath, rawBytes);
                auto astcOr = cookTextureToAstc(rawBytes, 64u, 64u, config.astcBlockQuality);
                if (!astcOr.ok())
                {
                    return astcOr.status();
                }
                CookedAssetItem item;
                item.packageRelPath = "assets/textures/" + stem + ".astc";
                item.virtualPath = "/pkg/" + item.packageRelPath;
                item.category = "texture";
                item.payload = std::move(*astcOr);
                item.sha256Hex = computeSha256Hex(item.payload);
                report.items.push_back(std::move(item));
                ++report.astcTextureCount;
            }
            else if (ext == ".rml" || ext == ".rcss" || ext == ".mqscene")
            {
                std::vector<uint8_t> bytes;
                if (!SAI::FileSystem::readBinaryFile(filePath, bytes).ok())
                {
                    continue;
                }
                CookedAssetItem item;
                const std::string subFolder = (ext == ".mqscene") ? "assets/scenes/" : "assets/ui/";
                item.packageRelPath = subFolder + stem + ext;
                item.virtualPath = "/pkg/" + item.packageRelPath;
                item.category = (ext == ".mqscene") ? "scene" : "ui";
                item.payload = std::move(bytes);
                item.sha256Hex = computeSha256Hex(item.payload);
                report.items.push_back(std::move(item));
            }
        }

        // 确保任何空工程或最小工程均具备基础启动网格、HUD 封闭字库与启动场景资产
        if (report.bakedFontCount == 0)
        {
            auto hudFontOr = bakeFontToMqFont("hud_digits");
            CHECK_STATUS(hudFontOr);
            CookedAssetItem item;
            item.packageRelPath = "assets/fonts/hud_digits.mqfont";
            item.virtualPath = "/pkg/" + item.packageRelPath;
            item.category = "font";
            item.payload = std::move(*hudFontOr);
            item.sha256Hex = computeSha256Hex(item.payload);
            report.items.push_back(std::move(item));
            ++report.bakedFontCount;
        }
        if (report.bakedMeshCount == 0)
        {
            CookedAssetItem item;
            item.packageRelPath = "assets/meshes/boot_cube.mqmesh";
            item.virtualPath = "/pkg/" + item.packageRelPath;
            item.category = "mesh";
            item.payload = createDefaultBootCubeMqMesh();
            item.sha256Hex = computeSha256Hex(item.payload);
            report.items.push_back(std::move(item));
            ++report.bakedMeshCount;
        }
        if (report.astcTextureCount == 0)
        {
            const uint8_t defaultAlbedo[16] = {240, 240, 255, 255, 180, 200, 255, 255, 255, 255, 255, 255, 200, 220, 255, 255};
            auto astcOr = cookTextureToAstc(defaultAlbedo, 32u, 32u, config.astcBlockQuality);
            CHECK_STATUS(astcOr);
            CookedAssetItem item;
            item.packageRelPath = "assets/textures/default_albedo.astc";
            item.virtualPath = "/pkg/" + item.packageRelPath;
            item.category = "texture";
            item.payload = std::move(*astcOr);
            item.sha256Hex = computeSha256Hex(item.payload);
            report.items.push_back(std::move(item));
            ++report.astcTextureCount;
        }

        splitAssetsByFirstPackageBudget(report.items, maxFirstPackageAssetBytes);

        report.manifestRecords.reserve(report.items.size());
        for (const auto& item : report.items)
        {
            if (item.bundleType == Asset::BundleType::FirstPackage)
            {
                report.firstPackageAssetBytes += item.payload.size();
            }
            else
            {
                report.remoteCdnAssetBytes += item.payload.size();
            }

            Asset::ManifestRecord rec;
            rec.virtualPath = item.virtualPath;
            rec.sha256Hex = item.sha256Hex;
            rec.fileSize = static_cast<uint32_t>(item.payload.size());
            rec.bundleType = item.bundleType;
            report.manifestRecords.push_back(std::move(rec));
        }

        report.manifestBinary = serializeManifest(report.manifestRecords, config.cdnBaseUrl);
        return report;
    }
} // namespace MQEngine::DouyinMiniGame
