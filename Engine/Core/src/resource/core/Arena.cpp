#include <MQEngine/resource/core/Arena.h>
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
            constexpr size_t kDefaultCacheLine = 64;

            uint8_t* allocateAlignedRaw(size_t byteSize)
            {
                return static_cast<uint8_t*>(::operator new(byteSize, std::align_val_t{kDefaultCacheLine}));
            }

            void freeAlignedRaw(uint8_t* ptr) noexcept
            {
                if (ptr) {
                    ::operator delete(ptr, std::align_val_t{kDefaultCacheLine});
                }
            }
        } // namespace

        Arena::Arena(size_t primaryCapacityBytes, MemoryTag tag)
            : m_primaryCapacity(alignUp(primaryCapacityBytes > 0 ? primaryCapacityBytes : 4096, kDefaultCacheLine))
            , m_tag(tag)
        {
            m_primaryBlock = allocateAlignedRaw(m_primaryCapacity);
            if (auto* tracker = MemoryTracker::InstancePtr()) {
                tracker->recordRawBytesAllocated(m_primaryCapacity, m_tag);
            }
        }

        Arena::~Arena()
        {
            reset();
            if (m_primaryBlock) {
                if (auto* tracker = MemoryTracker::InstancePtr()) {
                    tracker->recordRawBytesFreed(m_primaryCapacity, m_tag);
                }
                freeAlignedRaw(m_primaryBlock);
                m_primaryBlock = nullptr;
            }
        }

        void* Arena::tryAllocateFromPrimary(size_t byteSize, size_t alignment) noexcept
        {
            size_t current = m_primaryOffset.load(std::memory_order_relaxed);
            while (true) {
                const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_primaryBlock);
                const uintptr_t alignedAddr = alignUp(baseAddr + current, alignment);
                const size_t alignedOffset = static_cast<size_t>(alignedAddr - baseAddr);
                const size_t nextOffset = alignedOffset + byteSize;
                if (nextOffset > m_primaryCapacity) {
                    return nullptr;
                }
                if (m_primaryOffset.compare_exchange_weak(
                        current, nextOffset, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    recordPeakUsage(nextOffset + m_overflowUsedBytes.load(std::memory_order_relaxed));
                    return reinterpret_cast<void*>(alignedAddr);
                }
            }
        }

        void* Arena::allocateFromOverflow(size_t byteSize, size_t alignment)
        {
            // Note: 先尝试在当前链表头部的溢出块中进行 CAS 分配
            OverflowBlockNode* head = m_overflowHead.load(std::memory_order_acquire);
            while (head) {
                size_t currentOffset = head->offset.load(std::memory_order_relaxed);
                while (true) {
                    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(head->data);
                    const uintptr_t alignedAddr = alignUp(baseAddr + currentOffset, alignment);
                    const size_t alignedOffset = static_cast<size_t>(alignedAddr - baseAddr);
                    const size_t nextOffset = alignedOffset + byteSize;
                    if (nextOffset > head->capacity) {
                        break;
                    }
                    if (head->offset.compare_exchange_weak(
                            currentOffset, nextOffset, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                        const size_t delta = nextOffset - currentOffset;
                        const size_t totalOverflow = m_overflowUsedBytes.fetch_add(delta, std::memory_order_relaxed) + delta;
                        recordPeakUsage(m_primaryOffset.load(std::memory_order_relaxed) + totalOverflow);
                        return reinterpret_cast<void*>(alignedAddr);
                    }
                }
                break;
            }

            // Note: 分配新的溢出块节点并使用 CAS 原子推入链表头
            const size_t minBlockSize = std::max(m_primaryCapacity, alignUp(byteSize + alignment, kDefaultCacheLine));
            auto* newNode = new OverflowBlockNode();
            newNode->data = allocateAlignedRaw(minBlockSize);
            newNode->capacity = minBlockSize;
            if (auto* tracker = MemoryTracker::InstancePtr()) {
                tracker->recordRawBytesAllocated(minBlockSize, m_tag);
            }
            m_overflowCapacityBytes.fetch_add(minBlockSize, std::memory_order_relaxed);

            const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(newNode->data);
            const uintptr_t alignedAddr = alignUp(baseAddr, alignment);
            newNode->offset.store(static_cast<size_t>(alignedAddr - baseAddr) + byteSize, std::memory_order_relaxed);

            OverflowBlockNode* oldHead = m_overflowHead.load(std::memory_order_relaxed);
            do {
                newNode->next = oldHead;
            } while (!m_overflowHead.compare_exchange_weak(
                oldHead, newNode, std::memory_order_release, std::memory_order_relaxed));

            const size_t totalOverflow = m_overflowUsedBytes.fetch_add(newNode->offset.load(std::memory_order_relaxed), std::memory_order_relaxed) + newNode->offset.load(std::memory_order_relaxed);
            recordPeakUsage(m_primaryOffset.load(std::memory_order_relaxed) + totalOverflow);
            return reinterpret_cast<void*>(alignedAddr);
        }

        void* Arena::allocate(size_t byteSize, size_t alignment)
        {
            const size_t safeSize = byteSize > 0 ? byteSize : 1;
            const size_t safeAlign = alignment > 0 ? alignment : alignof(std::max_align_t);
            if (void* fastPtr = tryAllocateFromPrimary(safeSize, safeAlign)) {
                return fastPtr;
            }
            return allocateFromOverflow(safeSize, safeAlign);
        }

        void Arena::registerDestructor(void* target, void (*destroyFn)(void*))
        {
            if (!target || !destroyFn) {
                return;
            }
            auto* node = new DestructorNode{destroyFn, target, nullptr};
            DestructorNode* oldHead = m_destructorHead.load(std::memory_order_relaxed);
            do {
                node->next = oldHead;
            } while (!m_destructorHead.compare_exchange_weak(
                oldHead, node, std::memory_order_release, std::memory_order_relaxed));
        }

        void Arena::runDestructors() noexcept
        {
            DestructorNode* curr = m_destructorHead.exchange(nullptr, std::memory_order_acq_rel);
            while (curr) {
                if (curr->destroyFn && curr->target) {
                    curr->destroyFn(curr->target);
                }
                DestructorNode* next = curr->next;
                delete curr;
                curr = next;
            }
        }

        void Arena::freeOverflowBlocks() noexcept
        {
            OverflowBlockNode* curr = m_overflowHead.exchange(nullptr, std::memory_order_acq_rel);
            while (curr) {
                if (curr->data) {
                    if (auto* tracker = MemoryTracker::InstancePtr()) {
                        tracker->recordRawBytesFreed(curr->capacity, m_tag);
                    }
                    freeAlignedRaw(curr->data);
                    curr->data = nullptr;
                }
                OverflowBlockNode* next = curr->next;
                delete curr;
                curr = next;
            }
            m_overflowUsedBytes.store(0, std::memory_order_relaxed);
            m_overflowCapacityBytes.store(0, std::memory_order_relaxed);
        }

        void Arena::recordPeakUsage(size_t totalUsed) noexcept
        {
            size_t currentPeak = m_peakUsedBytes.load(std::memory_order_relaxed);
            while (totalUsed > currentPeak) {
                if (m_peakUsedBytes.compare_exchange_weak(
                        currentPeak, totalUsed, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    break;
                }
            }
        }

        void Arena::reset() noexcept
        {
            runDestructors();
            freeOverflowBlocks();
            m_primaryOffset.store(0, std::memory_order_release);
        }

        void Arena::rewindPrimary(size_t targetOffset) noexcept
        {
            size_t current = m_primaryOffset.load(std::memory_order_relaxed);
            if (targetOffset < current) {
                m_primaryOffset.store(targetOffset, std::memory_order_release);
            }
        }

        size_t Arena::usedBytes() const noexcept
        {
            return m_primaryOffset.load(std::memory_order_relaxed) + m_overflowUsedBytes.load(std::memory_order_relaxed);
        }

        size_t Arena::capacityBytes() const noexcept
        {
            return m_primaryCapacity + m_overflowCapacityBytes.load(std::memory_order_relaxed);
        }

        // ---------------- DoubleBufferedFrameArena 实现 ----------------

        DoubleBufferedFrameArena::DoubleBufferedFrameArena(size_t perFrameCapacityBytes)
        {
            m_arenas[0] = makeUnique<Arena>(perFrameCapacityBytes, MemoryTag::Arena);
            m_arenas[1] = makeUnique<Arena>(perFrameCapacityBytes, MemoryTag::Arena);
        }

        DoubleBufferedFrameArena::~DoubleBufferedFrameArena()
        {
            resetAll();
        }

        void DoubleBufferedFrameArena::nextFrame() noexcept
        {
            const uint64_t nextIdx = m_frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
            // 轮转至新活动帧，重置即将复用的前前帧分配器
            Arena& target = *m_arenas[nextIdx % 2];
            target.reset();
        }

        void DoubleBufferedFrameArena::resetAll() noexcept
        {
            if (m_arenas[0]) {
                m_arenas[0]->reset();
            }
            if (m_arenas[1]) {
                m_arenas[1]->reset();
            }
        }

        Arena& DoubleBufferedFrameArena::currentArena() noexcept
        {
            const uint64_t idx = m_frameIndex.load(std::memory_order_relaxed);
            return *m_arenas[idx % 2];
        }

        const Arena& DoubleBufferedFrameArena::currentArena() const noexcept
        {
            const uint64_t idx = m_frameIndex.load(std::memory_order_relaxed);
            return *m_arenas[idx % 2];
        }

        Arena& DoubleBufferedFrameArena::previousArena() noexcept
        {
            const uint64_t idx = m_frameIndex.load(std::memory_order_relaxed);
            return *m_arenas[(idx + 1) % 2];
        }

        const Arena& DoubleBufferedFrameArena::previousArena() const noexcept
        {
            const uint64_t idx = m_frameIndex.load(std::memory_order_relaxed);
            return *m_arenas[(idx + 1) % 2];
        }

    } // namespace Resource
} // namespace MQEngine
