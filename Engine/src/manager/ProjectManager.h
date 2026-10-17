#ifndef ENGINE_PROJECTMANAGER_H
#define ENGINE_PROJECTMANAGER_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "../plugin/PluginManager.h"
#include "../plugin/PluginContext.h"
#include "../data/ResourceLoader.h"
#include "../data/DataManager.h"
#include "PipelineManager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace MQEngine {

class Engine;

/**
 * @brief 工程配置数据结构
 */
struct ProjectConfig {
    std::string projectName;
    std::string version = "1.0.0";
    std::string projectFilePath;
    std::string projectDirectory;
    std::string assetRoot = "res";
    std::string resolvedAssetRoot;
    std::string pipelinePath;
    std::string resolvedPipelinePath;
    std::vector<std::string> plugins;
    std::string initialSceneUuid;
    std::unordered_map<std::string, std::string> settings;
};

/**
 * @brief 工程项目管理器，负责解析工程配置并驱动资产根重定向、管线加载与插件初始化
 */
class ENGINE_API ProjectManager {
public:
    explicit ProjectManager(ResourceLoader* loader = nullptr);
    virtual ~ProjectManager();

    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    /**
     * @brief 通过 ResourceLoader 读取并解析工程文件 (.mqproj / project.json)
     */
    StatusOr<ProjectConfig> loadProjectConfig(const std::string& projectFilePath);

    /**
     * @brief 将工程配置序列化并保存至指定路径
     */
    Status saveProjectConfig(const std::string& projectFilePath, const ProjectConfig& config);

    /**
     * @brief 打开工程文件并动态重定向资产根目录、加载渲染管线及初始化插件
     * @param projectFilePath .mqproj 或 project.json 的文件路径
     * @param engine 目标引擎实例指针
     */
    Status openProject(const std::string& projectFilePath, Engine* engine);

    /**
     * @brief 关闭当前工程，卸载插件并重置状态
     */
    Status closeProject(Engine* engine);

    /**
     * @brief 检查当前是否已打开工程
     */
    bool isProjectOpen() const { return m_isProjectOpen; }

    /**
     * @brief 获取当前工程配置
     */
    const ProjectConfig& getCurrentProject() const { return m_currentConfig; }

    /**
     * @brief 获取当前工程所在根目录
     */
    const std::string& getProjectDirectory() const { return m_currentConfig.projectDirectory; }

    /**
     * @brief 获取解析后的资产绝对或工作区路径
     */
    const std::string& getAssetRoot() const { return m_currentConfig.resolvedAssetRoot; }

    /**
     * @brief 获取插件管理器指针
     */
    PluginManager* getPluginManager() { return m_pluginManager.get(); }
    const PluginManager* getPluginManager() const { return m_pluginManager.get(); }

    /**
     * @brief 设置/替换自定义 ResourceLoader
     */
    void setResourceLoader(ResourceLoader* loader) { m_resourceLoader = loader; }

private:
    ResourceLoader* m_resourceLoader = nullptr;
    std::unique_ptr<ResourceLoader> m_defaultLoader;
    std::unique_ptr<PluginManager> m_pluginManager;
    ProjectConfig m_currentConfig;
    bool m_isProjectOpen = false;

    Status parseProjectJson(const std::string& jsonContent, const std::string& projectFilePath, ProjectConfig& outConfig);
    std::string resolvePath(const std::string& basePath, const std::string& relativeOrAbsolutePath) const;
    Status applyPipeline(Engine* engine);
    Status applyPlugins(Engine* engine);
};

} // namespace MQEngine

#endif // ENGINE_PROJECTMANAGER_H
