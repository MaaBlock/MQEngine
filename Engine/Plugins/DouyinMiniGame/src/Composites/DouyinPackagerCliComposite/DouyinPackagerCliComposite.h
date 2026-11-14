/**
 * @file DouyinPackagerCliComposite.h
 * @brief 抖音小游戏五步全自动离线烘焙与双包导出无头流水线 Composite（FEAT-01, FEAT-03, FEAT-54）
 */
#ifndef MQENGINE_DOUYIN_PACKAGER_CLI_COMPOSITE_H
#define MQENGINE_DOUYIN_PACKAGER_CLI_COMPOSITE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinPackagerConfig.h"
#include "DouyinShaderCooker.h"
#include "DouyinAssetCooker.h"
#include "DouyinScriptAndWasmBuilder.h"
#include "DouyinPackageExporter.h"

#include <MQEngine/asset/interface/Composite.h>
#include <functional>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 离线五步烘焙请求参数
     */
    struct DouyinCookRequest
    {
        std::string projectDir;
        std::string outputDir;
        bool useOverrideConfig{false};
        DouyinPackagerConfig overrideConfig;
    };

    /**
     * @brief 离线五步烘焙完整结果汇总
     */
    struct DouyinCookResult
    {
        DouyinPackagerConfig resolvedConfig;
        DouyinPipelineStepState stepState;
        ShaderCookReport shaderReport;
        AssetCookReport assetReport;
        ScriptBundleReport scriptReport;
        WasmBuildReport wasmReport;
        PackageExportReport exportReport;
    };

    /**
     * @brief 无头 CLI / CI 离线五步烘焙与发布流水线 Composite
     */
    class DOUYIN_PACKAGER_CLI_API DouyinPackagerCliComposite : public Asset::Composite
    {
    public:
        using ProgressCallback = std::function<void(int stepIndex, float progressRatio, const std::string& message)>;

        DouyinPackagerCliComposite();
        ~DouyinPackagerCliComposite() override = default;

        Status onLoad(Runtime::Engine* engine) override;
        Status onInit(Runtime::Engine* engine) override;
        void onTick(float deltaTime) override;
        void onUnload() override;

        std::string getName() const override { return "DouyinPackagerCliComposite"; }
        std::string getPluginName() const override { return "DouyinMiniGame"; }
        std::vector<std::string> getDependencies() const override { return {}; }
        std::vector<std::string> getRequiredTags() const override { return {"Cli", "Cook"}; }

        static StatusOr<DouyinCookResult> runCookPipeline(
            ResourceLoader* loader,
            const DouyinCookRequest& request,
            ProgressCallback progressCb = nullptr);

        static Status launchDouyinDevTools(
            const DouyinPackagerConfig& config,
            const std::string& mainPackageDir);

        bool isCliCookRequested() const noexcept { return m_cliCookRequested; }
        const Status& getLastCliCookStatus() const noexcept { return m_lastCliCookStatus; }

    private:
        void parseCliArguments();
        void executeStartupCliCook();

    private:
        bool m_cliCookRequested{false};
        bool m_hasExecutedCliCook{false};
        std::string m_cliProjectDir;
        std::string m_cliOutDir;
        std::string m_cliAppIdOverride;
        std::string m_cliCdnUrlOverride;
        std::string m_cliOrientationOverride;
        Status m_lastCliCookStatus{OkStatus()};
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_PACKAGER_CLI_COMPOSITE_H
