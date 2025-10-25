//
// Created by Administrator on 2025/1/20.
//

#ifndef TEXTURERENDERYSTEM_H
#define TEXTURERENDERYSTEM_H
#include "../data/DataManager.h"
#include "../data/Component.h"
#include <unordered_map>
#include <vector>

namespace MQEngine {
    struct TextureCacheKey
    {
        std::string modelUuid;
        std::string texturePath;
        FCT::Format format;
    };
    enum EngineTextureType
    {
        albedoTexture = entt::type_hash<AlbedoTextureComponent>::value(),
        normalTexture = entt::type_hash<NormalTextureComponent>::value(),
        emissiveTexture = entt::type_hash<EmissiveTextureComponent>::value(),
        ormTexture = entt::type_hash<OrmTextureComponent>::value()
    };
    inline FCT::Format getTextureFormat(uint32_t channels,EngineTextureType type)
    {
        switch (type)
        {
            // 颜色数据，srgb
        case albedoTexture:
            return FCT::Format::R8G8B8A8_SRGB;
        case emissiveTexture:
            return FCT::Format::R8G8B8A8_SRGB;
            // 非颜色数据,unorm
        case normalTexture:
            return FCT::Format::R8G8B8A8_UNORM;
        case ormTexture:
            if (channels == 4)
            {
                return FCT::Format::R8G8B8A8_UNORM;
            }
            if (channels == 3)
            {
                return FCT::Format::R8G8B8_UNORM;
            }
            FCT::ferr << "纹理通道数与纹理类型不匹配" << std::endl;
            return FCT::Format::R8G8B8A8_UNORM;
        default:
            return FCT::Format::UNDEFINED;
        }
    }
    class ENGINE_API TextureCacheSystem {
    public:
        TextureCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        ~TextureCacheSystem();
        void updateLogic();
        void collectTextures();
        void loadTexture(const std::string& modelUuid, const std::string& texturePath);
    private:
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;

        std::unordered_map<std::string, FCT::Image*> m_loadedTextures; // key: modelUuid + "|" + texturePath
    };
}

#endif //TEXTURERENDERYSTEM_H