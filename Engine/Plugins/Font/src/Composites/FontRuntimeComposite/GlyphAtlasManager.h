/**
 * @file GlyphAtlasManager.h
 * @brief 动态双格式字形图集管理器 (R8_UNORM / RGBA8_UNORM) 与内存告警 LRU 淘汰
 */
#ifndef MQENGINE_FONT_GLYPHATLASMANAGER_H
#define MQENGINE_FONT_GLYPHATLASMANAGER_H

#include "../../FontApi.h"
#include "FontTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine::Resource::Font
{
    struct GlyphAtlasPage
    {
        uint32_t pageId{0};
        TextureFormat format{TextureFormat::R8_UNORM};
        UniquePtr<Domain::Renderer::AtlasShelfPacker> packer;
        Ptr<FCT::Image> texture;
        std::vector<uint8_t> cpuPixels;
        bool gpuDirty{false};
        uint64_t lastAccessFrame{0};
        uint32_t glyphCount{0};

        GlyphAtlasPage()
            : packer(makeUnique<Domain::Renderer::AtlasShelfPacker>(1024, 1024, 1))
        {
        }
        ~GlyphAtlasPage() = default;
        GlyphAtlasPage(const GlyphAtlasPage&) = delete;
        GlyphAtlasPage& operator=(const GlyphAtlasPage&) = delete;
        GlyphAtlasPage(GlyphAtlasPage&&) noexcept = default;
        GlyphAtlasPage& operator=(GlyphAtlasPage&&) noexcept = default;
    };

    /**
     * @brief 动态字形图集管理器 (FEAT-59)
     */
    class FONT_RUNTIME_COMPOSITE_API GlyphAtlasManager
    {
    public:
        static constexpr uint32_t AtlasPageWidth = 1024;
        static constexpr uint32_t AtlasPageHeight = 1024;

        GlyphAtlasManager();
        ~GlyphAtlasManager() = default;

        GlyphAtlasManager(const GlyphAtlasManager&) = delete;
        GlyphAtlasManager& operator=(const GlyphAtlasManager&) = delete;
        GlyphAtlasManager(GlyphAtlasManager&&) noexcept = default;
        GlyphAtlasManager& operator=(GlyphAtlasManager&&) noexcept = default;

        Status initGpuResources(FCT::Context* ctx);
        void releaseGpuResources() noexcept;

        void setDeferGpuUpload(bool defer) noexcept { m_deferGpuUpload = defer; }
        [[nodiscard]] bool isDeferGpuUpload() const noexcept { return m_deferGpuUpload; }
        Status flushDirtyPagesToGpu(FCT::Context* ctx);

        static TextureFormat getGlyphFormat(char32_t codepoint) noexcept
        {
            if (codepoint >= 0x10000u || (codepoint >= 0x2600u && codepoint <= 0x27BFu))
            {
                return TextureFormat::R8G8B8A8_UNORM;
            }
            return TextureFormat::R8_UNORM;
        }

        Status allocateGlyph(
            char32_t codepoint,
            uint32_t width,
            uint32_t height,
            uint32_t& outPageId,
            Domain::Renderer::AtlasAllocation& outAlloc);

        Status uploadGlyphBitmap(
            uint32_t pageId,
            const Domain::Renderer::AtlasAllocation& alloc,
            const uint8_t* data,
            size_t dataSize,
            size_t rowPitch = 0);

        void onMemoryWarning();
        bool evictLruPage();

        [[nodiscard]] uint32_t getAtlasGeneration() const noexcept { return m_atlasGeneration; }
        [[nodiscard]] FCT::Image* getAtlasPageTexture(uint32_t pageId) const noexcept;
        [[nodiscard]] TextureFormat getAtlasPageFormat(uint32_t pageId) const noexcept;
        [[nodiscard]] size_t getPageCount() const noexcept { return m_pages.size(); }
        [[nodiscard]] const std::vector<GlyphAtlasPage>& getPages() const noexcept { return m_pages; }

        void setCurrentFrame(uint64_t frame) noexcept { m_currentFrame = frame; }
        [[nodiscard]] uint64_t getCurrentFrame() const noexcept { return m_currentFrame; }

    private:
        uint32_t createPage(TextureFormat format);
        void createGpuTextureForPage(GlyphAtlasPage& page);

    private:
        FCT::Context* m_ctx{nullptr};
        std::vector<GlyphAtlasPage> m_pages;
        uint32_t m_atlasGeneration{1};
        uint64_t m_currentFrame{0};
        uint32_t m_nextPageId{0};
        bool m_deferGpuUpload{false};
    };
}

#endif // MQENGINE_FONT_GLYPHATLASMANAGER_H
