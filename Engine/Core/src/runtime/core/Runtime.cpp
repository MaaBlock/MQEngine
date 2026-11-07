//
// Created by MaaBlock on 2026/11/6.
//

#include "Runtime.h"
#include "Engine.h"
#include "EngineGlobal.h"
#include "Host.h"
#include "../../base/StaticTokenGraph.h"
#include "../../render/manager/PassManager.h"
#include "../../render/manager/TechManager.h"
#include "../../scene/manager/RegistriesManager.h"
#include "../manager/PluginManager.h"
#include "../manager/SystemManager.h"
#include "../manager/UserCodeManager.h"
#include <cassert>

ENGINE_CLI_OPTION(std::string, project, "", "MQEngine 工程路径");
ENGINE_CLI_OPTION(std::string, p, "", "MQEngine 工程路径短参数别名");
ENGINE_CLI_OPTION(bool, headless, false, "无头模式运行");
ENGINE_CLI_OPTION(int32_t, test_frames, 0, "自动化测试运行的最大帧数限制（达到该帧数后引擎正常退出，0 表示无限运行）");
ENGINE_CLI_OPTION(std::string, screenshot, "", "自动截图保存路径（如 Screenshots/frame.png）");
ENGINE_CLI_OPTION(int32_t, capture_frame, 0, "自动截图触发的帧号（0 表示在 test_frames 退出前一帧或默认第 60 帧触发）");
ENGINE_CLI_OPTION(std::string, capture_target, "SceneColorTarget", "截图捕获的 RenderGraph 目标名称（支持逗号分隔多目标，如 SceneColorTarget,mainWindowColor,RetTarget）");
ENGINE_CLI_OPTION(std::string, local, "", "区域语言设置（统一UTF-8，如 zh_CN, en_US, ja_JP）");

namespace MQEngine {
namespace Runtime {

    namespace {
        /**
         * @brief 12 个顶层核心模块的编译期拓扑依赖图
         */
        using ModuleStaticGraph = StaticTokenGraph<
            StaticTokenNode<Base::Foundation, TypeList<>, TypeList<Host>>,
            StaticTokenNode<Host, TypeList<Base::Foundation>, TypeList<Platform::Platform>>,
            StaticTokenNode<Platform::Platform, TypeList<Host>, TypeList<Resource::Repository>>,
            StaticTokenNode<Resource::Repository, TypeList<Platform::Platform>, TypeList<Scene::World>>,
            StaticTokenNode<Scene::World, TypeList<Resource::Repository>, TypeList<Physics::Simulator>>,
            StaticTokenNode<Physics::Simulator, TypeList<Scene::World>, TypeList<Animation::Animator>>,
            StaticTokenNode<Animation::Animator, TypeList<Physics::Simulator>, TypeList<HID::Input>>,
            StaticTokenNode<HID::Input, TypeList<Animation::Animator>, TypeList<Network::Replication>>,
            StaticTokenNode<Network::Replication, TypeList<HID::Input>, TypeList<Script::Interpreter>>,
            StaticTokenNode<Script::Interpreter, TypeList<Network::Replication>, TypeList<Effect::Effector>>,
            StaticTokenNode<Effect::Effector, TypeList<Script::Interpreter>, TypeList<Render::Renderer>>,
            StaticTokenNode<Render::Renderer, TypeList<Effect::Effector>, TypeList<>>
        >;

        /**
         * @brief 6 个核心 Manager 的编译期拓扑同步依赖图
         */
        using ManagerStaticGraph = StaticTokenGraph<
            StaticTokenNode<PluginManager, TypeList<>, TypeList<UserCodeManager>>,
            StaticTokenNode<UserCodeManager, TypeList<PluginManager>, TypeList<SystemManager>>,
            StaticTokenNode<SystemManager, TypeList<UserCodeManager>, TypeList<RegistriesManager>>,
            StaticTokenNode<RegistriesManager, TypeList<SystemManager>, TypeList<PassManager>>,
            StaticTokenNode<PassManager, TypeList<RegistriesManager>, TypeList<TechManager>>,
            StaticTokenNode<TechManager, TypeList<PassManager>, TypeList<>>
        >;

        template <typename ModuleType>
        constexpr ModuleType& getEngineModule(Engine& engine) noexcept {
            if constexpr (std::is_same_v<ModuleType, Base::Foundation>) {
                return engine.foundation;
            } else if constexpr (std::is_same_v<ModuleType, Host>) {
                return engine.host;
            } else if constexpr (std::is_same_v<ModuleType, Platform::Platform>) {
                return engine.platform;
            } else if constexpr (std::is_same_v<ModuleType, Resource::Repository>) {
                return engine.resource;
            } else if constexpr (std::is_same_v<ModuleType, Scene::World>) {
                return engine.scene;
            } else if constexpr (std::is_same_v<ModuleType, Physics::Simulator>) {
                return engine.physics;
            } else if constexpr (std::is_same_v<ModuleType, Animation::Animator>) {
                return engine.animation;
            } else if constexpr (std::is_same_v<ModuleType, HID::Input>) {
                return engine.hid;
            } else if constexpr (std::is_same_v<ModuleType, Network::Replication>) {
                return engine.network;
            } else if constexpr (std::is_same_v<ModuleType, Script::Interpreter>) {
                return engine.script;
            } else if constexpr (std::is_same_v<ModuleType, Effect::Effector>) {
                return engine.effect;
            } else if constexpr (std::is_same_v<ModuleType, Render::Renderer>) {
                return engine.renderer;
            }
        }
    } // namespace

    Runtime* Runtime::s_instance = nullptr;

    Runtime::Runtime()
    {
        s_instance = this;
    }

    Runtime::~Runtime()
    {
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    Runtime& Runtime::instance()
    {
        if (s_instance) {
            return *s_instance;
        }
        static Runtime s_fallbackInstance;
        return s_fallbackInstance;
    }

    Application* Runtime::getApplication() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->application;
    }

    Base::Foundation* Runtime::getFoundation() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->foundation.get();
    }

    Host* Runtime::getHost() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->host.get();
    }

    EngineCLIOption* Runtime::getCLIOption() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->foundation->getCLIOption();
    }

    Platform::Platform* Runtime::getPlatform() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->platform.get();
    }

    Effect::Effector* Runtime::getEffect() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->effect.get();
    }

    Render::Renderer* Runtime::getRenderer() const noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->renderer.get();
    }

    SystemManager& Runtime::getSystemManager() noexcept
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return *m_engine->host->getSystemManager();
    }


    void Runtime::registerModules()
    {
        ZoneScoped();
        m_engine = makeUnique<Engine>();
        Engine::s_instance = m_engine.get();

        assert(CreateApplication != nullptr && "Application 工厂函数未注册");
        m_engine->application = CreateApplication();
        assert(m_engine->application != nullptr && "CreateApplication 返回空指针");

        m_engine->application->registerInfo();

        ModuleStaticGraph::forEachForward([this]<typename ModuleType>() {
            getEngineModule<ModuleType>(*m_engine).registerInfo();
        });
    }

    void Runtime::RegisterApplicationFactory(CreateApplicationFn fn)
    {
        CreateApplication = fn;
    }

    void Runtime::alignWorkingDirectory()
    {
        assert(m_engine != nullptr && m_engine->application != nullptr && "Engine 与 Application 必须已创建");
        std::string curDir = SAI::System::getCurrentDirectory();
        if (curDir.empty()) {
            return;
        }

        Application* app = m_engine->application;
        if (!hasProjectPath()) {
            std::string curProj = SAI::Path::join(curDir, Base::Foundation::kDefaultProjectFileName);
            if (SAI::FileSystem::exists(curProj)) {
                setProjectPath(SAI::Path::normalize(curProj));
            } else {
                std::string legacyProj = SAI::Path::join(curDir, Base::Foundation::kLegacyProjectFileName);
                if (SAI::FileSystem::exists(legacyProj)) {
                    setProjectPath(SAI::Path::normalize(legacyProj));
                } else {
                    const char* defaultProj = app->applicationRegisterInfo.defaultProjectPath;
                    if (defaultProj && defaultProj[0] != '\0') {
                        std::string foundProj = SAI::FileSystem::findUpwards(curDir, defaultProj, 6);
                        if (foundProj.empty() && m_argc > 0 && m_argv != nullptr && m_argv[0] != nullptr) {
                            foundProj = SAI::FileSystem::findUpwards(SAI::Path::parentPath(m_argv[0]), defaultProj, 6);
                        }
                        if (!foundProj.empty()) {
                            setProjectPath(foundProj);
                        }
                    }
                }
            }
        }

        if (hasProjectPath()) {
            std::string projPath = getProjectPath();
            if (SAI::FileSystem::exists(projPath)) {
                std::string absProj = SAI::Path::normalize(projPath);
                setProjectPath(absProj);
                (void)SAI::System::setCurrentDirectory(SAI::Path::parentPath(absProj));
            }
            return;
        }

        if (SAI::FileSystem::containsAny(curDir, {"Assets", "assets"})) {
            return;
        }

        std::string fallbackRoot = SAI::FileSystem::findDirectoryUpwardsWithAll(
            curDir, {"Assets/Pipelines/default_pipeline.json"}, 6);
        if (!fallbackRoot.empty()) {
            (void)SAI::System::setCurrentDirectory(fallbackRoot);
        }
    }

    void Runtime::initModules()
    {
        ZoneScoped();
        assert(m_engine != nullptr && "Engine 不能为空");
        ModuleStaticGraph::forEachForward([this]<typename ModuleType>() {
            Status st = getEngineModule<ModuleType>(*m_engine).init();
            assert(st.ok() && "Engine 核心模块初始化必须成功");
        });
    }

    void Runtime::loadPluginsAndUserCode()
    {
        ZoneScoped();
        DataManager* dataManager = m_engine->scene->getDataManager();
        PluginManager* pm = m_engine->host->getPluginManager();
        UserCodeManager* ucm = m_engine->host->getUserCodeManager();
        ScriptSystem* scriptSystem = m_engine->script->getScriptSystem();

        assert(dataManager != nullptr && "DataManager 不能为空");
        assert(pm != nullptr && "PluginManager 不能为空");
        assert(ucm != nullptr && "UserCodeManager 不能为空");
        assert(scriptSystem != nullptr && "ScriptSystem 不能为空");

        if (m_engine->foundation->isProjectOpen()) {
            setProjectOpened(true);
            const auto& cfg = m_engine->foundation->getCurrentConfig();
            const std::string baseDir = m_engine->foundation->getProjectDirectory();

            for (const auto& pluginIdent : cfg.plugins) {
                auto pStatus = pm->resolveAndPreloadPlugin(pluginIdent, baseDir, m_engine.get());
                if (!pStatus.ok()) {
                    spdlog::warn("Runtime: 预加载工程配置插件 [{}] 失败: {}", pluginIdent, pStatus.message());
                }
            }

            for (const auto& ucIdent : cfg.userCodes) {
                auto uStatus = ucm->resolveAndPreloadUserCode(ucIdent, baseDir, m_engine.get());
                if (!uStatus.ok()) {
                    spdlog::warn("Runtime: 预加载工程配置用户代码 [{}] 失败: {}", ucIdent, uStatus.message());
                }
            }

            auto refreshSt = pm->refreshComposites(m_engine.get());
            if (!refreshSt.ok()) {
                spdlog::warn("Runtime: TokenGraph 拓扑排序激发 Composite 失败: {}", refreshSt.message());
            }
        }

        if (!dataManager->getCurrentScene() && !dataManager->getInitialSceneUuid().empty()) {
            dataManager->openScene(dataManager->getInitialSceneUuid());
        }

        scriptSystem->loadScripts();
        scriptSystem->start();

        if (!hasTag("Editor")) {
            setPlayMode(true);
        }
    }

    void Runtime::unloadPluginsAndUserCode()
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getUserCodeManager()->unloadAll();
        m_engine->host->getPluginManager()->unloadAll();
        m_engine->foundation->clearProjectConfig();
        setProjectOpened(false);
    }

    void Runtime::termModules()
    {
        ZoneScoped();
        assert(m_engine != nullptr && "Engine 不能为空");
        if (FCT::Context* ctx = m_engine->platform->getContext()) {
            ctx->requestExit();
        }
        m_engine->foundation->getJobSystem()->waitAll();
        m_engine->foundation->getJobSystem()->shutdown();
        unloadPluginsAndUserCode();

        ModuleStaticGraph::forEachReverse([this]<typename ModuleType>() {
            getEngineModule<ModuleType>(*m_engine).term();
        });
    }
    void Runtime::unregisterModules()
    {

    }

    void Runtime::flushManagers()
    {
        ZoneScoped();
        assert(m_engine != nullptr && "Engine 不能为空");
        ManagerStaticGraph::forEachForward([this]<typename MgrType>() {
            if constexpr (std::is_same_v<MgrType, PluginManager>) {
                m_engine->host->getPluginManager()->syncTicker();
            } else if constexpr (std::is_same_v<MgrType, UserCodeManager>) {
                m_engine->host->getUserCodeManager()->syncTicker();
            } else if constexpr (std::is_same_v<MgrType, SystemManager>) {
                m_engine->host->getSystemManager()->syncTicker();
            } else if constexpr (std::is_same_v<MgrType, RegistriesManager>) {
                RegistriesManager* rm = m_engine->scene->getRegistriesManager();
                assert(rm != nullptr && "RegistriesManager 不能为空");
                rm->syncTicker();
            } else if constexpr (std::is_same_v<MgrType, PassManager>) {
                m_engine->renderer->getPassManager()->syncTicker();
            } else if constexpr (std::is_same_v<MgrType, TechManager>) {
                m_engine->effect->getTechManager()->syncTicker();
            }
        });
    }
    void Runtime::sync() {
        m_engine->renderer->flushAndSync(m_engine->platform.get());
        syncRenderThread();
    }
    void Runtime::onSync()
    {
        ZoneScoped();
        auto ctx = m_engine->platform.getContext();
        auto flow = ctx->getModule<FCT::FlowControl>();
        flow->executeSyncTickers();
        m_engine->renderer->onSync();
    }

    void Runtime::syncRenderThread()
    {
        ZoneScoped();
        auto ctx = m_engine->platform.getContext();
        flushLastFrame();
        onSync();
        ctx->_nextFrame();
    }

    void Runtime::init(int argc, char* argv[])
    {
        ZoneScoped();
        m_argc = argc;
        m_argv = argv;
        m_shuttingDown = false;
        m_frameCount = 0;

        registerModules();
        initModules();
        loadPluginsAndUserCode();
        m_engine->application->init();
    }

    void Runtime::run()
    {
        prepare();
        loop();
        finalize();
    }

    void Runtime::prepare()
    {
        ZoneScoped();
        flushManagers();
        m_engine->renderer->setupTickers();
    }

    void Runtime::loop()
    {
        while (m_engine->platform->isWindowRunning() &&
               !m_engine->host->logicTickManager->isExitRequested() &&
               !m_shuttingDown) {
            logicTick();
            sync();
        }
    }
    void Runtime::flushLastFrame()
    {
        auto ctx = m_engine->platform.getContext();
        ctx->_consumeLogicTask();
        ctx->_waitCurrentFlush();
    }
    void Runtime::finalize()
    {
        flushLastFrame();
    }

    void Runtime::logicTick()
    {
        ZoneScoped();

        static auto lastFrameTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - lastFrameTime).count() / 1000000.0f;
        lastFrameTime = currentTime;
        TracyPlot("LogicDeltaTime (ms)", deltaTime * 1000.0f);

        m_engine->foundation->beginLogicFrame();

        NetworkManager* netMgr = m_engine->network->getNetworkManager();
        NetworkSyncSystem* netSync = m_engine->network->getNetworkSyncSystem();
        assert(netMgr != nullptr && netSync != nullptr && "Network 子系统不能为空");
        netMgr->tick(deltaTime);
        netSync->setDeltaTime(deltaTime);

        SkeletalAnimationSystem* animSys = m_engine->animation->getSkeletalAnimationSystem();
        assert(animSys != nullptr && "SkeletalAnimationSystem 不能为空");
        animSys->setDeltaTime(isPlaying() ? deltaTime : 0.0f);

        PhysicsSystem* physSys = m_engine->physics->getPhysicsSystem();
        assert(physSys != nullptr && "PhysicsSystem 不能为空");
        m_accumulator += deltaTime;
        int steps = 0;
        while (m_accumulator >= m_fixedTimestep && steps < m_maxSubSteps) {
            if (isPlaying()) {
                physSys->fixedTick(m_fixedTimestep);
            }
            m_accumulator -= m_fixedTimestep;
            steps++;
        }

        ScriptSystem* scriptSys = m_engine->script->getScriptSystem();
        assert(scriptSys != nullptr && "ScriptSystem 不能为空");
        scriptSys->setLogicDeltaTime(deltaTime);
        if (isPlaying()) {
            scriptSys->update();
        }

        m_engine->host->getSystemManager()->logicTick();
        m_engine->effect->updateLogic();
        m_engine->renderer->updateLogicCaches();
        m_engine->application->logicTick();

        PluginManager* pm = m_engine->host->getPluginManager();
        UserCodeManager* ucm = m_engine->host->getUserCodeManager();
        assert(pm != nullptr && ucm != nullptr && "PluginManager 与 UserCodeManager 不能为空");

        pm->logicTick(deltaTime);
        pm->checkHotReload(m_engine.get());

        if (isPlaying()) {
            ucm->logicTick(deltaTime);
        }
        ucm->checkHotReload(m_engine.get());
        m_engine->renderer->processCapture();

        FrameMarkNamed("LogicThread");
    }

    void Runtime::term()
    {
        ZoneScoped();
        setShuttingDown(true);

        if (!m_engine) {
            return;
        }

        termModules();
        unregisterModules();

        delete m_engine->application;
        m_engine->application = nullptr;

        m_engine.reset();
        Engine::s_instance = nullptr;
    }

    Status Runtime::loadPlugin(const std::string& pluginIdent)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        std::string baseDir = m_engine->foundation->isProjectOpen()
            ? m_engine->foundation->getProjectDirectory()
            : "";
        return m_engine->host->getPluginManager()->resolveAndLoadPlugin(pluginIdent, baseDir, m_engine.get());
    }

    Status Runtime::unloadPlugin(const std::string& pluginName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->host->getPluginManager()->unloadPlugin(pluginName, m_engine.get());
    }

    Status Runtime::reloadPlugin(const std::string& pluginName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->host->getPluginManager()->reloadPlugin(pluginName, m_engine.get());
    }

    void Runtime::requestLoadPlugin(const std::string& pluginIdent)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        std::string baseDir = m_engine->foundation->isProjectOpen()
            ? m_engine->foundation->getProjectDirectory()
            : "";
        m_engine->host->getPluginManager()->requestLoadPlugin(pluginIdent, baseDir);
    }

    void Runtime::requestUnloadPlugin(const std::string& pluginName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getPluginManager()->requestUnloadPlugin(pluginName);
    }

    void Runtime::requestReloadPlugin(const std::string& pluginName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getPluginManager()->requestReloadPlugin(pluginName);
    }

    Status Runtime::loadUserCode(const std::string& userCodeIdent)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        std::string baseDir = m_engine->foundation->isProjectOpen()
            ? m_engine->foundation->getProjectDirectory()
            : "";
        return m_engine->host->getUserCodeManager()->resolveAndLoadUserCode(userCodeIdent, baseDir, m_engine.get());
    }

    Status Runtime::reloadUserCode(const std::string& moduleName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        UserCodeManager* ucm = m_engine->host->getUserCodeManager();
        if (moduleName.empty()) {
            return ucm->reloadAll(m_engine.get());
        }
        return ucm->reloadUserCode(moduleName, m_engine.get());
    }

    void Runtime::requestLoadUserCode(const std::string& userCodeIdent)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        std::string baseDir = m_engine->foundation->isProjectOpen()
            ? m_engine->foundation->getProjectDirectory()
            : "";
        m_engine->host->getUserCodeManager()->requestLoadUserCode(userCodeIdent, baseDir);
    }

    void Runtime::requestUnloadUserCode(const std::string& moduleName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getUserCodeManager()->requestUnloadUserCode(moduleName);
    }

    void Runtime::requestReloadUserCode(const std::string& moduleName)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getUserCodeManager()->requestReloadUserCode(moduleName);
    }

    void Runtime::hotReloadAll()
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getPluginManager()->checkHotReload(m_engine.get());
        m_engine->host->getUserCodeManager()->checkHotReload(m_engine.get());
    }

    bool Runtime::isAutoHotReloadEnabled() const
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->host->getPluginManager()->isAutoHotReloadEnabled();
    }

    void Runtime::setAutoHotReloadEnabled(bool enabled)
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getPluginManager()->setAutoHotReloadEnabled(enabled);
        m_engine->host->getUserCodeManager()->setAutoHotReloadEnabled(enabled);
    }

    std::string Runtime::getProjectName() const
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->foundation->getProjectName();
    }

    void Runtime::parseCommandLine(int argc, char* argv[])
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        cli->parse(argc, argv);
    }

    void Runtime::parseCommandLine(const std::vector<std::string>& args)
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        cli->parse(args);
    }

    bool Runtime::hasTag(const std::string& tag) const
    {
        assert(m_engine != nullptr && "Engine 不能为空");
        return m_engine->host->getPluginManager()->hasTag(tag);
    }

    void Runtime::setPlayMode(bool playing)
    {
        m_playing = playing;
        g_engineGlobal.isRunning = playing;
        assert(m_engine != nullptr && "Engine 不能为空");
        m_engine->host->getSystemManager()->requestSetSystemEnabled("ScriptCacheSystem", playing);
        if (playing) {
            assert(g_engineGlobal.cameraSystem != nullptr && "CameraSystem 不能为空");
            g_engineGlobal.cameraSystem->activateFirstSceneCamera();
        }
    }

    void Runtime::setHeadless(bool headless)
    {
        m_headless = headless;
        SetFlag(&FLAGS_headless, headless);
        if (m_engine) {
            m_engine->headLess = headless;
        }
    }

    void Runtime::setTestFrames(int32_t frames)
    {
        m_testFrames = (frames > 0) ? frames : 0;
        SetFlag(&FLAGS_test_frames, m_testFrames);
        if (m_engine) {
            m_engine->foundation->setTestFrames(m_testFrames);
        }
    }

    void Runtime::setScreenshotPath(const std::string& path)
    {
        m_screenshotPath = path;
        SetFlag(&FLAGS_screenshot, path);
        if (m_engine) {
            m_engine->foundation->engineConfig.screenshotPath = path;
        }
    }

    void Runtime::setCaptureFrame(int32_t frame)
    {
        m_captureFrame = (frame > 0) ? frame : 0;
        SetFlag(&FLAGS_capture_frame, m_captureFrame);
        if (m_engine) {
            m_engine->foundation->engineConfig.captureFrame = m_captureFrame;
        }
    }

    void Runtime::setCaptureTarget(const std::string& target)
    {
        m_captureTarget = target.empty() ? "SceneColorTarget" : target;
        SetFlag(&FLAGS_capture_target, m_captureTarget);
        if (m_engine) {
            m_engine->foundation->engineConfig.captureTarget = m_captureTarget;
        }
    }

    void Runtime::setProjectPath(const std::string& path)
    {
        m_projectPath = path;
        SetFlag(&FLAGS_project, path);
    }

    void Runtime::setLocal(const std::string& localName)
    {
        m_localName = localName;
        SetFlag(&FLAGS_local, localName);
    }

    bool Runtime::hasOption(const std::string& key) const
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        return cli->hasOption(key);
    }

    std::string Runtime::getOption(const std::string& key, const std::string& defaultVal) const
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        return cli->getOption(key, defaultVal);
    }

    void Runtime::setOption(const std::string& key, const std::string& value)
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        cli->setOption(key, value);
    }

    const std::vector<std::string>& Runtime::getRawArgs() const
    {
        auto* cli = getCLIOption();
        assert(cli != nullptr && "EngineCLIOption 不能为空");
        return cli->getRawArgs();
    }

} // namespace Runtime
} // namespace MQEngine
