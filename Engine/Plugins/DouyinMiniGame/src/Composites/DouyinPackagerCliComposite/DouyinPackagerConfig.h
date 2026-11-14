/**
 * @file DouyinPackagerConfig.h
 * @brief 抖音小游戏工程级打包与宿主能力持久化配置模型（FEAT-02）
 */
#ifndef MQENGINE_DOUYIN_PACKAGER_CONFIG_H
#define MQENGINE_DOUYIN_PACKAGER_CONFIG_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include <MQEngine/resource/config/ConfigDocument.h>
#include <MQEngine/resource/core/ResourceLoader.h>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 抖音小游戏全链路打包与运行时生态配置聚合体（支持 Boost.PFR 零宏编译期反射）
     */
    struct DouyinPackagerConfig
    {
        std::string appId{"tt_mqengine_demo"};
        std::string projectName{"MQDouyinMiniGame"};
        std::string orientation{"landscape"};

        std::string renderGraphPreset{"MobileForward.pgraph"};
        std::string lowTierFallbackGraph{"MobileForwardUnlit.pgraph"};
        float devicePixelRatioCap{2.0f};
        int32_t shadowAtlasResolution{1024};
        bool enableBloom{true};
        std::string astcBlockQuality{"6x6"};

        float mainPackageMaxMB{10.0f};
        float codePackageMaxMB{4.0f};

        std::string cdnBaseUrl{"https://cdn.example.com/mqgame/v1/"};
        int32_t localCacheQuotaMB{180};
        std::string bootScenePath{"Scenes/MainBoot.mqscene"};

        bool sidebarRewardEnabled{true};
        std::string rewardedAdUnitId{""};
        std::vector<std::string> videoShareTopics{"抖音小游戏", "MQEngine"};

        std::string emsdkRoot{"G:/emsdk"};
        std::string douyinIdeExecutable{""};
    };

    /**
     * @brief 抖音小游戏配置持久化与边界契约校验管理器（读写 <ProjectDir>/Config/douyin_minigame.toml）
     */
    class DOUYIN_PACKAGER_CLI_API DouyinConfigManager
    {
    public:
        static constexpr const char* kDefaultConfigRelPath = "Config/douyin_minigame.toml";
        static constexpr float kMinDprCap = 1.0f;
        static constexpr float kMaxDprCap = 3.0f;
        static constexpr float kHardMaxMainPackageMB = 20.0f;

        static std::string resolveConfigPath(std::string_view projectDir);

        static Status validateDprCap(float dprCap);
        static Status validateSceneRelativePath(std::string_view scenePath);
        static Status validate(const DouyinPackagerConfig& config);

        static std::string serializeToToml(const DouyinPackagerConfig& config);
        static StatusOr<DouyinPackagerConfig> parseFromToml(const std::string& tomlContent);

        static Status saveToFile(
            ResourceLoader* loader,
            const std::string& filePath,
            const DouyinPackagerConfig& config);

        static StatusOr<DouyinPackagerConfig> loadFromFile(
            ResourceLoader* loader,
            const std::string& filePath);

        static Status saveToProject(
            ResourceLoader* loader,
            const std::string& projectDir,
            const DouyinPackagerConfig& config);

        static StatusOr<DouyinPackagerConfig> loadFromProject(
            ResourceLoader* loader,
            const std::string& projectDir);
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_PACKAGER_CONFIG_H
