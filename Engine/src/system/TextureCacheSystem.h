//
// Created by Administrator on 2025/1/20.
//

#ifndef TEXTURERENDERYSTEM_H
#define TEXTURERENDERYSTEM_H
#include "../data/DataManager.h"
#include "../data/Component.h"
#include <unordered_map>
#include <vector>
namespace MQEngine
{
    enum EngineTextureType
    {
        albedoTexture = entt::type_hash<AlbedoTextureComponent>::value(),
        normalTexture = entt::type_hash<NormalTextureComponent>::value(),
        emissiveTexture = entt::type_hash<EmissiveTextureComponent>::value(),
        ormTexture = entt::type_hash<OrmTextureComponent>::value()
    };
    struct TextureCacheKey
    {
        std::string modelUuid;
        std::string texturePath;
        EngineTextureType format;
        bool operator==(const TextureCacheKey& key) const
        {
            return modelUuid == key.modelUuid && texturePath == key.texturePath && format == key.format;
        }
    };
}
namespace std
{
    template<>
    struct hash<MQEngine::TextureCacheKey> {
        size_t operator()(const MQEngine::TextureCacheKey& key) const
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, std::hash<std::string>{}(key.modelUuid));
            boost::hash_combine(seed, std::hash<std::string>{}(key.texturePath));
            boost::hash_combine(seed, std::hash<uint32_t>{}(static_cast<uint32_t>(key.format)));
            return seed;
        }
    };
}
namespace MQEngine {
    inline FCT::Format GetTextureFormat(uint32_t channels,EngineTextureType type)
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
        template <class TextureComponent>
        void processTextureComponent(TextureComponent& component, EngineTextureType format);
        ~TextureCacheSystem();
        void updateLogic();
        void collectTextures();
        void loadTexture(const std::string& modelUuid, const std::string& texturePath);
    private:
        /*
         * @brief 缓存某个texture到m_newTextureCache
         */
        Status cacheTexture(const std::string& modelUuid, const std::string& texturePath, EngineTextureType format);
        /*
         * @brief 获取缓存的Texture，如果未缓存就加载
         */
        StatusOr<FCT::Image*> getOrLoadTexture(const TextureCacheKey& key);
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;
        UniquePtr<FCT::ImageLoader> m_imageLoader;
        std::unordered_map<std::string, FCT::Image*> m_loadedTextures; // key: modelUuid + "|" + texturePath
        std::unordered_map<TextureCacheKey, FCT::Image*> m_newTextureCache;
    };
}

#endif //TEXTURERENDERYSTEM_H