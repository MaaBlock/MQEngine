/**
 * @file BakedFont.h
 * @brief 轨道 A 离线烘焙 .mqfont MSDF 矢量字库加载器、序列化器与度量表 (运行时零 FreeType 依赖)
 */
#ifndef MQENGINE_FONT_BAKEDFONT_H
#define MQENGINE_FONT_BAKEDFONT_H

#include "../../FontApi.h"
#include "FontTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine::Resource::Font
{
    /**
     * @brief .mqfont 离线二进制标头结构 (Magic: 'MQFT' = 0x4D514654)
     */
    struct MqFontBinaryHeader
    {
        uint32_t magic{0x4D514654u};
        uint32_t version{1u};
        uint32_t atlasWidth{512u};
        uint32_t atlasHeight{512u};
        uint32_t glyphCount{0u};
        uint32_t pixelByteSize{0u};
        float pxRange{4.0f};
        float lineHeight{32.0f};
    };

    struct MqFontGlyphBinaryEntry
    {
        uint32_t codepoint{0u};
        GlyphMetric metric{};
    };

    /**
     * @brief 离线预烘焙字库 (FEAT-56)
     */
    class FONT_RUNTIME_COMPOSITE_API BakedFont
    {
    public:
        static constexpr uint32_t Magic = 0x4D514654u;

        BakedFont() = default;
        ~BakedFont() = default;

        Status load(std::string_view path);
        Status loadFromBuffer(std::span<const uint8_t> data);
        [[nodiscard]] std::vector<uint8_t> saveToBuffer() const;

        void initDefaultCombatPreset(uint32_t atlasWidth = 512, uint32_t atlasHeight = 512);

        [[nodiscard]] bool hasGlyph(char32_t codepoint) const noexcept;
        [[nodiscard]] GlyphMetric getGlyphMetric(char32_t codepoint) const noexcept;

        [[nodiscard]] const std::unordered_set<char32_t>& getClosedCharset() const noexcept { return m_closedCharset; }
        [[nodiscard]] uint32_t getFreeTypeInvocationCount() const noexcept { return m_freeTypeInvocationCount; }

        [[nodiscard]] uint32_t getAtlasWidth() const noexcept { return m_atlasWidth; }
        [[nodiscard]] uint32_t getAtlasHeight() const noexcept { return m_atlasHeight; }
        [[nodiscard]] float getPxRange() const noexcept { return m_pxRange; }
        [[nodiscard]] float getLineHeight() const noexcept { return m_lineHeight; }
        [[nodiscard]] FCT::Image* getAtlasTexture() const noexcept { return m_atlasTexture.get(); }
        [[nodiscard]] const std::vector<uint8_t>& getAtlasPixels() const noexcept { return m_atlasPixels; }
        [[nodiscard]] bool isLoaded() const noexcept { return m_isLoaded; }

        void setAtlasDimensions(uint32_t width, uint32_t height) noexcept
        {
            m_atlasWidth = width;
            m_atlasHeight = height;
        }
        void setAtlasPixels(std::vector<uint8_t> pixels) noexcept { m_atlasPixels = std::move(pixels); }
        void registerGlyph(char32_t codepoint, const GlyphMetric& metric);

    private:
        std::unordered_set<char32_t> m_closedCharset;
        std::unordered_map<char32_t, GlyphMetric> m_metrics;
        std::vector<uint8_t> m_atlasPixels;
        uint32_t m_atlasWidth{512};
        uint32_t m_atlasHeight{512};
        float m_pxRange{4.0f};
        float m_lineHeight{32.0f};
        uint32_t m_freeTypeInvocationCount{0};
        Ptr<FCT::Image> m_atlasTexture;
        bool m_isLoaded{false};
    };
}

#endif // MQENGINE_FONT_BAKEDFONT_H
