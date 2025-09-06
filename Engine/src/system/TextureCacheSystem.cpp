#ifndef ENGINE_TEXTURE_RENDER_SYSTEM_CPP
#define ENGINE_TEXTURE_RENDER_SYSTEM_CPP

#include "TextureCacheSystem.h"
#include "../core/EngineGlobal.h"
#include "../core/TextureUtils.h"
#include <filesystem>

namespace MQEngine {

    TextureCacheSystem::TextureCacheSystem(FCT::Context* ctx, DataManager* dataManager)
    : m_ctx(ctx), m_dataManager(dataManager)
{
    m_modelLoader = g_engineGlobal.rt->createModelLoader();
        FCT::fout << "TextureRenderSystem 初始化" << std::endl;
    }

    TextureCacheSystem::~TextureCacheSystem() {
        for (auto& pair : m_loadedTextures) {
            if (pair.second) {
              pair.second->release();
            }
        }
        m_loadedTextures.clear();
        FCT::fout << "TextureRenderSystem 销毁" << std::endl;
    }

    void TextureCacheSystem::update() {
        collectTextures();
    }

    void TextureCacheSystem::collectTextures() {
        
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<DiffuseTextureComponent>();
            
            for (auto entity : view) {
                auto& textureComponent = view.get<DiffuseTextureComponent>(entity);
                
                if (textureComponent.modelUuid.empty() || textureComponent.texturePath.empty()) {
                    continue;
                }

                std::string textureKey = textureComponent.modelUuid + "|" + textureComponent.texturePath;

                if (m_loadedTextures.find(textureKey) == m_loadedTextures.end()) {
                    loadTexture(textureComponent.modelUuid, textureComponent.texturePath);
                }

                auto it = m_loadedTextures.find(textureKey);
                if (it != m_loadedTextures.end()) {
                    const_cast<DiffuseTextureComponent&>(textureComponent).texture = it->second;
                }
            }
        }
    }

    void TextureCacheSystem::loadTexture(const std::string& modelUuid, const std::string& texturePath) {
        std::string textureKey = modelUuid + "|" + texturePath;

        if (m_loadedTextures.find(textureKey) != m_loadedTextures.end()) {
            return;
        }
        
        try {
            if (!texturePath.empty() && texturePath[0] == '*') {
                FCT::fout << "开始加载内嵌纹理: " << texturePath << std::endl;

                std::string indexStr = texturePath.substr(1);
                int textureIndex = -1;
                try {
                    textureIndex = std::stoi(indexStr);
                } catch (const std::exception& e) {
                    FCT::fout << "内嵌纹理索引解析失败: " << indexStr << ", 错误: " << e.what() << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }
                
                FCT::fout << "内嵌纹理索引: " << textureIndex << std::endl;

                m_dataManager->updateModelPathList();
                std::string modelPath = m_dataManager->getModelPathByUuid(modelUuid);
                if (modelPath.empty()) {
                    FCT::fout << "无法找到模型UUID对应的路径: " << modelUuid << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }

                std::string modelRelativePath = m_dataManager->getModelRelativePathByUuid(modelUuid);
                if (modelRelativePath.empty()) {
                    FCT::fout << "无法找到模型UUID对应的相对路径: " << modelUuid << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                    return;
                }

                std::string fullModelPath = modelPath + "/" + modelRelativePath;
                
                FCT::fout << "尝试从模型文件加载内嵌纹理: " << fullModelPath << ", 索引: " << textureIndex << std::endl;

                std::vector<unsigned char> textureData;
                
                if (m_modelLoader->getEmbeddedTextureData(fullModelPath, textureIndex, textureData)) {
                    FCT::Image* texture = m_ctx->loadTexture(textureData.data(), textureData.size());
                    
                    if (texture) {
                        m_loadedTextures[textureKey] = texture;
                        FCT::fout << "成功加载内嵌纹理: " << texturePath << " from " << fullModelPath << std::endl;
                    } else {
                        FCT::fout << "从内存加载内嵌纹理失败: " << texturePath << std::endl;
                        m_loadedTextures[textureKey] = nullptr;
                    }
                } else {
                    FCT::fout << "获取内嵌纹理数据失败: " << texturePath << std::endl;
                    m_loadedTextures[textureKey] = nullptr;
                }
                return;
            }

            m_dataManager->updateModelPathList();
            std::string modelPath = m_dataManager->getModelPathByUuid(modelUuid);
            if (modelPath.empty()) {
                FCT::fout << "无法找到模型UUID对应的路径: " << modelUuid << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            std::string modelRelativePath = m_dataManager->getModelRelativePathByUuid(modelUuid);
            if (modelRelativePath.empty()) {
                FCT::fout << "无法找到模型UUID对应的相对路径: " << modelUuid << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            std::string fullModelPath = modelPath + "/" + modelRelativePath;
            std::filesystem::path modelFilePath(fullModelPath);
            std::filesystem::path modelDir = modelFilePath.parent_path();

            std::filesystem::path fullTexturePath = modelDir / texturePath;

            if (!std::filesystem::exists(fullTexturePath)) {
                FCT::fout << "纹理文件不存在: " << fullTexturePath << std::endl;
                m_loadedTextures[textureKey] = nullptr;
                return;
            }

            FCT::Image* texture = m_ctx->loadTexture(fullTexturePath.string());
            
            if (texture) {
                m_loadedTextures[textureKey] = texture;
                FCT::fout << "成功加载纹理: " << texturePath << " from " << fullTexturePath << std::endl;
            } else {
                FCT::fout << "加载纹理失败: " << texturePath << " from " << fullTexturePath << std::endl;
                m_loadedTextures[textureKey] = nullptr;
            }
            
        } catch (const std::exception& e) {
            FCT::fout << "加载纹理异常: " << e.what() << std::endl;
            m_loadedTextures[textureKey] = nullptr;
        }
    }



}

#endif // ENGINE_TEXTURE_RENDER_SYSTEM_CPP