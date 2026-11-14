/**
 * @file ProjectLauncherService.h
 * @brief 工程打开、最近工程记录、工程模板实例化与场景快捷路径服务
 */
#ifndef MQENGINE_EDITOR_BASE_PROJECT_LAUNCHER_SERVICE_H
#define MQENGINE_EDITOR_BASE_PROJECT_LAUNCHER_SERVICE_H

#include "../../EditorBaseApi.h"
#include "ProjectIdeConfigGenerator.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    class ResourceLoader;

    namespace EditorBase
    {
        struct EDITOR_UI_COMPOSITE_API ProjectTemplateInfo
        {
            std::string id;
            std::string templateDir;
            std::string displayName;
            std::string description;
            std::string startupScene;
            bool hasCMakeLists = false;
        };

        struct EDITOR_UI_COMPOSITE_API RecentProjectEntry
        {
            std::string name;
            std::string projectDir;
            std::string startupScene;
            int64_t lastOpenedEpochSec = 0;
            bool existsOnDisk = true;
        };

        struct EDITOR_UI_COMPOSITE_API ProjectSceneFileEntry
        {
            std::string displayRelPath;
            std::string fullPath;
            bool isMainScene = false;
        };

        struct EDITOR_UI_COMPOSITE_API CreateProjectFromTemplateRequest
        {
            std::string templateId;
            std::string templatesRootDir;
            std::string targetParentDir;
            std::string projectName;
            bool syncIdeFiles = true;
            bool openAfterCreate = true;
        };

        struct EDITOR_UI_COMPOSITE_API CreateProjectFromTemplateResult
        {
            std::string projectDir;
            std::string projectName;
            size_t copiedFilesCount = 0;
            bool ideSynced = false;
            bool launchedEditor = false;
        };

        /**
         * @brief 负责模板枚举/实例化、最近工程列表持久化、原生目录/场景选择器以及异步拉起新工程 Editor 实例
         */
        class EDITOR_UI_COMPOSITE_API ProjectLauncherService
        {
        public:
            ProjectLauncherService() = default;
            ~ProjectLauncherService() = default;

            static std::string resolveTemplatesRootDir();
            static std::string resolveEditorExecutablePath();
            static std::string resolveDefaultProjectsParentDir();
            static std::string getDefaultRecentProjectsPath();

            static bool isValidProjectDir(const std::string& projectDir, ResourceLoader* loader = nullptr);
            static bool readProjectMetadata(
                const std::string& projectDir,
                std::string& outName,
                std::string& outStartupScene,
                std::string& outDescription,
                ResourceLoader* loader = nullptr);

            static std::vector<ProjectTemplateInfo> discoverTemplates(
                ResourceLoader* loader = nullptr,
                const std::string& templatesRootOverride = "");

            static std::vector<ProjectSceneFileEntry> discoverProjectScenes(
                const std::string& projectDir,
                ResourceLoader* loader = nullptr);

            static std::vector<RecentProjectEntry> discoverWorkspaceProjects(
                ResourceLoader* loader = nullptr);

            static StatusOr<std::string> pickProjectDirectoryDialog(const std::string& initialDir = "");
            static StatusOr<std::string> pickOpenSceneFileDialog(const std::string& initialDir = "");
            static StatusOr<std::string> pickSaveSceneFileDialog(
                const std::string& initialDir = "",
                const std::string& defaultFileName = "NewScene.mqscene");

            static Status launchEditorProcess(const std::string& projectDir, ResourceLoader* loader = nullptr);

            Status loadRecentProjects(ResourceLoader* loader = nullptr, const std::string& customStoragePath = "");
            Status saveRecentProjects(ResourceLoader* loader = nullptr, const std::string& customStoragePath = "") const;
            Status recordRecentProject(
                const std::string& projectDir,
                ResourceLoader* loader = nullptr,
                const std::string& customStoragePath = "");
            void clearRecentProjects(ResourceLoader* loader = nullptr, const std::string& customStoragePath = "");
            void refreshRecentProjectsStatus(ResourceLoader* loader = nullptr);

            const std::vector<RecentProjectEntry>& getRecentProjects() const noexcept { return m_recentProjects; }

            Status openProject(
                const std::string& projectDir,
                ResourceLoader* loader = nullptr,
                const std::string& customStoragePath = "");

            StatusOr<CreateProjectFromTemplateResult> createProjectFromTemplate(
                const CreateProjectFromTemplateRequest& request,
                ResourceLoader* loader = nullptr,
                const std::string& customStoragePath = "");

        private:
            std::vector<RecentProjectEntry> m_recentProjects;
        };
    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_PROJECT_LAUNCHER_SERVICE_H
