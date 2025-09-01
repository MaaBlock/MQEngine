#ifndef COMPONENTREFLECTION_H
#define COMPONENTREFLECTION_H

#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
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
        bool, int, float, double, std::string,
        FCT::Vec3
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
        
    private:
        std::unordered_map<std::string, ComponentInfo> m_componentInfos;
        
        // 注册组件的模板函数
        template<typename T>
        void registerComponent(const std::string& name);
    };
    
} // namespace MQEngine

#endif // COMPONENTREFLECTION_H