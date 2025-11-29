#include "TextureCacheSystem.h"
#include "../core/EngineGlobal.h"
#include "../core/TextureUtils.h"
#include "../data/DataManager.h"
#include "ResourceActiveSystem.h"
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
        m_acquireQueue = new boost::lockfree::queue<TextureLoadResult*>(1024);
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
                if (g_engineGlobal.resourceActiveSystem) {
                    g_engineGlobal.resourceActiveSystem->requestActive(&component);
                }
            }
            return;
        }

        m_newTextureCache[key] = nullptr;
        cacheTextureAsync(key);
    }
    TextureCacheSystem::~TextureCacheSystem() {
        for (auto& pair : m_newTextureCache) {
            if (pair.second) {
                pair.second->release();
            }
        }
        m_newTextureCache.clear();
        
        if (m_resultsQueue) {
            m_resultsQueue->consume_all([](TextureLoadResult* res) {
                delete res;
            });
            delete m_resultsQueue;
        }
        if (m_acquireQueue) {
            m_acquireQueue->consume_all([](TextureLoadResult* res) {
                delete res;
            });
            delete m_acquireQueue;
        }
        FCT::fout << "TextureRenderSystem 销毁" << std::endl;
    }

    void TextureCacheSystem::updateLogic() {
        processLoadedTextures();
        collectTextures();
    }

    void TextureCacheSystem::updateRender()
    {
        TextureLoadResult* res;
        while (m_acquireQueue->pop(res))
        {
            if (res->result.ok())
            {
                FCT::Image* texture = res->result.value();
                if (texture)
                {
                    m_ctx->acquireImageOwnership(texture);
                    
                    while(!m_resultsQueue->push(res)) {

                    }
                }
                else
                {
                    while(!m_resultsQueue->push(res)) {}
                }
            }
            else
            {
                while(!m_resultsQueue->push(res)) {}
            }
        }
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
            if (res->result.ok())
            {
                FCT::Image* texture = res->result.value();
                if (texture)
                {
                    m_newTextureCache[res->key] = texture;
                }
                else
                {
                    FCT::ferr << "异步加载纹理 " << res->key.texturePath << "为NULL" << std::endl;
                    m_newTextureCache.erase(res->key);
                }
            }
            else
            {
                FCT::ferr << "异步加载纹理失败: " << res->key.texturePath << " " << res->result.status().message() << std::endl;
                m_newTextureCache.erase(res->key);
            }
            delete res;
        }
    }

    void TextureCacheSystem::cacheTextureAsync(const TextureCacheKey& key)
    {
        std::thread([this, key]() {
            std::vector<unsigned char> fileData;
            bool loaded = false;
            std::string errorMsg;

            try {
                if (!key.texturePath.empty() && key.texturePath[0] == '*')
                {
                    auto dataRes = m_dataManager->extractImage(key.modelUuid, key.texturePath);
                    if (dataRes.ok()) {
                        fileData = std::move(dataRes.value());
                        loaded = true;
                    } else {
                        errorMsg = std::string(dataRes.status().message());
                    }
                }
                else
                {
                    auto pathRes = m_dataManager->getModelTexturePath(key.modelUuid, key.texturePath);
                    if (pathRes.ok()) {
                        std::ifstream file(pathRes.value(), std::ios::binary | std::ios::ate);
                        if (file) {
                            std::streamsize size = file.tellg();
                            file.seekg(0, std::ios::beg);
                            if (size > 0) {
                                fileData.resize(size);
                                if (file.read((char*)fileData.data(), size)) {
                                     loaded = true;
                                } else {
                                     errorMsg = "Read failed";
                                }
                            } else {
                                errorMsg = "File empty";
                            }
                        } else {
                             errorMsg = "Open failed: " + pathRes.value();
                        }
                    } else {
                        errorMsg = std::string(pathRes.status().message());
                    }
                }
                
                if (!loaded) {
                    TextureLoadResult* res = new TextureLoadResult{key, UnknownError(errorMsg)};
                    while(!m_resultsQueue->push(res)) std::this_thread::yield();
                    return;
                }

                auto imageData = m_imageLoader->loadFromMemory(fileData.data(), fileData.size());
                if (imageData.data.empty()) {
                    TextureLoadResult* res = new TextureLoadResult{key, UnknownError("Decode failed")};
                    while(!m_resultsQueue->push(res)) std::this_thread::yield();
                    return;
                }

                FCT::SingleBufferImage* image = new FCT::SingleBufferImage(m_ctx);
                image->width(imageData.width);
                image->height(imageData.height);
                
                bool isSRGB = true;
                FCT::Format format;
                switch (imageData.channels) {
                    case 1: format = FCT::Format::R8_UNORM; break;
                    case 2: format = FCT::Format::R8G8_UNORM; break;
                    case 3: format = isSRGB ? FCT::Format::R8G8B8A8_SRGB : FCT::Format::R8G8B8_UNORM; break;
                    case 4: format = isSRGB ? FCT::Format::R8G8B8A8_SRGB : FCT::Format::R8G8B8A8_UNORM; break;
                    default: format = FCT::Format::R8G8B8A8_SRGB; break;
                }
                
                auto dstFormat = GetTextureFormat(4, key.format);
                image->format(dstFormat); 
                image->as(FCT::ImageUsage::Texture);
                image->initData(nullptr, 0);
                
                image->create(false);
                image->uploadAsync(std::move(imageData.data), [this, key, image]() {
                    TextureLoadResult* res = new TextureLoadResult{key, image};
                    while(!m_acquireQueue->push(res)) {

                    }
                });

            } catch (const std::exception& e) {
                TextureLoadResult* res = new TextureLoadResult{key, UnknownError(e.what())};
                while(!m_resultsQueue->push(res)) std::this_thread::yield();
            }
            
        }).detach();
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