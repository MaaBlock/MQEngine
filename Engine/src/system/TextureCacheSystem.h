//
// Created by Administrator on 2025/1/20.
//

#ifndef TEXTURERENDERYSTEM_H
#define TEXTURERENDERYSTEM_H
#include "../data/Component.h"
#include "./CacheSystem.h"
#include <unordered_map>
#include <vector>
#include <boost/lockfree/queue.hpp>

namespace MQEngine
{
    enum EngineTextureType
    {
        albedoTexture = entt::type_hash<AlbedoTextureComponent>::value(),
        diffuseTexture = entt::type_hash<DiffuseTextureComponent>::value(),
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
    
    struct TextureLoadResult {
        TextureCacheKey key;
        StatusOr<FCT::Image*> result;
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
        case diffuseTexture:
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
    class ENGINE_API TextureCacheSystem : public CacheSystem {
    public:
        TextureCacheSystem(FCT::Context* ctx, DataManager* dataManager);
        template <class TextureComponent>
        void processTextureComponent(TextureComponent& component, EngineTextureType format);
        ~TextureCacheSystem();
        void updateLogic();
        void collectTextures();
    private:
        /*
         * @brief 异步加载纹理数据，完成后放入队列
         */
        void cacheTextureAsync(const TextureCacheKey& key);
        
        /*
         * @brief 同步加载纹理 (在异步线程中调用，复用旧逻辑)
         */
        StatusOr<FCT::Image*> loadTextureSync(const TextureCacheKey& key);

        /*
         * @brief 主线程处理加载完成的队列，创建GPU资源
         */
        void processLoadedTextures();

        /*
         * @brief 获取缓存的Texture，如果未缓存就加载
         */
        StatusOr<FCT::Image*> getOrLoadTexture(const TextureCacheKey& key);
        FCT::Context* m_ctx;
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;
        UniquePtr<FCT::ImageLoader> m_imageLoader;
        std::unordered_map<TextureCacheKey, FCT::Image*> m_newTextureCache;
        boost::lockfree::queue<TextureLoadResult*>* m_resultsQueue;
    };
}

#endif //TEXTURERENDERYSTEM_H