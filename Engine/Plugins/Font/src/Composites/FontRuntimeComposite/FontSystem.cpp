/**
 * @file FontSystem.cpp
 * @brief 微内核双轨字体系统实现
 */
#include "FontSystem.h"
#include <MQEngine/resource/core/resource.h>

namespace MQEngine::Resource::Font
{
    FontSystem::FontSystem()
    {
        applyBackendModeFlags(FontBackendMode::Desktop_FreeType);
        seedDefaultPrebakedSubset();
    }

    FontSystem::FontSystem(FontBackendMode mode)
    {
        applyBackendModeFlags(mode);
        seedDefaultPrebakedSubset();
    }

    FontSystem::~FontSystem()
    {
        term();
    }

    void FontSystem::applyBackendModeFlags(FontBackendMode mode) noexcept
    {
        m_mode = mode;
        if (m_mode == FontBackendMode::Desktop_FreeType)
        {
            m_freeTypeActive = true;
            m_hintingEnabled = true;
        }
        else
        {
            m_freeTypeActive = false;
            m_hintingEnabled = false;
        }
    }

    void FontSystem::seedDefaultPrebakedSubset()
    {
        const std::u32string commonSubset = U"确定取消关闭打开进入返回设置开始暂停重试继续胜利失败得分金币等级";
        for (char32_t cp : commonSubset)
        {
            m_prebakedSubset.insert(cp);
        }
    }

    Status FontSystem::init(FontBackendMode mode)
    {
        ZoneScoped();
        applyBackendModeFlags(mode);
        seedDefaultPrebakedSubset();

        if (!m_systemFontProvider)
        {
            m_systemFontProvider = FCT::SAI::SystemFontProvider::create();
            if (m_systemFontProvider)
            {
                CHECK_STATUS(m_systemFontProvider->init());
            }
        }

        if (!m_bakedFont.isLoaded())
        {
            m_bakedFont.initDefaultCombatPreset(512, 512);
        }

        m_initialized = true;
        return OkStatus();
    }

    Status FontSystem::initGpuResources(FCT::Context* ctx)
    {
        return m_atlasManager.initGpuResources(ctx);
    }

    void FontSystem::term() noexcept
    {
        ZoneScoped();
        if (m_systemFontProvider)
        {
            m_systemFontProvider->shutdown();
            m_systemFontProvider = nullptr;
        }
        m_atlasManager.releaseGpuResources();
        m_cachedGlyphs.clear();
        m_registeredFontBlobs.clear();
        m_initialized = false;
    }

    Status FontSystem::registerFontFace(std::string_view family, std::string_view resourcePath, bool fallbackFace)
    {
        ZoneScoped();
        if (family.empty() || resourcePath.empty())
        {
            return InvalidArgumentError("字体族名与资源路径不能为空");
        }

        if (auto* resMod = ::MQEngine::Resource::Resource::InstancePtr())
        {
            if (auto* loader = resMod->getResourceLoader())
            {
                if (loader->exists(std::string(resourcePath)))
                {
                    auto textRes = loader->loadFile(std::string(resourcePath));
                    if (textRes.ok())
                    {
                        std::span<const uint8_t> rawSpan(
                            reinterpret_cast<const uint8_t*>(textRes->data()),
                            textRes->size());
                        return registerFontFaceMemory(family, rawSpan, fallbackFace);
                    }
                }
            }
        }

        if (FCT::SAI::FileSystem::exists(resourcePath))
        {
            auto bytesRes = FCT::SAI::FileSystem::readBinaryFile(resourcePath);
            if (bytesRes.ok())
            {
                return registerFontFaceMemory(family, *bytesRes, fallbackFace);
            }
        }

        // 若外部仅登记系统字体名映射（如小游戏端系统字库），记录空载荷族名并正常生效
        m_registeredFontBlobs[std::string(family)] = {};
        if (fallbackFace || m_fallbackFamily.empty())
        {
            m_fallbackFamily = std::string(family);
        }
        return OkStatus();
    }

    Status FontSystem::registerFontFaceMemory(
        std::string_view family,
        std::span<const uint8_t> data,
        bool fallbackFace)
    {
        if (family.empty())
        {
            return InvalidArgumentError("字体族名不能为空");
        }
        m_registeredFontBlobs[std::string(family)] = std::vector<uint8_t>(data.begin(), data.end());
        if (fallbackFace || m_fallbackFamily.empty())
        {
            m_fallbackFamily = std::string(family);
        }
        return OkStatus();
    }

    bool FontSystem::hasFontFace(std::string_view family) const noexcept
    {
        return m_registeredFontBlobs.find(std::string(family)) != m_registeredFontBlobs.end();
    }

    StatusOr<FCT::SAI::FontMetrics> FontSystem::queryFontMetrics(
        std::string_view family,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic)
    {
        ZoneScoped();
        if (fontSizePx == 0)
        {
            return InvalidArgumentError("字号像素必须大于 0");
        }

        const std::string_view effectiveFamily = family.empty() ? std::string_view(m_fallbackFamily) : family;
        if (m_systemFontProvider)
        {
            auto res = m_systemFontProvider->getFontMetrics(effectiveFamily, fontSizePx, weight, italic);
            if (res.ok())
            {
                return res;
            }
        }

        FCT::SAI::FontMetrics fallback{};
        const float px = static_cast<float>(fontSizePx);
        fallback.ascent = px * 0.8f;
        fallback.descent = px * 0.2f;
        fallback.lineSpacing = px * 1.2f;
        fallback.xHeight = px * 0.5f;
        fallback.underlinePosition = -px * 0.1f;
        fallback.underlineThickness = std::max(1.0f, px * 0.06f);
        return fallback;
    }

    void FontSystem::registerPrebakedSubset(std::span<const char32_t> codepoints)
    {
        for (char32_t cp : codepoints)
        {
            m_prebakedSubset.insert(cp);
        }
    }

    bool FontSystem::isGlyphInPrebakedSubset(char32_t codepoint) const noexcept
    {
        return m_prebakedSubset.find(codepoint) != m_prebakedSubset.end();
    }

    bool FontSystem::queriesSystemFontProvider(char32_t codepoint) const noexcept
    {
        return !isGlyphInPrebakedSubset(codepoint);
    }

    Status FontSystem::uploadSingleRasterizedGlyph(
        const FCT::SAI::RasterizedGlyphBatch& batch,
        const FCT::SAI::GlyphMetrics& gm,
        const GlyphCacheKey& key)
    {
        ZoneScoped();
        const uint32_t gw = static_cast<uint32_t>(std::max(0, gm.width));
        const uint32_t gh = static_cast<uint32_t>(std::max(0, gm.height));

        uint32_t pageId = 0;
        Domain::Renderer::AtlasAllocation alloc{};
        CHECK_STATUS(m_atlasManager.allocateGlyph(key.codepoint, gw, gh, pageId, alloc));

        const TextureFormat targetFmt = GlyphAtlasManager::getGlyphFormat(key.codepoint);
        if (gw > 0 && gh > 0 && batch.atlasWidth > 0 && batch.atlasHeight > 0 && !batch.pixelData.empty())
        {
            const size_t srcBpp = (batch.format == FCT::SAI::FontPixelFormat::RGBA8_UNORM) ? 4u : 1u;
            const size_t dstBpp = (targetFmt == TextureFormat::R8G8B8A8_UNORM) ? 4u : 1u;
            std::vector<uint8_t> subBitmap(static_cast<size_t>(gw) * gh * dstBpp, 0u);

            for (uint32_t row = 0; row < gh; ++row)
            {
                const uint32_t srcY = gm.atlasY + row;
                if (srcY >= batch.atlasHeight)
                {
                    break;
                }
                for (uint32_t col = 0; col < gw; ++col)
                {
                    const uint32_t srcX = gm.atlasX + col;
                    if (srcX >= batch.atlasWidth)
                    {
                        break;
                    }
                    const size_t srcIdx = (static_cast<size_t>(srcY) * batch.atlasWidth + srcX) * srcBpp;
                    const size_t dstIdx = (static_cast<size_t>(row) * gw + col) * dstBpp;
                    if (srcIdx + srcBpp > batch.pixelData.size())
                    {
                        continue;
                    }

                    if (srcBpp == dstBpp)
                    {
                        std::memcpy(&subBitmap[dstIdx], &batch.pixelData[srcIdx], dstBpp);
                    }
                    else if (srcBpp == 4u && dstBpp == 1u)
                    {
                        subBitmap[dstIdx] = batch.pixelData[srcIdx + 3u];
                    }
                    else
                    {
                        const uint8_t alpha = batch.pixelData[srcIdx];
                        subBitmap[dstIdx + 0u] = 255u;
                        subBitmap[dstIdx + 1u] = 255u;
                        subBitmap[dstIdx + 2u] = 255u;
                        subBitmap[dstIdx + 3u] = alpha;
                    }
                }
            }

            CHECK_STATUS(m_atlasManager.uploadGlyphBitmap(pageId, alloc, subBitmap.data(), subBitmap.size()));
        }

        CachedUiGlyph cached{};
        cached.pageId = pageId;
        cached.format = targetFmt;
        cached.generation = m_atlasManager.getAtlasGeneration();
        cached.metric.width = static_cast<float>(gw);
        cached.metric.height = static_cast<float>(gh);
        cached.metric.bearingX = static_cast<float>(gm.bearingX);
        cached.metric.bearingY = static_cast<float>(gm.bearingY);
        cached.metric.advanceX = static_cast<float>(gm.advanceX);
        cached.metric.advanceY = static_cast<float>(gm.advanceY);
        cached.metric.uvMinMax[0] = static_cast<uint16_t>((alloc.x * 65535u) / GlyphAtlasManager::AtlasPageWidth);
        cached.metric.uvMinMax[1] = static_cast<uint16_t>((alloc.y * 65535u) / GlyphAtlasManager::AtlasPageHeight);
        cached.metric.uvMinMax[2] = static_cast<uint16_t>(((alloc.x + alloc.width) * 65535u) / GlyphAtlasManager::AtlasPageWidth);
        cached.metric.uvMinMax[3] = static_cast<uint16_t>(((alloc.y + alloc.height) * 65535u) / GlyphAtlasManager::AtlasPageHeight);

        m_cachedGlyphs[key] = cached;
        return OkStatus();
    }

    Status FontSystem::ensureGlyphsCached(
        std::span<const char32_t> codepoints,
        uint16_t fontSizePx,
        std::string_view family,
        uint8_t weight,
        bool italic)
    {
        ZoneScoped();
        m_lastBatchCodepointCount = static_cast<uint32_t>(codepoints.size());
        if (codepoints.empty())
        {
            return OkStatus();
        }
        if (fontSizePx == 0)
        {
            return InvalidArgumentError("字号像素必须大于 0");
        }

        const std::string resolvedFamily = family.empty() ? m_fallbackFamily : std::string(family);
        const uint32_t currentGen = m_atlasManager.getAtlasGeneration();

        std::vector<FCT::SAI::GlyphRequest> missingRequests;
        missingRequests.reserve(codepoints.size());
        for (char32_t cp : codepoints)
        {
            GlyphCacheKey key{resolvedFamily, cp, fontSizePx, weight, italic};
            auto it = m_cachedGlyphs.find(key);
            if (it != m_cachedGlyphs.end() && it->second.generation == currentGen)
            {
                continue;
            }

            FCT::SAI::GlyphRequest req{};
            req.codepoint = static_cast<uint32_t>(cp);
            req.fontSizePx = fontSizePx;
            req.weight = weight;
            req.italic = italic;
            req.isEmoji = (GlyphAtlasManager::getGlyphFormat(cp) == TextureFormat::R8G8B8A8_UNORM);
            missingRequests.push_back(req);
        }

        if (missingRequests.empty())
        {
            return OkStatus();
        }

        if (m_systemFontProvider)
        {
            auto batchRes = m_systemFontProvider->rasterizeBatch(
                resolvedFamily, fontSizePx, weight, italic, missingRequests);
            if (batchRes.ok())
            {
                const auto& batch = *batchRes;
                for (const auto& gm : batch.glyphs)
                {
                    GlyphCacheKey key{resolvedFamily, static_cast<char32_t>(gm.codepoint), fontSizePx, weight, italic};
                    CHECK_STATUS(uploadSingleRasterizedGlyph(batch, gm, key));
                }

                const uint32_t refreshedGen = m_atlasManager.getAtlasGeneration();
                for (const auto& req : missingRequests)
                {
                    const char32_t cp = static_cast<char32_t>(req.codepoint);
                    GlyphCacheKey key{resolvedFamily, cp, fontSizePx, weight, italic};
                    auto existingIt = m_cachedGlyphs.find(key);
                    if (existingIt != m_cachedGlyphs.end() && existingIt->second.generation == refreshedGen)
                    {
                        continue;
                    }
                    uint32_t pageId = 0;
                    Domain::Renderer::AtlasAllocation alloc{};
                    CHECK_STATUS(m_atlasManager.allocateGlyph(cp, 0u, 0u, pageId, alloc));
                    CachedUiGlyph fallbackGlyph{};
                    fallbackGlyph.pageId = pageId;
                    fallbackGlyph.format = GlyphAtlasManager::getGlyphFormat(cp);
                    fallbackGlyph.generation = m_atlasManager.getAtlasGeneration();
                    fallbackGlyph.metric.bearingY = static_cast<float>(fontSizePx) * 0.8f;
                    fallbackGlyph.metric.advanceX = (cp < 0x80u)
                        ? std::max(1.0f, static_cast<float>(fontSizePx) * 0.5f)
                        : static_cast<float>(fontSizePx);
                    m_cachedGlyphs[key] = fallbackGlyph;
                }
                return OkStatus();
            }
        }

        // 无头环境且 SystemFontProvider 未初始化时，合成空白占位度量保证 UI 排版链路无阻塞
        for (const auto& req : missingRequests)
        {
            const char32_t cp = static_cast<char32_t>(req.codepoint);
            const uint32_t gw = (cp == U' ') ? 0u : static_cast<uint32_t>(fontSizePx);
            const uint32_t gh = (cp == U' ') ? 0u : static_cast<uint32_t>(fontSizePx);
            uint32_t pageId = 0;
            Domain::Renderer::AtlasAllocation alloc{};
            CHECK_STATUS(m_atlasManager.allocateGlyph(cp, gw, gh, pageId, alloc));

            CachedUiGlyph cached{};
            cached.pageId = pageId;
            cached.format = GlyphAtlasManager::getGlyphFormat(cp);
            cached.generation = m_atlasManager.getAtlasGeneration();
            cached.metric.width = static_cast<float>(gw);
            cached.metric.height = static_cast<float>(gh);
            cached.metric.bearingX = 0.0f;
            cached.metric.bearingY = static_cast<float>(fontSizePx) * 0.8f;
            cached.metric.advanceX = (cp < 0x80u) ? (static_cast<float>(fontSizePx) * 0.6f) : static_cast<float>(fontSizePx);
            cached.metric.advanceY = 0.0f;
            cached.metric.uvMinMax[0] = static_cast<uint16_t>((alloc.x * 65535u) / GlyphAtlasManager::AtlasPageWidth);
            cached.metric.uvMinMax[1] = static_cast<uint16_t>((alloc.y * 65535u) / GlyphAtlasManager::AtlasPageHeight);
            cached.metric.uvMinMax[2] = static_cast<uint16_t>(((alloc.x + alloc.width) * 65535u) / GlyphAtlasManager::AtlasPageWidth);
            cached.metric.uvMinMax[3] = static_cast<uint16_t>(((alloc.y + alloc.height) * 65535u) / GlyphAtlasManager::AtlasPageHeight);

            GlyphCacheKey key{resolvedFamily, cp, fontSizePx, weight, italic};
            m_cachedGlyphs[key] = cached;
        }
        return OkStatus();
    }

    bool FontSystem::tryGetCachedGlyph(
        char32_t codepoint,
        uint16_t fontSizePx,
        CachedUiGlyph& outGlyph) const noexcept
    {
        return tryGetCachedGlyph("", codepoint, fontSizePx, 4, false, outGlyph);
    }

    bool FontSystem::tryGetCachedGlyph(
        std::string_view family,
        char32_t codepoint,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic,
        CachedUiGlyph& outGlyph) const noexcept
    {
        const std::string resolvedFamily = family.empty() ? m_fallbackFamily : std::string(family);
        GlyphCacheKey key{resolvedFamily, codepoint, fontSizePx, weight, italic};
        auto it = m_cachedGlyphs.find(key);
        if (it == m_cachedGlyphs.end())
        {
            return false;
        }
        if (it->second.generation != m_atlasManager.getAtlasGeneration())
        {
            return false;
        }
        outGlyph = it->second;
        return true;
    }

    void FontSystem::onMemoryWarning()
    {
        ZoneScoped();
        m_atlasManager.onMemoryWarning();
        m_cachedGlyphs.clear();
    }
}
