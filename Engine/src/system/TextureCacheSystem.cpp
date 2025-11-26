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
        for (auto& pair : m_loadedTextures) {
            if (pair.second) {
              pair.second->release();
            }
        }
        m_loadedTextures.clear();
        
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
                if (textureComponent.modelUuid.empty() || textureComponent.texturePath.empty()) {
                    return;
                }

                std::string textureKey = textureComponent.modelUuid + "|" + textureComponent.texturePath;

                if (m_loadedTextures.find(textureKey) == m_loadedTextures.end()) {
                    loadTexture(textureComponent.modelUuid, textureComponent.texturePath);
                }

                auto it = m_loadedTextures.find(textureKey);
                if (it != m_loadedTextures.end()) {
                    const_cast<DiffuseTextureComponent&>(textureComponent).texture = it->second;
                }
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

    void TextureCacheSystem::loadTexture(const std::string& modelUuid, const std::string& texturePath)
    {
        std::string textureKey = modelUuid + "|" + texturePath;

        if (m_loadedTextures.find(textureKey) != m_loadedTextures.end())
        {
            return;
        }

        try
        {
            if (!texturePath.empty() && texturePath[0] == '*')
            {
                FCT::fout << "开始加载内嵌纹理: " << texturePath << std::endl;

                std::string indexStr = texturePath.substr(1);
                int textureIndex = -1;
                try
                {
                    textureIndex = std::stoi(indexStr);
                }
                catch (const std::exception& e)
                {
                    FCT::fout << "内嵌纹理索引解析失败: " << indexStr << ", 错误: " << e.what() << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }

                FCT::fout << "内嵌纹理索引: " << textureIndex << std::endl;

                m_dataManager->updateModelPathList();
                std::string modelPath = m_dataManager->getModelPathByUuid(modelUuid);
                if (modelPath.empty())
                {
                    FCT::fout << "无法找到模型UUID对应的路径: " << modelUuid << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }

                std::string modelRelativePath = m_dataManager->getModelRelativePathByUuid(modelUuid);
                if (modelRelativePath.empty())
                {
                    FCT::fout << "无法找到模型UUID对应的相对路径: " << modelUuid << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }

                std::string fullModelPath = modelPath + "/" + modelRelativePath;

                FCT::fout << "尝试从模型文件加载内嵌纹理: " << fullModelPath << ", 索引: " << textureIndex << std::endl;

                std::vector<unsigned char> textureData;

                if (m_modelLoader->getEmbeddedTextureData(fullModelPath, textureIndex, textureData))
                {
                    FCT::Image* texture = m_ctx->loadTexture(textureData.data(), textureData.size());

                    if (texture)
                    {
                        m_loadedTextures[textureKey] = texture;
                        FCT::fout << "成功加载内嵌纹理: " << texturePath << " from " << fullModelPath << std::endl;
                    }
                    else
                    {
                        FCT::fout << "从内存加载内嵌纹理失败: " << texturePath << std::endl;
                        m_loadedTextures[textureKey] = nullptr;
                    }
                }
                else
                {
                    FCT::fout << "获取内嵌纹理数据失败: " << texturePath << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                }
                return;
            }

            m_dataManager->updateModelPathList();
            std::string modelPath = m_dataManager->getModelPathByUuid(modelUuid);
            if (modelPath.empty())
            {
                FCT::fout << "无法找到模型UUID对应的路径: " << modelUuid << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            std::string modelRelativePath = m_dataManager->getModelRelativePathByUuid(modelUuid);
            if (modelRelativePath.empty())
            {
                FCT::fout << "无法找到模型UUID对应的相对路径: " << modelUuid << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            std::string fullModelPath = modelPath + "/" + modelRelativePath;
            std::filesystem::path modelFilePath(fullModelPath);
            std::filesystem::path modelDir = modelFilePath.parent_path();

            std::filesystem::path fullTexturePath = modelDir / texturePath;

            if (!std::filesystem::exists(fullTexturePath))
            {
                FCT::fout << "纹理文件不存在: " << fullTexturePath << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            FCT::Image* texture = m_ctx->loadTexture(fullTexturePath.string());

            if (texture)
            {
                m_loadedTextures[textureKey] = texture;
                FCT::fout << "成功加载纹理: " << texturePath << " from " << fullTexturePath << std::endl;
            }
            else
            {
                FCT::fout << "加载纹理失败: " << texturePath << " from " << fullTexturePath << std::endl;
                m_loadedTextures[textureKey] = nullptr;
            }
        }
        catch (const std::exception& e)
        {
            FCT::fout << "加载纹理异常: " << e.what() << std::endl;
            m_loadedTextures[textureKey] = nullptr;
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