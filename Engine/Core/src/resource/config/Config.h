#ifndef MQENGINE_RESOURCE_CONFIG_CONFIG_H
#define MQENGINE_RESOURCE_CONFIG_CONFIG_H

#include <string>
#include <unordered_map>
#include <vector>
#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/config/ConfigDocument.h>

namespace MQEngine
{
    class ResourceLoader;

    /**
     * @brief 工程配置数据结构（project.mq）
     */
    struct ProjectConfig
    {
        std::string projectName;
        std::string version = "1.0.0";
        std::string projectFilePath;
        std::string projectDirectory;
        std::string assetRoot = "Assets";
        std::string resolvedAssetRoot;
        std::string pipelinePath;
        std::string resolvedPipelinePath;
        std::string nativePath = "Native";
        std::string resolvedNativeDir;
        std::vector<std::string> plugins;
        std::string userCode = "UserCode";
        std::vector<std::string> userCodes;
        std::string startupScene;
        std::string initialSceneUuid;
        std::unordered_map<std::string, std::string> settings;
    };

    namespace Resource
    {
        /**
         * @brief Resource 层运行期配置与工程配置（project.mq）管理类
         */
        class ENGINE_API Config
        {
        public:
            static constexpr const char* kDefaultProjectFileName = "project.mq";

            std::string projectPath;
            std::string local;

            bool hasProjectPath() const noexcept { return !projectPath.empty(); }

            StatusOr<ProjectConfig> loadProjectConfig(const std::string& projectFilePath,
                                                      ResourceLoader* loader = nullptr);

            Status saveProjectConfig(const std::string& projectFilePath,
                                     const ProjectConfig& config,
                                     ResourceLoader* loader = nullptr);

            Status activateProjectConfig(const std::string& projectFilePath,
                                         ResourceLoader* loader = nullptr);

            void clearProjectConfig();

            bool isProjectOpen() const noexcept { return m_isProjectOpen; }
            const ProjectConfig& getCurrentProject() const noexcept { return m_currentConfig; }
            const ProjectConfig& getCurrentConfig() const noexcept { return m_currentConfig; }
            const std::string& getProjectDirectory() const noexcept { return m_currentConfig.projectDirectory; }
            const std::string& getAssetRoot() const noexcept { return m_currentConfig.resolvedAssetRoot; }
            std::string getProjectName() const noexcept { return m_isProjectOpen ? m_currentConfig.projectName : ""; }

            // 双根规范路径自省与获取接口（EngineDir 与 ProjectDir）
            void resolveCanonicalPaths(const std::string& explicitProjectPath = "");

            const std::string& getEngineDir() const noexcept { return m_engineDir; }
            const std::string& getProjectDir() const noexcept { return m_projectDir; }
            std::string getEnginePluginsDir() const { return m_enginePluginsDir; }
            std::string getProjectPluginsDir() const { return m_projectPluginsDir; }
            std::string getProjectNativeDir() const { return m_projectNativeDir; }
            std::string getProjectAssetsDir() const { return m_projectAssetsDir; }
            std::string getProjectScenesDir() const { return m_projectScenesDir; }

            void setEngineDir(const std::string& engineDir);
            void setProjectDir(const std::string& projectDir);

            // 启动时命令行首参数（argv[1]）与 --scene 覆盖路径捕获接口
            const std::string& getStartupFilePath() const noexcept { return m_startupFilePath; }
            void setStartupFilePath(std::string path) { m_startupFilePath = std::move(path); }
            const std::string& getScenePath() const noexcept { return m_scenePath; }
            void setScenePath(std::string path) { m_scenePath = std::move(path); }
            void parseCommandLine(int argc, char* argv[]);
            void parseCommandLine(const std::vector<std::string>& args);

        private:
            ResourceLoader* resolveLoader(ResourceLoader* explicitLoader);
            Status parseProjectConfigDocument(const ConfigDocument& doc,
                                              const std::string& projectFilePath,
                                              ProjectConfig& outConfig);
            void populateProjectIdentity(const ConfigDocument& doc,
                                         const std::string& projectFilePath,
                                         ProjectConfig& outConfig) const;
            void populateProjectPaths(const ConfigDocument& doc, ProjectConfig& outConfig) const;
            void populateProjectModules(const ConfigDocument& doc, ProjectConfig& outConfig) const;
            void buildProjectConfigDocument(const ProjectConfig& config, ConfigDocument& outDoc) const;

            ProjectConfig m_currentConfig;
            bool m_isProjectOpen = false;

            std::string m_engineDir;
            std::string m_projectDir;
            std::string m_enginePluginsDir;
            std::string m_projectPluginsDir;
            std::string m_projectNativeDir;
            std::string m_projectAssetsDir;
            std::string m_projectScenesDir;
            std::string m_startupFilePath;
            std::string m_scenePath;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CONFIG_CONFIG_H
