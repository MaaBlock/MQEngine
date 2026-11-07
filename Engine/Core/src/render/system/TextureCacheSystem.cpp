#include "TextureCacheSystem.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../core/TextureUtils.h"
#include "../../scene/manager/DataManager.h"
#include "../../base/JobSystem.h"
#include "../../resource/system/ResourceActiveSystem.h"
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
        initDefaultTextures();
        FCT::fout << "TextureRenderSystem 初始化" << std::endl;
    }

    FCT::Image* TextureCacheSystem::createSolidColorTexture(
        uint8_t r, uint8_t g, uint8_t b, uint8_t a, FCT::Format format)
    {
        if (!m_ctx)
            return nullptr;
        uint8_t pixel[4] = {r, g, b, a};
        auto* image = new FCT::SingleBufferImage(m_ctx);
        image->width(1);
        image->height(1);
        image->format(format);
        image->as(FCT::ImageUsage::Texture);
        image->initData(pixel, sizeof(pixel));
        image->create(true);
        return image;
    }

    void TextureCacheSystem::initDefaultTextures()
    {
        m_defaultWhiteTexture = createSolidColorTexture(255, 255, 255, 255, FCT::Format::R8G8B8A8_SRGB);
        m_defaultNormalTexture = createSolidColorTexture(128, 128, 255, 255, FCT::Format::R8G8B8A8_UNORM);
        m_defaultOrmTexture = createSolidColorTexture(255, 128, 0, 255, FCT::Format::R8G8B8A8_UNORM);
        m_defaultBlackTexture = createSolidColorTexture(0, 0, 0, 255, FCT::Format::R8G8B8A8_SRGB);
    }

    template<typename TextureComponent>
       void TextureCacheSystem::processTextureComponent(TextureComponent& component, EngineTextureType format)
    {
        if (component.texture || component.texturePath.empty())
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
        if (m_defaultWhiteTexture)
            m_defaultWhiteTexture->release();
        if (m_defaultNormalTexture)
            m_defaultNormalTexture->release();
        if (m_defaultOrmTexture)
            m_defaultOrmTexture->release();
        if (m_defaultBlackTexture)
            m_defaultBlackTexture->release();

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
        ZoneScoped();
        processLoadedTextures();
        collectTextures();
    }

    void TextureCacheSystem::updateRender()
    {
        ZoneScoped();
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
        ZoneScoped();
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
        ZoneScoped();
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
        ZoneScoped();

        auto asyncLoadTask = [this, key]() {
            ZoneScopedN("TextureCacheSystem::AsyncLoadTask");
            ZoneText(key.texturePath.c_str(), key.texturePath.size());
            std::vector<unsigned char> fileData;
            bool loaded = false;
            std::string errorMsg;

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
                            fileData.resize(static_cast<size_t>(size));
                            if (file.read(reinterpret_cast<char*>(fileData.data()), size)) {
                                loaded = true;
                            } else {
                                errorMsg = "文件读取失败";
                            }
                        } else {
                            errorMsg = "文件为空";
                        }
                    } else {
                        errorMsg = "打开文件失败: " + pathRes.value();
                    }
                } else {
                    errorMsg = std::string(pathRes.status().message());
                }
            }

            if (!loaded) {
                auto* res = new TextureLoadResult{key, UnknownError(errorMsg)};
                while (!m_resultsQueue->push(res)) {
                    std::this_thread::yield();
                }
                return;
            }

            auto imageData = m_imageLoader->loadFromMemory(fileData.data(), fileData.size());
            if (imageData.data.empty()) {
                auto* res = new TextureLoadResult{key, UnknownError("解码图像数据失败")};
                while (!m_resultsQueue->push(res)) {
                    std::this_thread::yield();
                }
                return;
            }

            auto* image = new FCT::SingleBufferImage(m_ctx);
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
                auto* res = new TextureLoadResult{key, image};
                while (!m_acquireQueue->push(res)) {
                    std::this_thread::yield();
                }
            });
        };

        if (g_engineGlobal.jobSystem) {
            g_engineGlobal.jobSystem->dispatch(std::move(asyncLoadTask), JobPriority::Normal);
        } else {
            ThreadPool::globalInstance().dispatch(std::move(asyncLoadTask), JobPriority::Normal);
        }
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