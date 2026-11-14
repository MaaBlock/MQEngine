/**
 * @file RmlUiBackendInterfaces.h
 * @brief RmlUi 6.0 引擎后端接口封装（直出 Backbuffer 渲染器与零拷贝虚拟纹理路由，FEAT-49 / FEAT-50）
 */
#ifndef MQENGINE_GAME_HTML_UI_RML_BACKEND_INTERFACES_H
#define MQENGINE_GAME_HTML_UI_RML_BACKEND_INTERFACES_H

#include "../../GameHtmlUiApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <Font/Composites/FontRuntimeComposite/FontTypes.h>

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

#include <RmlUi/Core.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine::Resource::Font
{
    class FontSystem;
}

namespace MQEngine::GameHtmlUI
{
    /**
     * @brief RmlUi 内存文件句柄描述结构
     */
    struct OpenedRmlFile
    {
        std::string data;
        size_t offset = 0;
    };

    /**
     * @brief 裁剪框简化描述结构
     */
    struct MQRmlScissorRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    /**
     * @brief RmlUiPass 通道直绘主屏 Backbuffer 契约描述 (FEAT-49)
     */
    struct MQRmlPassDescriptor
    {
        bool isBackbufferTarget = true;
        bool allocatesOffscreenRt = false;
        std::vector<std::string> inputTags{"UI", "RmlUi"};
    };

    /**
     * @brief RmlUi 2D 混合管线固定渲染状态描述 (FEAT-49)
     */
    struct MQRmlPipelineRenderState
    {
        bool depthWriteEnabled = false;
        bool depthTestEnabled = false;
        bool blendEnabled = true;
    };

    /**
     * @brief RmlUi 编译后 2D 网格几何体记录
     */
    struct MQRmlCompiledGeometry
    {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
        std::vector<uint32_t> gpuIndices;
        UniquePtr<FCT::VertexBuffer> cpuVertexBuffer;
        UniquePtr<FCT::RHI::VertexBuffer> gpuVertexBuffer;
        UniquePtr<FCT::RHI::IndexBuffer> gpuIndexBuffer;
    };

    /**
     * @brief RmlUi 单次绘制指令快照
     */
    struct MQRmlDrawCommand
    {
        Rml::CompiledGeometryHandle geometryHandle = 0;
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
        Rml::Vector2f translation{0.0f, 0.0f};
        Rml::TextureHandle texture = 0;
        Rml::Rectanglei scissor;
        bool scissorEnabled = false;
    };

    /**
     * @brief 零拷贝虚拟纹理 URI 路由器 (FEAT-50)
     *        负责拦截 @mq_glyph_atlas/<pageId> 与 @mq_shared_canvas/<name> 虚拟协议并映射到底层 GPU Image 句柄。
     */
    class RML_UI_COMPOSITE_API VirtualTextureRouter
    {
    public:
        VirtualTextureRouter();
        ~VirtualTextureRouter() = default;

        void bindFontSystem(Resource::Font::FontSystem* fontSystem) noexcept { m_fontSystem = fontSystem; }

        void setPlaceholderTextureHandle(Rml::TextureHandle handle) noexcept;
        [[nodiscard]] Rml::TextureHandle getPlaceholderTextureHandle() const noexcept { return m_placeholderHandle; }

        void registerFontAtlasPage(
            uint32_t pageId,
            Rml::TextureHandle handle,
            Resource::Font::TextureFormat format = Resource::Font::TextureFormat::R8_UNORM,
            Rml::Vector2i dimensions = Rml::Vector2i(1024, 1024));

        void registerSharedCanvas(
            std::string_view canvasName,
            Rml::TextureHandle handle,
            Rml::Vector2i dimensions = Rml::Vector2i(512, 512),
            FCT::Image* gpuImage = nullptr);

        void unregisterSharedCanvas(std::string_view canvasName);

        void notifyAssetLoaded(
            std::string_view uri,
            Rml::TextureHandle handle,
            Rml::Vector2i dimensions);

        Rml::TextureHandle loadTexture(Rml::Vector2i& outDimensions, std::string_view sourceUri);
        [[nodiscard]] Rml::TextureHandle loadTexture(std::string_view sourceUri);

        [[nodiscard]] FCT::Image* resolveBoundGpuImage(Rml::TextureHandle handle) const noexcept;
        [[nodiscard]] bool tryResolveAtlasPageId(Rml::TextureHandle handle, uint32_t& outPageId) const noexcept;
        void flushDirtyAssetTexturesToGpu(FCT::Context* ctx);
        void releaseOwnedGpuImages() noexcept;

    private:
        struct RegisteredTextureEntry
        {
            Rml::TextureHandle handle = 0;
            Rml::Vector2i dimensions{1, 1};
            Resource::Font::TextureFormat format = Resource::Font::TextureFormat::R8G8B8A8_UNORM;
            FCT::Image* gpuImage = nullptr;
            Ptr<FCT::Image> ownedGpuImage;
            std::vector<uint8_t> cpuRgbaPixels;
            bool gpuDirty = false;
        };

    private:
        Rml::TextureHandle resolveGlyphAtlasUri(std::string_view suffix, Rml::Vector2i& outDimensions);
        Rml::TextureHandle resolveSharedCanvasUri(std::string_view suffix, Rml::Vector2i& outDimensions);
        Rml::TextureHandle resolveStandardAssetUri(std::string_view uri, Rml::Vector2i& outDimensions);
        Resource::Font::FontSystem* activeFontSystem() const noexcept;

    private:
        Resource::Font::FontSystem* m_fontSystem = nullptr;
        Rml::TextureHandle m_placeholderHandle = 2;
        Rml::TextureHandle m_nextGeneratedHandle = 10000;

        std::unordered_map<uint32_t, RegisteredTextureEntry> m_atlasPages;
        std::unordered_map<std::string, RegisteredTextureEntry> m_sharedCanvases;
        std::unordered_map<std::string, RegisteredTextureEntry> m_loadedAssets;
        std::unordered_map<Rml::TextureHandle, FCT::Image*> m_handleToImage;
        std::unordered_map<Rml::TextureHandle, uint32_t> m_handleToAtlasPageId;
    };

    /**
     * @brief RmlUi 系统接口实现（高精度计时、剪贴板、统一日志上报）
     */
    class RML_UI_COMPOSITE_API MQRmlSystemInterface : public Rml::SystemInterface
    {
    public:
        MQRmlSystemInterface() = default;
        ~MQRmlSystemInterface() override = default;

        double GetElapsedTime() override;
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
        void SetClipboardText(const Rml::String& text) override;
        void GetClipboardText(Rml::String& text) override;

    private:
        std::string m_clipboard;
    };

    /**
     * @brief RmlUi 文件接口实现（全部经由引擎 ResourceLoader 读取并记录最近访问路径）
     */
    class RML_UI_COMPOSITE_API MQRmlFileInterface : public Rml::FileInterface
    {
    public:
        MQRmlFileInterface() = default;
        ~MQRmlFileInterface() override = default;

        Rml::FileHandle Open(const Rml::String& path) override;
        void Close(Rml::FileHandle file) override;
        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
        bool Seek(Rml::FileHandle file, long offset, int origin) override;
        size_t Tell(Rml::FileHandle file) override;
        size_t Length(Rml::FileHandle file) override;
        bool LoadFile(const Rml::String& path, Rml::String& out_data) override;

        [[nodiscard]] bool hasQueriedPath(std::string_view substring) const noexcept;
        void clearQueriedPaths() noexcept { m_queriedPaths.clear(); }

    private:
        void recordPathAccess(std::string_view path);

    private:
        std::vector<std::string> m_queriedPaths;
    };

    /**
     * @brief RmlUi 渲染后端接口实现：管理静态/瞬态 2D 网格、Scissor 裁剪及直绘 Backbuffer DrawBucket 输出 (FEAT-49)
     */
    class RML_UI_COMPOSITE_API MQRmlRenderInterface : public Rml::RenderInterface
    {
    public:
        MQRmlRenderInterface();
        ~MQRmlRenderInterface() override;

        void bindRhiContext(FCT::Context* rhiContext) noexcept { m_rhiContext = rhiContext; }
        void setDeferOverlayMode(bool defer) noexcept { m_deferOverlayMode = defer; }
        [[nodiscard]] bool isDeferOverlayMode() const noexcept { return m_deferOverlayMode; }
        [[nodiscard]] VirtualTextureRouter& getTextureRouter() noexcept { return m_textureRouter; }
        [[nodiscard]] const VirtualTextureRouter& getTextureRouter() const noexcept { return m_textureRouter; }

        void flushGeneratedTexturesToGpu(FCT::Context* ctx);
        [[nodiscard]] FCT::Image* resolveGeneratedGpuImage(Rml::TextureHandle handle) const noexcept;
        [[nodiscard]] std::vector<Ptr<FCT::Image>> takeRetiredGpuImages() noexcept;
        void releaseAllGpuTextures() noexcept;

        // ==================== Rml::RenderInterface 核心契约 ====================

        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices,
            Rml::Span<const int> indices) override;

        void RenderGeometry(
            Rml::CompiledGeometryHandle geometry,
            Rml::Vector2f translation,
            Rml::TextureHandle texture) override;

        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

        Rml::TextureHandle LoadTexture(
            Rml::Vector2i& texture_dimensions,
            const Rml::String& source) override;

        Rml::TextureHandle GenerateTexture(
            Rml::Span<const Rml::byte> source,
            Rml::Vector2i source_dimensions) override;

        void ReleaseTexture(Rml::TextureHandle texture) override;
        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

        // ==================== 引擎渲染图与测试审查接口 ====================

        Rml::CompiledGeometryHandle compileGeometry(
            const std::vector<Rml::Vertex>& vertices,
            const std::vector<int>& indices);

        void renderGeometry(
            Rml::CompiledGeometryHandle geometry,
            float transX,
            float transY,
            Rml::TextureHandle texture);

        void enableScissorRegion(bool enable) noexcept { EnableScissorRegion(enable); }
        void setScissorRegion(int x, int y, int width, int height) noexcept;

        [[nodiscard]] bool isScissorEnabled() const noexcept { return m_scissorEnabled; }
        [[nodiscard]] MQRmlScissorRect getScissorRegion() const noexcept;
        [[nodiscard]] bool isGeometryCompiled(Rml::CompiledGeometryHandle geometry) const noexcept;

        void clearDrawCommands() noexcept;
        [[nodiscard]] const std::vector<MQRmlDrawCommand>& getDrawCommands() const noexcept { return m_drawCommands; }
        [[nodiscard]] size_t getRecordedCommandCount() const noexcept { return m_drawCommands.size(); }
        [[nodiscard]] uint32_t getSkippedDrawCommandCount() const noexcept { return m_skippedDrawCommandCount; }

        [[nodiscard]] bool isBackbufferTarget() const noexcept { return true; }
        [[nodiscard]] bool allocatesOffscreenRt() const noexcept { return false; }
        [[nodiscard]] std::vector<std::string> getPassInputTags() const { return {"UI", "RmlUi"}; }
        [[nodiscard]] MQRmlPassDescriptor getPassDescriptor() const { return MQRmlPassDescriptor{}; }
        [[nodiscard]] MQRmlPipelineRenderState getPipelineRenderState() const { return MQRmlPipelineRenderState{}; }

        [[nodiscard]] Domain::Renderer::DrawBucket buildAggregateDrawBucket(uint32_t boundTechId = 1) const;
        void collectDrawBuckets(std::vector<Domain::Renderer::DrawBucket>& outBuckets, uint32_t boundTechId = 1) const;

    private:
        struct GeneratedRmlTexture
        {
            Rml::Vector2i dimensions{1, 1};
            std::vector<uint8_t> cpuStraightRgba;
            Ptr<FCT::Image> gpuImage;
            bool gpuDirty = true;
        };

        FCT::Context* m_rhiContext = nullptr;
        VirtualTextureRouter m_textureRouter;
        bool m_deferOverlayMode = false;

        bool m_scissorEnabled = false;
        Rml::Rectanglei m_scissorRegion = Rml::Rectanglei::FromPositionSize({0, 0}, {0, 0});

        std::unordered_map<Rml::CompiledGeometryHandle, UniquePtr<MQRmlCompiledGeometry>> m_compiledGeometries;
        std::unordered_map<Rml::TextureHandle, GeneratedRmlTexture> m_generatedTextures;
        std::vector<Ptr<FCT::Image>> m_retiredGeneratedImages;
        std::vector<MQRmlDrawCommand> m_drawCommands;
        Rml::CompiledGeometryHandle m_nextGeometryHandle = 1;
        uintptr_t m_generatedTextureCounter = 50000;
        uint32_t m_skippedDrawCommandCount = 0;
    };
} // namespace MQEngine::GameHtmlUI

#endif // MQENGINE_GAME_HTML_UI_RML_BACKEND_INTERFACES_H
