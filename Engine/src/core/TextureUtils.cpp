#include "TextureUtils.h"

namespace MQEngine
{
    const char* textureTypeToString(FCT::ModelInfo::TextureType type)
    {
        switch (type)
        {
            case FCT::ModelInfo::TextureType::diffuse:
                return "Diffuse";
            case FCT::ModelInfo::TextureType::specular:
                return "Specular";
            case FCT::ModelInfo::TextureType::normals:
                return "Normal";
            case FCT::ModelInfo::TextureType::height:
                return "Height";
            case FCT::ModelInfo::TextureType::emissive:
                return "Emission";
            case FCT::ModelInfo::TextureType::shininess:
                return "Shininess";
            case FCT::ModelInfo::TextureType::opacity:
                return "Opacity";
            case FCT::ModelInfo::TextureType::displacement:
                return "Displacement";
            case FCT::ModelInfo::TextureType::lightmap:
                return "Lightmap";
            case FCT::ModelInfo::TextureType::reflection:
                return "Reflection";
            case FCT::ModelInfo::TextureType::baseColor:
                return "Base Color";
            case FCT::ModelInfo::TextureType::normalCamera:
                return "Normal Camera";
            case FCT::ModelInfo::TextureType::emissionColor:
                return "Emission Color";
            case FCT::ModelInfo::TextureType::metalness:
                return "Metalness";
            case FCT::ModelInfo::TextureType::diffuseRoughness:
                return "Diffuse Roughness";
            case FCT::ModelInfo::TextureType::ambientOcclusion:
                return "Ambient Occlusion";
            case FCT::ModelInfo::TextureType::sheen:
                return "Sheen";
            case FCT::ModelInfo::TextureType::clearcoat:
                return "Clearcoat";
            case FCT::ModelInfo::TextureType::transmission:
                return "Transmission";
            case FCT::ModelInfo::TextureType::unknown:
            default:
                return "Unknown";
        }
    }
    
    const char* textureTypeToChineseString(FCT::ModelInfo::TextureType type)
    {
        switch (type)
        {
            case FCT::ModelInfo::TextureType::diffuse:
                return "漫反射";
            case FCT::ModelInfo::TextureType::specular:
                return "镜面反射";
            case FCT::ModelInfo::TextureType::normals:
                return "法线";
            case FCT::ModelInfo::TextureType::height:
                return "高度";
            case FCT::ModelInfo::TextureType::emissive:
                return "自发光";
            case FCT::ModelInfo::TextureType::shininess:
                return "光泽度";
            case FCT::ModelInfo::TextureType::opacity:
                return "不透明度";
            case FCT::ModelInfo::TextureType::displacement:
                return "位移";
            case FCT::ModelInfo::TextureType::lightmap:
                return "光照贴图";
            case FCT::ModelInfo::TextureType::reflection:
                return "反射";
            case FCT::ModelInfo::TextureType::baseColor:
                return "基础颜色";
            case FCT::ModelInfo::TextureType::normalCamera:
                return "相机法线";
            case FCT::ModelInfo::TextureType::emissionColor:
                return "自发光颜色";
            case FCT::ModelInfo::TextureType::metalness:
                return "金属度";
            case FCT::ModelInfo::TextureType::diffuseRoughness:
                return "漫反射粗糙度";
            case FCT::ModelInfo::TextureType::ambientOcclusion:
                return "环境光遮蔽";
            case FCT::ModelInfo::TextureType::sheen:
                return "光泽";
            case FCT::ModelInfo::TextureType::clearcoat:
                return "透明涂层";
            case FCT::ModelInfo::TextureType::transmission:
                return "透射";
            case FCT::ModelInfo::TextureType::unknown:
            default:
                return "未知";
        }
    }
}