#include <MQEngine/resource/core/BlockPool.h>
#include <MQEngine/resource/core/MemoryTracker.h>
#include <algorithm>
#include <cassert>
#include <new>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            constexpr size_t kDefaultPoolAlignment = 8;

            inline size_t alignPoolSize(size_t val) noexcept
            {
                return (val + (kDefaultPoolAlignment - 1)) & ~(kDefaultPoolAlignment - 1);
            }
        } // namespace

        BlockPool::BlockPool(size_t blockSize, size_t blocksPerChunk, MemoryTag tag)
            : m_blockSize(alignPoolSize(std::max(blockSize, sizeof(void*))))
            , m_blocksPerChunk(blocksPerChunk > 0 ? blocksPerChunk : 256)
            , m_tag(tag)
        {
        }

        BlockPool::~BlockPool()
        {
            void* dummy = nullptr;
            while (m_freeQueue.pop(dummy)) {}

            const size_t chunkSize = m_blockSize * m_blocksPerChunk;
            ChunkNode* curr = m_chunksHead.exchange(nullptr, std::memory_order_acq_rel);
            while (curr) {
                if (curr->data) {
                    if (auto* tracker = MemoryTracker::InstancePtr()) {
                        tracker->recordRawBytesFreed(chunkSize, m_tag);
                    }
                    ::operator delete[](curr->data);
                    curr->data = nullptr;
                }
                ChunkNode* next = curr->next;
                delete curr;
                curr = next;
            }
            m_inUseBlocks.store(0, std::memory_order_relaxed);
            m_freeBlocks.store(0, std::memory_order_relaxed);
            m_totalChunkCount.store(0, std::memory_order_relaxed);
        }

        void BlockPool::allocateNewChunk()
        {
            const size_t chunkSize = m_blockSize * m_blocksPerChunk;
            auto* chunkData = static_cast<uint8_t*>(::operator new[](chunkSize));
            if (auto* tracker = MemoryTracker::InstancePtr()) {
                tracker->recordRawBytesAllocated(chunkSize, m_tag);
            }
            auto* chunkNode = new ChunkNode{chunkData, nullptr};

            // Note: CAS 将 chunk 挂载到 m_chunksHead
            ChunkNode* oldHead = m_chunksHead.load(std::memory_order_relaxed);
            do {
                chunkNode->next = oldHead;
            } while (!m_chunksHead.compare_exchange_weak(
                oldHead, chunkNode, std::memory_order_release, std::memory_order_relaxed));

            m_totalChunkCount.fetch_add(1, std::memory_order_relaxed);

            // Note: 将新块全数压入无锁空闲队列
            for (size_t i = 0; i < m_blocksPerChunk; ++i) {
                uint8_t* blockAddr = chunkData + (i * m_blockSize);
                m_freeQueue.push(blockAddr);
            }
            m_freeBlocks.fetch_add(m_blocksPerChunk, std::memory_order_relaxed);
        }

        void* BlockPool::acquire()
        {
            void* block = nullptr;
            if (m_freeQueue.pop(block)) {
                m_inUseBlocks.fetch_add(1, std::memory_order_relaxed);
                size_t fb = m_freeBlocks.load(std::memory_order_relaxed);
                while (fb > 0 && !m_freeBlocks.compare_exchange_weak(fb, fb - 1, std::memory_order_relaxed)) {}
                return block;
            }

            // Note: 空闲队列为空时扩容新 Chunk
            allocateNewChunk();
            if (m_freeQueue.pop(block)) {
                m_inUseBlocks.fetch_add(1, std::memory_order_relaxed);
                size_t fb = m_freeBlocks.load(std::memory_order_relaxed);
                while (fb > 0 && !m_freeBlocks.compare_exchange_weak(fb, fb - 1, std::memory_order_relaxed)) {}
                return block;
            }

            assert(false && "块内存池分配失败：未能从新 chunk 弹出空闲块");
            return nullptr;
        }

        void BlockPool::release(void* block) noexcept
        {
            if (!block) {
                return;
            }

            m_freeQueue.push(block);
            size_t inUse = m_inUseBlocks.load(std::memory_order_relaxed);
            while (inUse > 0) {
                if (m_inUseBlocks.compare_exchange_weak(inUse, inUse - 1, std::memory_order_relaxed)) {
                    break;
                }
            }
            m_freeBlocks.fetch_add(1, std::memory_order_relaxed);
        }

        size_t BlockPool::allocatedBlockCount() const noexcept
        {
            return m_inUseBlocks.load(std::memory_order_relaxed);
        }

        size_t BlockPool::freeBlockCount() const noexcept
        {
            return m_freeBlocks.load(std::memory_order_relaxed);
        }

        size_t BlockPool::totalCapacityBytes() const noexcept
        {
            return m_totalChunkCount.load(std::memory_order_relaxed) * m_blockSize * m_blocksPerChunk;
        }

        // ---------------- BlockPoolManager 实现 ----------------

        BlockPoolManager::BlockPoolManager()
        {
            for (size_t i = 0; i < kPoolCount; ++i) {
                m_pools[i] = makeUnique<BlockPool>(kPoolSizes[i], 256, MemoryTag::Pool);
            }
        }

        size_t BlockPoolManager::findPoolIndex(size_t size) const noexcept
        {
            for (size_t i = 0; i < kPoolCount; ++i) {
                if (size <= kPoolSizes[i]) {
                    return i;
                }
            }
            return kPoolCount;
        }

        void* BlockPoolManager::acquire(size_t size)
        {
            const size_t idx = findPoolIndex(size);
            if (idx < kPoolCount) {
                return m_pools[idx]->acquire();
            }
            // 超出最大分级尺寸则直接向系统堆申请
            auto* raw = ::operator new[](size);
            if (auto* tracker = MemoryTracker::InstancePtr()) {
                tracker->recordAllocation(raw, size, "BlockPoolOverflow", MemoryTag::Pool);
            }
            return raw;
        }

        void BlockPoolManager::release(void* ptr, size_t size) noexcept
        {
            if (!ptr) {
                return;
            }
            const size_t idx = findPoolIndex(size);
            if (idx < kPoolCount) {
                m_pools[idx]->release(ptr);
                return;
            }
            if (auto* tracker = MemoryTracker::InstancePtr()) {
                tracker->recordFree(ptr);
            }
            ::operator delete[](ptr);
        }

        ScopedBlock BlockPoolManager::acquireScoped(size_t size)
        {
            const size_t idx = findPoolIndex(size);
            if (idx < kPoolCount) {
                void* mem = m_pools[idx]->acquire();
                return ScopedBlock(m_pools[idx].get(), mem, kPoolSizes[idx]);
            }
            void* mem = acquire(size);
            return ScopedBlock(nullptr, mem, size);
        }

        size_t BlockPoolManager::totalAllocatedBytes() const noexcept
        {
            size_t total = 0;
            for (size_t i = 0; i < kPoolCount; ++i) {
                if (m_pools[i]) {
                    total += m_pools[i]->allocatedBlockCount() * m_pools[i]->blockSize();
                }
            }
            return total;
        }

        size_t BlockPoolManager::totalCapacityBytes() const noexcept
        {
            size_t total = 0;
            for (size_t i = 0; i < kPoolCount; ++i) {
                if (m_pools[i]) {
                    total += m_pools[i]->totalCapacityBytes();
                }
            }
            return total;
        }

    } // namespace Resource
} // namespace MQEngine
