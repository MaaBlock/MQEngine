#ifndef MQENGINE_RESOURCE_CORE_BLOCKPOOL_H
#define MQENGINE_RESOURCE_CORE_BLOCKPOOL_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <cstddef>
#include <cstdint>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 侵入式空闲链表的线程安全定长块内存池，适用于网络数据包、几何姿态暂存块与高频小对象分配
         */
        class ENGINE_API BlockPool
        {
        public:
            BlockPool(size_t blockSize, size_t blocksPerChunk = 256, MemoryTag tag = MemoryTag::Pool);
            ~BlockPool();

            BlockPool(const BlockPool&) = delete;
            BlockPool& operator=(const BlockPool&) = delete;

            void* acquire();
            void release(void* block) noexcept;

            size_t blockSize() const noexcept { return m_blockSize; }
            size_t allocatedBlockCount() const noexcept;
            size_t freeBlockCount() const noexcept;
            size_t totalCapacityBytes() const noexcept;

        private:
            struct ChunkNode
            {
                uint8_t* data = nullptr;
                ChunkNode* next = nullptr;
            };

            void allocateNewChunk();

            size_t m_blockSize = 0;
            size_t m_blocksPerChunk = 0;
            MemoryTag m_tag = MemoryTag::Pool;

            std::atomic<ChunkNode*> m_chunksHead{nullptr};
            boost::lockfree::queue<void*> m_freeQueue{1024};
            std::atomic<size_t> m_inUseBlocks{0};
            std::atomic<size_t> m_freeBlocks{0};
            std::atomic<size_t> m_totalChunkCount{0};
        };

        /**
         * @brief RAII 定长池化内存块句柄，析构时自动归还至所属 BlockPool
         */
        class ENGINE_API ScopedBlock
        {
        public:
            ScopedBlock() noexcept = default;
            ScopedBlock(BlockPool* pool, void* data, size_t capacity) noexcept
                : m_pool(pool), m_data(static_cast<uint8_t*>(data)), m_capacity(capacity)
            {
            }

            ~ScopedBlock()
            {
                reset();
            }

            ScopedBlock(const ScopedBlock&) = delete;
            ScopedBlock& operator=(const ScopedBlock&) = delete;

            ScopedBlock(ScopedBlock&& other) noexcept
                : m_pool(other.m_pool), m_data(other.m_data), m_capacity(other.m_capacity)
            {
                other.m_pool = nullptr;
                other.m_data = nullptr;
                other.m_capacity = 0;
            }

            ScopedBlock& operator=(ScopedBlock&& other) noexcept
            {
                if (this != &other) {
                    reset();
                    m_pool = other.m_pool;
                    m_data = other.m_data;
                    m_capacity = other.m_capacity;
                    other.m_pool = nullptr;
                    other.m_data = nullptr;
                    other.m_capacity = 0;
                }
                return *this;
            }

            void reset() noexcept
            {
                if (m_pool && m_data) {
                    m_pool->release(m_data);
                }
                m_pool = nullptr;
                m_data = nullptr;
                m_capacity = 0;
            }

            uint8_t* data() noexcept { return m_data; }
            const uint8_t* data() const noexcept { return m_data; }
            size_t capacity() const noexcept { return m_capacity; }
            explicit operator bool() const noexcept { return m_data != nullptr; }

        private:
            BlockPool* m_pool = nullptr;
            uint8_t* m_data = nullptr;
            size_t m_capacity = 0;
        };

        /**
         * @brief 分级定长块内存池管理器：内置 16B ~ 4KB 常用尺寸分级池
         */
        class ENGINE_API BlockPoolManager : public CoreEngineManager<BlockPoolManager>
        {
        public:
            BlockPoolManager();
            ~BlockPoolManager() = default;

            void* acquire(size_t size);
            void release(void* ptr, size_t size) noexcept;

            ScopedBlock acquireScoped(size_t size);

            size_t totalAllocatedBytes() const noexcept;
            size_t totalCapacityBytes() const noexcept;

        private:
            static constexpr size_t kPoolCount = 9;
            static constexpr std::array<size_t, kPoolCount> kPoolSizes = {
                16, 32, 64, 128, 256, 512, 1024, 2048, 4096
            };

            size_t findPoolIndex(size_t size) const noexcept;

            std::array<UniquePtr<BlockPool>, kPoolCount> m_pools;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_BLOCKPOOL_H
