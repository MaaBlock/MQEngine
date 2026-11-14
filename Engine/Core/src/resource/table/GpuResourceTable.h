#ifndef MQENGINE_RESOURCE_TABLE_GPURESOURCETABLE_H
#define MQENGINE_RESOURCE_TABLE_GPURESOURCETABLE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/table/DataBase.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <boost/lockfree/queue.hpp>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief GPU 资源在内存数据库表中的行记录：
         *        严格遵循天然主键设计，底层资源裸指针（handle）即为 Primary Key，零多余抽象
         */
        struct GpuResourceRecord
        {
            const void* handle = nullptr; // 天然主键（Primary Key）：FCT::Image* / FCT::Buffer* / RHI 资源指针
            GpuResourceType type = GpuResourceType::Texture;
            GpuResourceState state = GpuResourceState::Ready;
            uint64_t vramBytes = 0;
            std::string debugName;
            uint64_t registrationFrame = 0;
            uint32_t bindlessSlot = UINT32_MAX;
        };

        /**
         * @brief GPU 资源内存数据库表（GpuResourceTable）：
         *        直接以 GPU 资源原生指针作为 Primary Key 进行登记、生命周期与 VRAM 统计追踪。
         *        基于 LockFreeResourceTable 严格无锁实现，杜绝互斥锁与人工合成 Key。
         */
        class ENGINE_API GpuResourceTable : public CoreEngineManager<GpuResourceTable>
        {
        public:
            GpuResourceTable();
            ~GpuResourceTable();

            GpuResourceTable(const GpuResourceTable&) = delete;
            GpuResourceTable& operator=(const GpuResourceTable&) = delete;

            // 资源登记与注销（直接以资源指针作为主键）
            void registerResource(const void* handle,
                                  GpuResourceType type,
                                  uint64_t vramBytes,
                                  const std::string& debugName = "",
                                  GpuResourceState state = GpuResourceState::Ready);

            void unregisterResource(const void* handle);

            // 四步延迟释放流水线：Step 1 逻辑退役标记与 Step 4 GPU 完成后终结回收
            void markResourceReleased(const void* handle);
            void finalizeUnregister(const void* handle);

            // 主键存在性与记录查询
            bool contains(const void* handle) const noexcept;
            const GpuResourceRecord* getRecord(const void* handle) const noexcept;
            GpuResourceRecord* getRecord(const void* handle) noexcept;

            void updateVram(const void* handle, uint64_t newVramBytes);
            void updateState(const void* handle, GpuResourceState state);

            // Bindless 描述符槽位分配与查询
            uint32_t allocateBindlessSlot(const void* handle);
            uint32_t getBindlessSlot(const void* handle) const noexcept;

            // 默认回退句柄管理与安全解析
            void setDefaultFallbackHandle(GpuResourceType type, const void* fallbackHandle) noexcept;
            const void* getDefaultFallbackHandle(GpuResourceType type) const noexcept;
            const void* resolveHandle(const void* handle, GpuResourceType type = GpuResourceType::Texture) const noexcept;

            // 泛型便捷操作
            template <typename T>
            void registerTyped(const T* handle,
                               GpuResourceType type,
                               uint64_t vramBytes,
                               const std::string& debugName = "",
                               GpuResourceState state = GpuResourceState::Ready)
            {
                registerResource(static_cast<const void*>(handle), type, vramBytes, debugName, state);
            }

            template <typename T>
            bool containsTyped(const T* handle) const noexcept
            {
                return contains(static_cast<const void*>(handle));
            }

            template <typename T>
            const T* resolveTyped(const T* handle, GpuResourceType type = GpuResourceType::Texture) const noexcept
            {
                return static_cast<const T*>(resolveHandle(static_cast<const void*>(handle), type));
            }

            size_t activeCount() const noexcept;
            uint64_t totalVramBytes() const noexcept;
            std::vector<GpuResourceRecord> snapshotRecords() const;

            void clear() noexcept;

        private:
            uint32_t acquireBindlessSlot();
            void releaseBindlessSlot(uint32_t slot);

            LockFreeResourceTable<const void*, GpuResourceRecord> m_table;
            std::atomic<uint64_t> m_totalVramBytes{0};
            std::atomic<uint32_t> m_nextBindlessSlot{3};
            boost::lockfree::queue<uint32_t> m_freeSlots{4096};
            std::array<std::atomic<const void*>, static_cast<size_t>(GpuResourceType::Count)> m_defaultFallbacks{};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_GPURESOURCETABLE_H
