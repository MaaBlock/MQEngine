#ifndef ENGINE_LAUNCHER_APPLICATION_H
#define ENGINE_LAUNCHER_APPLICATION_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    /**
     * @brief 启动器应用程序，负责工程发现、创建以及启动运行态 (Game) 或编辑器 (Editor)
     */
    class LauncherApplication : public Application
    {
    public:
        LauncherApplication() = default;
        ~LauncherApplication() override = default;

        void init() override;
        void logicTick() override;
        RenderConfig renderConfig() const override;

    private:
        void setupRenderCallbacks();
        void setupDefaultCamera();
        void ensureDemoProjectInitialized();
        Status initializeProjectDirectory(const std::string& projectDir,
                                          const std::string& projectName,
                                          const std::string& sceneUuid);
        Status ensureSceneBootstrapFiles(const std::string& assetRoot,
                                         const std::string& sceneName,
                                         const std::string& sceneUuid);
        void populateDefaultSceneEntities(const std::string& sceneUuid);

        void scanProjects();
        void scanDirectoryForProjects(const std::string& rootDir);
        bool addOrUpdateProjectEntry(const std::string& projectJsonPath);

        void renderUi();
        void renderHeaderBar();
        void renderMainContent();
        void renderProjectListPanel();
        void renderProjectDetailPanel();
        void renderActionButtons(const ProjectConfig& config);
        void renderNewProjectPopup();

        void browseAndOpenProject();
        void createNewProject();
        void launchProjectInApp(const std::string& appName, const ProjectConfig& config);
        StatusOr<std::string> locateExecutable(const std::string& appName) const;
        Status spawnProcess(const std::string& exePath,
                            const std::string& projectFilePath,
                            const std::string& workingDir);

    private:
        FCT::ImguiModule m_imguiModule;
        FCT::ImguiContext* m_imguiCtx = nullptr;
        entt::registry m_registry;
        entt::entity m_cameraEntity = entt::null;

        std::vector<ProjectConfig> m_projects;
        int m_selectedProjectIndex = -1;
        std::string m_statusMessage;
        bool m_statusIsError = false;

        bool m_openNewProjectPopup = false;
        char m_newProjectNameBuf[128] = "NewProject";
        char m_newProjectParentDirBuf[256] = "Example";
    };
}

#endif // ENGINE_LAUNCHER_APPLICATION_H
