/**
 * @file MQRmlFontEngine.h
 * @brief 对接微内核双轨字体系统的 RmlUi 6.0 自研字体引擎后端 (FEAT-48)
 */
#ifndef MQENGINE_GAME_HTML_UI_MQRML_FONT_ENGINE_H
#define MQENGINE_GAME_HTML_UI_MQRML_FONT_ENGINE_H

#include "../../GameHtmlUiApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetPreviousSibling
#undef GetPreviousSibling
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/StringUtilities.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine::GameHtmlUI
{
    /**
     * @brief RmlUi 字体特效层简化描述（承接 .rcss 描边与投影效果）
     */
    struct RmlFontEffectLayerDesc
    {
        Rml::FontEffect::Layer layer = Rml::FontEffect::Layer::Back;
        Rml::Colourb colour{0, 0, 0, 255};
        Rml::Vector2f offset{0.0f, 0.0f};
        float inflatePx = 0.0f;
        size_t fingerprint = 0;
    };

    /**
     * @brief 单个已缓存 UI 字形的 UV 与排版度量快照
     */
    struct RmlCachedGlyphEntry
    {
        uint32_t pageId = 0;
        bool isEmoji = false;
        int32_t width = 0;
        int32_t height = 0;
        int32_t bearingX = 0;
        int32_t bearingY = 0;
        int32_t advanceX = 0;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        uint32_t generation = 0;
    };

    /**
     * @brief 自研 RmlUi 字体引擎：剥离内置 FontEngineDefault，直连引擎 FontSystem 与动态字形图集
     */
    class RML_UI_COMPOSITE_API MQRmlFontEngine : public Rml::FontEngineInterface
    {
    public:
        explicit MQRmlFontEngine(Resource::Font::FontSystem* externalFontSystem = nullptr);
        ~MQRmlFontEngine() override;

        MQRmlFontEngine(const MQRmlFontEngine&) = delete;
        MQRmlFontEngine& operator=(const MQRmlFontEngine&) = delete;

        // ==================== Rml::FontEngineInterface 核心契约 ====================

        void Initialize() override;
        void Shutdown() override;

        bool LoadFontFace(
            const Rml::String& file_name,
            bool fallback_face,
            Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto) override;

        bool LoadFontFace(
            Rml::Span<const Rml::byte> data,
            const Rml::String& family,
            Rml::Style::FontStyle style,
            Rml::Style::FontWeight weight,
            bool fallback_face) override;

        Rml::FontFaceHandle GetFontFaceHandle(
            const Rml::String& family,
            Rml::Style::FontStyle style,
            Rml::Style::FontWeight weight,
            int size) override;

        Rml::FontEffectsHandle PrepareFontEffects(
            Rml::FontFaceHandle handle,
            const Rml::FontEffectList& font_effects) override;

        const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;

        int GetStringWidth(
            Rml::FontFaceHandle handle,
            Rml::StringView string,
            const Rml::TextShapingContext& text_shaping_context,
            Rml::Character prior_character = Rml::Character::Null) override;

        int GenerateString(
            Rml::RenderManager& render_manager,
            Rml::FontFaceHandle face_handle,
            Rml::FontEffectsHandle font_effects_handle,
            Rml::StringView string,
            Rml::Vector2f position,
            Rml::ColourbPremultiplied colour,
            float opacity,
            const Rml::TextShapingContext& text_shaping_context,
            Rml::TexturedMeshList& mesh_list) override;

        int GetVersion(Rml::FontFaceHandle handle) override;
        void ReleaseFontResources() override;

        // ==================== 引擎桥接与无头测试扩展接口 ====================

        void bindFontSystem(Resource::Font::FontSystem* fontSystem) noexcept;
        [[nodiscard]] Resource::Font::FontSystem* resolveFontSystem();
        [[nodiscard]] const Resource::Font::FontSystem* resolveFontSystem() const noexcept;

        void setDevicePixelRatio(float dpr) noexcept;
        [[nodiscard]] float getDevicePixelRatio() const noexcept { return m_devicePixelRatio; }

        void notifyAtlasInvalidated() noexcept;
        [[nodiscard]] int getGeneration() const noexcept;

        [[nodiscard]] Rml::FontFaceHandle getFontFaceHandle(
            std::string_view family,
            int sizePx = 16) noexcept;

        [[nodiscard]] int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string);
        [[nodiscard]] int getStringWidth(std::string_view text, int fontSizePx = 16);

        int GenerateString(
            Rml::RenderManager* renderManager,
            Rml::FontFaceHandle faceHandle,
            Rml::StringView string,
            Rml::Vector2f position,
            Rml::Colourb colour,
            std::vector<Rml::Mesh>& outMeshes);

        [[nodiscard]] FCT::SAI::FontMetrics getSignedFontMetrics(Rml::FontFaceHandle handle) const noexcept;
        [[nodiscard]] uint32_t getLastBatchWarmCount() const noexcept { return m_lastBatchWarmCount; }

        static void decodeUtf8String(std::string_view utf8, std::vector<char32_t>& outCodepoints);
        static uint16_t quantizePhysicalFontSize(int logicalSizePx, float dpr) noexcept;
        static std::string buildAtlasPageUri(uint32_t pageId, bool isEmoji);

    private:
        struct RegisteredFamily
        {
            std::string canonicalName;
            std::string providerFamily;
            std::string resourcePath;
            bool isSystemUi = false;
            bool isFallback = false;
            bool isPrebakedMqFont = false;
        };

        struct ActiveFaceInstance
        {
            Rml::FontFaceHandle handle = 0;
            std::string familyKey;
            std::string providerFamily;
            uint16_t logicalSizePx = 16;
            uint16_t physicalSizePx = 16;
            uint8_t weightBucket = 4;
            bool italic = false;
            Rml::FontMetrics metrics{};
        };

        struct PreparedEffectConfig
        {
            std::vector<RmlFontEffectLayerDesc> layers;
        };

        struct GlyphCacheKey
        {
            std::string familyKey;
            uint16_t physicalSizePx = 16;
            uint8_t weightBucket = 4;
            bool italic = false;
            char32_t codepoint = 0;

            bool operator==(const GlyphCacheKey& other) const noexcept = default;
        };

        struct GlyphCacheKeyHash
        {
            size_t operator()(const GlyphCacheKey& key) const noexcept;
        };

    private:
        void ensureDefaultSystemFamilyRegistered();
        FCT::SAI::SystemFontProvider* ensureFallbackProvider();
        ActiveFaceInstance* findFaceMutable(Rml::FontFaceHandle handle) noexcept;
        const ActiveFaceInstance* findFace(Rml::FontFaceHandle handle) const noexcept;
        void populateFaceMetrics(ActiveFaceInstance& face);
        void syncGenerationWithFontSystem();

        void batchWarmMissingCodepoints(
            const ActiveFaceInstance& face,
            std::span<const char32_t> codepoints);

        const RmlCachedGlyphEntry* lookupOrSynthesizeGlyph(
            const ActiveFaceInstance& face,
            char32_t codepoint);

        int emitStringGeometry(
            Rml::RenderManager* renderManager,
            const ActiveFaceInstance& face,
            Rml::FontEffectsHandle effectsHandle,
            std::span<const char32_t> codepoints,
            Rml::Vector2f position,
            Rml::ColourbPremultiplied baseColour,
            float opacity,
            float letterSpacing,
            Rml::TexturedMeshList& outTexturedMeshes);

        static std::string normalizeFamilyKey(std::string_view raw);
        static std::string extractFamilyFromPath(std::string_view path);
        static uint8_t mapRmlWeightToBucket(Rml::Style::FontWeight weight) noexcept;
        static bool isZeroWidthControlCodepoint(char32_t cp) noexcept;
        static bool isColorEmojiCodepoint(char32_t cp) noexcept;

    private:
        Resource::Font::FontSystem* m_externalFontSystem = nullptr;
        UniquePtr<Resource::Font::FontSystem> m_ownedFontSystem;
        UniquePtr<FCT::SAI::SystemFontProvider> m_provider;

        std::unordered_map<std::string, RegisteredFamily> m_registeredFamilies;
        std::unordered_map<std::string, Rml::FontFaceHandle> m_faceLookup;
        std::unordered_map<Rml::FontFaceHandle, ActiveFaceInstance> m_activeFaces;
        std::unordered_map<Rml::FontEffectsHandle, PreparedEffectConfig> m_effectConfigs;
        std::unordered_map<GlyphCacheKey, RmlCachedGlyphEntry, GlyphCacheKeyHash> m_localGlyphCache;

        Rml::FontMetrics m_fallbackMetrics{};
        Rml::FontFaceHandle m_nextFaceHandle = 1;
        Rml::FontEffectsHandle m_nextEffectsHandle = 1;

        uint32_t m_observedAtlasGeneration = 1;
        uint32_t m_manualInvalidationOffset = 0;
        uint32_t m_localShelfX = 2;
        uint32_t m_localShelfY = 2;
        uint32_t m_localShelfRowH = 0;
        uint32_t m_localPageIndex = 0;
        uint32_t m_lastBatchWarmCount = 0;

        float m_devicePixelRatio = 1.0f;
        bool m_initialized = false;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_MQRML_FONT_ENGINE_H
