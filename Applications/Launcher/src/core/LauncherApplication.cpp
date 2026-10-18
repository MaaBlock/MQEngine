#include "LauncherApplication.h"

using namespace FCT;

namespace MQEngine
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* kDefaultDemoSceneUuid = "default-scene-0001-0000-000000000001";
        constexpr const char* kDefaultDemoSceneName = "DefaultScene";
        constexpr const char* kDemoFloorModelUuid = "demo-model-floor1-0001-000000000001";
        constexpr const char* kDemoRobotModelUuid = "demo-model-robot0-0001-000000000002";
        constexpr const char* kDemoBallModelUuid = "demo-model-ball00-0001-000000000003";
        constexpr const char* kDemoFloorMatModelUuid = "demo-model-flrmat-0001-000000000004";

        std::string NormalizePathString(const fs::path& p)
        {
            std::error_code ec;
            fs::path resolved = fs::weakly_canonical(p, ec);
            const fs::path& target = ec ? p : resolved;
            return std::string(reinterpret_cast<const char*>(target.generic_u8string().c_str()));
        }
    }

    RenderConfig LauncherApplication::renderConfig() const
    {
        return RenderConfig{
            .target = RenderTarget::Texture,
            .windowTitle = "MQEngine Launcher",
            .defaultProjectPath = "Applications/Launcher/LauncherProject/project.json"
        };
    }

    void LauncherApplication::locateWorkspaceRoot()
    {
        std::error_code ec;
        fs::path probe = fs::current_path(ec);
        for (int depth = 0; depth < 8; ++depth)
        {
            if (fs::exists(probe / "CMakeLists.txt", ec) &&
                fs::exists(probe / "Engine", ec) &&
                fs::exists(probe / "Assets", ec))
            {
                m_workspaceRoot = NormalizePathString(probe);
                return;
            }
            if (!probe.has_parent_path() || probe == probe.parent_path())
                break;
            probe = probe.parent_path();
        }
        m_workspaceRoot = NormalizePathString(fs::current_path(ec));
    }

    std::string LauncherApplication::resolveWorkspacePath(const std::string& relativePath) const
    {
        if (relativePath.empty() || m_workspaceRoot.empty())
            return relativePath;
        fs::path p = fs::u8path(relativePath);
        if (p.is_absolute())
            return relativePath;
        return NormalizePathString(fs::u8path(m_workspaceRoot) / p);
    }

    void LauncherApplication::init()
    {
        ZoneScoped();
        locateWorkspaceRoot();

        setupRenderCallbacks();
        setupDefaultCamera();
        ensureDemoProjectInitialized();
        scanProjects();

        if (CommandLineArgs::instance().hasProject())
        {
            std::string cliProj = NormalizePathString(fs::u8path(CommandLineArgs::instance().getProjectPath()));
            std::string launcherProj = resolveWorkspacePath("Applications/Launcher/LauncherProject/project.json");
            if (cliProj != launcherProj)
                addOrUpdateProjectEntry(cliProj);
        }
    }

    void LauncherApplication::setupRenderCallbacks()
    {
        auto& engine = Engine::getInstance();
        Status pluginSt = engine.loadPlugin("ImguiPlugin");
        if (!pluginSt.ok())
        {
            spdlog::error("LauncherApplication: 加载 ImguiPlugin 失败: {}", pluginSt.message());
            return;
        }

        if (engine.getPluginManager())
        {
            m_imguiPlugin = engine.getPluginManager()->getPluginAs<ImguiPlugin>("ImguiPlugin");
        }

        if (m_imguiPlugin)
        {
            m_imguiPlugin->setClearColor(Vec4(0.10f, 0.11f, 0.13f, 1.0f));
            m_imguiPlugin->syncCallerContext();
            m_imguiCtx = m_imguiPlugin->getImguiContext();
        }
    }

    void LauncherApplication::setupDefaultCamera()
    {
        g_engineGlobal.dataManager->appendRegistry(&m_registry);
        m_cameraEntity = m_registry.create();
        m_registry.emplace<PositionComponent>(m_cameraEntity);
        m_registry.emplace<RotationComponent>(m_cameraEntity);
        m_registry.emplace<CameraComponent>(m_cameraEntity, CameraComponent{ .active = true });
    }

    void LauncherApplication::ensureDemoProjectInitialized()
    {
        std::string demoDir = resolveWorkspacePath("Example/DemoProject");
        auto st = initializeProjectDirectory(demoDir, "DemoProject", kDefaultDemoSceneUuid);
        if (!st.ok())
        {
            m_statusMessage = std::string("初始化 DemoProject 失败: ") + std::string(st.message());
            m_statusIsError = true;
            spdlog::warn("{}", m_statusMessage);
        }
    }

    Status LauncherApplication::initializeProjectDirectory(const std::string& projectDir,
                                                           const std::string& projectName,
                                                           const std::string& sceneUuid)
    {
        if (!g_engineGlobal.resourceLoader || !g_engineGlobal.dataManager)
            return FailedPreconditionError("ResourceLoader 或 DataManager 未就绪");

        auto* dataLoader = g_engineGlobal.dataManager->getDataLoader();
        dataLoader->ensureDirectory(projectDir);

        std::string assetRoot = projectDir + "/Assets";
        dataLoader->ensureDirectory(assetRoot);
        dataLoader->ensureDirectory(assetRoot + "/pipelines");
        dataLoader->ensureDirectory(assetRoot + "/scenes");
        dataLoader->ensureDirectory(assetRoot + "/models");
        dataLoader->ensureDirectory(assetRoot + "/scripts/js");
        dataLoader->ensureDirectory(assetRoot + "/shadergraph");
        dataLoader->ensureDirectory(assetRoot + "/snippets");

        std::string projectJsonPath = projectDir + "/project.json";
        if (!dataLoader->fileExists(projectJsonPath))
        {
            ProjectConfig cfg;
            cfg.projectName = projectName;
            cfg.version = "1.0.0";
            cfg.assetRoot = "Assets";
            cfg.pipelinePath = "Assets/pipelines/default_pipeline.json";
            cfg.initialSceneUuid = sceneUuid;
            cfg.settings["description"] = "MQEngine 默认工程";

            ProjectManager tempMgr(g_engineGlobal.resourceLoader);
            auto saveSt = tempMgr.saveProjectConfig(projectJsonPath, cfg);
            CHECK_STATUS(saveSt);
        }

        std::string targetPipelinePath = assetRoot + "/pipelines/default_pipeline.json";
        if (!dataLoader->fileExists(targetPipelinePath))
        {
            auto defaultPipeRes = g_engineGlobal.resourceLoader->loadFile(resolveWorkspacePath("Assets/pipelines/default_pipeline.json"));
            if (defaultPipeRes.ok())
            {
                auto pipeSaveSt = g_engineGlobal.resourceLoader->saveFile(targetPipelinePath, defaultPipeRes.value());
                CHECK_STATUS(pipeSaveSt);
            }
        }

        return ensureSceneBootstrapFiles(assetRoot, kDefaultDemoSceneName, sceneUuid);
    }

    Status LauncherApplication::ensureSceneBootstrapFiles(const std::string& assetRoot,
                                                          const std::string& sceneName,
                                                          const std::string& sceneUuid)
    {
        auto* dataLoader = g_engineGlobal.dataManager->getDataLoader();
        std::string sceneDir = assetRoot + "/scenes/" + sceneName;
        dataLoader->ensureDirectory(sceneDir);
        dataLoader->ensureDirectory(sceneDir + "/trunks");

        std::string uuidFilePath = sceneDir + "/scene.uuid";
        if (!dataLoader->fileExists(uuidFilePath))
        {
            auto outStream = dataLoader->openBinaryOutputStream(uuidFilePath);
            if (!outStream || !outStream->is_open())
                return InternalError("无法写入场景 UUID 文件: " + uuidFilePath);
            SceneUuidFile uuidData{ .uuid = sceneUuid };
            boost::archive::binary_oarchive ar(*outStream);
            ar << uuidData;
        }

        std::string settingPath = assetRoot + "/project.setting";
        if (!dataLoader->fileExists(settingPath))
        {
            auto outStream = dataLoader->openBinaryOutputStream(settingPath);
            if (!outStream || !outStream->is_open())
                return InternalError("无法写入工程设置文件: " + settingPath);
            ProjectSetting setting{ .initialSceneUuid = sceneUuid };
            boost::archive::binary_oarchive ar(*outStream);
            ar << setting;
        }

        bool hadRobotModel = dataLoader->fileExists(assetRoot + "/models/robot/model.uuid");
        ensureDefaultDemoModels(assetRoot);

        std::string sceneDatPath = sceneDir + "/scene.dat";
        if (!dataLoader->fileExists(sceneDatPath) || !hadRobotModel)
        {
            std::string prevAssetRoot = g_engineGlobal.dataManager->getAssetRoot();
            g_engineGlobal.dataManager->setAssetRoot(assetRoot);
            populateDefaultSceneEntities(sceneUuid);
            g_engineGlobal.dataManager->setAssetRoot(prevAssetRoot);
        }

        return OkStatus();
    }

    void LauncherApplication::ensureDefaultDemoModels(const std::string& assetRoot)
    {
        importBuiltinModel(assetRoot, "floor_1", kDemoFloorModelUuid, resolveWorkspacePath("Assets/models/floor_1.glb"));
        importBuiltinModel(assetRoot, "robot", kDemoRobotModelUuid, resolveWorkspacePath("Assets/models/robot.glb"));
        importBuiltinModel(assetRoot, "ball", kDemoBallModelUuid, resolveWorkspacePath("Assets/ball.obj"));
        importBuiltinModel(assetRoot, "floor_material", kDemoFloorMatModelUuid, resolveWorkspacePath("Assets/models/floor_material.glb"));
    }

    void LauncherApplication::importBuiltinModel(const std::string& assetRoot,
                                                 const std::string& modelDirName,
                                                 const std::string& modelUuid,
                                                 const std::string& sourceModelPath)
    {
        std::error_code ec;
        fs::path srcPath = fs::u8path(sourceModelPath);
        if (!fs::exists(srcPath, ec))
            return;

        fs::path targetDir = fs::u8path(assetRoot) / "models" / modelDirName;
        if (fs::exists(targetDir / "model.uuid", ec) && fs::exists(targetDir / "index.edit.dat", ec))
            return;

        fs::path sourceSubDir = targetDir / "source";
        fs::create_directories(sourceSubDir, ec);

        if (!g_engineGlobal.rt)
            return;
        UniquePtr<ModelLoader> modelLoader(g_engineGlobal.rt->createModelLoader());
        if (!modelLoader)
            return;

        std::set<std::string> allPaths = modelLoader->resolveModePaths(sourceModelPath);
        allPaths.insert(sourceModelPath);
        fs::path sourceBaseDir = srcPath.parent_path();

        for (const auto& filePath : allPaths)
        {
            fs::path sFile = fs::u8path(filePath);
            if (!fs::exists(sFile, ec))
                continue;
            fs::path dstFile;
            fs::path relPath = fs::relative(sFile, sourceBaseDir, ec);
            if (!ec && !relPath.empty() && relPath.native()[0] != '.')
                dstFile = sourceSubDir / relPath;
            else
                dstFile = sourceSubDir / sFile.filename();

            fs::create_directories(dstFile.parent_path(), ec);
            fs::copy_file(sFile, dstFile, fs::copy_options::overwrite_existing, ec);
        }

        try
        {
            ModelInfo::SceneInfo sceneInfo = modelLoader->loadModelInfo(sourceModelPath);
            std::ofstream indexOut(targetDir / "index.edit.dat", std::ios::binary);
            if (indexOut.is_open())
            {
                boost::archive::binary_oarchive ar(indexOut);
                ar << sceneInfo;
            }

            ModelUuidFile uuidFileData;
            uuidFileData.uuid = modelUuid;
            uuidFileData.modelRelativePath = "source/" + srcPath.filename().string();
            std::ofstream uuidOut(targetDir / "model.uuid", std::ios::binary);
            if (uuidOut.is_open())
            {
                boost::archive::binary_oarchive ar(uuidOut);
                ar << uuidFileData;
            }

            auto fileTime = fs::last_write_time(srcPath, ec);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
            std::ofstream tsOut(targetDir / "timestamp.dat", std::ios::binary);
            if (tsOut.is_open())
            {
                boost::archive::binary_oarchive ar(tsOut);
                ar << timestamp;
                ar << sourceModelPath;
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("导入默认模型 {} 失败: {}", modelDirName, e.what());
        }
    }

    void LauncherApplication::populateDefaultSceneEntities(const std::string& sceneUuid)
    {
        g_engineGlobal.dataManager->loadScene(sceneUuid);
        g_engineGlobal.dataManager->openScene(sceneUuid);
        Scene* scene = g_engineGlobal.dataManager->getCurrentScene();
        if (scene)
        {
            SceneTrunk* trunk = scene->getLoadedTrunk("default");
            if (trunk && trunk->getRegistry())
            {
                auto* reg = trunk->getRegistry();
                if (reg->view<CameraComponent>().empty())
                {
                    auto camEntity = reg->create();
                    reg->emplace<NameTag>(camEntity, NameTag{ "MainCamera" });
                    reg->emplace<PositionComponent>(camEntity, PositionComponent{ Vec3(2.8f, 2.2f, 2.8f) });
                    reg->emplace<RotationComponent>(camEntity, RotationComponent{ Vec3(-22.0f, -135.0f, 0.0f) });
                    reg->emplace<CameraComponent>(camEntity, CameraComponent{
                        .active = true,
                        .fov = 60.0f,
                        .nearPlane = 0.1f,
                        .farPlane = 100.0f
                    });

                    auto lightEntity = reg->create();
                    reg->emplace<NameTag>(lightEntity, NameTag{ "DirectionalLight" });
                    reg->emplace<PositionComponent>(lightEntity, PositionComponent{ Vec3(4.0f, 8.0f, 4.0f) });
                    reg->emplace<RotationComponent>(lightEntity, RotationComponent{ Vec3(-45.0f, 45.0f, 0.0f) });
                    DirectionalLightComponent dirLight{};
                    dirLight.direction = Vec3(-0.5f, -1.0f, -0.6f);
                    dirLight.color = Vec3(1.0f, 0.98f, 0.92f);
                    dirLight.intensity = 1.8f;
                    dirLight.enabled = true;
                    reg->emplace<DirectionalLightComponent>(lightEntity, dirLight);
                }
                else
                {
                    auto lightView = reg->view<DirectionalLightComponent>();
                    for (auto entity : lightView)
                    {
                        auto& dirLight = lightView.get<DirectionalLightComponent>(entity);
                        dirLight.direction = Vec3(-0.5f, -1.0f, -0.6f);
                        dirLight.color = Vec3(1.0f, 0.98f, 0.92f);
                        dirLight.intensity = 1.8f;
                        dirLight.enabled = true;
                    }
                }

                if (reg->view<StaticMeshInstance>().empty())
                {
                    auto floorEntity = reg->create();
                    reg->emplace<NameTag>(floorEntity, NameTag{ "Floor" });
                    reg->emplace<PositionComponent>(floorEntity, PositionComponent{ Vec3(0.0f, 0.0f, 0.0f) });
                    reg->emplace<RotationComponent>(floorEntity, RotationComponent{ Vec3(0.0f, 0.0f, 0.0f) });
                    reg->emplace<ScaleComponent>(floorEntity, ScaleComponent{ Vec3(2.0f, 1.0f, 2.0f) });
                    reg->emplace<StaticMeshInstance>(floorEntity, StaticMeshInstance{ kDemoFloorModelUuid, "Object_0" });
                    reg->emplace<DiffuseTextureComponent>(floorEntity, DiffuseTextureComponent{ kDemoFloorModelUuid, "*0" });
                    reg->emplace<NormalTextureComponent>(floorEntity, NormalTextureComponent{ kDemoFloorModelUuid, "*2" });

                    auto robotEntity = reg->create();
                    reg->emplace<NameTag>(robotEntity, NameTag{ "Robot" });
                    reg->emplace<PositionComponent>(robotEntity, PositionComponent{ Vec3(0.0f, 0.0f, 0.0f) });
                    reg->emplace<RotationComponent>(robotEntity, RotationComponent{ Vec3(0.0f, 45.0f, 0.0f) });
                    reg->emplace<ScaleComponent>(robotEntity, ScaleComponent{ Vec3(1.0f, 1.0f, 1.0f) });
                    reg->emplace<StaticMeshInstance>(robotEntity, StaticMeshInstance{ kDemoRobotModelUuid, "Bot_Bot_0" });
                    reg->emplace<DiffuseTextureComponent>(robotEntity, DiffuseTextureComponent{ kDemoRobotModelUuid, "*0" });
                    reg->emplace<NormalTextureComponent>(robotEntity, NormalTextureComponent{ kDemoRobotModelUuid, "*3" });

                    auto sphereEntity = reg->create();
                    reg->emplace<NameTag>(sphereEntity, NameTag{ "Sphere" });
                    reg->emplace<PositionComponent>(sphereEntity, PositionComponent{ Vec3(-1.3f, 0.4f, 0.8f) });
                    reg->emplace<RotationComponent>(sphereEntity, RotationComponent{ Vec3(0.0f, 0.0f, 0.0f) });
                    reg->emplace<ScaleComponent>(sphereEntity, ScaleComponent{ Vec3(2.0f, 2.0f, 2.0f) });
                    reg->emplace<StaticMeshInstance>(sphereEntity, StaticMeshInstance{ kDemoBallModelUuid, "ball" });
                }
            }
            g_engineGlobal.dataManager->saveScene(sceneUuid);
        }
        g_engineGlobal.dataManager->closeScene(sceneUuid);
    }

    void LauncherApplication::scanProjects()
    {
        ZoneScoped();
        m_projects.clear();
        m_selectedProjectIndex = -1;

        addOrUpdateProjectEntry(resolveWorkspacePath("Example/DemoProject/project.json"));
        scanDirectoryForProjects(resolveWorkspacePath("Example"));
        scanDirectoryForProjects(resolveWorkspacePath("Projects"));

        if (!m_projects.empty() && m_selectedProjectIndex < 0)
            m_selectedProjectIndex = 0;
    }

    void LauncherApplication::scanDirectoryForProjects(const std::string& rootDir)
    {
        auto* dataLoader = g_engineGlobal.dataManager ? g_engineGlobal.dataManager->getDataLoader() : nullptr;
        if (!dataLoader || !dataLoader->directoryExists(rootDir))
            return;

        auto subDirs = dataLoader->getSubDirectories(rootDir);
        for (const auto& subDir : subDirs)
        {
            std::string candidateJson = subDir + "/project.json";
            if (dataLoader->fileExists(candidateJson))
                addOrUpdateProjectEntry(candidateJson);
        }
    }

    bool LauncherApplication::addOrUpdateProjectEntry(const std::string& projectJsonPath)
    {
        if (!g_engineGlobal.resourceLoader)
            return false;

        fs::path pathObj = fs::u8path(projectJsonPath);
        std::error_code ec;
        if (fs::is_directory(pathObj, ec))
            pathObj /= "project.json";

        std::string normalizedPath = NormalizePathString(pathObj);
        ProjectManager loaderMgr(g_engineGlobal.resourceLoader);
        auto cfgRes = loaderMgr.loadProjectConfig(normalizedPath);
        if (!cfgRes.ok())
            return false;

        ProjectConfig cfg = cfgRes.value();
        cfg.projectFilePath = normalizedPath;
        cfg.projectDirectory = NormalizePathString(pathObj.parent_path());

        for (size_t i = 0; i < m_projects.size(); ++i)
        {
            if (m_projects[i].projectFilePath == cfg.projectFilePath)
            {
                m_projects[i] = cfg;
                m_selectedProjectIndex = static_cast<int>(i);
                return true;
            }
        }

        m_projects.push_back(cfg);
        m_selectedProjectIndex = static_cast<int>(m_projects.size()) - 1;
        return true;
    }

    void LauncherApplication::logicTick()
    {
        ZoneScoped();
        if (!m_imguiCtx)
            return;

        m_imguiCtx->push([this]()
        {
            if (m_imguiPlugin)
                m_imguiPlugin->syncCallerContext();
            renderUi();
        });
    }

    void LauncherApplication::renderUi()
    {
        ZoneScoped();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        if (ImGui::Begin("MQEngineLauncherRoot", nullptr, windowFlags))
        {
            renderHeaderBar();
            ImGui::Separator();
            renderMainContent();
            renderNewProjectPopup();
        }
        ImGui::End();
    }

    void LauncherApplication::renderHeaderBar()
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "MQEngine Launcher");
        ImGui::SameLine();
        ImGui::TextDisabled("| 工程管理与启动器");

        float rightOffset = ImGui::GetWindowWidth() - 320.0f;
        if (rightOffset > 220.0f)
            ImGui::SameLine(rightOffset);
        else
            ImGui::SameLine();

        if (ImGui::Button("打开现有工程...", ImVec2(110.0f, 0.0f)))
            browseAndOpenProject();

        ImGui::SameLine();
        if (ImGui::Button("新建工程...", ImVec2(95.0f, 0.0f)))
            m_openNewProjectPopup = true;

        ImGui::SameLine();
        if (ImGui::Button("刷新", ImVec2(75.0f, 0.0f)))
        {
            scanProjects();
            m_statusMessage = "已刷新工程列表";
            m_statusIsError = false;
        }
    }

    void LauncherApplication::renderMainContent()
    {
        ImGui::BeginChild("ProjectListPane", ImVec2(260.0f, 0.0f), true);
        renderProjectListPanel();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("ProjectDetailPane", ImVec2(0.0f, 0.0f), true);
        renderProjectDetailPanel();
        ImGui::EndChild();
    }

    void LauncherApplication::renderProjectListPanel()
    {
        ImGui::Text("工程列表 (%zu)", m_projects.size());
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(m_projects.size()); ++i)
        {
            const auto& proj = m_projects[i];
            bool isSelected = (m_selectedProjectIndex == i);
            std::string label = proj.projectName + "##proj_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_selectedProjectIndex = i;
                if (ImGui::IsMouseDoubleClicked(0))
                    launchProjectInApp("Game", proj);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", proj.projectFilePath.c_str());
        }
    }

    void LauncherApplication::renderProjectDetailPanel()
    {
        if (m_selectedProjectIndex < 0 || m_selectedProjectIndex >= static_cast<int>(m_projects.size()))
        {
            ImGui::TextDisabled("请在左侧选择一个工程，或点击上方按钮打开/新建工程。");
            return;
        }

        const auto& config = m_projects[m_selectedProjectIndex];
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "当前选中工程: %s", config.projectName.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("工程版本: %s", config.version.c_str());
        ImGui::TextWrapped("配置文件: %s", config.projectFilePath.c_str());
        ImGui::TextWrapped("工程目录: %s", config.projectDirectory.c_str());
        ImGui::TextWrapped("资源目录: %s", config.resolvedAssetRoot.c_str());
        ImGui::TextWrapped("渲染管线: %s", config.pipelinePath.empty() ? "默认管线" : config.pipelinePath.c_str());
        ImGui::TextWrapped("初始场景 UUID: %s", config.initialSceneUuid.empty() ? "未设置" : config.initialSceneUuid.c_str());

        if (config.settings.count("description"))
        {
            ImGui::Spacing();
            ImGui::TextDisabled("工程说明: %s", config.settings.at("description").c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        renderActionButtons(config);

        if (!m_statusMessage.empty())
        {
            ImGui::Spacing();
            ImVec4 color = m_statusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
            ImGui::TextColored(color, "%s", m_statusMessage.c_str());
        }
    }

    void LauncherApplication::renderActionButtons(const ProjectConfig& config)
    {
        if (ImGui::Button("运行项目 (Game)", ImVec2(180.0f, 42.0f)))
            launchProjectInApp("Game", config);

        ImGui::SameLine();

        if (ImGui::Button("打开编辑器 (Editor)", ImVec2(180.0f, 42.0f)))
            launchProjectInApp("Editor", config);
    }

    void LauncherApplication::renderNewProjectPopup()
    {
        if (m_openNewProjectPopup)
        {
            ImGui::OpenPopup("新建工程");
            m_openNewProjectPopup = false;
        }

        if (ImGui::BeginPopupModal("新建工程", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("工程名称", m_newProjectNameBuf, sizeof(m_newProjectNameBuf));
            ImGui::InputText("存放父目录", m_newProjectParentDirBuf, sizeof(m_newProjectParentDirBuf));
            ImGui::Spacing();

            if (ImGui::Button("创建", ImVec2(120.0f, 0.0f)))
            {
                createNewProject();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void LauncherApplication::browseAndOpenProject()
    {
        const char* filterPatterns[1] = { "*.json" };
        const char* selectedFile = tinyfd_openFileDialog(
            "选择 MQEngine 工程文件 (project.json)",
            "",
            1,
            filterPatterns,
            "Project JSON (project.json)",
            0);

        if (!selectedFile)
            return;

        if (addOrUpdateProjectEntry(selectedFile))
        {
            m_statusMessage = std::string("成功加载工程: ") + selectedFile;
            m_statusIsError = false;
        }
        else
        {
            m_statusMessage = std::string("无效的工程配置文件: ") + selectedFile;
            m_statusIsError = true;
        }
    }

    void LauncherApplication::createNewProject()
    {
        std::string projName = m_newProjectNameBuf;
        std::string parentDir = m_newProjectParentDirBuf;
        if (projName.empty())
        {
            m_statusMessage = "工程名称不能为空";
            m_statusIsError = true;
            return;
        }

        std::string relProjDir = parentDir.empty() ? projName : (parentDir + "/" + projName);
        std::string projDir = resolveWorkspacePath(relProjDir);
        auto st = initializeProjectDirectory(projDir, projName, kDefaultDemoSceneUuid);
        if (!st.ok())
        {
            m_statusMessage = std::string("创建工程失败: ") + std::string(st.message());
            m_statusIsError = true;
            return;
        }

        addOrUpdateProjectEntry(projDir + "/project.json");
        m_statusMessage = "已成功创建工程: " + projName;
        m_statusIsError = false;
    }

    void LauncherApplication::launchProjectInApp(const std::string& appName, const ProjectConfig& config)
    {
        auto exeRes = locateExecutable(appName);
        if (!exeRes.ok())
        {
            m_statusMessage = std::string(exeRes.status().message());
            m_statusIsError = true;
            spdlog::error("{}", m_statusMessage);
            return;
        }

        auto st = spawnProcess(exeRes.value(), config.projectFilePath, config.projectDirectory);
        if (!st.ok())
        {
            m_statusMessage = std::string("启动失败: ") + std::string(st.message());
            m_statusIsError = true;
            spdlog::error("{}", m_statusMessage);
            return;
        }

        m_statusMessage = "已启动 " + appName + " -> " + config.projectName;
        m_statusIsError = false;
        spdlog::info("{}", m_statusMessage);
    }

    StatusOr<std::string> LauncherApplication::locateExecutable(const std::string& appName) const
    {
        std::string exeFileName = appName + ".exe";
        std::vector<fs::path> candidates;

#ifdef _WIN32
        wchar_t modulePathBuf[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePathBuf, MAX_PATH) > 0)
        {
            fs::path launcherExeDir = fs::path(modulePathBuf).parent_path();
            candidates.push_back(launcherExeDir / exeFileName);
            candidates.push_back(launcherExeDir.parent_path() / appName / exeFileName);
        }
#endif
        candidates.push_back(fs::u8path(resolveWorkspacePath("Output/build-windows-debug/Applications/" + appName + "/" + exeFileName)));
        candidates.push_back(fs::u8path(resolveWorkspacePath("Output/build-windows-release/Applications/" + appName + "/" + exeFileName)));
        candidates.push_back(fs::u8path(resolveWorkspacePath("build-windows-debug/Applications/" + appName + "/" + exeFileName)));
        candidates.push_back(fs::u8path(resolveWorkspacePath("Applications/" + appName + "/" + exeFileName)));

        std::error_code ec;
        for (const auto& candidate : candidates)
        {
            if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec))
                return NormalizePathString(candidate);
        }

        return NotFoundError("未找到可执行程序 " + exeFileName + "，请先编译 " + appName + " 目标");
    }

    Status LauncherApplication::spawnProcess(const std::string& exePath,
                                             const std::string& projectFilePath,
                                             const std::string& workingDir)
    {
#ifdef _WIN32
        fs::path exeFsPath = fs::u8path(exePath);
        fs::path projFsPath = fs::u8path(projectFilePath);
        fs::path workFsDir = workingDir.empty() ? projFsPath.parent_path() : fs::u8path(workingDir);

        std::wstring cmdLine = L"\"" + exeFsPath.wstring() + L"\" --project \"" + projFsPath.wstring() + L"\"";
        std::wstring workDirW = workFsDir.wstring();

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');

        BOOL ok = CreateProcessW(
            exeFsPath.c_str(),
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            workDirW.empty() ? nullptr : workDirW.c_str(),
            &si,
            &pi);

        if (!ok)
            return InternalError("CreateProcessW 调用失败，错误码: " + std::to_string(GetLastError()));

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return OkStatus();
#else
        return UnimplementedError("当前平台暂未实现子进程启动");
#endif
    }


}
