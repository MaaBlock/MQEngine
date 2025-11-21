#include "../engineapi.h"
#include "CreateApplication.h"
#include "Tech.hpp"

namespace MQEngine
{
    CreateApplicationFn GCreateApplication = nullptr;
}

#include "g_engineShaderObjectPixel.h"
#include "g_engineShaderObjectVertex.h"
#include "g_engineShaderShadowVertex.h"
#include "g_engineShaderDiffuseObjectPixel.h"
#include "g_engineShaderDiffuseObjectVertex.h"
#include "g_engineShaderNormalMapObjectPixel.h"
#include "g_engineShaderNormalMapObjectVertex.h"
#include "g_engineShaderPBRVertex.h"
#include "g_engineShaderPBRPixel.h"
#include "g_engineShaderSkyboxVertex.h"
#include "g_engineShaderSkyboxPixel.h"
#include "../data/Component.h"
#include "../data/Camera.h"
#include "./GraphicsEnv.h"
#include "../manager/RegistriesManager.h"
#include "../system/ScriptCacheSystem.h"
namespace FCT
{
    std::string LoadStringFromStringResource(const unsigned char* resource, size_t size)
    {
        return std::string(reinterpret_cast<const char*>(resource), size);
    }
}
namespace MQEngine
{
    EngineGlobal g_engineGlobal;
    void Engine::registerEnttComponents()
    {
        entt::registry dummyRegistry;
        [&]<typename... Components>(std::tuple<Components...>)
        {
            (dummyRegistry.storage<Components>(), ...);
        }(AllComponentsList{});
    }
    void Engine::settingUpEnv()
    {
        m_systemManager.init();
        g_engineGlobal.systemManager = &m_systemManager;
        m_nodeEnv = new NodeEnvironment;
        //todo:要从DataLoader里读包
        m_nodeEnv->addModulePath("./res/scripts/node_modules");
        m_nodeEnv->setup();
        g_engineGlobal.rt = &m_rt;
        m_registriesManager = makeUnique<RegistriesManager>();
        g_engineGlobal.registriesManager = m_registriesManager.get();
        m_dataManager = new DataManager();
        g_engineGlobal.dataManager = m_dataManager;
        m_wnd = m_rt.createWindow(800,600,m_application->renderConfig().windowTitle);
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->bind(m_ctx);
        m_dataManager->loadRes();
        m_autoViewport = m_wnd->getModule<WindowModule::AutoViewport>();
        m_application->global.wnd = m_wnd;
        m_application->global.ctx = m_ctx;
        m_application->global.dataManager = m_dataManager;
        m_application->global.runtime = &m_rt;
        g_engineGlobal.ctx = m_ctx;
        g_engineGlobal.dataManager = m_dataManager;
        m_cameraSystem = makeUnique<CameraSystem>(m_ctx,m_dataManager);
        m_meshRenderSystem = makeUnique<MeshCacheSystem>(m_ctx,m_dataManager);
        m_scriptSystem = makeUnique<ScriptSystem>();
        m_scriptCacheSystem = makeUnique<ScriptCacheSystem>(m_dataManager, m_scriptSystem.get());
        m_skyboxCacheSystem = makeUnique<SkyboxCacheSystem>(m_ctx, m_dataManager);
        m_matrixCacheSystem = makeUnique<MatrixCacheSystem>(m_ctx,m_dataManager);
        m_lightingSystem = makeUnique<LightingSystem>(m_ctx,m_dataManager);
        m_textureRenderSystem = makeUnique<TextureCacheSystem>(m_ctx,m_dataManager);
        m_shininessSystem = makeUnique<ShininessSystem>(m_ctx, m_dataManager);
        m_registriesManager = makeUnique<RegistriesManager>();
        m_textureSamplerSystem = makeUnique<TextureSamplerSystem>(m_ctx);
        g_engineGlobal.scriptSystem = m_scriptSystem.get();
        g_engineGlobal.scriptCacheSystem = m_scriptCacheSystem.get();
        g_engineGlobal.cameraSystem = m_cameraSystem.get();
        g_engineGlobal.lightingSystem = m_lightingSystem.get();
        g_engineGlobal.matrixCacheSystem = m_matrixCacheSystem.get();
        g_engineGlobal.textureRenderSystem = m_textureRenderSystem.get();
        g_engineGlobal.shininessSystem = m_shininessSystem.get();
        g_engineGlobal.registriesManager = m_registriesManager.get();
        
        m_systemManager.requestAddSystem("ScriptCacheSystem", m_scriptCacheSystem.get());
        m_systemManager.requestSetSystemEnabled("ScriptCacheSystem", false);
        
        m_application->init();
        m_techManager = makeUnique<TechManager>();
    }

    void Engine::settingUpTechs()
    {
        
        EntityOperationCallback universalEntityCallback = [](const EntityRenderContext& context) {
            if (context.registry.all_of<StaticMeshInstance>(context.entity))
            {
                const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
                if (meshInstance.mesh != nullptr)
                {
                    g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                    context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                }
            }
        };

        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                TechName{"BasicTech"},
                VertexShaderSource{LoadStringFromStringResource(g_engineShaderObjectVertex,g_engineShaderObjectVertexSize)},
                PixelShaderSource{LoadStringFromStringResource(g_engineShaderObjectPixel,g_engineShaderObjectPixelSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    ModelUniformSlot,
                },
                m_cameraSystem.get(),
                m_lightingSystem.get(),
                ComponentFilter{
                    .include_types = {entt::type_id<StaticMeshInstance>()},
                    .exclude_types = {
                        entt::type_id<DiffuseTextureComponent>(),
                        entt::type_id<AlbedoTextureComponent>()
                    }
                }
                .exclude<AlbedoTextureComponent>(),
                universalEntityCallback
            ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                 TechName{"DiffuseTech"},
                 VertexShaderSource{LoadStringFromStringResource(g_engineShaderDiffuseObjectVertex,g_engineShaderDiffuseObjectVertexSize)},
                 PixelShaderSource{LoadStringFromStringResource(g_engineShaderDiffuseObjectPixel,g_engineShaderDiffuseObjectPixelSize)},
                 vertexLayout,
                 pixelLayout,
                 std::vector<FCT::UniformSlot>{
                     DirectionalLightUniformSlot,
                     CameraUniformSlot,
                     ViewPosUniformSlot,
                     ShadowUniformSlot,
                     ModelUniformSlot,
                 },
                 std::vector<FCT::TextureSlot>{
                     TextureSlot{"diffuseTexture"}
                 },
                 ComponentFilter{
                     .include_types = {
                         entt::type_id<DiffuseTextureComponent>(),
                         entt::type_id<StaticMeshInstance>()
                     },
                     .exclude_types = {
                         entt::type_id<NormalTextureComponent>()
                     }
                 }
                 .exclude<AlbedoTextureComponent>(),
                 m_lightingSystem.get(),
                 m_cameraSystem.get(),
                 m_textureSamplerSystem.get(),
                 EntityOperationCallback(
                     [](const EntityRenderContext& context)
                     {
                        if (context.registry.all_of<StaticMeshInstance>(context.entity))
                        {
                            const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
                            const auto& diffuseTexture = context.registry.get<DiffuseTextureComponent>(context.entity);
                            if (diffuseTexture.texture)
                                context.layout->bindTexture("diffuseTexture", diffuseTexture.texture);
                            if (meshInstance.mesh)
                            {
                                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                                context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                            }
                        }
                    })
                    ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }

        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                TechName{"NormalMapTech"},
                VertexShaderSource{LoadStringFromStringResource(g_engineShaderNormalMapObjectVertex,g_engineShaderNormalMapObjectVertexSize)},
                PixelShaderSource{LoadStringFromStringResource(g_engineShaderNormalMapObjectPixel,g_engineShaderNormalMapObjectPixelSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    ModelUniformSlot,
                    ShininessUniformSlot
                },
                std::vector<FCT::SamplerSlot>{
                    //SamplerSlot{"shadowSampler"},
                    //SamplerSlot{"diffuseSampler"},
                    //SamplerSlot{"normalSampler"}
                },
                std::vector<FCT::TextureSlot>{
                    TextureSlot{"diffuseTexture"},
                    TextureSlot{"normalTexture"}
                },
                m_textureSamplerSystem.get(),
                m_lightingSystem.get(),
                m_cameraSystem.get(),
                ComponentFilter()
                .include<DiffuseTextureComponent,NormalTextureComponent,StaticMeshInstance>()
                .exclude<AlbedoTextureComponent>(),
                EntityOperationCallback(
                    [](const EntityRenderContext& context)
                    {
                        if (context.registry.all_of<StaticMeshInstance>(context.entity))
                        {
                            const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
                            const auto& diffuseTexture = context.registry.get<DiffuseTextureComponent>(context.entity);
                            const auto& normalMap = context.registry.get<NormalTextureComponent>(context.entity);
                            if (diffuseTexture.texture)
                                context.layout->bindTexture("diffuseTexture", diffuseTexture.texture);
                            if (normalMap.texture)
                                context.layout->bindTexture("normalTexture", normalMap.texture);

                            g_engineGlobal.shininessSystem->bindShininess(&context.registry, context.entity, context.layout);

                            if (meshInstance.mesh)
                            {
                                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                                context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                            }
                        }
                    })
            ));

            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                TechName{"NormalMapTech"},
                //VertexShaderSource{LoadStringFromStringResource(g_engineShaderNormalMapObjectVertex,g_engineShaderNormalMapObjectVertexSize)},
                //PixelShaderSource{LoadStringFromStringResource(g_engineShaderNormalMapObjectPixel,g_engineShaderNormalMapObjectPixelSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    ModelUniformSlot,
                },
                std::vector<FCT::TextureSlot>{
                    TextureSlot{"diffuseTexture"},
                    TextureSlot{"normalTexture"}
                },
                m_textureSamplerSystem.get(),
                m_lightingSystem.get(),
                m_cameraSystem.get(),
                ComponentFilter()
                .include<
                DiffuseTextureComponent,
                StaticMeshInstance,
                AlbedoTextureComponent,
                NormalTextureComponent,
                OrmTextureComponent
                >()
                .exclude<AlbedoTextureComponent>(),
                EntityOperationCallback(
                    [](const EntityRenderContext& context)
                    {
                        if (context.registry.all_of<StaticMeshInstance>(context.entity))
                        {
                            const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
                            const auto& diffuseTexture = context.registry.get<DiffuseTextureComponent>(context.entity);
                            const auto& normalMap = context.registry.get<NormalTextureComponent>(context.entity);
                            if (diffuseTexture.texture)
                                context.layout->bindTexture("diffuseTexture", diffuseTexture.texture);
                            if (normalMap.texture)
                                context.layout->bindTexture("normalTexture", normalMap.texture);

                            g_engineGlobal.shininessSystem->bindShininess(&context.registry, context.entity, context.layout);

                            if (meshInstance.mesh)
                            {
                                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                                context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                            }
                        }
                    })
            ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
        {
            auto ret = m_techManager->addTech("ShadowMapPass", Tech(
                TechName{"ShadowTech"},
                VertexShaderSource{LoadStringFromStringResource(
                    g_engineShaderShadowVertex, g_engineShaderShadowVertexSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    //ShadowUniformSlot,
                    ModelUniformSlot
                },
                ComponentFilter{
                    {entt::type_id<StaticMeshInstance>()}
                },
                m_lightingSystem.get(),
                universalEntityCallback
                ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                TechName{"PBRTech"},
                VertexShaderSource{LoadStringFromStringResource(g_engineShaderPBRVertex,g_engineShaderPBRVertexSize)},
                PixelShaderSource{LoadStringFromStringResource(g_engineShaderPBRPixel,g_engineShaderPBRPixelSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    DirectionalLightUniformSlot,
                    CameraUniformSlot,
                    ViewPosUniformSlot,
                    ShadowUniformSlot,
                    ModelUniformSlot,
                },
                std::vector<FCT::TextureSlot>{
                    TextureSlot{"albedoTexture"},
                    TextureSlot{"normalTexture"},
                    TextureSlot{"ormTexture"}
                },
                m_lightingSystem.get(),
                m_cameraSystem.get(),
                m_textureSamplerSystem.get(),
                ComponentFilter()
                .include<
                    StaticMeshInstance,
                    AlbedoTextureComponent,
                    NormalTextureComponent,
                    OrmTextureComponent
                >(),
                EntityOperationCallback(
                    [](const EntityRenderContext& context)
                    {
                        if (context.registry.all_of<StaticMeshInstance>(context.entity))
                        {
                            const auto& meshInstance = context.registry.get<StaticMeshInstance>(context.entity);
                            const auto& albedoTexture = context.registry.get<AlbedoTextureComponent>(context.entity);
                            const auto& normalMap = context.registry.get<NormalTextureComponent>(context.entity);
                            const auto& ormTexture = context.registry.get<OrmTextureComponent>(context.entity);

                            if (albedoTexture.texture)
                                context.layout->bindTexture("albedoTexture", albedoTexture.texture);
                            if (normalMap.texture)
                                context.layout->bindTexture("normalTexture", normalMap.texture);
                            if (ormTexture.texture)
                                context.layout->bindTexture("ormTexture", ormTexture.texture);

                            if (meshInstance.mesh)
                            {
                                g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);
                                context.layout->drawMesh(context.cmdBuf, meshInstance.mesh);
                            }
                        }
                    })
            ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
        {
            auto ret = m_techManager->addTech("ObjectPass", Tech(
                TechName{"SkyboxTech"},
                VertexShaderSource{LoadStringFromStringResource(g_engineShaderSkyboxVertex, g_engineShaderSkyboxVertexSize)},
                PixelShaderSource{LoadStringFromStringResource(g_engineShaderSkyboxPixel, g_engineShaderSkyboxPixelSize)},
                vertexLayout,
                pixelLayout,
                std::vector<FCT::UniformSlot>{
                    CameraUniformSlot,
                    ModelUniformSlot 
                },
                std::vector<FCT::TextureSlot>{
                    TextureSlot{"skyboxTexture", FCT::TextureType::TextureCube}
                },
                m_cameraSystem.get(),
                m_textureSamplerSystem.get(),
                ComponentFilter()
                .include<CacheSkyboxComponent>(),
                EntityOperationCallback(
                    [this](const EntityRenderContext& context)
                    {
                        if (context.registry.all_of<CacheSkyboxComponent>(context.entity))
                        {
                            const auto& skyboxComp = context.registry.get<CacheSkyboxComponent>(context.entity);
                            
                            if (skyboxComp.texture) {
                                context.layout->bindTexture("skyboxTexture", skyboxComp.texture);
                            }

                            // Bind model matrix
                            g_engineGlobal.matrixCacheSystem->bindModelMatrix(&context.registry, context.entity, context.layout);

                            if (m_skyboxMesh)
                            {
                                context.layout->drawMesh(context.cmdBuf, m_skyboxMesh);
                            }
                        }
                    })
            ));
            if (!ret.ok())
                spdlog::error("Failed to add tech: {}", ret.message());
        }
    }

    void Engine::settingUpPass()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        graph->addPass(
            "ShadowMapPass",
            EnablePassClear(ClearType::depth,1.0f),
            DepthStencil("DepthFromLigth0Image",
                2048,2048,
                Format::D32_SFLOAT
                ));
        Target SceenColorTarget;
        DepthStencil SceenDepthTarget;
        if (m_application->renderConfig().target == RenderTarget::Window)
        {
            SceenColorTarget =
                Target("SceneColorTarget", m_wnd);
            SceenDepthTarget =
                DepthStencil("SceneDepthTarget", m_wnd);
        }
        else if (m_application->renderConfig().target == RenderTarget::Texture)
        {
            SceenColorTarget =
                Target("SceneColorTarget",1024, 768, Format::R8G8B8A8_SRGB);
            SceenDepthTarget =
                DepthStencil("SceneDepthTarget",Format::D32_SFLOAT);
        }
        graph->addPass(
            "ObjectPass",
            Texture("DepthFromLigth0Image"),
            EnablePassClear(ClearType::color | ClearType::depthStencil,
                Vec4(0,0,0,1)),
            SceenColorTarget,
            Target("PosTarget"),
            Target("RetTarget"),
            SceenDepthTarget
            );
        {
            RenderCallBack::SettingUpPass callback;
            callback.graph = graph;
            m_application->renderCallBackDispatcher.trigger(callback);
        }
        graph->compile();
        {
            RenderCallBack::KeepImage callback;
            callback.graph = graph;
            m_application->renderCallBackDispatcher.trigger(callback);
        }
    }

    void Engine::settingUpResources()
    {
        m_shadowSampler = m_ctx->createResource<Sampler>();
        m_shadowSampler->setShadowMap();
        m_shadowSampler->create();

        m_diffuseSampler = m_ctx->createResource<Sampler>();
        m_diffuseSampler->setFilter(FCT::FilterMode::Linear, FCT::FilterMode::Linear, FCT::FilterMode::Nearest);
        m_diffuseSampler->setAddressMode(FCT::AddressMode::Repeat, FCT::AddressMode::Repeat, FCT::AddressMode::Repeat);
        m_diffuseSampler->create();
        
        m_skyboxMesh = new FCT::StaticMesh<uint32_t>(m_ctx, vertexLayout);
        
        std::vector<FCT::Vec3> positions = {
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}, // Front
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1}  // Back
        };
        
        for(const auto& pos : positions) {
            m_skyboxMesh->addVertex(
                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),     // Color
                FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),  // Position
                FCT::Vec2(0.0f, 0.0f),                 // TexCoord
                FCT::Vec3(0.0f, 0.0f, 1.0f),           // Normal
                FCT::Vec3(1.0f, 0.0f, 0.0f),           // Tangent
                FCT::Vec3(0.0f, 1.0f, 0.0f)            // Bitangent
            );
        }

        std::vector<uint32_t> indices = {
            0, 2, 1, 0, 3, 2, // Front
            5, 7, 6, 5, 4, 7, // Back
            3, 6, 2, 3, 7, 6, // Top
            4, 1, 5, 4, 0, 1, // Bottom
            1, 6, 5, 1, 2, 6, // Right
            4, 3, 0, 4, 7, 3  // Left
        };
        
        m_skyboxMesh->setIndices(indices);
        m_skyboxMesh->create();
    }


    void Engine::settingUpSync()
    {
        auto &syncGraph = m_ctx->syncTickers();
        syncGraph["ManagerSyncTicker"] = {
            [this]()
            {
                m_registriesManager->syncTicker();
            },
            {},
            {}
        };
        RenderCallBack::SettingSync callback{
            .graph = syncGraph
        };
        m_application->renderCallBackDispatcher.trigger(callback);
        syncGraph.update();
    }

    void Engine::settingUpSubmitTicker()
    {
        auto graph = m_ctx->getModule<RenderGraph>();

        m_ctx->pipeHub().passPipe.subscribe<PassInfo>("ShadowMapPass", [this](PassInfo& passInfo) {
            m_shadowPassOutputInfo = passInfo.outputInfo;
        });
        
        m_ctx->pipeHub().passPipe.subscribe<PassInfo>("ObjectPass", [this](PassInfo& passInfo) {
            m_objectPassOutputInfo = passInfo.outputInfo;
        });
        auto& submitTickers = m_ctx->submitTickers();
        submitTickers["MatrixCacheSystemUpdateTicker"] = {
            [this]() {
                m_matrixCacheSystem->updateRender();
                m_cameraSystem->updateRender();
                m_shininessSystem->updateUniforms();
                m_lightingSystem->updateRender();
            },
            {},
            {RenderGraphTickers::RenderGraphSubmit}
        };
        submitTickers.update();

        RenderCallBack::SubscribePass callback;
        callback.graph = graph;
        m_application->renderCallBackDispatcher.trigger(callback);

    }

    void Engine::initUniformValue()
    {
    }


    void Engine::logicTick()
    {
        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count() / 1000000.0f;
        lastFrameTime = currentTime;
        m_application->logicTick();
        m_matrixCacheSystem->updateLogic();
        m_cameraSystem->updateLogic();
        m_skyboxCacheSystem->updateLogic();
        m_meshRenderSystem->update();
        m_textureRenderSystem->updateLogic();
        m_lightingSystem->updateLogic();
        m_shininessSystem->update();
        m_scriptSystem->setLogicDeltaTime(deltaTime);
        m_systemManager.logicTick();
        m_scriptSystem->update();
        m_ctx->flush();
    }

    void Engine::init(Application* application)
    {
        s_instance = this;
        m_application = application;
        registerEnttComponents();
        settingUpEnv();
        settingUpPass();
        settingUpTechs();
        settingUpResources();
        initUniformValue();
        settingUpSubmitTicker();
        settingUpSync();

        if (m_scriptSystem) {
            m_scriptSystem->loadScripts();
            m_scriptSystem->start();
        }
    }

    void Engine::loop()
    {
        while (m_wnd->isRunning())
        {
            logicTick();
        }
    }

    void Engine::term()
    {
        if (m_scriptSystem) {
            m_scriptSystem->cleanUp();
        }
        RenderCallBack::WindowClose callback;
        m_application->renderCallBackDispatcher.trigger(callback);

        m_nodeEnv->stop();
        delete m_nodeEnv;
        m_ctx->release();
        m_wnd->release();
        m_systemManager.term();
        NodeCommon::Term();
        s_instance = nullptr;
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }

    Engine::Engine() {
        //s_instance = this;
    }

    Engine::~Engine() {
        //s_instance = nullptr;
    }

    void Engine::RegisterApplicationFactory(CreateApplicationFn fn)
    {
        GCreateApplication = fn;
    }
}
