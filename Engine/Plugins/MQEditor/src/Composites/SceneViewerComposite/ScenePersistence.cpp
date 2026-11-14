/**
 * @file ScenePersistence.cpp
 * @brief 场景新建、打开、保存、另存为、主场景设置与 EditorSession.json 会话持久化实现
 */
#include "SceneViewerComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/config/ConfigDocument.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/EditorUiComposite/UiSignals.h>
#include <Composites/EditorUiComposite/ProjectLauncherService.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace detail
        {
            // Note: Boost.PFR 要求反射类型具备外部链接，不可放入匿名命名空间
            struct EditorSessionSnapshot
            {
                std::string last_opened_scene;
                bool use_game_camera_preview = false;
                bool editor_camera_orthographic = false;
                double editor_camera_ortho_size = 12.0;
                double editor_camera_fov = 60.0;
                std::vector<double> editor_camera_pos{0.0, 4.5, -9.5};
                std::vector<double> editor_camera_target{0.0, 1.0, 0.0};
                std::vector<double> editor_camera_rot{18.0, 0.0, 0.0};
            };
        } // namespace detail

        using detail::EditorSessionSnapshot;

        namespace
        {
            std::string normalizePathSlashes(std::string_view raw)
            {
                if (raw.empty())
                {
                    return "";
                }
                std::string norm = SAI::Path::lexicallyNormal(raw);
                while (norm.size() > 1 && norm.back() == '/')
                {
                    if (norm.size() == 3 && norm[1] == ':')
                    {
                        break;
                    }
                    norm.pop_back();
                }
                return norm;
            }

            std::string toLowerAsciiCopy(std::string_view input)
            {
                std::string out(input);
                std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return out;
            }

            std::string getActiveProjectDirectorySafe(EditorBase::EditorUiComposite* ui)
            {
                std::string dir;
                if (ui)
                {
                    dir = ui->getActiveProjectDirectory();
                }
                if (dir.empty())
                {
                    if (auto* resMod = Resource::Resource::InstancePtr())
                    {
                        dir = resMod->getProjectDir();
                        if (dir.empty() && resMod->getConfig())
                        {
                            dir = resMod->getConfig()->getProjectDirectory();
                        }
                    }
                }
                dir = normalizePathSlashes(dir);
                return (dir == ".") ? std::string("") : dir;
            }

            bool isAbsoluteDiskPath(std::string_view path)
            {
                if (path.empty())
                {
                    return false;
                }
                if (path.front() == '/')
                {
                    return true;
                }
                return path.size() >= 2 &&
                       std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
                       path[1] == ':';
            }

            std::string stripLeadingDotSlash(std::string path)
            {
                while (path.size() >= 2 && path[0] == '.' && path[1] == '/')
                {
                    path.erase(0, 2);
                }
                return path;
            }
        } // namespace

        std::string SceneViewerComposite::resolveProjectPath(const std::string& path) const
        {
            std::string clean = stripLeadingDotSlash(normalizePathSlashes(path));
            if (clean.empty())
            {
                return "";
            }
            if (isAbsoluteDiskPath(clean))
            {
                return clean;
            }

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            const std::string projDir = getActiveProjectDirectorySafe(m_editorUiComposite);
            if (!projDir.empty())
            {
                const std::string joined = normalizePathSlashes(SAI::Path::join(projDir, clean));
                if (loader && loader->exists(joined))
                {
                    return joined;
                }
                if (!loader || !loader->exists(clean))
                {
                    return joined;
                }
            }
            return clean;
        }

        std::string SceneViewerComposite::toProjectRelativePath(const std::string& path) const
        {
            std::string normPath = stripLeadingDotSlash(normalizePathSlashes(path));
            if (normPath.empty())
            {
                return "";
            }
            const std::string projDir = getActiveProjectDirectorySafe(m_editorUiComposite);
            if (projDir.empty())
            {
                return normPath;
            }

            const std::string prefix = projDir + "/";
            if (normPath.size() > prefix.size() &&
                toLowerAsciiCopy(std::string_view(normPath.data(), prefix.size())) == toLowerAsciiCopy(prefix))
            {
                return normPath.substr(prefix.size());
            }
            return normPath;
        }

        std::string SceneViewerComposite::getEditorSessionFilePath() const
        {
            const std::string projDir = getActiveProjectDirectorySafe(m_editorUiComposite);
            if (projDir.empty())
            {
                return "Saved/Config/EditorSession.json";
            }
            return normalizePathSlashes(SAI::Path::join(projDir, "Saved/Config/EditorSession.json"));
        }

        void SceneViewerComposite::updateEditorUiSceneState() const
        {
            if (!m_editorUiComposite)
            {
                return;
            }
            auto& menuState = m_editorUiComposite->getUiContext().mutableState<EditorBase::MainMenuViewState>();
            menuState.activeScenePath = m_activeScenePath;
            menuState.activeSceneName = m_activeScenePath.empty() ? std::string("") : SAI::Path::stem(m_activeScenePath);
            menuState.mainScenePath = m_mainScenePath;
        }

        void SceneViewerComposite::registerSceneSignalHandlers()
        {
            if (m_sceneSignalsRegistered || !m_editorUiComposite)
            {
                return;
            }
            auto& uiCtx = m_editorUiComposite->getUiContext();
            uiCtx.registerSignalHandler<EditorBase::NewSceneSignal>([this](const EditorBase::NewSceneSignal&) {
                (void)newEmptyScene();
            });
            uiCtx.registerSignalHandler<EditorBase::OpenSceneSignal>([this](const EditorBase::OpenSceneSignal& sig) {
                (void)openSceneFromFile(sig.scenePath);
            });
            uiCtx.registerSignalHandler<EditorBase::SaveActiveSceneSignal>([this](const EditorBase::SaveActiveSceneSignal&) {
                (void)saveActiveScene();
            });
            uiCtx.registerSignalHandler<EditorBase::SaveSceneAsSignal>([this](const EditorBase::SaveSceneAsSignal& sig) {
                (void)saveSceneAs(sig.scenePath);
            });
            uiCtx.registerSignalHandler<EditorBase::SetCurrentSceneAsMainSignal>([this](const EditorBase::SetCurrentSceneAsMainSignal&) {
                (void)setCurrentSceneAsMain();
            });
            m_sceneSignalsRegistered = true;
        }

        void SceneViewerComposite::loadEditorSession()
        {
            ZoneScoped();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            const std::string sessionPath = getEditorSessionFilePath();
            if (!loader || !loader->exists(sessionPath))
            {
                return;
            }

            StatusOr<EditorSessionSnapshot> loadedOr =
                ConfigDocument::loadStructFromFile<EditorSessionSnapshot>(loader, sessionPath, ConfigFormat::Json);
            if (!loadedOr.ok())
            {
                return;
            }

            const EditorSessionSnapshot& snap = loadedOr.value();
            m_useGameCameraPreview = snap.use_game_camera_preview;
            m_cameraSource = m_useGameCameraPreview
                ? ViewportCameraSource::ActiveGameMain
                : ViewportCameraSource::EditorFree;
            m_editorCameraOrthographic = snap.editor_camera_orthographic;
            m_editorCameraOrthoSize = static_cast<float>(std::max(snap.editor_camera_ortho_size, 0.5));
            m_cameraFov = static_cast<float>(std::clamp(snap.editor_camera_fov, 15.0, 140.0));
            if (snap.editor_camera_pos.size() == 3)
            {
                for (int i = 0; i < 3; ++i) m_cameraPos[i] = static_cast<float>(snap.editor_camera_pos[i]);
            }
            if (snap.editor_camera_target.size() == 3)
            {
                for (int i = 0; i < 3; ++i) m_cameraTarget[i] = static_cast<float>(snap.editor_camera_target[i]);
            }
            if (snap.editor_camera_rot.size() == 3)
            {
                for (int i = 0; i < 3; ++i) m_cameraRot[i] = static_cast<float>(snap.editor_camera_rot[i]);
            }
        }

        void SceneViewerComposite::saveEditorSession() const
        {
            ZoneScoped();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return;
            }

            const std::string sessionPath = getEditorSessionFilePath();
            const std::string parentDir = normalizePathSlashes(SAI::Path::parentPath(sessionPath));
            if (!parentDir.empty())
            {
                (void)loader->ensureDir(parentDir);
            }

            EditorSessionSnapshot snap;
            if (ConfigDocument::loadStructFromFile<EditorSessionSnapshot>(loader, sessionPath, ConfigFormat::Json).ok())
            {
                auto existingOr = ConfigDocument::loadStructFromFile<EditorSessionSnapshot>(loader, sessionPath, ConfigFormat::Json);
                if (existingOr.ok())
                {
                    snap = std::move(existingOr.value());
                }
            }

            if (!m_activeScenePath.empty())
            {
                snap.last_opened_scene = m_activeScenePath;
            }
            snap.use_game_camera_preview = m_useGameCameraPreview;
            snap.editor_camera_orthographic = m_editorCameraOrthographic;
            snap.editor_camera_ortho_size = static_cast<double>(m_editorCameraOrthoSize);
            snap.editor_camera_fov = static_cast<double>(m_cameraFov);
            snap.editor_camera_pos = {m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]};
            snap.editor_camera_target = {m_cameraTarget[0], m_cameraTarget[1], m_cameraTarget[2]};
            snap.editor_camera_rot = {m_cameraRot[0], m_cameraRot[1], m_cameraRot[2]};

            (void)ConfigDocument::saveStructToFile(loader, sessionPath, snap, ConfigFormat::Json);
        }

        void SceneViewerComposite::syncProjectStartupStateIfReady()
        {
            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            if (!scene)
            {
                return;
            }

            const std::string curProjDir = getActiveProjectDirectorySafe(m_editorUiComposite);
            if (m_initialStartupResolved && curProjDir == m_boundProjectDir)
            {
                return;
            }

            if (isPiePlaying())
            {
                stopPieSession();
            }

            m_initialStartupResolved = true;
            m_boundProjectDir = curProjDir;

            if (!curProjDir.empty())
            {
                const std::string nativeDir = normalizePathSlashes(SAI::Path::join(curProjDir, "Native"));
                (void)domain->gameplay.loadProjectNativeScriptLibraries(nativeDir);
            }

            loadEditorSession();
            loadInitialStartupScene();
            ensureEditorCameraEntity();
            syncEntitiesFromDomain();
            syncCameraToRenderer();
        }

        void SceneViewerComposite::loadInitialStartupScene()
        {
            ZoneScoped();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            auto* cfg = resMod ? resMod->getConfig() : nullptr;

            if (cfg)
            {
                const auto& proj = cfg->getCurrentProject();
                m_mainScenePath = !proj.startupScene.empty()
                    ? toProjectRelativePath(proj.startupScene)
                    : toProjectRelativePath(proj.initialSceneUuid);
            }

            auto tryOpenCandidate = [&](const std::string& rawPath) -> bool {
                if (rawPath.empty() || !loader)
                {
                    return false;
                }
                const std::string resolved = resolveProjectPath(rawPath);
                if (resolved.empty() || !loader->exists(resolved))
                {
                    return false;
                }
                return openSceneFromFile(resolved).ok();
            };

            // 1. CLI --scene 最高优先级
            if (cfg && tryOpenCandidate(cfg->getScenePath()))
            {
                return;
            }

            // 2. Saved/Config/EditorSession.json 的 last_opened_scene
            if (loader)
            {
                const std::string sessionPath = getEditorSessionFilePath();
                if (loader->exists(sessionPath))
                {
                    auto snapOr = ConfigDocument::loadStructFromFile<EditorSessionSnapshot>(loader, sessionPath, ConfigFormat::Json);
                    if (snapOr.ok() && tryOpenCandidate(snapOr.value().last_opened_scene))
                    {
                        return;
                    }
                }
            }

            // 3. project.mq 的 startup_scene
            if (tryOpenCandidate(m_mainScenePath))
            {
                return;
            }

            // 4. 兜底：保持纯空场景，绝不制造任何硬编码演示实体
            m_activeScenePath.clear();
            m_entities.clear();
            m_selectedEntityId = 0;
            m_nextEntityId = 1;
            updateEditorUiSceneState();
            spdlog::info("[SceneViewerComposite] Initial startup scene is empty (no CLI/session/startup_scene)");
        }

        Status SceneViewerComposite::newEmptyScene()
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            if (scene)
            {
                scene->clearScene(false);
            }
            if (m_editorUiComposite)
            {
                m_editorUiComposite->getCommandStack().clear();
            }

            m_entities.clear();
            m_selectedEntityId = 0;
            m_nextEntityId = 1;
            m_activeScenePath.clear();
            updateEditorUiSceneState();
            return OkStatus();
        }

        Status SceneViewerComposite::openSceneFromFile(const std::string& scenePath)
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!scene || !loader)
            {
                return FailedPreconditionError("SceneSubsystem 或 ResourceLoader 尚未初始化");
            }

            const std::string resolvedPath = resolveProjectPath(scenePath);
            if (resolvedPath.empty() || !loader->exists(resolvedPath))
            {
                const std::string msg = "场景文件不存在: " + resolvedPath;
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Error, msg, "Scene");
                    m_editorUiComposite->showToast("打开场景失败", msg, EditorBase::LogEntry::Level::Error, 3.2f);
                }
                return NotFoundError(msg);
            }

            Status st = scene->loadSceneFromFile(resolvedPath, loader);
            if (!st.ok())
            {
                const std::string msg = std::string(st.message());
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Error, "加载场景失败: " + msg, "Scene");
                    m_editorUiComposite->showToast("加载场景失败", msg, EditorBase::LogEntry::Level::Error, 3.2f);
                }
                return st;
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->getCommandStack().clear();
            }
            m_entities.clear();
            m_selectedEntityId = 0;
            m_nextEntityId = 1;
            syncEntitiesFromDomain();

            m_activeScenePath = toProjectRelativePath(resolvedPath);
            saveEditorSession();
            updateEditorUiSceneState();
            spdlog::info("[SceneViewerComposite] Opened scene '{}' (resolved='{}', entities={})",
                         m_activeScenePath, resolvedPath, m_entities.size());

            if (m_editorUiComposite)
            {
                const std::string msg = std::string(m_editorUiComposite->tr("已打开场景: ", "Opened scene: ")) + m_activeScenePath;
                m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Info, msg, "Scene");
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("场景已打开", "Scene Opened"),
                    m_activeScenePath,
                    EditorBase::LogEntry::Level::Info,
                    2.5f);
            }
            return OkStatus();
        }

        Status SceneViewerComposite::saveActiveScene()
        {
            ZoneScoped();
            if (m_activeScenePath.empty())
            {
                std::string scenesDir = "Scenes";
                const std::string projDir = getActiveProjectDirectorySafe(m_editorUiComposite);
                if (!projDir.empty())
                {
                    scenesDir = normalizePathSlashes(SAI::Path::join(projDir, "Scenes"));
                }
                auto pickedOr = EditorBase::ProjectLauncherService::pickSaveSceneFileDialog(scenesDir, "NewScene.mqscene");
                if (!pickedOr.ok())
                {
                    if (m_editorUiComposite)
                    {
                        m_editorUiComposite->showToast(
                            m_editorUiComposite->tr("未保存场景", "Scene Not Saved"),
                            m_editorUiComposite->tr("请选择“文件 -> 场景另存为...”指定保存路径", "Use File -> Save Scene As... to choose a path"),
                            EditorBase::LogEntry::Level::Warn,
                            2.8f);
                    }
                    return pickedOr.status();
                }
                return saveSceneAs(pickedOr.value());
            }
            return saveSceneAs(m_activeScenePath);
        }

        Status SceneViewerComposite::saveSceneAs(const std::string& targetPath)
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!scene || !loader)
            {
                return FailedPreconditionError("SceneSubsystem 或 ResourceLoader 尚未初始化");
            }

            std::string normalizedTarget = targetPath;
            if (SAI::Path::extension(normalizedTarget).empty())
            {
                normalizedTarget += ".mqscene";
            }
            const std::string resolvedPath = resolveProjectPath(normalizedTarget);
            if (resolvedPath.empty())
            {
                return InvalidArgumentError("目标场景路径为空");
            }

            const std::string parentDir = normalizePathSlashes(SAI::Path::parentPath(resolvedPath));
            if (!parentDir.empty())
            {
                CHECK_STATUS(loader->ensureDir(parentDir));
            }

            Status st = scene->saveSceneToFile(resolvedPath, loader);
            if (!st.ok())
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Error, std::string(st.message()), "Scene");
                    m_editorUiComposite->showToast("保存场景失败", std::string(st.message()), EditorBase::LogEntry::Level::Error, 3.2f);
                }
                return st;
            }

            m_activeScenePath = toProjectRelativePath(resolvedPath);
            saveEditorSession();
            updateEditorUiSceneState();
            spdlog::info("[SceneViewerComposite] Saved scene '{}' (resolved='{}', entities={})",
                         m_activeScenePath, resolvedPath, m_entities.size());

            if (m_editorUiComposite)
            {
                const std::string msg = std::string(m_editorUiComposite->tr("场景已保存: ", "Saved scene: ")) + m_activeScenePath;
                m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Info, msg, "Scene");
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("场景已保存", "Scene Saved"),
                    m_activeScenePath,
                    EditorBase::LogEntry::Level::Info,
                    2.5f);
            }
            return OkStatus();
        }

        Status SceneViewerComposite::setCurrentSceneAsMain()
        {
            ZoneScoped();
            if (m_activeScenePath.empty())
            {
                CHECK_STATUS(saveActiveScene());
                if (m_activeScenePath.empty())
                {
                    return FailedPreconditionError("请先保存当前场景再设为主场景");
                }
            }

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            auto* cfg = resMod ? resMod->getConfig() : nullptr;
            const std::string projDir = getActiveProjectDirectorySafe(m_editorUiComposite);
            if (!cfg || !loader || projDir.empty())
            {
                return FailedPreconditionError("当前未打开工程，无法修改 project.mq 主场景");
            }

            const std::string projectFile = normalizePathSlashes(SAI::Path::join(projDir, Resource::Config::kDefaultProjectFileName));
            StatusOr<ProjectConfig> loadedOr = cfg->loadProjectConfig(projectFile, loader);
            CHECK_STATUS(loadedOr.status());

            ProjectConfig updated = std::move(loadedOr.value());
            updated.startupScene = m_activeScenePath;
            updated.initialSceneUuid = m_activeScenePath;
            CHECK_STATUS(cfg->saveProjectConfig(projectFile, updated, loader));
            (void)cfg->activateProjectConfig(projectFile, loader);

            m_mainScenePath = m_activeScenePath;
            updateEditorUiSceneState();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->addLog(
                    EditorBase::LogEntry::Level::Info,
                    std::string(m_editorUiComposite->tr("已将工程主场景设为: ", "Set project main scene: ")) + m_mainScenePath,
                    "Scene");
                m_editorUiComposite->showToast(
                    m_editorUiComposite->tr("主场景已更新", "Main Scene Updated"),
                    m_mainScenePath,
                    EditorBase::LogEntry::Level::Info,
                    2.8f);
            }
            return OkStatus();
        }

        std::string SceneViewerComposite::getStartupSceneDisplayHint() const
        {
            std::string startupScene = m_mainScenePath;
            if (auto* resMod = Resource::Resource::InstancePtr(); resMod && resMod->getConfig())
            {
                const auto& proj = resMod->getConfig()->getCurrentProject();
                if (!proj.startupScene.empty())
                {
                    startupScene = proj.startupScene;
                }
            }
            return startupScene;
        }

        void SceneViewerComposite::savePrePieState()
        {
            ZoneScoped();
            m_prePieScenePath = m_activeScenePath;
            m_prePieBackupFilePath.clear();
            m_prePieSwitchedScene = false;
            m_prePieCameraSource = m_cameraSource;
            m_prePieSpecificCameraRegistry = m_specificCameraRegistry;
            m_prePieSpecificCameraEcsEntity = m_specificCameraEcsEntity;
            m_prePieUseGameCameraPreview = m_useGameCameraPreview;
            m_prePieSelectedEntityId = m_selectedEntityId;
            m_prePieEditorCameraOrthographic = m_editorCameraOrthographic;
            m_prePieEditorCameraOrthoSize = m_editorCameraOrthoSize;
            for (int i = 0; i < 3; ++i)
            {
                m_prePieCameraPos[i] = m_cameraPos[i];
                m_prePieCameraTarget[i] = m_cameraTarget[i];
                m_prePieCameraRot[i] = m_cameraRot[i];
            }
            m_prePieEntities = m_entities;

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            if (scene)
            {
                scene->getReplay().savePieSnapshot(scene->getRegistry());
                scene->getReplay().startRecording();
                scene->getReplay().captureKeyframe(0, scene->getRegistry());
            }
        }

        void SceneViewerComposite::restorePrePieState()
        {
            ZoneScoped();
            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;

            if (scene)
            {
                scene->getReplay().stopRecording();
                if (m_prePieSwitchedScene)
                {
                    scene->clearScene(false);
                    if (!m_prePieBackupFilePath.empty() && loader && loader->exists(m_prePieBackupFilePath))
                    {
                        (void)scene->loadSceneFromFile(m_prePieBackupFilePath, loader);
                    }
                    else if (!m_prePieScenePath.empty() && loader)
                    {
                        const std::string prevAbs = resolveProjectPath(m_prePieScenePath);
                        if (!prevAbs.empty() && loader->exists(prevAbs))
                        {
                            (void)scene->loadSceneFromFile(prevAbs, loader);
                        }
                    }
                    else
                    {
                        m_entities = m_prePieEntities;
                        for (auto& ent : m_entities)
                        {
                            ent.ecsEntity = FCT::ECS::null;
                            syncEntityToDomain(ent);
                        }
                    }
                    m_entities.clear();
                    m_nextEntityId = 1;
                    syncEntitiesFromDomain();
                }
                else
                {
                    (void)scene->getReplay().restorePieSnapshot(scene->getRegistry());
                    auto rbView = scene->getRegistry().view<Domain::Scene::RigidBodyComponent>();
                    for (auto ent : rbView)
                    {
                        auto& rb = rbView.get<Domain::Scene::RigidBodyComponent>(ent);
                        rb.linearVelocity = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
                        rb.angularVelocity = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
                    }
                    syncEntitiesFromDomain();
                }
            }

            m_activeScenePath = m_prePieScenePath;
            m_cameraSource = m_prePieCameraSource;
            m_specificCameraRegistry = m_prePieSpecificCameraRegistry;
            m_specificCameraEcsEntity = m_prePieSpecificCameraEcsEntity;
            m_useGameCameraPreview = m_prePieUseGameCameraPreview;
            m_selectedEntityId = m_prePieSelectedEntityId;
            m_editorCameraOrthographic = m_prePieEditorCameraOrthographic;
            m_editorCameraOrthoSize = m_prePieEditorCameraOrthoSize;
            for (int i = 0; i < 3; ++i)
            {
                m_cameraPos[i] = m_prePieCameraPos[i];
                m_cameraTarget[i] = m_prePieCameraTarget[i];
                m_cameraRot[i] = m_prePieCameraRot[i];
            }
            m_prePieEntities.clear();
            m_prePieBackupFilePath.clear();
            m_prePieSwitchedScene = false;
            updateEditorUiSceneState();
        }

        void SceneViewerComposite::startPieSession(PieStartMode mode)
        {
            ZoneScoped();
            if (isPiePlaying())
            {
                return;
            }

            m_pieStartMode = mode;
            savePrePieState();

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;

            if (mode == PieStartMode::FromMainScene)
            {
                const std::string startupHint = getStartupSceneDisplayHint();
                if (!startupHint.empty())
                {
                    m_mainScenePath = toProjectRelativePath(startupHint);
                }
                const std::string startupAbs = resolveProjectPath(startupHint);
                if (!startupHint.empty() && loader && loader->exists(startupAbs) && scene)
                {
                    const std::string backupAbs = resolveProjectPath("Saved/Temp/PiePreSceneBackup.mqscene");
                    if (!backupAbs.empty())
                    {
                        const std::string backupParent = normalizePathSlashes(SAI::Path::parentPath(backupAbs));
                        if (backupParent.empty() || loader->ensureDir(backupParent).ok())
                        {
                            if (scene->saveSceneToFile(backupAbs, loader).ok())
                            {
                                m_prePieBackupFilePath = backupAbs;
                            }
                        }
                    }

                    scene->clearScene(false);
                    Status loadSt = scene->loadSceneFromFile(startupAbs, loader);
                    if (!loadSt.ok())
                    {
                        if (m_editorUiComposite)
                        {
                            m_editorUiComposite->addLog(
                                EditorBase::LogEntry::Level::Error,
                                std::string("PIE 加载主场景失败: ") + std::string(loadSt.message()),
                                "SceneViewer");
                        }
                        m_prePieSwitchedScene = true;
                        restorePrePieState();
                        return;
                    }
                    m_prePieSwitchedScene = true;
                    m_activeScenePath = toProjectRelativePath(startupAbs);
                    m_entities.clear();
                    m_selectedEntityId = 0;
                    m_nextEntityId = 1;
                    syncEntitiesFromDomain();
                    updateEditorUiSceneState();
                }
                else if (m_editorUiComposite)
                {
                    m_editorUiComposite->addLog(
                        EditorBase::LogEntry::Level::Warn,
                        m_editorUiComposite->tr(
                            "未配置有效的 project.mq startup_scene，已回退从当前场景启动 PIE",
                            "No valid startup_scene configured in project.mq, falling back to current scene"),
                        "SceneViewer");
                }
            }

            m_wasPiePlaying = true;
            m_pieTimeAccum = 0.0f;
            m_cameraSource = ViewportCameraSource::ActiveGameMain;
            syncCameraToRenderer();

            if (auto* engine = Runtime::Engine::getInstance())
            {
                Status tagSt = engine->addApplicationTag("Game");
                if (!tagSt.ok())
                {
                    m_wasPiePlaying = false;
                    restorePrePieState();
                    syncCameraToRenderer();
                    if (m_editorUiComposite)
                    {
                        const std::string errMsg = std::string(tagSt.message());
                        m_editorUiComposite->addLog(
                            EditorBase::LogEntry::Level::Error,
                            "激活 Game 运行标签失败: " + errMsg,
                            "SceneViewer");
                        m_editorUiComposite->showToast(
                            m_editorUiComposite->tr("启动失败", "Play Failed"),
                            errMsg,
                            EditorBase::LogEntry::Level::Error,
                            3.2f);
                    }
                    return;
                }
            }

            syncEntitiesFromDomain();
            syncCameraToRenderer();
            spdlog::info("[SceneViewerComposite] Started PIE session (mode={}, scene='{}')",
                         mode == PieStartMode::FromMainScene ? "FromMainScene" : "FromCurrentScene",
                         m_activeScenePath);

            if (m_editorUiComposite)
            {
                const char* logMsg = (mode == PieStartMode::FromMainScene)
                    ? m_editorUiComposite->tr(
                        "已从主场景进入 PIE 运行模式（Game 标签已激活：Native、RmlUi、物理与 MainWindow 相机就绪）",
                        "Entered PIE from Main Scene (Game tag active: Native, RmlUi, physics and MainWindow camera)")
                    : m_editorUiComposite->tr(
                        "已从当前场景进入 PIE 运行模式（Game 标签已激活：Native、RmlUi、物理与 MainWindow 相机就绪）",
                        "Entered PIE from Current Scene (Game tag active: Native, RmlUi, physics and MainWindow camera)");
                m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Info, logMsg, "SceneViewer");
            }
        }

        void SceneViewerComposite::stopPieSession()
        {
            ZoneScoped();
            if (!isPiePlaying())
            {
                return;
            }

            if (auto* engine = Runtime::Engine::getInstance())
            {
                engine->removeApplicationTag("Game");
            }
            m_wasPiePlaying = false;

            restorePrePieState();
            syncCameraToRenderer();
            spdlog::info("[SceneViewerComposite] Stopped PIE session (restored scene='{}')", m_activeScenePath);

            if (m_editorUiComposite)
            {
                m_editorUiComposite->addLog(
                    EditorBase::LogEntry::Level::Info,
                    m_editorUiComposite->tr(
                        "已退出 PIE 运行模式，Game 标签已移除且编辑态场景与自由相机已完整恢复",
                        "Exited PIE mode, Game tag removed and edit-mode scene restored"),
                    "SceneViewer");
            }
        }
    } // namespace MQEditor
} // namespace MQEngine
