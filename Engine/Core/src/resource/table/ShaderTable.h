#ifndef MQENGINE_RESOURCE_TABLE_SHADERTABLE_H
#define MQENGINE_RESOURCE_TABLE_SHADERTABLE_H

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
         * @brief GPU 着色器字节码与阶段元数据记录：
         *        以原生 Shader 裸指针（void*）作为天然 Primary Key
         */
        struct ShaderRecord
        {
            const void* handle = nullptr;
            uint32_t stage = 0;
            size_t bytecodeSize = 0;
            std::string entryPoint = "main";
            std::string debugName;
            GpuResourceState state = GpuResourceState::Ready;
            const void* fallbackHandle = nullptr;
        };

        /**
         * @brief 专用无锁着色器资源表（ShaderTable）：
         *        以 Shader 裸指针为主键，提供零锁并发注册、查询与阶段反射元数据管理
         */
        class ENGINE_API ShaderTable : public CoreEngineManager<ShaderTable>
        {
        public:
            ShaderTable();
            ~ShaderTable();

            ShaderTable(const ShaderTable&) = delete;
            ShaderTable& operator=(const ShaderTable&) = delete;

            void registerShader(const void* handle, ShaderRecord record);
            void unregisterShader(const void* handle);

            const ShaderRecord* getShader(const void* handle) const noexcept;
            ShaderRecord* getShader(const void* handle) noexcept;
            bool contains(const void* handle) const noexcept;

            void setDefaultFallback(const void* fallbackHandle) noexcept;
            const void* getDefaultFallback() const noexcept;
            const void* resolveHandle(const void* handle) const noexcept;

            template <typename ShaderT>
            void registerTyped(const ShaderT* handle, ShaderRecord record)
            {
                registerShader(static_cast<const void*>(handle), std::move(record));
            }

            template <typename ShaderT>
            const ShaderT* resolveTyped(const ShaderT* handle) const noexcept
            {
                return static_cast<const ShaderT*>(resolveHandle(static_cast<const void*>(handle)));
            }

            size_t activeCount() const noexcept { return m_table.size(); }
            void clear() noexcept;

        private:
            LockFreeResourceTable<const void*, ShaderRecord> m_table;
            std::atomic<const void*> m_defaultFallback{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_SHADERTABLE_H
