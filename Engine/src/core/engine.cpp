#include "../engineapi.h"
#include "Tech.hpp"
#include "g_engineShaderObjectPixel.h"
#include "g_engineShaderObjectVertex.h"
#include "g_engineShaderShadowVertex.h"
#include "g_engineShaderDiffuseObjectPixel.h"
#include "g_engineShaderDiffuseObjectVertex.h"
#include "../data/Component.h"
#include "../data/Camera.h"
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
    void Engine::settingUpEnv()
    {
        m_systemManager.init();
        m_nodeEnv = new NodeEnvironment;
        m_nodeEnv->addModulePath("./node_modules");
        m_nodeEnv->setup();
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
        g_engineGlobal.rt = &m_rt;
        g_engineGlobal.dataManager = m_dataManager;
        m_application->init();
        m_cameraSystem = makeUnique<CameraSystem>(m_ctx,m_dataManager);
        m_meshRenderSystem = makeUnique<MeshCacheSystem>(m_ctx,m_dataManager);
        m_scriptSystem = makeUnique<ScriptSystem>();
        m_matrixCacheSystem = makeUnique<MatrixCacheSystem>(m_ctx,m_dataManager);
        m_lightingSystem = makeUnique<LightingSystem>(m_ctx,m_dataManager);
        m_textureRenderSystem = makeUnique<TextureCacheSystem>(m_ctx,m_dataManager);
        g_engineGlobal.scriptSystem = m_scriptSystem.get();
        g_engineGlobal.cameraSystem = m_cameraSystem.get();
        g_engineGlobal.lightingSystem = m_lightingSystem.get();
        g_engineGlobal.matrixCacheSystem = m_matrixCacheSystem.get();
        g_engineGlobal.textureRenderSystem = m_textureRenderSystem.get();
        m_techManager = makeUnique<TechManager>();
    }

    void Engine::settingUpTechs()
    {
        TechBindCallback objectPassCallback = [this](FCT::Layout* layout, const std::string& techName, const std::string& passName) {
            layout->bindSampler("shadowSampler", m_shadowSampler);
            g_engineGlobal.cameraSystem->bind(layout);
            g_engineGlobal.lightingSystem->bind(layout);
        };

        TechBindCallback shadowPassCallback = [](FCT::Layout* layout, const std::string& techName, const std::string& passName) {
            g_engineGlobal.lightingSystem->bind(layout);
        };
        
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
        
        m_techManager->addTech("ObjectPass", Tech(
            TechName{"BasicTech"},
            VertexShaderSource{LoadStringFromStringResource(g_engineShaderObjectVertex,g_engineShaderObjectVertexSize)},
            PixelShaderSource{LoadStringFromStringResource(g_engineShaderObjectPixel,g_engineShaderObjectPixelSize)},
            vertexLayout,
            pixelLayout,
            std::vector<FCT::UniformSlot>{
                DirectionalLightUniformSlot,
                CameraUniformSlot,
                ViewPosUniformSlot,
                ShadowUniformSlot,
                ModelUniformSlot,
            },
            std::vector<FCT::SamplerSlot>{
                SamplerSlot{"shadowSampler"}
            },
            ComponentFilter{
                {entt::type_id<StaticMeshInstance>()},
                {entt::type_id<DiffuseTextureComponent>()}
            },
            objectPassCallback,
            universalEntityCallback
        ));
        
        m_techManager->addTech("ObjectPass", Tech(
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
             std::vector<FCT::SamplerSlot>{
                 SamplerSlot{"shadowSampler"},
                 SamplerSlot{"diffuseSampler"}
             },
             std::vector<FCT::TextureSlot>{
                 TextureSlot{"diffuseTexture"}
             },
             ComponentFilter{
                 {
                     entt::type_id<DiffuseTextureComponent>(),
                     entt::type_id<StaticMeshInstance>()
                 }
             },
             objectPassCallback,
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
        
        m_techManager->addTech("ShadowMapPass", Tech(
            TechName{"ShadowTech"},
            VertexShaderSource{LoadStringFromStringResource(
                g_engineShaderShadowVertex, g_engineShaderShadowVertexSize)},
            vertexLayout,
            pixelLayout,
            std::vector<FCT::UniformSlot>{
                ShadowUniformSlot,
                ModelUniformSlot
            },
            ComponentFilter{
                {entt::type_id<StaticMeshInstance>()}
            },
            shadowPassCallback,
            universalEntityCallback
        ));
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
                Target("SceneColorTarget",1024, 768, Format::R8G8B8A8_UNORM);
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

        // Shadow and lighting uniforms are now handled by LightingSystem
        m_floorModelUniform = Uniform(m_ctx,ModelUniformSlot);
    }


    void Engine::settingUpSync()
    {
        auto &syncGraph = m_ctx->syncTickers();
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
                m_matrixCacheSystem->updateUniforms();
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
        m_floorModelUniform.setValue("modelMatrix", FCT::Mat4());
        m_floorModelUniform.update();
    }


    void Engine::logicTick()
    {
        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count() / 1000000.0f;
        lastFrameTime = currentTime;
        m_application->logicTick();
        m_matrixCacheSystem->update();
        m_cameraSystem->update();
        m_meshRenderSystem->update();
        m_textureRenderSystem->update();
        m_lightingSystem->update();
        m_scriptSystem->setLogicDeltaTime(deltaTime);
        m_scriptSystem->update();
        
        m_ctx->flush();
    }

    void Engine::init(Application* application)
    {
        m_application = application;
        settingUpEnv();
        settingUpPass();
        settingUpTechs();
        settingUpResources();
        initUniformValue();
        settingUpSubmitTicker();
        settingUpSync();

        if (m_scriptSystem) {
            m_scriptSystem->loadScripts();
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
        m_nodeEnv->stop();
        delete m_nodeEnv;
        m_ctx->release();
        m_wnd->release();
        m_systemManager.term();
        NodeCommon::Term();
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }
}
