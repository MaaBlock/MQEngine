#ifndef MQENGINE_RESOURCE_TABLE_MATERIALTABLE_H
#define MQENGINE_RESOURCE_TABLE_MATERIALTABLE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/table/DataBase.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 材质与着色器参数绑定记录：
         *        以原生材质裸指针（void*）作为天然 Primary Key
         */
        struct MaterialRecord
        {
            const void* handle = nullptr;
            uint32_t shadingModel = 0;
            uint32_t albedoSlot = UINT32_MAX;
            uint32_t normalSlot = UINT32_MAX;
            uint32_t metallicRoughnessSlot = UINT32_MAX;
            uint32_t occlusionSlot = UINT32_MAX;
            uint32_t emissiveSlot = UINT32_MAX;
            uint64_t vramBytes = 0;
            std::string debugName;
            GpuResourceState state = GpuResourceState::Ready;
            const void* fallbackHandle = nullptr;
        };

        /**
         * @brief 专用无锁材质资源表（MaterialTable）：
         *        以材质裸指针为主键，提供零锁并发注册、查询与 Bindless 材质纹理槽位解析
         */
        class ENGINE_API MaterialTable : public CoreEngineManager<MaterialTable>
        {
        public:
            MaterialTable();
            ~MaterialTable();

            MaterialTable(const MaterialTable&) = delete;
            MaterialTable& operator=(const MaterialTable&) = delete;

            void registerMaterial(const void* handle, MaterialRecord record);
            void unregisterMaterial(const void* handle);

            const MaterialRecord* getMaterial(const void* handle) const noexcept;
            MaterialRecord* getMaterial(const void* handle) noexcept;
            bool contains(const void* handle) const noexcept;

            void setDefaultFallback(const void* fallbackHandle) noexcept;
            const void* getDefaultFallback() const noexcept;
            const void* resolveHandle(const void* handle) const noexcept;

            template <typename MatT>
            void registerTyped(const MatT* handle, MaterialRecord record)
            {
                registerMaterial(static_cast<const void*>(handle), std::move(record));
            }

            template <typename MatT>
            const MatT* resolveTyped(const MatT* handle) const noexcept
            {
                return static_cast<const MatT*>(resolveHandle(static_cast<const void*>(handle)));
            }

            size_t activeCount() const noexcept { return m_table.size(); }
            uint64_t totalVramBytes() const noexcept { return m_totalVramBytes.load(std::memory_order_relaxed); }
            void clear() noexcept;

        private:
            LockFreeResourceTable<const void*, MaterialRecord> m_table;
            std::atomic<uint64_t> m_totalVramBytes{0};
            std::atomic<const void*> m_defaultFallback{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_MATERIALTABLE_H
