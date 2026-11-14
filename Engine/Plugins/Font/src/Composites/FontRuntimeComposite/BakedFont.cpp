/**
 * @file BakedFont.cpp
 * @brief 轨道 A 离线烘焙 .mqfont MSDF 矢量字库加载器与二进制序列化实现
 */
#include "BakedFont.h"
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>

namespace MQEngine::Resource::Font
{
    void BakedFont::initDefaultCombatPreset(uint32_t atlasWidth, uint32_t atlasHeight)
    {
        ZoneScoped();
        m_closedCharset.clear();
        m_metrics.clear();
        m_atlasWidth = std::max(64u, atlasWidth);
        m_atlasHeight = std::max(64u, atlasHeight);
        m_freeTypeInvocationCount = 0;

        Domain::Renderer::AtlasShelfPacker packer(m_atlasWidth, m_atlasHeight, 2);
        const std::u32string closedSet = U"0123456789+-x!%暴击闪避格挡免疫CRITMISS";

        for (char32_t cp : closedSet)
        {
            const bool isCjk = (cp >= 0x4E00u);
            const uint32_t gw = isCjk ? 32u : 16u;
            const uint32_t gh = isCjk ? 32u : 24u;
            auto alloc = packer.allocate(gw, gh);

            GlyphMetric m{};
            m.width = static_cast<float>(gw);
            m.height = static_cast<float>(gh);
            m.bearingX = isCjk ? 2.0f : 1.0f;
            m.bearingY = isCjk ? 30.0f : 18.0f;
            m.advanceX = isCjk ? 32.0f : 16.0f;
            m.advanceY = 0.0f;
            if (alloc.success)
            {
                m.uvMinMax[0] = static_cast<uint16_t>((alloc.x * 65535u) / m_atlasWidth);
                m.uvMinMax[1] = static_cast<uint16_t>((alloc.y * 65535u) / m_atlasHeight);
                m.uvMinMax[2] = static_cast<uint16_t>(((alloc.x + alloc.width) * 65535u) / m_atlasWidth);
                m.uvMinMax[3] = static_cast<uint16_t>(((alloc.y + alloc.height) * 65535u) / m_atlasHeight);
            }
            registerGlyph(cp, m);
        }

        m_isLoaded = true;
    }

    Status BakedFont::loadFromBuffer(std::span<const uint8_t> data)
    {
        ZoneScoped();
        if (data.size() < sizeof(MqFontBinaryHeader))
        {
            return InvalidArgumentError(".mqfont 缓冲区长度不足以容纳标头");
        }

        MqFontBinaryHeader header{};
        std::memcpy(&header, data.data(), sizeof(MqFontBinaryHeader));
        if (header.magic != Magic)
        {
            return InvalidArgumentError(".mqfont 标头 Magic 校验失败");
        }
        if (header.atlasWidth == 0 || header.atlasHeight == 0)
        {
            return InvalidArgumentError(".mqfont 图集尺寸非法");
        }

        const uint64_t glyphBytes = static_cast<uint64_t>(header.glyphCount) * sizeof(MqFontGlyphBinaryEntry);
        const uint64_t totalExpected = sizeof(MqFontBinaryHeader) + glyphBytes + header.pixelByteSize;
        if (data.size() < totalExpected)
        {
            return InvalidArgumentError(".mqfont 二进制流被截断");
        }

        m_closedCharset.clear();
        m_metrics.clear();
        m_atlasWidth = header.atlasWidth;
        m_atlasHeight = header.atlasHeight;
        m_pxRange = header.pxRange;
        m_lineHeight = header.lineHeight;
        m_freeTypeInvocationCount = 0;

        const uint8_t* ptr = data.data() + sizeof(MqFontBinaryHeader);
        for (uint32_t i = 0; i < header.glyphCount; ++i)
        {
            MqFontGlyphBinaryEntry entry{};
            std::memcpy(&entry, ptr, sizeof(MqFontGlyphBinaryEntry));
            ptr += sizeof(MqFontGlyphBinaryEntry);
            registerGlyph(static_cast<char32_t>(entry.codepoint), entry.metric);
        }

        m_atlasPixels.assign(ptr, ptr + header.pixelByteSize);
        m_isLoaded = true;
        return OkStatus();
    }

    std::vector<uint8_t> BakedFont::saveToBuffer() const
    {
        ZoneScoped();
        std::vector<MqFontGlyphBinaryEntry> sortedEntries;
        sortedEntries.reserve(m_metrics.size());
        for (const auto& [cp, metric] : m_metrics)
        {
            sortedEntries.push_back(MqFontGlyphBinaryEntry{static_cast<uint32_t>(cp), metric});
        }
        std::sort(sortedEntries.begin(), sortedEntries.end(), [](const auto& a, const auto& b) {
            return a.codepoint < b.codepoint;
        });

        MqFontBinaryHeader header{};
        header.magic = Magic;
        header.version = 1u;
        header.atlasWidth = m_atlasWidth;
        header.atlasHeight = m_atlasHeight;
        header.glyphCount = static_cast<uint32_t>(sortedEntries.size());
        header.pixelByteSize = static_cast<uint32_t>(m_atlasPixels.size());
        header.pxRange = m_pxRange;
        header.lineHeight = m_lineHeight;

        const size_t glyphBytes = sortedEntries.size() * sizeof(MqFontGlyphBinaryEntry);
        std::vector<uint8_t> out(sizeof(MqFontBinaryHeader) + glyphBytes + m_atlasPixels.size());

        uint8_t* dst = out.data();
        std::memcpy(dst, &header, sizeof(MqFontBinaryHeader));
        dst += sizeof(MqFontBinaryHeader);
        if (glyphBytes > 0)
        {
            std::memcpy(dst, sortedEntries.data(), glyphBytes);
            dst += glyphBytes;
        }
        if (!m_atlasPixels.empty())
        {
            std::memcpy(dst, m_atlasPixels.data(), m_atlasPixels.size());
        }
        return out;
    }

    Status BakedFont::load(std::string_view path)
    {
        ZoneScoped();
        if (path.empty())
        {
            return InvalidArgumentError("字体路径不能为空");
        }

        m_freeTypeInvocationCount = 0;

        // 优先通过 ResourceLoader 加载真实 .mqfont 二进制流
        if (auto* resMod = ::MQEngine::Resource::Resource::InstancePtr())
        {
            if (auto* loader = resMod->getResourceLoader())
            {
                if (loader->exists(std::string(path)))
                {
                    auto textRes = loader->loadFile(std::string(path));
                    if (textRes.ok() && !textRes->empty())
                    {
                        std::span<const uint8_t> rawSpan(
                            reinterpret_cast<const uint8_t*>(textRes->data()),
                            textRes->size());
                        return loadFromBuffer(rawSpan);
                    }
                }
            }
        }

        // 次级回退：通过 FCT::SAI::FileSystem 读取包内虚拟路径
        if (FCT::SAI::FileSystem::exists(path))
        {
            auto bytesRes = FCT::SAI::FileSystem::readBinaryFile(path);
            if (bytesRes.ok() && !bytesRes->empty())
            {
                return loadFromBuffer(*bytesRes);
            }
        }

        // 若目标资产尚未离线落盘，初始化内置封闭战斗字集默认度量表，保障运行时零 FreeType 调用
        initDefaultCombatPreset(512, 512);
        return OkStatus();
    }

    bool BakedFont::hasGlyph(char32_t codepoint) const noexcept
    {
        return m_metrics.find(codepoint) != m_metrics.end();
    }

    GlyphMetric BakedFont::getGlyphMetric(char32_t codepoint) const noexcept
    {
        auto it = m_metrics.find(codepoint);
        if (it != m_metrics.end())
        {
            return it->second;
        }

        // 未烘焙生僻字符安全回退：返回 10.0f 默认步进，绝不触发运行时光栅化
        GlyphMetric fallback{};
        fallback.advanceX = 10.0f;
        fallback.advanceY = 0.0f;
        fallback.bearingX = 0.0f;
        fallback.bearingY = 10.0f;
        fallback.width = 10.0f;
        fallback.height = 14.0f;
        return fallback;
    }

    void BakedFont::registerGlyph(char32_t codepoint, const GlyphMetric& metric)
    {
        m_closedCharset.insert(codepoint);
        m_metrics[codepoint] = metric;
    }
}
