#ifndef MQENGINE_DOMAIN_RENDERER_DATA_ATLASSHELFPACKER_H
#define MQENGINE_DOMAIN_RENDERER_DATA_ATLASSHELFPACKER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <vector>
#include <mutex>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief 2D 图集分配结果
             */
            struct AtlasAllocation
            {
                uint32_t x = 0;
                uint32_t y = 0;
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t layer = 0;
                bool success = false;
            };

            /**
             * @brief Tier 0 Vulkan 1.0 全局纹理图集装箱分配器 (Shelf Packer)
             *        用于将异步解码纹理打包进全局 Texture2DArray，杜绝 Vulkan 1.0 逐材质切换描述符
             */
            class AtlasShelfPacker
            {
            public:
                struct Shelf
                {
                    uint32_t y = 0;
                    uint32_t height = 0;
                    uint32_t currentX = 0;
                };

                struct Layer
                {
                    uint32_t currentShelfY = 0;
                    std::vector<Shelf> shelves;
                };

                explicit AtlasShelfPacker(uint32_t atlasWidth = 2048, uint32_t atlasHeight = 2048, uint32_t maxLayers = 16)
                    : m_atlasWidth(atlasWidth)
                    , m_atlasHeight(atlasHeight)
                    , m_maxLayers(maxLayers)
                {
                    m_layers.resize(m_maxLayers);
                }

                AtlasAllocation allocate(uint32_t width, uint32_t height)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    AtlasAllocation alloc{};
                    if (width == 0 || height == 0 || width > m_atlasWidth || height > m_atlasHeight) {
                        return alloc;
                    }

                    // 1. 尝试在现有图层与现有 Shelf 中匹配最佳适配
                    for (uint32_t layerIdx = 0; layerIdx < m_layers.size(); ++layerIdx) {
                        auto& layer = m_layers[layerIdx];
                        for (auto& shelf : layer.shelves) {
                            if (shelf.currentX + width <= m_atlasWidth && height <= shelf.height) {
                                alloc.x = shelf.currentX;
                                alloc.y = shelf.y;
                                alloc.width = width;
                                alloc.height = height;
                                alloc.layer = layerIdx;
                                alloc.success = true;
                                shelf.currentX += width;
                                return alloc;
                            }
                        }

                        // 2. 现有 Shelf 装不下，在该图层新建一个 Shelf
                        if (layer.currentShelfY + height <= m_atlasHeight) {
                            Shelf newShelf;
                            newShelf.y = layer.currentShelfY;
                            newShelf.height = height;
                            newShelf.currentX = width;
                            layer.currentShelfY += height;
                            layer.shelves.push_back(newShelf);

                            alloc.x = 0;
                            alloc.y = newShelf.y;
                            alloc.width = width;
                            alloc.height = height;
                            alloc.layer = layerIdx;
                            alloc.success = true;
                            return alloc;
                        }
                    }

                    // 全部图层均已满载
                    alloc.success = false;
                    return alloc;
                }

                FCT::Vec4 calculateUvScaleOffset(const AtlasAllocation& alloc) const noexcept
                {
                    if (!alloc.success || m_atlasWidth == 0 || m_atlasHeight == 0) {
                        return FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
                    }
                    const float sx = static_cast<float>(alloc.width) / static_cast<float>(m_atlasWidth);
                    const float sy = static_cast<float>(alloc.height) / static_cast<float>(m_atlasHeight);
                    const float ox = static_cast<float>(alloc.x) / static_cast<float>(m_atlasWidth);
                    const float oy = static_cast<float>(alloc.y) / static_cast<float>(m_atlasHeight);
                    return FCT::Vec4{sx, sy, ox, oy};
                }

                void reset()
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (auto& layer : m_layers) {
                        layer.currentShelfY = 0;
                        layer.shelves.clear();
                    }
                }

                uint32_t getAtlasWidth() const noexcept { return m_atlasWidth; }
                uint32_t getAtlasHeight() const noexcept { return m_atlasHeight; }
                uint32_t getMaxLayers() const noexcept { return m_maxLayers; }

            private:
                uint32_t m_atlasWidth;
                uint32_t m_atlasHeight;
                uint32_t m_maxLayers;
                std::vector<Layer> m_layers;
                mutable std::mutex m_mutex;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_DATA_ATLASSHELFPACKER_H
