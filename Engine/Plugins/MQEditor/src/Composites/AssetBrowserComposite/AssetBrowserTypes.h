/**
 * @file AssetBrowserTypes.h
 * @brief 内容资产浏览器分类枚举、资产条目、.mqmeta 伴生元数据与无锁导入队列定义
 */
#ifndef MQENGINE_MQ_EDITOR_ASSET_BROWSER_TYPES_H
#define MQENGINE_MQ_EDITOR_ASSET_BROWSER_TYPES_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 资产文件类别枚举
         */
        enum class AssetCategory : uint8_t
        {
            Directory = 0,
            Mesh,
            Texture,
            Material,
            Shader,
            Script,
            SourceCode,
            Config,
            Scene,
            Unknown
        };

        using AssetFileType = AssetCategory;

        /**
         * @brief 资产 .mqmeta 伴生元数据侧车描述
         */
        struct AssetMetadataSidecar
        {
            std::string sceneName;
            uint32_t meshCount = 0;
            uint32_t materialCount = 0;
            uint32_t textureCount = 0;
            uint32_t boneCount = 0;
            uint32_t animationCount = 0;
            uint32_t totalVertices = 0;
            uint32_t totalTriangles = 0;
            bool hasSidecar = false;
        };

        /**
         * @brief 目录树快捷导航节点描述
         */
        struct DirectoryTreeNode
        {
            std::string label;
            std::string folderPath;
        };

        /**
         * @brief 资产浏览器单个资产条目描述
         */
        struct AssetEntry
        {
            std::string name;
            std::string path;
            std::string extension;
            AssetCategory type = AssetCategory::Unknown;
            size_t sizeBytes = 0;
            bool isDirectory = false;
            AssetMetadataSidecar sidecar{};
        };

        using AssetItemDesc = AssetEntry;

        /**
         * @brief 单生产者单消费者 (SPSC) 无锁环形队列，用于跨窗口事件回调投递拖拽文件路径
         */
        template <typename T, size_t Capacity = 256>
        class LockFreeSpscQueue
        {
        public:
            LockFreeSpscQueue() = default;
            ~LockFreeSpscQueue() = default;

            bool push(T item)
            {
                const size_t tail = m_tail.load(std::memory_order_relaxed);
                const size_t head = m_head.load(std::memory_order_acquire);
                if (((tail + 1) % Capacity) == head)
                {
                    return false;
                }
                m_ring[tail] = std::move(item);
                m_tail.store((tail + 1) % Capacity, std::memory_order_release);
                return true;
            }

            bool pop(T& outItem)
            {
                const size_t head = m_head.load(std::memory_order_relaxed);
                const size_t tail = m_tail.load(std::memory_order_acquire);
                if (head == tail)
                {
                    return false;
                }
                outItem = std::move(m_ring[head]);
                m_head.store((head + 1) % Capacity, std::memory_order_release);
                return true;
            }

            bool empty() const noexcept
            {
                return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
            }

        private:
            std::array<T, Capacity> m_ring;
            std::atomic<size_t> m_head{0};
            std::atomic<size_t> m_tail{0};
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_ASSET_BROWSER_TYPES_H
