/**
 * @file Test_Tier4_S5_DouyinEcosystemAndCookLoop.cpp
 * @brief Tier 4 真实负载综合场景 S5：五步烘焙导出双包、Python/Node 门禁验证、抖音宿主生态服务与内存自救闭环
 */
#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include "DouyinMiniGame.h"
#include "GameHtmlUI.h"
#include <FCT_SAI/FileSystem.h>
#include <FCT/UI/MiniGame_Window.h>
#include <FCT/Context/TransientResourcePool.h>

#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include <MQEngine/asset/loader/AsyncLoader.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using namespace MQEngine::DouyinMiniGame;

        std::string resolveTestingToolScript(std::string_view relativePath)
        {
#ifdef MQ_E2E_REPO_ROOT
            const std::filesystem::path fromMacro = std::filesystem::path(MQ_E2E_REPO_ROOT) / relativePath;
            if (std::filesystem::exists(fromMacro))
            {
                return fromMacro.generic_string();
            }
#endif
            const std::string found = FCT::SAI::FileSystem::findUpwards(
                std::filesystem::current_path().string(),
                relativePath,
                8);
            return found;
        }

        class ScenarioHostApiAdapter : public IDouyinHostApi
        {
        public:
            ScenarioHostApiAdapter()
                : m_realApi(::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi())
            {
            }

            ~ScenarioHostApiAdapter() override = default;

            bool sidebarSceneAvailable{true};
            bool shouldCloseAdEnded{true};
            bool splashDismissed{false};
            int32_t lastRecorderDuration{0};
            std::string lastSharedTopic;
            std::string lastOpenDataJson;
            uint32_t rankReadCount{0};
            DouyinSystemSafeArea safeArea{390.0f, 844.0f, 3.0f, 0.0f, 47.0f, 390.0f, 810.0f};
            DouyinCapsuleRect capsuleRect{288.0f, 51.0f, 375.0f, 83.0f, 87.0f, 32.0f};

            void bindOnShowListener(std::function<void(const DouyinShowEvent&)> listener) override
            {
                m_onShow = listener;
                m_realApi->bindOnShowListener(std::move(listener));
            }

            void bindOnMemoryWarningListener(std::function<void(int32_t)> listener) override
            {
                m_onMemWarn = listener;
                m_realApi->bindOnMemoryWarningListener(std::move(listener));
            }

            void checkSidebarScene(std::function<void(StatusOr<bool>)> callback) override
            {
                m_realApi->checkSidebarScene(nullptr);
                if (callback)
                {
                    callback(sidebarSceneAvailable);
                }
            }

            void navigateToSidebarScene(std::function<void(Status)> callback) override
            {
                m_realApi->navigateToSidebarScene(nullptr);
                if (callback)
                {
                    callback(sidebarSceneAvailable ? OkStatus() : FailedPreconditionError("Sidebar unavailable"));
                }
            }

            Status createRewardedVideoAd(const std::string& adUnitId) override
            {
                Status st = m_realApi->createRewardedVideoAd(adUnitId);
                if (!st.ok())
                {
                    return st;
                }
                m_hasAdUnit = true;
                return st;
            }

            Status loadRewardedVideoAd() override
            {
                const Status realSt = m_realApi->loadRewardedVideoAd();
                if (m_hasAdUnit)
                {
                    return realSt;
                }
                return OkStatus();
            }

            void showRewardedVideoAd(
                std::function<void(bool)> onClose,
                std::function<void(int32_t, const std::string&)> /*onError*/) override
            {
                m_realApi->showRewardedVideoAd(nullptr, nullptr);
                m_pendingAdClose = std::move(onClose);
            }

            void finishShowingAd()
            {
                if (m_pendingAdClose)
                {
                    auto cb = std::move(m_pendingAdClose);
                    m_pendingAdClose = nullptr;
                    cb(shouldCloseAdEnded);
                }
            }

            Status recorderStart(int32_t durationSec) override
            {
                lastRecorderDuration = durationSec;
                return m_realApi->recorderStart(durationSec);
            }

            Status recorderPause() override { return m_realApi->recorderPause(); }
            Status recorderResume() override { return m_realApi->recorderResume(); }

            StatusOr<std::string> recorderStop() override
            {
                auto realStopOr = m_realApi->recorderStop();
                (void)realStopOr;
                return std::string("ttfile://temp/highlight_clip.mp4");
            }

            Status recorderRecordClip(double beforeSec, double afterSec) override
            {
                return m_realApi->recorderRecordClip(beforeSec, afterSec);
            }

            void shareVideoMessage(
                const DouyinShareVideoRequest& req,
                std::function<void(Status)> callback) override
            {
                if (!req.videoTopics.empty())
                {
                    lastSharedTopic = req.videoTopics.front();
                }
                m_realApi->shareVideoMessage(req, nullptr);
                if (callback)
                {
                    callback(req.videoPath.empty() ? InvalidArgumentError("Missing videoPath") : OkStatus());
                }
            }

            DouyinSystemSafeArea querySystemSafeArea() override
            {
                (void)m_realApi->querySystemSafeArea();
                return safeArea;
            }

            DouyinCapsuleRect queryMenuButtonCapsuleRect() override
            {
                (void)m_realApi->queryMenuButtonCapsuleRect();
                return capsuleRect;
            }

            Status postOpenDataMessage(const std::string& jsonPayload) override
            {
                lastOpenDataJson = jsonPayload;
                return m_realApi->postOpenDataMessage(jsonPayload);
            }

            bool readSharedRankCanvasRgba(
                uint32_t& outWidth,
                uint32_t& outHeight,
                std::vector<uint8_t>& outRgbaPixels) override
            {
                ++rankReadCount;
                (void)m_realApi->readSharedRankCanvasRgba(outWidth, outHeight, outRgbaPixels);
                outWidth = 256u;
                outHeight = 256u;
                outRgbaPixels.assign(256u * 256u * 4u, 200u);
                return true;
            }

            void dismissBootSplash() override
            {
                splashDismissed = true;
                m_realApi->dismissBootSplash();
            }

            void pumpMicrotasks() override
            {
                m_realApi->pumpMicrotasks();
            }

        private:
            UniquePtr<IDouyinHostApi> m_realApi;
            bool m_hasAdUnit{false};
            std::function<void(const DouyinShowEvent&)> m_onShow;
            std::function<void(int32_t)> m_onMemWarn;
            std::function<void(bool)> m_pendingAdClose;
        };

        struct CookProgressTracker
        {
            uint32_t callbackCount{0};
            float lastRatio{0.0f};
        };

        struct CookProgressFunctor
        {
            CookProgressTracker* tracker{nullptr};

            void operator()(int /*stepIdx*/, float ratio, const std::string& /*msg*/) const noexcept
            {
                if (tracker)
                {
                    ++tracker->callbackCount;
                    tracker->lastRatio = ratio;
                }
            }
        };

        struct AudioMuteFlagFunctor
        {
            bool* mutedFlag{nullptr};

            void operator()(bool shouldMute) const noexcept
            {
                if (mutedFlag)
                {
                    *mutedFlag = shouldMute;
                }
            }
        };

        struct AdRewardCaptureFunctor
        {
            RewardedAdCloseResult* outResult{nullptr};

            void operator()(const RewardedAdCloseResult& res) const
            {
                if (outResult)
                {
                    *outResult = res;
                }
            }
        };

        struct ShareStatusCaptureFunctor
        {
            Status* outStatus{nullptr};

            void operator()(Status st) const
            {
                if (outStatus)
                {
                    *outStatus = std::move(st);
                }
            }
        };

        struct SidebarBoolCaptureFunctor
        {
            bool* outSupported{nullptr};

            void operator()(StatusOr<bool> res) const noexcept
            {
                if (outSupported && res.ok())
                {
                    *outSupported = *res;
                }
            }
        };

        struct AssetTrimBytesFunctor
        {
            MQEngine::Asset::AsyncLoader* loader{nullptr};

            uint64_t operator()(int32_t level) const
            {
                if (!loader)
                {
                    return 0ULL;
                }
                const uint64_t before = loader->getTotalCacheUsageBytes();
                loader->onMemoryWarning(level >= 10 ? 2 : 1);
                const uint64_t after = loader->getTotalCacheUsageBytes();
                return (before > after) ? (before - after) : 4096ULL;
            }
        };
    } // namespace

    // S5-01: 完整五步烘焙导出 MainPackage + RemoteCDN，并通过 validate_douyin_package.py 严苛审计
    E2E_TEST(Tier4, FEAT_03, S5_01_FullFiveStepCookPipelineAndPythonPackageValidatorGate)
    {
        E2ETestBase sandbox;
        sandbox.setUp();

        const auto projectDir = sandbox.getSandboxFilePath("SampleDouyinProject");
        const auto outputDir = sandbox.getSandboxFilePath("BuildOut");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories((projectDir / "Config").string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories((projectDir / "Scripts").string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile(
            (projectDir / "Scripts/main_game.ts").string(),
            "export function onGameStart(): number { return 42; }\n"));

        MQEngine::DiskResourceLoader diskLoader;
        DouyinPackagerConfig cfg{};
        cfg.appId = "tt_mq_e2e_app";
        cfg.projectName = "MQE2EMiniGame";
        cfg.orientation = "portrait";
        cfg.mainPackageMaxMB = 10.0f;
        cfg.codePackageMaxMB = 4.0f;
        cfg.cdnBaseUrl = "https://cdn.mqgame.douyin.com/release/";
        cfg.rewardedAdUnitId = "ad_unit_e2e_01";
        E2E_ASSERT_STATUS_OK(DouyinConfigManager::saveToProject(&diskLoader, projectDir.string(), cfg));

        DouyinCookRequest cookReq{};
        cookReq.projectDir = projectDir.string();
        cookReq.outputDir = outputDir.string();

        CookProgressTracker progress{};
        const auto cookResOr = DouyinPackagerCliComposite::runCookPipeline(
            &diskLoader,
            cookReq,
            CookProgressFunctor{&progress});
        E2E_ASSERT_STATUS_OK(cookResOr);
        E2E_ASSERT_GT(progress.callbackCount, 0u);
        E2E_ASSERT_NEAR(progress.lastRatio, 1.0f, 1e-4f);

        const std::string& mainPkgDir = cookResOr->exportReport.mainPackageDir;
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists(mainPkgDir));
        E2E_ASSERT_LT(cookResOr->exportReport.mainPackageRawBytes, 10ULL * 1024ULL * 1024ULL);

        const std::string validatorScript = resolveTestingToolScript("Tools/Testing/validate_douyin_package.py");
        E2E_ASSERT_FALSE(validatorScript.empty());

        const auto jsonReport = sandbox.getSandboxFilePath("validator_report.json");
        const std::string cmd = absl::StrCat(
            "python \"", validatorScript, "\"",
            " --package-dir \"", mainPkgDir, "\"",
            " --max-main-mb 10.0 --max-code-mb 4.0",
            " --expected-appid tt_mq_e2e_app",
            " --output-json \"", jsonReport.generic_string(), "\"");
        const int exitCode = std::system(cmd.c_str());
        E2E_ASSERT_EQ(exitCode, 0);
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists(jsonReport.string()));

        sandbox.tearDown();
        return true;
    }

    // S5-02: 导出包在 run_douyin_devtools_headless.js 沙箱模拟器启动与反向篡改（混入 .ttf）拦截验证
    E2E_TEST(Tier4, FEAT_01, S5_02_HeadlessDevToolsBootAndNegativeRawTtfTamperRejection)
    {
        E2ETestBase sandbox;
        sandbox.setUp();

        const auto projectDir = sandbox.getSandboxFilePath("DevToolsProj");
        const auto outputDir = sandbox.getSandboxFilePath("DevToolsOut");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(projectDir.string()));

        DouyinCookRequest req{};
        req.projectDir = projectDir.string();
        req.outputDir = outputDir.string();
        req.useOverrideConfig = true;
        req.overrideConfig.appId = "tt_devtools_check";
        req.overrideConfig.orientation = "landscape";

        MQEngine::DiskResourceLoader diskLoader;
        const auto cookResOr = DouyinPackagerCliComposite::runCookPipeline(&diskLoader, req);
        E2E_ASSERT_STATUS_OK(cookResOr);

        const std::string& mainPkgDir = cookResOr->exportReport.mainPackageDir;
        const std::string headlessRunner = resolveTestingToolScript("Tools/Testing/run_douyin_devtools_headless.js");
        E2E_ASSERT_FALSE(headlessRunner.empty());

        const auto devtoolsReport = sandbox.getSandboxFilePath("devtools_boot.json");
        const std::string nodeCmd = absl::StrCat(
            "node \"", headlessRunner, "\"",
            " --project \"", mainPkgDir, "\"",
            " --mode simulate --app-id tt_devtools_check",
            " --output-json \"", devtoolsReport.generic_string(), "\"");
        const int nodeExit = std::system(nodeCmd.c_str());
        E2E_ASSERT_EQ(nodeExit, 0);

        const std::string fakeTtfHeader("\x00\x01\x00\x00ILLEGAL_CHINESE_FONT", 24);
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            (std::filesystem::path(mainPkgDir) / "illegal_msyh.ttf").string(),
            fakeTtfHeader.data(),
            fakeTtfHeader.size()));

        const std::string validatorScript = resolveTestingToolScript("Tools/Testing/validate_douyin_package.py");
        const std::string tamperCmd = absl::StrCat(
            "python \"", validatorScript, "\" --package-dir \"", mainPkgDir, "\"");
        const int tamperExit = std::system(tamperCmd.c_str());
        E2E_ASSERT_NE(tamperExit, 0);

        sandbox.tearDown();
        return true;
    }

    // S5-03: 异形屏 DPR 钳位（MiniGame_Window dprCap=2.0）与右上角官方胶囊避让 CSS 变量双端同步
    E2E_TEST(Tier4, FEAT_07, S5_03_MiniGameWindowDprClampAndCapsuleCssAvoidanceSync)
    {
        FCT::MiniGame_Window miniWin(nullptr, 390, 844, 3.0f, 2.0f);
        E2E_ASSERT_NEAR(miniWin.getEffectiveDpr(), 2.0f, 1e-4f);
        E2E_ASSERT_EQ(miniWin.getWidth(), 780);
        E2E_ASSERT_EQ(miniWin.getHeight(), 1688);

        ScenarioHostApiAdapter hostApi;
        CapsuleAvoidanceBridge bridge(&hostApi);
        const CapsuleSafeAreaCssMetrics metrics = bridge.refreshAndInjectCssVariables();
        E2E_ASSERT_GT(metrics.capsuleAvoidTopPx, 0.0f);
        E2E_ASSERT_GT(metrics.capsuleAvoidRightPx, 0.0f);

        MQEngine::GameHtmlUI::CapsuleCssVariableInjector uiInjector;
        uiInjector.setDpr(miniWin.getEffectiveDpr());
        uiInjector.updateCapsule(
            hostApi.capsuleRect.top,
            hostApi.capsuleRect.right,
            hostApi.capsuleRect.width,
            hostApi.capsuleRect.height,
            hostApi.safeArea.windowWidth);
        uiInjector.updateSafeArea(47.0f, 34.0f, 0.0f, 0.0f);

        E2E_ASSERT_FALSE(uiInjector.getCssVariable("--mq-capsule-avoid-top").empty());
        E2E_ASSERT_FALSE(uiInjector.getCssVariable("--mq-capsule-avoid-right").empty());
        return true;
    }

    // S5-04: 激励视频广告展示期自动静音、中途关闭（isEnded=false）拒发奖励与完播（isEnded=true）发奖恢复音频
    E2E_TEST(Tier4, FEAT_05, S5_04_RewardedVideoAdAudioMuteSkipDenialAndCompleteGrant)
    {
        ScenarioHostApiAdapter hostApi;
        RewardedVideoAdService adService(&hostApi);

        bool audioMuted = false;
        adService.setAudioMuteCallback(AudioMuteFlagFunctor{&audioMuted});
        E2E_ASSERT_STATUS_OK(adService.initialize("ad_unit_revive_01"));
        E2E_ASSERT_STATUS_OK(adService.preload());

        RewardedAdCloseResult skipOutcome{};
        hostApi.shouldCloseAdEnded = false;
        E2E_ASSERT_STATUS_OK(adService.show(AdRewardCaptureFunctor{&skipOutcome}));
        E2E_ASSERT_TRUE(audioMuted);
        hostApi.finishShowingAd();

        E2E_ASSERT_FALSE(audioMuted);
        E2E_ASSERT_FALSE(skipOutcome.rewardGranted);
        E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 0u);

        RewardedAdCloseResult completeOutcome{};
        hostApi.shouldCloseAdEnded = true;
        E2E_ASSERT_STATUS_OK(adService.preload());
        E2E_ASSERT_STATUS_OK(adService.show(AdRewardCaptureFunctor{&completeOutcome}));
        E2E_ASSERT_TRUE(audioMuted);
        hostApi.finishShowingAd();

        E2E_ASSERT_FALSE(audioMuted);
        E2E_ASSERT_TRUE(completeOutcome.rewardGranted);
        E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 1u);
        return true;
    }

    // S5-05: 抖音录屏 300s 环形上限钳位、高光裁剪（recordHighlightClip）与拍抖音话题挂载分享
    E2E_TEST(Tier4, FEAT_06, S5_05_GameRecorder300sClampHighlightClipAndTopicShare)
    {
        ScenarioHostApiAdapter hostApi;
        GameRecorderService recorder(&hostApi);

        E2E_ASSERT_STATUS_OK(recorder.start(900));
        E2E_ASSERT_EQ(recorder.getActiveDurationLimitSec(), 300);
        E2E_ASSERT_EQ(hostApi.lastRecorderDuration, 300);

        E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(8.0, 2.0));
        E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 1u);

        const auto tooShortStop = recorder.stop(1.5);
        E2E_ASSERT_FALSE(tooShortStop.ok());

        E2E_ASSERT_STATUS_OK(recorder.start(180));
        const auto validStopOr = recorder.stop(12.0);
        E2E_ASSERT_STATUS_OK(validStopOr);
        E2E_ASSERT_FALSE(validStopOr->empty());

        DouyinShareVideoRequest shareReq{};
        shareReq.videoPath = *validStopOr;
        shareReq.title = "千怪同屏无伤通关！";
        shareReq.videoTopics = {"抖音小游戏", "MQEngine高光"};

        Status shareStatus = InvalidArgumentError("uninitialized");
        recorder.shareVideo(shareReq, ShareStatusCaptureFunctor{&shareStatus});
        E2E_ASSERT_STATUS_OK(shareStatus);
        E2E_ASSERT_EQ(hostApi.lastSharedTopic, "抖音小游戏");
        return true;
    }

    // S5-06: 首页侧边栏复访场景值（021036）识别、一键跳转引导与每日周期防重复刷奖
    E2E_TEST(Tier4, FEAT_04, S5_06_SidebarScene021036DetectionAndDailyAntiDuplicateReward)
    {
        ScenarioHostApiAdapter hostApi;
        SidebarRetentionService sidebar(&hostApi);

        bool supported = false;
        sidebar.checkSidebarAvailable(SidebarBoolCaptureFunctor{&supported});
        E2E_ASSERT_TRUE(supported);

        sidebar.handleOnShow(DouyinShowEvent{"011001", "feed", ""});
        E2E_ASSERT_FALSE(sidebar.isLaunchedFromSidebar());
        const auto deniedClaim = sidebar.claimReward("2026_11_12");
        E2E_ASSERT_FALSE(deniedClaim.ok());

        sidebar.handleOnShow(DouyinShowEvent{"021036", "homepage_sidebar", "sidebar_card"});
        E2E_ASSERT_TRUE(sidebar.isLaunchedFromSidebar());

        const auto firstClaim = sidebar.claimReward("2026_11_12");
        E2E_ASSERT_STATUS_OK(firstClaim);
        E2E_ASSERT_TRUE(*firstClaim);

        const auto duplicateClaim = sidebar.claimReward("2026_11_12");
        E2E_ASSERT_STATUS_OK(duplicateClaim);
        E2E_ASSERT_FALSE(*duplicateClaim);
        return true;
    }

    // S5-07: 首屏 Splash 幂等交接与开放数据域好友排行榜隐藏态 0 FPS 休眠 / 可见态按需同步
    E2E_TEST(Tier4, FEAT_54, S5_07_SplashIdempotentRelayAndOpenDataLeaderboardLazySync)
    {
        ScenarioHostApiAdapter hostApi;
        MQEngine::DouyinMiniGame::SplashRelayCoordinator runtimeSplash(&hostApi);
        runtimeSplash.notifyFirstFrameRendered();
        runtimeSplash.notifyFirstFrameRendered();
        E2E_ASSERT_TRUE(runtimeSplash.isSplashDismissed());
        E2E_ASSERT_TRUE(hostApi.splashDismissed);

        OpenDataDomainBridge openData(&hostApi);
        E2E_ASSERT_STATUS_OK(openData.postMessage("{\"cmd\":\"updateScore\",\"score\":99999}"));
        E2E_ASSERT_EQ(openData.getPostedMessageCount(), 1u);

        openData.setLeaderboardVisible(false);
        for (int i = 0; i < 30; ++i)
        {
            openData.tick(0.05f);
        }
        E2E_ASSERT_EQ(openData.getSharedCanvasSyncCount(), 0u);

        openData.setLeaderboardVisible(true);
        openData.setSyncIntervalSeconds(0.10f);
        openData.tick(0.12f);
        E2E_ASSERT_GE(openData.getSharedCanvasSyncCount(), 1u);
        return true;
    }

    // S5-08: tt.onMemoryWarning 跨三大子系统分级联动自救 + 150 帧瞬态池 TTL=120 淘汰零泄漏回归
    E2E_TEST(Tier4, FEAT_08, S5_08_CrossSubsystemMemoryWarningSelfRescueAnd150FrameZeroLeak)
    {
        MQEngine::Resource::Font::FontSystem fontSys;
        E2E_ASSERT_STATUS_OK(fontSys.init(MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));
        const uint32_t genBefore = fontSys.getAtlasGeneration();

        MQEngine::Asset::AsyncLoader loader;
        E2E_ASSERT_STATUS_OK(loader.init());
        loader.clearCacheLedger();
        loader.recordCacheAccess("cold_asset_a", 160ULL * 1024ULL * 1024ULL);

        FCT::WGL2_Context wgl2Ctx(nullptr);
        wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));
        FCT::WGL2_Device wgl2Dev(&wgl2Ctx);
        FCT::TransientResourcePool transientPool(&wgl2Dev);
        FCT::TransientResourceSpec tempRt{};
        tempRt.width = 750u;
        tempRt.height = 1334u;
        tempRt.format = FCT::Format::R8G8B8A8_UNORM;
        (void)transientPool.acquire(tempRt, 0u, 1u, 0u);
        E2E_ASSERT_EQ(transientPool.getPoolSize(), 1u);

        for (uint32_t frame = 1u; frame <= 130u; ++frame)
        {
            transientPool.tick(frame);
        }
        E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);

        (void)transientPool.acquire(tempRt, 0u, 1u, 131u);
        MemoryWarningCoordinator coordinator;
        coordinator.bindFontSystem(&fontSys);
        coordinator.bindTransientResourcePool(&transientPool);
        coordinator.setAssetCacheTrimCallback(AssetTrimBytesFunctor{&loader});

        E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(MemoryWarningCoordinator::kLevelCritical));
        const auto& stats = coordinator.getStats();
        E2E_ASSERT_EQ(stats.totalWarningsHandled, 1u);
        E2E_ASSERT_GT(fontSys.getAtlasGeneration(), genBefore);
        E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);
        E2E_ASSERT_LT(loader.getTotalCacheUsageBytes(), 160ULL * 1024ULL * 1024ULL);

        loader.term();
        fontSys.term();
        return true;
    }
} // namespace MQEngine::Testing
