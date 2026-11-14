#ifndef MQENGINE_RESOURCE_CORE_MEMORYTRACKER_H
#define MQENGINE_RESOURCE_CORE_MEMORYTRACKER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 内存分配追踪结构体
         */
        struct MemoryAllocationRecord
        {
            void* pointer = nullptr;
            std::string describe;
            size_t byteSize = 0;
            MemoryTag tag = MemoryTag::General;
        };

        struct MemoryAllocationSlotTable;

        /**
         * @brief CPU 内存分配全局追踪管理器：负责跨子系统内存指标统计、标签化开销剖析与泄漏诊断（无锁架构）
         */
        class ENGINE_API MemoryTracker : public CoreEngineManager<MemoryTracker>
        {
        public:
            MemoryTracker();
            ~MemoryTracker();

            MemoryTracker(const MemoryTracker&) = delete;
            MemoryTracker& operator=(const MemoryTracker&) = delete;

            void recordAllocation(void* ptr, size_t byteSize, const std::string& describe, MemoryTag tag = MemoryTag::General);
            bool recordFree(void* ptr);

            void recordRawBytesAllocated(size_t byteSize, MemoryTag tag = MemoryTag::General) noexcept;
            void recordRawBytesFreed(size_t byteSize, MemoryTag tag = MemoryTag::General) noexcept;

            MemoryStatsSnapshot snapshotStats() const;
            size_t activeBytes() const noexcept;
            size_t peakBytes() const noexcept;
            size_t activeAllocationCount() const noexcept;
            size_t tagActiveBytes(MemoryTag tag) const noexcept;
            size_t tagPeakBytes(MemoryTag tag) const noexcept;

            std::string dumpReport() const;
            void dumpReport(std::ostream& out) const;
            void resetStats() noexcept;

        private:
            void updatePeakAtomic(size_t currentBytes, size_t tagIndex) noexcept;

            UniquePtr<MemoryAllocationSlotTable> m_slotTable;
            std::atomic<size_t> m_activeBytes{0};
            std::atomic<size_t> m_peakBytes{0};
            std::atomic<size_t> m_activeAllocations{0};
            std::atomic<uint64_t> m_totalAllocations{0};
            std::atomic<uint64_t> m_totalFrees{0};
            std::atomic<uint64_t> m_untrackedFrees{0};
            std::array<std::atomic<size_t>, static_cast<size_t>(MemoryTag::Count)> m_tagActiveBytes;
            std::array<std::atomic<size_t>, static_cast<size_t>(MemoryTag::Count)> m_tagPeakBytes;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_MEMORYTRACKER_H
