//
// Created by Administrator on 2025/8/25.
//

#ifndef COMPONENT_H
#define COMPONENT_H
#include "NameTag.h"
#include "../thirdparty/thirdparty.h"
#include <boost/describe.hpp>
#include "../core/UniformSlots.h"
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

    struct ENGINE_API TickerScriptComponent
    {
        std::string functionName;

        TickerScriptComponent() = default;

        TickerScriptComponent(const std::string& funcName)
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
    BOOST_DESCRIBE_STRUCT(TickerScriptComponent, (), (functionName))

    struct ENGINE_API OnStartScriptComponent
    {
        std::string functionName;

        OnStartScriptComponent() = default;

        OnStartScriptComponent(const std::string& funcName)
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
    BOOST_DESCRIBE_STRUCT(OnStartScriptComponent, (), (functionName))

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

    struct ENGINE_API ShininessComponent
    {
        float shininess = 32.0f;
        ShininessComponent() = default;

        ShininessComponent(float shininess)
            : shininess(shininess)
        {
        }

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & shininess;
        }
    };
    BOOST_DESCRIBE_STRUCT(ShininessComponent, (), (shininess))

    struct ENGINE_API CacheShininess
    {
        FCT::UniquePtr<FCT::Uniform> uniform;
        bool init = false;

        CacheShininess(FCT::Context* ctx)
        {
            uniform = FCT::makeUnique<FCT::Uniform>(ctx, ShininessUniformSlot);
        }
    };
    struct CacheResource
    {
        /*
         * @brief 在render线程中是否可见
         */
        bool visible = false;
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {

        }
    };
    struct TextureComponent : CacheResource
    {
        TextureComponent(std::string uuid, std::string path) : modelUuid(uuid), texturePath(path) {}
        TextureComponent() : modelUuid(), texturePath(),texture(nullptr)
        {

        }
        /*
         * @brief 用于定位模型
         */
        std::string modelUuid;
        /*
         * @brief 相对模型的相对路径
         */
        std::string texturePath;
        /*
         * @brief 缓存的图片对象
         */
        FCT::Image* texture = nullptr;

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & boost::serialization::base_object<CacheResource>(*this);
            ar & modelUuid;
            ar & texturePath;
        }
    };
    //BOOST_DESCRIBE_STRUCT(TextureComponent, (CacheResource), (modelUuid, texturePath))

    
    struct DiffuseTextureComponent : public TextureComponent
    {
        DiffuseTextureComponent() : TextureComponent()
        {

        }
        DiffuseTextureComponent(std::string uuid, std::string path)
            : TextureComponent(uuid, path)
        {
        }
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & boost::serialization::base_object<TextureComponent>(*this);
        }
    };
    //BOOST_DESCRIBE_STRUCT(DiffuseTextureComponent, (TextureComponent), ())
    struct NormalMapComponent : public TextureComponent
    {
        NormalMapComponent()
        {

        }
        NormalMapComponent(std::string uuid, std::string path)
            : TextureComponent(uuid, path)
        {
        }

        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & boost::serialization::base_object<TextureComponent>(*this);
        }
    };
    //BOOST_DESCRIBE_STRUCT(NormalMapComponent, (TextureComponent), ())

    struct AlbedoTextureComponent : TextureComponent
    {
        AlbedoTextureComponent(std::string uuid, std::string path) : TextureComponent(uuid, path)
        {

        }
    };
    //BOOST_DESCRIBE_STRUCT(AlbedoTextureComponent, (TextureComponent), ())

    struct NormalTextureComponent : TextureComponent
    {
        NormalTextureComponent(std::string uuid, std::string path) : TextureComponent(uuid, path)
        {

        }
    };
    //BOOST_DESCRIBE_STRUCT(NormalTextureComponent, (TextureComponent), ())

    struct EmissiveTextureComponent : TextureComponent
    {
        EmissiveTextureComponent(std::string uuid, std::string path) : TextureComponent(uuid, path)
        {

        }
    };
    //BOOST_DESCRIBE_STRUCT(EmissiveTextureComponent, (TextureComponent), ())

    struct OrmTextureComponent : TextureComponent
    {
        OrmTextureComponent(std::string uuid, std::string path) : TextureComponent(uuid, path)
        {

        }
    };
    //BOOST_DESCRIBE_STRUCT(OrmTextureComponent, (TextureComponent), ())
}

#endif //COMPONENT_H
