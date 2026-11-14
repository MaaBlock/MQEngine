/**
 * @file FontSystem.h
 * @brief 微内核双轨字体系统门面与跨端后端分流中枢
 */
#ifndef MQENGINE_FONT_FONTSYSTEM_H
#define MQENGINE_FONT_FONTSYSTEM_H

#include "../../FontApi.h"
#include "FontTypes.h"
#include "BakedFont.h"
#include "InGameTextComponent.h"
#include "GlyphAtlasManager.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine::Resource::Font
{
    /**
     * @brief 微内核双轨字体系统管理器 (FEAT-55, FEAT-58, FEAT-59)
     */
    class FONT_RUNTIME_COMPOSITE_API FontSystem
    {
    public:
        FontSystem();
        explicit FontSystem(FontBackendMode mode);
        ~FontSystem();

        FontSystem(const FontSystem&) = delete;
        FontSystem& operator=(const FontSystem&) = delete;
        FontSystem(FontSystem&&) noexcept = default;
        FontSystem& operator=(FontSystem&&) noexcept = default;

        Status init(FontBackendMode mode = FontBackendMode::Desktop_FreeType);
        Status initGpuResources(FCT::Context* ctx);
        void setDeferGpuUpload(bool defer) noexcept { m_atlasManager.setDeferGpuUpload(defer); }
        Status flushDirtyPagesToGpu(FCT::Context* ctx) { return m_atlasManager.flushDirtyPagesToGpu(ctx); }
        void term() noexcept;

        [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
        [[nodiscard]] FontBackendMode getBackendMode() const noexcept { return m_mode; }
        [[nodiscard]] bool isFreeTypeBackendActive() const noexcept { return m_freeTypeActive; }
        [[nodiscard]] bool isHintingEnabled() const noexcept { return m_hintingEnabled; }

        [[nodiscard]] BakedFont& getBakedFont() noexcept { return m_bakedFont; }
        [[nodiscard]] const BakedFont& getBakedFont() const noexcept { return m_bakedFont; }

        [[nodiscard]] GlyphAtlasManager& getAtlasManager() noexcept { return m_atlasManager; }
        [[nodiscard]] const GlyphAtlasManager& getAtlasManager() const noexcept { return m_atlasManager; }

        [[nodiscard]] InGameTextComponent& getInGameText() noexcept { return m_inGameText; }
        [[nodiscard]] const InGameTextComponent& getInGameText() const noexcept { return m_inGameText; }

        Status registerFontFace(std::string_view family, std::string_view resourcePath, bool fallbackFace = false);
        Status registerFontFaceMemory(std::string_view family, std::span<const uint8_t> data, bool fallbackFace = false);
        [[nodiscard]] bool hasFontFace(std::string_view family) const noexcept;
        [[nodiscard]] std::string_view getFallbackFontFamily() const noexcept { return m_fallbackFamily; }

        StatusOr<FCT::SAI::FontMetrics> queryFontMetrics(
            std::string_view family,
            uint16_t fontSizePx,
            uint8_t weight = 4,
            bool italic = false);

        void registerPrebakedSubset(std::span<const char32_t> codepoints);
        [[nodiscard]] bool isGlyphInPrebakedSubset(char32_t codepoint) const noexcept;
        [[nodiscard]] bool queriesSystemFontProvider(char32_t codepoint) const noexcept;

        Status ensureGlyphsCached(
            std::span<const char32_t> codepoints,
            uint16_t fontSizePx = 16,
            std::string_view family = "",
            uint8_t weight = 4,
            bool italic = false);

        bool tryGetCachedGlyph(
            char32_t codepoint,
            uint16_t fontSizePx,
            CachedUiGlyph& outGlyph) const noexcept;

        bool tryGetCachedGlyph(
            std::string_view family,
            char32_t codepoint,
            uint16_t fontSizePx,
            uint8_t weight,
            bool italic,
            CachedUiGlyph& outGlyph) const noexcept;

        [[nodiscard]] uint32_t getLastBatchCodepointCount() const noexcept { return m_lastBatchCodepointCount; }
        [[nodiscard]] uint32_t getAtlasGeneration() const noexcept { return m_atlasManager.getAtlasGeneration(); }
        [[nodiscard]] FCT::Image* getAtlasPageTexture(uint32_t pageId) const noexcept { return m_atlasManager.getAtlasPageTexture(pageId); }

        void onMemoryWarning();

    private:
        struct GlyphCacheKey
        {
            std::string family;
            char32_t codepoint{0};
            uint16_t fontSizePx{16};
            uint8_t weight{4};
            bool italic{false};

            bool operator==(const GlyphCacheKey& o) const noexcept
            {
                return codepoint == o.codepoint &&
                       fontSizePx == o.fontSizePx &&
                       weight == o.weight &&
                       italic == o.italic &&
                       family == o.family;
            }
        };

        struct GlyphCacheKeyHash
        {
            size_t operator()(const GlyphCacheKey& k) const noexcept
            {
                size_t h = std::hash<std::string>{}(k.family);
                h ^= (static_cast<size_t>(k.codepoint) * 2654435761u) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= (static_cast<size_t>(k.fontSizePx) << 16) ^ (static_cast<size_t>(k.weight) << 1) ^ (k.italic ? 1u : 0u);
                return h;
            }
        };

        void applyBackendModeFlags(FontBackendMode mode) noexcept;
        void seedDefaultPrebakedSubset();
        Status uploadSingleRasterizedGlyph(
            const FCT::SAI::RasterizedGlyphBatch& batch,
            const FCT::SAI::GlyphMetrics& gm,
            const GlyphCacheKey& key);

    private:
        FontBackendMode m_mode{FontBackendMode::Desktop_FreeType};
        bool m_freeTypeActive{true};
        bool m_hintingEnabled{true};
        BakedFont m_bakedFont;
        GlyphAtlasManager m_atlasManager;
        InGameTextComponent m_inGameText;
        std::unordered_set<char32_t> m_prebakedSubset;
        std::unordered_map<std::string, std::vector<uint8_t>> m_registeredFontBlobs;
        std::string m_fallbackFamily;
        std::unordered_map<GlyphCacheKey, CachedUiGlyph, GlyphCacheKeyHash> m_cachedGlyphs;
        UniquePtr<FCT::SAI::SystemFontProvider> m_systemFontProvider;
        uint32_t m_lastBatchCodepointCount{0};
        bool m_initialized{false};
    };
}

#endif // MQENGINE_FONT_FONTSYSTEM_H
