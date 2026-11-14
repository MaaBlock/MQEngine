#ifndef MQENGINE_RESOURCE_TABLE_SCHEMATABLE_H
#define MQENGINE_RESOURCE_TABLE_SCHEMATABLE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/table/TypeMetaTable.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 数据库表结构定义与迁移管理器（SchemaTable）：
         *        承载内存数据库各表与 ECS 组件结构定义（Schema），
         *        并驱动热更新时的关系迁移状态机（ALTER TABLE：按同名字段自动将旧存储池字段迁移至新存储池）
         */
        class ENGINE_API SchemaTable : public CoreEngineManager<SchemaTable>
        {
        public:
            SchemaTable() = default;
            ~SchemaTable() = default;

            SchemaTable(const SchemaTable&) = delete;
            SchemaTable& operator=(const SchemaTable&) = delete;

            // 注册表结构或组件 Schema
            Status registerSchema(const TypeDescriptor& desc);

            template <typename T>
            Status registerSchema(const std::string& typeName = "", uint32_t version = 1)
            {
                if (auto* meta = TypeMetaTable::InstancePtr()) {
                    const std::string resolvedName = typeName.empty() ? ExtractShortTypeName<T>() : typeName;
                    const auto& desc = meta->registerType<T>(resolvedName, version);
                    return registerSchema(desc);
                }
                return InternalError("TypeMetaTable 尚未初始化");
            }

            template <typename... Ts>
            Status registerSchemas(ComponentTypeList<Ts...>, uint32_t version = 1)
            {
                Status status = OkStatus();
                ((status = status.ok() ? registerSchema<Ts>("", version) : status), ...);
                return status;
            }

            const TypeDescriptor* getSchema(const std::string& typeName, uint32_t version = 0) const;
            std::vector<const TypeDescriptor*> getAllLatestSchemas() const;

            /**
             * @brief ALTER TABLE 状态迁移：将单个旧版本对象迁移至新版本对象
             */
            Status alterTable(const TypeDescriptor& oldSchema,
                              const TypeDescriptor& newSchema,
                              const void* oldInstance,
                              void* newInstance);

            /**
             * @brief ALTER TABLE 批量状态迁移：迁移整块数组/连续存储池
             */
            Status alterTableBuffer(const TypeDescriptor& oldSchema,
                                    const TypeDescriptor& newSchema,
                                    const void* oldBuffer,
                                    size_t count,
                                    void* newBuffer);

            /**
             * @brief 模板化快捷状态迁移
             */
            template <typename OldT, typename NewT>
            Status alterTableTyped(const OldT& oldObj, NewT& newObj)
            {
                auto* meta = TypeMetaTable::InstancePtr();
                if (!meta) {
                    return InternalError("TypeMetaTable 尚未初始化");
                }
                const auto& oldDesc = meta->registerType<OldT>();
                const auto& newDesc = meta->registerType<NewT>();
                return alterTable(oldDesc, newDesc, &oldObj, &newObj);
            }

            void clear() noexcept;

        private:
            std::unordered_map<std::string, std::vector<TypeDescriptor>> m_schemasByName;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_SCHEMATABLE_H
