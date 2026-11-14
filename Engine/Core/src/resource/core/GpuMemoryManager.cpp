#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <algorithm>
#include <cassert>
#include <iomanip>
#include <new>
#include <sstream>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            constexpr size_t kStagingAlignment = 256; // 满足大多数 GPU 缓冲拷贝对齐要求
            constexpr size_t kMinStagingBlockSize = 64 * 1024; // 64KB 最小分块

            inline size_t alignStagingSize(size_t val) noexcept
            {
                return (val + (kStagingAlignment - 1)) & ~(kStagingAlignment - 1);
            }
        } // namespace

        GpuMemoryManager::GpuMemoryManager()
        {
            for (auto& v : m_typeVramBytes) {
                v.store(0, std::memory_order_relaxed);
            }
        }

        GpuMemoryManager::~GpuMemoryManager()
        {
            flushAllDeferredReleases();

            for (auto& item : m_stagingPool) {
                if (item.cpuMemory) {
                    ::operator delete(item.cpuMemory, std::align_val_t{kStagingAlignment});
                    item.cpuMemory = nullptr;
                }
            }
            m_stagingPool.clear();
        }

        void GpuMemoryManager::recordVramAlloc(GpuResourceType type, uint64_t bytes) noexcept
        {
            const size_t typeIdx = static_cast<size_t>(type);
            if (typeIdx >= static_cast<size_t>(GpuResourceType::Count)) {
                return;
            }

            m_typeVramBytes[typeIdx].fetch_add(bytes, std::memory_order_relaxed);
            const uint64_t newActive = m_activeVramBytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
            updatePeakVramAtomic(newActive);
        }

        void GpuMemoryManager::recordVramFree(GpuResourceType type, uint64_t bytes) noexcept
        {
            const size_t typeIdx = static_cast<size_t>(type);
            if (typeIdx >= static_cast<size_t>(GpuResourceType::Count)) {
                return;
            }

            uint64_t curActive = m_activeVramBytes.load(std::memory_order_relaxed);
            while (curActive > 0) {
                uint64_t nextActive = (curActive >= bytes) ? (curActive - bytes) : 0;
                if (m_activeVramBytes.compare_exchange_weak(curActive, nextActive, std::memory_order_relaxed)) {
                    break;
                }
            }

            auto& typeBytes = m_typeVramBytes[typeIdx];
            uint64_t curType = typeBytes.load(std::memory_order_relaxed);
            while (curType > 0) {
                uint64_t nextType = (curType >= bytes) ? (curType - bytes) : 0;
                if (typeBytes.compare_exchange_weak(curType, nextType, std::memory_order_relaxed)) {
                    break;
                }
            }
        }

        void GpuMemoryManager::updatePeakVramAtomic(uint64_t current) noexcept
        {
            uint64_t prev = m_peakVramBytes.load(std::memory_order_relaxed);
            while (current > prev && !m_peakVramBytes.compare_exchange_weak(prev, current, std::memory_order_relaxed)) {}
        }

        uint64_t GpuMemoryManager::activeVramBytes() const noexcept
        {
            return m_activeVramBytes.load(std::memory_order_relaxed);
        }

        uint64_t GpuMemoryManager::peakVramBytes() const noexcept
        {
            return m_peakVramBytes.load(std::memory_order_relaxed);
        }

        uint64_t GpuMemoryManager::typeVramBytes(GpuResourceType type) const noexcept
        {
            const size_t typeIdx = static_cast<size_t>(type);
            if (typeIdx >= static_cast<size_t>(GpuResourceType::Count)) {
                return 0;
            }
            return m_typeVramBytes[typeIdx].load(std::memory_order_relaxed);
        }

        void* GpuMemoryManager::acquireStagingBuffer(size_t size)
        {
            const size_t alignedSize = alignStagingSize(std::max(size, kMinStagingBlockSize));

            // 单渲染宿主（RenderHost）独占访问，无需锁保护
            for (auto& item : m_stagingPool) {
                if (!item.inUse && item.capacity >= size) {
                    item.inUse = true;
                    return item.cpuMemory;
                }
            }

            // 未命中则创建新的暂存块
            void* mem = ::operator new(alignedSize, std::align_val_t{kStagingAlignment});
            m_stagingPool.push_back(StagingBufferItem{mem, alignedSize, true});
            return mem;
        }

        void GpuMemoryManager::releaseStagingBuffer(void* buffer) noexcept
        {
            if (!buffer) {
                return;
            }
            for (auto& item : m_stagingPool) {
                if (item.cpuMemory == buffer) {
                    item.inUse = false;
                    return;
                }
            }
            // 若不在池中直接销毁
            ::operator delete(buffer, std::align_val_t{kStagingAlignment});
        }

        void GpuMemoryManager::deferRelease(void* resource, std::function<void(void*)> deleter, uint64_t currentFrameIndex)
        {
            if (!resource || !deleter) {
                return;
            }
            auto* item = FCT_NEW(DeferredReleaseItem, DeferredReleaseItem{resource, std::move(deleter), currentFrameIndex});
            m_deferredQueue.push(item);
            m_pendingDeferredCount.fetch_add(1, std::memory_order_relaxed);
        }

        void GpuMemoryManager::deferReleaseResource(
            const void* handle,
            std::function<void(const void*)> deleter,
            uint64_t currentFrameIndex)
        {
            if (!handle || !deleter) {
                return;
            }

            uint32_t bindlessSlot = UINT32_MAX;
            uint64_t vramBytes = 0;

            // Step 1: 标记逻辑退役为 Released 状态（保留槽位与记录，供 resolveHandle 安全回退）
            if (auto* gpuTable = GpuResourceTable::InstancePtr()) {
                if (const auto* rec = gpuTable->getRecord(handle)) {
                    bindlessSlot = rec->bindlessSlot;
                    vramBytes = rec->vramBytes;
                }
                gpuTable->markResourceReleased(handle);
            }

            // Step 3: 多生产者推入无锁待删队列
            auto* item = FCT_NEW(PendingGpuDeletion, PendingGpuDeletion{
                handle,
                bindlessSlot,
                vramBytes,
                std::move(deleter),
                currentFrameIndex
            });
            m_deletionQueue.push(item);
            m_pendingDeferredCount.fetch_add(1, std::memory_order_relaxed);
        }

        void GpuMemoryManager::processFrameDeletions(uint64_t completedFrameIndex)
        {
            // 单消费者（RenderHost）：将无锁队列中的待退役项归入帧桶
            PendingGpuDeletion* pending = nullptr;
            while (m_deletionQueue.pop(pending)) {
                if (pending) {
                    const uint32_t bucket = static_cast<uint32_t>(pending->retiredFrame % kMaxFramesInFlight);
                    m_frameDeletions[bucket].push_back(std::move(*pending));
                    FCT_DELETE(pending);
                }
            }

            // Step 4: 执行在途帧到达后的物理销毁与槽位归还
            std::vector<PendingGpuDeletion> readyToDelete;
            for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                auto& bucket = m_frameDeletions[f];
                auto it = bucket.begin();
                while (it != bucket.end()) {
                    if (it->retiredFrame + kMaxFramesInFlight <= completedFrameIndex) {
                        readyToDelete.push_back(std::move(*it));
                        it = bucket.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            auto* gpuTable = GpuResourceTable::InstancePtr();
            for (auto& item : readyToDelete) {
                if (item.deleter && item.handle) {
                    item.deleter(item.handle);
                }
                if (gpuTable && item.handle) {
                    gpuTable->finalizeUnregister(item.handle);
                }
                if (m_pendingDeferredCount.load(std::memory_order_relaxed) > 0) {
                    m_pendingDeferredCount.fetch_sub(1, std::memory_order_relaxed);
                }
            }

            // 同步刷新原有通用 DeferredQueue
            flushDeferredReleases(completedFrameIndex);
        }

        void GpuMemoryManager::flushDeferredReleases(uint64_t completedFrameIndex)
        {
            DeferredReleaseItem* pendingItem = nullptr;
            while (m_deferredQueue.pop(pendingItem)) {
                if (pendingItem) {
                    m_localDeferredItems.push_back(std::move(*pendingItem));
                    FCT_DELETE(pendingItem);
                }
            }

            const uint32_t maxFrames = m_maxFramesInFlight.load(std::memory_order_relaxed);
            auto it = m_localDeferredItems.begin();
            while (it != m_localDeferredItems.end()) {
                if (it->retiredFrame + maxFrames <= completedFrameIndex) {
                    if (it->deleter && it->resource) {
                        it->deleter(it->resource);
                    }
                    if (m_pendingDeferredCount.load(std::memory_order_relaxed) > 0) {
                        m_pendingDeferredCount.fetch_sub(1, std::memory_order_relaxed);
                    }
                    it = m_localDeferredItems.erase(it);
                } else {
                    ++it;
                }
            }
        }

        void GpuMemoryManager::flushAllDeferredReleases()
        {
            DeferredReleaseItem* pendingItem = nullptr;
            while (m_deferredQueue.pop(pendingItem)) {
                if (pendingItem) {
                    m_localDeferredItems.push_back(std::move(*pendingItem));
                    FCT_DELETE(pendingItem);
                }
            }

            for (auto& item : m_localDeferredItems) {
                if (item.deleter && item.resource) {
                    item.deleter(item.resource);
                }
            }
            m_localDeferredItems.clear();

            PendingGpuDeletion* pendingDel = nullptr;
            while (m_deletionQueue.pop(pendingDel)) {
                if (pendingDel) {
                    const uint32_t bucket = static_cast<uint32_t>(pendingDel->retiredFrame % kMaxFramesInFlight);
                    m_frameDeletions[bucket].push_back(std::move(*pendingDel));
                    FCT_DELETE(pendingDel);
                }
            }

            std::vector<PendingGpuDeletion> allDeletions;
            for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                allDeletions.insert(
                    allDeletions.end(),
                    std::make_move_iterator(m_frameDeletions[f].begin()),
                    std::make_move_iterator(m_frameDeletions[f].end()));
                m_frameDeletions[f].clear();
            }

            auto* gpuTable = GpuResourceTable::InstancePtr();
            for (auto& item : allDeletions) {
                if (item.deleter && item.handle) {
                    item.deleter(item.handle);
                }
                if (gpuTable && item.handle) {
                    gpuTable->finalizeUnregister(item.handle);
                }
            }
            m_pendingDeferredCount.store(0, std::memory_order_relaxed);
        }

        size_t GpuMemoryManager::pendingDeferredCount() const noexcept
        {
            return m_pendingDeferredCount.load(std::memory_order_relaxed);
        }

        std::string GpuMemoryManager::dumpVramReport() const
        {
            std::ostringstream oss;
            dumpVramReport(oss);
            return oss.str();
        }

        void GpuMemoryManager::dumpVramReport(std::ostream& out) const
        {
            out << "==================== MQEngine GPU VRAM Report ====================\n";
            const uint64_t active = activeVramBytes();
            const uint64_t peak = peakVramBytes();
            out << "Active VRAM : " << active << " bytes ("
                << std::fixed << std::setprecision(2) << (static_cast<double>(active) / (1024.0 * 1024.0)) << " MB)\n";
            out << "Peak VRAM   : " << peak << " bytes ("
                << std::fixed << std::setprecision(2) << (static_cast<double>(peak) / (1024.0 * 1024.0)) << " MB)\n";
            out << "-------------------------- Type Breakdown -------------------------\n";

            for (size_t i = 0; i < static_cast<size_t>(GpuResourceType::Count); ++i) {
                const auto type = static_cast<GpuResourceType>(i);
                const uint64_t bytes = typeVramBytes(type);
                out << std::setw(14) << std::left << getGpuResourceTypeName(type)
                    << " : " << std::setw(10) << std::right << bytes << " bytes ("
                    << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB)\n";
            }
            out << "==================================================================\n";
        }

    } // namespace Resource
} // namespace MQEngine
