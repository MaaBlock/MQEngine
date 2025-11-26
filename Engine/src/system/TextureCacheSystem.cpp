#ifndef ENGINE_TEXTURE_RENDER_SYSTEM_CPP
#define ENGINE_TEXTURE_RENDER_SYSTEM_CPP

#include "TextureCacheSystem.h"
#include "../core/EngineGlobal.h"
#include "../core/TextureUtils.h"
#include "../data/DataManager.h"
#include <filesystem>
#include <thread>
#include <fstream>

namespace MQEngine {

    TextureCacheSystem::TextureCacheSystem(FCT::Context* ctx, DataManager* dataManager)
    : m_ctx(ctx), m_dataManager(dataManager)
    {
        m_modelLoader = g_engineGlobal.rt->createModelLoader();
        m_imageLoader = UniquePtr(g_engineGlobal.rt->createImageLoader());
        m_resultsQueue = new boost::lockfree::queue<TextureLoadResult*>(1024);
        FCT::fout << "TextureRenderSystem 初始化" << std::endl;
    }
    template<typename TextureComponent>
       void TextureCacheSystem::processTextureComponent(TextureComponent& component, EngineTextureType format)
    {
        if (component.texture)
            return;

        TextureCacheKey key = {
            .modelUuid = component.modelUuid,
            .texturePath = component.texturePath,
            .format = format,
        };

        if (m_newTextureCache.count(key))
        {
            auto* texture = m_newTextureCache[key];
            if (texture)
            {
                component.texture = texture;
            }
            return;
        }

        m_newTextureCache[key] = nullptr;
        cacheTextureAsync(key);
    }
    TextureCacheSystem::~TextureCacheSystem() {
        
        if (m_resultsQueue) {
            m_resultsQueue->consume_all([](TextureLoadResult* res) {
                delete res;
            });
            delete m_resultsQueue;
        }
        FCT::fout << "TextureRenderSystem 销毁" << std::endl;
    }

    void TextureCacheSystem::updateLogic() {
        processLoadedTextures();
        collectTextures();
    }

    void TextureCacheSystem::collectTextures() {
        
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            registry->view<DiffuseTextureComponent>().each([this](DiffuseTextureComponent& textureComponent)
            {
                processTextureComponent(textureComponent, diffuseTexture);
            });
            registry->view<AlbedoTextureComponent>().each([this](AlbedoTextureComponent& component)
            {
                processTextureComponent(component, albedoTexture);
            });
            registry->view<NormalTextureComponent>().each([this](NormalTextureComponent& component)
            {
                processTextureComponent(component, normalTexture);
            });

            registry->view<EmissiveTextureComponent>().each([this](EmissiveTextureComponent& component)
            {
                processTextureComponent(component, emissiveTexture);
            });

            registry->view<OrmTextureComponent>().each([this](OrmTextureComponent& component)
            {
                processTextureComponent(component, ormTexture);
            });
        }
    }

    void TextureCacheSystem::processLoadedTextures()
    {
        TextureLoadResult* res;
        while (m_resultsQueue->pop(res))
        {
            if (!res->result.ok())
            {
                FCT::ferr << "异步加载纹理失败: " << res->key.texturePath << " "
                          << res->result.status().message() << std::endl;
                m_newTextureCache.erase(res->key);
                delete res;
                continue;
            }
            Image* texture = res->result.value();
            if (!texture)
            {
                FCT::ferr << "异步加载纹理 " << res->key.texturePath << "为NULL" << std::endl;
                m_newTextureCache.erase(res->key);
                delete res;
                continue;
            }
            m_newTextureCache[res->key] = texture;
            delete res;
        }
    }

    void TextureCacheSystem::cacheTextureAsync(const TextureCacheKey& key)
    {
        std::thread([this, key]() {
            TextureLoadResult* result = new TextureLoadResult();
            result->key = key;
            result->result = loadTextureSync(key);
            while (!m_resultsQueue->push(result)) {
                std::this_thread::yield();
            }
        }).detach();
    }
    StatusOr<FCT::Image*> TextureCacheSystem::loadTextureSync(const TextureCacheKey& key)
    {
        if (!key.texturePath.empty() && key.texturePath[0] == '*')
        {
            auto data = m_dataManager->extractImage(key.modelUuid, key.texturePath);
            CHECK_STATUS(data);
            auto dstFormat = GetTextureFormat(4, key.format);
            FCT::Image* texture = m_ctx->loadTexture(data.value(), dstFormat);
            if (!texture)
                return UnknownError("纹理读取失败");
            return texture;
        }
        auto path = m_dataManager->getModelTexturePath(key.modelUuid, key.texturePath);
        CHECK_STATUS(path);
        auto dstFormat = GetTextureFormat(4, key.format);
        FCT::Image* texture = m_ctx->loadTexture(path.value(), dstFormat);
        if (!texture)
            return UnknownError("从纹理路径 " + path.value() + " 读取失败");
        return texture;
    }
    StatusOr<FCT::Image*> TextureCacheSystem::getOrLoadTexture(const TextureCacheKey& key)
    {
        if (m_newTextureCache.count(key))
            return m_newTextureCache[key];
        m_newTextureCache[key] = nullptr;
        cacheTextureAsync(key);
        return m_newTextureCache[key];
    }
} // namespace MQEngine

#endif // ENGINE_TEXTURE_RENDER_SYSTEM_CPP