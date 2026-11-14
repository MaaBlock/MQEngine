/**
 * @file Test_Tier3_Douyin_Packaging_Runtime.cpp
 * @brief Tier 3 正交组合测试：插件标签隔离 × 五步全自动烘焙双包导出 × 抖音 tt.* 宿主生态服务 × FCT_WebJS 句柄表与零拷贝视图 × 软键盘桥接 × 纯数据渲染图契约
 *        覆盖特性：FEAT_01, FEAT_02, FEAT_03, FEAT_04, FEAT_05, FEAT_06, FEAT_07,
 *                 FEAT_44, FEAT_53, FEAT_67, FEAT_68, FEAT_69, FEAT_71, FEAT_72
 */
#include "Common/E2ECommon.h"

#ifndef E2E_ASSERT_NEAR
#define E2E_ASSERT_NEAR(val1, val2, absError) \
    E2E_ASSERT(std::abs((val1) - (val2)) <= (absError), \
               ::absl::StrCat("期望近似相等: |", #val1, " - ", #val2, "| <= ", #absError))
#endif

#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/ShaderCrossCompiler.h>
#include <FCT_SAI/VirtualKeyboard.h>

#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/asset/interface/Plugin.h>
#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include "Composites/DouyinPackagerCliComposite/DouyinAssetCooker.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackageExporter.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerCliComposite.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerConfig.h"
#include "Composites/DouyinPackagerCliComposite/DouyinScriptAndWasmBuilder.h"
#include "Composites/DouyinPackagerCliComposite/DouyinShaderCooker.h"
#include "Composites/DouyinPlatformRuntimeComposite/CapsuleAndOpenDataService.h"
#include "Composites/DouyinPlatformRuntimeComposite/DouyinPlatformRuntimeComposite.h"
#include "Composites/DouyinPlatformRuntimeComposite/DouyinWebJsAdapter.h"
#include "Composites/DouyinPlatformRuntimeComposite/GameRecorderService.h"
#include "Composites/DouyinPlatformRuntimeComposite/RewardedVideoAdService.h"
#include "Composites/DouyinPlatformRuntimeComposite/SidebarRetentionService.h"
#include "Composites/RmlUiComposite/RmlUiComposite.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// 通过 C FFI 符号直连真实 FCT_WebJS 运行时句柄表，规避与 FCT_Node 同名 C++ 类符号冲突
extern "C"
{
    uint32_t fct_webjs_alloc_object();
    void fct_webjs_retain_handle(uint32_t handle);
    void fct_webjs_release_handle(uint32_t handle);
    void fct_webjs_set_prop_string(uint32_t handle, const char* key, const char* val);
    void fct_webjs_set_prop_number(uint32_t handle, const char* key, double val);
    void fct_webjs_set_prop_handle(uint32_t handle, const char* key, uint32_t valHandle);
    uint32_t fct_webjs_get_prop_handle(uint32_t handle, const char* key);
    uint32_t fct_webjs_create_typed_array(const void* data, size_t byteLength, uint8_t type);
    void fct_webjs_promise_resolve(uint32_t promiseHandle, uint32_t valHandle);
    void fct_webjs_promise_reject(uint32_t promiseHandle, uint32_t valHandle);
}

namespace MQEngine::Testing
{
    namespace
    {
        constexpr uint32_t kWebJsHandleGlobal = 1u;
        constexpr uint32_t kWebJsHandleUndefined = 2u;
        constexpr uint32_t kWebJsHandleFirstDynamic = 5u;
        constexpr uint8_t kWebJsTypedArrayFloat32 = 6u;

        using Asset::Composite;
        using Asset::PluginMetadata;
        using Asset::PluginPackage;
        using Domain::Renderer::DrawBucket;
        namespace InputTags = Domain::Renderer::InputTags;
        namespace PassTags = Domain::Renderer::PassTags;
        using Domain::Renderer::PassTargetBinding;
        using Domain::Renderer::PassTextureInput;
        using Domain::Renderer::PipelineBindingDesc;
        using Domain::Renderer::PureDataPassNode;
        using Domain::Renderer::RenderGraphAsset;
        using Domain::Renderer::RenderGraphAssetDesc;
        using Domain::Renderer::RenderGraphPassNodeDesc;
        using Domain::Renderer::RenderGraphMatcher;

        using DouyinMiniGame::CapsuleAvoidanceBridge;
        using DouyinMiniGame::DouyinCapsuleRect;
        using DouyinMiniGame::DouyinConfigManager;
        using DouyinMiniGame::DouyinCookRequest;
        using DouyinMiniGame::DouyinPackageExporter;
        using DouyinMiniGame::DouyinPackagerCliComposite;
        using DouyinMiniGame::DouyinPackagerConfig;
        using DouyinMiniGame::DouyinPipelineStepState;
        using DouyinMiniGame::DouyinPlatformRuntimeComposite;
        using DouyinMiniGame::DouyinShaderCooker;
        using DouyinMiniGame::DouyinShareVideoRequest;
        using DouyinMiniGame::DouyinShowEvent;
        using DouyinMiniGame::DouyinSystemSafeArea;
        using DouyinMiniGame::GameRecorderService;
        using DouyinMiniGame::IDouyinHostApi;
        using DouyinMiniGame::OpenDataDomainBridge;
        using DouyinMiniGame::RecorderState;
        using DouyinMiniGame::RewardedAdCloseResult;
        using DouyinMiniGame::RewardedVideoAdService;
        using DouyinMiniGame::SidebarRetentionService;

        using GameHtmlUI::OpenDataLeaderboardController;
        using GameHtmlUI::RmlUiComposite;
        using GameHtmlUI::RmlUiKeyboardBridge;
        using GameHtmlUI::VirtualTextureRouter;

        void T3Douyin_InitHeadlessWgl2(FCT::WGL2_Context& ctx)
        {
            ctx.createPlatform(FCT::ContextCreateFlags(0));
        }

        // RAII 临时构建工作区目录守卫：使用 DiskResourceLoader 读写并在退出时自动清理
        struct T3Douyin_TempWorkspaceGuard
        {
            std::filesystem::path rootPath;
            std::string projectDir;
            std::string outputDir;

            explicit T3Douyin_TempWorkspaceGuard(std::string_view tag)
            {
                const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                rootPath = std::filesystem::temp_directory_path() /
                           absl::StrCat("mq_t3_douyin_", tag, "_", stamp);
                projectDir = (rootPath / "Project").generic_string();
                outputDir = (rootPath / "BuildOut").generic_string();
                std::error_code ec;
                std::filesystem::create_directories(projectDir, ec);
                std::filesystem::create_directories(outputDir, ec);
            }

            ~T3Douyin_TempWorkspaceGuard()
            {
                std::error_code ec;
                std::filesystem::remove_all(rootPath, ec);
            }
        };

        // 基于 Composite::getRequiredTags 子集包含规则执行宿主环境激活筛选
        std::vector<std::string> T3Douyin_FilterActiveCompositeNames(
            std::span<const Composite* const> candidates,
            std::span<const std::string> hostActiveTags)
        {
            std::vector<std::string> matchedNames;
            for (const Composite* comp : candidates)
            {
                if (!comp)
                {
                    continue;
                }
                const std::vector<std::string> reqTags = comp->getRequiredTags();
                bool allSatisfied = !reqTags.empty();
                for (const std::string& req : reqTags)
                {
                    if (std::find(hostActiveTags.begin(), hostActiveTags.end(), req) == hostActiveTags.end())
                    {
                        allSatisfied = false;
                        break;
                    }
                }
                if (allSatisfied)
                {
                    matchedNames.push_back(comp->getName());
                }
            }
            return matchedNames;
        }

        // 在临时工程目录下布置用于五步离线烘焙的最小真实资产集
        Status T3Douyin_StageMinimalProjectAssets(DiskResourceLoader& loader, const std::string& projectDir)
        {
            (void)loader.makeDir(projectDir + "/Config");
            (void)loader.makeDir(projectDir + "/Scripts");
            (void)loader.makeDir(projectDir + "/Assets");

            DouyinPackagerConfig cfg;
            cfg.appId = "tt_tier3_cook_verify";
            cfg.projectName = "Tier3MiniGameDemo";
            cfg.orientation = "landscape";
            cfg.renderGraphPreset = "MiniGameForwardPlus.pgraph";
            cfg.devicePixelRatioCap = 2.0f;
            cfg.mainPackageMaxMB = 10.0f;
            cfg.codePackageMaxMB = 4.0f;
            cfg.cdnBaseUrl = "https://cdn.mqengine.test/tier3/v1/";
            CHECK_STATUS(DouyinConfigManager::saveToProject(&loader, projectDir, cfg));

            const std::string bootTs =
                "export interface BootState { ready: boolean; score: number; }\n"
                "export function startGame(): BootState { return { ready: true, score: 100 }; }\n";
            CHECK_STATUS(loader.saveFile(projectDir + "/Scripts/BootMain.ts", bootTs));

            const std::string triangleObj =
                "v 0.0 0.0 0.0\n"
                "v 1.0 0.0 0.0\n"
                "v 0.0 1.0 0.0\n"
                "vn 0.0 0.0 1.0\n"
                "vt 0.0 0.0\n"
                "vt 1.0 0.0\n"
                "vt 0.0 1.0\n"
                "f 1/1/1 2/2/1 3/3/1\n";
            CHECK_STATUS(loader.saveFile(projectDir + "/Assets/HeroTri.obj", triangleObj));
            return OkStatus();
        }

        // 具名五步烘焙进度记录仿函数
        struct T3Douyin_CookProgressRecorder
        {
            std::vector<int>* visitedSteps{nullptr};
            float* lastRatio{nullptr};

            void operator()(int stepIndex, float progressRatio, const std::string&) const
            {
                if (visitedSteps)
                {
                    if (std::find(visitedSteps->begin(), visitedSteps->end(), stepIndex) == visitedSteps->end())
                    {
                        visitedSteps->push_back(stepIndex);
                    }
                }
                if (lastRatio)
                {
                    *lastRatio = progressRatio;
                }
            }
        };

        // 具名侧边栏可用性探测回调仿函数
        struct T3Douyin_BoolStatusOrRecorder
        {
            bool* invoked{nullptr};
            bool* valueOut{nullptr};

            void operator()(StatusOr<bool> res) const
            {
                if (invoked)
                {
                    *invoked = true;
                }
                if (valueOut && res.ok())
                {
                    *valueOut = *res;
                }
            }
        };

        // 具名通用 Status 回调记录仿函数
        struct T3Douyin_StatusCallbackRecorder
        {
            uint32_t* callCount{nullptr};
            Status* lastStatus{nullptr};

            void operator()(Status st) const
            {
                if (callCount)
                {
                    ++(*callCount);
                }
                if (lastStatus)
                {
                    *lastStatus = std::move(st);
                }
            }
        };

        // 具名激励视频音频静音观察仿函数
        struct T3Douyin_AudioMuteRecorder
        {
            uint32_t* muteTransitions{nullptr};
            bool* currentMuted{nullptr};

            void operator()(bool shouldMute) const
            {
                if (muteTransitions)
                {
                    ++(*muteTransitions);
                }
                if (currentMuted)
                {
                    *currentMuted = shouldMute;
                }
            }
        };

        // 具名激励视频结算结果记录仿函数
        struct T3Douyin_RewardAdResultRecorder
        {
            uint32_t* callbackCount{nullptr};
            RewardedAdCloseResult* lastResult{nullptr};

            void operator()(const RewardedAdCloseResult& res) const
            {
                if (callbackCount)
                {
                    ++(*callbackCount);
                }
                if (lastResult)
                {
                    *lastResult = res;
                }
            }
        };
    } // namespace

    // =========================================================================
    // 1. FEAT_01: plugin.mq 元数据解析 × Desktop / CLI / DouyinMiniGame 三态标签正交隔离
    // =========================================================================
    E2E_TEST(Tier3, FEAT_01, Pair_DouyinRt_PluginMetadataTomlAndHostTagIsolationAcrossComposites)
    {
        DiskResourceLoader diskLoader;
        std::string pluginToml;
        const char* candidatePaths[] = {
#ifdef MQ_E2E_REPO_ROOT
            MQ_E2E_REPO_ROOT "/Engine/Plugins/DouyinMiniGame/plugin.mq",
#endif
            "Engine/Plugins/DouyinMiniGame/plugin.mq",
            "../Engine/Plugins/DouyinMiniGame/plugin.mq",
            "../../Engine/Plugins/DouyinMiniGame/plugin.mq",
            "../../../Engine/Plugins/DouyinMiniGame/plugin.mq"
        };
        for (const char* p : candidatePaths)
        {
            if (diskLoader.exists(p))
            {
                auto loadedOr = diskLoader.loadFile(p);
                if (loadedOr.ok())
                {
                    pluginToml = *loadedOr;
                    break;
                }
            }
        }
        E2E_ASSERT_FALSE(pluginToml.empty());

        auto metaOr = PluginPackage::parseMetadataFromToml(pluginToml);
        E2E_ASSERT_STATUS_OK(metaOr);
        const PluginMetadata& meta = *metaOr;
        E2E_ASSERT_EQ(meta.name, "DouyinMiniGame");
        E2E_ASSERT_EQ(meta.composites.size(), 3u);

        DouyinPackagerCliComposite cliComp;
        DouyinPlatformRuntimeComposite douyinRtComp;
        RmlUiComposite htmlUiComp;

        const std::vector<const Composite*> allComposites = {&cliComp, &douyinRtComp, &htmlUiComp};

        // 桌面游戏运行态仅激活 RmlUiComposite，对抖音小游戏插件保持严格零侵入
        const std::vector<std::string> desktopTags = {"Runtime", "Game"};
        const auto desktopActive = T3Douyin_FilterActiveCompositeNames(allComposites, desktopTags);
        E2E_ASSERT_EQ(desktopActive.size(), 1u);
        E2E_ASSERT_EQ(desktopActive[0], "RmlUiComposite");

        // 无头 CLI 烘焙模式仅激活 DouyinPackagerCliComposite
        const std::vector<std::string> cliCookTags = {"Cli", "Cook"};
        const auto cliActive = T3Douyin_FilterActiveCompositeNames(allComposites, cliCookTags);
        E2E_ASSERT_EQ(cliActive.size(), 1u);
        E2E_ASSERT_EQ(cliActive[0], "DouyinPackagerCliComposite");

        // 抖音小游戏宿主运行态同时激活 RmlUiComposite 与 DouyinPlatformRuntimeComposite，但剥离打包器
        const std::vector<std::string> miniGameTags = {"Runtime", "Game", "MiniGame", "Douyin"};
        const auto miniGameActive = T3Douyin_FilterActiveCompositeNames(allComposites, miniGameTags);
        E2E_ASSERT_EQ(miniGameActive.size(), 2u);
        E2E_ASSERT_TRUE(std::find(miniGameActive.begin(), miniGameActive.end(), "DouyinPlatformRuntimeComposite") != miniGameActive.end());
        E2E_ASSERT_TRUE(std::find(miniGameActive.begin(), miniGameActive.end(), "RmlUiComposite") != miniGameActive.end());
        return true;
    }

    // =========================================================================
    // 2. FEAT_02: DouyinPackagerConfig Boost.PFR TOML 持久化往返 × DPR / 20MB 硬红线边界拦截
    // =========================================================================
    E2E_TEST(Tier3, FEAT_02, Pair_DouyinRt_PackagerConfigPfrTomlPersistenceAndBoundaryValidation)
    {
        E2E_ASSERT_FALSE(DouyinConfigManager::validateDprCap(0.75f).ok());
        E2E_ASSERT_FALSE(DouyinConfigManager::validateDprCap(3.5f).ok());
        E2E_ASSERT_STATUS_OK(DouyinConfigManager::validateDprCap(2.0f));

        DouyinPackagerConfig overBudgetCfg;
        overBudgetCfg.mainPackageMaxMB = 24.0f;
        E2E_ASSERT_FALSE(DouyinConfigManager::validate(overBudgetCfg).ok());

        DouyinPackagerConfig badSceneCfg;
        badSceneCfg.bootScenePath = "../EscapeOutsideProject.mqscene";
        E2E_ASSERT_FALSE(DouyinConfigManager::validate(badSceneCfg).ok());

        T3Douyin_TempWorkspaceGuard ws("feat02_cfg");
        DiskResourceLoader diskLoader;

        DouyinPackagerConfig srcCfg;
        srcCfg.appId = "tt_pfr_roundtrip_02";
        srcCfg.projectName = "MQOrthogonalPairwise";
        srcCfg.orientation = "portrait";
        srcCfg.renderGraphPreset = "MiniGameForwardPlus.pgraph";
        srcCfg.lowTierFallbackGraph = "MiniGameForwardLite.pgraph";
        srcCfg.devicePixelRatioCap = 2.0f;
        srcCfg.shadowAtlasResolution = 1024;
        srcCfg.astcBlockQuality = "6x6";
        srcCfg.mainPackageMaxMB = 10.0f;
        srcCfg.codePackageMaxMB = 4.0f;
        srcCfg.localCacheQuotaMB = 180;
        srcCfg.rewardedAdUnitId = "ad_slot_8801";
        srcCfg.videoShareTopics = {"抖音小游戏", "MQEngine实测"};

        E2E_ASSERT_STATUS_OK(DouyinConfigManager::saveToProject(&diskLoader, ws.projectDir, srcCfg));
        const std::string savedPath = DouyinConfigManager::resolveConfigPath(ws.projectDir);
        E2E_ASSERT_TRUE(diskLoader.exists(savedPath));

        auto loadedOr = DouyinConfigManager::loadFromProject(&diskLoader, ws.projectDir);
        E2E_ASSERT_STATUS_OK(loadedOr);
        const DouyinPackagerConfig& dstCfg = *loadedOr;

        E2E_ASSERT_EQ(dstCfg.appId, srcCfg.appId);
        E2E_ASSERT_EQ(dstCfg.orientation, "portrait");
        E2E_ASSERT_EQ(dstCfg.renderGraphPreset, "MiniGameForwardPlus.pgraph");
        E2E_ASSERT_NEAR(dstCfg.devicePixelRatioCap, 2.0f, 1e-4f);
        E2E_ASSERT_NEAR(dstCfg.mainPackageMaxMB, 10.0f, 1e-4f);
        E2E_ASSERT_NEAR(dstCfg.codePackageMaxMB, 4.0f, 1e-4f);
        E2E_ASSERT_EQ(dstCfg.localCacheQuotaMB, 180);
        E2E_ASSERT_EQ(dstCfg.rewardedAdUnitId, "ad_slot_8801");
        E2E_ASSERT_EQ(dstCfg.videoShareTopics.size(), 2u);
        return true;
    }

    // =========================================================================
    // 3. FEAT_03: 五步离线烘焙流水线全链路落盘 × 双包体积红线与 AssetManifest 二进制加载闭环
    // =========================================================================
    E2E_TEST(Tier3, FEAT_03, Pair_DouyinRt_FiveStepCookPipelineEndToEndDualPackageBudgetsAndPreconditions)
    {
        // Step 5 前置门禁：若跳过任一上游步骤必须拒绝导出
        DouyinPipelineStepState incompleteSteps{true, true, false, true};
        E2E_ASSERT_FALSE(DouyinPackageExporter::validateStepPreconditions(incompleteSteps).ok());

        E2E_ASSERT_TRUE(DouyinShaderCooker::isDesktopOnlyPassTag("RayTracing"));
        E2E_ASSERT_TRUE(DouyinShaderCooker::isDesktopOnlyPassTag("DeferredGBuffer"));
        E2E_ASSERT_FALSE(DouyinShaderCooker::isDesktopOnlyPassTag("ForwardPlus"));

        E2E_ASSERT_TRUE(DouyinPackageExporter::isProhibitedMainPackageFilename("SimHei.ttf"));
        E2E_ASSERT_TRUE(DouyinPackageExporter::isProhibitedMainPackageFilename("PbrLit.spv"));
        E2E_ASSERT_FALSE(DouyinPackageExporter::isProhibitedMainPackageFilename("CombatDigits.mqfont"));

        T3Douyin_TempWorkspaceGuard ws("feat03_cook");
        DiskResourceLoader diskLoader;
        E2E_ASSERT_STATUS_OK(T3Douyin_StageMinimalProjectAssets(diskLoader, ws.projectDir));

        DouyinCookRequest req;
        req.projectDir = ws.projectDir;
        req.outputDir = ws.outputDir;

        std::vector<int> visitedSteps;
        float finalProgress = 0.0f;
        auto cookResOr = DouyinPackagerCliComposite::runCookPipeline(
            &diskLoader,
            req,
            T3Douyin_CookProgressRecorder{&visitedSteps, &finalProgress});
        E2E_ASSERT_STATUS_OK(cookResOr);
        const auto& res = *cookResOr;

        E2E_ASSERT_TRUE(res.stepState.step1ShadersDone);
        E2E_ASSERT_TRUE(res.stepState.step2AssetsDone);
        E2E_ASSERT_TRUE(res.stepState.step3ScriptsDone);
        E2E_ASSERT_TRUE(res.stepState.step4WasmDone);
        E2E_ASSERT_GE(visitedSteps.size(), 5u);
        E2E_ASSERT_NEAR(finalProgress, 1.0f, 1e-4f);

        // 校验 Step 4 Wasm 编译参数与标准 \0asm 魔数头
        const auto& flags = res.wasmReport.compileFlags;
        E2E_ASSERT_TRUE(std::find(flags.begin(), flags.end(), "-Oz") != flags.end());
        E2E_ASSERT_TRUE(std::find(flags.begin(), flags.end(), "-msimd128") != flags.end());
        E2E_ASSERT_TRUE(std::find(flags.begin(), flags.end(), "-fno-exceptions") != flags.end());
        E2E_ASSERT_GE(res.wasmReport.wasmBinary.size(), 8u);
        E2E_ASSERT_EQ(res.wasmReport.wasmBinary[0], 0x00u);
        E2E_ASSERT_EQ(res.wasmReport.wasmBinary[1], 0x61u);
        E2E_ASSERT_EQ(res.wasmReport.wasmBinary[2], 0x73u);
        E2E_ASSERT_EQ(res.wasmReport.wasmBinary[3], 0x6Du);

        // 校验 Step 5 双包导出体积红线：代码包 <= 4MB，主包 <= 10MB
        E2E_ASSERT_GT(res.exportReport.mainPackageRawBytes, 0u);
        E2E_ASSERT_LE(res.exportReport.codeRawBytes, 4ull * 1024ull * 1024ull);
        E2E_ASSERT_LE(res.exportReport.mainPackageRawBytes, 10ull * 1024ull * 1024ull);
        E2E_ASSERT_TRUE(diskLoader.exists(res.exportReport.mainPackageDir + "/game.json"));
        E2E_ASSERT_TRUE(diskLoader.exists(res.exportReport.mainPackageDir + "/game.js"));
        E2E_ASSERT_TRUE(diskLoader.exists(res.exportReport.mainPackageDir + "/tt-adapter.js"));

        // 跨特性配对：产出的 asset_manifest.bin 可被引擎 AssetManifest 原地无损加载寻址
        Asset::AssetManifest runtimeManifest;
        E2E_ASSERT_STATUS_OK(runtimeManifest.loadFromBuffer(
            std::span<const uint8_t>(
                res.assetReport.manifestBinary.data(),
                res.assetReport.manifestBinary.size())));
        E2E_ASSERT_GT(runtimeManifest.getRecordCount(), 0u);
        return true;
    }

    // =========================================================================
    // 4. FEAT_04: 抖音侧边栏复访场景 021036 鉴别 × 一键跳转与同周期幂等防刷领奖
    // =========================================================================
    E2E_TEST(Tier3, FEAT_04, Pair_DouyinRt_SidebarSceneRevisitDetectionNavigateAndIdempotentDailyClaim)
    {
        DouyinPlatformRuntimeComposite rtComp;
        rtComp.setCustomHostApi(DouyinMiniGame::createWebJsDouyinHostApi());
        SidebarRetentionService& sidebar = rtComp.getSidebarService();

        bool checkCalled = false;
        bool checkSupported = false;
        sidebar.checkSidebarAvailable(T3Douyin_BoolStatusOrRecorder{&checkCalled, &checkSupported});
        E2E_ASSERT_TRUE(checkCalled);
        E2E_ASSERT_TRUE(checkSupported);
        E2E_ASSERT_TRUE(sidebar.isSidebarSupported());

        // 普通信息流启动 (011001) 严禁冒领侧边栏复访奖励
        sidebar.handleOnShow(DouyinShowEvent{"011001", "homepage", "feed"});
        E2E_ASSERT_FALSE(sidebar.isLaunchedFromSidebar());
        auto deniedClaim = sidebar.claimReward("daily_2026_11_12");
        E2E_ASSERT_FALSE(deniedClaim.ok());

        uint32_t navCalls = 0;
        Status navStatus = InternalError("uninitialized");
        sidebar.navigateToSidebar(T3Douyin_StatusCallbackRecorder{&navCalls, &navStatus});
        E2E_ASSERT_EQ(navCalls, 1u);
        E2E_ASSERT_STATUS_OK(navStatus);

        // 用户从抖音首页侧边栏返回触发 onShow(scene == "021036")，首次领奖成功，重复点击幂等返回 false
        sidebar.handleOnShow(DouyinShowEvent{"021036", "homepage", "sidebar_card"});
        E2E_ASSERT_TRUE(sidebar.isLaunchedFromSidebar());

        auto firstClaimOr = sidebar.claimReward("daily_2026_11_12");
        E2E_ASSERT_STATUS_OK(firstClaimOr);
        E2E_ASSERT_TRUE(*firstClaimOr);
        E2E_ASSERT_TRUE(sidebar.hasClaimedReward("daily_2026_11_12"));

        auto dupClaimOr = sidebar.claimReward("daily_2026_11_12");
        E2E_ASSERT_STATUS_OK(dupClaimOr);
        E2E_ASSERT_FALSE(*dupClaimOr);
        return true;
    }

    // =========================================================================
    // 5. FEAT_05: 激励视频广告自动音频静音恢复 × 中途退出零发奖 vs 完整完播发奖契约
    // =========================================================================
    E2E_TEST(Tier3, FEAT_05, Pair_DouyinRt_RewardedVideoAdAudioMuteGuardAbortNoRewardVsCompleteGrant)
    {
        UniquePtr<IDouyinHostApi> hostApi = DouyinMiniGame::createWebJsDouyinHostApi();
        RewardedVideoAdService adService(hostApi.get());

        uint32_t muteTransitions = 0;
        bool currentMuted = false;
        adService.setAudioMuteCallback(T3Douyin_AudioMuteRecorder{&muteTransitions, &currentMuted});

        E2E_ASSERT_STATUS_OK(adService.initialize("adunit_douyin_tier3_05"));
        E2E_ASSERT_TRUE(adService.isInitialized());
        E2E_ASSERT_TRUE(adService.isPreloaded());

        uint32_t resultCount = 0;
        RewardedAdCloseResult lastResult{};

        // 第一次拉起播放：自动静音游戏主音轨，模拟用户中途提前关闭 (isEnded = false)
        E2E_ASSERT_STATUS_OK(adService.show(T3Douyin_RewardAdResultRecorder{&resultCount, &lastResult}));
        E2E_ASSERT_TRUE(adService.isShowing());
        E2E_ASSERT_TRUE(adService.isEngineAudioMuted());
        E2E_ASSERT_TRUE(currentMuted);

        // 播放期间禁止重入并发 show()
        E2E_ASSERT_FALSE(adService.show(T3Douyin_RewardAdResultRecorder{&resultCount, &lastResult}).ok());

        adService.notifyAdClosed(false);
        E2E_ASSERT_FALSE(adService.isShowing());
        E2E_ASSERT_FALSE(adService.isEngineAudioMuted());
        E2E_ASSERT_FALSE(currentMuted);
        E2E_ASSERT_EQ(resultCount, 1u);
        E2E_ASSERT_FALSE(lastResult.rewardGranted);
        E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 0u);

        // 第二次拉起播放并完整观看结束 (isEnded = true)：自动恢复音轨并精确发放 1 次奖励
        E2E_ASSERT_STATUS_OK(adService.show(T3Douyin_RewardAdResultRecorder{&resultCount, &lastResult}));
        E2E_ASSERT_TRUE(currentMuted);
        adService.notifyAdClosed(true);

        E2E_ASSERT_FALSE(currentMuted);
        E2E_ASSERT_EQ(resultCount, 2u);
        E2E_ASSERT_TRUE(lastResult.isEnded);
        E2E_ASSERT_TRUE(lastResult.rewardGranted);
        E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 1u);
        E2E_ASSERT_EQ(muteTransitions, 4u);
        return true;
    }

    // =========================================================================
    // 6. FEAT_06: 环形录屏 300s 钳位与高光切片 × <3s 短录屏防呆拦截与短视频话题挂载分享
    // =========================================================================
    E2E_TEST(Tier3, FEAT_06, Pair_DouyinRt_GameRecorderRingClamp300sShortStopGuardClipAndTopicShare)
    {
        UniquePtr<IDouyinHostApi> hostApi = DouyinMiniGame::createWebJsDouyinHostApi();
        GameRecorderService recorder(hostApi.get());

        // 超出 300s 请求自动钳位至抖音环形缓冲上限 300s
        E2E_ASSERT_STATUS_OK(recorder.start(900));
        E2E_ASSERT_TRUE(recorder.getState() == RecorderState::Recording);
        E2E_ASSERT_EQ(recorder.getActiveDurationLimitSec(), GameRecorderService::kMaxDurationSeconds);

        E2E_ASSERT_STATUS_OK(recorder.pause());
        E2E_ASSERT_TRUE(recorder.getState() == RecorderState::Paused);
        E2E_ASSERT_STATUS_OK(recorder.resume());
        E2E_ASSERT_TRUE(recorder.getState() == RecorderState::Recording);

        E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(8.0, 4.0));
        E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 1u);

        // 录制不足 3.0s 停止必须返回错误状态且不得产出残缺路径
        auto shortStopOr = recorder.stop(1.75);
        E2E_ASSERT_FALSE(shortStopOr.ok());
        E2E_ASSERT_TRUE(recorder.getState() == RecorderState::Idle);

        // 重新录制满 12.5s 后正常生成临时视频路径并挂载话题拉起拍抖音分享器
        E2E_ASSERT_STATUS_OK(recorder.start(180));
        auto validVideoPathOr = recorder.stop(12.5);
        E2E_ASSERT_STATUS_OK(validVideoPathOr);
        E2E_ASSERT_FALSE(validVideoPathOr->empty());

        DouyinShareVideoRequest shareReq;
        shareReq.title = "首通高光连击";
        shareReq.videoTopics = {"抖音小游戏", "MQEngine"};

        uint32_t shareCbCount = 0;
        Status shareStatus = InternalError("pending");
        recorder.shareVideo(shareReq, T3Douyin_StatusCallbackRecorder{&shareCbCount, &shareStatus});
        E2E_ASSERT_EQ(shareCbCount, 1u);
        E2E_ASSERT_STATUS_OK(shareStatus);
        return true;
    }

    // =========================================================================
    // 7. FEAT_07: CapsuleAvoidanceBridge 跨插件直注 RmlUiComposite × OpenData 排行榜按需纹理同步
    // =========================================================================
    E2E_TEST(Tier3, FEAT_07, Pair_DouyinRt_CapsuleBridgeInjectsIntoRmlUiAndOpenDataSyncsSharedRankTexture)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        T3Douyin_InitHeadlessWgl2(wgl2Ctx);

        UniquePtr<IDouyinHostApi> hostApi = DouyinMiniGame::createWebJsDouyinHostApi();

        RmlUiComposite uiComp;
        uiComp.initFontSubsystem();
        const std::string hudRml =
            "<rml><body id=\"body\"><div id=\"top_right_hud\">Menu</div></body></rml>";
        E2E_ASSERT_STATUS_OK(uiComp.loadDocumentFromMemory(hudRml, "douyin_hud"));

        CapsuleAvoidanceBridge capsuleBridge(hostApi.get());
        capsuleBridge.setRmlUiComposite(&uiComp);
        capsuleBridge.setTargetRootElementId("body");

        // 注入包含负坐标噪声的边界框，验证净化钳制并写穿至 RmlUiComposite
        DouyinSystemSafeArea sysArea{390.0f, 844.0f, 2.0f, -4.0f, 44.0f, 390.0f, 810.0f};
        DouyinCapsuleRect rawCapsule{290.0f, 48.0f, 378.0f, 80.0f, 88.0f, 32.0f};
        const auto metrics = capsuleBridge.applyExplicitRects(sysArea, rawCapsule);
        E2E_ASSERT_GE(metrics.safeLeftPx, 0.0f);
        E2E_ASSERT_GE(metrics.capsuleAvoidTopPx, 48.0f);
        E2E_ASSERT_GE(metrics.capsuleAvoidRightPx, 100.0f);
        E2E_ASSERT_FALSE(capsuleBridge.getInjectedCssVars().empty());

        VirtualTextureRouter router;
        OpenDataLeaderboardController leaderboard(&wgl2Ctx, &router);
        (void)leaderboard.initializeSharedCanvas(256u, 256u);
        FCT::Image* rankImg = leaderboard.getSharedCanvasImage();
        E2E_ASSERT_TRUE(rankImg != nullptr);

        OpenDataDomainBridge openData(hostApi.get());
        openData.bindTargetRankTexture(rankImg);
        openData.setSyncIntervalSeconds(0.10f);

        // 非法非平衡 JSON 负载必须被拦截，合法单向分数上报正常送达
        E2E_ASSERT_FALSE(openData.postMessage("invalid_raw_text").ok());
        E2E_ASSERT_STATUS_OK(openData.postMessage("{\"command\":\"setUserScore\",\"score\":12800}"));
        E2E_ASSERT_EQ(openData.getPostedMessageCount(), 1u);

        // 排行榜未打开时 tick 不产生任何 SharedCanvas 像素回读与上传
        openData.setLeaderboardVisible(false);
        openData.tick(0.50f);
        E2E_ASSERT_EQ(openData.getSharedCanvasSyncCount(), 0u);

        // 打开排行榜弹窗后按同步周期拉取子域画布并更新 @mq_shared_canvas/rank 绑定的 GPU 纹理
        openData.setLeaderboardVisible(true);
        openData.tick(0.15f);
        E2E_ASSERT_GE(openData.getSharedCanvasSyncCount(), 1u);

        uiComp.onUnload();
        return true;
    }

    // =========================================================================
    // 8. FEAT_44 (1/2): FCT_WebJS 句柄表引用计数复用 × globalThis.tt 跨层宿主对象绑定验证
    // =========================================================================
    E2E_TEST(Tier3, FEAT_44, Pair_DouyinRt_WebJsHandleTableRetainReleaseSlotReuseAndGlobalTtBinding)
    {
        UniquePtr<IDouyinHostApi> hostApi = DouyinMiniGame::createWebJsDouyinHostApi();
        (void)hostApi->querySystemSafeArea();

        // DouyinWebJsAdapter 初始化后必须在 globalThis (HandleGlobal = 1) 上挂载有效动态 tt 对象句柄
        const uint32_t ttHandle = fct_webjs_get_prop_handle(kWebJsHandleGlobal, "tt");
        E2E_ASSERT_GE(ttHandle, kWebJsHandleFirstDynamic);

        const uint32_t parentObj = fct_webjs_alloc_object();
        const uint32_t childObj = fct_webjs_alloc_object();
        E2E_ASSERT_GE(parentObj, kWebJsHandleFirstDynamic);
        E2E_ASSERT_NE(parentObj, childObj);

        fct_webjs_set_prop_string(childObj, "tag", "douyin_session");
        fct_webjs_set_prop_number(childObj, "fpsBudget", 60.0);
        fct_webjs_set_prop_handle(parentObj, "session", childObj);
        E2E_ASSERT_EQ(fct_webjs_get_prop_handle(parentObj, "session"), childObj);
        E2E_ASSERT_EQ(fct_webjs_get_prop_handle(parentObj, "non_existent"), kWebJsHandleUndefined);

        // 验证引用计数递增与归零后的空闲链表槽位精确复用（零句柄泄漏）
        fct_webjs_retain_handle(childObj);
        fct_webjs_release_handle(childObj);
        // 此时引用计数仍为 1，新分配对象不得抢占存活的 childObj 槽位
        const uint32_t anotherObj = fct_webjs_alloc_object();
        E2E_ASSERT_NE(anotherObj, childObj);

        // 第二次释放使 childObj 引用计数归零进入 freeList，下一次分配必然立即复用该槽位 ID
        fct_webjs_release_handle(childObj);
        const uint32_t reusedObj = fct_webjs_alloc_object();
        E2E_ASSERT_EQ(reusedObj, childObj);

        fct_webjs_release_handle(reusedObj);
        fct_webjs_release_handle(anotherObj);
        fct_webjs_release_handle(parentObj);
        return true;
    }

    // =========================================================================
    // 9. FEAT_44 (2/2): FCT_WebJS 零拷贝 TypedArray 线性内存视图 × Promise 决议/拒绝微任务闭环
    // =========================================================================
    E2E_TEST(Tier3, FEAT_44, Pair_DouyinRt_WebJsZeroCopyTypedArrayViewAndPromiseSettleLifecycle)
    {
        UniquePtr<IDouyinHostApi> hostApi = DouyinMiniGame::createWebJsDouyinHostApi();

        alignas(16) float linearInstanceMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            12.5f, 4.0f, -8.0f, 1.0f
        };

        const uint32_t typedArrHandle = fct_webjs_create_typed_array(
            linearInstanceMatrix,
            sizeof(linearInstanceMatrix),
            kWebJsTypedArrayFloat32);
        E2E_ASSERT_GE(typedArrHandle, kWebJsHandleFirstDynamic);

        const uint32_t uploadPacket = fct_webjs_alloc_object();
        fct_webjs_set_prop_handle(uploadPacket, "matrixView", typedArrHandle);
        E2E_ASSERT_EQ(fct_webjs_get_prop_handle(uploadPacket, "matrixView"), typedArrHandle);

        // 验证 Promise 成功决议与异常拒绝句柄生命周期及宿主微任务泵送
        const uint32_t fulfilledPromise = fct_webjs_alloc_object();
        const uint32_t rejectedPromise = fct_webjs_alloc_object();
        const uint32_t errPayload = fct_webjs_alloc_object();
        fct_webjs_set_prop_string(errPayload, "reason", "network_timeout");

        fct_webjs_promise_resolve(fulfilledPromise, uploadPacket);
        fct_webjs_promise_reject(rejectedPromise, errPayload);
        hostApi->pumpMicrotasks();

        fct_webjs_release_handle(errPayload);
        fct_webjs_release_handle(rejectedPromise);
        fct_webjs_release_handle(fulfilledPromise);
        fct_webjs_release_handle(uploadPacket);
        fct_webjs_release_handle(typedArrHandle);
        return true;
    }

    // =========================================================================
    // 10. FEAT_53: RmlUiKeyboardBridge 聚焦唤起原生软键盘 × UTF-8 多字节码点边界截断与失焦收起
    // =========================================================================
    E2E_TEST(Tier3, FEAT_53, Pair_DouyinRt_RmlUiKeyboardBridgeFocusShowsNativeKeyboardUtf8TruncateAndBlurHides)
    {
        (void)FCT::SAI::VirtualKeyboard::hide();
        FCT::SAI::VirtualKeyboard::clearCallbacks();

        RmlUiComposite uiComp;
        uiComp.initFontSubsystem();
        const std::string formRml =
            "<rml><body><input id=\"role_name\" type=\"text\" maxlength=\"5\" value=\"旅人\"/></body></rml>";
        E2E_ASSERT_STATUS_OK(uiComp.loadDocumentFromMemory(formRml, "login_form"));

        RmlUiKeyboardBridge kbBridge(uiComp.getActiveDocument());
        E2E_ASSERT_FALSE(FCT::SAI::VirtualKeyboard::isShowing());

        // 聚焦输入框自动拉起 FCT::SAI::VirtualKeyboard 并同步默认值与最大字符数上限 5
        kbBridge.onElementFocus("role_name", "text", "旅人", 5);
        E2E_ASSERT_TRUE(kbBridge.isKeyboardActive());
        E2E_ASSERT_TRUE(FCT::SAI::VirtualKeyboard::isShowing());
        E2E_ASSERT_EQ(FCT::SAI::VirtualKeyboard::getCurrentConfig().maxLength, 5);
        E2E_ASSERT_EQ(kbBridge.getFocusedElementId(), "role_name");

        // 通过宿主 C FFI 注入 8 个码点的超长中文与 Emoji 混合串，必须在第 5 个完整 UTF-8 码点边界截断
        fct_keyboard_dispatch_input("无敌暴龙神🔥88");
        const std::string backfilled = kbBridge.getElementValue("role_name");
        E2E_ASSERT_EQ(backfilled, "无敌暴龙神");
        // 5 个汉字恰占 15 字节，严禁截出半个 UTF-8 多字节残片
        E2E_ASSERT_EQ(backfilled.size(), 15u);

        fct_keyboard_dispatch_confirm("剑圣🔥A999");
        E2E_ASSERT_EQ(kbBridge.getElementValue("role_name"), "剑圣🔥A9");

        kbBridge.onElementBlur("role_name");
        E2E_ASSERT_FALSE(kbBridge.isKeyboardActive());
        E2E_ASSERT_FALSE(FCT::SAI::VirtualKeyboard::isShowing());

        uiComp.onUnload();
        return true;
    }

    // =========================================================================
    // 11. FEAT_67: 64 位双向标签掩码匹配器 (.mqtech required/excluded Pass 与 Input 正交过滤)
    // =========================================================================
    E2E_TEST(Tier3, FEAT_67, Pair_DouyinRt_Bidirectional64BitTagMaskMatchingAndExcludedTagFiltering)
    {
        const std::string mqtechToml = R"(
[technique]
id = 67

[[pipelines]]
id = 6701
name = "PBR_OpaqueForward"
required_pass_tags = ["Geometry", "Opaque", "ForwardPlus"]
excluded_pass_tags = ["Shadow", "UI"]
required_input_tags = ["Mesh", "Opaque"]
excluded_input_tags = ["Translucent", "UI"]
order = 10

[[pipelines]]
id = 6702
name = "ShadowDepth_Caster"
required_pass_tags = ["Shadow"]
excluded_pass_tags = ["UI", "PostProcess"]
required_input_tags = ["Mesh", "CastShadow"]
excluded_input_tags = ["UI"]
render_target_count = 0
depth_write = true
order = 0
)";

        auto pipesOr = RenderGraphMatcher::LoadTechFromToml(mqtechToml, 67u);
        E2E_ASSERT_STATUS_OK(pipesOr);
        E2E_ASSERT_EQ(pipesOr->size(), 2u);

        RenderGraphMatcher matcher;
        for (const auto& p : *pipesOr)
        {
            matcher.registerPipeline(p);
        }

        const DrawBucket opaqueCaster(67u, InputTags::Mesh | InputTags::Opaque | InputTags::CastShadow, 16u);
        const DrawBucket glassNoShadow(67u, InputTags::Mesh | InputTags::Translucent, 4u);
        const DrawBucket uiWidget(67u, InputTags::UI | InputTags::RmlUi, 1u);
        const std::vector<DrawBucket> frameBuckets = {opaqueCaster, glassNoShadow, uiWidget};

        // ForwardPlus Opaque Pass 仅命中 opaqueCaster，含 excludedInputTags 的半透明与 UI 桶全部被硬排除
        const uint64_t forwardPassMask = PassTags::Geometry | PassTags::Opaque | PassTags::ForwardPlus;
        const auto forwardExec = matcher.evaluatePassExecutionItems(forwardPassMask, frameBuckets);
        E2E_ASSERT_EQ(forwardExec.size(), 1u);
        E2E_ASSERT_EQ(forwardExec[0].pipelineId, 6701u);
        E2E_ASSERT_EQ(forwardExec[0].bucket->instanceCount, 16u);

        // Shadow Pass 仅命中携带 CastShadow 的网格桶
        const auto shadowExec = matcher.evaluatePassExecutionItems(PassTags::Shadow, frameBuckets);
        E2E_ASSERT_EQ(shadowExec.size(), 1u);
        E2E_ASSERT_EQ(shadowExec[0].pipelineId, 6702u);
        return true;
    }

    // =========================================================================
    // 12. FEAT_68: 纯数据 Pass 容器零 Shader 字段约束 × 8 管线槽上限、Pin Slot 回退与有向环拦截
    // =========================================================================
    E2E_TEST(Tier3, FEAT_68, Pair_DouyinRt_PureDataPassNodeZeroShaderConstraintSlotCapAndCycleRejection)
    {
        E2E_ASSERT_TRUE(std::is_standard_layout_v<PureDataPassNode>);

        PureDataPassNode pureNode{};
        E2E_ASSERT_FALSE(pureNode.bindPipeline(0u).ok());
        for (uint32_t slot = 1u; slot <= 8u; ++slot)
        {
            E2E_ASSERT_STATUS_OK(pureNode.bindPipeline(6800u + slot));
        }
        E2E_ASSERT_EQ(pureNode.getBoundPipelines().size(), 8u);
        E2E_ASSERT_FALSE(pureNode.bindPipeline(6899u).ok());

        // 1) 在 .pgraph Pass 节点内夹带任何 Shader 字段必须被解析器当场拦截
        const std::string illegalShaderGraphToml = R"(
name = "IllegalShaderInPass"
[[passes]]
name = "MainPass"
tags = ["ForwardPlus"]
targets = ["$Backbuffer"]
vertex_shader = "Shaders/IllegalEmbedded.vs"
)";
        E2E_ASSERT_FALSE(RenderGraphAsset::LoadFromToml(illegalShaderGraphToml).ok());

        // 2) 含读写互相等待的有向循环图谱必须被 Kahn 拓扑环检测拦截
        const std::string cyclicGraphToml = R"(
name = "CyclicHazardGraph"
[[images]]
name = "PingTex"
format = "RGBA8_UNORM"
[[images]]
name = "PongTex"
format = "RGBA8_UNORM"

[[passes]]
name = "PassPing"
tags = ["PostProcess"]
targets = ["PingTex"]
textures = [{ slot = "prev", source = "PongTex", fallback = "Black1x1" }]

[[passes]]
name = "PassPong"
tags = ["PostProcess"]
targets = ["PongTex"]
textures = [{ slot = "prev", source = "PingTex", fallback = "Black1x1" }]
)";
        E2E_ASSERT_FALSE(RenderGraphAsset::LoadFromToml(cyclicGraphToml).ok());

        // 3) 合法纯数据无环图谱正常解析 Pin Slot 物理目标与未连线 fallback 默认贴图
        const std::string validPinGraphToml = R"(
name = "ValidPinSlotGraph"
[[images]]
name = "ShadowMapTex"
format = "D24_UNORM_S8_UINT"

[[passes]]
name = "ShadowPass"
tags = ["Shadow"]
depth_stencil = "ShadowMapTex"
clear_depth = 1.0

[[passes]]
name = "ForwardLitPass"
tags = ["Geometry", "Opaque", "ForwardPlus"]
targets = ["$Backbuffer"]
textures = [
    { slot = "tShadowAtlas", source = "ShadowMapTex", fallback = "White1x1" },
    { slot = "tEnvBrdfLut", source = "", fallback = "DefaultBrdfLut" }
]
)";
        auto validGraphOr = RenderGraphAsset::LoadFromToml(validPinGraphToml);
        E2E_ASSERT_STATUS_OK(validGraphOr);
        E2E_ASSERT_TRUE(validGraphOr->isAcyclic());

        auto shadowPinOr = validGraphOr->resolvePinSlot("ForwardLitPass", "tShadowAtlas");
        E2E_ASSERT_STATUS_OK(shadowPinOr);
        E2E_ASSERT_EQ(*shadowPinOr, "ShadowMapTex");

        auto fallbackPinOr = validGraphOr->resolvePinSlot("ForwardLitPass", "tEnvBrdfLut");
        E2E_ASSERT_STATUS_OK(fallbackPinOr);
        E2E_ASSERT_EQ(*fallbackPinOr, "DefaultBrdfLut");
        return true;
    }

    // =========================================================================
    // 13. FEAT_69: 上下游双重布局签名防呆校验（缺失顶点切线 / MRT 数量失配 / 无深度附件深度写）
    // =========================================================================
    E2E_TEST(Tier3, FEAT_69, Pair_DouyinRt_DualBoundaryLayoutSignatureValidationCatchesVertexMrtAndDepthMismatches)
    {
        PipelineBindingDesc gbufferPipe{};
        gbufferPipe.pipelineId = 6901u;
        gbufferPipe.name = "Deferred_NormalMappedGBuffer";
        gbufferPipe.expectedVertexAttributes = {"Position3f", "Normal3f", "TexCoord2f", "Tangent4f"};
        gbufferPipe.expectedRenderTargetCount = 3u;
        gbufferPipe.requiresDepthWrite = true;

        DrawBucket incompleteMeshBucket(1u, InputTags::Mesh | InputTags::Opaque, 8u);
        incompleteMeshBucket.attributeNames = {"Position3f", "Normal3f", "TexCoord2f"};

        DrawBucket completeMeshBucket(1u, InputTags::Mesh | InputTags::Opaque, 8u);
        completeMeshBucket.attributeNames = {"Position3f", "Normal3f", "TexCoord2f", "Tangent4f"};

        // 上游校验：网格缺失管线要求的 Tangent4f 顶点属性，必须在提交前拦截
        const Status missingAttrSt = RenderGraphMatcher::validateLayoutSignatures(
            gbufferPipe, incompleteMeshBucket, 3u, true);
        E2E_ASSERT_FALSE(missingAttrSt.ok());

        // 下游校验 1：3 输出 GBuffer 管线被错误挂载到仅含 1 个颜色附件的 Forward Pass
        const Status mrtMismatchSt = RenderGraphMatcher::validateLayoutSignatures(
            gbufferPipe, completeMeshBucket, 1u, true);
        E2E_ASSERT_FALSE(mrtMismatchSt.ok());

        // 下游校验 2：开启深度写入的 3D 管线被挂载到无深度缓冲的纯 2D Pass
        const Status missingDepthSt = RenderGraphMatcher::validateLayoutSignatures(
            gbufferPipe, completeMeshBucket, 3u, false);
        E2E_ASSERT_FALSE(missingDepthSt.ok());

        // 上游顶点属性齐备且下游 3-MRT + DepthStencil 签名完全吻合时放行通过
        const Status matchedSt = RenderGraphMatcher::validateLayoutSignatures(
            gbufferPipe, completeMeshBucket, 3u, true);
        E2E_ASSERT_STATUS_OK(matchedSt);
        return true;
    }

    // =========================================================================
    // 14. FEAT_71: 同 Pass 多管线执行队列 (order, pipelineId) 跨正逆序注册严格确定性排序不变量
    // =========================================================================
    E2E_TEST(Tier3, FEAT_71, Pair_DouyinRt_SamePassExecutionItemsDeterministicSortOrderAndPipelineIdInvariant)
    {
        PipelineBindingDesc pEarlyPrepass{};
        pEarlyPrepass.pipelineId = 7140u;
        pEarlyPrepass.ownerTechId = 1u;
        pEarlyPrepass.requiredPassTags = PassTags::ForwardPlus;
        pEarlyPrepass.requiredInputTags = InputTags::Mesh;
        pEarlyPrepass.order = -10;

        PipelineBindingDesc pOpaqueA{};
        pOpaqueA.pipelineId = 7110u;
        pOpaqueA.ownerTechId = 1u;
        pOpaqueA.requiredPassTags = PassTags::ForwardPlus;
        pOpaqueA.requiredInputTags = InputTags::Mesh;
        pOpaqueA.order = 10;

        PipelineBindingDesc pOpaqueB{};
        pOpaqueB.pipelineId = 7125u;
        pOpaqueB.ownerTechId = 1u;
        pOpaqueB.requiredPassTags = PassTags::ForwardPlus;
        pOpaqueB.requiredInputTags = InputTags::Mesh;
        pOpaqueB.order = 10;

        PipelineBindingDesc pLateOutline{};
        pLateOutline.pipelineId = 7105u;
        pLateOutline.ownerTechId = 1u;
        pLateOutline.requiredPassTags = PassTags::ForwardPlus;
        pLateOutline.requiredInputTags = InputTags::Mesh;
        pLateOutline.order = 50;

        RenderGraphMatcher matcherForward;
        matcherForward.registerPipeline(pLateOutline);
        matcherForward.registerPipeline(pOpaqueB);
        matcherForward.registerPipeline(pOpaqueA);
        matcherForward.registerPipeline(pEarlyPrepass);

        RenderGraphMatcher matcherReverse;
        matcherReverse.registerPipeline(pEarlyPrepass);
        matcherReverse.registerPipeline(pOpaqueA);
        matcherReverse.registerPipeline(pOpaqueB);
        matcherReverse.registerPipeline(pLateOutline);

        const std::vector<DrawBucket> buckets = {
            DrawBucket(1u, InputTags::Mesh | InputTags::Opaque, 24u)
        };

        const auto itemsA = matcherForward.evaluatePassExecutionItems(PassTags::ForwardPlus, buckets);
        const auto itemsB = matcherReverse.evaluatePassExecutionItems(PassTags::ForwardPlus, buckets);

        E2E_ASSERT_EQ(itemsA.size(), 4u);
        E2E_ASSERT_EQ(itemsB.size(), 4u);

        // 无论管线注册先后如何乱序，同 Pass 执行顺序必须严格收敛为 (-10,7140) -> (10,7110) -> (10,7125) -> (50,7105)
        const uint32_t expectedIds[4] = {7140u, 7110u, 7125u, 7105u};
        for (size_t i = 0; i < 4u; ++i)
        {
            E2E_ASSERT_EQ(itemsA[i].pipelineId, expectedIds[i]);
            E2E_ASSERT_EQ(itemsB[i].pipelineId, expectedIds[i]);
        }
        return true;
    }

    // =========================================================================
    // 15. FEAT_72: 四大官方渲染图预设拓扑对比 × TBDR 深度丢弃后下游非法采样防呆审计
    // =========================================================================
    E2E_TEST(Tier3, FEAT_72, Pair_DouyinRt_FourOfficialRenderGraphPresetsTopologyBackbufferAndDepthHazardAudit)
    {
        const RenderGraphAssetDesc desktopDeferred = RenderGraphAsset::CreateDesktopDeferredHdrPreset();
        const RenderGraphAssetDesc desktopHybridRt = RenderGraphAsset::CreateDesktopHybridRayTracingPreset();
        const RenderGraphAssetDesc miniForwardPlus = RenderGraphAsset::CreateMiniGameForwardPlusPreset();
        const RenderGraphAssetDesc miniForwardLite = RenderGraphAsset::CreateMiniGameForwardLitePreset();

        E2E_ASSERT_TRUE(desktopDeferred.isAcyclic());
        E2E_ASSERT_TRUE(desktopHybridRt.isAcyclic());
        E2E_ASSERT_TRUE(miniForwardPlus.isAcyclic());
        E2E_ASSERT_TRUE(miniForwardLite.isAcyclic());

        E2E_ASSERT_STATUS_OK(desktopDeferred.validateDepthSamplingHazard());
        E2E_ASSERT_STATUS_OK(desktopHybridRt.validateDepthSamplingHazard());
        E2E_ASSERT_STATUS_OK(miniForwardPlus.validateDepthSamplingHazard());
        E2E_ASSERT_STATUS_OK(miniForwardLite.validateDepthSamplingHazard());

        // 桌面端预设拥有多张离屏 GBuffer/HDR 颜色图像，且 HybridRT 预设包含光线追踪标签
        E2E_ASSERT_GT(desktopDeferred.countOffscreenColorImages(), 0u);
        E2E_ASSERT_TRUE((desktopDeferred.collectUnionPassTagMask() & PassTags::DeferredGBuffer) != 0ULL);
        E2E_ASSERT_TRUE((desktopHybridRt.collectUnionPassTagMask() & PassTags::RayTracing) != 0ULL);

        // PRD 10 §3.1-3.2：ForwardPlus 保留单张非 MRT 离屏 HDR 缓冲，ForwardLite 直出主屏 Backbuffer（0 离屏）
        E2E_ASSERT_FALSE(miniForwardPlus.isDirectToBackbuffer());
        E2E_ASSERT_EQ(miniForwardPlus.countOffscreenColorImages(), 1u);
        E2E_ASSERT_TRUE((miniForwardPlus.collectUnionPassTagMask() & PassTags::ForwardPlus) != 0ULL);

        E2E_ASSERT_TRUE(miniForwardLite.isDirectToBackbuffer());
        E2E_ASSERT_EQ(miniForwardLite.countOffscreenColorImages(), 0u);
        E2E_ASSERT_TRUE((miniForwardLite.collectUnionPassTagMask() & PassTags::ForwardLite) != 0ULL);
        E2E_ASSERT_EQ(miniForwardLite.collectUnionPassTagMask() & PassTags::DeferredGBuffer, 0ULL);

        // 构造上游已声明 discardDepthAfterPass (glInvalidateFramebuffer) 却在下游继续采样的非法图谱，验证防呆拦截
        RenderGraphAssetDesc hazardGraph = miniForwardPlus;
        RenderGraphPassNodeDesc basePass{};
        basePass.name = "TileDiscardPass";
        basePass.depthStencilImage = "TransientDepthD24";
        basePass.discardDepthAfterPass = true;

        RenderGraphPassNodeDesc illegalPostPass{};
        illegalPostPass.name = "IllegalSsaoSampleAfterDiscard";
        illegalPostPass.targets.push_back(PassTargetBinding{"$Backbuffer", true});
        illegalPostPass.textures.push_back(PassTextureInput{"tDepth", "TransientDepthD24", "White1x1"});

        hazardGraph.passes.clear();
        hazardGraph.passes.push_back(std::move(basePass));
        hazardGraph.passes.push_back(std::move(illegalPostPass));
        E2E_ASSERT_FALSE(hazardGraph.validateDepthSamplingHazard().ok());
        return true;
    }
} // namespace MQEngine::Testing
