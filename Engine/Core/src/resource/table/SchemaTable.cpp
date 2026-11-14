#include <MQEngine/resource/table/SchemaTable.h>
#include <algorithm>
#include <cassert>
#include <cstring>

namespace MQEngine
{
    namespace Resource
    {
        Status SchemaTable::registerSchema(const TypeDescriptor& desc)
        {
            if (desc.typeName.empty()) {
                return InvalidArgumentError("Schema 结构类型名不能为空");
            }

            auto& versions = m_schemasByName[desc.typeName];
            for (auto& existing : versions) {
                if (existing.schemaVersion == desc.schemaVersion) {
                    existing = desc;
                    return OkStatus();
                }
            }
            versions.push_back(desc);
            std::sort(versions.begin(), versions.end(), [](const TypeDescriptor& a, const TypeDescriptor& b) {
                return a.schemaVersion < b.schemaVersion;
            });

            return OkStatus();
        }

        const TypeDescriptor* SchemaTable::getSchema(const std::string& typeName, uint32_t version) const
        {
            auto it = m_schemasByName.find(typeName);
            if (it == m_schemasByName.end() || it->second.empty()) {
                return nullptr;
            }

            if (version == 0) {
                // 返回最高/最新版本 Schema
                return &it->second.back();
            }

            for (const auto& item : it->second) {
                if (item.schemaVersion == version) {
                    return &item;
                }
            }
            return nullptr;
        }

        std::vector<const TypeDescriptor*> SchemaTable::getAllLatestSchemas() const
        {
            std::vector<const TypeDescriptor*> result;
            result.reserve(m_schemasByName.size());
            for (const auto& [name, versions] : m_schemasByName)
            {
                if (!versions.empty())
                {
                    result.push_back(&versions.back());
                }
            }
            std::sort(result.begin(), result.end(), [](const TypeDescriptor* a, const TypeDescriptor* b) {
                return a->typeName < b->typeName;
            });
            return result;
        }

        Status SchemaTable::alterTable(const TypeDescriptor& oldSchema,
                                       const TypeDescriptor& newSchema,
                                       const void* oldInstance,
                                       void* newInstance)
        {
            if (!oldInstance || !newInstance) {
                return InvalidArgumentError("ALTER TABLE 迁移对象指针不能为空");
            }

            // 1. 初始化新对象为默认状态
            if (newSchema.defaultConstruct) {
                newSchema.defaultConstruct(newInstance);
            } else {
                std::memset(newInstance, 0, newSchema.typeSize);
            }

            auto* dstBase = static_cast<uint8_t*>(newInstance);
            const auto* srcBase = static_cast<const uint8_t*>(oldInstance);

            // 2. 遍历新表字段，寻找旧表中同名字段并执行关系投影迁移
            for (const auto& newField : newSchema.fields) {
                const FieldDescriptor* oldField = oldSchema.findField(newField.name);

                // 若按名称未匹配到，且字段总数相同且类型哈希一致，则按索引对齐尝试
                if (!oldField && (oldSchema.fields.size() == newSchema.fields.size())) {
                    const auto* candidate = oldSchema.findFieldByIndex(newField.fieldIndex);
                    if (candidate && candidate->typeHash == newField.typeHash) {
                        oldField = candidate;
                    }
                }

                if (oldField) {
                    uint8_t* dstFieldPtr = dstBase + newField.offset;
                    const uint8_t* srcFieldPtr = srcBase + oldField->offset;

                    if (oldField->size == newField.size) {
                        // 尺寸一致直接内存按位迁移
                        std::memcpy(dstFieldPtr, srcFieldPtr, newField.size);
                    } else if (oldField->size == 4 && newField.size == 8) {
                        // 常见 32 位整型扩展为 64 位整型
                        const int32_t val32 = *reinterpret_cast<const int32_t*>(srcFieldPtr);
                        *reinterpret_cast<int64_t*>(dstFieldPtr) = val32;
                    } else if (oldField->size == 8 && newField.size == 4) {
                        // 64 位截断为 32 位
                        const int64_t val64 = *reinterpret_cast<const int64_t*>(srcFieldPtr);
                        *reinterpret_cast<int32_t*>(dstFieldPtr) = static_cast<int32_t>(val64);
                    } else {
                        // 尺寸不兼容时拷贝小部分
                        const size_t copyBytes = std::min(oldField->size, newField.size);
                        std::memcpy(dstFieldPtr, srcFieldPtr, copyBytes);
                    }
                }
            }

            return OkStatus();
        }

        Status SchemaTable::alterTableBuffer(const TypeDescriptor& oldSchema,
                                             const TypeDescriptor& newSchema,
                                             const void* oldBuffer,
                                             size_t count,
                                             void* newBuffer)
        {
            if (count == 0) {
                return OkStatus();
            }
            if (!oldBuffer || !newBuffer) {
                return InvalidArgumentError("ALTER TABLE 批量迁移缓冲区指针不能为空");
            }
            if (oldSchema.typeSize == 0 || newSchema.typeSize == 0) {
                return InvalidArgumentError("ALTER TABLE 模式类型步长不能为 0");
            }

            const auto* srcPtr = static_cast<const uint8_t*>(oldBuffer);
            auto* dstPtr = static_cast<uint8_t*>(newBuffer);

            for (size_t i = 0; i < count; ++i) {
                const void* oldRecord = srcPtr + (i * oldSchema.typeSize);
                void* newRecord = dstPtr + (i * newSchema.typeSize);
                CHECK_STATUS(alterTable(oldSchema, newSchema, oldRecord, newRecord));
            }

            return OkStatus();
        }

        void SchemaTable::clear() noexcept
        {
            m_schemasByName.clear();
        }

    } // namespace Resource
} // namespace MQEngine
