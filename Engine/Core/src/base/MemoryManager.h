#ifndef ENGINE_MEMORY_MANAGER_H
#define ENGINE_MEMORY_MANAGER_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "./CoreEngineManager.h"

namespace MQEngine {

    /**
     * @brief 引擎级内存管理器，统一托管跨模块 MemoryTracker、双缓冲逻辑帧 Arena、渲染帧 Arena 与网络定长块内存池
     */
    class ENGINE_API MemoryManager : public CoreEngineManager<MemoryManager> {
    public:
        static constexpr size_t kDefaultLogicArenaBytes = 4 * 1024 * 1024;
        static constexpr size_t kDefaultRenderArenaBytes = 2 * 1024 * 1024;
        static constexpr size_t kSmallPacketBlockBytes = 2048;
        static constexpr size_t kLargePacketBlockBytes = 65536;

        using TrimCallback = std::function<size_t()>;

        MemoryManager();
        ~MemoryManager();

        MemoryManager(const MemoryManager&) = delete;
        MemoryManager& operator=(const MemoryManager&) = delete;

        void registerInfo() {}

        void beginLogicFrame() noexcept;
        void beginRenderFrame() noexcept;

        MemoryTracker& tracker() noexcept { return m_tracker; }
        const MemoryTracker& tracker() const noexcept { return m_tracker; }

        DoubleBufferedFrameArena& logicFrameArena() noexcept { return m_logicFrameArena; }
        const DoubleBufferedFrameArena& logicFrameArena() const noexcept { return m_logicFrameArena; }

        LinearArena& currentLogicArena() noexcept { return m_logicFrameArena.currentArena(); }
        LinearArena& renderFrameArena() noexcept { return m_renderFrameArena; }

        ScopedPoolBuffer acquirePacketBuffer(size_t minBytes);
        BlockMemoryPool& smallPacketPool() noexcept { return m_smallPacketPool; }
        BlockMemoryPool& largePacketPool() noexcept { return m_largePacketPool; }

        uint64_t registerTrimCallback(const std::string& subsystemName, TrimCallback callback);
        void unregisterTrimCallback(uint64_t handle);

        uint64_t getActiveBytes() const;
        uint64_t getPeakBytes() const;
        uint64_t getActiveAllocationCount() const;
        uint64_t getTotalAllocationCount() const;
        uint64_t getTagActiveBytes(const std::string& tagName) const;
        uint64_t getLogicFrameArenaUsedBytes() const;
        uint64_t getLogicFrameArenaPeakBytes() const;
        uint64_t getPacketPoolCapacityBytes() const;
        uint64_t trimCaches();
        std::string dumpMemoryReport() const;

    private:
        struct TrimEntry {
            uint64_t id = 0;
            std::string name;
            TrimCallback callback;
        };

        static MemoryTag parseMemoryTag(const std::string& tagName) noexcept;

        MemoryTracker m_tracker;
        DoubleBufferedFrameArena m_logicFrameArena;
        LinearArena m_renderFrameArena;
        BlockMemoryPool m_smallPacketPool;
        BlockMemoryPool m_largePacketPool;

        mutable std::mutex m_trimMutex;
        uint64_t m_nextTrimId = 1;
        std::vector<TrimEntry> m_trimCallbacks;
    };

} // namespace MQEngine

#endif // ENGINE_MEMORY_MANAGER_H
