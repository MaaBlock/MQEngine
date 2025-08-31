//
// Created by Administrator on 2025/8/25.
//

#ifndef COMPONENT_H
#define COMPONENT_H
#include "NameTag.h"
#include "../thirdparty/thirdparty.h"
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
}

#endif //COMPONENT_H
