#ifndef MQENGINE_LAUNCHER_COMPOSITE_H
#define MQENGINE_LAUNCHER_COMPOSITE_H

#include "thirdparty/thirdparty.h"
#include "IHtmlGameUiService.h"
#include <string>
#include <vector>

#ifdef _WIN32
    #ifdef BUILD_LAUNCHER_NATIVE
        #define LAUNCHER_NATIVE_API __declspec(dllexport)
    #else
        #define LAUNCHER_NATIVE_API __declspec(dllimport)
    #endif
#else
    #define LAUNCHER_NATIVE_API
#endif

namespace MQEngine {

/**
 * @brief 工程创建时的脚本与用户代码语言勾选项
 */
struct LAUNCHER_NATIVE_API ProjectScriptOptions {
    bool enableTypeScript = true;
    bool enableJavaScript = false;
    bool enablePython = false;
    bool enableCppUserCode = false;
};

/**
 * @brief 启动器功能组合体，基于 HTML Game UI 插件体系承载工程发现、创建与启动管理全套业务逻辑
 */
class LAUNCHER_NATIVE_API LauncherComposite : public Runtime::Composite {
public:
    LauncherComposite() = default;
    ~LauncherComposite() override = default;

    void registerInfo() override;
    Status onLoad(Runtime::Engine* engine) override;
    Status onInit(Runtime::Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

    void refreshUiData();
    void selectProject(int index);

private:
    void locateWorkspaceRoot();
    std::string resolveWorkspacePath(const std::string& relativePath) const;
    void setupDefaultCamera();
    void cleanupDefaultCamera();

    void setupHtmlUiBindings();
    void openNewProjectModal();
    void closeNewProjectModal();
    void handleCreateProjectFromModal();
    void handleLaunchCurrentProject(const std::string& appName);

    void ensureDemoProjectInitialized();
    Status initializeProjectDirectory(const std::string& projectDir,
                                      const std::string& projectName,
                                      const std::string& sceneUuid,
                                      const ProjectScriptOptions& scriptOptions);
    Status ensureProjectBaseDirectories(const std::string& projectDir,
                                        const std::string& assetRoot,
                                        const ProjectScriptOptions& scriptOptions);
    Status ensureProjectConfigFile(const std::string& projectDir,
                                   const std::string& projectName,
                                   const std::string& sceneUuid,
                                   const ProjectScriptOptions& scriptOptions);
    Status ensureDefaultPipelineConfig(const std::string& assetRoot);
    Status initializeSelectedScriptLanguages(const std::string& projectDir,
                                             const std::string& assetRoot,
                                             const std::string& projectName,
                                             const ProjectScriptOptions& scriptOptions);
    Status ensureNodeEnginePackageModule(const std::string& scriptsRoot);
    Status ensureScriptsPackageJson(const std::string& scriptsRoot);
    Status initializeTypeScriptEnvironment(const std::string& scriptsRoot);
    Status initializeJavaScriptEnvironment(const std::string& scriptsRoot);
    Status initializePythonEnvironment(const std::string& scriptsRoot);
    Status initializeCppUserCodeEnvironment(const std::string& projectDir,
                                            const std::string& projectName);
    Status saveFileIfMissing(const std::string& filePath, const std::string& content);
    std::string formatSelectedScriptLanguages(const ProjectScriptOptions& scriptOptions) const;
    std::string detectProjectScriptLanguagesSummary(const ProjectConfig& config) const;

    Status ensureSceneBootstrapFiles(const std::string& assetRoot,
                                     const std::string& sceneName,
                                     const std::string& sceneUuid);
    void ensureDefaultDemoModels(const std::string& assetRoot);
    void importBuiltinModel(const std::string& assetRoot,
                            const std::string& modelDirName,
                            const std::string& modelUuid,
                            const std::string& sourceModelPath);
    void populateDefaultSceneEntities(const std::string& sceneUuid);

    void scanProjects();
    void scanDirectoryForProjects(const std::string& rootDir);
    bool addOrUpdateProjectEntry(const std::string& projectJsonPath);

    void browseAndOpenProject();
    void createNewProject(const std::string& name,
                          const std::string& parentDir,
                          const ProjectScriptOptions& scriptOptions);
    void launchProjectInApp(const std::string& appName, const ProjectConfig& config);
    StatusOr<std::string> locateExecutable(const std::string& appName) const;
    Status spawnProcess(const std::string& exePath,
                        const std::string& projectFilePath,
                        const std::string& workingDir);

private:
    Runtime::Engine* m_engine = nullptr;
    IHtmlGameUiService* m_htmlUiService = nullptr;
    entt::registry m_registry;
    entt::entity m_cameraEntity = entt::null;

    std::string m_workspaceRoot;
    std::vector<ProjectConfig> m_projects;
    int m_selectedProjectIndex = -1;
    std::string m_statusMessage;
    bool m_statusIsError = false;

    char m_newProjectNameBuf[128] = "NewProject";
    char m_newProjectParentDirBuf[256] = "Example";
    ProjectScriptOptions m_newProjectScriptOptions{};
};

} // namespace MQEngine

#endif // MQENGINE_LAUNCHER_COMPOSITE_H
