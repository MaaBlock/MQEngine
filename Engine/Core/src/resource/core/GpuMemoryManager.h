#ifndef MQENGINE_RESOURCE_CORE_GPUMEMORYMANAGER_H
#define MQENGINE_RESOURCE_CORE_GPUMEMORYMANAGER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>
#include <boost/lockfree/queue.hpp>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief GPU 资源类型枚举
         */
        enum class GpuResourceType : uint8_t
        {
            Texture = 0,
            Buffer = 1,
            RenderTarget = 2,
            Shader = 3,
            Sampler = 4,
            Pipeline = 5,
            Custom = 6,
            Count = 7
        };

        inline constexpr const char* getGpuResourceTypeName(GpuResourceType type) noexcept
        {
            switch (type) {
                case GpuResourceType::Texture:      return "Texture";
                case GpuResourceType::Buffer:       return "Buffer";
                case GpuResourceType::RenderTarget: return "RenderTarget";
                case GpuResourceType::Shader:       return "Shader";
                case GpuResourceType::Sampler:      return "Sampler";
                case GpuResourceType::Pipeline:     return "Pipeline";
                case GpuResourceType::Custom:       return "Custom";
                default:                            return "Unknown";
            }
        }

        /**
         * @brief GPU 资源生命周期状态
         */
        enum class GpuResourceState : uint8_t
        {
            Empty = 0,
            Loading = 1,
            Ready = 2,
            Released = 3,
            Error = 4
        };

        /**
         * @brief GPU 暂存缓冲区项（用于 CPU 到 GPU 异步上传）
         */
        struct StagingBufferItem
        {
            void* cpuMemory = nullptr;
            size_t capacity = 0;
            bool inUse = false;
        };

        /**
         * @brief 延迟释放队列项：挂入当前逻辑帧，待 GPU 完成 Fence 信号后安全物理销毁
         */
        struct DeferredReleaseItem
        {
            void* resource = nullptr;
            std::function<void(void*)> deleter;
            uint64_t retiredFrame = 0;
        };

        /**
         * @brief GPU 资源延迟销毁项（严格以原生资源指针为 Primary Key，关联 Bindless 槽位与物理 Deleter）
         */
        struct PendingGpuDeletion
        {
            const void* handle = nullptr;
            uint32_t bindlessSlot = UINT32_MAX;
            uint64_t vramBytes = 0;
            std::function<void(const void*)> deleter;
            uint64_t retiredFrame = 0;
        };

        /**
         * @brief GPU 显存管理器：负责 VRAM 占用追踪、Staging 上传池与跨帧延迟释放队列生命周期管控（完全无锁架构）
         */
        class ENGINE_API GpuMemoryManager : public CoreEngineManager<GpuMemoryManager>
        {
        public:
            static constexpr uint32_t kMaxFramesInFlight = 3;

            GpuMemoryManager();
            ~GpuMemoryManager();

            GpuMemoryManager(const GpuMemoryManager&) = delete;
            GpuMemoryManager& operator=(const GpuMemoryManager&) = delete;

            // VRAM 统计追踪（无锁原子操作）
            void recordVramAlloc(GpuResourceType type, uint64_t bytes) noexcept;
            void recordVramFree(GpuResourceType type, uint64_t bytes) noexcept;

            uint64_t activeVramBytes() const noexcept;
            uint64_t peakVramBytes() const noexcept;
            uint64_t typeVramBytes(GpuResourceType type) const noexcept;

            // Staging 暂存缓冲区池（单渲染宿主独占访问，无需锁）
            void* acquireStagingBuffer(size_t size);
            void releaseStagingBuffer(void* buffer) noexcept;

            // 跨帧延迟释放流水线（多生产者无锁推入，渲染宿主单向消费）
            void deferRelease(void* resource, std::function<void(void*)> deleter, uint64_t currentFrameIndex);

            // 生产级 4 步延迟释放流水线：Step 3 挂入延迟释放桶与 Step 4 GPU 完成后物理销毁并归还槽位
            void deferReleaseResource(
                const void* handle,
                std::function<void(const void*)> deleter,
                uint64_t currentFrameIndex);

            void processFrameDeletions(uint64_t completedFrameIndex);

            template <typename T>
            void deferDelete(T* resource, uint64_t currentFrameIndex)
            {
                if (!resource) {
                    return;
                }
                deferRelease(resource, [](void* ptr) {
                    delete static_cast<T*>(ptr);
                }, currentFrameIndex);
            }

            void flushDeferredReleases(uint64_t completedFrameIndex);
            void flushAllDeferredReleases();

            size_t pendingDeferredCount() const noexcept;
            uint32_t maxFramesInFlight() const noexcept { return m_maxFramesInFlight.load(std::memory_order_relaxed); }
            void setMaxFramesInFlight(uint32_t frames) noexcept { m_maxFramesInFlight.store(frames, std::memory_order_relaxed); }

            std::string dumpVramReport() const;
            void dumpVramReport(std::ostream& out) const;

        private:
            void updatePeakVramAtomic(uint64_t current) noexcept;

            std::atomic<uint64_t> m_activeVramBytes{0};
            std::atomic<uint64_t> m_peakVramBytes{0};
            std::array<std::atomic<uint64_t>, static_cast<size_t>(GpuResourceType::Count)> m_typeVramBytes;

            std::vector<StagingBufferItem> m_stagingPool;

            boost::lockfree::queue<DeferredReleaseItem*> m_deferredQueue{1024};
            boost::lockfree::queue<PendingGpuDeletion*> m_deletionQueue{1024};
            std::vector<DeferredReleaseItem> m_localDeferredItems;
            std::array<std::vector<PendingGpuDeletion>, kMaxFramesInFlight> m_frameDeletions;

            std::atomic<uint32_t> m_maxFramesInFlight{3};
            std::atomic<size_t> m_pendingDeferredCount{0};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_GPUMEMORYMANAGER_H
