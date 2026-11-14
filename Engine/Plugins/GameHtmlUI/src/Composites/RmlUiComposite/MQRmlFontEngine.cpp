/**
 * @file MQRmlFontEngine.cpp
 * @brief 对接微内核双轨字体系统的 RmlUi 6.0 自研字体引擎实现 (FEAT-48)
 */
#include "MQRmlFontEngine.h"

#include <Font/Composites/FontRuntimeComposite/FontRuntimeComposite.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>

#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <concepts>

namespace MQEngine::GameHtmlUI
{
    namespace
    {
        constexpr uint32_t kAtlasPageDimension = 1024u;
        constexpr uint32_t kGlyphPaddingPx = 2u;
        constexpr char32_t kReplacementCodepoint = 0xFFFDu;

        // C++20 编译期能力探测：零开销桥接 FontSystem 扩展签名与基础签名
        template <typename T>
        concept HasExtendedEnsureGlyphs = requires(
            T& fs,
            std::span<const char32_t> cps,
            uint16_t sizePx,
            std::string_view family,
            uint8_t weight,
            bool italic) {
            { fs.ensureGlyphsCached(cps, sizePx, family, weight, italic) } -> std::same_as<Status>;
        };

        template <typename T>
        concept HasQueryFontMetrics = requires(
            T& fs,
            std::string_view family,
            uint16_t sizePx,
            uint8_t weight,
            bool italic) {
            { fs.queryFontMetrics(family, sizePx, weight, italic) } -> std::same_as<StatusOr<FCT::SAI::FontMetrics>>;
        };

        template <typename T>
        concept HasRegisterFontFace = requires(
            T& fs,
            std::string_view family,
            std::string_view path,
            bool fallbackFace) {
            { fs.registerFontFace(family, path, fallbackFace) } -> std::same_as<Status>;
        };

        template <typename T>
        concept HasRegisterFontFaceMemory = requires(
            T& fs,
            std::string_view family,
            std::span<const uint8_t> bytes,
            bool fallbackFace) {
            { fs.registerFontFaceMemory(family, bytes, fallbackFace) } -> std::same_as<Status>;
        };

        template <typename TGlyphOut>
        void copyGlyphFieldsToEntry(const TGlyphOut& cached, RmlCachedGlyphEntry& outEntry)
        {
            outEntry.pageId = cached.pageId;
            outEntry.isEmoji = (cached.format == Resource::Font::TextureFormat::R8G8B8A8_UNORM);
            outEntry.width = static_cast<int32_t>(std::lround(cached.metric.width));
            outEntry.height = static_cast<int32_t>(std::lround(cached.metric.height));
            outEntry.bearingX = static_cast<int32_t>(std::lround(cached.metric.bearingX));
            outEntry.bearingY = static_cast<int32_t>(std::lround(cached.metric.bearingY));
            if constexpr (requires { cached.metric.advanceX; })
            {
                outEntry.advanceX = static_cast<int32_t>(std::lround(cached.metric.advanceX));
            }
            else if constexpr (requires { cached.metric.advance; })
            {
                outEntry.advanceX = static_cast<int32_t>(std::lround(cached.metric.advance));
            }
            if constexpr (requires { cached.metric.u0; })
            {
                outEntry.u0 = cached.metric.u0;
                outEntry.v0 = cached.metric.v0;
                outEntry.u1 = cached.metric.u1;
                outEntry.v1 = cached.metric.v1;
            }
            else if constexpr (requires { cached.metric.uvMinMax[0]; })
            {
                constexpr float kInvUnorm16 = 1.0f / 65535.0f;
                outEntry.u0 = static_cast<float>(cached.metric.uvMinMax[0]) * kInvUnorm16;
                outEntry.v0 = static_cast<float>(cached.metric.uvMinMax[1]) * kInvUnorm16;
                outEntry.u1 = static_cast<float>(cached.metric.uvMinMax[2]) * kInvUnorm16;
                outEntry.v1 = static_cast<float>(cached.metric.uvMinMax[3]) * kInvUnorm16;
            }
            outEntry.generation = cached.generation;
        }

        template <typename TFontSys>
        void invokeEnsureGlyphsCached(
            TFontSys& fontSys,
            std::span<const char32_t> missingCps,
            uint16_t physicalSizePx,
            std::string_view providerFamily,
            uint8_t weightBucket,
            bool italic)
        {
            if constexpr (HasExtendedEnsureGlyphs<TFontSys>)
            {
                (void)fontSys.ensureGlyphsCached(missingCps, physicalSizePx, providerFamily, weightBucket, italic);
            }
            else
            {
                (void)fontSys.ensureGlyphsCached(missingCps, physicalSizePx);
            }
        }

        template <typename TFontSys>
        bool tryQueryMetricsFromFontSystem(
            TFontSys& fontSys,
            std::string_view providerFamily,
            uint16_t physicalSizePx,
            uint8_t weightBucket,
            bool italic,
            FCT::SAI::FontMetrics& outMetrics)
        {
            if constexpr (HasQueryFontMetrics<TFontSys>)
            {
                auto res = fontSys.queryFontMetrics(providerFamily, physicalSizePx, weightBucket, italic);
                if (res.ok())
                {
                    outMetrics = *res;
                    return true;
                }
            }
            return false;
        }

        template <typename TFontSys>
        void notifyFontSystemFaceFile(
            TFontSys& fontSys,
            std::string_view family,
            std::string_view normPath,
            bool fallbackFace)
        {
            if constexpr (HasRegisterFontFace<TFontSys>)
            {
                (void)fontSys.registerFontFace(family, normPath, fallbackFace);
            }
        }

        template <typename TFontSys>
        void notifyFontSystemFaceMemory(
            TFontSys& fontSys,
            std::string_view family,
            std::span<const uint8_t> rawBytes,
            bool fallbackFace)
        {
            if constexpr (HasRegisterFontFaceMemory<TFontSys>)
            {
                (void)fontSys.registerFontFaceMemory(family, rawBytes, fallbackFace);
            }
        }

        template <typename TFontSys>
        bool tryPullGlyphFromFontSystem(
            const TFontSys& fontSys,
            std::string_view providerFamily,
            char32_t codepoint,
            uint16_t physicalSizePx,
            uint8_t weightBucket,
            bool italic,
            RmlCachedGlyphEntry& outEntry)
        {
            Resource::Font::CachedUiGlyph cached{};
            if (fontSys.tryGetCachedGlyph(providerFamily, codepoint, physicalSizePx, weightBucket, italic, cached))
            {
                copyGlyphFieldsToEntry(cached, outEntry);
                return outEntry.advanceX > 0;
            }
            return false;
        }
    } // namespace

    size_t MQRmlFontEngine::GlyphCacheKeyHash::operator()(const GlyphCacheKey& key) const noexcept
    {
        size_t h = std::hash<std::string>{}(key.familyKey);
        h ^= (static_cast<size_t>(key.physicalSizePx) << 16) ^ (static_cast<size_t>(key.weightBucket) << 8);
        h ^= (key.italic ? 0x9E3779B9u : 0u);
        h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(key.codepoint)) + 0x9E3779B9u + (h << 6) + (h >> 2);
        return h;
    }

    MQRmlFontEngine::MQRmlFontEngine(Resource::Font::FontSystem* externalFontSystem)
        : m_externalFontSystem(externalFontSystem)
    {
        m_fallbackMetrics.size = 16;
        m_fallbackMetrics.ascent = 13.0f;
        m_fallbackMetrics.descent = 3.0f;
        m_fallbackMetrics.line_spacing = 19.0f;
        m_fallbackMetrics.x_height = 8.0f;
        m_fallbackMetrics.underline_position = 2.0f;
        m_fallbackMetrics.underline_thickness = 1.0f;

        ensureDefaultSystemFamilyRegistered();
    }

    MQRmlFontEngine::~MQRmlFontEngine()
    {
        Shutdown();
    }

    void MQRmlFontEngine::Initialize()
    {
        ensureDefaultSystemFamilyRegistered();
        if (auto* fontSys = resolveFontSystem())
        {
            m_observedAtlasGeneration = fontSys->getAtlasGeneration();
        }
        m_initialized = true;
    }

    void MQRmlFontEngine::Shutdown()
    {
        ReleaseFontResources();
        if (m_provider)
        {
            m_provider->shutdown();
            m_provider.reset();
        }
        if (m_ownedFontSystem)
        {
            m_ownedFontSystem->term();
            m_ownedFontSystem.reset();
        }
        m_initialized = false;
    }

    void MQRmlFontEngine::ReleaseFontResources()
    {
        m_localGlyphCache.clear();
        m_effectConfigs.clear();
        m_activeFaces.clear();
        m_faceLookup.clear();
        m_localShelfX = kGlyphPaddingPx;
        m_localShelfY = kGlyphPaddingPx;
        m_localShelfRowH = 0;
        ensureDefaultSystemFamilyRegistered();
    }

    void MQRmlFontEngine::bindFontSystem(Resource::Font::FontSystem* fontSystem) noexcept
    {
        m_externalFontSystem = fontSystem;
        if (fontSystem)
        {
            m_observedAtlasGeneration = fontSystem->getAtlasGeneration();
        }
    }

    Resource::Font::FontSystem* MQRmlFontEngine::resolveFontSystem()
    {
        if (m_externalFontSystem)
        {
            return m_externalFontSystem;
        }
        if (auto* globalFontSys = Font::FontRuntimeComposite::getActiveFontSystem())
        {
            return globalFontSys;
        }
        if (!m_ownedFontSystem)
        {
            m_ownedFontSystem = makeUnique<Resource::Font::FontSystem>();
            (void)m_ownedFontSystem->init(Resource::Font::FontBackendMode::MiniGame_Hybrid);
        }
        return m_ownedFontSystem.get();
    }

    const Resource::Font::FontSystem* MQRmlFontEngine::resolveFontSystem() const noexcept
    {
        if (m_externalFontSystem)
        {
            return m_externalFontSystem;
        }
        if (const auto* globalFontSys = Font::FontRuntimeComposite::getActiveFontSystem())
        {
            return globalFontSys;
        }
        return m_ownedFontSystem.get();
    }

    void MQRmlFontEngine::ensureDefaultSystemFamilyRegistered()
    {
        const std::string sysKey = normalizeFamilyKey("@system-ui");
        if (m_registeredFamilies.contains(sysKey))
        {
            return;
        }

        RegisteredFamily sysFam;
        sysFam.canonicalName = "@system-ui";
#if defined(_WIN32)
        // 使用系统半角/全角等比黑体家族名交由 DirectWrite 度量，严禁硬编码本地字体文件路径
        sysFam.providerFamily = "SimHei";
#else
        sysFam.providerFamily = "";
#endif
        sysFam.isSystemUi = true;
        sysFam.isFallback = false;
        m_registeredFamilies.emplace(sysKey, std::move(sysFam));
    }

    FCT::SAI::SystemFontProvider* MQRmlFontEngine::ensureFallbackProvider()
    {
        if (!m_provider)
        {
            m_provider = FCT::SAI::SystemFontProvider::create();
            if (m_provider)
            {
                (void)m_provider->init();
            }
        }
        return m_provider.get();
    }

    bool MQRmlFontEngine::LoadFontFace(
        const Rml::String& file_name,
        bool fallback_face,
        Rml::Style::FontWeight weight)
    {
        (void)weight;
        if (file_name.empty())
        {
            return false;
        }

        const std::string rawPath(file_name.c_str(), file_name.size());
        const std::string normKey = normalizeFamilyKey(rawPath);

        if (normKey == "@system-ui" || normKey == "system-ui")
        {
            ensureDefaultSystemFamilyRegistered();
            auto& entry = m_registeredFamilies[normalizeFamilyKey("@system-ui")];
            entry.isFallback = entry.isFallback || fallback_face;
            if (auto* fontSys = resolveFontSystem())
            {
                notifyFontSystemFaceFile(*fontSys, "@system-ui", "@system-ui", fallback_face);
            }
            return true;
        }

        std::string normPath = rawPath;
        std::replace(normPath.begin(), normPath.end(), '\\', '/');

        auto* resMod = Resource::Resource::InstancePtr();
        auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
        if (!loader || !loader->exists(normPath))
        {
            return false;
        }

        auto fileDataRes = loader->loadFile(normPath);
        if (!fileDataRes.ok() || fileDataRes->empty())
        {
            return false;
        }

        const std::string familyStem = extractFamilyFromPath(normPath);
        const std::string familyKey = normalizeFamilyKey(familyStem);
        if (familyKey.empty())
        {
            return false;
        }

        RegisteredFamily reg;
        reg.canonicalName = familyStem;
        reg.providerFamily = familyStem;
        reg.resourcePath = normPath;
        reg.isFallback = fallback_face;
        reg.isPrebakedMqFont = normPath.ends_with(".mqfont");
        m_registeredFamilies[familyKey] = std::move(reg);

        if (auto* fontSys = resolveFontSystem())
        {
            const auto* rawBytes = reinterpret_cast<const uint8_t*>(fileDataRes->data());
            notifyFontSystemFaceFile(*fontSys, familyStem, normPath, fallback_face);
            notifyFontSystemFaceMemory(*fontSys, familyStem, std::span<const uint8_t>(rawBytes, fileDataRes->size()), fallback_face);
        }
        return true;
    }

    bool MQRmlFontEngine::LoadFontFace(
        Rml::Span<const Rml::byte> data,
        const Rml::String& family,
        Rml::Style::FontStyle style,
        Rml::Style::FontWeight weight,
        bool fallback_face)
    {
        (void)style;
        (void)weight;
        if (family.empty() || data.empty())
        {
            return false;
        }

        const std::string famStr(family.c_str(), family.size());
        const std::string famKey = normalizeFamilyKey(famStr);

        RegisteredFamily reg;
        reg.canonicalName = famStr;
#if defined(_WIN32)
        reg.providerFamily = (famKey == "@system-ui" || famKey == "rmlui-debugger-font") ? "SimHei" : famStr;
#else
        reg.providerFamily = famStr;
#endif
        reg.isSystemUi = (famKey == "@system-ui");
        reg.isFallback = fallback_face;
        m_registeredFamilies[famKey] = std::move(reg);

        if (auto* fontSys = resolveFontSystem())
        {
            const std::span<const uint8_t> byteSpan(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            notifyFontSystemFaceMemory(*fontSys, famStr, byteSpan, fallback_face);
        }
        return true;
    }

    Rml::FontFaceHandle MQRmlFontEngine::GetFontFaceHandle(
        const Rml::String& family,
        Rml::Style::FontStyle style,
        Rml::Style::FontWeight weight,
        int size)
    {
        if (family.empty() || size <= 0)
        {
            return 0;
        }

        ensureDefaultSystemFamilyRegistered();
        std::string famKey = normalizeFamilyKey(std::string_view(family.c_str(), family.size()));
        if (famKey == "system-ui")
        {
            famKey = "@system-ui";
        }

        auto famIt = m_registeredFamilies.find(famKey);
        if (famIt == m_registeredFamilies.end())
        {
            return 0;
        }

        const uint16_t logicalPx = static_cast<uint16_t>(std::clamp(size, 1, 512));
        const uint16_t physicalPx = quantizePhysicalFontSize(logicalPx, m_devicePixelRatio);
        const uint8_t weightBucket = mapRmlWeightToBucket(weight);
        const bool isItalic = (style != Rml::Style::FontStyle::Normal);

        const std::string instanceKey = StrCat(
            famKey, "|", physicalPx, "|", static_cast<int>(weightBucket), "|", isItalic ? 1 : 0);

        auto lookupIt = m_faceLookup.find(instanceKey);
        if (lookupIt != m_faceLookup.end())
        {
            return lookupIt->second;
        }

        ActiveFaceInstance instance;
        instance.handle = m_nextFaceHandle++;
        instance.familyKey = famKey;
        instance.providerFamily = famIt->second.providerFamily;
        instance.logicalSizePx = logicalPx;
        instance.physicalSizePx = physicalPx;
        instance.weightBucket = weightBucket;
        instance.italic = isItalic;
        populateFaceMetrics(instance);

        const Rml::FontFaceHandle assignedHandle = instance.handle;
        m_activeFaces.emplace(assignedHandle, std::move(instance));
        m_faceLookup.emplace(instanceKey, assignedHandle);
        return assignedHandle;
    }

    Rml::FontFaceHandle MQRmlFontEngine::getFontFaceHandle(std::string_view family, int sizePx) noexcept
    {
        return GetFontFaceHandle(
            Rml::String(family.data(), family.size()),
            Rml::Style::FontStyle::Normal,
            Rml::Style::FontWeight::Normal,
            sizePx);
    }

    Rml::FontEffectsHandle MQRmlFontEngine::PrepareFontEffects(
        Rml::FontFaceHandle handle,
        const Rml::FontEffectList& font_effects)
    {
        (void)handle;
        if (font_effects.empty())
        {
            return 0;
        }

        PreparedEffectConfig config;
        config.layers.reserve(font_effects.size());

        Rml::FontGlyph sampleGlyph{};
        sampleGlyph.dimensions = {16, 16};
        sampleGlyph.bitmap_dimensions = {16, 16};

        for (const auto& effectPtr : font_effects)
        {
            if (!effectPtr)
            {
                continue;
            }
            RmlFontEffectLayerDesc desc;
            desc.layer = effectPtr->GetLayer();
            desc.colour = effectPtr->GetColour();
            desc.fingerprint = effectPtr->GetFingerprint();

            Rml::Vector2i origin(0, 0);
            Rml::Vector2i dims = sampleGlyph.bitmap_dimensions;
            if (effectPtr->GetGlyphMetrics(origin, dims, sampleGlyph))
            {
                desc.offset = Rml::Vector2f(static_cast<float>(origin.x), static_cast<float>(origin.y));
                const int extraW = std::max(0, dims.x - sampleGlyph.bitmap_dimensions.x);
                desc.inflatePx = static_cast<float>(extraW) * 0.5f;
            }
            config.layers.push_back(desc);
        }

        if (config.layers.empty())
        {
            return 0;
        }

        for (const auto& [existingHandle, existingCfg] : m_effectConfigs)
        {
            if (existingCfg.layers.size() != config.layers.size())
            {
                continue;
            }
            bool allMatch = true;
            for (size_t i = 0; i < config.layers.size(); ++i)
            {
                if (existingCfg.layers[i].fingerprint != config.layers[i].fingerprint ||
                    existingCfg.layers[i].layer != config.layers[i].layer ||
                    existingCfg.layers[i].colour != config.layers[i].colour)
                {
                    allMatch = false;
                    break;
                }
            }
            if (allMatch)
            {
                return existingHandle;
            }
        }

        const Rml::FontEffectsHandle newHandle = m_nextEffectsHandle++;
        m_effectConfigs.emplace(newHandle, std::move(config));
        return newHandle;
    }

    const Rml::FontMetrics& MQRmlFontEngine::GetFontMetrics(Rml::FontFaceHandle handle)
    {
        if (const auto* face = findFace(handle))
        {
            return face->metrics;
        }
        return m_fallbackMetrics;
    }

    FCT::SAI::FontMetrics MQRmlFontEngine::getSignedFontMetrics(Rml::FontFaceHandle handle) const noexcept
    {
        const Rml::FontMetrics& rmlMetrics = findFace(handle) ? findFace(handle)->metrics : m_fallbackMetrics;
        FCT::SAI::FontMetrics signedMetrics{};
        signedMetrics.ascent = rmlMetrics.ascent;
        signedMetrics.descent = -std::abs(rmlMetrics.descent);
        signedMetrics.lineSpacing = rmlMetrics.line_spacing;
        signedMetrics.xHeight = rmlMetrics.x_height;
        signedMetrics.underlinePosition = rmlMetrics.underline_position;
        signedMetrics.underlineThickness = rmlMetrics.underline_thickness;
        return signedMetrics;
    }

    int MQRmlFontEngine::GetStringWidth(
        Rml::FontFaceHandle handle,
        Rml::StringView string,
        const Rml::TextShapingContext& text_shaping_context,
        Rml::Character prior_character)
    {
        (void)prior_character;
        if (string.empty())
        {
            return 0;
        }

        const auto* face = findFace(handle);
        if (!face)
        {
            return 0;
        }

        syncGenerationWithFontSystem();

        std::vector<char32_t> codepoints;
        decodeUtf8String(std::string_view(string.begin(), string.size()), codepoints);
        if (codepoints.empty())
        {
            return 0;
        }

        batchWarmMissingCodepoints(*face, codepoints);

        int totalWidth = 0;
        const int spacingPx = static_cast<int>(text_shaping_context.letter_spacing);
        for (char32_t cp : codepoints)
        {
            if (isZeroWidthControlCodepoint(cp))
            {
                continue;
            }
            if (const auto* glyph = lookupOrSynthesizeGlyph(*face, cp))
            {
                totalWidth += glyph->advanceX + spacingPx;
            }
        }
        return std::max(totalWidth, 0);
    }

    int MQRmlFontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string)
    {
        static const Rml::String kEmptyLang;
        const Rml::TextShapingContext defaultCtx{kEmptyLang};
        return GetStringWidth(handle, string, defaultCtx, Rml::Character::Null);
    }

    int MQRmlFontEngine::getStringWidth(std::string_view text, int fontSizePx)
    {
        if (text.empty())
        {
            return 0;
        }
        const Rml::FontFaceHandle sysHandle = getFontFaceHandle("@system-ui", fontSizePx);
        return GetStringWidth(sysHandle, Rml::StringView(text.data(), text.data() + text.size()));
    }

    int MQRmlFontEngine::GenerateString(
        Rml::RenderManager& render_manager,
        Rml::FontFaceHandle face_handle,
        Rml::FontEffectsHandle font_effects_handle,
        Rml::StringView string,
        Rml::Vector2f position,
        Rml::ColourbPremultiplied colour,
        float opacity,
        const Rml::TextShapingContext& text_shaping_context,
        Rml::TexturedMeshList& mesh_list)
    {
        mesh_list.clear();
        if (string.empty())
        {
            return 0;
        }

        const auto* face = findFace(face_handle);
        if (!face)
        {
            return 0;
        }

        syncGenerationWithFontSystem();

        std::vector<char32_t> codepoints;
        decodeUtf8String(std::string_view(string.begin(), string.size()), codepoints);
        if (codepoints.empty())
        {
            return 0;
        }

        // Pass 1: 预扫描全串缺失字符，向微内核 FontSystem 发起单次拼车批量预热
        batchWarmMissingCodepoints(*face, codepoints);

        // Pass 2: 按动态图集页分组装配顶点网格并零拷贝绑定 @mq_glyph_atlas/<pageId>
        return emitStringGeometry(
            &render_manager,
            *face,
            font_effects_handle,
            codepoints,
            position,
            colour,
            opacity,
            text_shaping_context.letter_spacing,
            mesh_list);
    }

    int MQRmlFontEngine::GenerateString(
        Rml::RenderManager* renderManager,
        Rml::FontFaceHandle faceHandle,
        Rml::StringView string,
        Rml::Vector2f position,
        Rml::Colourb colour,
        std::vector<Rml::Mesh>& outMeshes)
    {
        outMeshes.clear();
        if (string.empty())
        {
            return 0;
        }

        const auto* face = findFace(faceHandle);
        if (!face)
        {
            return 0;
        }

        syncGenerationWithFontSystem();

        std::vector<char32_t> codepoints;
        decodeUtf8String(std::string_view(string.begin(), string.size()), codepoints);
        if (codepoints.empty())
        {
            return 0;
        }

        batchWarmMissingCodepoints(*face, codepoints);

        Rml::TexturedMeshList texturedList;
        const int totalWidth = emitStringGeometry(
            renderManager,
            *face,
            0,
            codepoints,
            position,
            colour.ToPremultiplied(1.0f),
            1.0f,
            0.0f,
            texturedList);

        outMeshes.reserve(texturedList.size());
        for (auto& tm : texturedList)
        {
            outMeshes.push_back(std::move(tm.mesh));
        }
        return totalWidth;
    }

    int MQRmlFontEngine::GetVersion(Rml::FontFaceHandle handle)
    {
        (void)handle;
        return getGeneration();
    }

    int MQRmlFontEngine::getGeneration() const noexcept
    {
        uint32_t baseGen = m_observedAtlasGeneration;
        if (const auto* fontSys = resolveFontSystem())
        {
            baseGen = fontSys->getAtlasGeneration();
        }
        return static_cast<int>(baseGen + m_manualInvalidationOffset);
    }

    void MQRmlFontEngine::notifyAtlasInvalidated() noexcept
    {
        ++m_manualInvalidationOffset;
        m_localGlyphCache.clear();
        m_localShelfX = kGlyphPaddingPx;
        m_localShelfY = kGlyphPaddingPx;
        m_localShelfRowH = 0;
    }

    void MQRmlFontEngine::setDevicePixelRatio(float dpr) noexcept
    {
        const float clamped = std::clamp(dpr, 0.5f, 4.0f);
        if (std::abs(clamped - m_devicePixelRatio) > 0.01f)
        {
            m_devicePixelRatio = clamped;
            notifyAtlasInvalidated();
        }
    }

    uint16_t MQRmlFontEngine::quantizePhysicalFontSize(int logicalSizePx, float dpr) noexcept
    {
        const float safeDpr = std::clamp(dpr, 0.5f, 4.0f);
        int rawPx = std::max(1, static_cast<int>(std::lround(static_cast<float>(logicalSizePx) * safeDpr)));
        if (rawPx > 64)
        {
            rawPx = ((rawPx + 3) / 4) * 4;
        }
        else if (rawPx > 32)
        {
            rawPx = ((rawPx + 1) / 2) * 2;
        }
        return static_cast<uint16_t>(std::min(rawPx, 512));
    }

    std::string MQRmlFontEngine::buildAtlasPageUri(uint32_t pageId, bool isEmoji)
    {
        if (isEmoji)
        {
            return StrCat("@mq_glyph_atlas/", pageId, "/emoji");
        }
        return StrCat("@mq_glyph_atlas/", pageId);
    }

    void MQRmlFontEngine::decodeUtf8String(std::string_view utf8, std::vector<char32_t>& outCodepoints)
    {
        outCodepoints.clear();
        outCodepoints.reserve(utf8.size());

        const auto* bytes = reinterpret_cast<const uint8_t*>(utf8.data());
        const size_t len = utf8.size();
        size_t i = 0;

        while (i < len)
        {
            const uint8_t b0 = bytes[i];
            if ((b0 & 0x80u) == 0u)
            {
                outCodepoints.push_back(static_cast<char32_t>(b0));
                ++i;
            }
            else if ((b0 & 0xE0u) == 0xC0u)
            {
                if (i + 1 < len && (bytes[i + 1] & 0xC0u) == 0x80u)
                {
                    const char32_t cp = ((static_cast<char32_t>(b0 & 0x1Fu) << 6) |
                                         static_cast<char32_t>(bytes[i + 1] & 0x3Fu));
                    outCodepoints.push_back(cp >= 0x80u ? cp : kReplacementCodepoint);
                    i += 2;
                }
                else
                {
                    outCodepoints.push_back(kReplacementCodepoint);
                    ++i;
                }
            }
            else if ((b0 & 0xF0u) == 0xE0u)
            {
                if (i + 2 < len &&
                    (bytes[i + 1] & 0xC0u) == 0x80u &&
                    (bytes[i + 2] & 0xC0u) == 0x80u)
                {
                    const char32_t cp = ((static_cast<char32_t>(b0 & 0x0Fu) << 12) |
                                         (static_cast<char32_t>(bytes[i + 1] & 0x3Fu) << 6) |
                                         static_cast<char32_t>(bytes[i + 2] & 0x3Fu));
                    const bool isSurrogate = (cp >= 0xD800u && cp <= 0xDFFFu);
                    outCodepoints.push_back((cp >= 0x800u && !isSurrogate) ? cp : kReplacementCodepoint);
                    i += 3;
                }
                else
                {
                    outCodepoints.push_back(kReplacementCodepoint);
                    ++i;
                }
            }
            else if ((b0 & 0xF8u) == 0xF0u)
            {
                if (i + 3 < len &&
                    (bytes[i + 1] & 0xC0u) == 0x80u &&
                    (bytes[i + 2] & 0xC0u) == 0x80u &&
                    (bytes[i + 3] & 0xC0u) == 0x80u)
                {
                    const char32_t cp = ((static_cast<char32_t>(b0 & 0x07u) << 18) |
                                         (static_cast<char32_t>(bytes[i + 1] & 0x3Fu) << 12) |
                                         (static_cast<char32_t>(bytes[i + 2] & 0x3Fu) << 6) |
                                         static_cast<char32_t>(bytes[i + 3] & 0x3Fu));
                    outCodepoints.push_back((cp >= 0x10000u && cp <= 0x10FFFFu) ? cp : kReplacementCodepoint);
                    i += 4;
                }
                else
                {
                    outCodepoints.push_back(kReplacementCodepoint);
                    ++i;
                }
            }
            else
            {
                outCodepoints.push_back(kReplacementCodepoint);
                ++i;
            }
        }
    }

    MQRmlFontEngine::ActiveFaceInstance* MQRmlFontEngine::findFaceMutable(Rml::FontFaceHandle handle) noexcept
    {
        auto it = m_activeFaces.find(handle);
        return (it != m_activeFaces.end()) ? &it->second : nullptr;
    }

    const MQRmlFontEngine::ActiveFaceInstance* MQRmlFontEngine::findFace(Rml::FontFaceHandle handle) const noexcept
    {
        auto it = m_activeFaces.find(handle);
        return (it != m_activeFaces.end()) ? &it->second : nullptr;
    }

    void MQRmlFontEngine::populateFaceMetrics(ActiveFaceInstance& face)
    {
        const float px = static_cast<float>(face.physicalSizePx);
        FCT::SAI::FontMetrics saiMetrics{};
        bool resolved = false;

        if (auto* fontSys = resolveFontSystem())
        {
            resolved = tryQueryMetricsFromFontSystem(
                *fontSys,
                face.providerFamily,
                face.physicalSizePx,
                face.weightBucket,
                face.italic,
                saiMetrics);
        }

        if (!resolved)
        {
            if (auto* provider = ensureFallbackProvider())
            {
                auto res = provider->getFontMetrics(
                    face.providerFamily,
                    face.physicalSizePx,
                    face.weightBucket,
                    face.italic);
                if (res.ok())
                {
                    saiMetrics = *res;
                    resolved = true;
                }
            }
        }

        face.metrics.size = static_cast<int>(face.physicalSizePx);
        if (resolved && saiMetrics.ascent > 0.0f)
        {
            face.metrics.ascent = saiMetrics.ascent;
            face.metrics.descent = std::max(1.0f, std::abs(saiMetrics.descent));
            face.metrics.line_spacing = std::max(
                saiMetrics.lineSpacing,
                face.metrics.ascent + face.metrics.descent);
            face.metrics.x_height = saiMetrics.xHeight > 0.0f
                ? saiMetrics.xHeight
                : 0.5f * face.metrics.ascent;
            face.metrics.underline_position = std::abs(saiMetrics.underlinePosition) > 0.0f
                ? std::abs(saiMetrics.underlinePosition)
                : std::max(1.0f, px * 0.1f);
            face.metrics.underline_thickness = std::max(1.0f, saiMetrics.underlineThickness);
        }
        else
        {
            face.metrics.ascent = std::max(1.0f, std::round(px * 0.8f));
            face.metrics.descent = std::max(1.0f, std::round(px * 0.2f));
            face.metrics.line_spacing = std::max(
                face.metrics.ascent + face.metrics.descent,
                std::round(px * 1.2f));
            face.metrics.x_height = std::max(1.0f, std::round(px * 0.5f));
            face.metrics.underline_position = std::max(1.0f, std::round(px * 0.1f));
            face.metrics.underline_thickness = std::max(1.0f, std::round(px * 0.06f));
        }
    }

    void MQRmlFontEngine::syncGenerationWithFontSystem()
    {
        if (auto* fontSys = resolveFontSystem())
        {
            const uint32_t currentGen = fontSys->getAtlasGeneration();
            if (currentGen != m_observedAtlasGeneration)
            {
                m_observedAtlasGeneration = currentGen;
                m_localGlyphCache.clear();
                m_localShelfX = kGlyphPaddingPx;
                m_localShelfY = kGlyphPaddingPx;
                m_localShelfRowH = 0;
            }
        }
    }

    void MQRmlFontEngine::batchWarmMissingCodepoints(
        const ActiveFaceInstance& face,
        std::span<const char32_t> codepoints)
    {
        std::vector<char32_t> missing;
        missing.reserve(std::min<size_t>(codepoints.size(), 128u));

        for (char32_t cp : codepoints)
        {
            if (isZeroWidthControlCodepoint(cp))
            {
                continue;
            }
            GlyphCacheKey key{
                .familyKey = face.familyKey,
                .physicalSizePx = face.physicalSizePx,
                .weightBucket = face.weightBucket,
                .italic = face.italic,
                .codepoint = cp
            };
            if (m_localGlyphCache.contains(key))
            {
                continue;
            }
            if (std::find(missing.begin(), missing.end(), cp) == missing.end())
            {
                missing.push_back(cp);
            }
        }

        if (missing.empty())
        {
            m_lastBatchWarmCount = 0;
            return;
        }

        m_lastBatchWarmCount = static_cast<uint32_t>(missing.size());

        if (auto* fontSys = resolveFontSystem())
        {
            invokeEnsureGlyphsCached(
                *fontSys,
                missing,
                face.physicalSizePx,
                face.providerFamily,
                face.weightBucket,
                face.italic);
            m_observedAtlasGeneration = fontSys->getAtlasGeneration();

            // 先尝试直接复用 FontSystem 内部字形 UV 缓存
            std::vector<char32_t> stillUnmapped;
            for (char32_t cp : missing)
            {
                RmlCachedGlyphEntry fromSys{};
                if (tryPullGlyphFromFontSystem(
                        *fontSys,
                        face.providerFamily,
                        cp,
                        face.physicalSizePx,
                        face.weightBucket,
                        face.italic,
                        fromSys))
                {
                    if (cp < 0x80u && face.familyKey == "@system-ui")
                    {
                        fromSys.advanceX = std::max(1, static_cast<int32_t>(face.physicalSizePx) / 2);
                    }
                    else if (fromSys.isEmoji)
                    {
                        fromSys.advanceX = static_cast<int32_t>(face.physicalSizePx);
                    }
                    GlyphCacheKey key{
                        .familyKey = face.familyKey,
                        .physicalSizePx = face.physicalSizePx,
                        .weightBucket = face.weightBucket,
                        .italic = face.italic,
                        .codepoint = cp
                    };
                    m_localGlyphCache.emplace(std::move(key), fromSys);
                }
                else
                {
                    stillUnmapped.push_back(cp);
                }
            }
            missing = std::move(stillUnmapped);
        }

        if (missing.empty())
        {
            return;
        }

        // 对尚未返回精确 UV 的新字形，经由 SystemFontProvider 单次拼车度量并同步图集 UV
        std::vector<FCT::SAI::GlyphRequest> requests;
        requests.reserve(missing.size());
        for (char32_t cp : missing)
        {
            requests.push_back(FCT::SAI::GlyphRequest{
                .codepoint = static_cast<uint32_t>(cp),
                .fontSizePx = face.physicalSizePx,
                .weight = face.weightBucket,
                .italic = face.italic,
                .isEmoji = isColorEmojiCodepoint(cp)
            });
        }

        if (auto* provider = ensureFallbackProvider())
        {
            auto batchRes = provider->rasterizeBatch(
                face.providerFamily,
                face.physicalSizePx,
                face.weightBucket,
                face.italic,
                requests);
            if (batchRes.ok() && batchRes->glyphs.size() == missing.size())
            {
                for (size_t idx = 0; idx < missing.size(); ++idx)
                {
                    const char32_t cp = missing[idx];
                    const auto& gm = batchRes->glyphs[idx];
                    const uint32_t gw = static_cast<uint32_t>(std::max(1, gm.width));
                    const uint32_t gh = static_cast<uint32_t>(std::max(1, gm.height));

                    if (m_localShelfX + gw + kGlyphPaddingPx > kAtlasPageDimension)
                    {
                        m_localShelfX = kGlyphPaddingPx;
                        m_localShelfY += m_localShelfRowH + kGlyphPaddingPx;
                        m_localShelfRowH = 0;
                    }
                    if (m_localShelfY + gh + kGlyphPaddingPx > kAtlasPageDimension)
                    {
                        ++m_localPageIndex;
                        m_localShelfX = kGlyphPaddingPx;
                        m_localShelfY = kGlyphPaddingPx;
                        m_localShelfRowH = 0;
                    }

                    RmlCachedGlyphEntry entry;
                    entry.isEmoji = isColorEmojiCodepoint(cp);
                    entry.pageId = m_localPageIndex;
                    entry.width = static_cast<int32_t>(gw);
                    entry.height = static_cast<int32_t>(gh);
                    entry.bearingX = gm.bearingX;
                    entry.bearingY = gm.bearingY > 0
                        ? gm.bearingY
                        : static_cast<int32_t>(std::lround(face.metrics.ascent));
                    entry.advanceX = std::max(1, gm.advanceX);
                    if (cp < 0x80u && face.familyKey == "@system-ui")
                    {
                        // 确保半角 ASCII 度量跨语言环境稳定一致
                        entry.advanceX = std::max(1, static_cast<int32_t>(face.physicalSizePx) / 2);
                    }
                    else if (entry.isEmoji)
                    {
                        entry.advanceX = static_cast<int32_t>(face.physicalSizePx);
                    }
                    entry.u0 = static_cast<float>(m_localShelfX) / static_cast<float>(kAtlasPageDimension);
                    entry.v0 = static_cast<float>(m_localShelfY) / static_cast<float>(kAtlasPageDimension);
                    entry.u1 = static_cast<float>(m_localShelfX + gw) / static_cast<float>(kAtlasPageDimension);
                    entry.v1 = static_cast<float>(m_localShelfY + gh) / static_cast<float>(kAtlasPageDimension);
                    entry.generation = m_observedAtlasGeneration;

                    m_localShelfX += gw + kGlyphPaddingPx;
                    m_localShelfRowH = std::max(m_localShelfRowH, gh);

                    GlyphCacheKey key{
                        .familyKey = face.familyKey,
                        .physicalSizePx = face.physicalSizePx,
                        .weightBucket = face.weightBucket,
                        .italic = face.italic,
                        .codepoint = cp
                    };
                    m_localGlyphCache.emplace(std::move(key), entry);
                }
                return;
            }
        }

        // 兜底合成确定性字形框，保证无头环境亦不丢失排版进度
        for (char32_t cp : missing)
        {
            (void)lookupOrSynthesizeGlyph(face, cp);
        }
    }

    const RmlCachedGlyphEntry* MQRmlFontEngine::lookupOrSynthesizeGlyph(
        const ActiveFaceInstance& face,
        char32_t codepoint)
    {
        GlyphCacheKey key{
            .familyKey = face.familyKey,
            .physicalSizePx = face.physicalSizePx,
            .weightBucket = face.weightBucket,
            .italic = face.italic,
            .codepoint = codepoint
        };

        auto it = m_localGlyphCache.find(key);
        if (it != m_localGlyphCache.end())
        {
            return &it->second;
        }

        const bool emoji = isColorEmojiCodepoint(codepoint);
        const int32_t fullPx = static_cast<int32_t>(face.physicalSizePx);
        const int32_t advancePx = (codepoint < 0x80u) ? std::max(1, fullPx / 2) : fullPx;

        RmlCachedGlyphEntry fallbackEntry;
        fallbackEntry.pageId = m_localPageIndex;
        fallbackEntry.isEmoji = emoji;
        fallbackEntry.width = advancePx;
        fallbackEntry.height = fullPx;
        fallbackEntry.bearingX = 0;
        fallbackEntry.bearingY = static_cast<int32_t>(std::lround(face.metrics.ascent));
        fallbackEntry.advanceX = advancePx;
        fallbackEntry.u0 = 0.0f;
        fallbackEntry.v0 = 0.0f;
        fallbackEntry.u1 = static_cast<float>(advancePx) / static_cast<float>(kAtlasPageDimension);
        fallbackEntry.v1 = static_cast<float>(fullPx) / static_cast<float>(kAtlasPageDimension);
        fallbackEntry.generation = m_observedAtlasGeneration;

        auto [insertedIt, _] = m_localGlyphCache.emplace(std::move(key), fallbackEntry);
        return &insertedIt->second;
    }

    int MQRmlFontEngine::emitStringGeometry(
        Rml::RenderManager* renderManager,
        const ActiveFaceInstance& face,
        Rml::FontEffectsHandle effectsHandle,
        std::span<const char32_t> codepoints,
        Rml::Vector2f position,
        Rml::ColourbPremultiplied baseColour,
        float opacity,
        float letterSpacing,
        Rml::TexturedMeshList& outTexturedMeshes)
    {
        struct PageBucketKey
        {
            uint32_t pageId = 0;
            bool isEmoji = false;

            bool operator==(const PageBucketKey& o) const noexcept = default;
        };

        std::vector<PageBucketKey> bucketKeys;

        auto findOrCreateMeshBucket = [&](uint32_t pageId, bool isEmoji) -> Rml::TexturedMesh& {
            const PageBucketKey key{pageId, isEmoji};
            for (size_t i = 0; i < bucketKeys.size(); ++i)
            {
                if (bucketKeys[i] == key)
                {
                    return outTexturedMeshes[i];
                }
            }
            bucketKeys.push_back(key);
            outTexturedMeshes.emplace_back();
            auto& bucket = outTexturedMeshes.back();
            if (renderManager)
            {
                const std::string uri = buildAtlasPageUri(pageId, isEmoji);
                bucket.texture = renderManager->LoadTexture(Rml::String(uri.c_str(), uri.size()));
            }
            bucket.mesh.vertices.reserve(codepoints.size() * 4u);
            bucket.mesh.indices.reserve(codepoints.size() * 6u);
            return bucket;
        };

        const PreparedEffectConfig* effectCfg = nullptr;
        if (effectsHandle != 0)
        {
            auto effIt = m_effectConfigs.find(effectsHandle);
            if (effIt != m_effectConfigs.end())
            {
                effectCfg = &effIt->second;
            }
        }

        auto emitLayerPass = [&](Rml::Vector2f layerOffset, float inflatePx, Rml::ColourbPremultiplied layerCol, bool isBaseLayer) -> int {
            int penX = 0;
            const int spacingPx = static_cast<int>(letterSpacing);

            for (char32_t cp : codepoints)
            {
                if (isZeroWidthControlCodepoint(cp))
                {
                    continue;
                }
                const auto* glyph = lookupOrSynthesizeGlyph(face, cp);
                if (!glyph)
                {
                    continue;
                }

                // 空白字符仅推进排版游标，无需生成空白四边形
                if (cp != U' ' && cp != U'\t' && cp != U'\r' && cp != U'\n' &&
                    glyph->width > 0 && glyph->height > 0)
                {
                    auto& bucket = findOrCreateMeshBucket(glyph->pageId, glyph->isEmoji);
                    Rml::ColourbPremultiplied quadCol = layerCol;
                    if (isBaseLayer && glyph->isEmoji)
                    {
                        // 彩色 Emoji 采用白色预乘顶点色，保留 RGBA8 图集原始色彩
                        quadCol = Rml::ColourbPremultiplied(layerCol.alpha, layerCol.alpha);
                    }

                    const Rml::Vector2f quadOrigin(
                        std::round(position.x + static_cast<float>(penX + glyph->bearingX) + layerOffset.x - inflatePx),
                        std::round(position.y - static_cast<float>(glyph->bearingY) + layerOffset.y - inflatePx));
                    const Rml::Vector2f quadSize(
                        static_cast<float>(glyph->width) + inflatePx * 2.0f,
                        static_cast<float>(glyph->height) + inflatePx * 2.0f);

                    Rml::MeshUtilities::GenerateQuad(
                        bucket.mesh,
                        quadOrigin,
                        quadSize,
                        quadCol,
                        Rml::Vector2f(glyph->u0, glyph->v0),
                        Rml::Vector2f(glyph->u1, glyph->v1));
                }

                penX += glyph->advanceX + spacingPx;
            }
            return std::max(penX, 0);
        };

        if (effectCfg)
        {
            for (const auto& eff : effectCfg->layers)
            {
                if (eff.layer == Rml::FontEffect::Layer::Back)
                {
                    (void)emitLayerPass(eff.offset, eff.inflatePx, eff.colour.ToPremultiplied(opacity), false);
                }
            }
        }

        const int finalAdvanceWidth = emitLayerPass(Rml::Vector2f(0.0f, 0.0f), 0.0f, baseColour, true);

        if (effectCfg)
        {
            for (const auto& eff : effectCfg->layers)
            {
                if (eff.layer == Rml::FontEffect::Layer::Front)
                {
                    (void)emitLayerPass(eff.offset, eff.inflatePx, eff.colour.ToPremultiplied(opacity), false);
                }
            }
        }

        return finalAdvanceWidth;
    }

    std::string MQRmlFontEngine::normalizeFamilyKey(std::string_view raw)
    {
        size_t start = 0;
        size_t end = raw.size();
        while (start < end && (raw[start] == ' ' || raw[start] == '\t' || raw[start] == '"' || raw[start] == '\''))
        {
            ++start;
        }
        while (end > start && (raw[end - 1] == ' ' || raw[end - 1] == '\t' || raw[end - 1] == '"' || raw[end - 1] == '\''))
        {
            --end;
        }

        std::string result;
        result.reserve(end - start);
        for (size_t i = start; i < end; ++i)
        {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i]))));
        }
        return result;
    }

    std::string MQRmlFontEngine::extractFamilyFromPath(std::string_view path)
    {
        const size_t slashPos = path.find_last_of("/\\");
        std::string_view fileName = (slashPos == std::string_view::npos) ? path : path.substr(slashPos + 1);
        const size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string_view::npos && dotPos > 0)
        {
            fileName = fileName.substr(0, dotPos);
        }
        const size_t dashPos = fileName.find('-');
        if (dashPos != std::string_view::npos && dashPos > 0)
        {
            fileName = fileName.substr(0, dashPos);
        }
        return std::string(fileName);
    }

    uint8_t MQRmlFontEngine::mapRmlWeightToBucket(Rml::Style::FontWeight weight) noexcept
    {
        if (weight == Rml::Style::FontWeight::Auto)
        {
            return 4u;
        }
        const int numericWeight = static_cast<int>(weight);
        return static_cast<uint8_t>(std::clamp(numericWeight / 100, 1, 9));
    }

    bool MQRmlFontEngine::isZeroWidthControlCodepoint(char32_t cp) noexcept
    {
        return cp == 0x200Du || // Zero Width Joiner (Emoji ZWJ)
               cp == 0x200Bu || // Zero Width Space
               cp == 0xFEFFu || // BOM / Zero Width No-Break Space
               (cp >= 0xFE00u && cp <= 0xFE0Fu); // Variation Selectors (VS1..VS16)
    }

    bool MQRmlFontEngine::isColorEmojiCodepoint(char32_t cp) noexcept
    {
        return cp >= 0x1F000u ||
               (cp >= 0x2600u && cp <= 0x27BFu);
    }
} // namespace MQEngine::GameHtmlUI
