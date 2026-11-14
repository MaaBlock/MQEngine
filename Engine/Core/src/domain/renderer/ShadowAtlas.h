/**
 * @file ShadowAtlas.h
 * @brief 单张 2048x2048 动态视口阴影图集管理器 (PRD 02 §2.1, FEAT-11)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_SHADOWATLAS_H
#define MQENGINE_DOMAIN_RENDERER_SHADOWATLAS_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <string_view>
#include <vector>

namespace MQEngine::Domain::Renderer
{
    /**
     * @brief 阴影图集子视口矩形描述
     */
    struct ShadowViewportRect
    {
        uint32_t x{0};
        uint32_t y{0};
        uint32_t width{0};
        uint32_t height{0};

        [[nodiscard]] bool intersects(const ShadowViewportRect& other) const noexcept
        {
            return !(x + width <= other.x || other.x + other.width <= x ||
                     y + height <= other.y || other.y + other.height <= y);
        }
    };

    /**
     * @brief 单 FBO 多光源动态视口切分阴影图集
     *        通过单张深度纹理与 glViewport 动态子区域切分，消除逐光源 FBO 切换与多阴影采样器开销。
     */
    class ENGINE_API ShadowAtlas
    {
    public:
        static constexpr uint32_t DefaultAtlasSize = 2048;

        explicit ShadowAtlas(uint32_t width = DefaultAtlasSize, uint32_t height = DefaultAtlasSize);
        ~ShadowAtlas();

        ShadowAtlas(const ShadowAtlas&) = delete;
        ShadowAtlas& operator=(const ShadowAtlas&) = delete;

        Status initGpuResources(FCT::Context* ctx);
        void releaseGpuResources() noexcept;

        void reset() noexcept;

        /**
         * @brief 基于 Shelf 策略动态分配一块不重叠的阴影子视口
         */
        bool allocateTile(uint32_t reqWidth, uint32_t reqHeight, ShadowViewportRect& outViewport);

        /**
         * @brief 按固定网格尺寸分配指定下标瓦片（越界返回 ResourceExhaustedError）
         */
        StatusOr<ShadowViewportRect> allocateGridTile(uint32_t tileIndex, uint32_t tileSize = 512);

        /**
         * @brief 手动注册自定义子视口并执行图集边界与非重叠校验（越界返回 OutOfRangeError）
         */
        Status insertCustomViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

        [[nodiscard]] const ShadowViewportRect* getTile(uint32_t index) const noexcept;
        [[nodiscard]] const std::vector<ShadowViewportRect>& getAllocatedTiles() const noexcept { return m_allocatedRects; }
        [[nodiscard]] size_t getAllocatedCount() const noexcept { return m_allocatedRects.size(); }

        [[nodiscard]] uint32_t getWidth() const noexcept { return m_width; }
        [[nodiscard]] uint32_t getHeight() const noexcept { return m_height; }
        [[nodiscard]] FCT::Format getFormat() const noexcept { return FCT::Format::D24_UNORM_S8_UINT; }
        [[nodiscard]] uint32_t getGlInternalFormat() const noexcept { return 0x88F0u; } // GL_DEPTH24_STENCIL8

        [[nodiscard]] uint32_t getShadowSamplerCount() const noexcept { return 1; }
        [[nodiscard]] static constexpr std::string_view getShadowSamplerSlotName() noexcept { return "u_ShadowAtlas"; }

        /**
         * @brief 计算子视口在图集内的归一化 UV 缩放与偏移 (xy = scale, zw = offset)
         */
        [[nodiscard]] FCT::Vec4 computeUvScaleOffset(const ShadowViewportRect& rect) const noexcept;

        /**
         * @brief 计算从裁剪空间 NDC [-1, 1] 映射到阴影图集子视口 UV 与 [0, 1] 深度的偏置矩阵
         */
        [[nodiscard]] FCT::Mat4 computeShadowBiasMatrix(const ShadowViewportRect& rect) const noexcept;

        [[nodiscard]] FCT::Image* getAtlasImage() const noexcept { return m_atlasImage; }

    private:
        struct ShelfRow
        {
            uint32_t y{0};
            uint32_t height{0};
            uint32_t cursorX{0};
        };

        bool overlapsExisting(const ShadowViewportRect& candidate) const noexcept;

        uint32_t m_width{DefaultAtlasSize};
        uint32_t m_height{DefaultAtlasSize};
        uint32_t m_nextShelfY{0};
        std::vector<ShelfRow> m_shelves;
        std::vector<ShadowViewportRect> m_allocatedRects;
        FCT::SingleBufferImage* m_atlasImage{nullptr};
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_SHADOWATLAS_H
