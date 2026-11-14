#include <MQEngine/resource/core/resource.h>
#include <MQEngine/runtime/core/Engine.h>
#include <cassert>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            std::string normalizeCliPathValue(std::string_view raw)
            {
                while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) {
                    raw.remove_prefix(1);
                }
                while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) {
                    raw.remove_suffix(1);
                }
                if (raw.size() >= 2 &&
                    ((raw.front() == '"' && raw.back() == '"') ||
                     (raw.front() == '\'' && raw.back() == '\''))) {
                    raw.remove_prefix(1);
                    raw.remove_suffix(1);
                }
                if (!raw.empty() && raw.back() == '"') {
                    raw.remove_suffix(1);
                }
                std::string result(raw);
                std::replace(result.begin(), result.end(), '\\', '/');
                while (result.size() > 3 && result.back() == '/') {
                    result.pop_back();
                }
                return result;
            }

            /**
             * @brief Resource 层命令行参数职责链处理器：解析 --project / -p、--scene / -s 与 --local 参数存入 Resource::Config
             */
            class ResourceCommandLineHandler : public ICommandLineHandler
            {
            public:
                explicit ResourceCommandLineHandler(Config& config) noexcept
                    : m_config(config)
                {
                }

            protected:
                bool onHandle(const CommandLineArg& arg) override
                {
                    if (arg.matchesKey("project") || arg.matchesKey("p")) {
                        m_config.projectPath = normalizeCliPathValue(arg.value);
                        return true;
                    }
                    if (arg.matchesKey("scene") || arg.matchesKey("s")) {
                        m_config.setScenePath(normalizeCliPathValue(arg.value));
                        return true;
                    }
                    if (arg.matchesKey("local")) {
                        m_config.local = arg.value;
                        return true;
                    }
                    if (arg.isPositional && m_config.getStartupFilePath().empty()) {
                        m_config.setStartupFilePath(normalizeCliPathValue(arg.value));
                        return true;
                    }
                    return false;
                }

            private:
                Config& m_config;
            };
        } // namespace

        Resource::Resource()
            : arena(2 * 1024 * 1024, MemoryTag::Arena)
        {
        }

        Resource::~Resource()
        {
            term();
        }

        void Resource::registerInfo()
        {
            memoryTracker.registerInfo();
            blockPoolManager.registerInfo();
            gpuMemoryManager.registerInfo();
            tableRegistry.registerInfo();
            resourceLoader.registerInfo();
            dataLoader.registerInfo();
            commandLineParser.registerInfo();
            textureDecoderRegistry.registerInfo();

            registerCommandLineHandler(makeUnique<ResourceCommandLineHandler>(config));
        }

        Status Resource::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");

            commandLineParser.parse();
            if (config.getStartupFilePath().empty()) {
                config.parseCommandLine(commandLineParser.getRawArgs());
            }
            CHECK_STATUS(textureDecoderRegistry.init());

            // 依据命令行参数与执行路径统一自省解析双根规范路径（先将 --project 规范为绝对路径）
            if (config.hasProjectPath()) {
                std::error_code pathEc;
                std::filesystem::path projFs(config.projectPath);
                if (!projFs.is_absolute()) {
                    projFs = std::filesystem::absolute(projFs, pathEc);
                }
                std::string normalizedProj = projFs.lexically_normal().generic_string();
                while (normalizedProj.size() > 3 && normalizedProj.back() == '/') {
                    normalizedProj.pop_back();
                }
                config.projectPath = normalizedProj;
            }
            config.resolveCanonicalPaths(config.projectPath);

            if (!config.local.empty()) {
                SetLocal(config.local.c_str());
            } else if (engine->application != nullptr) {
                SetLocal(engine->application->applicationRegisterInfo.local);
            }

            if (config.hasProjectPath()) {
                if (SAI::FileSystem::isDirectory(config.projectPath)) {
                    config.setProjectDir(config.projectPath);
                    std::string candidateProjFile = SAI::Path::join(config.projectPath, Config::kDefaultProjectFileName);
                    if (!SAI::FileSystem::isFile(candidateProjFile)) {
                        candidateProjFile.clear();
                        auto dirEntries = resourceLoader.readDir(config.projectPath);
                        if (dirEntries.ok()) {
                            for (const auto& entry : *dirEntries) {
                                if (entry && entry->extension() == ".mqproj") {
                                    candidateProjFile = entry->path();
                                    break;
                                }
                            }
                        }
                    }
                    if (!candidateProjFile.empty()) {
                        Status cfgSt = config.activateProjectConfig(candidateProjFile, &resourceLoader);
                        if (!cfgSt.ok()) {
                            spdlog::error("Resource: 初始化加载工程配置失败 [{}]: {}",
                                          candidateProjFile, std::string(cfgSt.message()));
                        }
                    } else {
                        spdlog::warn("Resource: 工程目录 [{}] 下未找到 project.mq 或 .mqproj 配置文件",
                                     config.projectPath);
                    }
                } else if (SAI::FileSystem::isFile(config.projectPath)) {
                    Status cfgSt = config.activateProjectConfig(config.projectPath, &resourceLoader);
                    if (!cfgSt.ok()) {
                        spdlog::error("Resource: 初始化加载工程配置失败 [{}]: {}",
                                      config.projectPath, std::string(cfgSt.message()));
                    }
                } else {
                    spdlog::error("Resource: 指定的 --project 路径不存在: [{}]", config.projectPath);
                }
            } else if (SAI::FileSystem::isFile(Config::kDefaultProjectFileName)) {
                (void)config.activateProjectConfig(Config::kDefaultProjectFileName, &resourceLoader);
            }

            if (config.isProjectOpen() &&
                !config.getAssetRoot().empty()) {
                m_assetRoot = config.getAssetRoot();
            } else {
                m_assetRoot = config.getProjectAssetsDir();
            }

            m_initialized = true;
            return OkStatus();
        }

        void Resource::term()
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;

            gpuMemoryManager.flushAllDeferredReleases();
            textureDecoderRegistry.term();
            tableRegistry.clear();
            config.clearProjectConfig();
            arena.reset();
        }

    } // namespace Resource
} // namespace MQEngine
