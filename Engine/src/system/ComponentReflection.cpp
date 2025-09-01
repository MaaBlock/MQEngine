#include "ComponentReflection.h"
#include <iostream>
#include <stdexcept>

namespace MQEngine {
    
    ComponentReflection::ComponentReflection() {
        registerComponent<PositionComponent>("PositionComponent");
        registerComponent<RotationComponent>("RotationComponent");
        registerComponent<ScaleComponent>("ScaleComponent");
        registerComponent<CameraComponent>("CameraComponent");
        registerComponent<NameTag>("NameTag");
        registerComponent<ScriptComponent>("ScriptComponent");
        registerComponent<StaticMeshInstance>("StaticMeshInstance");
        registerComponent<DirectionalLightComponent>("DirectionalLightComponent");
        registerComponent<DiffuseTextureComponent>("DiffuseTextureComponent");
    }
    
    const std::unordered_map<std::string, ComponentInfo>& ComponentReflection::getComponentInfos() const {
        return m_componentInfos;
    }
    
    bool ComponentReflection::hasComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it == m_componentInfos.end()) {
            return false;
        }
        return it->second.hasComponent(registry, entity);
    }
    
    ComponentValue ComponentReflection::getComponentField(entt::registry& registry, entt::entity entity, 
                                                        const std::string& componentName, const std::string& fieldName) const {
        auto componentIt = m_componentInfos.find(componentName);
        if (componentIt == m_componentInfos.end()) {
            throw std::runtime_error("Component not found: " + componentName);
        }
        
        if (!componentIt->second.hasComponent(registry, entity)) {
            throw std::runtime_error("Entity does not have component: " + componentName);
        }
        
        void* componentPtr = componentIt->second.getComponent(registry, entity);
        if (!componentPtr) {
            throw std::runtime_error("Failed to get component pointer");
        }
        
        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                return field.getter(componentPtr);
            }
        }
        
        throw std::runtime_error("Field not found: " + fieldName + " in component " + componentName);
    }
    
    void ComponentReflection::setComponentField(entt::registry& registry, entt::entity entity, 
                                              const std::string& componentName, const std::string& fieldName, 
                                              const ComponentValue& value) const {
        auto componentIt = m_componentInfos.find(componentName);
        if (componentIt == m_componentInfos.end()) {
            throw std::runtime_error("Component not found: " + componentName);
        }
        
        if (!componentIt->second.hasComponent(registry, entity)) {
            throw std::runtime_error("Entity does not have component: " + componentName);
        }
        
        void* componentPtr = componentIt->second.getComponent(registry, entity);
        if (!componentPtr) {
            throw std::runtime_error("Failed to get component pointer");
        }
        
        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                field.setter(componentPtr, value);
                return;
            }
        }
        
        throw std::runtime_error("Field not found: " + fieldName + " in component " + componentName);
    }
    
    void ComponentReflection::addComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it == m_componentInfos.end()) {
            throw std::runtime_error("Component not found: " + componentName);
        }
        it->second.addComponent(registry, entity);
    }
    
    void ComponentReflection::removeComponent(entt::registry& registry, entt::entity entity, const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it == m_componentInfos.end()) {
            throw std::runtime_error("Component not found: " + componentName);
        }
        it->second.removeComponent(registry, entity);
    }
    
    std::vector<std::string> ComponentReflection::getComponentFieldNames(const std::string& componentName) const {
        auto it = m_componentInfos.find(componentName);
        if (it != m_componentInfos.end()) {
            std::vector<std::string> fieldNames;
            for (const auto& field : it->second.fields) {
                fieldNames.push_back(field.name);
            }
            return fieldNames;
        }
        return {};
    }
    
    std::string ComponentReflection::getComponentFieldType(const std::string& componentName, const std::string& fieldName) const {
        auto componentIt = m_componentInfos.find(componentName);
        if (componentIt == m_componentInfos.end()) {
            return "";
        }
        
        for (const auto& field : componentIt->second.fields) {
            if (field.name == fieldName) {
                return field.type;
            }
        }
        
        return "";
    }
    
    std::vector<std::string> ComponentReflection::getRegisteredComponentNames() const {
        std::vector<std::string> componentNames;
        for (const auto& pair : m_componentInfos) {
            componentNames.push_back(pair.first);
        }
        return componentNames;
    }
    
    template<typename T>
    void ComponentReflection::registerComponent(const std::string& name) {
        ComponentInfo info;
        info.name = name;
        
        // 使用boost::describe获取字段信息
        using Descriptors = boost::describe::describe_members<T, boost::describe::mod_any_access>;
        
        boost::mp11::mp_for_each<Descriptors>([&](auto descriptor) {
            ComponentFieldInfo fieldInfo;
            fieldInfo.name = descriptor.name;
            
            using FieldType = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<T>().*descriptor.pointer)>>;
            
            // 设置字段类型名称
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
            } else if constexpr (std::is_same_v<FieldType, FCT::Vec3>) {
                fieldInfo.type = "Vec3";
            } else {
                fieldInfo.type = "unknown";
            }
            
            // 设置getter
            fieldInfo.getter = [descriptor](void* componentPtr) -> ComponentValue {
                T* typedPtr = static_cast<T*>(componentPtr);
                const auto& value = typedPtr->*descriptor.pointer;
                
                using FieldType = std::remove_cv_t<std::remove_reference_t<decltype(value)>>;
                if constexpr (std::is_same_v<FieldType, bool>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<FieldType, int>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<FieldType, float>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<FieldType, double>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<FieldType, std::string>) {
                    return ComponentValue(value);
                } else if constexpr (std::is_same_v<FieldType, FCT::Vec3>) {
                    return ComponentValue(value);
                } else {
                    throw std::runtime_error("Unsupported field type in getter");
                }
            };
            
            // 设置setter
            fieldInfo.setter = [descriptor](void* componentPtr, const ComponentValue& value) {
                T* typedPtr = static_cast<T*>(componentPtr);
                
                using FieldType = std::remove_cv_t<std::remove_reference_t<decltype(typedPtr->*descriptor.pointer)>>;
                if constexpr (std::is_same_v<FieldType, bool>) {
                    typedPtr->*descriptor.pointer = std::get<bool>(value);
                } else if constexpr (std::is_same_v<FieldType, int>) {
                    typedPtr->*descriptor.pointer = std::get<int>(value);
                } else if constexpr (std::is_same_v<FieldType, float>) {
                    typedPtr->*descriptor.pointer = std::get<float>(value);
                } else if constexpr (std::is_same_v<FieldType, double>) {
                    typedPtr->*descriptor.pointer = std::get<double>(value);
                } else if constexpr (std::is_same_v<FieldType, std::string>) {
                    typedPtr->*descriptor.pointer = std::get<std::string>(value);
                } else if constexpr (std::is_same_v<FieldType, FCT::Vec3>) {
                    typedPtr->*descriptor.pointer = std::get<FCT::Vec3>(value);
                } else {
                    throw std::runtime_error("Unsupported field type in setter");
                }
            };
            
            info.fields.push_back(std::move(fieldInfo));
        });
        
        // 设置组件操作函数
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

    
} // namespace MQEngine