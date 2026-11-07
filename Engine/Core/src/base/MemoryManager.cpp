#include "MemoryManager.h"

namespace MQEngine {

    MemoryManager::MemoryManager()
        : m_logicFrameArena(kDefaultLogicArenaBytes)
        , m_renderFrameArena(kDefaultRenderArenaBytes, MemoryTag::Arena)
        , m_smallPacketPool(kSmallPacketBlockBytes, 256, MemoryTag::Network)
        , m_largePacketPool(kLargePacketBlockBytes, 32, MemoryTag::Network) {
        MemoryTracker::setSharedInstance(&m_tracker);
    }

    MemoryManager::~MemoryManager() {
        if (&MemoryTracker::instance() == &m_tracker) {
            MemoryTracker::setSharedInstance(nullptr);
        }
    }

    void MemoryManager::beginLogicFrame() noexcept {
        m_logicFrameArena.nextFrame();
        const auto stats = m_tracker.snapshotStats();
        TracyPlot("MemoryActive (KB)", static_cast<double>(stats.activeBytes) / 1024.0);
        TracyPlot("LogicArenaPeak (KB)", static_cast<double>(m_logicFrameArena.previousArena().usedBytes()) / 1024.0);
    }

    void MemoryManager::beginRenderFrame() noexcept {
        m_renderFrameArena.reset();
    }

    ScopedPoolBuffer MemoryManager::acquirePacketBuffer(size_t minBytes) {
        if (minBytes <= m_smallPacketPool.blockSize()) {
            return ScopedPoolBuffer(&m_smallPacketPool, m_smallPacketPool.acquire(), m_smallPacketPool.blockSize());
        }
        if (minBytes <= m_largePacketPool.blockSize()) {
            return ScopedPoolBuffer(&m_largePacketPool, m_largePacketPool.acquire(), m_largePacketPool.blockSize());
        }
        return ScopedPoolBuffer{};
    }

    uint64_t MemoryManager::registerTrimCallback(const std::string& subsystemName, TrimCallback callback) {
        if (!callback) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_trimMutex);
        const uint64_t id = m_nextTrimId++;
        m_trimCallbacks.push_back(TrimEntry{id, subsystemName, std::move(callback)});
        return id;
    }

    void MemoryManager::unregisterTrimCallback(uint64_t handle) {
        if (handle == 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_trimMutex);
        m_trimCallbacks.erase(
            std::remove_if(m_trimCallbacks.begin(), m_trimCallbacks.end(),
                [handle](const TrimEntry& entry) { return entry.id == handle; }),
            m_trimCallbacks.end());
    }

    MemoryTag MemoryManager::parseMemoryTag(const std::string& tagName) noexcept {
        if (tagName == "RHI") return MemoryTag::RHI;
        if (tagName == "Asset") return MemoryTag::Asset;
        if (tagName == "Animation") return MemoryTag::Animation;
        if (tagName == "Network") return MemoryTag::Network;
        if (tagName == "Script") return MemoryTag::Script;
        if (tagName == "ECS") return MemoryTag::ECS;
        if (tagName == "Arena") return MemoryTag::Arena;
        if (tagName == "Pool") return MemoryTag::Pool;
        return MemoryTag::General;
    }

    uint64_t MemoryManager::getActiveBytes() const {
        return static_cast<uint64_t>(m_tracker.snapshotStats().activeBytes);
    }

    uint64_t MemoryManager::getPeakBytes() const {
        return static_cast<uint64_t>(m_tracker.snapshotStats().peakBytes);
    }

    uint64_t MemoryManager::getActiveAllocationCount() const {
        return static_cast<uint64_t>(m_tracker.activeAllocationCount());
    }

    uint64_t MemoryManager::getTotalAllocationCount() const {
        return m_tracker.snapshotStats().totalAllocations;
    }

    uint64_t MemoryManager::getTagActiveBytes(const std::string& tagName) const {
        const auto stats = m_tracker.snapshotStats();
        const size_t idx = static_cast<size_t>(parseMemoryTag(tagName));
        return idx < stats.tagActiveBytes.size() ? static_cast<uint64_t>(stats.tagActiveBytes[idx]) : 0;
    }

    uint64_t MemoryManager::getLogicFrameArenaUsedBytes() const {
        return static_cast<uint64_t>(m_logicFrameArena.currentArena().usedBytes());
    }

    uint64_t MemoryManager::getLogicFrameArenaPeakBytes() const {
        return static_cast<uint64_t>(std::max(
            m_logicFrameArena.currentArena().peakUsedBytes(),
            m_logicFrameArena.previousArena().peakUsedBytes()));
    }

    uint64_t MemoryManager::getPacketPoolCapacityBytes() const {
        return static_cast<uint64_t>(
            m_smallPacketPool.totalCapacityBytes() + m_largePacketPool.totalCapacityBytes());
    }

    uint64_t MemoryManager::trimCaches() {
        std::vector<TrimEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_trimMutex);
            snapshot = m_trimCallbacks;
        }
        uint64_t totalReclaimed = 0;
        for (const auto& entry : snapshot) {
            if (entry.callback) {
                try {
                    totalReclaimed += static_cast<uint64_t>(entry.callback());
                } catch (const std::exception& e) {
                    spdlog::warn("MemoryManager 执行缓存回收 [{}] 异常: {}", entry.name, e.what());
                }
            }
        }
        return totalReclaimed;
    }

    std::string MemoryManager::dumpMemoryReport() const {
        std::ostringstream oss;
        m_tracker.dumpReport(oss);
        oss << "LogicFrameArena 当前帧已用: " << m_logicFrameArena.currentArena().usedBytes()
            << " B, 历史单帧峰值: " << getLogicFrameArenaPeakBytes() << " B" << std::endl;
        oss << "RenderFrameArena 已用: " << m_renderFrameArena.usedBytes()
            << " B, 历史峰值: " << m_renderFrameArena.peakUsedBytes() << " B" << std::endl;
        oss << "SmallPacketPool 在用块: " << m_smallPacketPool.allocatedBlockCount()
            << " / 空闲块: " << m_smallPacketPool.freeBlockCount() << std::endl;
        oss << "LargePacketPool 在用块: " << m_largePacketPool.allocatedBlockCount()
            << " / 空闲块: " << m_largePacketPool.freeBlockCount() << std::endl;
        return oss.str();
    }

} // namespace MQEngine
