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
        registerComponent<TickerScriptComponent>("TickerScriptComponent");
        registerComponent<OnStartScriptComponent>("OnStartScriptComponent");
        registerComponent<StaticMeshInstance>("StaticMeshInstance");
        registerComponent<DirectionalLightComponent>("DirectionalLightComponent");
        //registerComponent<DiffuseTextureComponent>("DiffuseTextureComponent");
        //registerComponent<NormalMapComponent>("NormalMapComponent");
        registerComponent<ShininessComponent>("ShininessComponent");
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

} // namespace MQEngine