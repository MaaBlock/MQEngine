/**
 * @file DouyinPackageExporter.h
 * @brief Step 5 双包产物导出（MainPackage + RemoteCDN）、首屏 <50ms game.js 生成与体积红线审计器（FEAT-03, FEAT-54）
 */
#ifndef MQENGINE_DOUYIN_PACKAGE_EXPORTER_H
#define MQENGINE_DOUYIN_PACKAGE_EXPORTER_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinPackagerConfig.h"
#include "DouyinShaderCooker.h"
#include "DouyinAssetCooker.h"
#include "DouyinScriptAndWasmBuilder.h"

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 五步烘焙管线阶段状态掩码，用于强制校验阶段前置顺序
     */
    struct DouyinPipelineStepState
    {
        bool step1ShadersDone{false};
        bool step2AssetsDone{false};
        bool step3ScriptsDone{false};
        bool step4WasmDone{false};
    };

    /**
     * @brief Step 5 双包导出完成统计报告
     */
    struct PackageExportReport
    {
        std::string mainPackageDir;
        std::string remoteCdnDir;
        uint64_t mainPackageRawBytes{0};
        uint64_t codeRawBytes{0};
        uint64_t remoteCdnRawBytes{0};
        uint32_t mainPackageFileCount{0};
        uint32_t remoteCdnFileCount{0};
    };

    /**
     * @brief 抖音小游戏双包产物生成器与宿主契约审计器
     */
    class DOUYIN_PACKAGER_CLI_API DouyinPackageExporter
    {
    public:
        static std::string generateGameJson(const DouyinPackagerConfig& config);
        static std::string generateProjectConfigJson(const DouyinPackagerConfig& config);
        static std::string generateTtAdapterJs(const DouyinPackagerConfig& config);
        static std::string generateBootSplashGameJs(const DouyinPackagerConfig& config);

        static bool isProhibitedMainPackageFilename(std::string_view fileName) noexcept;

        static Status validateStepPreconditions(const DouyinPipelineStepState& stepState);

        static StatusOr<PackageExportReport> exportPackages(
            ResourceLoader* loader,
            const std::string& outDir,
            const DouyinPackagerConfig& config,
            const DouyinPipelineStepState& stepState,
            const ShaderCookReport& shaderReport,
            const AssetCookReport& assetReport,
            const ScriptBundleReport& scriptReport,
            const WasmBuildReport& wasmReport);
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_PACKAGE_EXPORTER_H
