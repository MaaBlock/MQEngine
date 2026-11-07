#ifndef COMPONENTREFLECTION_H
#define COMPONENTREFLECTION_H

#include <boost/mp11.hpp>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>
#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../render/data/Camera.h"
#include "../../render/manager/PipelineRegistries.h"
#include "../../thirdparty/thirdparty.h"
#include "../data/Component.h"
#include "../data/NameTag.h"

namespace MQEngine {

    using ComponentValue = std::variant<
        bool, int, float, double, std::string, Vec2, Vec3<>, Vec4, std::vector<std::string>
    >;

    struct ComponentFieldInfo {
        std::string name;
        std::string type;
        std::function<ComponentValue(void*)> getter;
        std::function<void(void*, const ComponentValue&)> setter;
    };

    using ComponentHasFn = bool(*)(entt::registry&, entt::entity);
    using ComponentGetFn = void*(*)(entt::registry&, entt::entity);
    using ComponentAddFn = void(*)(entt::registry&, entt::entity);
    using ComponentRemoveFn = void(*)(entt::registry&, entt::entity);
    using ComponentResetPoolFn = void(*)(entt::registry&);
    using ComponentWarmPoolFn = void(*)(entt::registry&);

    struct ComponentInfo {
        std::string name;
        entt::id_type typeId = 0;
        std::vector<ComponentFieldInfo> fields;
        ComponentHasFn hasComponent = nullptr;
        ComponentGetFn getComponent = nullptr;
        ComponentAddFn addComponent = nullptr;
        ComponentRemoveFn removeComponent = nullptr;
        ComponentResetPoolFn clearAndResetPool = nullptr;
        ComponentWarmPoolFn warmPool = nullptr;
    };

    /**
     * @brief 单个实体上某个组件的字段状态快照（用于 DLL 热重载前后跨模块边界保留组件数据）
     */
    struct EntityComponentFieldSnapshot {
        entt::entity entity = entt::null;
        std::unordered_map<std::string, ComponentValue> fields;
    };

    /**
     * @brief 单个 entt::registry 内指定组件集合的热重载快照
     */
    struct RegistryComponentSnapshot {
        entt::registry* registry = nullptr;
        std::unordered_map<std::string, std::vector<EntityComponentFieldSnapshot>> components;
    };

    using ComponentHotReloadSnapshot = std::vector<RegistryComponentSnapshot>;

    class ENGINE_API ComponentReflection : public CoreEngineManager<ComponentReflection> {
    public:
        using JsStructRegisterFn = void(*)(FCT::NB::Package&, const std::string&);
        using PyStructRegisterFn = void(*)(FCT::PyBri::Package&, const std::string&);

        ComponentReflection();
        ~ComponentReflection();
        void registerInfo();
        void init();
        void clear();
        void unregisterComponent(const std::string& componentName);

        const std::unordered_map<std::string, ComponentInfo>& getComponentInfos() const;
        bool hasComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        ComponentValue getComponentField(entt::registry& registry, entt::entity entity,
                                       const std::string& componentName, const std::string& fieldName) const;
        void setComponentField(entt::registry& registry, entt::entity entity,
                             const std::string& componentName, const std::string& fieldName,
                             const ComponentValue& value) const;
        void addComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        void removeComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        std::vector<std::string> getComponentFieldNames(const std::string& componentName) const;
        std::string getComponentFieldType(const std::string& componentName, const std::string& fieldName) const;
        std::vector<std::string> getRegisteredComponentNames() const;

        /**
         * @brief 在卸载插件或 UserCode DLL 前，对指定组件集合在所有活跃 registry 中的实体字段值进行快照
         */
        ComponentHotReloadSnapshot captureSnapshot(
            const std::vector<entt::registry*>& registries,
            const std::vector<std::string>& componentNames) const;

        /**
         * @brief 在 FreeLibrary 之前彻底清空并销毁指定组件在各 registry 中的 storage pool，
         *        防止卸载 DLL 后 entt::registry 持有指向已卸载 DLL 代码段的虚表与析构器
         */
        void purgeComponentPools(
            const std::vector<entt::registry*>& registries,
            const std::vector<std::string>& componentNames) const;

        /**
         * @brief 在新 DLL 加载并重新注册组件后，将快照中的组件及字段数据恢复到各实体
         */
        void restoreSnapshot(const ComponentHotReloadSnapshot& snapshot) const;

        /**
         * @brief 为指定 registry 预热所有已注册组件（包含插件与 UserCode 动态注册组件）的 storage pool，
         *        防止运行期首次挂载组件时触发 entt::registry 底层 pools 哈希表扩容重哈希
         */
        void warmComponentPools(entt::registry& registry) const;

        const ComponentInfo* findComponent(const std::string& componentName) const {
            auto it = m_componentInfos.find(componentName);
            return it != m_componentInfos.end() ? &it->second : nullptr;
        }

        void registerCustomComponent(const ComponentInfo& info) {
            m_componentInfos[info.name] = info;
            warmAllActiveRegistries(m_componentInfos[info.name]);
        }

        template<typename T>
        void registerCustomComponent(const std::string& name = "") {
            std::string resolvedName = name.empty() ? FCT::NB::cleanTypeName(typeid(T).name()) : name;
            ComponentRegistry::instance().registerComponent<T>(resolvedName);
            registerComponent<T>(resolvedName);
            registerScriptStructCallbacks(
                resolvedName,
                [](FCT::NB::Package& pkg, const std::string& n) { pkg.registerStruct<T>(n, true); },
                [](FCT::PyBri::Package& pkg, const std::string& n) { pkg.registerStruct<T>(n, true); }
            );
        }

        template<typename TupleType>
        void registerComponentTuple() {
            registerTupleImpl<TupleType>(std::make_index_sequence<std::tuple_size_v<TupleType>>{});
        }

        /**
         * @brief 注册组件类型，自动通过 Boost.Describe 或 C++20 Boost.PFR 零宏反射提取全部字段访问器
         */
        template<typename T>
        void registerComponent(const std::string& name = "") {
            std::string resolvedName = name.empty() ? FCT::NB::cleanTypeName(typeid(T).name()) : name;
            ComponentInfo info;
            info.name = resolvedName;
            info.typeId = entt::type_hash<T>::value();

            if constexpr (FCT::Reflect::HasStaticTypeMeta<T>) {
                populateFieldsFromStaticMeta<T>(info);
            } else if constexpr (FCT::NB::has_boost_describe_v<T>) {
                populateFieldsFromDescribe<T>(info);
            } else if constexpr (FCT::NB::is_pfr_reflectable_v<T>) {
                populateFieldsFromPfr<T>(info);
            }

            bindRegistryOperations<T>(info);
            auto& storedInfo = (m_componentInfos[resolvedName] = std::move(info));
            warmAllActiveRegistries(storedInfo);
        }

    private:
        void warmAllActiveRegistries(const ComponentInfo& info) const;
        void registerScriptStructCallbacks(const std::string& resolvedName,
                                           JsStructRegisterFn jsReg,
                                           PyStructRegisterFn pyReg);
        template<typename FieldType>
        static constexpr bool isSupportedFieldType() {
            return std::is_same_v<FieldType, bool> ||
                   std::is_same_v<FieldType, int> ||
                   std::is_same_v<FieldType, float> ||
                   std::is_same_v<FieldType, double> ||
                   std::is_same_v<FieldType, std::string> ||
                   std::is_same_v<FieldType, Vec2> ||
                   std::is_same_v<FieldType, Vec3<>> ||
                   std::is_same_v<FieldType, Vec3<double>> ||
                   std::is_same_v<FieldType, Vec4> ||
                   std::is_same_v<FieldType, std::vector<std::string>>;
        }

        template<typename FieldType>
        static std::string resolveFieldTypeName() {
            if constexpr (std::is_same_v<FieldType, bool>) return "bool";
            else if constexpr (std::is_same_v<FieldType, int>) return "int";
            else if constexpr (std::is_same_v<FieldType, float>) return "float";
            else if constexpr (std::is_same_v<FieldType, double>) return "double";
            else if constexpr (std::is_same_v<FieldType, std::string>) return "string";
            else if constexpr (std::is_same_v<FieldType, Vec2>) return "Vec2";
            else if constexpr (std::is_same_v<FieldType, Vec3<>> || std::is_same_v<FieldType, Vec3<double>>) return "Vec3";
            else if constexpr (std::is_same_v<FieldType, Vec4>) return "Vec4";
            else if constexpr (std::is_same_v<FieldType, std::vector<std::string>>) return "string[]";
            else return "";
        }

        template<typename T, typename FieldType, typename GetterFn, typename SetterFn>
        static ComponentFieldInfo createFieldAccessor(std::string fieldName, GetterFn&& getterFn, SetterFn&& setterFn) {
            ComponentFieldInfo fieldInfo;
            fieldInfo.name = std::move(fieldName);
            fieldInfo.type = resolveFieldTypeName<FieldType>();
            fieldInfo.getter = [getterFn = std::forward<GetterFn>(getterFn)](void* componentPtr) -> ComponentValue {
                const T* typedPtr = static_cast<const T*>(componentPtr);
                const auto& value = getterFn(*typedPtr);
                if constexpr (std::is_same_v<FieldType, Vec3<double>>) {
                    return ComponentValue(Vec3<>(value));
                } else {
                    return ComponentValue(value);
                }
            };
            fieldInfo.setter = [setterFn = std::forward<SetterFn>(setterFn)](void* componentPtr, const ComponentValue& value) {
                T* typedPtr = static_cast<T*>(componentPtr);
                if constexpr (std::is_same_v<FieldType, Vec3<double>>) {
                    if (std::holds_alternative<Vec3<>>(value)) {
                        setterFn(*typedPtr, Vec3<double>(std::get<Vec3<>>(value)));
                    }
                } else {
                    if (std::holds_alternative<FieldType>(value)) {
                        setterFn(*typedPtr, std::get<FieldType>(value));
                    }
                }
            };
            return fieldInfo;
        }

        template<typename T>
        static void populateFieldsFromStaticMeta(ComponentInfo& info) {
            FCT::Reflect::templateForFields<T>([&]<auto Field>() {
                using FieldType = typename decltype(Field)::ValueType;
                if constexpr (isSupportedFieldType<FieldType>()) {
                    info.fields.push_back(createFieldAccessor<T, FieldType>(
                        std::string(Field.name),
                        [](const T& obj) -> const FieldType& { return obj.*(Field.ptr); },
                        [](T& obj, const FieldType& val) { obj.*(Field.ptr) = val; }
                    ));
                }
            });
        }

        template<typename T>
        static void populateFieldsFromDescribe(ComponentInfo& info) {
            using Descriptors = boost::describe::describe_members<
                T,
                boost::describe::mod_any_access | boost::describe::mod_inherited>;

            boost::mp11::mp_for_each<Descriptors>([&](auto descriptor) {
                using FieldType = std::remove_cvref_t<decltype(std::declval<T>().*descriptor.pointer)>;
                if constexpr (isSupportedFieldType<FieldType>()) {
                    info.fields.push_back(createFieldAccessor<T, FieldType>(
                        descriptor.name,
                        [descriptor](const T& obj) -> const FieldType& { return obj.*descriptor.pointer; },
                        [descriptor](T& obj, const FieldType& val) { obj.*descriptor.pointer = val; }
                    ));
                }
            });
        }

        template<typename T>
        static void populateFieldsFromPfr(ComponentInfo& info) {
            constexpr size_t fieldCount = boost::pfr::tuple_size_v<T>;
            auto registerSingleField = [&]<std::size_t I>() {
                using FieldType = std::remove_cvref_t<boost::pfr::tuple_element_t<I, T>>;
                if constexpr (isSupportedFieldType<FieldType>()) {
                    constexpr std::string_view fname = boost::pfr::get_name<I, T>();
                    info.fields.push_back(createFieldAccessor<T, FieldType>(
                        std::string(fname),
                        [](const T& obj) -> const FieldType& { return boost::pfr::get<I>(obj); },
                        [](T& obj, const FieldType& val) { boost::pfr::get<I>(obj) = val; }
                    ));
                }
            };
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                (registerSingleField.template operator()<I>(), ...);
            }(std::make_index_sequence<fieldCount>{});
        }

        template<typename T>
        static void bindRegistryOperations(ComponentInfo& info) {
            info.hasComponent = [](entt::registry& registry, entt::entity entity) -> bool {
                return registry.all_of<T>(entity);
            };
            info.getComponent = [](entt::registry& registry, entt::entity entity) -> void* {
                return registry.all_of<T>(entity) ? &registry.get<T>(entity) : nullptr;
            };
            info.addComponent = [](entt::registry& registry, entt::entity entity) {
                if (!registry.all_of<T>(entity)) {
                    registry.emplace<T>(entity);
                }
            };
            info.removeComponent = [](entt::registry& registry, entt::entity entity) {
                if (registry.all_of<T>(entity)) {
                    registry.remove<T>(entity);
                }
            };
            info.clearAndResetPool = [](entt::registry& registry) {
                registry.clear<T>();
                registry.reset(entt::type_hash<T>::value());
            };
            info.warmPool = [](entt::registry& registry) {
                (void)registry.storage<T>();
            };
        }

        template<typename ElemType>
        void registerSingleFromTuple() {
            if constexpr (FCT::NB::is_reflectable_struct_v<ElemType> &&
                          std::is_default_constructible_v<ElemType>) {
                if (FCT::NB::countReflectableFields<ElemType>() > 0) {
                    std::string compName = FCT::NB::cleanTypeName(typeid(ElemType).name());
                    registerComponent<ElemType>(compName);
                }
            }
        }

        template<typename TupleType, std::size_t... I>
        void registerTupleImpl(std::index_sequence<I...>) {
            (registerSingleFromTuple<std::tuple_element_t<I, TupleType>>(), ...);
        }

        std::unordered_map<std::string, ComponentInfo> m_componentInfos;
    };

} // namespace MQEngine

#endif // COMPONENTREFLECTION_H
