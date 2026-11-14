#include <MQEngine/resource/core/MemoryTracker.h>
#include <cassert>
#include <iomanip>
#include <sstream>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            constexpr size_t kSlotTableSize = 65536;
            constexpr size_t kSlotMask = kSlotTableSize - 1;
            constexpr size_t kMaxProbes = 64;

            inline size_t hashPointer(const void* ptr) noexcept
            {
                const auto val = reinterpret_cast<uintptr_t>(ptr);
                return ((val >> 4) ^ (val >> 16)) & kSlotMask;
            }
        } // namespace

        struct MemoryAllocationSlot
        {
            std::atomic<void*> pointer{nullptr};
            std::atomic<size_t> byteSize{0};
            std::atomic<uint8_t> tag{0};

            MemoryAllocationSlot() = default;
        };

        struct MemoryAllocationSlotTable
        {
            std::array<MemoryAllocationSlot, kSlotTableSize> slots;

            MemoryAllocationSlotTable() = default;
        };

        MemoryTracker::MemoryTracker()
            : m_slotTable(makeUnique<MemoryAllocationSlotTable>())
        {
            for (auto& v : m_tagActiveBytes) {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : m_tagPeakBytes) {
                v.store(0, std::memory_order_relaxed);
            }
        }

        MemoryTracker::~MemoryTracker()
        {
            resetStats();
        }

        void MemoryTracker::recordAllocation(void* ptr, size_t byteSize, const std::string& describe, MemoryTag tag)
        {
            if (!ptr) {
                return;
            }

            const size_t tagIdx = static_cast<size_t>(tag);
            assert(tagIdx < static_cast<size_t>(MemoryTag::Count) && "MemoryTag 索引越界");

            if (m_slotTable) {
                const size_t startIdx = hashPointer(ptr);

                for (size_t i = 0; i < kMaxProbes; ++i) {
                    auto& slot = m_slotTable->slots[(startIdx + i) & kSlotMask];
                    void* expected = nullptr;
                    if (slot.pointer.compare_exchange_strong(expected, ptr, std::memory_order_acq_rel)) {
                        slot.byteSize.store(byteSize, std::memory_order_release);
                        slot.tag.store(static_cast<uint8_t>(tag), std::memory_order_release);
                        m_activeAllocations.fetch_add(1, std::memory_order_relaxed);
                        break;
                    } else if (expected == ptr) {
                        const size_t oldSize = slot.byteSize.exchange(byteSize, std::memory_order_acq_rel);
                        const uint8_t oldTag = slot.tag.exchange(static_cast<uint8_t>(tag), std::memory_order_acq_rel);
                        if (oldTag < static_cast<size_t>(MemoryTag::Count)) {
                            auto& oldTagBytes = m_tagActiveBytes[oldTag];
                            size_t cur = oldTagBytes.load(std::memory_order_relaxed);
                            while (cur >= oldSize && !oldTagBytes.compare_exchange_weak(cur, cur - oldSize, std::memory_order_relaxed)) {}
                        }
                        size_t curActive = m_activeBytes.load(std::memory_order_relaxed);
                        while (curActive >= oldSize && !m_activeBytes.compare_exchange_weak(curActive, curActive - oldSize, std::memory_order_relaxed)) {}
                        break;
                    }
                }
            }

            const size_t newActive = m_activeBytes.fetch_add(byteSize, std::memory_order_relaxed) + byteSize;
            m_tagActiveBytes[tagIdx].fetch_add(byteSize, std::memory_order_relaxed);
            m_totalAllocations.fetch_add(1, std::memory_order_relaxed);
            updatePeakAtomic(newActive, tagIdx);

            // 同步上报 FCT 底层追踪器
            FCT::MemoryTracker::instance().recordAllocation(ptr, byteSize, describe, tag);
        }

        bool MemoryTracker::recordFree(void* ptr)
        {
            if (!ptr) {
                return true;
            }

            bool found = false;
            size_t freedBytes = 0;
            size_t tagIdx = static_cast<size_t>(MemoryTag::General);

            if (m_slotTable) {
                const size_t startIdx = hashPointer(ptr);
                for (size_t i = 0; i < kMaxProbes; ++i) {
                    auto& slot = m_slotTable->slots[(startIdx + i) & kSlotMask];
                    if (slot.pointer.load(std::memory_order_relaxed) == ptr) {
                        void* expected = ptr;
                        if (slot.pointer.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
                            freedBytes = slot.byteSize.exchange(0, std::memory_order_acq_rel);
                            tagIdx = slot.tag.load(std::memory_order_relaxed);
                            found = true;
                            m_activeAllocations.fetch_sub(1, std::memory_order_relaxed);
                            break;
                        }
                    }
                }
            }

            if (found) {
                size_t curActive = m_activeBytes.load(std::memory_order_relaxed);
                while (curActive > 0) {
                    size_t next = (curActive >= freedBytes) ? (curActive - freedBytes) : 0;
                    if (m_activeBytes.compare_exchange_weak(curActive, next, std::memory_order_relaxed)) break;
                }

                if (tagIdx < static_cast<size_t>(MemoryTag::Count)) {
                    auto& tagBytes = m_tagActiveBytes[tagIdx];
                    size_t curTag = tagBytes.load(std::memory_order_relaxed);
                    while (curTag > 0) {
                        size_t next = (curTag >= freedBytes) ? (curTag - freedBytes) : 0;
                        if (tagBytes.compare_exchange_weak(curTag, next, std::memory_order_relaxed)) break;
                    }
                }
                m_totalFrees.fetch_add(1, std::memory_order_relaxed);
            } else {
                m_untrackedFrees.fetch_add(1, std::memory_order_relaxed);
            }

            FCT::MemoryTracker::instance().recordFree(ptr);
            return found;
        }

        void MemoryTracker::recordRawBytesAllocated(size_t byteSize, MemoryTag tag) noexcept
        {
            const size_t tagIdx = static_cast<size_t>(tag);
            if (tagIdx >= static_cast<size_t>(MemoryTag::Count)) {
                return;
            }

            const size_t newActive = m_activeBytes.fetch_add(byteSize, std::memory_order_relaxed) + byteSize;
            m_tagActiveBytes[tagIdx].fetch_add(byteSize, std::memory_order_relaxed);
            m_totalAllocations.fetch_add(1, std::memory_order_relaxed);
            updatePeakAtomic(newActive, tagIdx);

            FCT::MemoryTracker::instance().recordRawBytesAllocated(byteSize, tag);
        }

        void MemoryTracker::recordRawBytesFreed(size_t byteSize, MemoryTag tag) noexcept
        {
            const size_t tagIdx = static_cast<size_t>(tag);
            if (tagIdx >= static_cast<size_t>(MemoryTag::Count)) {
                return;
            }

            size_t curActive = m_activeBytes.load(std::memory_order_relaxed);
            while (curActive > 0) {
                size_t next = (curActive >= byteSize) ? (curActive - byteSize) : 0;
                if (m_activeBytes.compare_exchange_weak(curActive, next, std::memory_order_relaxed)) break;
            }

            auto& tagBytes = m_tagActiveBytes[tagIdx];
            size_t curTag = tagBytes.load(std::memory_order_relaxed);
            while (curTag > 0) {
                size_t next = (curTag >= byteSize) ? (curTag - byteSize) : 0;
                if (tagBytes.compare_exchange_weak(curTag, next, std::memory_order_relaxed)) break;
            }

            m_totalFrees.fetch_add(1, std::memory_order_relaxed);
            FCT::MemoryTracker::instance().recordRawBytesFreed(byteSize, tag);
        }

        MemoryStatsSnapshot MemoryTracker::snapshotStats() const
        {
            MemoryStatsSnapshot snap;
            snap.activeBytes = m_activeBytes.load(std::memory_order_relaxed);
            snap.peakBytes = m_peakBytes.load(std::memory_order_relaxed);
            snap.activeAllocations = m_activeAllocations.load(std::memory_order_relaxed);
            snap.totalAllocations = m_totalAllocations.load(std::memory_order_relaxed);
            snap.totalFrees = m_totalFrees.load(std::memory_order_relaxed);
            snap.untrackedFrees = m_untrackedFrees.load(std::memory_order_relaxed);
            for (size_t i = 0; i < static_cast<size_t>(MemoryTag::Count); ++i) {
                snap.tagActiveBytes[i] = m_tagActiveBytes[i].load(std::memory_order_relaxed);
                snap.tagPeakBytes[i] = m_tagPeakBytes[i].load(std::memory_order_relaxed);
            }
            return snap;
        }

        size_t MemoryTracker::activeBytes() const noexcept
        {
            return m_activeBytes.load(std::memory_order_relaxed);
        }

        size_t MemoryTracker::peakBytes() const noexcept
        {
            return m_peakBytes.load(std::memory_order_relaxed);
        }

        size_t MemoryTracker::activeAllocationCount() const noexcept
        {
            return m_activeAllocations.load(std::memory_order_relaxed);
        }

        size_t MemoryTracker::tagActiveBytes(MemoryTag tag) const noexcept
        {
            const size_t tagIdx = static_cast<size_t>(tag);
            if (tagIdx >= static_cast<size_t>(MemoryTag::Count)) {
                return 0;
            }
            return m_tagActiveBytes[tagIdx].load(std::memory_order_relaxed);
        }

        size_t MemoryTracker::tagPeakBytes(MemoryTag tag) const noexcept
        {
            const size_t tagIdx = static_cast<size_t>(tag);
            if (tagIdx >= static_cast<size_t>(MemoryTag::Count)) {
                return 0;
            }
            return m_tagPeakBytes[tagIdx].load(std::memory_order_relaxed);
        }

        std::string MemoryTracker::dumpReport() const
        {
            std::ostringstream oss;
            dumpReport(oss);
            return oss.str();
        }

        void MemoryTracker::dumpReport(std::ostream& out) const
        {
            const auto snap = snapshotStats();
            out << "==================== MQEngine Memory Tracker Report ====================\n";
            out << "Active Allocations : " << snap.activeAllocations << "\n";
            out << "Active Bytes       : " << snap.activeBytes << " bytes ("
                << std::fixed << std::setprecision(2) << (static_cast<double>(snap.activeBytes) / (1024.0 * 1024.0)) << " MB)\n";
            out << "Peak Bytes         : " << snap.peakBytes << " bytes ("
                << std::fixed << std::setprecision(2) << (static_cast<double>(snap.peakBytes) / (1024.0 * 1024.0)) << " MB)\n";
            out << "Total Allocations  : " << snap.totalAllocations << "\n";
            out << "Total Frees        : " << snap.totalFrees << "\n";
            out << "Untracked Frees    : " << snap.untrackedFrees << "\n";
            out << "--------------------------- Breakdown By Tag ---------------------------\n";

            for (size_t i = 0; i < static_cast<size_t>(MemoryTag::Count); ++i) {
                const auto tag = static_cast<MemoryTag>(i);
                out << std::setw(12) << std::left << FCT::getMemoryTagName(tag)
                    << " | Active: " << std::setw(10) << std::right << snap.tagActiveBytes[i] << " B"
                    << " | Peak: " << std::setw(10) << std::right << snap.tagPeakBytes[i] << " B\n";
            }
            out << "========================================================================\n";
        }

        void MemoryTracker::resetStats() noexcept
        {
            if (m_slotTable) {
                for (auto& slot : m_slotTable->slots) {
                    slot.pointer.store(nullptr, std::memory_order_relaxed);
                    slot.byteSize.store(0, std::memory_order_relaxed);
                    slot.tag.store(0, std::memory_order_relaxed);
                }
            }
            m_activeBytes.store(0, std::memory_order_relaxed);
            m_peakBytes.store(0, std::memory_order_relaxed);
            m_activeAllocations.store(0, std::memory_order_relaxed);
            m_totalAllocations.store(0, std::memory_order_relaxed);
            m_totalFrees.store(0, std::memory_order_relaxed);
            m_untrackedFrees.store(0, std::memory_order_relaxed);
            for (auto& v : m_tagActiveBytes) {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : m_tagPeakBytes) {
                v.store(0, std::memory_order_relaxed);
            }
        }

        void MemoryTracker::updatePeakAtomic(size_t currentBytes, size_t tagIndex) noexcept
        {
            size_t prevPeak = m_peakBytes.load(std::memory_order_relaxed);
            while (currentBytes > prevPeak && !m_peakBytes.compare_exchange_weak(prevPeak, currentBytes, std::memory_order_relaxed)) {}

            if (tagIndex < static_cast<size_t>(MemoryTag::Count)) {
                const size_t currentTagBytes = m_tagActiveBytes[tagIndex].load(std::memory_order_relaxed);
                auto& tagPeak = m_tagPeakBytes[tagIndex];
                size_t prevTagPeak = tagPeak.load(std::memory_order_relaxed);
                while (currentTagBytes > prevTagPeak && !tagPeak.compare_exchange_weak(prevTagPeak, currentTagBytes, std::memory_order_relaxed)) {}
            }
        }

    } // namespace Resource
} // namespace MQEngine
