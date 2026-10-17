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
#include "g_engineShaderToneMappingVertex.h"
#include "g_engineShaderToneMappingPixel.h"
#include "../data/Component.h"
#include "../data/Camera.h"
#include "./GraphicsEnv.h"
#include "../manager/RegistriesManager.h"
#include "../manager/ShaderGraphManager.h"
#include "../system/ScriptCacheSystem.h"
#include "../data/DiskResourceLoader.h"
#include "../manager/PipelineManager.h"
#include "../manager/ProjectManager.h"
#include "../plugin/PluginManager.h"
#include "../system/ComponentReflection.h"
#include "CommandLineArgs.h"
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
        m_nodeEnv = new FCT::JS::NodeEnvironment;
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
        m_inputSystem = new InputSystem();
        m_skyboxCacheSystem = makeUnique<SkyboxCacheSystem>(m_ctx, m_dataManager);
        m_matrixCacheSystem = makeUnique<MatrixCacheSystem>(m_ctx,m_dataManager);
        m_lightingSystem = makeUnique<LightingSystem>(m_ctx,m_dataManager);
        m_textureRenderSystem = makeUnique<TextureCacheSystem>(m_ctx,m_dataManager);
        m_shininessSystem = makeUnique<ShininessSystem>(m_ctx, m_dataManager);
        m_registriesManager = makeUnique<RegistriesManager>();
        m_textureSamplerSystem = makeUnique<TextureSamplerSystem>(m_ctx);
        g_engineGlobal.scriptSystem = m_scriptSystem.get();
        g_engineGlobal.scriptCacheSystem = m_scriptCacheSystem.get();
        g_engineGlobal.inputSystem = m_inputSystem;
        g_engineGlobal.cameraSystem = m_cameraSystem.get();
        g_engineGlobal.lightingSystem = m_lightingSystem.get();
        g_engineGlobal.matrixCacheSystem = m_matrixCacheSystem.get();
        g_engineGlobal.textureRenderSystem = m_textureRenderSystem.get();
        g_engineGlobal.shininessSystem = m_shininessSystem.get();
        g_engineGlobal.registriesManager = m_registriesManager.get();
        
        m_systemManager.requestAddSystem("ScriptCacheSystem", m_scriptCacheSystem.get());
        m_systemManager.requestSetSystemEnabled("ScriptCacheSystem", false);
        m_systemManager.requestAddSystem("InputSystem", m_inputSystem);
        
        m_systemManager.requestAddSystem("CameraSystem", m_cameraSystem.get());
      //  m_systemManager.requestSetSystemEnabled("CameraSystem", false);
        m_systemManager.requestAddSystem("LightingSystem", m_lightingSystem.get());
        //m_systemManager.requestSetSystemEnabled("LightingSystem", false);
        m_systemManager.requestAddSystem("TextureSamplerSystem", m_textureSamplerSystem.get());
       // m_systemManager.requestSetSystemEnabled("TextureSamplerSystem", false);
        
        m_resourceLoader = makeUnique<DiskResourceLoader>();
        g_engineGlobal.resourceLoader = m_resourceLoader.get();

        m_shaderSnippetManager = makeUnique<ShaderSnippetManager>();
        g_engineGlobal.shaderSnippetManager = m_shaderSnippetManager.get();

        m_shaderGraphManager = makeUnique<ShaderGraphManager>();
        g_engineGlobal.shaderGraphManager = m_shaderGraphManager.get();
        
        m_shaderGraphManager->registerVertexLayout("StandardMeshVertexLayout", StandardMeshVertexLayout);
        m_shaderGraphManager->registerPixelLayout("StandardPixelLayout", pixelLayout); // Assuming 'pixelLayout' exists in Engine members or global

        ShaderGraphConfig sgConfig = m_shaderGraphManager->getConfig();
        sgConfig.bindedSystemNames = {"CameraSystem", "LightingSystem", "TextureSamplerSystem"};
        m_shaderGraphManager->setConfig(sgConfig);

        m_application->init();
        m_techManager = makeUnique<TechManager>();
        
        m_shaderFileWatcher = makeUnique<ShaderFileWatcher>(m_scriptSystem->getNodeEnvironment(), m_shaderSnippetManager.get());
        auto snippetStatus = m_shaderSnippetManager->loadSnippetFromResource();
        if (!snippetStatus.ok()) {
            spdlog::warn("加载着色器片段失败: {}", snippetStatus.message());
        }
        m_shaderFileWatcher->startWatching("./res/snippets/");

        registerShaderSnippets();
    }

    void Engine::settingUpTechs()
    {
        if (!m_pipelineManager) {
            m_pipelineManager = makeUnique<PipelineManager>();
        }

        const auto& config = m_pipelineManager->getConfig();
        auto buildRes = m_pipelineManager->buildTechManager(config, m_techManager.get());
        if (!buildRes.ok()) {
            spdlog::error("通过 PipelineManager 构建 TechManager 失败: {}", buildRes.message());
        }
    }

    void Engine::settingUpPass()
    {
        auto graph = m_ctx->getModule<RenderGraph>();
        if (!m_pipelineManager) {
            m_pipelineManager = makeUnique<PipelineManager>();
        }

        auto loadRes = m_pipelineManager->loadPipeline("res/pipelines/default_pipeline.json");
        if (loadRes.ok()) {
            auto buildRes = m_pipelineManager->buildRenderGraph(loadRes.value(), graph, m_wnd, m_application->renderConfig().target);
            if (!buildRes.ok()) {
                spdlog::error("通过 PipelineManager 构建 RenderGraph 失败: {}", buildRes.message());
            }
        } else {
            spdlog::error("加载默认管线配置失败: {}", loadRes.status().message());
        }

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
        
        m_resourceActiveSystem = makeUnique<ResourceActiveSystem>(m_dataManager);
        g_engineGlobal.resourceActiveSystem = m_resourceActiveSystem.get();
        m_systemManager.requestAddSystem("ResourceActiveSystem", m_resourceActiveSystem.get());
        
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

        m_fullScreenMesh = new FCT::StaticMesh<uint32_t>(m_ctx, vertexLayout);
        std::vector<FCT::Vec3> fullScreenQuadPositions = {
            {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0}
        };
        for(const auto& pos : fullScreenQuadPositions) {
            m_fullScreenMesh->addVertex(
                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),     // Color
                FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),  // Position
                FCT::Vec2(pos.x * 0.5f + 0.5f, 1.0f - (pos.y * 0.5f + 0.5f)), // TexCoord
                FCT::Vec3(0.0f, 0.0f, 1.0f),           // Normal
                FCT::Vec3(1.0f, 0.0f, 0.0f),           // Tangent
                FCT::Vec3(0.0f, 1.0f, 0.0f)            // Bitangent
            );
        }
        std::vector<uint32_t> fullScreenQuadIndices = {
            0, 2, 1, 0, 3, 2
        };
        m_fullScreenMesh->setIndices(fullScreenQuadIndices);
        m_fullScreenMesh->create();
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
        (void)syncGraph.update();
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
                m_resourceActiveSystem->updateRender();
                m_textureRenderSystem->updateRender(); // Added: Process Acquire Barriers
                m_matrixCacheSystem->updateRender();
                //m_cameraSystem->updateRender();
                m_shininessSystem->updateUniforms();
                m_systemManager.renderTick();
                if (m_projectManager && m_projectManager->getPluginManager()) {
                    m_projectManager->getPluginManager()->renderTick();
                }
                //m_lightingSystem->updateRender();
            },
            {},
            {RenderGraphTickers::RenderGraphSubmit}
        };
        (void)submitTickers.update();

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
        //m_cameraSystem->updateLogic();
        m_skyboxCacheSystem->updateLogic();
        m_meshRenderSystem->update();
        m_textureRenderSystem->updateLogic();
        //m_lightingSystem->updateLogic();
        m_shininessSystem->update();
        if (m_shaderFileWatcher) {
            m_shaderFileWatcher->update();
        }
        m_scriptSystem->setLogicDeltaTime(deltaTime);
        m_systemManager.logicTick();
        if (m_projectManager && m_projectManager->getPluginManager()) {
            m_projectManager->getPluginManager()->logicTick(deltaTime);
        }
        m_scriptSystem->update();
        m_ctx->flush();
    }

    namespace
    {
        void AlignWorkingDirectoryForInit()
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if (CommandLineArgs::instance().hasProject())
            {
                fs::path projPath = fs::u8path(CommandLineArgs::instance().getProjectPath());
                if (fs::exists(projPath, ec))
                {
                    fs::path absProj = fs::weakly_canonical(projPath, ec);
                    if (!ec)
                    {
                        CommandLineArgs::instance().setProjectPath(
                            std::string(reinterpret_cast<const char*>(absProj.generic_u8string().c_str())));
                        fs::path projDir = absProj.parent_path();
                        if (fs::is_directory(projDir, ec))
                            fs::current_path(projDir, ec);
                    }
                }
                return;
            }

            fs::path cur = fs::current_path(ec);
            if (ec || fs::exists(cur / "res", ec) || fs::exists(cur / "Res", ec))
                return;

            fs::path probe = cur;
            for (int depth = 0; depth < 6 && probe.has_parent_path(); ++depth)
            {
                if (fs::exists(probe / "Res" / "pipelines" / "default_pipeline.json", ec))
                {
                    fs::current_path(probe, ec);
                    return;
                }
                if (probe == probe.parent_path())
                    break;
                probe = probe.parent_path();
            }
        }
    }

    void Engine::init(Application* application)
    {
        s_instance = this;
        m_application = application;
        AlignWorkingDirectoryForInit();
        m_pipelineManager = makeUnique<PipelineManager>();
        m_componentReflection = makeUnique<ComponentReflection>();
        registerEnttComponents();
        settingUpEnv();
        m_projectManager = makeUnique<ProjectManager>(m_resourceLoader.get());
        settingUpResources();
        settingUpPass();
        settingUpTechs();
        initUniformValue();
        settingUpSubmitTicker();
        settingUpSync();

        if (CommandLineArgs::instance().hasProject()) {
            auto st = m_projectManager->openProject(CommandLineArgs::instance().getProjectPath(), this);
            if (!st.ok()) {
                spdlog::error("初始化时打开工程失败 [{}]: {}", CommandLineArgs::instance().getProjectPath(), st.message());
            }
        }

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
        if (m_inputSystem) {
            // InputSystem inherits from RefCount, but we manage its lifecycle here as a system
            // Ensure it's unregistered
            m_inputSystem->onDeactivate();
            // Assuming initial ref count allows us to delete or release. 
            // If RefCount is strict, we might need m_inputSystem->release();
            // For now, using delete as we used 'new'.
            m_inputSystem->release();
            m_inputSystem = nullptr;
        }
        RenderCallBack::WindowClose callback;
        m_application->renderCallBackDispatcher.trigger(callback);

        m_nodeEnv->stop();
        delete m_nodeEnv;
        m_ctx->release();
        m_wnd->release();
        if (m_projectManager) {
            (void)m_projectManager->closeProject(this);
        }
        m_systemManager.term();
        FCT::JS::NodeCommon::Term();
        if (m_dataManager) {
            delete m_dataManager;
            m_dataManager = nullptr;
            g_engineGlobal.dataManager = nullptr;
        }
        s_instance = nullptr;
    }

    PluginManager* Engine::getPluginManager() const {
        return m_projectManager ? m_projectManager->getPluginManager() : nullptr;
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }

    Engine::Engine() {
        //s_instance = this;
#ifdef FCT_DEBUG
        spdlog::set_level(spdlog::level::debug);
#endif
    }

    Engine::~Engine() {
        //s_instance = nullptr;
    }

    void Engine::RegisterApplicationFactory(CreateApplicationFn fn)
    {
        GCreateApplication = fn;
    }
}
