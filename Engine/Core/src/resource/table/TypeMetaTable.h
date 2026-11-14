#ifndef MQENGINE_RESOURCE_TABLE_TYPEMETATABLE_H
#define MQENGINE_RESOURCE_TABLE_TYPEMETATABLE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <boost/pfr.hpp>
#include <boost/pfr/core_name.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief 编译期组件类型列表（用于变参折叠表达式批量注册 Schema、序列化与编辑器自动检视面板）
         */
        template <typename... Ts>
        struct ComponentTypeList {};

        /**
         * @brief 自动剥离 class/struct 与命名空间前缀，提取纯净短类型名（如 "CameraComponent"）
         */
        template <typename T>
        inline std::string ExtractShortTypeName()
        {
            std::string raw = typeid(T).name();
            const size_t colonPos = raw.rfind("::");
            if (colonPos != std::string::npos)
            {
                return raw.substr(colonPos + 2);
            }
            const size_t spacePos = raw.rfind(' ');
            if (spacePos != std::string::npos)
            {
                return raw.substr(spacePos + 1);
            }
            return raw;
        }

        /**
         * @brief 编译期推导的字段基础图元类型枚举（供运行时动态反射检视面板与属性序列化按偏移量直接读写，支持插件动态注册组件）
         */
        enum class EFieldPrimitiveKind : uint8_t
        {
            Unknown = 0,
            Bool,
            Int32,
            UInt32,
            Float,
            Double,
            String,
            Vec2,
            Vec3,
            Vec4
        };

        template <typename FieldT>
        constexpr EFieldPrimitiveKind DeduceFieldPrimitiveKind() noexcept
        {
            using CleanT = std::decay_t<FieldT>;
            if constexpr (std::is_same_v<CleanT, bool>) return EFieldPrimitiveKind::Bool;
            else if constexpr (std::is_same_v<CleanT, int32_t>) return EFieldPrimitiveKind::Int32;
            else if constexpr (std::is_same_v<CleanT, uint32_t>) return EFieldPrimitiveKind::UInt32;
            else if constexpr (std::is_same_v<CleanT, float>) return EFieldPrimitiveKind::Float;
            else if constexpr (std::is_same_v<CleanT, double>) return EFieldPrimitiveKind::Double;
            else if constexpr (std::is_same_v<CleanT, std::string>) return EFieldPrimitiveKind::String;
            else if constexpr (std::is_same_v<CleanT, FCT::Vec2>) return EFieldPrimitiveKind::Vec2;
            else if constexpr (std::is_same_v<CleanT, FCT::Vec3<>>) return EFieldPrimitiveKind::Vec3;
            else if constexpr (std::is_same_v<CleanT, FCT::Vec4>) return EFieldPrimitiveKind::Vec4;
            else return EFieldPrimitiveKind::Unknown;
        }

        /**
         * @brief 字段元数据描述符
         */
        struct FieldDescriptor
        {
            std::string name;
            std::string typeName;
            size_t offset = 0;
            size_t size = 0;
            size_t alignment = 0;
            size_t typeHash = 0;
            size_t fieldIndex = 0;
            EFieldPrimitiveKind primitiveKind = EFieldPrimitiveKind::Unknown;
        };

        /**
         * @brief 类型与表结构元数据描述符（Schema）：
         *        不仅包含字段偏移量与类型信息，还内嵌由编译期模板自动生成的类型擦除生命周期函数与 ECS Registry 操作函数指针，
         *        使任何插件在运行期注册的新组件都能被引擎/编辑器直接挂载、卸载、检视与 Undo/Redo 快照恢复。
         */
        struct TypeDescriptor
        {
            std::string typeName;
            size_t typeSize = 0;
            size_t typeAlignment = 0;
            size_t typeHash = 0;
            uint32_t schemaVersion = 1;
            std::vector<FieldDescriptor> fields;

            // 类型擦除的对象生命周期函数指针
            void (*defaultConstruct)(void* ptr) = nullptr;
            void (*destruct)(void* ptr) = nullptr;
            void (*copyConstruct)(void* dst, const void* src) = nullptr;
            void (*moveConstruct)(void* dst, void* src) = nullptr;
            void (*copyAssign)(void* dst, const void* src) = nullptr;

            // 类型擦除的 ECS 组件存储池操作函数指针（支持跨插件 DLL 动态组件操作与通用撤销/重做命令）
            bool (*ecsHas)(const FCT::ECS::registry& reg, FCT::ECS::entity ent) = nullptr;
            void* (*ecsGetMutable)(FCT::ECS::registry& reg, FCT::ECS::entity ent) = nullptr;
            const void* (*ecsGetConst)(const FCT::ECS::registry& reg, FCT::ECS::entity ent) = nullptr;
            void* (*ecsEmplaceOrReplace)(FCT::ECS::registry& reg, FCT::ECS::entity ent, const void* srcOrNull) = nullptr;
            void (*ecsRemove)(FCT::ECS::registry& reg, FCT::ECS::entity ent) = nullptr;

            const FieldDescriptor* findField(const std::string& fieldName) const noexcept
            {
                for (const auto& f : fields) {
                    if (f.name == fieldName) {
                        return &f;
                    }
                }
                return nullptr;
            }

            const FieldDescriptor* findFieldByIndex(size_t index) const noexcept
            {
                if (index < fields.size()) {
                    return &fields[index];
                }
                return nullptr;
            }
        };

        /**
         * @brief 编译期与运行期类型元数据表（TypeMetaTable）：
         *        利用 C++20 Boost.PFR 零宏反射自动解构聚合体结构、字段名、偏移量与内存布局
         */
        class ENGINE_API TypeMetaTable : public CoreEngineManager<TypeMetaTable>
        {
        public:
            TypeMetaTable() = default;
            ~TypeMetaTable() = default;

            TypeMetaTable(const TypeMetaTable&) = delete;
            TypeMetaTable& operator=(const TypeMetaTable&) = delete;

            /**
             * @brief 零宏编译期反射注册类型
             */
            template <typename T>
            const TypeDescriptor& registerType(const std::string& customName = "", uint32_t version = 1)
            {
                const std::string name = customName.empty() ? typeid(T).name() : customName;
                const size_t thash = typeid(T).hash_code();

                auto it = m_descriptorsByName.find(name);
                if (it != m_descriptorsByName.end()) {
                    return it->second;
                }

                TypeDescriptor desc;
                desc.typeName = name;
                desc.typeSize = sizeof(T);
                desc.typeAlignment = alignof(T);
                desc.typeHash = thash;
                desc.schemaVersion = version;

                if constexpr (std::is_default_constructible_v<T>) {
                    desc.defaultConstruct = [](void* p) { new (p) T(); };
                }
                if constexpr (std::is_destructible_v<T>) {
                    desc.destruct = [](void* p) { static_cast<T*>(p)->~T(); };
                }
                if constexpr (std::is_copy_constructible_v<T>) {
                    desc.copyConstruct = [](void* dst, const void* src) {
                        new (dst) T(*static_cast<const T*>(src));
                    };
                }
                if constexpr (std::is_move_constructible_v<T>) {
                    desc.moveConstruct = [](void* dst, void* src) {
                        new (dst) T(std::move(*static_cast<T*>(src)));
                    };
                }
                if constexpr (std::is_copy_assignable_v<T>) {
                    desc.copyAssign = [](void* dst, const void* src) {
                        *static_cast<T*>(dst) = *static_cast<const T*>(src);
                    };
                }

                // 自动生成类型擦除的 ECS Registry 操作入口（支持插件动态注册组件的挂载/卸载/读写/Undo/Redo）
                if constexpr (std::is_default_constructible_v<T> && std::is_copy_constructible_v<T> && std::is_move_assignable_v<T>) {
                    desc.ecsHas = [](const FCT::ECS::registry& reg, FCT::ECS::entity ent) -> bool {
                        return ent != FCT::ECS::null && reg.valid(ent) && reg.all_of<T>(ent);
                    };
                    desc.ecsGetMutable = [](FCT::ECS::registry& reg, FCT::ECS::entity ent) -> void* {
                        return (ent != FCT::ECS::null && reg.valid(ent) && reg.all_of<T>(ent))
                            ? static_cast<void*>(&reg.get<T>(ent))
                            : nullptr;
                    };
                    desc.ecsGetConst = [](const FCT::ECS::registry& reg, FCT::ECS::entity ent) -> const void* {
                        return (ent != FCT::ECS::null && reg.valid(ent) && reg.all_of<T>(ent))
                            ? static_cast<const void*>(&reg.get<T>(ent))
                            : nullptr;
                    };
                    desc.ecsEmplaceOrReplace = [](FCT::ECS::registry& reg, FCT::ECS::entity ent, const void* srcOrNull) -> void* {
                        if (ent == FCT::ECS::null || !reg.valid(ent)) {
                            return nullptr;
                        }
                        if (srcOrNull) {
                            return static_cast<void*>(&reg.emplace_or_replace<T>(ent, *static_cast<const T*>(srcOrNull)));
                        }
                        return static_cast<void*>(&reg.emplace_or_replace<T>(ent));
                    };
                    desc.ecsRemove = [](FCT::ECS::registry& reg, FCT::ECS::entity ent) {
                        if (ent != FCT::ECS::null && reg.valid(ent) && reg.all_of<T>(ent)) {
                            reg.remove<T>(ent);
                        }
                    };
                }

                // C++20 Boost.PFR 零宏自动字段解构
                if constexpr (std::is_aggregate_v<T>) {
                    constexpr size_t fieldCount = boost::pfr::tuple_size_v<T>;
                    desc.fields.reserve(fieldCount);

                    T dummy{};
                    boost::pfr::for_each_field(dummy, [&](const auto& field, size_t idx) {
                        using FieldType = std::decay_t<decltype(field)>;
                        FieldDescriptor fd;
                        fd.fieldIndex = idx;
                        fd.typeName = typeid(FieldType).name();
                        fd.size = sizeof(FieldType);
                        fd.alignment = alignof(FieldType);
                        fd.typeHash = typeid(FieldType).hash_code();
                        fd.offset = reinterpret_cast<const uint8_t*>(&field) - reinterpret_cast<const uint8_t*>(&dummy);
                        fd.primitiveKind = DeduceFieldPrimitiveKind<FieldType>();

                        // 提取编译期静态字段名
                        constexpr auto names = boost::pfr::names_as_array<T>();
                        if (idx < names.size()) {
                            fd.name = std::string(names[idx]);
                        } else {
                            fd.name = "field_" + std::to_string(idx);
                        }
                        desc.fields.push_back(std::move(fd));
                    });
                }

                m_descriptorsByHash[thash] = desc;
                auto [insertIt, success] = m_descriptorsByName.emplace(name, std::move(desc));
                return insertIt->second;
            }

            const TypeDescriptor* getType(const std::string& name) const noexcept;
            const TypeDescriptor* getType(size_t typeHash) const noexcept;

            template <typename T>
            const TypeDescriptor* getType() const noexcept
            {
                return getType(typeid(T).hash_code());
            }

            bool hasType(const std::string& name) const noexcept;
            size_t registeredTypeCount() const noexcept;

            void clear() noexcept;

        private:
            std::unordered_map<std::string, TypeDescriptor> m_descriptorsByName;
            std::unordered_map<size_t, TypeDescriptor> m_descriptorsByHash;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_TABLE_TYPEMETATABLE_H
