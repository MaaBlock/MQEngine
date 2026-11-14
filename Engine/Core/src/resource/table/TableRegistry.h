#ifndef MQENGINE_RESOURCE_TABLE_TABLEREGISTRY_H
#define MQENGINE_RESOURCE_TABLE_TABLEREGISTRY_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/table/DataBase.h>
#include <MQEngine/resource/memory/ComponentDataBase.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/resource/table/TextureTable.h>
#include <MQEngine/resource/table/MeshTable.h>
#include <MQEngine/resource/table/MaterialTable.h>
#include <MQEngine/resource/table/PipelineTable.h>
#include <MQEngine/resource/table/ShaderTable.h>
#include <MQEngine/resource/table/TypeMetaTable.h>
#include <MQEngine/resource/table/SchemaTable.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 内存数据库全局表管理器（TableRegistry）：
         *        集中管理 DataBase（entt::registry 内存数据库）、独立 GPU 资源表（Texture/Mesh/Material/Pipeline/Shader）、
         *        类型元数据表、结构迁移表与无锁键值配置表，提供跨表主键安全关联查询接口
         */
        class ENGINE_API TableRegistry : public CoreEngineManager<TableRegistry>
        {
        public:
            TableRegistry();
            ~TableRegistry();

            TableRegistry(const TableRegistry&) = delete;
            TableRegistry& operator=(const TableRegistry&) = delete;

            void registerInfo() {}

            // ECS 内存数据库主表（DataBase）
            DataBase& dataBase() noexcept { return m_primaryDataBase; }
            const DataBase& dataBase() const noexcept { return m_primaryDataBase; }

            DataBase& primaryDataBase() noexcept { return m_primaryDataBase; }
            const DataBase& primaryDataBase() const noexcept { return m_primaryDataBase; }

            ComponentDataBase& primaryEcsTable() noexcept { return m_primaryDataBase; }
            const ComponentDataBase& primaryEcsTable() const noexcept { return m_primaryDataBase; }

            DataBase& getOrCreateNamedEcsTable(const std::string& tableName);
            DataBase* getNamedEcsTable(const std::string& tableName) noexcept;

            // 独立无锁 GPU 资源专用表
            TextureTable& textureTable() noexcept { return m_textureTable; }
            const TextureTable& textureTable() const noexcept { return m_textureTable; }

            MeshTable& meshTable() noexcept { return m_meshTable; }
            const MeshTable& meshTable() const noexcept { return m_meshTable; }

            MaterialTable& materialTable() noexcept { return m_materialTable; }
            const MaterialTable& materialTable() const noexcept { return m_materialTable; }

            PipelineTable& pipelineTable() noexcept { return m_pipelineTable; }
            const PipelineTable& pipelineTable() const noexcept { return m_pipelineTable; }

            ShaderTable& shaderTable() noexcept { return m_shaderTable; }
            const ShaderTable& shaderTable() const noexcept { return m_shaderTable; }

            // 综合 GPU 资源表
            GpuResourceTable& gpuResourceTable() noexcept { return m_gpuResourceTable; }
            const GpuResourceTable& gpuResourceTable() const noexcept { return m_gpuResourceTable; }

            // 类型与反射元数据表（C++20 Boost.PFR 零宏反射）
            TypeMetaTable& typeMetaTable() noexcept { return m_typeMetaTable; }
            const TypeMetaTable& typeMetaTable() const noexcept { return m_typeMetaTable; }

            // 表结构定义与热更新状态迁移表（ALTER TABLE）
            SchemaTable& schemaTable() noexcept { return m_schemaTable; }
            const SchemaTable& schemaTable() const noexcept { return m_schemaTable; }

            // 通用无锁键值配置表
            void setKeyValue(const std::string& key, const std::string& value);
            std::string getKeyValue(const std::string& key, const std::string& defaultValue = "") const;
            bool hasKey(const std::string& key) const noexcept;

            // 跨表天然主键安全关联查询
            const void* resolveGpuHandle(const void* handle, GpuResourceType type = GpuResourceType::Texture) const noexcept
            {
                return m_gpuResourceTable.resolveHandle(handle, type);
            }

            template <typename ComponentT>
            const void* lookupGpuHandleFromEntity(FCT::ECS::entity entity,
                                                 std::function<const void*(const ComponentT&)> handleExtractor,
                                                 GpuResourceType type = GpuResourceType::Texture) const
            {
                if (const auto* comp = m_primaryDataBase.tryGet<ComponentT>(entity)) {
                    const void* ptr = handleExtractor(*comp);
                    return m_gpuResourceTable.resolveHandle(ptr, type);
                }
                return m_gpuResourceTable.getDefaultFallbackHandle(type);
            }

            void clear() noexcept;

        private:
            struct KvNode
            {
                std::string key;
                std::string value;
                KvNode* next = nullptr;
            };

            ComponentDataBase m_primaryDataBase;
            std::unordered_map<std::string, UniquePtr<ComponentDataBase>> m_namedEcsTables;

            TextureTable m_textureTable;
            MeshTable m_meshTable;
            MaterialTable m_materialTable;
            PipelineTable m_pipelineTable;
            ShaderTable m_shaderTable;

            GpuResourceTable m_gpuResourceTable;
            TypeMetaTable m_typeMetaTable;
            SchemaTable m_schemaTable;

            std::atomic<KvNode*> m_kvHead{nullptr};
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_TABLEREGISTRY_H
