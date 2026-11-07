#include "Foundation.h"
#include "../resource/core/DiskResourceLoader.h"
#include "../runtime/core/Engine.h"
#include "../runtime/core/EngineGlobal.h"
#include "../runtime/core/Runtime.h"
#include "../runtime/interface/Application.h"
#include "../scene/data/Component.h"
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace MQEngine
{
    namespace Base
    {
        Foundation::Foundation() = default;

        Foundation::~Foundation()
        {
            term();
        }

        void Foundation::registerInfo()
        {
            memoryManager.registerInfo();
            jobSystem.registerInfo();
            cliOption.registerInfo();
            componentReflection.registerInfo();

            g_engineGlobal.foundation = this;
            g_engineGlobal.memoryManager = &memoryManager;
            g_engineGlobal.jobSystem = &jobSystem;
            g_engineGlobal.componentReflection = &componentReflection;

            if (auto* engine = Runtime::Engine::getInstance()) {
                if (engine->application) {
                    for (const auto& opt : engine->application->applicationRegisterInfo.cliOptions) {
                        cliOption.registerOptionDesc(opt);
                    }
                }
            }
        }

        void Foundation::registerEnttComponents()
        {
            ZoneScoped();
            entt::registry dummyRegistry;
            [&]<typename... Components>(std::tuple<Components...>) {
                (dummyRegistry.storage<Components>(), ...);
            }(AllComponentsList{});
        }

        ResourceLoader* Foundation::resolveLoader(ResourceLoader* explicitLoader)
        {
            if (explicitLoader) {
                return explicitLoader;
            }
            if (auto* engine = Runtime::Engine::getInstance()) {
                if (engine->resource && engine->resource->getResourceLoader()) {
                    return engine->resource->getResourceLoader();
                }
            }
            if (g_engineGlobal.resourceLoader) {
                return g_engineGlobal.resourceLoader;
            }
            static DiskResourceLoader s_fallbackLoader;
            return &s_fallbackLoader;
        }

        StatusOr<ProjectConfig> Foundation::loadProjectConfig(const std::string& projectFilePath,
                                                              ResourceLoader* loader)
        {
            ResourceLoader* activeLoader = resolveLoader(loader);
            auto docRes = ConfigDocument::loadFromFile(activeLoader, projectFilePath);
            if (!docRes.ok()) {
                spdlog::error("Foundation: 无法读取或解析工程文件: {}, 错误: {}",
                              projectFilePath, docRes.status().message());
                return docRes.status();
            }

            ProjectConfig config;
            CHECK_STATUS(parseProjectConfigDocument(docRes.value(), projectFilePath, config));
            return config;
        }

        Status Foundation::parseProjectConfigDocument(const ConfigDocument& doc,
                                                      const std::string& projectFilePath,
                                                      ProjectConfig& outConfig)
        {
            if (!doc.root().isTable()) {
                return InvalidArgumentError("工程配置文件格式无效：根节点必须为配置表");
            }

            doc.readStruct(outConfig);
            populateProjectIdentity(doc, projectFilePath, outConfig);
            populateProjectPaths(doc, outConfig);
            populateProjectModules(doc, outConfig);

            if (outConfig.settings.empty() && doc.hasSection("settings")) {
                outConfig.settings = doc.getStringMap("settings");
            }

            return OkStatus();
        }

        void Foundation::populateProjectIdentity(const ConfigDocument& doc,
                                                 const std::string& projectFilePath,
                                                 ProjectConfig& outConfig) const
        {
            outConfig.projectFilePath = projectFilePath;
            outConfig.projectDirectory = SAI::Path::directoryOrDot(projectFilePath);

            outConfig.projectName = doc.getStringOr(
                {"projectName", "project.name", "name"}, outConfig.projectName);
            if (outConfig.projectName.empty()) {
                std::string stemName = SAI::Path::stem(projectFilePath);
                if (stemName == "project" && !outConfig.projectDirectory.empty()) {
                    stemName = SAI::Path::fileName(outConfig.projectDirectory);
                }
                outConfig.projectName = stemName.empty() ? projectFilePath : stemName;
            }

            outConfig.version = doc.getStringOr(
                {"version", "project.version"},
                outConfig.version.empty() ? "1.0.0" : outConfig.version);
        }

        void Foundation::populateProjectPaths(const ConfigDocument& doc,
                                              ProjectConfig& outConfig) const
        {
            outConfig.assetRoot = doc.getStringOr(
                {"assetRoot", "assetsRoot", "assets", "paths.assetRoot", "project.assetRoot"},
                outConfig.assetRoot.empty() ? "Assets" : outConfig.assetRoot);
            outConfig.resolvedAssetRoot = SAI::Path::resolve(outConfig.projectDirectory, outConfig.assetRoot);

            outConfig.pipelinePath = doc.getStringOr(
                {"pipelinePath", "pipeline", "paths.pipelinePath", "renderer.pipelinePath"},
                outConfig.pipelinePath);
            if (!outConfig.pipelinePath.empty()) {
                outConfig.resolvedPipelinePath = SAI::Path::resolve(outConfig.projectDirectory, outConfig.pipelinePath);
            } else {
                outConfig.resolvedPipelinePath.clear();
            }

            outConfig.initialSceneUuid = doc.getStringOr(
                {"initialSceneUuid", "initialScene", "scene", "project.initialSceneUuid"},
                outConfig.initialSceneUuid);
        }

        void Foundation::populateProjectModules(const ConfigDocument& doc,
                                                ProjectConfig& outConfig) const
        {
            if (outConfig.plugins.empty()) {
                outConfig.plugins = doc.getStringArrayOr({"plugins", "modules.plugins"});
            }

            outConfig.userCode = doc.getStringOr(
                {"userCode", "modules.userCode", "project.userCode"},
                outConfig.userCode.empty() ? "UserCode" : outConfig.userCode);

            if (outConfig.userCodes.empty()) {
                outConfig.userCodes = doc.getStringArrayOr({"userCodes", "modules.userCodes"});
            }
            if (outConfig.userCodes.empty() && !outConfig.userCode.empty()) {
                outConfig.userCodes.push_back(outConfig.userCode);
            }
        }

        void Foundation::buildProjectConfigDocument(const ProjectConfig& config,
                                                    ConfigDocument& outDoc) const
        {
            outDoc.writeStruct(config);
            outDoc.root().removeKey("projectFilePath");
            outDoc.root().removeKey("projectDirectory");
            outDoc.root().removeKey("resolvedAssetRoot");
            outDoc.root().removeKey("resolvedPipelinePath");

            if (config.userCode.empty()) {
                outDoc.root().removeKey("userCode");
            }
            if (config.userCodes.empty()) {
                outDoc.root().removeKey("userCodes");
            }
            if (config.initialSceneUuid.empty()) {
                outDoc.root().removeKey("initialSceneUuid");
            }
        }

        Status Foundation::saveProjectConfig(const std::string& projectFilePath,
                                             const ProjectConfig& config,
                                             ResourceLoader* loader)
        {
            ResourceLoader* activeLoader = resolveLoader(loader);
            ConfigDocument doc;
            buildProjectConfigDocument(config, doc);
            return doc.saveToFile(activeLoader, projectFilePath, ConfigFormat::Auto);
        }

        Status Foundation::activateProjectConfig(const std::string& projectFilePath,
                                                 ResourceLoader* loader)
        {
            auto configRes = loadProjectConfig(projectFilePath, loader);
            if (!configRes.ok()) {
                return configRes.status();
            }
            m_currentConfig = configRes.value();
            m_isProjectOpen = true;
            if (!m_currentConfig.projectDirectory.empty()) {
                (void)SAI::System::setCurrentDirectory(m_currentConfig.projectDirectory);
            }
            Runtime::Runtime::instance().setProjectOpened(true);
            return OkStatus();
        }

        void Foundation::clearProjectConfig()
        {
            m_isProjectOpen = false;
            m_currentConfig = ProjectConfig{};
            Runtime::Runtime::instance().setProjectOpened(false);
        }

        Status Foundation::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto& runtime = Runtime::Runtime::instance();
            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && engine->application != nullptr && "Engine 与 Application 不能为空");

            if (runtime.getArgc() > 0 && runtime.getArgv() != nullptr) {
                cliOption.parse(runtime.getArgc(), runtime.getArgv());
            }
            if (cliOption.hasOption("project")) {
                runtime.setProjectPath(cliOption.getOption("project"));
            } else if (cliOption.hasOption("p")) {
                runtime.setProjectPath(cliOption.getOption("p"));
            }
            if (cliOption.hasOption("headless") || cliOption.hasOption("no-window")) {
                runtime.setHeadless(true);
            }
            if (cliOption.hasOption("test-frames")) {
                runtime.setTestFrames(cliOption.getIntOption("test-frames"));
            } else if (cliOption.hasOption("frames")) {
                runtime.setTestFrames(cliOption.getIntOption("frames"));
            }
            if (cliOption.hasOption("screenshot")) {
                runtime.setScreenshotPath(cliOption.getOption("screenshot"));
            } else if (cliOption.hasOption("capture_output")) {
                runtime.setScreenshotPath(cliOption.getOption("capture_output"));
            }
            if (cliOption.hasOption("capture-frame")) {
                runtime.setCaptureFrame(cliOption.getIntOption("capture-frame"));
            }
            if (cliOption.hasOption("capture-target")) {
                runtime.setCaptureTarget(cliOption.getOption("capture-target"));
            }
            if (cliOption.hasOption("local")) {
                runtime.setLocal(cliOption.getOption("local"));
            }
            engineConfig.parseFromCLI(cliOption);

            runtime.alignWorkingDirectory();

            if (runtime.shouldOpenProjectOnInit()) {
                Status cfgSt = activateProjectConfig(runtime.getProjectPath());
                if (!cfgSt.ok()) {
                    spdlog::error("Foundation: 初始化加载工程配置失败 [{}]: {}",
                                  runtime.getProjectPath(), cfgSt.message());
                }
            }

            const bool headless = runtime.isHeadless() || engine->headLess || engineConfig.headless;
            engineConfig.headless = headless;
            engine->headLess = headless;
            runtime.setHeadless(headless);

            if (engineConfig.testFrames <= 0 && runtime.getTestFrames() > 0) {
                engineConfig.testFrames = runtime.getTestFrames();
            } else if (engineConfig.testFrames > 0) {
                runtime.setTestFrames(engineConfig.testFrames);
            }

            if (engineConfig.screenshotPath.empty() && runtime.hasScreenshotPath()) {
                engineConfig.screenshotPath = runtime.getScreenshotPath();
            }
            if (engineConfig.captureFrame <= 0 && runtime.getCaptureFrame() > 0) {
                engineConfig.captureFrame = runtime.getCaptureFrame();
            }

            const std::string cliLocalName = !engineConfig.local.empty() ? engineConfig.local : runtime.getLocal();
            engineConfig.local = cliLocalName;
            const Local fallbackLocal = engine->application->applicationRegisterInfo.local;
            if (!cliLocalName.empty()) {
                SetLocal(cliLocalName.c_str());
            } else {
                SetLocal(fallbackLocal);
            }

            if (headless) {
#ifdef _WIN32
                SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
                _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
                _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
                _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
                _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
                _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#endif
            }

            registerEnttComponents();
            componentReflection.init();

            g_engineGlobal.foundation = this;
            g_engineGlobal.memoryManager = &memoryManager;
            g_engineGlobal.jobSystem = &jobSystem;
            g_engineGlobal.componentReflection = &componentReflection;
            m_initialized = true;
            return OkStatus();
        }

        void Foundation::beginLogicFrame()
        {
            memoryManager.beginLogicFrame();
            jobSystem.pumpMainThreadTasks();
        }

        void Foundation::term()
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;
            clearProjectConfig();

            componentReflection.clear();
            jobSystem.shutdown();
            if (g_engineGlobal.componentReflection == &componentReflection) {
                g_engineGlobal.componentReflection = nullptr;
            }
            if (g_engineGlobal.jobSystem == &jobSystem) {
                g_engineGlobal.jobSystem = nullptr;
            }
            if (g_engineGlobal.memoryManager == &memoryManager) {
                g_engineGlobal.memoryManager = nullptr;
            }
            if (g_engineGlobal.foundation == this) {
                g_engineGlobal.foundation = nullptr;
            }
        }

        void Foundation::addLogSink(const std::shared_ptr<spdlog::sinks::sink>& sink)
        {
            if (!sink) {
                return;
            }
            if (auto logger = spdlog::default_logger()) {
                auto& sinks = logger->sinks();
                if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end()) {
                    sinks.push_back(sink);
                }
            }
        }

        void Foundation::removeLogSink(const std::shared_ptr<spdlog::sinks::sink>& sink)
        {
            if (!sink) {
                return;
            }
            if (auto logger = spdlog::default_logger()) {
                auto& sinks = logger->sinks();
                sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
            }
        }

    } // namespace Base
} // namespace MQEngine
