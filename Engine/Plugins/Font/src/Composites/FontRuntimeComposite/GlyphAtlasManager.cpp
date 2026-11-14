/**
 * @file GlyphAtlasManager.cpp
 * @brief 动态双格式字形图集管理器实现
 */
#include "GlyphAtlasManager.h"

namespace MQEngine::Resource::Font
{
    GlyphAtlasManager::GlyphAtlasManager()
    {
        // 启动仅预创建第 0 页单通道 R8 图集，RGBA8 Emoji 图集保持惰性按需分配
        createPage(TextureFormat::R8_UNORM);
    }

    Status GlyphAtlasManager::initGpuResources(FCT::Context* ctx)
    {
        ZoneScoped();
        m_ctx = ctx;
        if (!m_ctx || m_deferGpuUpload)
        {
            return OkStatus();
        }
        for (auto& page : m_pages)
        {
            if (!page.texture)
            {
                createGpuTextureForPage(page);
            }
            if (page.texture && page.gpuDirty && !page.cpuPixels.empty())
            {
                page.texture->updateData(page.cpuPixels.data(), page.cpuPixels.size());
                page.gpuDirty = false;
            }
        }
        return OkStatus();
    }

    Status GlyphAtlasManager::flushDirtyPagesToGpu(FCT::Context* ctx)
    {
        ZoneScoped();
        if (ctx)
        {
            m_ctx = ctx;
        }
        if (!m_ctx)
        {
            return OkStatus();
        }
        for (auto& page : m_pages)
        {
            if (!page.texture)
            {
                createGpuTextureForPage(page);
                page.gpuDirty = true;
            }
            if (page.texture && page.gpuDirty && !page.cpuPixels.empty())
            {
                page.texture->updateData(page.cpuPixels.data(), page.cpuPixels.size());
                page.gpuDirty = false;
            }
        }
        return OkStatus();
    }

    void GlyphAtlasManager::releaseGpuResources() noexcept
    {
        for (auto& page : m_pages)
        {
            page.texture = nullptr;
        }
        m_ctx = nullptr;
    }

    void GlyphAtlasManager::createGpuTextureForPage(GlyphAtlasPage& page)
    {
        if (!m_ctx)
        {
            return;
        }
        auto* img = FCT_NEW(FCT::SingleBufferImage, m_ctx);
        if (!img)
        {
            return;
        }
        img->width(static_cast<int>(AtlasPageWidth));
        img->height(static_cast<int>(AtlasPageHeight));
        img->arrayLayers(1);
        img->format(page.format == TextureFormat::R8G8B8A8_UNORM
                        ? FCT::Format::R8G8B8A8_UNORM
                        : FCT::Format::R8_UNORM);
        img->as(FCT::ImageUsage::Texture);
        img->create();
        page.texture = Ptr<FCT::Image>(img);
        img->release();
    }

    uint32_t GlyphAtlasManager::createPage(TextureFormat format)
    {
        GlyphAtlasPage page;
        page.pageId = m_nextPageId++;
        page.format = format;
        if (!page.packer)
        {
            page.packer = makeUnique<Domain::Renderer::AtlasShelfPacker>(AtlasPageWidth, AtlasPageHeight, 1);
        }
        const size_t bytesPerPixel = (format == TextureFormat::R8G8B8A8_UNORM) ? 4u : 1u;
        page.cpuPixels.assign(static_cast<size_t>(AtlasPageWidth) * AtlasPageHeight * bytesPerPixel, 0u);
        page.gpuDirty = true;
        page.lastAccessFrame = m_currentFrame;
        page.glyphCount = 0;
        if (!m_deferGpuUpload)
        {
            createGpuTextureForPage(page);
        }
        m_pages.push_back(std::move(page));
        return m_pages.back().pageId;
    }

    Status GlyphAtlasManager::allocateGlyph(
        char32_t codepoint,
        uint32_t width,
        uint32_t height,
        uint32_t& outPageId,
        Domain::Renderer::AtlasAllocation& outAlloc)
    {
        ZoneScoped();
        const TextureFormat targetFmt = getGlyphFormat(codepoint);

        // 空白字符 (width==0 或 height==0) 不占用像素块，直接绑定同格式首页返回成功
        if (width == 0 || height == 0)
        {
            for (auto& page : m_pages)
            {
                if (page.format == targetFmt)
                {
                    outPageId = page.pageId;
                    outAlloc = Domain::Renderer::AtlasAllocation{0, 0, 0, 0, 0, true};
                    page.lastAccessFrame = m_currentFrame;
                    return OkStatus();
                }
            }
            outPageId = createPage(targetFmt);
            outAlloc = Domain::Renderer::AtlasAllocation{0, 0, 0, 0, 0, true};
            return OkStatus();
        }

        if (width > AtlasPageWidth || height > AtlasPageHeight)
        {
            return ResourceExhaustedError("字形尺寸超出单图集页面 1024x1024 上限");
        }

        // 预留 1px 透明隔离边，防止双线性采样时相邻字形边缘渗色
        const uint32_t paddedW = (width < AtlasPageWidth) ? (width + 1u) : width;
        const uint32_t paddedH = (height < AtlasPageHeight) ? (height + 1u) : height;

        for (auto& page : m_pages)
        {
            if (page.format == targetFmt && page.packer)
            {
                outAlloc = page.packer->allocate(paddedW, paddedH);
                if (outAlloc.success)
                {
                    outAlloc.width = width;
                    outAlloc.height = height;
                    outPageId = page.pageId;
                    page.lastAccessFrame = m_currentFrame;
                    page.glyphCount++;
                    return OkStatus();
                }
            }
        }

        const uint32_t newPageId = createPage(targetFmt);
        auto& newPage = m_pages.back();
        outAlloc = newPage.packer->allocate(paddedW, paddedH);
        if (!outAlloc.success)
        {
            return ResourceExhaustedError("字形尺寸超出单图集页面 1024x1024 上限");
        }

        outAlloc.width = width;
        outAlloc.height = height;
        outPageId = newPageId;
        newPage.lastAccessFrame = m_currentFrame;
        newPage.glyphCount++;
        return OkStatus();
    }

    Status GlyphAtlasManager::uploadGlyphBitmap(
        uint32_t pageId,
        const Domain::Renderer::AtlasAllocation& alloc,
        const uint8_t* data,
        size_t dataSize,
        size_t rowPitch)
    {
        ZoneScoped();
        for (auto& page : m_pages)
        {
            if (page.pageId != pageId)
            {
                continue;
            }

            page.lastAccessFrame = m_currentFrame;
            if (alloc.width == 0 || alloc.height == 0)
            {
                return OkStatus();
            }

            const size_t bytesPerPixel = (page.format == TextureFormat::R8G8B8A8_UNORM) ? 4u : 1u;
            const size_t effectivePitch = (rowPitch > 0) ? rowPitch : (static_cast<size_t>(alloc.width) * bytesPerPixel);
            const size_t minRequiredBytes = static_cast<size_t>(alloc.height - 1u) * effectivePitch +
                                            static_cast<size_t>(alloc.width) * bytesPerPixel;
            if (!data || dataSize < minRequiredBytes)
            {
                return InvalidArgumentError("字形位图缓冲区尺寸不足");
            }

            const size_t pageRowBytes = static_cast<size_t>(AtlasPageWidth) * bytesPerPixel;
            const size_t expectedPageBytes = pageRowBytes * AtlasPageHeight;
            if (page.cpuPixels.size() != expectedPageBytes)
            {
                page.cpuPixels.assign(expectedPageBytes, 0u);
            }

            if (alloc.x < AtlasPageWidth && alloc.y < AtlasPageHeight)
            {
                const uint32_t copyW = std::min(alloc.width, AtlasPageWidth - alloc.x);
                const uint32_t copyH = std::min(alloc.height, AtlasPageHeight - alloc.y);
                const size_t copyRowBytes = static_cast<size_t>(copyW) * bytesPerPixel;
                for (uint32_t r = 0; r < copyH; ++r)
                {
                    const size_t dstOffset = static_cast<size_t>(alloc.y + r) * pageRowBytes +
                                             static_cast<size_t>(alloc.x) * bytesPerPixel;
                    const size_t srcOffset = static_cast<size_t>(r) * effectivePitch;
                    std::memcpy(page.cpuPixels.data() + dstOffset, data + srcOffset, copyRowBytes);
                }
                page.gpuDirty = true;
            }

            if (!m_deferGpuUpload && page.texture)
            {
                page.texture->updateSubRegion(
                    alloc.x,
                    alloc.y,
                    alloc.width,
                    alloc.height,
                    0u,
                    0u,
                    data,
                    effectivePitch);
                page.gpuDirty = false;
            }
            return OkStatus();
        }
        return NotFoundError(StrCat("未找到图集页: ", pageId));
    }

    void GlyphAtlasManager::onMemoryWarning()
    {
        (void)evictLruPage();
    }

    bool GlyphAtlasManager::evictLruPage()
    {
        ZoneScoped();
        if (m_pages.empty())
        {
            createPage(TextureFormat::R8_UNORM);
            m_atlasGeneration++;
            return true;
        }

        if (m_pages.size() == 1)
        {
            // 仅剩单页时执行原位重置装箱器并递增世代号，强制客户端释放旧 UV 缓存
            if (m_pages[0].packer)
            {
                m_pages[0].packer->reset();
            }
            std::fill(m_pages[0].cpuPixels.begin(), m_pages[0].cpuPixels.end(), 0u);
            m_pages[0].gpuDirty = true;
            m_pages[0].glyphCount = 0;
            m_pages[0].lastAccessFrame = m_currentFrame;
            m_atlasGeneration++;
            return true;
        }

        size_t lruIndex = 0;
        uint64_t minFrame = m_pages[0].lastAccessFrame;
        for (size_t i = 1; i < m_pages.size(); ++i)
        {
            if (m_pages[i].lastAccessFrame < minFrame)
            {
                minFrame = m_pages[i].lastAccessFrame;
                lruIndex = i;
            }
        }

        m_pages.erase(m_pages.begin() + static_cast<std::ptrdiff_t>(lruIndex));
        m_atlasGeneration++;
        return true;
    }

    FCT::Image* GlyphAtlasManager::getAtlasPageTexture(uint32_t pageId) const noexcept
    {
        for (const auto& page : m_pages)
        {
            if (page.pageId == pageId)
            {
                return page.texture.get();
            }
        }
        return nullptr;
    }

    TextureFormat GlyphAtlasManager::getAtlasPageFormat(uint32_t pageId) const noexcept
    {
        for (const auto& page : m_pages)
        {
            if (page.pageId == pageId)
            {
                return page.format;
            }
        }
        return TextureFormat::R8_UNORM;
    }
}
