#ifndef MQENGINE_RESOURCE_TABLE_PIPELINETABLE_H
#define MQENGINE_RESOURCE_TABLE_PIPELINETABLE_H

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
         * @brief GPU 管线与渲染通道绑定记录：
         *        以原生管线裸指针（void*）作为天然 Primary Key
         */
        struct PipelineRecord
        {
            const void* handle = nullptr;
            std::string passName;
            uint32_t pipelineType = 0;
            const void* vertexShader = nullptr;
            const void* fragmentShader = nullptr;
            const void* computeShader = nullptr;
            std::string debugName;
            GpuResourceState state = GpuResourceState::Ready;
            const void* fallbackHandle = nullptr;
        };

        /**
         * @brief 专用无锁管线资源表（PipelineTable）：
         *        以管线裸指针为主键，提供零锁并发注册、查询与 Shader 阶段关联
         */
        class ENGINE_API PipelineTable : public CoreEngineManager<PipelineTable>
        {
        public:
            PipelineTable();
            ~PipelineTable();

            PipelineTable(const PipelineTable&) = delete;
            PipelineTable& operator=(const PipelineTable&) = delete;

            void registerPipeline(const void* handle, PipelineRecord record);
            void unregisterPipeline(const void* handle);

            const PipelineRecord* getPipeline(const void* handle) const noexcept;
            PipelineRecord* getPipeline(const void* handle) noexcept;
            bool contains(const void* handle) const noexcept;

            void setDefaultFallback(const void* fallbackHandle) noexcept;
            const void* getDefaultFallback() const noexcept;
            const void* resolveHandle(const void* handle) const noexcept;

            template <typename PipeT>
            void registerTyped(const PipeT* handle, PipelineRecord record)
            {
                registerPipeline(static_cast<const void*>(handle), std::move(record));
            }

            template <typename PipeT>
            const PipeT* resolveTyped(const PipeT* handle) const noexcept
            {
                return static_cast<const PipeT*>(resolveHandle(static_cast<const void*>(handle)));
            }

            size_t activeCount() const noexcept { return m_table.size(); }
            void clear() noexcept;

        private:
            LockFreeResourceTable<const void*, PipelineRecord> m_table;
            std::atomic<const void*> m_defaultFallback{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_PIPELINETABLE_H
