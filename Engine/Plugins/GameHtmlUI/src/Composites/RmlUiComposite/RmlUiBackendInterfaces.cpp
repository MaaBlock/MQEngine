/**
 * @file RmlUiBackendInterfaces.cpp
 * @brief RmlUi 6.0 引擎后端接口封装实现（直绘 Backbuffer 几何缓冲、Scissor 裁剪与零拷贝虚拟纹理路由）
 */
#include "RmlUiBackendInterfaces.h"

#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include <Font/Composites/FontRuntimeComposite/FontRuntimeComposite.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>

namespace MQEngine::GameHtmlUI
{
    namespace
    {
        constexpr std::string_view kGlyphAtlasPrefix = "@mq_glyph_atlas/";
        constexpr std::string_view kEmojiSubPrefix = "emoji_";
        constexpr std::string_view kSharedCanvasPrefix = "@mq_shared_canvas/";
        constexpr int kDefaultAtlasDimension = 1024;

        inline constexpr FCT::VertexLayout kRml2dVertexLayout = {
            FCT::VertexElement{FCT::VtxType::Position2f, "position"},
            FCT::VertexElement{FCT::VtxType::Custom, "color", FCT::Format::R8G8B8A8_UNORM},
            FCT::VertexElement{FCT::VtxType::TexCoord2f, "texCoord"}
        };
        static_assert(sizeof(Rml::Vertex) == 20, "Rml::Vertex 字节跨度需与 kRml2dVertexLayout 严格一致");

        class DeferredRmlGpuBufferHolder : public FCT::RefCount
        {
        public:
            DeferredRmlGpuBufferHolder(
                UniquePtr<FCT::VertexBuffer> cpuVb,
                UniquePtr<FCT::RHI::VertexBuffer> vb,
                UniquePtr<FCT::RHI::IndexBuffer> ib)
                : m_cpuVb(std::move(cpuVb)), m_vb(std::move(vb)), m_ib(std::move(ib))
            {
            }

            ~DeferredRmlGpuBufferHolder() override = default;

        private:
            UniquePtr<FCT::VertexBuffer> m_cpuVb;
            UniquePtr<FCT::RHI::VertexBuffer> m_vb;
            UniquePtr<FCT::RHI::IndexBuffer> m_ib;
        };

        bool parseUnsignedUint32(std::string_view text, uint32_t& outValue) noexcept
        {
            if (text.empty())
            {
                return false;
            }
            uint32_t parsed = 0;
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (ec != std::errc{} || ptr != text.data() + text.size())
            {
                return false;
            }
            outValue = parsed;
            return true;
        }

        std::string resolveUiAssetDiskPath(const std::string& normPath)
        {
            if (normPath.empty())
            {
                return normPath;
            }
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (resMod && loader && !resMod->getProjectDir().empty())
            {
                const std::string projCandidate = FCT::SAI::Path::join(resMod->getProjectDir(), normPath);
                if (loader->exists(projCandidate))
                {
                    return projCandidate;
                }
            }
            return normPath;
        }

        bool tryReadTextWithSaiFallback(const std::string& normPath, std::string& outContent)
        {
            const std::string resolvedPath = resolveUiAssetDiskPath(normPath);
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (loader && loader->exists(resolvedPath))
            {
                auto res = loader->loadFile(resolvedPath);
                if (res.ok())
                {
                    outContent = std::move(*res);
                    return true;
                }
            }

            if (FCT::SAI::FileSystem::exists(resolvedPath))
            {
                auto saiRes = FCT::SAI::FileSystem::readTextFile(resolvedPath);
                if (saiRes.ok())
                {
                    outContent = std::move(*saiRes);
                    return true;
                }
            }
            return false;
        }
    } // namespace

    // ==============================================================================
    // VirtualTextureRouter 实现 (FEAT-50)
    // ==============================================================================

    VirtualTextureRouter::VirtualTextureRouter() = default;

    void VirtualTextureRouter::setPlaceholderTextureHandle(Rml::TextureHandle handle) noexcept
    {
        if (handle != 0)
        {
            m_placeholderHandle = handle;
        }
    }

    void VirtualTextureRouter::registerFontAtlasPage(
        uint32_t pageId,
        Rml::TextureHandle handle,
        Resource::Font::TextureFormat format,
        Rml::Vector2i dimensions)
    {
        RegisteredTextureEntry entry;
        entry.handle = handle;
        entry.dimensions = dimensions;
        entry.format = format;
        m_atlasPages[pageId] = entry;
        if (handle != 0)
        {
            m_handleToAtlasPageId[handle] = pageId;
        }
    }

    void VirtualTextureRouter::registerSharedCanvas(
        std::string_view canvasName,
        Rml::TextureHandle handle,
        Rml::Vector2i dimensions,
        FCT::Image* gpuImage)
    {
        if (canvasName.empty() || handle == 0)
        {
            return;
        }
        RegisteredTextureEntry entry;
        entry.handle = handle;
        entry.dimensions = dimensions;
        entry.gpuImage = gpuImage;
        m_sharedCanvases[std::string(canvasName)] = entry;
        if (gpuImage)
        {
            m_handleToImage[handle] = gpuImage;
        }
    }

    void VirtualTextureRouter::unregisterSharedCanvas(std::string_view canvasName)
    {
        auto it = m_sharedCanvases.find(std::string(canvasName));
        if (it != m_sharedCanvases.end())
        {
            m_handleToImage.erase(it->second.handle);
            m_sharedCanvases.erase(it);
        }
    }

    void VirtualTextureRouter::notifyAssetLoaded(
        std::string_view uri,
        Rml::TextureHandle handle,
        Rml::Vector2i dimensions)
    {
        if (uri.empty() || handle == 0)
        {
            return;
        }
        RegisteredTextureEntry entry;
        entry.handle = handle;
        entry.dimensions = dimensions;
        m_loadedAssets[std::string(uri)] = entry;
    }

    Rml::TextureHandle VirtualTextureRouter::loadTexture(
        Rml::Vector2i& outDimensions,
        std::string_view sourceUri)
    {
        if (sourceUri.empty())
        {
            outDimensions = Rml::Vector2i(1, 1);
            return m_placeholderHandle;
        }

        if (sourceUri.starts_with(kGlyphAtlasPrefix))
        {
            return resolveGlyphAtlasUri(sourceUri.substr(kGlyphAtlasPrefix.size()), outDimensions);
        }

        if (sourceUri.starts_with(kSharedCanvasPrefix))
        {
            return resolveSharedCanvasUri(sourceUri.substr(kSharedCanvasPrefix.size()), outDimensions);
        }

        if (sourceUri.starts_with('@'))
        {
            outDimensions = Rml::Vector2i(1, 1);
            return m_placeholderHandle;
        }

        return resolveStandardAssetUri(sourceUri, outDimensions);
    }

    Rml::TextureHandle VirtualTextureRouter::loadTexture(std::string_view sourceUri)
    {
        Rml::Vector2i ignoredDims(0, 0);
        return loadTexture(ignoredDims, sourceUri);
    }

    FCT::Image* VirtualTextureRouter::resolveBoundGpuImage(Rml::TextureHandle handle) const noexcept
    {
        auto it = m_handleToImage.find(handle);
        return (it != m_handleToImage.end()) ? it->second : nullptr;
    }

    bool VirtualTextureRouter::tryResolveAtlasPageId(Rml::TextureHandle handle, uint32_t& outPageId) const noexcept
    {
        auto it = m_handleToAtlasPageId.find(handle);
        if (it == m_handleToAtlasPageId.end())
        {
            return false;
        }
        outPageId = it->second;
        return true;
    }

    void VirtualTextureRouter::flushDirtyAssetTexturesToGpu(FCT::Context* ctx)
    {
        if (!ctx)
        {
            return;
        }
        for (auto& [uri, entry] : m_loadedAssets)
        {
            (void)uri;
            if (!entry.gpuDirty || entry.cpuRgbaPixels.empty() || entry.dimensions.x <= 0 || entry.dimensions.y <= 0)
            {
                continue;
            }
            if (!entry.ownedGpuImage)
            {
                auto* img = FCT_NEW(FCT::SingleBufferImage, ctx);
                if (!img)
                {
                    continue;
                }
                img->width(entry.dimensions.x);
                img->height(entry.dimensions.y);
                img->arrayLayers(1);
                img->format(FCT::Format::R8G8B8A8_UNORM);
                img->as(FCT::ImageUsage::Texture);
                img->create();
                entry.ownedGpuImage = Ptr<FCT::Image>(img);
                img->release();
            }
            if (entry.ownedGpuImage)
            {
                entry.ownedGpuImage->updateData(entry.cpuRgbaPixels.data(), entry.cpuRgbaPixels.size());
                entry.gpuImage = entry.ownedGpuImage.get();
                m_handleToImage[entry.handle] = entry.gpuImage;
                entry.gpuDirty = false;
            }
        }
    }

    void VirtualTextureRouter::releaseOwnedGpuImages() noexcept
    {
        for (auto& [uri, entry] : m_loadedAssets)
        {
            (void)uri;
            if (entry.ownedGpuImage)
            {
                m_handleToImage.erase(entry.handle);
                entry.gpuImage = nullptr;
                entry.ownedGpuImage = nullptr;
                if (!entry.cpuRgbaPixels.empty())
                {
                    entry.gpuDirty = true;
                }
            }
        }
    }

    Resource::Font::FontSystem* VirtualTextureRouter::activeFontSystem() const noexcept
    {
        if (m_fontSystem)
        {
            return m_fontSystem;
        }
        return Font::FontRuntimeComposite::getActiveFontSystem();
    }

    Rml::TextureHandle VirtualTextureRouter::resolveGlyphAtlasUri(
        std::string_view suffix,
        Rml::Vector2i& outDimensions)
    {
        std::string_view idToken = suffix;
        if (idToken.starts_with(kEmojiSubPrefix))
        {
            idToken.remove_prefix(kEmojiSubPrefix.size());
        }
        else if (idToken.ends_with("/emoji"))
        {
            idToken.remove_suffix(6u);
        }

        uint32_t pageId = 0;
        if (!parseUnsignedUint32(idToken, pageId))
        {
            outDimensions = Rml::Vector2i(1, 1);
            return m_placeholderHandle;
        }

        auto explicitIt = m_atlasPages.find(pageId);
        if (explicitIt != m_atlasPages.end() && explicitIt->second.handle != 0)
        {
            outDimensions = explicitIt->second.dimensions;
            m_handleToAtlasPageId[explicitIt->second.handle] = pageId;
            return explicitIt->second.handle;
        }

        if (auto* fontSys = activeFontSystem())
        {
            if (FCT::Image* pageImage = fontSys->getAtlasPageTexture(pageId))
            {
                const int w = pageImage->width() > 0 ? pageImage->width() : kDefaultAtlasDimension;
                const int h = pageImage->height() > 0 ? pageImage->height() : kDefaultAtlasDimension;
                outDimensions = Rml::Vector2i(w, h);
                const auto gpuHandle = reinterpret_cast<Rml::TextureHandle>(pageImage);
                m_handleToImage[gpuHandle] = pageImage;
                m_handleToAtlasPageId[gpuHandle] = pageId;
                return gpuHandle;
            }

            if (pageId < fontSys->getAtlasManager().getPageCount())
            {
                outDimensions = Rml::Vector2i(kDefaultAtlasDimension, kDefaultAtlasDimension);
                const auto synHandle = static_cast<Rml::TextureHandle>(100u + pageId + 1u);
                m_handleToAtlasPageId[synHandle] = pageId;
                return synHandle;
            }
        }

        outDimensions = Rml::Vector2i(1, 1);
        return m_placeholderHandle;
    }

    Rml::TextureHandle VirtualTextureRouter::resolveSharedCanvasUri(
        std::string_view suffix,
        Rml::Vector2i& outDimensions)
    {
        if (suffix.empty())
        {
            outDimensions = Rml::Vector2i(1, 1);
            return m_placeholderHandle;
        }

        auto it = m_sharedCanvases.find(std::string(suffix));
        if (it != m_sharedCanvases.end() && it->second.handle != 0)
        {
            outDimensions = it->second.dimensions;
            return it->second.handle;
        }

        outDimensions = Rml::Vector2i(1, 1);
        return m_placeholderHandle;
    }

    Rml::TextureHandle VirtualTextureRouter::resolveStandardAssetUri(
        std::string_view uri,
        Rml::Vector2i& outDimensions)
    {
        std::string normPath(uri);
        std::replace(normPath.begin(), normPath.end(), '\\', '/');

        auto loadedIt = m_loadedAssets.find(normPath);
        if (loadedIt != m_loadedAssets.end() && loadedIt->second.handle != 0)
        {
            outDimensions = loadedIt->second.dimensions;
            return loadedIt->second.handle;
        }

        // 远端流式 URL 尚未就绪时立即返回 1x1 占位句柄，等待 notifyAssetLoaded 热替换
        if (normPath.starts_with("http://") || normPath.starts_with("https://"))
        {
            outDimensions = Rml::Vector2i(1, 1);
            return m_placeholderHandle;
        }

        const std::string resolvedPath = resolveUiAssetDiskPath(normPath);
        auto* resMod = Resource::Resource::InstancePtr();
        auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
        if (loader && loader->exists(resolvedPath))
        {
            RegisteredTextureEntry entry;
            entry.handle = m_nextGeneratedHandle++;
            entry.dimensions = Rml::Vector2i(128, 128);
            auto fileRes = loader->loadFile(resolvedPath);
            if (fileRes.ok() && !fileRes->empty() && resMod)
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(fileRes->data());
                auto decoded = resMod->decodeTextureFromMemory(bytes, fileRes->size(), normPath);
                if (decoded.ok() && decoded->width > 0 && decoded->height > 0)
                {
                    entry.dimensions = Rml::Vector2i(static_cast<int>(decoded->width), static_cast<int>(decoded->height));
                    const size_t pixelCount = static_cast<size_t>(decoded->width) * static_cast<size_t>(decoded->height);
                    if (!decoded->pixels.empty())
                    {
                        if (decoded->channels == 4 && decoded->pixels.size() >= pixelCount * 4u)
                        {
                            entry.cpuRgbaPixels.assign(decoded->pixels.begin(), decoded->pixels.begin() + pixelCount * 4u);
                            entry.gpuDirty = true;
                        }
                        else if (decoded->channels == 3 && decoded->pixels.size() >= pixelCount * 3u)
                        {
                            entry.cpuRgbaPixels.resize(pixelCount * 4u);
                            for (size_t i = 0; i < pixelCount; ++i)
                            {
                                entry.cpuRgbaPixels[i * 4u + 0u] = decoded->pixels[i * 3u + 0u];
                                entry.cpuRgbaPixels[i * 4u + 1u] = decoded->pixels[i * 3u + 1u];
                                entry.cpuRgbaPixels[i * 4u + 2u] = decoded->pixels[i * 3u + 2u];
                                entry.cpuRgbaPixels[i * 4u + 3u] = 255u;
                            }
                            entry.gpuDirty = true;
                        }
                        else if (decoded->channels == 1 && decoded->pixels.size() >= pixelCount)
                        {
                            entry.cpuRgbaPixels.resize(pixelCount * 4u);
                            for (size_t i = 0; i < pixelCount; ++i)
                            {
                                const uint8_t v = decoded->pixels[i];
                                entry.cpuRgbaPixels[i * 4u + 0u] = v;
                                entry.cpuRgbaPixels[i * 4u + 1u] = v;
                                entry.cpuRgbaPixels[i * 4u + 2u] = v;
                                entry.cpuRgbaPixels[i * 4u + 3u] = 255u;
                            }
                            entry.gpuDirty = true;
                        }
                    }
                }
            }
            outDimensions = entry.dimensions;
            m_loadedAssets[normPath] = entry;
            return entry.handle;
        }

        outDimensions = Rml::Vector2i(1, 1);
        return m_placeholderHandle;
    }

    // ==============================================================================
    // MQRmlSystemInterface 实现
    // ==============================================================================

    double MQRmlSystemInterface::GetElapsedTime()
    {
        static const auto s_startTime = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - s_startTime).count();
    }

    bool MQRmlSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
    {
        switch (type)
        {
        case Rml::Log::LT_ERROR:
            spdlog::error("[RmlUi Error] {}", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            spdlog::warn("[RmlUi Warning] {}", message.c_str());
            break;
        case Rml::Log::LT_INFO:
        default:
            spdlog::info("[RmlUi] {}", message.c_str());
            break;
        }
        return true;
    }

    void MQRmlSystemInterface::SetClipboardText(const Rml::String& text)
    {
        m_clipboard = text;
    }

    void MQRmlSystemInterface::GetClipboardText(Rml::String& text)
    {
        text = m_clipboard;
    }

    // ==============================================================================
    // MQRmlFileInterface 实现
    // ==============================================================================

    void MQRmlFileInterface::recordPathAccess(std::string_view path)
    {
        m_queriedPaths.emplace_back(path);
    }

    bool MQRmlFileInterface::hasQueriedPath(std::string_view substring) const noexcept
    {
        if (substring.empty())
        {
            return false;
        }
        for (const auto& recorded : m_queriedPaths)
        {
            if (recorded.find(substring) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    Rml::FileHandle MQRmlFileInterface::Open(const Rml::String& path)
    {
        std::string normPath(path.c_str(), path.size());
        std::replace(normPath.begin(), normPath.end(), '\\', '/');
        recordPathAccess(normPath);

        std::string fileBytes;
        if (!tryReadTextWithSaiFallback(normPath, fileBytes))
        {
            return 0;
        }

        auto* desc = FCT_NEW(OpenedRmlFile);
        desc->data = std::move(fileBytes);
        desc->offset = 0;
        return reinterpret_cast<Rml::FileHandle>(desc);
    }

    void MQRmlFileInterface::Close(Rml::FileHandle file)
    {
        if (file)
        {
            auto* desc = reinterpret_cast<OpenedRmlFile*>(file);
            FCT_DELETE(desc);
        }
    }

    size_t MQRmlFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
    {
        if (!file || !buffer || size == 0)
        {
            return 0;
        }

        auto* desc = reinterpret_cast<OpenedRmlFile*>(file);
        if (desc->offset >= desc->data.size())
        {
            return 0;
        }

        const size_t available = desc->data.size() - desc->offset;
        const size_t toRead = std::min(size, available);
        std::memcpy(buffer, desc->data.data() + desc->offset, toRead);
        desc->offset += toRead;
        return toRead;
    }

    bool MQRmlFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
    {
        if (!file)
        {
            return false;
        }

        auto* desc = reinterpret_cast<OpenedRmlFile*>(file);
        long newOffset = 0;
        switch (origin)
        {
        case SEEK_SET:
            newOffset = offset;
            break;
        case SEEK_CUR:
            newOffset = static_cast<long>(desc->offset) + offset;
            break;
        case SEEK_END:
            newOffset = static_cast<long>(desc->data.size()) + offset;
            break;
        default:
            return false;
        }

        if (newOffset < 0 || newOffset > static_cast<long>(desc->data.size()))
        {
            return false;
        }

        desc->offset = static_cast<size_t>(newOffset);
        return true;
    }

    size_t MQRmlFileInterface::Tell(Rml::FileHandle file)
    {
        if (!file)
        {
            return 0;
        }
        return reinterpret_cast<OpenedRmlFile*>(file)->offset;
    }

    size_t MQRmlFileInterface::Length(Rml::FileHandle file)
    {
        if (!file)
        {
            return 0;
        }
        return reinterpret_cast<OpenedRmlFile*>(file)->data.size();
    }

    bool MQRmlFileInterface::LoadFile(const Rml::String& path, Rml::String& out_data)
    {
        std::string normPath(path.c_str(), path.size());
        std::replace(normPath.begin(), normPath.end(), '\\', '/');
        recordPathAccess(normPath);

        std::string fileBytes;
        if (!tryReadTextWithSaiFallback(normPath, fileBytes))
        {
            return false;
        }

        out_data = Rml::String(fileBytes.data(), fileBytes.size());
        return true;
    }

    // ==============================================================================
    // MQRmlRenderInterface 实现 (FEAT-49)
    // ==============================================================================

    MQRmlRenderInterface::MQRmlRenderInterface() = default;

    MQRmlRenderInterface::~MQRmlRenderInterface()
    {
        while (!m_compiledGeometries.empty())
        {
            ReleaseGeometry(m_compiledGeometries.begin()->first);
        }
        releaseAllGpuTextures();
    }

    Rml::CompiledGeometryHandle MQRmlRenderInterface::CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices)
    {
        if (vertices.empty() || indices.empty())
        {
            return 0;
        }

        auto geom = makeUnique<MQRmlCompiledGeometry>();
        geom->vertices.assign(vertices.begin(), vertices.end());
        geom->indices.assign(indices.begin(), indices.end());

        geom->gpuIndices.reserve(indices.size());
        for (int idx : indices)
        {
            geom->gpuIndices.push_back(static_cast<uint32_t>(std::max(0, idx)));
        }

        if (m_rhiContext && !m_deferOverlayMode)
        {
            if (auto* rawVb = m_rhiContext->createResource<FCT::RHI::VertexBuffer>())
            {
                geom->cpuVertexBuffer = makeUnique<FCT::VertexBuffer>(kRml2dVertexLayout);
                geom->cpuVertexBuffer->resize(geom->vertices.size());
                std::memcpy(
                    geom->cpuVertexBuffer->getData(),
                    geom->vertices.data(),
                    geom->vertices.size() * sizeof(Rml::Vertex));

                geom->gpuVertexBuffer = UniquePtr<FCT::RHI::VertexBuffer>(rawVb);
                geom->gpuVertexBuffer->updateFrequency(FCT::UpdateFrequency::Dynamic);
                geom->gpuVertexBuffer->vertexBuffer(geom->cpuVertexBuffer.get());
                geom->gpuVertexBuffer->create();
                geom->gpuVertexBuffer->updataBuffer();
            }
            if (auto* rawIb = m_rhiContext->createResource<FCT::RHI::IndexBuffer>())
            {
                geom->gpuIndexBuffer = UniquePtr<FCT::RHI::IndexBuffer>(rawIb);
                geom->gpuIndexBuffer->updateFrequency(FCT::UpdateFrequency::Dynamic);
                geom->gpuIndexBuffer->indexBuffer(geom->gpuIndices);
                geom->gpuIndexBuffer->create();
                geom->gpuIndexBuffer->updataBuffer();
            }
        }

        const Rml::CompiledGeometryHandle handle = m_nextGeometryHandle++;
        m_compiledGeometries.emplace(handle, std::move(geom));
        return handle;
    }

    Rml::CompiledGeometryHandle MQRmlRenderInterface::compileGeometry(
        const std::vector<Rml::Vertex>& vertices,
        const std::vector<int>& indices)
    {
        return CompileGeometry(
            Rml::Span<const Rml::Vertex>(vertices.data(), vertices.size()),
            Rml::Span<const int>(indices.data(), indices.size()));
    }

    void MQRmlRenderInterface::RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture)
    {
        if (geometry == 0)
        {
            ++m_skippedDrawCommandCount;
            return;
        }

        auto it = m_compiledGeometries.find(geometry);
        if (it == m_compiledGeometries.end() || !it->second)
        {
            ++m_skippedDrawCommandCount;
            return;
        }

        MQRmlDrawCommand cmd;
        cmd.geometryHandle = geometry;
        cmd.vertices = it->second->vertices;
        cmd.indices = it->second->indices;
        cmd.translation = translation;
        cmd.texture = texture;
        cmd.scissor = m_scissorRegion;
        cmd.scissorEnabled = m_scissorEnabled;
        m_drawCommands.push_back(std::move(cmd));
    }

    void MQRmlRenderInterface::renderGeometry(
        Rml::CompiledGeometryHandle geometry,
        float transX,
        float transY,
        Rml::TextureHandle texture)
    {
        RenderGeometry(geometry, Rml::Vector2f(transX, transY), texture);
    }

    void MQRmlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        if (geometry == 0)
        {
            return;
        }

        auto it = m_compiledGeometries.find(geometry);
        if (it == m_compiledGeometries.end())
        {
            return;
        }

        if (m_rhiContext && it->second &&
            (it->second->cpuVertexBuffer || it->second->gpuVertexBuffer || it->second->gpuIndexBuffer))
        {
            auto* deferredHolder = FCT_NEW(
                DeferredRmlGpuBufferHolder,
                std::move(it->second->cpuVertexBuffer),
                std::move(it->second->gpuVertexBuffer),
                std::move(it->second->gpuIndexBuffer));

            auto* renderHost = Host::RenderHost::InstancePtr();
            auto* gpuMem = Resource::GpuMemoryManager::InstancePtr();
            if (renderHost && gpuMem && renderHost->getContext() == m_rhiContext)
            {
                // Note: Context::deferRelease 按环形槽位在 Sync 阶段推进，若同帧先提交 DrawBucket 后关闭/重编文档或处于单在途帧设备，
                //       可能早于在途 Fence 完成销毁缓冲；统一交由 GpuMemoryManager 按单调递增 64 位渲染帧号跨帧回收。
                const uint64_t retiredFrame = renderHost->getRenderFrameCount() + 2u;
                gpuMem->deferRelease(
                    deferredHolder,
                    [](void* ptr) {
                        if (auto* holder = static_cast<DeferredRmlGpuBufferHolder*>(ptr))
                        {
                            holder->release();
                        }
                    },
                    retiredFrame);
            }
            else
            {
                m_rhiContext->deferRelease(deferredHolder);
            }
        }

        m_compiledGeometries.erase(it);
    }

    Rml::TextureHandle MQRmlRenderInterface::LoadTexture(
        Rml::Vector2i& texture_dimensions,
        const Rml::String& source)
    {
        return m_textureRouter.loadTexture(
            texture_dimensions,
            std::string_view(source.c_str(), source.size()));
    }

    Rml::TextureHandle MQRmlRenderInterface::GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i source_dimensions)
    {
        if (source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0)
        {
            return m_textureRouter.getPlaceholderTextureHandle();
        }
        const size_t pixelCount = static_cast<size_t>(source_dimensions.x) * static_cast<size_t>(source_dimensions.y);
        const size_t expectedBytes = pixelCount * 4u;
        const Rml::TextureHandle handle = static_cast<Rml::TextureHandle>(m_generatedTextureCounter++);

        if (source.size() >= expectedBytes)
        {
            GeneratedRmlTexture tex;
            tex.dimensions = source_dimensions;
            tex.cpuStraightRgba.resize(expectedBytes);
            for (size_t i = 0; i < pixelCount; ++i)
            {
                const uint32_t pr = source[i * 4u + 0u];
                const uint32_t pg = source[i * 4u + 1u];
                const uint32_t pb = source[i * 4u + 2u];
                const uint32_t pa = source[i * 4u + 3u];
                if (pa > 0u)
                {
                    tex.cpuStraightRgba[i * 4u + 0u] = static_cast<uint8_t>(std::min(255u, (pr * 255u + pa / 2u) / pa));
                    tex.cpuStraightRgba[i * 4u + 1u] = static_cast<uint8_t>(std::min(255u, (pg * 255u + pa / 2u) / pa));
                    tex.cpuStraightRgba[i * 4u + 2u] = static_cast<uint8_t>(std::min(255u, (pb * 255u + pa / 2u) / pa));
                    tex.cpuStraightRgba[i * 4u + 3u] = static_cast<uint8_t>(pa);
                }
                else
                {
                    tex.cpuStraightRgba[i * 4u + 0u] = 0u;
                    tex.cpuStraightRgba[i * 4u + 1u] = 0u;
                    tex.cpuStraightRgba[i * 4u + 2u] = 0u;
                    tex.cpuStraightRgba[i * 4u + 3u] = 0u;
                }
            }
            tex.gpuDirty = true;
            m_generatedTextures.emplace(handle, std::move(tex));
        }

        return handle;
    }

    void MQRmlRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
    {
        auto it = m_generatedTextures.find(texture);
        if (it != m_generatedTextures.end())
        {
            if (it->second.gpuImage)
            {
                m_retiredGeneratedImages.push_back(std::move(it->second.gpuImage));
            }
            m_generatedTextures.erase(it);
        }
    }

    void MQRmlRenderInterface::flushGeneratedTexturesToGpu(FCT::Context* ctx)
    {
        if (!ctx)
        {
            return;
        }
        for (auto& [handle, tex] : m_generatedTextures)
        {
            (void)handle;
            if (!tex.gpuDirty || tex.cpuStraightRgba.empty() || tex.dimensions.x <= 0 || tex.dimensions.y <= 0)
            {
                continue;
            }
            if (!tex.gpuImage)
            {
                auto* img = FCT_NEW(FCT::SingleBufferImage, ctx);
                if (!img)
                {
                    continue;
                }
                img->width(tex.dimensions.x);
                img->height(tex.dimensions.y);
                img->arrayLayers(1);
                img->format(FCT::Format::R8G8B8A8_UNORM);
                img->as(FCT::ImageUsage::Texture);
                img->create();
                tex.gpuImage = Ptr<FCT::Image>(img);
                img->release();
            }
            if (tex.gpuImage)
            {
                tex.gpuImage->updateData(tex.cpuStraightRgba.data(), tex.cpuStraightRgba.size());
                tex.gpuDirty = false;
            }
        }
    }

    FCT::Image* MQRmlRenderInterface::resolveGeneratedGpuImage(Rml::TextureHandle handle) const noexcept
    {
        auto it = m_generatedTextures.find(handle);
        return (it != m_generatedTextures.end() && it->second.gpuImage) ? it->second.gpuImage.get() : nullptr;
    }

    std::vector<Ptr<FCT::Image>> MQRmlRenderInterface::takeRetiredGpuImages() noexcept
    {
        std::vector<Ptr<FCT::Image>> retired = std::move(m_retiredGeneratedImages);
        m_retiredGeneratedImages.clear();
        return retired;
    }

    void MQRmlRenderInterface::releaseAllGpuTextures() noexcept
    {
        m_retiredGeneratedImages.clear();
        m_generatedTextures.clear();
        m_textureRouter.releaseOwnedGpuImages();
    }

    void MQRmlRenderInterface::EnableScissorRegion(bool enable)
    {
        m_scissorEnabled = enable;
    }

    void MQRmlRenderInterface::SetScissorRegion(Rml::Rectanglei region)
    {
        m_scissorRegion = region;
    }

    void MQRmlRenderInterface::setScissorRegion(int x, int y, int width, int height) noexcept
    {
        m_scissorRegion = Rml::Rectanglei::FromPositionSize(
            Rml::Vector2i(x, y),
            Rml::Vector2i(std::max(0, width), std::max(0, height)));
    }

    MQRmlScissorRect MQRmlRenderInterface::getScissorRegion() const noexcept
    {
        return MQRmlScissorRect{
            .x = m_scissorRegion.Left(),
            .y = m_scissorRegion.Top(),
            .width = m_scissorRegion.Width(),
            .height = m_scissorRegion.Height()
        };
    }

    bool MQRmlRenderInterface::isGeometryCompiled(Rml::CompiledGeometryHandle geometry) const noexcept
    {
        return geometry != 0 && m_compiledGeometries.contains(geometry);
    }

    void MQRmlRenderInterface::clearDrawCommands() noexcept
    {
        m_drawCommands.clear();
    }

    Domain::Renderer::DrawBucket MQRmlRenderInterface::buildAggregateDrawBucket(uint32_t boundTechId) const
    {
        Domain::Renderer::DrawBucket bucket(
            boundTechId,
            Domain::Renderer::InputTags::UI | Domain::Renderer::InputTags::RmlUi,
            static_cast<uint32_t>(std::max<size_t>(1u, m_drawCommands.size())));

        bucket.attributeNames = {"Position2D", "Color4UB", "TexCoord2F"};

        uint32_t totalIndices = 0;
        for (const auto& cmd : m_drawCommands)
        {
            totalIndices += static_cast<uint32_t>(cmd.indices.size());
            auto geomIt = m_compiledGeometries.find(cmd.geometryHandle);
            if (geomIt != m_compiledGeometries.end() && geomIt->second)
            {
                if (!bucket.vertexBuffer && geomIt->second->gpuVertexBuffer)
                {
                    bucket.vertexBuffer = geomIt->second->gpuVertexBuffer.get();
                }
                if (!bucket.indexBuffer && geomIt->second->gpuIndexBuffer)
                {
                    bucket.indexBuffer = geomIt->second->gpuIndexBuffer.get();
                }
            }
        }
        bucket.indexCount = totalIndices;
        return bucket;
    }

    void MQRmlRenderInterface::collectDrawBuckets(
        std::vector<Domain::Renderer::DrawBucket>& outBuckets,
        uint32_t boundTechId) const
    {
        outBuckets.clear();
        if (m_drawCommands.empty())
        {
            return;
        }

        outBuckets.reserve(m_drawCommands.size());
        const uint64_t tagsMask = Domain::Renderer::InputTags::UI | Domain::Renderer::InputTags::RmlUi;

        for (const auto& cmd : m_drawCommands)
        {
            Domain::Renderer::DrawBucket bucket(boundTechId, tagsMask, 1u);
            bucket.attributeNames = {"Position2D", "Color4UB", "TexCoord2F"};
            bucket.indexCount = static_cast<uint32_t>(cmd.indices.size());

            auto geomIt = m_compiledGeometries.find(cmd.geometryHandle);
            if (geomIt != m_compiledGeometries.end() && geomIt->second)
            {
                bucket.vertexBuffer = geomIt->second->gpuVertexBuffer.get();
                bucket.indexBuffer = geomIt->second->gpuIndexBuffer.get();
            }
            outBuckets.push_back(std::move(bucket));
        }
    }
} // namespace MQEngine::GameHtmlUI
