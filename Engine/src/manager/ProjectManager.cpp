#include "ProjectManager.h"
#include "../core/engine.h"
#include "../data/DiskResourceLoader.h"
#include "../data/DataManager.h"
#include "../plugin/PluginManager.h"
#include "../plugin/PluginContext.h"
#include "PipelineManager.h"
#include <boost/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace MQEngine {

using absl::FailedPreconditionError;

namespace fs = std::filesystem;

ProjectManager::ProjectManager(ResourceLoader* loader)
    : m_resourceLoader(loader),
      m_pluginManager(std::make_unique<PluginManager>()) {
}

ProjectManager::~ProjectManager() = default;

StatusOr<ProjectConfig> ProjectManager::loadProjectConfig(const std::string& projectFilePath) {
    if (!m_resourceLoader) {
        if (!m_defaultLoader) {
            m_defaultLoader = std::make_unique<DiskResourceLoader>();
        }
        m_resourceLoader = m_defaultLoader.get();
    }

    auto contentRes = m_resourceLoader->loadFile(projectFilePath);
    if (!contentRes.ok()) {
        spdlog::error("ProjectManager: 无法读取工程文件: {}, 错误: {}", projectFilePath, contentRes.status().message());
        return contentRes.status();
    }

    ProjectConfig config;
    Status parseStatus = parseProjectJson(contentRes.value(), projectFilePath, config);
    if (!parseStatus.ok()) {
        return parseStatus;
    }

    return config;
}

Status ProjectManager::parseProjectJson(const std::string& jsonContent,
                                        const std::string& projectFilePath,
                                        ProjectConfig& outConfig) {
    boost::system::error_code ec;
    auto jv = boost::json::parse(jsonContent, ec);
    if (ec) {
        return InvalidArgumentError(StrCat("解析工程 JSON 失败: ", ec.message()));
    }

    try {
        if (!jv.is_object()) {
            return InvalidArgumentError("工程配置文件格式无效：根节点必须为 JSON 对象");
        }

        const auto& obj = jv.as_object();

        outConfig.projectFilePath = projectFilePath;
        try {
            fs::path p = fs::u8path(projectFilePath);
            outConfig.projectDirectory = p.has_parent_path() ? std::string((const char*)p.parent_path().generic_u8string().c_str()) : ".";

            if (obj.contains("projectName") && obj.at("projectName").is_string()) {
                outConfig.projectName = obj.at("projectName").as_string().c_str();
            } else if (obj.contains("name") && obj.at("name").is_string()) {
                outConfig.projectName = obj.at("name").as_string().c_str();
            } else {
                outConfig.projectName = (const char*)p.stem().generic_u8string().c_str();
            }
        } catch (...) {
            outConfig.projectDirectory = ".";
            if (obj.contains("projectName") && obj.at("projectName").is_string()) {
                outConfig.projectName = obj.at("projectName").as_string().c_str();
            } else if (obj.contains("name") && obj.at("name").is_string()) {
                outConfig.projectName = obj.at("name").as_string().c_str();
            } else {
                outConfig.projectName = projectFilePath;
            }
        }

        if (obj.contains("version") && obj.at("version").is_string()) {
            outConfig.version = obj.at("version").as_string().c_str();
        }

        if (obj.contains("assetRoot") && obj.at("assetRoot").is_string()) {
            outConfig.assetRoot = obj.at("assetRoot").as_string().c_str();
        } else if (obj.contains("assetsRoot") && obj.at("assetsRoot").is_string()) {
            outConfig.assetRoot = obj.at("assetsRoot").as_string().c_str();
        } else if (obj.contains("assets") && obj.at("assets").is_string()) {
            outConfig.assetRoot = obj.at("assets").as_string().c_str();
        } else {
            outConfig.assetRoot = "res";
        }
        outConfig.resolvedAssetRoot = resolvePath(outConfig.projectDirectory, outConfig.assetRoot);

        if (obj.contains("pipelinePath") && obj.at("pipelinePath").is_string()) {
            outConfig.pipelinePath = obj.at("pipelinePath").as_string().c_str();
        } else if (obj.contains("pipeline") && obj.at("pipeline").is_string()) {
            outConfig.pipelinePath = obj.at("pipeline").as_string().c_str();
        }
        if (!outConfig.pipelinePath.empty()) {
            outConfig.resolvedPipelinePath = resolvePath(outConfig.projectDirectory, outConfig.pipelinePath);
        }

        outConfig.plugins.clear();
        if (obj.contains("plugins") && obj.at("plugins").is_array()) {
            for (const auto& item : obj.at("plugins").as_array()) {
                if (item.is_string()) {
                    outConfig.plugins.emplace_back(item.as_string().c_str());
                }
            }
        }

        if (obj.contains("initialSceneUuid") && obj.at("initialSceneUuid").is_string()) {
            outConfig.initialSceneUuid = obj.at("initialSceneUuid").as_string().c_str();
        } else if (obj.contains("initialScene") && obj.at("initialScene").is_string()) {
            outConfig.initialSceneUuid = obj.at("initialScene").as_string().c_str();
        } else if (obj.contains("scene") && obj.at("scene").is_string()) {
            outConfig.initialSceneUuid = obj.at("scene").as_string().c_str();
        }

        if (obj.contains("settings") && obj.at("settings").is_object()) {
            for (const auto& [k, v] : obj.at("settings").as_object()) {
                if (v.is_string()) {
                    outConfig.settings[std::string(k)] = v.as_string().c_str();
                } else {
                    outConfig.settings[std::string(k)] = boost::json::serialize(v);
                }
            }
        }

        return OkStatus();
    } catch (const std::exception& e) {
        return InvalidArgumentError(StrCat("解析工程 JSON 失败: ", e.what()));
    }
}

std::string ProjectManager::resolvePath(const std::string& basePath, const std::string& relativeOrAbsolutePath) const {
    try {
        fs::path p = fs::u8path(relativeOrAbsolutePath);
        if (p.is_absolute()) {
            return std::string((const char*)p.lexically_normal().generic_u8string().c_str());
        }
        fs::path base = fs::u8path(basePath);
        return std::string((const char*)(base / p).lexically_normal().generic_u8string().c_str());
    } catch (...) {
        if (basePath.empty() || basePath == ".") {
            return relativeOrAbsolutePath;
        }
        return basePath + "/" + relativeOrAbsolutePath;
    }
}

Status ProjectManager::openProject(const std::string& projectFilePath, Engine* engine) {
    if (!engine) {
        return InvalidArgumentError("openProject: Engine 指针为空");
    }

    if (m_isProjectOpen) {
        Status closeStatus = closeProject(engine);
        if (!closeStatus.ok()) {
            spdlog::warn("ProjectManager: 关闭前一个工程失败: {}", closeStatus.message());
        }
    }

    auto configRes = loadProjectConfig(projectFilePath);
    if (!configRes.ok()) {
        return configRes.status();
    }

    m_currentConfig = configRes.value();

    if (engine->getDataManager()) {
        engine->getDataManager()->setAssetRoot(m_currentConfig.resolvedAssetRoot);
        spdlog::info("ProjectManager: DataManager 资产根路径重定向为: {}", m_currentConfig.resolvedAssetRoot);
    }

    Status pipeStatus = applyPipeline(engine);
    if (!pipeStatus.ok()) {
        return pipeStatus;
    }

    Status pluginStatus = applyPlugins(engine);
    if (!pluginStatus.ok()) {
        return pluginStatus;
    }

    if (!m_currentConfig.initialSceneUuid.empty() && engine->getDataManager()) {
        try {
            engine->getDataManager()->openScene(m_currentConfig.initialSceneUuid);
            spdlog::info("ProjectManager: 已打开初始场景: {}", m_currentConfig.initialSceneUuid);
        } catch (const std::exception& e) {
            spdlog::warn("ProjectManager: 打开初始场景警告: {}", e.what());
        }
    }

    m_isProjectOpen = true;
    spdlog::info("ProjectManager: 成功打开工程 [{}] (版本: {})", m_currentConfig.projectName, m_currentConfig.version);
    return OkStatus();
}

Status ProjectManager::applyPipeline(Engine* engine) {
    if (m_currentConfig.resolvedPipelinePath.empty()) {
        return OkStatus();
    }

    auto* pipeMgr = engine->getPipelineManager();
    if (!pipeMgr) {
        return FailedPreconditionError("ProjectManager: Engine 未包含有效的 PipelineManager");
    }

    auto* graph = engine->getContext() ? engine->getContext()->getModule<FCT::RenderGraph>() : nullptr;
    auto* techMgr = engine->getTechManager();
    auto* wnd = engine->getWindow();
    auto targetType = engine->getApplication() ? engine->getApplication()->renderConfig().target : RenderTarget::Window;

    Status status = pipeMgr->loadAndApplyPipeline(m_currentConfig.resolvedPipelinePath, graph, techMgr, wnd, targetType);
    if (!status.ok()) {
        spdlog::error("ProjectManager: 加载管线配置失败 [{}]: {}", m_currentConfig.resolvedPipelinePath, status.message());
        return status;
    }

    spdlog::info("ProjectManager: 成功加载并应用工程管线: {}", m_currentConfig.resolvedPipelinePath);
    return OkStatus();
}

Status ProjectManager::applyPlugins(Engine* engine) {
    if (m_currentConfig.plugins.empty()) {
        return OkStatus();
    }

    PluginContext ctx;
    ctx.engine = engine;
    ctx.renderGraph = engine->getContext() ? engine->getContext()->getModule<FCT::RenderGraph>() : nullptr;
    ctx.techManager = engine->getTechManager();
    ctx.systemManager = &engine->getSystemManager();
    ctx.resourceLoader = m_resourceLoader;
    ctx.dataManager = engine->getDataManager();
    ctx.componentReflection = engine->getComponentReflection();

    for (const auto& pluginIdent : m_currentConfig.plugins) {
        if (pluginIdent.ends_with(".dll") || pluginIdent.ends_with(".so")) {
            std::string dllPath = resolvePath(m_currentConfig.projectDirectory, pluginIdent);
            Status status = m_pluginManager->loadDynamicPlugin(dllPath, ctx);
            if (!status.ok()) {
                spdlog::error("ProjectManager: 加载动态插件失败 [{}]: {}", dllPath, status.message());
                return status;
            }
        } else {
            Status status = m_pluginManager->loadAndInitPlugin(pluginIdent, ctx);
            if (!status.ok()) {
                spdlog::error("ProjectManager: 加载静态插件失败 [{}]: {}", pluginIdent, status.message());
                return status;
            }
        }
    }

    return OkStatus();
}

Status ProjectManager::closeProject(Engine* engine) {
    if (!m_isProjectOpen) {
        return OkStatus();
    }

    m_pluginManager->unloadAll();

    if (engine && engine->getDataManager()) {
        engine->getDataManager()->setAssetRoot("./res");
    }

    m_isProjectOpen = false;
    spdlog::info("ProjectManager: 工程 [{}] 已关闭", m_currentConfig.projectName);
    m_currentConfig = ProjectConfig{};

    return OkStatus();
}

Status ProjectManager::saveProjectConfig(const std::string& projectFilePath, const ProjectConfig& config) {
    if (!m_resourceLoader) {
        if (!m_defaultLoader) {
            m_defaultLoader = std::make_unique<DiskResourceLoader>();
        }
        m_resourceLoader = m_defaultLoader.get();
    }

    boost::json::object obj;
    obj["projectName"] = config.projectName;
    obj["version"] = config.version;
    obj["assetRoot"] = config.assetRoot;
    obj["pipelinePath"] = config.pipelinePath;

    boost::json::array pluginsArray;
    for (const auto& p : config.plugins) {
        pluginsArray.push_back(p.c_str());
    }
    obj["plugins"] = pluginsArray;

    if (!config.initialSceneUuid.empty()) {
        obj["initialSceneUuid"] = config.initialSceneUuid;
    }

    boost::json::object settingsObj;
    for (const auto& [k, v] : config.settings) {
        settingsObj[k] = v;
    }
    obj["settings"] = settingsObj;

    std::string jsonStr = boost::json::serialize(obj);
    return m_resourceLoader->saveFile(projectFilePath, jsonStr);
}

} // namespace MQEngine
