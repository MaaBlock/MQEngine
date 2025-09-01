//
// Created by Administrator on 2025/8/25.
//

#ifndef COMPONENT_H
#define COMPONENT_H
#include "NameTag.h"
#include "../thirdparty/thirdparty.h"
#include <boost/describe.hpp>
namespace MQEngine {
    struct StaticMeshInstance
    {
        std::string modelUuid;          // 模型UUID，用于定位模型文件
        std::string meshName;           // 网格体名称，用于从模型中选择特定网格
        FCT::StaticMesh<uint32_t>* mesh; // 实际存储的mesh，默认为nullptr表示未加载
        
        StaticMeshInstance() : mesh(nullptr) {}
        
        StaticMeshInstance(const std::string& uuid, const std::string& name) 
            : modelUuid(uuid), meshName(name), mesh(nullptr)
        {
        }
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & modelUuid;
            ar & meshName;
        }
    };
    BOOST_DESCRIBE_STRUCT(StaticMeshInstance, (), (modelUuid, meshName))

    struct ENGINE_API ScriptComponent
    {
        std::string functionName;       
        
        ScriptComponent() = default;
        
        ScriptComponent(const std::string& funcName) 
            : functionName(funcName)
        {
        }
        
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & functionName;
        }
    };
    BOOST_DESCRIBE_STRUCT(ScriptComponent, (), (functionName))

    struct ENGINE_API DirectionalLightComponent
    {
        FCT::Vec3 direction = FCT::Vec3(0.0f, -1.0f, 0.0f);  // 光照方向
        FCT::Vec3 color = FCT::Vec3(1.0f, 1.0f, 1.0f);       // 光照颜色
        float intensity = 1.0f;                              // 光照强度
        bool enabled = true;                                  // 是否启用
        
        DirectionalLightComponent() = default;
        
        DirectionalLightComponent(const FCT::Vec3& dir, const FCT::Vec3& col, float intens, bool enable = true)
            : direction(dir), color(col), intensity(intens), enabled(enable)
        {
        }
        
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & direction;
            ar & color;
            ar & intensity;
            ar & enabled;
        }
    };
    BOOST_DESCRIBE_STRUCT(DirectionalLightComponent, (), (direction, color, intensity, enabled))
}

#endif //COMPONENT_H
