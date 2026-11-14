/**
 * @file DouyinPackagerConfig.cpp
 * @brief 抖音小游戏工程配置 TOML 零宏反射持久化与合法性校验实现
 */
#include "DouyinPackagerConfig.h"
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include <cmath>

namespace MQEngine::DouyinMiniGame
{
    namespace Detail
    {
        // Note: 对应 PRD 01 §2.2 分节 TOML 结构的聚合体，置于具名子命名空间以满足 Boost.PFR 外部链接约束
        struct TomlDouyinSection
        {
            std::string app_id;
            std::string project_name;
            std::string orientation;
        };

        struct TomlRenderSection
        {
            std::string render_graph_preset;
            std::string low_tier_fallback_graph;
            double device_pixel_ratio_cap{2.0};
            int64_t shadow_atlas_resolution{1024};
            bool enable_bloom{true};
            std::string astc_block_quality;
        };

        struct TomlPackagingSection
        {
            double main_package_max_mb{10.0};
            double code_package_max_mb{4.0};
        };

        struct TomlStreamingSection
        {
            std::string cdn_base_url;
            int64_t local_cache_quota_mb{180};
            std::string boot_scene;
        };

        struct TomlPlatformSection
        {
            bool sidebar_reward_enabled{true};
            std::string rewarded_ad_unit_id;
            std::vector<std::string> video_share_topics;
        };

        struct TomlToolchainSection
        {
            std::string emsdk_root;
            std::string douyin_ide_executable;
        };
    } // namespace Detail

    namespace
    {
        using Detail::TomlDouyinSection;
        using Detail::TomlRenderSection;
        using Detail::TomlPackagingSection;
        using Detail::TomlStreamingSection;
        using Detail::TomlPlatformSection;
        using Detail::TomlToolchainSection;

        bool hasPathTraversalSegment(std::string_view path)
        {
            std::string normalized(path);
            for (char& ch : normalized)
            {
                if (ch == '\\')
                {
                    ch = '/';
                }
            }

            size_t start = 0;
            while (start <= normalized.size())
            {
                const size_t slashPos = normalized.find('/', start);
                const std::string_view seg = (slashPos == std::string::npos)
                    ? std::string_view(normalized).substr(start)
                    : std::string_view(normalized).substr(start, slashPos - start);
                if (seg == "..")
                {
                    return true;
                }
                if (slashPos == std::string::npos)
                {
                    break;
                }
                start = slashPos + 1;
            }
            return false;
        }

        bool isAbsoluteOrUncPath(std::string_view path)
        {
            if (path.empty())
            {
                return false;
            }
            if (path.front() == '/' || path.front() == '\\')
            {
                return true;
            }
            if (path.size() >= 2 && path[1] == ':')
            {
                return true;
            }
            return false;
        }

        void applySectionsFromNode(const ConfigNode& root, DouyinPackagerConfig& outConfig)
        {
            // 先支持顶层扁平 Boost.PFR 字段读取
            root.readStruct(outConfig);

            if (const ConfigNode* douyinSec = root.findSection("douyin"))
            {
                TomlDouyinSection sec{outConfig.appId, outConfig.projectName, outConfig.orientation};
                douyinSec->readStruct(sec);
                outConfig.appId = sec.app_id;
                outConfig.projectName = sec.project_name;
                outConfig.orientation = sec.orientation;
            }

            if (const ConfigNode* renderSec = root.findSection("render"))
            {
                TomlRenderSection sec{
                    outConfig.renderGraphPreset,
                    outConfig.lowTierFallbackGraph,
                    static_cast<double>(outConfig.devicePixelRatioCap),
                    static_cast<int64_t>(outConfig.shadowAtlasResolution),
                    outConfig.enableBloom,
                    outConfig.astcBlockQuality
                };
                renderSec->readStruct(sec);
                outConfig.renderGraphPreset = sec.render_graph_preset;
                outConfig.lowTierFallbackGraph = sec.low_tier_fallback_graph;
                outConfig.devicePixelRatioCap = static_cast<float>(sec.device_pixel_ratio_cap);
                outConfig.shadowAtlasResolution = static_cast<int32_t>(sec.shadow_atlas_resolution);
                outConfig.enableBloom = sec.enable_bloom;
                outConfig.astcBlockQuality = sec.astc_block_quality;
            }

            if (const ConfigNode* pkgSec = root.findSection("packaging"))
            {
                TomlPackagingSection sec{
                    static_cast<double>(outConfig.mainPackageMaxMB),
                    static_cast<double>(outConfig.codePackageMaxMB)
                };
                pkgSec->readStruct(sec);
                outConfig.mainPackageMaxMB = static_cast<float>(sec.main_package_max_mb);
                outConfig.codePackageMaxMB = static_cast<float>(sec.code_package_max_mb);
            }

            if (const ConfigNode* streamSec = root.findSection("streaming"))
            {
                TomlStreamingSection sec{
                    outConfig.cdnBaseUrl,
                    static_cast<int64_t>(outConfig.localCacheQuotaMB),
                    outConfig.bootScenePath
                };
                streamSec->readStruct(sec);
                outConfig.cdnBaseUrl = sec.cdn_base_url;
                outConfig.localCacheQuotaMB = static_cast<int32_t>(sec.local_cache_quota_mb);
                outConfig.bootScenePath = sec.boot_scene;
            }

            if (const ConfigNode* platSec = root.findSection("platform"))
            {
                TomlPlatformSection sec{
                    outConfig.sidebarRewardEnabled,
                    outConfig.rewardedAdUnitId,
                    outConfig.videoShareTopics
                };
                platSec->readStruct(sec);
                outConfig.sidebarRewardEnabled = sec.sidebar_reward_enabled;
                outConfig.rewardedAdUnitId = sec.rewarded_ad_unit_id;
                outConfig.videoShareTopics = sec.video_share_topics;
            }

            if (const ConfigNode* toolSec = root.findSection("toolchain"))
            {
                TomlToolchainSection sec{outConfig.emsdkRoot, outConfig.douyinIdeExecutable};
                toolSec->readStruct(sec);
                outConfig.emsdkRoot = sec.emsdk_root;
                outConfig.douyinIdeExecutable = sec.douyin_ide_executable;
            }
        }
    } // namespace

    std::string DouyinConfigManager::resolveConfigPath(std::string_view projectDir)
    {
        if (projectDir.empty())
        {
            return std::string(kDefaultConfigRelPath);
        }
        return SAI::Path::join(projectDir, kDefaultConfigRelPath);
    }

    Status DouyinConfigManager::validateDprCap(float dprCap)
    {
        if (!std::isfinite(dprCap) || dprCap < kMinDprCap || dprCap > kMaxDprCap)
        {
            return InvalidArgumentError("devicePixelRatioCap 必须位于闭区间 [1.0, 3.0] 内");
        }
        return OkStatus();
    }

    Status DouyinConfigManager::validateSceneRelativePath(std::string_view scenePath)
    {
        if (scenePath.empty())
        {
            return InvalidArgumentError("bootScenePath 启动场景路径不能为空");
        }
        if (isAbsoluteOrUncPath(scenePath) || hasPathTraversalSegment(scenePath))
        {
            return PermissionDeniedError("bootScenePath 严禁包含绝对盘符或 '..' 路径逃逸片段");
        }
        return OkStatus();
    }

    Status DouyinConfigManager::validate(const DouyinPackagerConfig& config)
    {
        if (config.appId.empty())
        {
            return InvalidArgumentError("抖音小游戏 appId 不能为空");
        }
        if (config.projectName.empty())
        {
            return InvalidArgumentError("抖音小游戏 projectName 不能为空");
        }
        if (config.orientation != "portrait" && config.orientation != "landscape")
        {
            return InvalidArgumentError("orientation 仅支持 'portrait' 或 'landscape'");
        }

        CHECK_STATUS(validateDprCap(config.devicePixelRatioCap));
        CHECK_STATUS(validateSceneRelativePath(config.bootScenePath));

        if (config.renderGraphPreset.empty())
        {
            return InvalidArgumentError("renderGraphPreset 目标帧图预设不能为空");
        }
        if (hasPathTraversalSegment(config.renderGraphPreset))
        {
            return PermissionDeniedError("renderGraphPreset 不允许包含 '..' 路径穿越");
        }
        if (config.astcBlockQuality != "4x4" && config.astcBlockQuality != "6x6")
        {
            return InvalidArgumentError("astcBlockQuality 仅支持 '4x4' 或 '6x6'");
        }
        if (!std::isfinite(config.mainPackageMaxMB) ||
            config.mainPackageMaxMB <= 0.0f ||
            config.mainPackageMaxMB > kHardMaxMainPackageMB)
        {
            return InvalidArgumentError("mainPackageMaxMB 必须处于 (0, 20.0] MB 区间内");
        }
        if (!std::isfinite(config.codePackageMaxMB) ||
            config.codePackageMaxMB <= 0.0f ||
            config.codePackageMaxMB > config.mainPackageMaxMB)
        {
            return InvalidArgumentError("codePackageMaxMB 必须大于 0 且不超过 mainPackageMaxMB");
        }
        if (config.localCacheQuotaMB < 16 || config.localCacheQuotaMB > 200)
        {
            return InvalidArgumentError("localCacheQuotaMB 必须位于 [16, 200] MB 宿主沙箱区间内");
        }
        if (config.shadowAtlasResolution != 512 &&
            config.shadowAtlasResolution != 1024 &&
            config.shadowAtlasResolution != 2048)
        {
            return InvalidArgumentError("shadowAtlasResolution 仅支持 512 / 1024 / 2048");
        }
        return OkStatus();
    }

    std::string DouyinConfigManager::serializeToToml(const DouyinPackagerConfig& config)
    {
        ConfigDocument doc;
        ConfigNode& root = doc.root();

        root.section("douyin").writeStruct(TomlDouyinSection{
            config.appId,
            config.projectName,
            config.orientation
        });
        root.section("render").writeStruct(TomlRenderSection{
            config.renderGraphPreset,
            config.lowTierFallbackGraph,
            static_cast<double>(config.devicePixelRatioCap),
            static_cast<int64_t>(config.shadowAtlasResolution),
            config.enableBloom,
            config.astcBlockQuality
        });
        root.section("packaging").writeStruct(TomlPackagingSection{
            static_cast<double>(config.mainPackageMaxMB),
            static_cast<double>(config.codePackageMaxMB)
        });
        root.section("streaming").writeStruct(TomlStreamingSection{
            config.cdnBaseUrl,
            static_cast<int64_t>(config.localCacheQuotaMB),
            config.bootScenePath
        });
        root.section("platform").writeStruct(TomlPlatformSection{
            config.sidebarRewardEnabled,
            config.rewardedAdUnitId,
            config.videoShareTopics
        });
        root.section("toolchain").writeStruct(TomlToolchainSection{
            config.emsdkRoot,
            config.douyinIdeExecutable
        });

        return doc.serialize(ConfigFormat::Toml);
    }

    StatusOr<DouyinPackagerConfig> DouyinConfigManager::parseFromToml(const std::string& tomlContent)
    {
        auto docOr = ConfigDocument::parse(tomlContent, ConfigFormat::Toml);
        if (!docOr.ok())
        {
            return docOr.status();
        }

        DouyinPackagerConfig cfg{};
        applySectionsFromNode(docOr->root(), cfg);
        CHECK_STATUS(validate(cfg));
        return cfg;
    }

    Status DouyinConfigManager::saveToFile(
        ResourceLoader* loader,
        const std::string& filePath,
        const DouyinPackagerConfig& config)
    {
        CHECK_STATUS(validate(config));

        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        const std::string parentDir = SAI::Path::parentPath(filePath);
        if (!parentDir.empty())
        {
            CHECK_STATUS(activeLoader->ensureDir(parentDir));
        }

        const std::string tomlText = serializeToToml(config);
        return activeLoader->saveFile(filePath, tomlText);
    }

    StatusOr<DouyinPackagerConfig> DouyinConfigManager::loadFromFile(
        ResourceLoader* loader,
        const std::string& filePath)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        auto docOr = ConfigDocument::loadFromFile(activeLoader, filePath, ConfigFormat::Toml);
        if (!docOr.ok())
        {
            return docOr.status();
        }

        DouyinPackagerConfig cfg{};
        applySectionsFromNode(docOr->root(), cfg);
        CHECK_STATUS(validate(cfg));
        return cfg;
    }

    Status DouyinConfigManager::saveToProject(
        ResourceLoader* loader,
        const std::string& projectDir,
        const DouyinPackagerConfig& config)
    {
        if (projectDir.empty())
        {
            return InvalidArgumentError("projectDir 工程根目录不能为空");
        }
        return saveToFile(loader, resolveConfigPath(projectDir), config);
    }

    StatusOr<DouyinPackagerConfig> DouyinConfigManager::loadFromProject(
        ResourceLoader* loader,
        const std::string& projectDir)
    {
        if (projectDir.empty())
        {
            return InvalidArgumentError("projectDir 工程根目录不能为空");
        }
        return loadFromFile(loader, resolveConfigPath(projectDir));
    }
} // namespace MQEngine::DouyinMiniGame
