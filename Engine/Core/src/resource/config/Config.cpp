#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/resource.h>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            std::string normalizeNativeDirPath(const std::string& projectDir, const std::string& nativeSubDir)
            {
                if (projectDir.empty()) {
                    return "";
                }
                const std::string subDir = nativeSubDir.empty() ? std::string("Native") : nativeSubDir;
                std::error_code ec;
                std::filesystem::path combined = std::filesystem::path(projectDir) / std::filesystem::path(subDir);
                std::string normalized = std::filesystem::absolute(combined, ec).lexically_normal().generic_string();
                while (normalized.size() > 3 && normalized.back() == '/') {
                    normalized.pop_back();
                }
                return normalized;
            }
        }

        ResourceLoader* Config::resolveLoader(ResourceLoader* explicitLoader)
        {
            if (explicitLoader) {
                return explicitLoader;
            }
            if (auto* res = Resource::InstancePtr()) {
                if (res->getResourceLoader()) {
                    return res->getResourceLoader();
                }
            }
            if (auto* loader = DiskResourceLoader::InstancePtr()) {
                return loader;
            }
            static DiskResourceLoader s_fallbackLoader;
            return &s_fallbackLoader;
        }

        StatusOr<ProjectConfig> Config::loadProjectConfig(const std::string& projectFilePath,
                                                          ResourceLoader* loader)
        {
            ResourceLoader* activeLoader = resolveLoader(loader);
            auto docRes = ConfigDocument::loadFromFile(activeLoader, projectFilePath);
            if (!docRes.ok()) {
                spdlog::error("Resource::Config: 无法读取或解析工程文件: {}, 错误: {}",
                              projectFilePath, docRes.status().message());
                return docRes.status();
            }

            ProjectConfig config;
            CHECK_STATUS(parseProjectConfigDocument(docRes.value(), projectFilePath, config));
            return config;
        }

        Status Config::parseProjectConfigDocument(const ConfigDocument& doc,
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

        void Config::populateProjectIdentity(const ConfigDocument& doc,
                                             const std::string& projectFilePath,
                                             ProjectConfig& outConfig) const
        {
            std::error_code ec;
            std::filesystem::path projFileFs(projectFilePath);
            if (!projectFilePath.empty()) {
                projFileFs = std::filesystem::absolute(projFileFs, ec).lexically_normal();
            }
            outConfig.projectFilePath = projFileFs.empty() ? projectFilePath : projFileFs.generic_string();
            std::string dirStr = SAI::Path::directoryOrDot(outConfig.projectFilePath);
            if (!dirStr.empty()) {
                std::filesystem::path dirFs = std::filesystem::absolute(std::filesystem::path(dirStr), ec).lexically_normal();
                dirStr = dirFs.generic_string();
                while (dirStr.size() > 3 && dirStr.back() == '/') {
                    dirStr.pop_back();
                }
            }
            outConfig.projectDirectory = dirStr;

            outConfig.projectName = doc.getStringOr(
                {"projectName", "project.name", "name"}, outConfig.projectName);
            if (outConfig.projectName.empty()) {
                std::string stemName = SAI::Path::stem(outConfig.projectFilePath);
                if (stemName == "project" && !outConfig.projectDirectory.empty()) {
                    stemName = SAI::Path::fileName(outConfig.projectDirectory);
                }
                outConfig.projectName = stemName.empty() ? outConfig.projectFilePath : stemName;
            }

            outConfig.version = doc.getStringOr(
                {"version", "project.version"},
                outConfig.version.empty() ? "1.0.0" : outConfig.version);
        }

        void Config::populateProjectPaths(const ConfigDocument& doc,
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

            const std::string existingStartup =
                !outConfig.startupScene.empty() ? outConfig.startupScene : outConfig.initialSceneUuid;
            const std::string resolvedStartupScene = doc.getStringOr(
                {"startup_scene", "startupScene", "initialSceneUuid", "initialScene", "scene", "project.startup_scene", "project.initialSceneUuid"},
                existingStartup);
            outConfig.startupScene = resolvedStartupScene;
            outConfig.initialSceneUuid = resolvedStartupScene;

            outConfig.nativePath = doc.getStringOr(
                {"paths.native", "native", "paths.nativeDir", "project.native"},
                outConfig.nativePath.empty() ? "Native" : outConfig.nativePath);
            outConfig.resolvedNativeDir = normalizeNativeDirPath(outConfig.projectDirectory, outConfig.nativePath);
        }

        void Config::populateProjectModules(const ConfigDocument& doc,
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

        void Config::buildProjectConfigDocument(const ProjectConfig& config,
                                                ConfigDocument& outDoc) const
        {
            outDoc.writeStruct(config);
            outDoc.root().removeKey("projectFilePath");
            outDoc.root().removeKey("projectDirectory");
            outDoc.root().removeKey("resolvedAssetRoot");
            outDoc.root().removeKey("resolvedPipelinePath");
            outDoc.root().removeKey("resolvedNativeDir");
            outDoc.root().removeKey("nativePath");
            if (!config.nativePath.empty()) {
                outDoc.root().setString("paths.native", config.nativePath);
            }

            if (config.userCode.empty()) {
                outDoc.root().removeKey("userCode");
            }
            if (config.userCodes.empty()) {
                outDoc.root().removeKey("userCodes");
            }
            outDoc.root().removeKey("startupScene");
            outDoc.root().removeKey("initialSceneUuid");
            const std::string effectiveStartupScene =
                !config.startupScene.empty() ? config.startupScene : config.initialSceneUuid;
            if (!effectiveStartupScene.empty()) {
                outDoc.root().setString("startup_scene", effectiveStartupScene);
            } else {
                outDoc.root().removeKey("startup_scene");
            }
        }

        Status Config::saveProjectConfig(const std::string& projectFilePath,
                                         const ProjectConfig& config,
                                         ResourceLoader* loader)
        {
            ResourceLoader* activeLoader = resolveLoader(loader);
            ConfigDocument doc;
            buildProjectConfigDocument(config, doc);
            return doc.saveToFile(activeLoader, projectFilePath, ConfigFormat::Auto);
        }

        Status Config::activateProjectConfig(const std::string& projectFilePath,
                                             ResourceLoader* loader)
        {
            auto configRes = loadProjectConfig(projectFilePath, loader);
            if (!configRes.ok()) {
                return configRes.status();
            }
            m_currentConfig = configRes.value();
            m_isProjectOpen = true;
            if (!m_currentConfig.projectDirectory.empty()) {
                setProjectDir(m_currentConfig.projectDirectory);
                (void)SAI::System::setCurrentDirectory(m_currentConfig.projectDirectory);
            }
            if (!m_currentConfig.resolvedAssetRoot.empty()) {
                m_projectAssetsDir = m_currentConfig.resolvedAssetRoot;
            }
            if (!m_currentConfig.resolvedNativeDir.empty()) {
                m_projectNativeDir = m_currentConfig.resolvedNativeDir;
            } else if (!m_projectDir.empty()) {
                m_projectNativeDir = normalizeNativeDirPath(m_projectDir, m_currentConfig.nativePath);
            }
            return OkStatus();
        }

        void Config::clearProjectConfig()
        {
            m_isProjectOpen = false;
            m_currentConfig = ProjectConfig{};
            resolveCanonicalPaths("");
            m_projectNativeDir.clear();
        }

        void Config::setEngineDir(const std::string& engineDir)
        {
            if (engineDir.empty()) {
                return;
            }
            std::filesystem::path p(engineDir);
            m_engineDir = std::filesystem::absolute(p).lexically_normal().generic_string();
            m_enginePluginsDir = m_engineDir + "/Plugins";
        }

        void Config::setProjectDir(const std::string& projectDir)
        {
            if (projectDir.empty()) {
                return;
            }
            std::filesystem::path p(projectDir);
            m_projectDir = std::filesystem::absolute(p).lexically_normal().generic_string();
            m_projectPluginsDir = m_projectDir + "/Plugins";
            m_projectAssetsDir = m_currentConfig.resolvedAssetRoot.empty() ? (m_projectDir + "/Assets") : m_currentConfig.resolvedAssetRoot;
            m_projectScenesDir = m_projectDir + "/Scenes";
            m_projectNativeDir = (!m_currentConfig.resolvedNativeDir.empty() && m_isProjectOpen && m_currentConfig.projectDirectory == m_projectDir)
                ? m_currentConfig.resolvedNativeDir
                : normalizeNativeDirPath(m_projectDir, m_currentConfig.nativePath);
        }

        void Config::resolveCanonicalPaths(const std::string& explicitProjectPath)
        {
            // 1. 自省解析 EngineDir（基于当前进程可执行文件绝对路径）
            std::string exeDir;
#ifdef _WIN32
            wchar_t wideBuf[MAX_PATH] = {0};
            DWORD len = GetModuleFileNameW(nullptr, wideBuf, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                std::filesystem::path exeFsPath(wideBuf);
                exeDir = exeFsPath.parent_path().generic_string();
            }
#endif
            if (exeDir.empty()) {
                exeDir = std::filesystem::current_path().generic_string();
            }

            // 依据可执行文件目录自省查找引擎与 Plugins 根目录
            std::filesystem::path exePath(exeDir);
            std::error_code ec;
            if (std::filesystem::exists(exePath / "Plugins", ec)) {
                m_engineDir = std::filesystem::absolute(exePath).lexically_normal().generic_string();
                m_enginePluginsDir = m_engineDir + "/Plugins";
            } else if (std::filesystem::exists(exePath / ".." / ".." / "Plugins", ec)) {
                m_engineDir = std::filesystem::absolute(exePath / ".." / "..").lexically_normal().generic_string();
                m_enginePluginsDir = m_engineDir + "/Plugins";
            } else if (std::filesystem::exists(exePath / ".." / "Plugins", ec)) {
                m_engineDir = std::filesystem::absolute(exePath / "..").lexically_normal().generic_string();
                m_enginePluginsDir = m_engineDir + "/Plugins";
            } else if (std::filesystem::exists(exePath / ".." / ".." / "Engine" / "Plugins", ec)) {
                m_engineDir = std::filesystem::absolute(exePath / ".." / "..").lexically_normal().generic_string();
                m_enginePluginsDir = m_engineDir + "/Engine/Plugins";
            } else {
                // 回退到当前工作目录探测
                std::filesystem::path cwd = std::filesystem::current_path();
                if (std::filesystem::exists(cwd / "Engine" / "Plugins", ec)) {
                    m_engineDir = std::filesystem::absolute(cwd).lexically_normal().generic_string();
                    m_enginePluginsDir = m_engineDir + "/Engine/Plugins";
                } else if (std::filesystem::exists(cwd / "Plugins", ec)) {
                    m_engineDir = std::filesystem::absolute(cwd).lexically_normal().generic_string();
                    m_enginePluginsDir = m_engineDir + "/Plugins";
                } else {
                    m_engineDir = std::filesystem::absolute(exePath).lexically_normal().generic_string();
                    m_enginePluginsDir = m_engineDir + "/Plugins";
                }
            }

            // 2. 自省解析 ProjectDir（优先使用显式指定工程路径，其次 projectPath，最后当前工作目录）
            std::string rawProjPath = explicitProjectPath;
            if (rawProjPath.empty()) {
                rawProjPath = projectPath;
            }
            while (!rawProjPath.empty() && (rawProjPath.front() == '"' || rawProjPath.front() == '\'' || rawProjPath.front() == ' ')) {
                rawProjPath.erase(rawProjPath.begin());
            }
            while (!rawProjPath.empty() && (rawProjPath.back() == '"' || rawProjPath.back() == '\'' || rawProjPath.back() == ' ')) {
                rawProjPath.pop_back();
            }

            if (!rawProjPath.empty()) {
                std::filesystem::path projPath(rawProjPath);
                if (std::filesystem::is_directory(projPath, ec)) {
                    m_projectDir = std::filesystem::absolute(projPath, ec).lexically_normal().generic_string();
                } else {
                    m_projectDir = std::filesystem::absolute(projPath.parent_path(), ec).lexically_normal().generic_string();
                }
                while (m_projectDir.size() > 3 && m_projectDir.back() == '/') {
                    m_projectDir.pop_back();
                }
                m_projectNativeDir = normalizeNativeDirPath(m_projectDir, m_currentConfig.nativePath);
            } else {
                m_projectDir = std::filesystem::current_path().lexically_normal().generic_string();
                if (m_isProjectOpen && !m_currentConfig.projectDirectory.empty()) {
                    m_projectNativeDir = !m_currentConfig.resolvedNativeDir.empty()
                        ? m_currentConfig.resolvedNativeDir
                        : normalizeNativeDirPath(m_projectDir, m_currentConfig.nativePath);
                } else {
                    m_projectNativeDir.clear();
                }
            }

            m_projectPluginsDir = m_projectDir + "/Plugins";
            m_projectAssetsDir = m_currentConfig.resolvedAssetRoot.empty() ? (m_projectDir + "/Assets") : m_currentConfig.resolvedAssetRoot;
            m_projectScenesDir = m_projectDir + "/Scenes";
        }

        void Config::parseCommandLine(int argc, char* argv[])
        {
            // Note: 解析命令行 argv[1]，若非以 '--' 或 '-' 开头的选项参数，则作为启动目标外部文件路径捕获
            if (argc > 1 && argv != nullptr && argv[1] != nullptr) {
                std::string firstArg = argv[1];
                if (firstArg.rfind("--", 0) != 0 && firstArg.rfind("-", 0) != 0) {
                    m_startupFilePath = std::move(firstArg);
                }
            }
        }

        void Config::parseCommandLine(const std::vector<std::string>& args)
        {
            // Note: 解析 args[1]，若非选项标记则捕获为 m_startupFilePath
            if (args.size() > 1) {
                const std::string& firstArg = args[1];
                if (firstArg.rfind("--", 0) != 0 && firstArg.rfind("-", 0) != 0) {
                    m_startupFilePath = firstArg;
                }
            }
        }

    } // namespace Resource
} // namespace MQEngine
