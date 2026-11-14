/**
 * @file ShadowAtlas.cpp
 * @brief 单张 2048x2048 动态视口阴影图集管理器实现
 */
#include <MQEngine/domain/renderer/ShadowAtlas.h>

namespace MQEngine::Domain::Renderer
{
    ShadowAtlas::ShadowAtlas(uint32_t width, uint32_t height)
        : m_width(width > 0 ? width : DefaultAtlasSize)
        , m_height(height > 0 ? height : DefaultAtlasSize)
    {
    }

    ShadowAtlas::~ShadowAtlas()
    {
        releaseGpuResources();
    }

    Status ShadowAtlas::initGpuResources(FCT::Context* ctx)
    {
        if (!ctx)
        {
            return InvalidArgumentError("ShadowAtlas 初始化需要有效的 FCT::Context");
        }

        releaseGpuResources();

        m_atlasImage = FCT_NEW(FCT::SingleBufferImage, ctx);
        m_atlasImage->width(m_width);
        m_atlasImage->height(m_height);
        m_atlasImage->format(FCT::Format::D24_UNORM_S8_UINT);
        m_atlasImage->as(FCT::ImageUsage::DepthStencil | FCT::ImageUsage::Texture);
        m_atlasImage->create();
        return OkStatus();
    }

    void ShadowAtlas::releaseGpuResources() noexcept
    {
        if (m_atlasImage)
        {
            FCT_DELETE(m_atlasImage);
            m_atlasImage = nullptr;
        }
    }

    void ShadowAtlas::reset() noexcept
    {
        m_nextShelfY = 0;
        m_shelves.clear();
        m_allocatedRects.clear();
    }

    bool ShadowAtlas::overlapsExisting(const ShadowViewportRect& candidate) const noexcept
    {
        for (const auto& existing : m_allocatedRects)
        {
            if (candidate.intersects(existing))
            {
                return true;
            }
        }
        return false;
    }

    bool ShadowAtlas::allocateTile(uint32_t reqWidth, uint32_t reqHeight, ShadowViewportRect& outViewport)
    {
        if (reqWidth == 0 || reqHeight == 0 || reqWidth > m_width || reqHeight > m_height)
        {
            return false;
        }

        for (auto& shelf : m_shelves)
        {
            if (reqHeight <= shelf.height && shelf.cursorX + reqWidth <= m_width)
            {
                ShadowViewportRect candidate{shelf.cursorX, shelf.y, reqWidth, reqHeight};
                if (!overlapsExisting(candidate))
                {
                    shelf.cursorX += reqWidth;
                    m_allocatedRects.push_back(candidate);
                    outViewport = candidate;
                    return true;
                }
            }
        }

        if (m_nextShelfY + reqHeight > m_height)
        {
            return false;
        }

        ShadowViewportRect candidate{0, m_nextShelfY, reqWidth, reqHeight};
        if (overlapsExisting(candidate))
        {
            return false;
        }

        ShelfRow newShelf;
        newShelf.y = m_nextShelfY;
        newShelf.height = reqHeight;
        newShelf.cursorX = reqWidth;
        m_nextShelfY += reqHeight;
        m_shelves.push_back(newShelf);

        m_allocatedRects.push_back(candidate);
        outViewport = candidate;
        return true;
    }

    StatusOr<ShadowViewportRect> ShadowAtlas::allocateGridTile(uint32_t tileIndex, uint32_t tileSize)
    {
        if (tileSize == 0 || tileSize > m_width || tileSize > m_height)
        {
            return InvalidArgumentError("ShadowAtlas 网格瓦片尺寸非法");
        }

        const uint32_t cols = m_width / tileSize;
        const uint32_t rows = m_height / tileSize;
        const uint32_t maxTiles = cols * rows;
        if (tileIndex >= maxTiles)
        {
            return ResourceExhaustedError("ShadowAtlas 瓦片容量已满，拒绝越界分配");
        }

        const uint32_t col = tileIndex % cols;
        const uint32_t row = tileIndex / cols;
        ShadowViewportRect rect{col * tileSize, row * tileSize, tileSize, tileSize};
        if (overlapsExisting(rect))
        {
            return ResourceExhaustedError("ShadowAtlas 目标网格瓦片已被占用");
        }

        m_allocatedRects.push_back(rect);
        m_nextShelfY = std::max(m_nextShelfY, rect.y + rect.height);
        return rect;
    }

    Status ShadowAtlas::insertCustomViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return InvalidArgumentError("ShadowAtlas 自定义视口宽高必须大于 0");
        }

        if (static_cast<uint64_t>(x) + static_cast<uint64_t>(width) > m_width ||
            static_cast<uint64_t>(y) + static_cast<uint64_t>(height) > m_height)
        {
            return OutOfRangeError("ShadowAtlas 自定义视口超出图集物理边界");
        }

        ShadowViewportRect rect{x, y, width, height};
        if (overlapsExisting(rect))
        {
            return InvalidArgumentError("ShadowAtlas 自定义视口与已分配区域重叠");
        }

        m_allocatedRects.push_back(rect);
        m_nextShelfY = std::max(m_nextShelfY, rect.y + rect.height);
        return OkStatus();
    }

    const ShadowViewportRect* ShadowAtlas::getTile(uint32_t index) const noexcept
    {
        if (index >= m_allocatedRects.size())
        {
            return nullptr;
        }
        return &m_allocatedRects[index];
    }

    FCT::Vec4 ShadowAtlas::computeUvScaleOffset(const ShadowViewportRect& rect) const noexcept
    {
        if (m_width == 0 || m_height == 0)
        {
            return FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
        }
        const float invW = 1.0f / static_cast<float>(m_width);
        const float invH = 1.0f / static_cast<float>(m_height);
        return FCT::Vec4{
            static_cast<float>(rect.width) * invW,
            static_cast<float>(rect.height) * invH,
            static_cast<float>(rect.x) * invW,
            static_cast<float>(rect.y) * invH
        };
    }

    FCT::Mat4 ShadowAtlas::computeShadowBiasMatrix(const ShadowViewportRect& rect) const noexcept
    {
        const FCT::Vec4 uv = computeUvScaleOffset(rect);
        FCT::Mat4 bias{};
        // NDC [-1, 1] -> [0, 1] 再缩放偏移至图集子视口
        bias.m[0]  = 0.5f * uv.x;
        bias.m[5]  = 0.5f * uv.y;
        bias.m[10] = 0.5f;
        bias.m[15] = 1.0f;

        bias.m[3]  = 0.5f * uv.x + uv.z;
        bias.m[7]  = 0.5f * uv.y + uv.w;
        bias.m[11] = 0.5f;
        return bias;
    }
}
