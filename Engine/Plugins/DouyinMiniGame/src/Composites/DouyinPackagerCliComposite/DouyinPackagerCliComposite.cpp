/**
 * @file DouyinPackagerCliComposite.cpp
 * @brief 抖音小游戏五步全自动离线烘焙流水线编排与 --douyin-cook CLI 入口实现
 */
#include "DouyinPackagerCliComposite.h"
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/host/logic/LogicHost.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        void notifyProgress(
            const DouyinPackagerCliComposite::ProgressCallback& cb,
            int stepIndex,
            float ratio,
            const std::string& message)
        {
            if (cb)
            {
                cb(stepIndex, ratio, message);
            }
        }
    } // namespace

    DouyinPackagerCliComposite::DouyinPackagerCliComposite() = default;

    Status DouyinPackagerCliComposite::onLoad(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onLoad(engine));
        parseCliArguments();
        return OkStatus();
    }

    Status DouyinPackagerCliComposite::onInit(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onInit(engine));
        return OkStatus();
    }

    void DouyinPackagerCliComposite::onTick(float /*deltaTime*/)
    {
        if (m_cliCookRequested && !m_hasExecutedCliCook)
        {
            m_hasExecutedCliCook = true;
            executeStartupCliCook();
            if (auto* logicHost = Host::LogicHost::InstancePtr())
            {
                logicHost->requestExit();
            }
        }
    }

    void DouyinPackagerCliComposite::onUnload()
    {
        Asset::Composite::onUnload();
    }

    StatusOr<DouyinCookResult> DouyinPackagerCliComposite::runCookPipeline(
        ResourceLoader* loader,
        const DouyinCookRequest& request,
        ProgressCallback progressCb)
    {
        if (request.projectDir.empty())
        {
            return InvalidArgumentError("DouyinCookRequest.projectDir 工程根目录不能为空");
        }
        if (request.outputDir.empty())
        {
            return InvalidArgumentError("DouyinCookRequest.outputDir 输出根目录不能为空");
        }

        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        DouyinCookResult result;
        if (request.useOverrideConfig)
        {
            result.resolvedConfig = request.overrideConfig;
        }
        else
        {
            const std::string cfgPath = DouyinConfigManager::resolveConfigPath(request.projectDir);
            if (activeLoader->exists(cfgPath))
            {
                auto loadedCfgOr = DouyinConfigManager::loadFromFile(activeLoader, cfgPath);
                if (!loadedCfgOr.ok())
                {
                    return loadedCfgOr.status();
                }
                result.resolvedConfig = *loadedCfgOr;
            }
        }

        CHECK_STATUS(DouyinConfigManager::validate(result.resolvedConfig));

        // Step 1: 静态变体关系裁剪 (.pgraph ⋈ .mqtech) 与 SPIRV-Cross #version 300 es 离线转译
        notifyProgress(progressCb, 1, 0.15f, "Step 1/5: 裁剪桌面冗余变体并离线转译 #version 300 es 着色器包...");
        auto shaderReportOr = DouyinShaderCooker::cookProjectShaders(
            activeLoader,
            request.projectDir,
            result.resolvedConfig);
        if (!shaderReportOr.ok())
        {
            return shaderReportOr.status();
        }
        result.shaderReport = std::move(*shaderReportOr);
        result.stepState.step1ShadersDone = true;

        // Step 3 & Step 4 先行产出代码体积以便精确计算 Step 2 首包可用资产配额
        notifyProgress(progressCb, 3, 0.45f, "Step 3/5: Tree-shaking 打包工程 TypeScript 与蓝图逻辑脚本...");
        auto scriptReportOr = DouyinScriptAndWasmBuilder::bundleProjectScripts(
            activeLoader,
            request.projectDir,
            result.resolvedConfig);
        if (!scriptReportOr.ok())
        {
            return scriptReportOr.status();
        }
        result.scriptReport = std::move(*scriptReportOr);
        result.stepState.step3ScriptsDone = true;

        notifyProgress(progressCb, 4, 0.65f, "Step 4/5: 编译并裁剪 WebAssembly SIMD128 引擎核心 (engine.wasm)...");
        const std::string scratchDir = SAI::Path::join(request.outputDir, ".cook_scratch");
        auto wasmReportOr = DouyinScriptAndWasmBuilder::buildEngineWasm(
            activeLoader,
            result.resolvedConfig,
            scratchDir);
        if (SAI::FileSystem::exists(scratchDir))
        {
            (void)SAI::FileSystem::removeAll(scratchDir);
        }
        if (!wasmReportOr.ok())
        {
            return wasmReportOr.status();
        }
        result.wasmReport = std::move(*wasmReportOr);
        result.stepState.step4WasmDone = true;

        // Step 2: 资产烘焙与首包/CDN 双区切分
        notifyProgress(progressCb, 2, 0.80f, "Step 2/5: 烘焙 ASTC 纹理、0KB .mqfont 字集、.mqmesh 网格与三级寻址清单...");
        const uint64_t totalMainCapBytes =
            static_cast<uint64_t>(result.resolvedConfig.mainPackageMaxMB * 1024.0f * 1024.0f);
        const uint64_t reservedFixedBytes =
            result.wasmReport.wasmBinary.size() +
            result.shaderReport.shaderPackBinary.size() +
            result.scriptReport.bundledJsCode.size() +
            64ull * 1024ull;
        const uint64_t availableAssetBudget =
            (totalMainCapBytes > reservedFixedBytes) ? (totalMainCapBytes - reservedFixedBytes) : 0ull;

        auto assetReportOr = DouyinAssetCooker::cookProjectAssets(
            activeLoader,
            request.projectDir,
            result.resolvedConfig,
            availableAssetBudget);
        if (!assetReportOr.ok())
        {
            return assetReportOr.status();
        }
        result.assetReport = std::move(*assetReportOr);
        result.stepState.step2AssetsDone = true;

        // Step 5: 双包导出与红线门禁校验
        notifyProgress(progressCb, 5, 0.95f, "Step 5/5: 导出 MainPackage (<10MB) + RemoteCDN 双包产物并校验首屏 game.js...");
        auto exportReportOr = DouyinPackageExporter::exportPackages(
            activeLoader,
            request.outputDir,
            result.resolvedConfig,
            result.stepState,
            result.shaderReport,
            result.assetReport,
            result.scriptReport,
            result.wasmReport);
        if (!exportReportOr.ok())
        {
            return exportReportOr.status();
        }
        result.exportReport = std::move(*exportReportOr);

        notifyProgress(progressCb, 5, 1.0f, "五步全自动离线烘焙与双包发布完成！");
        return result;
    }

    Status DouyinPackagerCliComposite::launchDouyinDevTools(
        const DouyinPackagerConfig& config,
        const std::string& mainPackageDir)
    {
        if (mainPackageDir.empty() || !SAI::FileSystem::exists(mainPackageDir))
        {
            return NotFoundError("MainPackage 目录不存在，请先执行五步烘焙: " + mainPackageDir);
        }

        if (!config.douyinIdeExecutable.empty() && SAI::FileSystem::exists(config.douyinIdeExecutable))
        {
            const std::string cmd = "\"\"" + config.douyinIdeExecutable + "\" --project \"" + mainPackageDir + "\"\"";
            const int rc = std::system(cmd.c_str());
            if (rc != 0)
            {
                return InternalError("抖音开发者工具进程返回非零状态码: " + std::to_string(rc));
            }
            return OkStatus();
        }

        return NotFoundError("未配置或未找到抖音开发者工具可执行文件 (douyinIdeExecutable)");
    }

    void DouyinPackagerCliComposite::parseCliArguments()
    {
        auto* res = Resource::Resource::InstancePtr();
        if (!res || !res->getCommandLineParser())
        {
            return;
        }

        for (const auto& arg : res->getCommandLineParser()->getParsedArgs())
        {
            if (arg.matchesKey("douyin-cook"))
            {
                m_cliCookRequested = arg.asBool(true);
            }
            else if (arg.matchesKey("project") || arg.matchesKey("project-dir"))
            {
                m_cliProjectDir = arg.value;
            }
            else if (arg.matchesKey("out-dir") || arg.matchesKey("output-dir"))
            {
                m_cliOutDir = arg.value;
            }
            else if (arg.matchesKey("app-id"))
            {
                m_cliAppIdOverride = arg.value;
            }
            else if (arg.matchesKey("cdn-url"))
            {
                m_cliCdnUrlOverride = arg.value;
            }
            else if (arg.matchesKey("orientation"))
            {
                m_cliOrientationOverride = arg.value;
            }
        }
    }

    void DouyinPackagerCliComposite::executeStartupCliCook()
    {
        auto* res = Resource::Resource::InstancePtr();
        ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;

        DouyinCookRequest req;
        req.projectDir = m_cliProjectDir;
        if (req.projectDir.empty() && res)
        {
            req.projectDir = res->getProjectDir();
        }
        if (req.projectDir.empty() && res)
        {
            req.projectDir = res->getEngineDir();
        }
        req.outputDir = m_cliOutDir.empty()
            ? SAI::Path::join(req.projectDir, "Dist/DouyinMiniGame")
            : m_cliOutDir;

        const std::string cfgPath = DouyinConfigManager::resolveConfigPath(req.projectDir);
        if (loader && loader->exists(cfgPath))
        {
            auto cfgOr = DouyinConfigManager::loadFromFile(loader, cfgPath);
            if (cfgOr.ok())
            {
                req.overrideConfig = *cfgOr;
                req.useOverrideConfig = true;
            }
        }
        if (!m_cliAppIdOverride.empty())
        {
            req.overrideConfig.appId = m_cliAppIdOverride;
            req.useOverrideConfig = true;
        }
        if (!m_cliCdnUrlOverride.empty())
        {
            req.overrideConfig.cdnBaseUrl = m_cliCdnUrlOverride;
            req.useOverrideConfig = true;
        }
        if (!m_cliOrientationOverride.empty())
        {
            req.overrideConfig.orientation = m_cliOrientationOverride;
            req.useOverrideConfig = true;
        }

        auto cookResOr = runCookPipeline(
            loader,
            req,
            [](int step, float ratio, const std::string& msg) {
                std::printf("[DouyinCook][Step %d][%3d%%] %s\n",
                            step,
                            static_cast<int>(ratio * 100.0f),
                            msg.c_str());
                std::fflush(stdout);
            });

        if (!cookResOr.ok())
        {
            m_lastCliCookStatus = cookResOr.status();
            std::fprintf(stderr,
                         "[DouyinCook][ERROR] %s\n",
                         std::string(m_lastCliCookStatus.message()).c_str());
            std::fflush(stderr);
            return;
        }

        m_lastCliCookStatus = OkStatus();
        const auto& r = *cookResOr;
        std::printf(
            "[DouyinCook][SUCCESS] MainPackage=%s (%llu bytes, %u files) | RemoteCDN=%s (%llu bytes, %u files) | Shaders=%u compiled (%u pruned) | Wasm=%zu bytes (%s)\n",
            r.exportReport.mainPackageDir.c_str(),
            static_cast<unsigned long long>(r.exportReport.mainPackageRawBytes),
            r.exportReport.mainPackageFileCount,
            r.exportReport.remoteCdnDir.c_str(),
            static_cast<unsigned long long>(r.exportReport.remoteCdnRawBytes),
            r.exportReport.remoteCdnFileCount,
            r.shaderReport.compiledVariantCount,
            r.shaderReport.prunedVariantCount,
            r.wasmReport.wasmBinary.size(),
            r.wasmReport.compiledViaEmsdkToolchain ? "emsdk-clang" : "builtin-simd");
        std::fflush(stdout);
    }
} // namespace MQEngine::DouyinMiniGame

MQ_EXPORT_COMPOSITE(MQEngine::DouyinMiniGame::DouyinPackagerCliComposite)
