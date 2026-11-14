#ifndef MQENGINE_RESOURCE_TABLE_MESHTABLE_H
#define MQENGINE_RESOURCE_TABLE_MESHTABLE_H

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
         * @brief 网格与顶点索引几何资源记录：
         *        以原生 GPU 网格裸指针（FCT::StaticMesh* / void*）作为天然 Primary Key
         */
        struct MeshRecord
        {
            const void* handle = nullptr;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            uint32_t subMeshCount = 1;
            uint64_t vramBytes = 0;
            FCT::Vec3<float> aabbMin{};
            FCT::Vec3<float> aabbMax{};
            std::string debugName;
            GpuResourceState state = GpuResourceState::Ready;
            const void* fallbackHandle = nullptr;
        };

        /**
         * @brief 专用无锁网格资源表（MeshTable）：
         *        以 GPU 网格裸指针为主键，提供零锁并发注册、查询与 AABB 边界检索
         */
        class ENGINE_API MeshTable : public CoreEngineManager<MeshTable>
        {
        public:
            MeshTable();
            ~MeshTable();

            MeshTable(const MeshTable&) = delete;
            MeshTable& operator=(const MeshTable&) = delete;

            void registerMesh(const void* handle, MeshRecord record);
            void unregisterMesh(const void* handle);

            const MeshRecord* getMesh(const void* handle) const noexcept;
            MeshRecord* getMesh(const void* handle) noexcept;
            bool contains(const void* handle) const noexcept;

            void setDefaultFallback(const void* fallbackHandle) noexcept;
            const void* getDefaultFallback() const noexcept;
            const void* resolveHandle(const void* handle) const noexcept;

            template <typename MeshT>
            void registerTyped(const MeshT* handle, MeshRecord record)
            {
                registerMesh(static_cast<const void*>(handle), std::move(record));
            }

            template <typename MeshT>
            const MeshT* resolveTyped(const MeshT* handle) const noexcept
            {
                return static_cast<const MeshT*>(resolveHandle(static_cast<const void*>(handle)));
            }

            size_t activeCount() const noexcept { return m_table.size(); }
            uint64_t totalVramBytes() const noexcept { return m_totalVramBytes.load(std::memory_order_relaxed); }
            void clear() noexcept;

        private:
            LockFreeResourceTable<const void*, MeshRecord> m_table;
            std::atomic<uint64_t> m_totalVramBytes{0};
            std::atomic<const void*> m_defaultFallback{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_MESHTABLE_H
