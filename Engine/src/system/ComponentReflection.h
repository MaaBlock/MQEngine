#ifndef COMPONENTREFLECTION_H
#define COMPONENTREFLECTION_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../data/Camera.h"
#include "../data/Component.h"
#include "../data/NameTag.h"
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <string>
#include <variant>
#include <unordered_map>
#include <functional>

namespace MQEngine {

    using ComponentValue = std::variant<
        bool, int, float, double, std::string, Vec3
    >;

    struct ComponentFieldInfo {
        std::string name;
        std::string type;
        std::function<ComponentValue(void*)> getter;
        std::function<void(void*, const ComponentValue&)> setter;
    };

    struct ComponentInfo {
        std::string name;
        std::vector<ComponentFieldInfo> fields;
        std::function<bool(entt::registry&, entt::entity)> hasComponent;
        std::function<void*(entt::registry&, entt::entity)> getComponent;
        std::function<void(entt::registry&, entt::entity)> addComponent;
        std::function<void(entt::registry&, entt::entity)> removeComponent;
    };
    
    class ENGINE_API ComponentReflection {
    public:
        ComponentReflection();
        ~ComponentReflection() = default;
        
        // 获取所有注册的组件信息
        const std::unordered_map<std::string, ComponentInfo>& getComponentInfos() const;
        
        // 检查实体是否有指定组件
        bool hasComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        
        // 获取组件字段值
        ComponentValue getComponentField(entt::registry& registry, entt::entity entity, 
                                       const std::string& componentName, const std::string& fieldName) const;
        
        // 设置组件字段值
        void setComponentField(entt::registry& registry, entt::entity entity, 
                             const std::string& componentName, const std::string& fieldName, 
                             const ComponentValue& value) const;
        
        // 添加组件到实体
        void addComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        
        // 从实体移除组件
        void removeComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const;
        
        // 获取组件字段名
    std::vector<std::string> getComponentFieldNames(const std::string& componentName) const;
    
    // 获取组件字段类型
    std::string getComponentFieldType(const std::string& componentName, const std::string& fieldName) const;
    
    // 获取所有注册的组件名
    std::vector<std::string> getRegisteredComponentNames() const;

    /**
     * @brief 动态注册自定义组件信息
     */
    void registerCustomComponent(const ComponentInfo& info) {
        m_componentInfos[info.name] = info;
    }

    /**
     * @brief 动态注册自定义组件类型与反射元数据
     */
    template<typename T>
    void registerCustomComponent(const std::string& name) {
        registerComponent<T>(name);
    }

    /**
     * @brief 注册组件的模板函数
     */
    template<typename T>
    void registerComponent(const std::string& name) {
        ComponentInfo info;
        info.name = name;

        using Descriptors = boost::describe::describe_members<T, boost::describe::mod_any_access>;

        boost::mp11::mp_for_each<Descriptors>([&](auto descriptor) {
            ComponentFieldInfo fieldInfo;
            fieldInfo.name = descriptor.name;

            using FieldType = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<T>().*descriptor.pointer)>>;

            if constexpr (std::is_same_v<FieldType, bool>) {
                fieldInfo.type = "bool";
            } else if constexpr (std::is_same_v<FieldType, int>) {
                fieldInfo.type = "int";
            } else if constexpr (std::is_same_v<FieldType, float>) {
                fieldInfo.type = "float";
            } else if constexpr (std::is_same_v<FieldType, double>) {
                fieldInfo.type = "double";
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                fieldInfo.type = "string";
            } else if constexpr (std::is_same_v<FieldType, Vec3> || std::is_same_v<FieldType, FCT::Vec3>) {
                fieldInfo.type = "Vec3";
            } else {
                fieldInfo.type = "unknown";
            }

            fieldInfo.getter = [descriptor](void* componentPtr) -> ComponentValue {
                T* typedPtr = static_cast<T*>(componentPtr);
                const auto& value = typedPtr->*descriptor.pointer;

                using ValueFieldType = std::remove_cv_t<std::remove_reference_t<decltype(value)>>;
                if constexpr (std::is_same_v<ValueFieldType, bool>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<ValueFieldType, int>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<ValueFieldType, float>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<ValueFieldType, double>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<ValueFieldType, std::string>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<ValueFieldType, Vec3> || std::is_same_v<ValueFieldType, FCT::Vec3>) {
                    return ComponentValue(value);
                } else {
                    throw std::runtime_error("Unsupported field type in getter");
                }
            };

            fieldInfo.setter = [descriptor](void* componentPtr, const ComponentValue& value) {
                T* typedPtr = static_cast<T*>(componentPtr);

                using ValueFieldType = std::remove_cv_t<std::remove_reference_t<decltype(typedPtr->*descriptor.pointer)>>;
                if constexpr (std::is_same_v<ValueFieldType, bool>) {
                    typedPtr->*descriptor.pointer = std::get<bool>(value);
                } else if constexpr (std::is_same_v<ValueFieldType, int>) {
                    typedPtr->*descriptor.pointer = std::get<int>(value);
                } else if constexpr (std::is_same_v<ValueFieldType, float>) {
                    typedPtr->*descriptor.pointer = std::get<float>(value);
                } else if constexpr (std::is_same_v<ValueFieldType, double>) {
                    typedPtr->*descriptor.pointer = std::get<double>(value);
                } else if constexpr (std::is_same_v<ValueFieldType, std::string>) {
                    typedPtr->*descriptor.pointer = std::get<std::string>(value);
                } else if constexpr (std::is_same_v<ValueFieldType, Vec3> || std::is_same_v<ValueFieldType, FCT::Vec3>) {
                    typedPtr->*descriptor.pointer = std::get<Vec3>(value);
                } else {
                    throw std::runtime_error("Unsupported field type in setter");
                }
            };

            info.fields.push_back(std::move(fieldInfo));
        });

        info.hasComponent = [](entt::registry& registry, entt::entity entity) -> bool {
            return registry.all_of<T>(entity);
        };

        info.getComponent = [](entt::registry& registry, entt::entity entity) -> void* {
            if (registry.all_of<T>(entity)) {
                return &registry.get<T>(entity);
            }
            return nullptr;
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

        m_componentInfos[name] = std::move(info);
    }

private:
    std::unordered_map<std::string, ComponentInfo> m_componentInfos;
};

} // namespace MQEngine

#endif // COMPONENTREFLECTION_H