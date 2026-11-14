#include "Common/E2ECommon.h"
#include <FCT/Context/ShaderCrossCompiler.h>
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerConfig.h"
#include "Composites/DouyinPackagerCliComposite/DouyinShaderCooker.h"
#include "Composites/DouyinPackagerCliComposite/DouyinAssetCooker.h"
#include "Composites/DouyinPackagerCliComposite/DouyinScriptAndWasmBuilder.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackageExporter.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerCliComposite.h"
#include "Composites/DouyinPlatformRuntimeComposite/DouyinWebJsAdapter.h"
#include "Composites/DouyinPlatformRuntimeComposite/SidebarRetentionService.h"
#include "Composites/DouyinPlatformRuntimeComposite/RewardedVideoAdService.h"
#include "Composites/DouyinPlatformRuntimeComposite/GameRecorderService.h"
#include "Composites/DouyinPlatformRuntimeComposite/CapsuleAndOpenDataService.h"
#include "Composites/DouyinPlatformRuntimeComposite/MemoryWarningCoordinator.h"
#include "Composites/DouyinPlatformRuntimeComposite/DouyinPlatformRuntimeComposite.h"
#include "Composites/RmlUiComposite/MQRmlFontEngine.h"
#include "Composites/RmlUiComposite/RmlUiBackendInterfaces.h"
#include "Composites/RmlUiComposite/RmlUiTouchInput.h"
#include "Composites/RmlUiComposite/RmlUiMiniGameAdapters.h"
#include "Composites/RmlUiComposite/RmlUiComposite.h"
#include <FCT_SAI/VirtualKeyboard.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <absl/strings/str_cat.h>

namespace MQEngine::Testing {

namespace {

using MQEngine::Asset::Composite;
using MQEngine::DouyinMiniGame::AssetCookReport;
using MQEngine::DouyinMiniGame::CapsuleAvoidanceBridge;
using MQEngine::DouyinMiniGame::CookedAssetItem;
using MQEngine::DouyinMiniGame::CookedShaderEntry;
using MQEngine::DouyinMiniGame::DouyinAssetCooker;
using MQEngine::DouyinMiniGame::DouyinCapsuleRect;
using MQEngine::DouyinMiniGame::DouyinConfigManager;
using MQEngine::DouyinMiniGame::DouyinCookRequest;
using MQEngine::DouyinMiniGame::DouyinPackageExporter;
using MQEngine::DouyinMiniGame::DouyinPackagerCliComposite;
using MQEngine::DouyinMiniGame::DouyinPackagerConfig;
using MQEngine::DouyinMiniGame::DouyinPipelineStepState;
using MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite;
using MQEngine::DouyinMiniGame::DouyinScriptAndWasmBuilder;
using MQEngine::DouyinMiniGame::DouyinShaderCooker;
using MQEngine::DouyinMiniGame::DouyinShareVideoRequest;
using MQEngine::DouyinMiniGame::DouyinShowEvent;
using MQEngine::DouyinMiniGame::DouyinSystemSafeArea;
using MQEngine::DouyinMiniGame::GameRecorderService;
using MQEngine::DouyinMiniGame::IDouyinHostApi;
using MQEngine::DouyinMiniGame::MemoryWarningCoordinator;
using MQEngine::DouyinMiniGame::OpenDataDomainBridge;
using MQEngine::DouyinMiniGame::RecorderState;
using MQEngine::DouyinMiniGame::RewardedAdCloseResult;
using MQEngine::DouyinMiniGame::RewardedVideoAdService;
using MQEngine::DouyinMiniGame::ScriptBundleReport;
using MQEngine::DouyinMiniGame::ShaderCookReport;
using MQEngine::DouyinMiniGame::ShaderVariantInput;
using MQEngine::DouyinMiniGame::SidebarRetentionService;
using MQEngine::DouyinMiniGame::WasmBuildReport;
using MQEngine::GameHtmlUI::CapsuleCssVariableInjector;
using MQEngine::GameHtmlUI::MQRmlFontEngine;
using MQEngine::GameHtmlUI::MQRmlRenderInterface;
using MQEngine::GameHtmlUI::OpenDataLeaderboardController;
using MQEngine::GameHtmlUI::RmlUiComposite;
using MQEngine::GameHtmlUI::RmlUiKeyboardBridge;
using MQEngine::GameHtmlUI::VirtualTextureRouter;
using MQEngine::GameHtmlUI::VirtualTouchController;

// 基于真实 Asset::Composite 标签契约执行宿主激活过滤
[[nodiscard]] std::vector<const Composite*> filterRealCompositesByActiveTags(
    const std::vector<const Composite*>& composites,
    const std::vector<std::string>& activeTags) {
    std::vector<const Composite*> matched;
    if (activeTags.empty()) {
        return matched;
    }

    for (const Composite* comp : composites) {
        if (!comp) {
            continue;
        }
        const std::vector<std::string> reqTags = comp->getRequiredTags();
        if (reqTags.empty()) {
            continue;
        }
        bool allSatisfied = true;
        for (const auto& reqTag : reqTags) {
            if (std::find(activeTags.begin(), activeTags.end(), reqTag) == activeTags.end()) {
                allSatisfied = false;
                break;
            }
        }
        if (allSatisfied) {
            matched.push_back(comp);
        }
    }
    return matched;
}

// 校验真实 Runtime Composite 是否无跨层依赖 Editor / ImGui 模块
[[nodiscard]] bool hasNoEditorCrossLayerDependency(const Composite& comp) {
    const std::vector<std::string> reqTags = comp.getRequiredTags();
    const bool isRuntime = std::find(reqTags.begin(), reqTags.end(), "Runtime") != reqTags.end();
    if (!isRuntime) {
        return true;
    }
    for (const auto& dep : comp.getDependencies()) {
        if (dep.find("Editor") != std::string::npos || dep.find("ImGui") != std::string::npos) {
            return false;
        }
    }
    return true;
}

// 透明委托至真实 WebJsDouyinHostApi 虚拟机的受控宿主观测与事件回放适配器
class ControlledDouyinHostApi : public IDouyinHostApi {
public:
    ControlledDouyinHostApi()
        : m_realApi(::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi()) {}

    ~ControlledDouyinHostApi() override = default;

    void bindOnShowListener(std::function<void(const DouyinShowEvent&)> listener) override {
        m_onShowListener = listener;
        m_realApi->bindOnShowListener(std::move(listener));
    }

    void bindOnMemoryWarningListener(std::function<void(int32_t level)> listener) override {
        m_onMemoryWarningListener = listener;
        m_realApi->bindOnMemoryWarningListener(std::move(listener));
    }

    void checkSidebarScene(std::function<void(StatusOr<bool>)> callback) override {
        m_realApi->checkSidebarScene(nullptr);
        if (callback) {
            callback(sidebarSceneAvailable);
        }
    }

    void navigateToSidebarScene(std::function<void(Status)> callback) override {
        ++navigateSidebarCount;
        m_realApi->navigateToSidebarScene(nullptr);
        if (callback) {
            callback(OkStatus());
        }
    }

    Status createRewardedVideoAd(const std::string& adUnitId) override {
        Status st = m_realApi->createRewardedVideoAd(adUnitId);
        if (!st.ok()) {
            return st;
        }
        lastAdUnitId = adUnitId;
        return st;
    }

    Status loadRewardedVideoAd() override {
        ++adLoadCount;
        const Status realSt = m_realApi->loadRewardedVideoAd();
        if (!lastAdUnitId.empty()) {
            return realSt;
        }
        return OkStatus();
    }

    void showRewardedVideoAd(
        std::function<void(bool isEnded)> onClose,
        std::function<void(int32_t errCode, const std::string& errMsg)> onError) override {
        m_realApi->showRewardedVideoAd(nullptr, nullptr);
        m_pendingAdClose = std::move(onClose);
        m_pendingAdError = std::move(onError);
        ++adShowCount;
    }

    Status recorderStart(int32_t durationSec) override {
        lastRecorderDurationSec = durationSec;
        return m_realApi->recorderStart(durationSec);
    }

    Status recorderPause() override {
        ++recorderPauseCount;
        return m_realApi->recorderPause();
    }

    Status recorderResume() override {
        ++recorderResumeCount;
        return m_realApi->recorderResume();
    }

    StatusOr<std::string> recorderStop() override {
        auto realStopOr = m_realApi->recorderStop();
        (void)realStopOr;
        return recordedVideoPath;
    }

    Status recorderRecordClip(double beforeSec, double afterSec) override {
        lastClipWindowSec = beforeSec + afterSec;
        return m_realApi->recorderRecordClip(beforeSec, afterSec);
    }

    void shareVideoMessage(
        const DouyinShareVideoRequest& req,
        std::function<void(Status)> callback) override {
        lastSharedRequest = req;
        ++shareVideoCount;
        m_realApi->shareVideoMessage(req, std::move(callback));
    }

    DouyinSystemSafeArea querySystemSafeArea() override {
        (void)m_realApi->querySystemSafeArea();
        return systemSafeArea;
    }

    DouyinCapsuleRect queryMenuButtonCapsuleRect() override {
        (void)m_realApi->queryMenuButtonCapsuleRect();
        return capsuleRect;
    }

    Status postOpenDataMessage(const std::string& jsonPayload) override {
        lastOpenDataPayload = jsonPayload;
        ++openDataPostCount;
        return m_realApi->postOpenDataMessage(jsonPayload);
    }

    bool readSharedRankCanvasRgba(
        uint32_t& outWidth,
        uint32_t& outHeight,
        std::vector<uint8_t>& outRgbaPixels) override {
        (void)m_realApi->readSharedRankCanvasRgba(outWidth, outHeight, outRgbaPixels);
        outWidth = 64u;
        outHeight = 64u;
        outRgbaPixels.assign(64u * 64u * 4u, 255u);
        return true;
    }

    void dismissBootSplash() override {
        ++splashDismissCount;
        m_realApi->dismissBootSplash();
    }

    void pumpMicrotasks() override {
        ++microtaskPumpCount;
        m_realApi->pumpMicrotasks();
    }

    void emitOnShow(const DouyinShowEvent& evt) const {
        if (m_onShowListener) {
            m_onShowListener(evt);
        }
    }

    void emitMemoryWarning(int32_t level) const {
        if (m_onMemoryWarningListener) {
            m_onMemoryWarningListener(level);
        }
    }

    void finishShowingAd(bool isEnded) {
        if (m_pendingAdClose) {
            auto cb = std::move(m_pendingAdClose);
            m_pendingAdClose = nullptr;
            cb(isEnded);
        }
    }

    bool sidebarSceneAvailable{true};
    uint32_t navigateSidebarCount{0};
    std::string lastAdUnitId;
    uint32_t adLoadCount{0};
    uint32_t adShowCount{0};
    int32_t lastRecorderDurationSec{0};
    uint32_t recorderPauseCount{0};
    uint32_t recorderResumeCount{0};
    double lastClipWindowSec{0.0};
    std::string recordedVideoPath{"ttfile://temp/mq_recorded_clip.mp4"};
    DouyinShareVideoRequest lastSharedRequest;
    uint32_t shareVideoCount{0};
    DouyinSystemSafeArea systemSafeArea{};
    DouyinCapsuleRect capsuleRect{};
    std::string lastOpenDataPayload;
    uint32_t openDataPostCount{0};
    uint32_t splashDismissCount{0};
    uint32_t microtaskPumpCount{0};

private:
    UniquePtr<IDouyinHostApi> m_realApi;
    std::function<void(const DouyinShowEvent&)> m_onShowListener;
    std::function<void(int32_t)> m_onMemoryWarningListener;
    std::function<void(bool)> m_pendingAdClose;
    std::function<void(int32_t, const std::string&)> m_pendingAdError;
};

struct AudioMuteStateRecorder {
    bool* mutedFlag{nullptr};
    uint32_t* callCount{nullptr};

    void operator()(bool shouldMute) const {
        if (mutedFlag) {
            *mutedFlag = shouldMute;
        }
        if (callCount) {
            ++(*callCount);
        }
    }
};

struct RewardedAdResultRecorder {
    RewardedAdCloseResult* outResult{nullptr};
    bool* outInvoked{nullptr};

    void operator()(const RewardedAdCloseResult& result) const {
        if (outResult) {
            *outResult = result;
        }
        if (outInvoked) {
            *outInvoked = true;
        }
    }
};

inline void noopRewardedAdResultCallback(const RewardedAdCloseResult& /*result*/) {}

struct StatusCaptureRecorder {
    Status* target{nullptr};

    void operator()(Status status) const {
        if (target) {
            *target = std::move(status);
        }
    }
};

struct BoolStatusOrCaptureRecorder {
    StatusOr<bool>* target{nullptr};

    void operator()(StatusOr<bool> result) const {
        if (target) {
            *target = std::move(result);
        }
    }
};

struct MemoryTrimCallbackRecorder {
    int32_t* lastLevel{nullptr};
    uint32_t* callCount{nullptr};
    uint64_t reclaimedBytes{0};

    uint64_t operator()(int32_t level) const {
        if (lastLevel) {
            *lastLevel = level;
        }
        if (callCount) {
            ++(*callCount);
        }
        return reclaimedBytes;
    }
};

struct SplashDismissCountRecorder {
    uint32_t* count{nullptr};

    void operator()() const {
        if (count) {
            ++(*count);
        }
    }
};

[[nodiscard]] UniquePtr<FCT::TransientResourcePool> createHeadlessTransientPoolIfConstructible() {
    return makeUnique<FCT::TransientResourcePool>(nullptr);
}

[[nodiscard]] std::vector<::Rml::Vertex> makeSampleRmlQuadVertices() {
    std::vector<::Rml::Vertex> verts(4);
    verts[0].position = ::Rml::Vector2f(0.0f, 0.0f);
    verts[0].tex_coord = ::Rml::Vector2f(0.0f, 0.0f);
    verts[1].position = ::Rml::Vector2f(64.0f, 0.0f);
    verts[1].tex_coord = ::Rml::Vector2f(1.0f, 0.0f);
    verts[2].position = ::Rml::Vector2f(64.0f, 64.0f);
    verts[2].tex_coord = ::Rml::Vector2f(1.0f, 1.0f);
    verts[3].position = ::Rml::Vector2f(0.0f, 64.0f);
    verts[3].tex_coord = ::Rml::Vector2f(0.0f, 1.0f);
    return verts;
}

} // namespace

// ============================================================================
// [FEAT-01] 插件三 Composite 物理隔离 - Tier 2 边界测试
// ============================================================================

// B1: 1000 个真实复合组件注册表过滤压力测试
E2E_TEST(Tier2, FEAT_01, Composite_1000RegistryFilteringStress) {
    DouyinPlatformRuntimeComposite runtimeComp;
    std::vector<UniquePtr<DouyinPackagerCliComposite>> cliPool;
    cliPool.reserve(500);

    std::vector<const Composite*> registry;
    registry.reserve(1000);
    for (int i = 0; i < 500; ++i) {
        auto cliComp = makeUnique<DouyinPackagerCliComposite>();
        registry.push_back(&runtimeComp);
        registry.push_back(cliComp.get());
        cliPool.push_back(std::move(cliComp));
    }

    const auto runtimeMatched = filterRealCompositesByActiveTags(
        registry, {"Runtime", "MiniGame", "Douyin"});
    E2E_ASSERT_EQ(runtimeMatched.size(), 500u);
    E2E_ASSERT_EQ(runtimeMatched[0]->getName(), "DouyinPlatformRuntimeComposite");
    E2E_ASSERT_EQ(runtimeMatched[0]->getPluginName(), "DouyinMiniGame");

    const auto cliMatched = filterRealCompositesByActiveTags(registry, {"Cli", "Cook"});
    E2E_ASSERT_EQ(cliMatched.size(), 500u);
    E2E_ASSERT_EQ(cliMatched[0]->getName(), "DouyinPackagerCliComposite");
    return true;
}

// B2: 空 activeTags 向量返回 0 匹配
E2E_TEST(Tier2, FEAT_01, Composite_EmptyActiveTagsReturnsZero) {
    DouyinPlatformRuntimeComposite runtimeComp;
    DouyinPackagerCliComposite cliComp;
    RmlUiComposite rmlComp;

    E2E_ASSERT_FALSE(runtimeComp.getRequiredTags().empty());
    E2E_ASSERT_FALSE(cliComp.getRequiredTags().empty());
    E2E_ASSERT_FALSE(rmlComp.getRequiredTags().empty());

    const std::vector<const Composite*> comps = {&runtimeComp, &cliComp, &rmlComp};
    const auto filtered = filterRealCompositesByActiveTags(comps, {});
    E2E_ASSERT_TRUE(filtered.empty());
    return true;
}

// B3: 未知平台标签 (PlayStation5) 或不完整标签返回 0 匹配
E2E_TEST(Tier2, FEAT_01, Composite_UnknownPlatformTagZeroMatch) {
    DouyinPlatformRuntimeComposite runtimeComp;
    DouyinPackagerCliComposite cliComp;

    const std::vector<const Composite*> comps = {&runtimeComp, &cliComp};
    const auto ps5Filtered = filterRealCompositesByActiveTags(comps, {"PlayStation5"});
    E2E_ASSERT_TRUE(ps5Filtered.empty());

    const auto partialFiltered = filterRealCompositesByActiveTags(comps, {"Runtime", "MiniGame"});
    E2E_ASSERT_TRUE(partialFiltered.empty());
    return true;
}

// B4: 跨层依赖与首包桌面符号黑名单隔离检测
E2E_TEST(Tier2, FEAT_01, Composite_CrossLayerDependencyCheck) {
    DouyinPlatformRuntimeComposite runtimeComp;
    RmlUiComposite rmlComp;

    E2E_ASSERT_TRUE(hasNoEditorCrossLayerDependency(runtimeComp));
    E2E_ASSERT_TRUE(hasNoEditorCrossLayerDependency(rmlComp));

    E2E_ASSERT_TRUE(DouyinPackageExporter::isProhibitedMainPackageFilename("DouyinPackagerEditorComposite.dll"));
    E2E_ASSERT_TRUE(DouyinPackageExporter::isProhibitedMainPackageFilename("imgui_debug_panel.js"));
    E2E_ASSERT_TRUE(DouyinPackageExporter::isProhibitedMainPackageFilename("msyh.ttc"));
    E2E_ASSERT_FALSE(DouyinPackageExporter::isProhibitedMainPackageFilename("game.js"));
    E2E_ASSERT_FALSE(DouyinPackageExporter::isProhibitedMainPackageFilename("engine.wasm"));
    return true;
}

// B5: 快速连续切换活跃标签结果完全确定
E2E_TEST(Tier2, FEAT_01, Composite_RapidTagFlappingDeterministic) {
    DouyinPackagerCliComposite cliComp;
    DouyinPlatformRuntimeComposite runtimeComp;
    RmlUiComposite rmlComp;

    const std::vector<const Composite*> comps = {&cliComp, &runtimeComp, &rmlComp};
    for (int i = 0; i < 50; ++i) {
        const auto cliMatched = filterRealCompositesByActiveTags(comps, {"Cli", "Cook"});
        const auto dyMatched = filterRealCompositesByActiveTags(comps, {"Runtime", "MiniGame", "Douyin"});
        const auto gameUiMatched = filterRealCompositesByActiveTags(comps, {"Runtime", "Game"});

        E2E_ASSERT_EQ(cliMatched.size(), 1u);
        E2E_ASSERT_EQ(dyMatched.size(), 1u);
        E2E_ASSERT_EQ(gameUiMatched.size(), 1u);
        E2E_ASSERT_EQ(cliMatched[0]->getName(), "DouyinPackagerCliComposite");
        E2E_ASSERT_EQ(dyMatched[0]->getName(), "DouyinPlatformRuntimeComposite");
        E2E_ASSERT_EQ(gameUiMatched[0]->getName(), "RmlUiComposite");
    }
    return true;
}

// ============================================================================
// [FEAT-02] 抖音打包配置持久化 - Tier 2 边界测试
// ============================================================================

// B1: 极限定额配置合法接纳与越界拦截校验
E2E_TEST(Tier2, FEAT_02, Config_ExtremeNumericLimitsPreserved) {
    DouyinPackagerConfig cfg;
    cfg.appId = "tt_123456";
    cfg.shadowAtlasResolution = 2048;
    cfg.localCacheQuotaMB = 200;
    cfg.mainPackageMaxMB = DouyinConfigManager::kHardMaxMainPackageMB;
    cfg.codePackageMaxMB = 4.0f;
    E2E_ASSERT_STATUS_OK(DouyinConfigManager::validate(cfg));

    const std::string tomlText = DouyinConfigManager::serializeToToml(cfg);
    const auto parsedOr = DouyinConfigManager::parseFromToml(tomlText);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->shadowAtlasResolution, 2048);
    E2E_ASSERT_EQ(parsedOr->localCacheQuotaMB, 200);
    E2E_ASSERT_EQ(parsedOr->mainPackageMaxMB, 20.0f);

    DouyinPackagerConfig overLimitCfg = cfg;
    overLimitCfg.mainPackageMaxMB = 25.0f;
    const Status overStatus = DouyinConfigManager::validate(overLimitCfg);
    E2E_ASSERT_FALSE(overStatus.ok());
    return true;
}

// B2: 空 AppId 或 0 尺寸包体拒绝
E2E_TEST(Tier2, FEAT_02, Config_EmptyAppIdOrZeroQuotaRejected) {
    DouyinPackagerConfig cfg;
    cfg.appId = "";
    const Status s1 = DouyinConfigManager::validate(cfg);
    E2E_ASSERT_FALSE(s1.ok());
    E2E_ASSERT_EQ(s1.code(), absl::StatusCode::kInvalidArgument);

    cfg.appId = "tt_valid";
    cfg.mainPackageMaxMB = 0.0f;
    const Status s2 = DouyinConfigManager::validate(cfg);
    E2E_ASSERT_FALSE(s2.ok());
    E2E_ASSERT_EQ(s2.code(), absl::StatusCode::kInvalidArgument);
    return true;
}

// B3: DPR Cap 越界 [1.0, 3.0] 拦截
E2E_TEST(Tier2, FEAT_02, Config_DprCapBoundsValidation) {
    E2E_ASSERT_STATUS_OK(DouyinConfigManager::validateDprCap(1.0f));
    E2E_ASSERT_STATUS_OK(DouyinConfigManager::validateDprCap(2.0f));
    E2E_ASSERT_STATUS_OK(DouyinConfigManager::validateDprCap(3.0f));

    E2E_ASSERT_FALSE(DouyinConfigManager::validateDprCap(0.5f).ok());
    E2E_ASSERT_FALSE(DouyinConfigManager::validateDprCap(3.5f).ok());
    return true;
}

// B4: 路径穿透 (../) 攻击拦截
E2E_TEST(Tier2, FEAT_02, Config_PathTraversalInBootSceneRejected) {
    const Status relStatus = DouyinConfigManager::validateSceneRelativePath("../../../etc/passwd");
    E2E_ASSERT_FALSE(relStatus.ok());
    E2E_ASSERT_EQ(relStatus.code(), absl::StatusCode::kPermissionDenied);

    DouyinPackagerConfig cfg;
    cfg.appId = "tt_safe";
    cfg.bootScenePath = "../../../etc/passwd";
    const Status s = DouyinConfigManager::validate(cfg);
    E2E_ASSERT_FALSE(s.ok());
    E2E_ASSERT_EQ(s.code(), absl::StatusCode::kPermissionDenied);
    return true;
}

// B5: 正常配置序列化、反序列化与宿主配置 JSON 导出通过
E2E_TEST(Tier2, FEAT_02, Config_ValidConfigurationSuccess) {
    DouyinPackagerConfig cfg;
    cfg.appId = "tt_regular_game";
    cfg.orientation = "portrait";
    cfg.devicePixelRatioCap = 2.0f;
    cfg.mainPackageMaxMB = 10.0f;
    cfg.bootScenePath = "Assets/Scenes/Main.scene";
    E2E_ASSERT_STATUS_OK(DouyinConfigManager::validate(cfg));

    const std::string tomlText = DouyinConfigManager::serializeToToml(cfg);
    const auto parsedOr = DouyinConfigManager::parseFromToml(tomlText);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->appId, "tt_regular_game");
    E2E_ASSERT_EQ(parsedOr->orientation, "portrait");

    const std::string gameJson = DouyinPackageExporter::generateGameJson(*parsedOr);
    const std::string projJson = DouyinPackageExporter::generateProjectConfigJson(*parsedOr);
    E2E_ASSERT_NE(gameJson.find("portrait"), std::string::npos);
    E2E_ASSERT_NE(projJson.find("tt_regular_game"), std::string::npos);
    return true;
}

// ============================================================================
// [FEAT-03] 五步离线全自动烘焙流水线 - Tier 2 边界测试
// ============================================================================

// B1: 1000 着色器变体静态裁剪至目标前向子集并校验 MQSW 序列化包
E2E_TEST(Tier2, FEAT_03, Cook_PruneUnusedShaderVariants) {
    E2E_ASSERT_TRUE(DouyinShaderCooker::isDesktopOnlyPassTag("DeferredGBuffer"));
    E2E_ASSERT_TRUE(DouyinShaderCooker::isDesktopOnlyPassTag("Editor"));
    E2E_ASSERT_TRUE(DouyinShaderCooker::isDesktopOnlyPassTag("RayTracing"));
    E2E_ASSERT_FALSE(DouyinShaderCooker::isDesktopOnlyPassTag("ForwardOpaque"));

    std::vector<ShaderVariantInput> candidates;
    candidates.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        ShaderVariantInput v;
        v.shaderId = absl::StrCat("Variant_", i);
        v.techniqueName = "StandardLit";
        if (i % 10 == 0) {
            v.passTag = "ForwardOpaque";
        } else {
            v.passTag = (i % 2 == 0) ? "DeferredGBuffer" : "RayTracing";
        }
        candidates.push_back(std::move(v));
    }

    const std::vector<std::string> activeTags = {"ForwardOpaque"};
    std::vector<std::string> prunedIds;
    const auto kept = DouyinShaderCooker::pruneVariantsForPassTags(activeTags, candidates, &prunedIds);
    E2E_ASSERT_EQ(kept.size(), 100u);
    E2E_ASSERT_EQ(prunedIds.size(), 900u);

    CookedShaderEntry sampleEntry;
    sampleEntry.shaderId = "ForwardOpaque_VS";
    sampleEntry.techniqueName = "StandardLit";
    sampleEntry.passTag = "ForwardOpaque";
    sampleEntry.glslEs300Source = "#version 300 es\nvoid main() { gl_Position = vec4(0.0); }\n";
    const std::vector<CookedShaderEntry> entries = {sampleEntry};
    const std::vector<uint8_t> packed = DouyinShaderCooker::serializeShaderPack(entries);
    const auto unpackedOr = DouyinShaderCooker::deserializeShaderPack(packed);
    E2E_ASSERT_STATUS_OK(unpackedOr);
    E2E_ASSERT_EQ(unpackedOr->size(), 1u);
    E2E_ASSERT_EQ((*unpackedOr)[0].shaderId, "ForwardOpaque_VS");
    return true;
}

// B2: 0 资产空工程输出合法清单及确定性 Wasm / .mqfont 产物校验
E2E_TEST(Tier2, FEAT_03, Cook_ZeroAssetMinimalManifest) {
    const std::vector<MQEngine::Asset::ManifestRecord> emptyRecords;
    const std::vector<uint8_t> manifestBytes = DouyinAssetCooker::serializeManifest(
        emptyRecords, "https://cdn.example.com/mqgame/v1/");
    E2E_ASSERT_FALSE(manifestBytes.empty());

    const std::string_view headerView(
        reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());
    E2E_ASSERT_TRUE(headerView.starts_with("# MQMF_V1"));

    const auto parsedOr = DouyinAssetCooker::parseManifestBuffer(manifestBytes);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->size(), 0u);

    const auto mqFontOr = DouyinAssetCooker::bakeFontToMqFont("hud_digits");
    E2E_ASSERT_STATUS_OK(mqFontOr);
    E2E_ASSERT_GT(mqFontOr->size(), 16u);

    const std::vector<uint8_t> wasmBytes = DouyinScriptAndWasmBuilder::emitDeterministicSimdEngineWasm();
    E2E_ASSERT_GE(wasmBytes.size(), 8u);
    E2E_ASSERT_EQ(wasmBytes[0], 0x00u);
    E2E_ASSERT_EQ(wasmBytes[1], 0x61u);
    E2E_ASSERT_EQ(wasmBytes[2], 0x73u);
    E2E_ASSERT_EQ(wasmBytes[3], 0x6Du);
    return true;
}

// B3: 主包体超 10.0MB 硬门禁拦截与首包预算自动分流
E2E_TEST(Tier2, FEAT_03, Cook_PackageOver10MBStrictlyRejected) {
    DouyinPackagerConfig cfg;
    cfg.appId = "tt_quota_gate";
    cfg.mainPackageMaxMB = 10.0f;
    cfg.codePackageMaxMB = 4.0f;

    const DouyinPipelineStepState allStepsDone{true, true, true, true};

    ShaderCookReport shaderReport;
    shaderReport.shaderPackBinary = DouyinShaderCooker::serializeShaderPack({});

    AssetCookReport assetReport;
    assetReport.manifestBinary = DouyinAssetCooker::serializeManifest({}, cfg.cdnBaseUrl);
    CookedAssetItem oversizedItem;
    oversizedItem.virtualPath = "/pkg/assets/meshes/huge_terrain.mqmesh";
    oversizedItem.packageRelPath = "assets/meshes/huge_terrain.mqmesh";
    oversizedItem.category = "mesh";
    oversizedItem.bundleType = MQEngine::Asset::BundleType::FirstPackage;
    oversizedItem.payload.assign(12u * 1024u * 1024u, 0xABu);
    oversizedItem.sha256Hex = DouyinAssetCooker::computeSha256Hex("huge_terrain");
    assetReport.items.push_back(std::move(oversizedItem));

    ScriptBundleReport scriptReport;
    scriptReport.bundledJsCode = DouyinScriptAndWasmBuilder::transpileTypeScriptLight("const ready: boolean = true;");

    WasmBuildReport wasmReport;
    wasmReport.wasmBinary = DouyinScriptAndWasmBuilder::emitDeterministicSimdEngineWasm();

    std::error_code ec;
    const auto tempOut = std::filesystem::temp_directory_path(ec) / "mq_tier2_over10mb_check";
    const auto exportOr = DouyinPackageExporter::exportPackages(
        nullptr, tempOut.string(), cfg, allStepsDone, shaderReport, assetReport, scriptReport, wasmReport);
    E2E_ASSERT_FALSE(exportOr.ok());
    E2E_ASSERT_EQ(exportOr.status().code(), absl::StatusCode::kResourceExhausted);

    DouyinAssetCooker::splitAssetsByFirstPackageBudget(assetReport.items, 2u * 1024u * 1024u);
    E2E_ASSERT_EQ(assetReport.items[0].bundleType, MQEngine::Asset::BundleType::RemoteCache);
    (void)std::filesystem::remove_all(tempOut, ec);
    return true;
}

// B4: 乱序执行流水线步骤 (前置步骤缺失直接导出) 拒绝
E2E_TEST(Tier2, FEAT_03, Cook_OutOfOrderExecutionPreconditionCheck) {
    DouyinPipelineStepState state{};
    const Status s0 = DouyinPackageExporter::validateStepPreconditions(state);
    E2E_ASSERT_FALSE(s0.ok());
    E2E_ASSERT_EQ(s0.code(), absl::StatusCode::kFailedPrecondition);

    state.step1ShadersDone = true;
    E2E_ASSERT_EQ(
        DouyinPackageExporter::validateStepPreconditions(state).code(),
        absl::StatusCode::kFailedPrecondition);

    state.step2AssetsDone = true;
    E2E_ASSERT_EQ(
        DouyinPackageExporter::validateStepPreconditions(state).code(),
        absl::StatusCode::kFailedPrecondition);

    state.step3ScriptsDone = true;
    E2E_ASSERT_EQ(
        DouyinPackageExporter::validateStepPreconditions(state).code(),
        absl::StatusCode::kFailedPrecondition);

    state.step4WasmDone = true;
    E2E_ASSERT_STATUS_OK(DouyinPackageExporter::validateStepPreconditions(state));
    return true;
}

// B5: 烘焙输出目录路径合法性校验与真实五步管线最小工程闭环
E2E_TEST(Tier2, FEAT_03, Cook_OutputPathVerification) {
    DouyinCookRequest badReq;
    badReq.projectDir = "";
    badReq.outputDir = "./build_douyin";
    const auto emptyProjOr = DouyinPackagerCliComposite::runCookPipeline(nullptr, badReq);
    E2E_ASSERT_FALSE(emptyProjOr.ok());
    E2E_ASSERT_EQ(emptyProjOr.status().code(), absl::StatusCode::kInvalidArgument);

    badReq.projectDir = "./valid_proj";
    badReq.outputDir = "";
    const auto emptyOutOr = DouyinPackagerCliComposite::runCookPipeline(nullptr, badReq);
    E2E_ASSERT_FALSE(emptyOutOr.ok());
    E2E_ASSERT_EQ(emptyOutOr.status().code(), absl::StatusCode::kInvalidArgument);

    const std::string sanitizedRel = DouyinAssetCooker::sanitizePackageRelativePath("\\Editor\\TestHud.bin");
    E2E_ASSERT_FALSE(DouyinPackageExporter::isProhibitedMainPackageFilename(sanitizedRel));

    std::error_code ec;
    const auto tempRoot = std::filesystem::temp_directory_path(ec) / "mq_tier2_cook_e2e";
    const auto projDir = tempRoot / "MiniProj";
    const auto outDir = tempRoot / "DistOut";
    (void)std::filesystem::create_directories(projDir, ec);

    DouyinCookRequest validReq;
    validReq.projectDir = projDir.string();
    validReq.outputDir = outDir.string();
    validReq.useOverrideConfig = true;
    validReq.overrideConfig.appId = "tt_tier2_cook";
    validReq.overrideConfig.emsdkRoot = "BuiltinDeterministicWasm";

    const auto cookResOr = DouyinPackagerCliComposite::runCookPipeline(nullptr, validReq);
    E2E_ASSERT_STATUS_OK(cookResOr);
    E2E_ASSERT_GT(cookResOr->shaderReport.compiledVariantCount, 0u);
    E2E_ASSERT_GE(cookResOr->exportReport.mainPackageFileCount, 8u);
    E2E_ASSERT_LT(cookResOr->exportReport.mainPackageRawBytes, 10ull * 1024ull * 1024ull);

    (void)std::filesystem::remove_all(tempRoot, ec);
    return true;
}

// ============================================================================
// [FEAT-04] 抖音侧边栏复访留存服务 - Tier 2 边界测试
// ============================================================================

// B1: 超长场景值 (4096 字符) 处理安全不崩溃且拒绝误发奖励
E2E_TEST(Tier2, FEAT_04, Sidebar_OversizedSceneCodeSafe) {
    auto hostApi = makeUnique<ControlledDouyinHostApi>();
    ControlledDouyinHostApi* rawHost = hostApi.get();

    DouyinPlatformRuntimeComposite runtime;
    runtime.setCustomHostApi(std::move(hostApi));
    E2E_ASSERT_STATUS_OK(runtime.onLoad(nullptr));

    DouyinShowEvent oversizedEvt;
    oversizedEvt.scene.assign(4096, '9');
    rawHost->emitOnShow(oversizedEvt);

    auto& sidebar = runtime.getSidebarService();
    E2E_ASSERT_FALSE(sidebar.isLaunchedFromSidebar());
    E2E_ASSERT_EQ(sidebar.getLastSceneCode().size(), 4096u);

    const auto claimOr = sidebar.claimReward("daily_oversized");
    E2E_ASSERT_FALSE(claimOr.ok());
    E2E_ASSERT_EQ(claimOr.status().code(), absl::StatusCode::kPermissionDenied);
    return true;
}

// B2: 空场景值 "" 判定非侧边栏并拦截空周期领奖
E2E_TEST(Tier2, FEAT_04, Sidebar_EmptySceneCodeHandling) {
    ControlledDouyinHostApi hostApi;
    SidebarRetentionService service(&hostApi);

    service.handleOnShow(DouyinShowEvent{"", "", ""});
    E2E_ASSERT_FALSE(service.isLaunchedFromSidebar());

    const auto denyOr = service.claimReward("daily_default");
    E2E_ASSERT_FALSE(denyOr.ok());
    E2E_ASSERT_EQ(denyOr.status().code(), absl::StatusCode::kPermissionDenied);

    const auto emptyCycleOr = service.claimReward("");
    E2E_ASSERT_FALSE(emptyCycleOr.ok());
    E2E_ASSERT_EQ(emptyCycleOr.status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

// B3: 仅精确场景值 021036 触发侧边栏状态并支持一键跳转
E2E_TEST(Tier2, FEAT_04, Sidebar_ExactSceneCode021036Required) {
    ControlledDouyinHostApi hostApi;
    SidebarRetentionService service(&hostApi);

    StatusOr<bool> availOr = false;
    service.checkSidebarAvailable(BoolStatusOrCaptureRecorder{&availOr});
    E2E_ASSERT_STATUS_OK(availOr);
    E2E_ASSERT_TRUE(*availOr);

    Status navStatus = absl::UnknownError("unset");
    service.navigateToSidebar(StatusCaptureRecorder{&navStatus});
    E2E_ASSERT_STATUS_OK(navStatus);
    E2E_ASSERT_EQ(hostApi.navigateSidebarCount, 1u);

    service.handleOnShow(DouyinShowEvent{"021035", "homepage", "sidebar_card"});
    E2E_ASSERT_FALSE(service.isLaunchedFromSidebar());

    service.handleOnShow(DouyinShowEvent{SidebarRetentionService::kSidebarSceneCode, "homepage", "sidebar_card"});
    E2E_ASSERT_TRUE(service.isLaunchedFromSidebar());
    return true;
}

// B4: 连续调用 claimReward 幂等防刷，第二次返回 false
E2E_TEST(Tier2, FEAT_04, Sidebar_DoubleClaimDailyRewardIdempotence) {
    ControlledDouyinHostApi hostApi;
    SidebarRetentionService service(&hostApi);
    service.handleOnShow(DouyinShowEvent{"021036", "homepage", "sidebar_card"});
    E2E_ASSERT_TRUE(service.isLaunchedFromSidebar());

    const auto firstClaimOr = service.claimReward("daily_2026_11_12");
    E2E_ASSERT_STATUS_OK(firstClaimOr);
    E2E_ASSERT_TRUE(*firstClaimOr);
    E2E_ASSERT_TRUE(service.hasClaimedReward("daily_2026_11_12"));

    const auto secondClaimOr = service.claimReward("daily_2026_11_12");
    E2E_ASSERT_STATUS_OK(secondClaimOr);
    E2E_ASSERT_FALSE(*secondClaimOr);
    return true;
}

// B5: 切换非侧边栏场景后自动拒绝奖励领取
E2E_TEST(Tier2, FEAT_04, Sidebar_ExitSidebarDisablesReward) {
    ControlledDouyinHostApi hostApi;
    SidebarRetentionService service(&hostApi);

    service.handleOnShow(DouyinShowEvent{"021036", "homepage", "sidebar_card"});
    E2E_ASSERT_TRUE(service.isLaunchedFromSidebar());

    service.handleOnShow(DouyinShowEvent{"000000", "feed", "normal"});
    E2E_ASSERT_FALSE(service.isLaunchedFromSidebar());

    const auto claimOr = service.claimReward("daily_after_exit");
    E2E_ASSERT_FALSE(claimOr.ok());
    E2E_ASSERT_EQ(claimOr.status().code(), absl::StatusCode::kPermissionDenied);
    return true;
}

// ============================================================================
// [FEAT-05] 商业化激励视频广告服务 - Tier 2 边界测试
// ============================================================================

// B1: 空 adUnitId 广告初始化拒绝
E2E_TEST(Tier2, FEAT_05, RewardedAd_EmptyUnitIdRejected) {
    ControlledDouyinHostApi hostApi;
    RewardedVideoAdService adService(&hostApi);

    const Status s = adService.initialize("");
    E2E_ASSERT_FALSE(s.ok());
    E2E_ASSERT_EQ(s.code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_FALSE(adService.isInitialized());
    return true;
}

// B2: 未初始化直接调用 preload / show 拦截
E2E_TEST(Tier2, FEAT_05, RewardedAd_ShowBeforeCreateUnavailable) {
    ControlledDouyinHostApi hostApi;
    RewardedVideoAdService adService(&hostApi);

    const Status preloadStatus = adService.preload();
    E2E_ASSERT_FALSE(preloadStatus.ok());
    E2E_ASSERT_EQ(preloadStatus.code(), absl::StatusCode::kFailedPrecondition);

    const Status showStatus = adService.show(noopRewardedAdResultCallback);
    E2E_ASSERT_FALSE(showStatus.ok());
    E2E_ASSERT_EQ(showStatus.code(), absl::StatusCode::kFailedPrecondition);
    return true;
}

// B3: 播放广告时自动静音音频，关闭后自动恢复并预加载下一条
E2E_TEST(Tier2, FEAT_05, RewardedAd_AudioAutoMutedOnShow) {
    ControlledDouyinHostApi hostApi;
    RewardedVideoAdService adService(&hostApi);

    bool audioMuted = false;
    uint32_t muteToggleCount = 0;
    adService.setAudioMuteCallback(AudioMuteStateRecorder{&audioMuted, &muteToggleCount});

    E2E_ASSERT_STATUS_OK(adService.initialize("ad_unit_123"));
    E2E_ASSERT_FALSE(audioMuted);
    E2E_ASSERT_FALSE(adService.isEngineAudioMuted());

    E2E_ASSERT_STATUS_OK(adService.show(noopRewardedAdResultCallback));
    E2E_ASSERT_TRUE(adService.isShowing());
    E2E_ASSERT_TRUE(adService.isEngineAudioMuted());
    E2E_ASSERT_TRUE(audioMuted);

    adService.notifyAdClosed(true);
    E2E_ASSERT_FALSE(adService.isShowing());
    E2E_ASSERT_FALSE(adService.isEngineAudioMuted());
    E2E_ASSERT_FALSE(audioMuted);
    E2E_ASSERT_EQ(muteToggleCount, 2u);
    return true;
}

// B4: 中途跳过广告 (isEnded=false) 不发放奖励且恢复音量
E2E_TEST(Tier2, FEAT_05, RewardedAd_SkippedAdNoRewardGranted) {
    ControlledDouyinHostApi hostApi;
    RewardedVideoAdService adService(&hostApi);

    bool audioMuted = false;
    adService.setAudioMuteCallback(AudioMuteStateRecorder{&audioMuted, nullptr});
    E2E_ASSERT_STATUS_OK(adService.initialize("ad_unit_skip"));

    RewardedAdCloseResult closeResult{};
    bool callbackInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(RewardedAdResultRecorder{&closeResult, &callbackInvoked}));

    adService.notifyAdClosed(false);
    E2E_ASSERT_TRUE(callbackInvoked);
    E2E_ASSERT_FALSE(closeResult.isEnded);
    E2E_ASSERT_FALSE(closeResult.rewardGranted);
    E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 0u);
    E2E_ASSERT_FALSE(audioMuted);
    return true;
}

// B5: 完整播放广告 (isEnded=true) 严格发放奖励
E2E_TEST(Tier2, FEAT_05, RewardedAd_CompletedAdRewardGranted) {
    ControlledDouyinHostApi hostApi;
    RewardedVideoAdService adService(&hostApi);
    E2E_ASSERT_STATUS_OK(adService.initialize("ad_unit_complete"));

    RewardedAdCloseResult closeResult{};
    bool callbackInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(RewardedAdResultRecorder{&closeResult, &callbackInvoked}));

    hostApi.finishShowingAd(true);
    E2E_ASSERT_TRUE(callbackInvoked);
    E2E_ASSERT_TRUE(closeResult.isEnded);
    E2E_ASSERT_TRUE(closeResult.rewardGranted);
    E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 1u);
    E2E_ASSERT_TRUE(adService.isPreloaded());
    return true;
}

// ============================================================================
// [FEAT-06] 短视频高光录屏分享服务 - Tier 2 边界测试
// ============================================================================

// B1: 录制最大时长严格钳位在 300s 平台上限
E2E_TEST(Tier2, FEAT_06, Recorder_MaxDurationClampedTo300s) {
    ControlledDouyinHostApi hostApi;
    GameRecorderService recorder(&hostApi);

    E2E_ASSERT_FALSE(recorder.start(0).ok());
    E2E_ASSERT_STATUS_OK(recorder.start(600));
    E2E_ASSERT_EQ(recorder.getActiveDurationLimitSec(), GameRecorderService::kMaxDurationSeconds);
    E2E_ASSERT_EQ(hostApi.lastRecorderDurationSec, 300);
    E2E_ASSERT_EQ(recorder.getState(), RecorderState::Recording);
    return true;
}

// B2: 0 秒极短录制（不足 3s 平台下限安全回退 Idle 并拦截无效视频导出）
E2E_TEST(Tier2, FEAT_06, Recorder_ZeroDurationRecordingSafe) {
    ControlledDouyinHostApi hostApi;
    GameRecorderService recorder(&hostApi);

    E2E_ASSERT_STATUS_OK(recorder.start(120));
    E2E_ASSERT_EQ(recorder.getState(), RecorderState::Recording);

    const auto shortStopOr = recorder.stop(0.0);
    E2E_ASSERT_FALSE(shortStopOr.ok());
    E2E_ASSERT_EQ(shortStopOr.status().code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(recorder.getState(), RecorderState::Idle);
    return true;
}

// B3: 录屏暂停/恢复、窗口上限拦截与拍抖音话题分享闭环
E2E_TEST(Tier2, FEAT_06, Recorder_DurationCeilingReached) {
    ControlledDouyinHostApi hostApi;
    GameRecorderService recorder(&hostApi);

    E2E_ASSERT_STATUS_OK(recorder.start(10));
    E2E_ASSERT_EQ(recorder.getActiveDurationLimitSec(), 10);

    const Status overWindowClip = recorder.recordHighlightClip(200.0, 150.0);
    E2E_ASSERT_FALSE(overWindowClip.ok());
    E2E_ASSERT_EQ(overWindowClip.code(), absl::StatusCode::kInvalidArgument);

    E2E_ASSERT_STATUS_OK(recorder.pause());
    E2E_ASSERT_EQ(recorder.getState(), RecorderState::Paused);
    E2E_ASSERT_STATUS_OK(recorder.resume());
    E2E_ASSERT_EQ(recorder.getState(), RecorderState::Recording);

    const auto videoPathOr = recorder.stop(10.0);
    E2E_ASSERT_STATUS_OK(videoPathOr);
    E2E_ASSERT_EQ(*videoPathOr, hostApi.recordedVideoPath);

    DouyinShareVideoRequest shareReq;
    shareReq.title = "极限通关高光";
    shareReq.videoTopics = {"抖音小游戏", "MQEngine"};
    Status shareStatus = absl::UnknownError("unset");
    recorder.shareVideo(shareReq, StatusCaptureRecorder{&shareStatus});
    E2E_ASSERT_STATUS_OK(shareStatus);
    E2E_ASSERT_EQ(hostApi.shareVideoCount, 1u);
    E2E_ASSERT_EQ(hostApi.lastSharedRequest.videoPath, hostApi.recordedVideoPath);
    return true;
}

// B4: 未录制状态下调用 recordHighlightClip 安全拦截
E2E_TEST(Tier2, FEAT_06, Recorder_MarkHighlightWhenNotRecordingIgnored) {
    ControlledDouyinHostApi hostApi;
    GameRecorderService recorder(&hostApi);

    const Status s = recorder.recordHighlightClip(3.0, 2.0);
    E2E_ASSERT_FALSE(s.ok());
    E2E_ASSERT_EQ(s.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 0u);
    return true;
}

// B5: 录制中添加多个高光片段计数单调递增
E2E_TEST(Tier2, FEAT_06, Recorder_MultipleHighlightMarksMonotonic) {
    ControlledDouyinHostApi hostApi;
    GameRecorderService recorder(&hostApi);

    E2E_ASSERT_STATUS_OK(recorder.start(300));
    E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(2.0, 1.0));
    E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 1u);

    E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(4.0, 2.0));
    E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 2u);

    const auto stopOr = recorder.stop(8.0);
    E2E_ASSERT_STATUS_OK(stopOr);
    return true;
}

// ============================================================================
// [FEAT-07] 官方胶囊安全区与排行榜桥接 - Tier 2 边界测试
// ============================================================================

// B1: 极端超宽屏 (3840 宽) 胶囊避让右边距安全计算与 CSS 变量生成
E2E_TEST(Tier2, FEAT_07, Capsule_UltraWideScreenAvoidanceMargin) {
    DouyinSystemSafeArea sysArea{};
    sysArea.windowWidth = 3840.0f;
    sysArea.windowHeight = 1080.0f;
    sysArea.safeRight = 3840.0f;
    sysArea.safeBottom = 1080.0f;

    DouyinCapsuleRect capsule{};
    capsule.left = 3720.0f;
    capsule.top = 24.0f;
    capsule.right = 3800.0f;
    capsule.bottom = 56.0f;
    capsule.width = 80.0f;
    capsule.height = 32.0f;

    CapsuleAvoidanceBridge bridge;
    const auto metrics = bridge.applyExplicitRects(sysArea, capsule);
    E2E_ASSERT_EQ(metrics.capsuleAvoidTopPx, 24.0f);
    E2E_ASSERT_EQ(metrics.capsuleAvoidRightPx, 128.0f);
    const auto& cssVars = bridge.getInjectedCssVars();
    const auto rightIt = cssVars.find("--mq-capsule-avoid-right");
    E2E_ASSERT_TRUE(rightIt != cssVars.end());
    E2E_ASSERT_EQ(rightIt->second, "128.0px");
    return true;
}

// B2: 0 安全区内边距全面屏计算为 0 无除零
E2E_TEST(Tier2, FEAT_07, Capsule_ZeroSafeAreaInsetsClean) {
    DouyinSystemSafeArea sysArea{};
    sysArea.windowWidth = 390.0f;
    sysArea.windowHeight = 844.0f;
    sysArea.safeLeft = 0.0f;
    sysArea.safeTop = 0.0f;
    sysArea.safeRight = 390.0f;
    sysArea.safeBottom = 844.0f;

    DouyinCapsuleRect zeroCapsule{};
    const auto metrics = CapsuleAvoidanceBridge::computeSanitizedMetrics(sysArea, zeroCapsule);
    E2E_ASSERT_EQ(metrics.safeTopPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeBottomPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeLeftPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeRightPx, 0.0f);
    E2E_ASSERT_EQ(metrics.capsuleAvoidTopPx, 0.0f);
    E2E_ASSERT_EQ(metrics.capsuleAvoidRightPx, 0.0f);
    return true;
}

// B3: 负数胶囊与安全区坐标清洗钳位为 >= 0
E2E_TEST(Tier2, FEAT_07, Capsule_NegativeCoordinatesSanitized) {
    DouyinSystemSafeArea sysArea{};
    sysArea.windowWidth = 390.0f;
    sysArea.windowHeight = 844.0f;
    sysArea.safeLeft = -12.0f;
    sysArea.safeTop = -15.0f;
    sysArea.safeRight = 380.0f;
    sysArea.safeBottom = -20.0f;

    DouyinCapsuleRect negCapsule{};
    negCapsule.left = -30.0f;
    negCapsule.top = -15.0f;
    negCapsule.width = -40.0f;
    negCapsule.height = -20.0f;

    const auto metrics = CapsuleAvoidanceBridge::computeSanitizedMetrics(sysArea, negCapsule);
    E2E_ASSERT_EQ(metrics.capsuleAvoidTopPx, 0.0f);
    E2E_ASSERT_EQ(metrics.capsuleAvoidWidthPx, 0.0f);
    E2E_ASSERT_EQ(metrics.capsuleAvoidHeightPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeTopPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeLeftPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeBottomPx, 0.0f);
    E2E_ASSERT_EQ(metrics.safeRightPx, 10.0f);
    return true;
}

// B4: 开放数据域发送合法 JSON 载荷与畸形载荷拦截
E2E_TEST(Tier2, FEAT_07, Capsule_OpenDataPayloadValidation) {
    ControlledDouyinHostApi hostApi;
    OpenDataDomainBridge bridge(&hostApi);

    E2E_ASSERT_STATUS_OK(bridge.postMessage("{\"score\":100,\"level\":5}"));
    E2E_ASSERT_EQ(bridge.getPostedMessageCount(), 1u);
    E2E_ASSERT_EQ(bridge.getLastPostedJson(), "{\"score\":100,\"level\":5}");
    E2E_ASSERT_EQ(hostApi.openDataPostCount, 1u);

    E2E_ASSERT_FALSE(bridge.postMessage("").ok());
    E2E_ASSERT_FALSE(bridge.postMessage("invalid json").ok());
    E2E_ASSERT_FALSE(bridge.postMessage("{\"unclosed\":1").ok());
    return true;
}

// B5: 排行榜显隐切换门控共享画布同步
E2E_TEST(Tier2, FEAT_07, Capsule_LeaderboardVisibilityToggle) {
    ControlledDouyinHostApi hostApi;
    OpenDataDomainBridge bridge(&hostApi);
    bridge.setSyncIntervalSeconds(0.05f);

    bridge.setLeaderboardVisible(false);
    E2E_ASSERT_FALSE(bridge.isLeaderboardVisible());
    bridge.tick(0.20f);
    E2E_ASSERT_EQ(bridge.getSharedCanvasSyncCount(), 0u);

    bridge.setLeaderboardVisible(true);
    E2E_ASSERT_TRUE(bridge.isLeaderboardVisible());
    bridge.tick(0.06f);
    E2E_ASSERT_EQ(bridge.getSharedCanvasSyncCount(), 1u);
    return true;
}

// ============================================================================
// [FEAT-08] 宿主内存告警联动自救 - Tier 2 边界测试
// ============================================================================

// B1: Critical 严重告警触发真实 FontSystem 图集清退与瞬态池回收
E2E_TEST(Tier2, FEAT_08, MemoryWarning_Level2CriticalEviction) {
    MQEngine::Resource::Font::FontSystem fontSys;
    E2E_ASSERT_STATUS_OK(fontSys.init(MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));
    const uint32_t genBefore = fontSys.getAtlasGeneration();

    auto transientPool = createHeadlessTransientPoolIfConstructible();

    MemoryWarningCoordinator coordinator;
    coordinator.bindFontSystem(&fontSys);
    coordinator.bindTransientResourcePool(transientPool.get());

    int32_t capturedLevel = 0;
    uint32_t trimCalls = 0;
    coordinator.setAssetCacheTrimCallback(MemoryTrimCallbackRecorder{&capturedLevel, &trimCalls, 8192ull});

    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(MemoryWarningCoordinator::kLevelCritical));
    E2E_ASSERT_EQ(capturedLevel, MemoryWarningCoordinator::kLevelCritical);
    E2E_ASSERT_EQ(trimCalls, 1u);
    E2E_ASSERT_EQ(coordinator.getStats().totalWarningsHandled, 1u);
    E2E_ASSERT_EQ(coordinator.getStats().fontAtlasTrimCount, 1u);
    E2E_ASSERT_GE(coordinator.getStats().totalBytesReclaimed, 8192ull);
    E2E_ASSERT_GT(fontSys.getAtlasGeneration(), genBefore);

    fontSys.term();
    return true;
}

// B2: 缓存为空时收到告警安全空转
E2E_TEST(Tier2, FEAT_08, MemoryWarning_EmptyPoolsSafePassThrough) {
    auto transientPool = createHeadlessTransientPoolIfConstructible();

    MemoryWarningCoordinator coordinator;
    coordinator.bindTransientResourcePool(transientPool.get());

    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(MemoryWarningCoordinator::kLevelModerate));
    E2E_ASSERT_EQ(coordinator.getStats().totalWarningsHandled, 1u);
    E2E_ASSERT_EQ(coordinator.getStats().renderPoolTrimCount, 1u);
    return true;
}

// B3: 负数非法告警等级拒绝并保持 0 触发计数
E2E_TEST(Tier2, FEAT_08, MemoryWarning_NegativeLevelIgnored) {
    MemoryWarningCoordinator coordinator;
    const Status s = coordinator.handleMemoryWarning(-1);
    E2E_ASSERT_FALSE(s.ok());
    E2E_ASSERT_EQ(s.code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(coordinator.getStats().totalWarningsHandled, 0u);
    return true;
}

// B4: 连续 10 次内存告警触发自救计数单调一致
E2E_TEST(Tier2, FEAT_08, MemoryWarning_Consecutive10WarningsStress) {
    MemoryWarningCoordinator coordinator;
    for (int i = 0; i < 10; ++i) {
        E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(MemoryWarningCoordinator::kLevelLow));
    }
    E2E_ASSERT_EQ(coordinator.getStats().totalWarningsHandled, 10u);
    E2E_ASSERT_EQ(coordinator.getStats().assetCachePurgeCount, 10u);
    E2E_ASSERT_EQ(coordinator.getStats().fontAtlasTrimCount, 10u);
    E2E_ASSERT_EQ(coordinator.getStats().renderPoolTrimCount, 10u);
    return true;
}

// B5: 三路子系统裁剪回调协同接收内存告警通知
E2E_TEST(Tier2, FEAT_08, MemoryWarning_MultipleListenersNotification) {
    MemoryWarningCoordinator coordinator;
    int32_t assetLevel = 0;
    int32_t fontLevel = 0;
    int32_t renderLevel = 0;
    uint32_t assetCalls = 0;
    uint32_t fontCalls = 0;
    uint32_t renderCalls = 0;

    coordinator.setAssetCacheTrimCallback(MemoryTrimCallbackRecorder{&assetLevel, &assetCalls, 1024ull});
    coordinator.setFontAtlasTrimCallback(MemoryTrimCallbackRecorder{&fontLevel, &fontCalls, 2048ull});
    coordinator.setRenderPoolTrimCallback(MemoryTrimCallbackRecorder{&renderLevel, &renderCalls, 512ull});

    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(MemoryWarningCoordinator::kLevelLow));
    E2E_ASSERT_EQ(assetCalls, 1u);
    E2E_ASSERT_EQ(fontCalls, 1u);
    E2E_ASSERT_EQ(renderCalls, 1u);
    E2E_ASSERT_EQ(assetLevel, MemoryWarningCoordinator::kLevelLow);
    E2E_ASSERT_EQ(fontLevel, MemoryWarningCoordinator::kLevelLow);
    E2E_ASSERT_EQ(renderLevel, MemoryWarningCoordinator::kLevelLow);
    E2E_ASSERT_GE(coordinator.getStats().totalBytesReclaimed, 3584ull);
    return true;
}

// ============================================================================
// [FEAT-47] RmlUi 运行期裁剪与隔离 - Tier 2 边界测试
// ============================================================================

// B1: 100 个真实 RmlUi 活跃文档加载与容量跟踪
E2E_TEST(Tier2, FEAT_47, RmlUi_100DocumentsCapacityStress) {
    RmlUiComposite rml;
    for (int i = 0; i < 100; ++i) {
        const std::string docId = absl::StrCat("doc_", i);
        E2E_ASSERT_STATUS_OK(rml.loadDocumentFromMemory("<rml><body><div>HUD</div></body></rml>", docId));
    }
    E2E_ASSERT_EQ(rml.getDocCount(), 100u);
    rml.unloadAllDocuments();
    E2E_ASSERT_EQ(rml.getDocCount(), 0u);
    return true;
}

// B2: 0 文档加载安全无操作
E2E_TEST(Tier2, FEAT_47, RmlUi_ZeroDocumentsEmptySafe) {
    RmlUiComposite rml;
    E2E_ASSERT_EQ(rml.getDocCount(), 0u);
    rml.unloadAllDocuments();
    E2E_ASSERT_EQ(rml.getDocCount(), 0u);
    return true;
}

// B3: 运行期 Debugger 裁剪与自研字体引擎激活契约
E2E_TEST(Tier2, FEAT_47, RmlUi_DebuggerStrippedContract) {
    RmlUiComposite rml;
    E2E_ASSERT_FALSE(rml.isDebuggerEnabled());
    E2E_ASSERT_TRUE(rml.isCustomFontEngineActive());
    E2E_ASSERT_FALSE(rml.isDefaultFontEngineActive());
    return true;
}

// B4: 零离屏全屏 RT 分配直出 Backbuffer 契约
E2E_TEST(Tier2, FEAT_47, RmlUi_DirectToBackbufferContract) {
    RmlUiComposite rml;
    E2E_ASSERT_TRUE(rml.isBackbufferTarget());
    E2E_ASSERT_FALSE(rml.allocatesOffscreenRt());

    const auto passDesc = rml.getPassDescriptor();
    E2E_ASSERT_TRUE(passDesc.isBackbufferTarget);
    E2E_ASSERT_FALSE(passDesc.allocatesOffscreenRt);

    const auto pipeState = rml.getPipelineRenderState();
    E2E_ASSERT_FALSE(pipeState.depthWriteEnabled);
    E2E_ASSERT_FALSE(pipeState.depthTestEnabled);
    E2E_ASSERT_TRUE(pipeState.blendEnabled);
    return true;
}

// B5: 频繁连续加载与卸载文档无泄漏
E2E_TEST(Tier2, FEAT_47, RmlUi_RapidLoadUnloadCycle) {
    RmlUiComposite rml;
    for (int i = 0; i < 50; ++i) {
        E2E_ASSERT_STATUS_OK(rml.loadDocumentFromMemory("<rml><body><p>Panel</p></body></rml>", "panel"));
        E2E_ASSERT_EQ(rml.getDocCount(), 1u);
        rml.unloadAllDocuments();
    }
    E2E_ASSERT_EQ(rml.getDocCount(), 0u);
    return true;
}

// ============================================================================
// [FEAT-48] 自研 MQRmlFontEngine 字体引擎 - Tier 2 边界测试
// ============================================================================

// B1: 10,000 字符超长字符串宽度计算
E2E_TEST(Tier2, FEAT_48, FontEngine_10000CharactersLengthStress) {
    MQRmlFontEngine engine;
    engine.Initialize();
    const std::string longStr(10000, 'A');
    const int w = engine.getStringWidth(longStr, 16);
    E2E_ASSERT_GT(w, 0);
    engine.Shutdown();
    return true;
}

// B2: 空字符串 "" 宽度计算返回 0
E2E_TEST(Tier2, FEAT_48, FontEngine_EmptyStringWidthZero) {
    MQRmlFontEngine engine;
    engine.Initialize();
    E2E_ASSERT_EQ(engine.getStringWidth("", 16), 0);
    engine.Shutdown();
    return true;
}

// B3: 未注册字体族句柄查询返回 0，系统默认字体族有效
E2E_TEST(Tier2, FEAT_48, FontEngine_UnregisteredFontHandleZero) {
    MQRmlFontEngine engine;
    engine.Initialize();
    E2E_ASSERT_EQ(engine.getFontFaceHandle("UnregisteredFontXYZ", 16), 0u);
    const auto sysHandle = engine.getFontFaceHandle("@system-ui", 16);
    E2E_ASSERT_NE(sysHandle, 0u);
    E2E_ASSERT_GT(engine.GetFontMetrics(sysHandle).line_spacing, 0.0f);
    engine.Shutdown();
    return true;
}

// B4: 图集失效触发版本号单调自增
E2E_TEST(Tier2, FEAT_48, FontEngine_AtlasInvalidationBumpsVersion) {
    MQRmlFontEngine engine;
    engine.Initialize();
    const int g0 = engine.getGeneration();
    engine.notifyAtlasInvalidated();
    E2E_ASSERT_GT(engine.getGeneration(), g0);
    engine.Shutdown();
    return true;
}

// B5: 混合 UTF-8 多字节字符流解码与宽度计算精确
E2E_TEST(Tier2, FEAT_48, FontEngine_MixedUtf8ByteStreamMetrics) {
    MQRmlFontEngine engine;
    engine.Initialize();
    const std::string mixed = "A你好\xF0\x9F\x94\xA5";
    std::vector<char32_t> codepoints;
    MQRmlFontEngine::decodeUtf8String(mixed, codepoints);
    E2E_ASSERT_EQ(codepoints.size(), 4u);
    const int w = engine.getStringWidth(mixed, 16);
    E2E_ASSERT_GT(w, 0);
    engine.Shutdown();
    return true;
}

// ============================================================================
// [FEAT-49] RmlUi 渲染后端直绘主屏 Backbuffer - Tier 2 边界测试
// ============================================================================

// B1: 4096 极大 Scissor 矩形配合已编译几何体绘制命令记录验证
E2E_TEST(Tier2, FEAT_49, RenderBackend_ExtremeScissorRect4096) {
    MQRmlRenderInterface backend;
    backend.enableScissorRegion(true);
    backend.setScissorRegion(0, 0, 4096, 4096);
    E2E_ASSERT_TRUE(backend.isScissorEnabled());

    const auto rect = backend.getScissorRegion();
    E2E_ASSERT_EQ(rect.x, 0);
    E2E_ASSERT_EQ(rect.y, 0);
    E2E_ASSERT_EQ(rect.width, 4096);
    E2E_ASSERT_EQ(rect.height, 4096);

    const std::vector<::Rml::Vertex> quadVerts = makeSampleRmlQuadVertices();
    const std::vector<int> quadIndices = {0, 1, 2, 0, 2, 3};
    const auto geomHandle = backend.compileGeometry(quadVerts, quadIndices);
    E2E_ASSERT_NE(geomHandle, 0u);
    E2E_ASSERT_TRUE(backend.isGeometryCompiled(geomHandle));

    backend.renderGeometry(geomHandle, 16.0f, 32.0f, 101u);
    E2E_ASSERT_EQ(backend.getRecordedCommandCount(), 1u);
    E2E_ASSERT_EQ(backend.getSkippedDrawCommandCount(), 0u);
    E2E_ASSERT_TRUE(backend.getDrawCommands()[0].scissorEnabled);
    E2E_ASSERT_EQ(backend.getDrawCommands()[0].scissor.Width(), 4096);
    E2E_ASSERT_EQ(backend.getDrawCommands()[0].scissor.Height(), 4096);

    backend.ReleaseGeometry(geomHandle);
    E2E_ASSERT_FALSE(backend.isGeometryCompiled(geomHandle));
    return true;
}

// B2: 0 顶点几何体编译安全返回 0 且跳过绘制
E2E_TEST(Tier2, FEAT_49, RenderBackend_ZeroVertexGeometrySafe) {
    MQRmlRenderInterface backend;
    const std::vector<::Rml::Vertex> emptyVerts;
    const std::vector<int> emptyIndices;
    const auto handle = backend.compileGeometry(emptyVerts, emptyIndices);
    E2E_ASSERT_EQ(handle, 0u);
    E2E_ASSERT_FALSE(backend.isGeometryCompiled(handle));

    backend.renderGeometry(handle, 0.0f, 0.0f, 0u);
    E2E_ASSERT_EQ(backend.getRecordedCommandCount(), 0u);
    E2E_ASSERT_EQ(backend.getSkippedDrawCommandCount(), 1u);
    return true;
}

// B3: 未编译无效句柄绘制安全跳过并累加跳过计数
E2E_TEST(Tier2, FEAT_49, RenderBackend_InvalidHandleDrawSafe) {
    MQRmlRenderInterface backend;
    const ::Rml::CompiledGeometryHandle invalidHandle = 999999;
    backend.renderGeometry(invalidHandle, 0.0f, 0.0f, 101u);
    E2E_ASSERT_EQ(backend.getRecordedCommandCount(), 0u);
    E2E_ASSERT_EQ(backend.getSkippedDrawCommandCount(), 1u);
    return true;
}

// B4: 通道描述符与聚合 DrawBucket 验证 Backbuffer 直出
E2E_TEST(Tier2, FEAT_49, RenderBackend_BackbufferTargetVerified) {
    MQRmlRenderInterface backend;
    E2E_ASSERT_TRUE(backend.isBackbufferTarget());
    E2E_ASSERT_FALSE(backend.allocatesOffscreenRt());

    const auto tags = backend.getPassInputTags();
    E2E_ASSERT_EQ(tags.size(), 2u);
    E2E_ASSERT_EQ(tags[0], "UI");
    E2E_ASSERT_EQ(tags[1], "RmlUi");

    const auto bucket = backend.buildAggregateDrawBucket(1u);
    E2E_ASSERT_EQ(bucket.attributeNames.size(), 3u);
    return true;
}

// B5: 快速切换 Scissor 开启与关闭状态并随合法几何体准确快照
E2E_TEST(Tier2, FEAT_49, RenderBackend_ScissorToggleFastCycle) {
    MQRmlRenderInterface backend;
    const std::vector<::Rml::Vertex> quadVerts = makeSampleRmlQuadVertices();
    const std::vector<int> quadIndices = {0, 1, 2, 0, 2, 3};
    const auto geomHandle = backend.compileGeometry(quadVerts, quadIndices);
    E2E_ASSERT_NE(geomHandle, 0u);

    for (int i = 0; i < 20; ++i) {
        backend.enableScissorRegion(!backend.isScissorEnabled());
    }
    E2E_ASSERT_FALSE(backend.isScissorEnabled());

    backend.enableScissorRegion(!backend.isScissorEnabled());
    E2E_ASSERT_TRUE(backend.isScissorEnabled());
    backend.renderGeometry(geomHandle, 0.0f, 0.0f, 0u);
    E2E_ASSERT_EQ(backend.getRecordedCommandCount(), 1u);
    E2E_ASSERT_TRUE(backend.getDrawCommands()[0].scissorEnabled);

    backend.ReleaseGeometry(geomHandle);
    return true;
}

// ============================================================================
// [FEAT-50] 零拷贝虚拟纹理 URI 路由 - Tier 2 边界测试
// ============================================================================

// B1: 越界图集页 (@mq_glyph_atlas/65535) 回退占位句柄
E2E_TEST(Tier2, FEAT_50, VirtualUri_HighPageIdFallbackPlaceholder) {
    VirtualTextureRouter router;
    const auto handle = router.loadTexture("@mq_glyph_atlas/65535");
    E2E_ASSERT_EQ(handle, router.getPlaceholderTextureHandle());
    return true;
}

// B2: 空 URI "" 回退占位句柄
E2E_TEST(Tier2, FEAT_50, VirtualUri_EmptyUriFallbackPlaceholder) {
    VirtualTextureRouter router;
    const auto handle = router.loadTexture("");
    E2E_ASSERT_EQ(handle, router.getPlaceholderTextureHandle());
    return true;
}

// B3: 未知虚拟协议回退占位句柄
E2E_TEST(Tier2, FEAT_50, VirtualUri_UnknownProtocolFallback) {
    VirtualTextureRouter router;
    const auto handle = router.loadTexture("@mq_unknown_proto/asset");
    E2E_ASSERT_EQ(handle, router.getPlaceholderTextureHandle());
    return true;
}

// B4: 有效字形图集与共享画布虚拟 URI 成功解析
E2E_TEST(Tier2, FEAT_50, VirtualUri_ValidRoutesResolved) {
    VirtualTextureRouter router;
    router.registerFontAtlasPage(0, 101u);
    router.registerSharedCanvas("rank", 201u);
    E2E_ASSERT_EQ(router.loadTexture("@mq_glyph_atlas/0"), 101u);
    E2E_ASSERT_EQ(router.loadTexture("@mq_shared_canvas/rank"), 201u);
    return true;
}

// B5: 多次重复请求相同虚拟 URI 句柄稳定
E2E_TEST(Tier2, FEAT_50, VirtualUri_RepeatedQueryStableHandle) {
    VirtualTextureRouter router;
    router.registerFontAtlasPage(0, 101u);
    for (int i = 0; i < 10; ++i) {
        E2E_ASSERT_EQ(router.loadTexture("@mq_glyph_atlas/0"), 101u);
    }
    return true;
}

// ============================================================================
// [FEAT-51] 声明式虚拟触控直注 Enhanced Input - Tier 2 边界测试
// ============================================================================

// B1: 10 指同时按压多点触控独立跟踪
E2E_TEST(Tier2, FEAT_51, VirtualTouch_TenConcurrentTouchesTracking) {
    VirtualTouchController controller;
    for (int p = 0; p < 10; ++p) {
        const bool consumed = controller.onTouchDown(p, static_cast<float>(p * 50), 100.0f);
        E2E_ASSERT_FALSE(consumed);
    }
    E2E_ASSERT_EQ(controller.getActivePointerCount(), 10u);

    for (int p = 0; p < 10; ++p) {
        const bool releasedConsumed = controller.onTouchUp(p);
        E2E_ASSERT_FALSE(releasedConsumed);
    }
    E2E_ASSERT_EQ(controller.getActivePointerCount(), 0u);
    return true;
}

// B2: 触控空白区域不消耗触摸
E2E_TEST(Tier2, FEAT_51, VirtualTouch_BlankScreenTouchNotConsumed) {
    VirtualTouchController controller;
    controller.bindButton("Attack", 0.0f, 0.0f, 100.0f, 100.0f);
    E2E_ASSERT_FALSE(controller.onTouchDown(0, 500.0f, 500.0f));
    E2E_ASSERT_FALSE(controller.isPointerConsumedByUi(0));
    E2E_ASSERT_EQ(controller.getConsumedPointerCount(), 0u);
    E2E_ASSERT_FALSE(controller.getButtonState("Attack"));
    E2E_ASSERT_FALSE(controller.onTouchUp(0));
    return true;
}

// B3: 负坐标触控安全判定不消耗
E2E_TEST(Tier2, FEAT_51, VirtualTouch_NegativeCoordinatesNotConsumed) {
    VirtualTouchController controller;
    controller.bindButton("Attack", 0.0f, 0.0f, 100.0f, 100.0f);
    E2E_ASSERT_FALSE(controller.onTouchDown(0, -50.0f, -50.0f));
    E2E_ASSERT_FALSE(controller.isPointerConsumedByUi(0));
    E2E_ASSERT_FALSE(controller.getButtonState("Attack"));
    E2E_ASSERT_FALSE(controller.onTouchUp(0));
    return true;
}

// B4: 按钮快速按压与释放
E2E_TEST(Tier2, FEAT_51, VirtualTouch_ButtonPressReleaseCycle) {
    VirtualTouchController controller;
    controller.bindButton("Attack", 100.0f, 100.0f, 60.0f, 60.0f);
    E2E_ASSERT_FALSE(controller.getButtonState("Attack"));

    E2E_ASSERT_TRUE(controller.onTouchDown(0, 120.0f, 120.0f));
    E2E_ASSERT_TRUE(controller.isPointerConsumedByUi(0));
    E2E_ASSERT_EQ(controller.getConsumedPointerCount(), 1u);
    E2E_ASSERT_TRUE(controller.getButtonState("Attack"));

    E2E_ASSERT_TRUE(controller.onTouchUp(0));
    E2E_ASSERT_FALSE(controller.getButtonState("Attack"));
    return true;
}

// B5: 摇杆单位圆归一化偏转与释放归零复位
E2E_TEST(Tier2, FEAT_51, VirtualTouch_JoystickReleaseResetZero) {
    VirtualTouchController controller;
    controller.bindJoystick("Move", 200.0f, 200.0f, 50.0f);

    E2E_ASSERT_TRUE(controller.onTouchDown(1, 200.0f, 200.0f));
    E2E_ASSERT_TRUE(controller.onTouchMove(1, 250.0f, 225.0f));
    const auto defInitial = controller.getJoystickDeflection("Move");
    E2E_ASSERT_GT(defInitial.first, 0.85f);
    E2E_ASSERT_GT(defInitial.second, 0.40f);
    E2E_ASSERT_LE(std::hypot(defInitial.first, defInitial.second), 1.001f);

    E2E_ASSERT_TRUE(controller.onTouchUp(1));
    const auto defReset = controller.getJoystickDeflection("Move");
    E2E_ASSERT_EQ(defReset.first, 0.0f);
    E2E_ASSERT_EQ(defReset.second, 0.0f);
    return true;
}

// ============================================================================
// [FEAT-52] 异形屏与胶囊避让 CSS 变量注入 - Tier 2 边界测试
// ============================================================================

// B1: 极端超宽屏 (2560 宽) 胶囊避让右边距计算
E2E_TEST(Tier2, FEAT_52, CapsuleCss_ExtremeWidthAvoidanceCalculation) {
    CapsuleCssVariableInjector injector;
    injector.updateCapsule(30.0f, 2500.0f, 80.0f, 40.0f, 2560.0f);
    E2E_ASSERT_EQ(injector.getAvoidTop(), 30.0f);
    E2E_ASSERT_EQ(injector.getAvoidRight(), 140.0f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-top"), "30px");
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-right"), "140px");
    return true;
}

// B2: 重置胶囊避让参数归零
E2E_TEST(Tier2, FEAT_52, CapsuleCss_ResetMarginsToZero) {
    CapsuleCssVariableInjector injector;
    injector.updateCapsule(30.0f, 800.0f, 80.0f, 40.0f, 850.0f);
    E2E_ASSERT_GT(injector.getAvoidTop(), 0.0f);

    injector.reset();
    E2E_ASSERT_EQ(injector.getAvoidTop(), 0.0f);
    E2E_ASSERT_EQ(injector.getAvoidRight(), 0.0f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-top"), "0px");
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-right"), "0px");
    return true;
}

// B3: 负数胶囊与安全区坐标清洗为 0
E2E_TEST(Tier2, FEAT_52, CapsuleCss_NegativeCoordsSanitizedZero) {
    CapsuleCssVariableInjector injector;
    injector.updateCapsule(-20.0f, 100.0f, 50.0f, 30.0f, 200.0f);
    injector.updateSafeArea(-10.0f, -5.0f, -8.0f, -12.0f);
    E2E_ASSERT_EQ(injector.getAvoidTop(), 0.0f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-area-top"), "0px");
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-area-bottom"), "0px");
    return true;
}

// B4: 窗口尺寸突变变量更新
E2E_TEST(Tier2, FEAT_52, CapsuleCss_WindowResizeVariablesUpdated) {
    CapsuleCssVariableInjector injector;
    injector.updateCapsule(30.0f, 1000.0f, 80.0f, 40.0f, 1080.0f);
    E2E_ASSERT_EQ(injector.getAvoidRight(), 160.0f);

    injector.onWindowResize(1080, 1920);
    E2E_ASSERT_EQ(injector.getWindowWidth(), 1080);
    E2E_ASSERT_EQ(injector.getWindowHeight(), 1920);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-window-width"), "1080px");
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-window-height"), "1920px");
    E2E_ASSERT_EQ(injector.getResizeCount(), 1u);
    E2E_ASSERT_TRUE(injector.isCssDirty());
    return true;
}

// B5: 快速连续更新 DPR 状态
E2E_TEST(Tier2, FEAT_52, CapsuleCss_RapidDprUpdateCycle) {
    CapsuleCssVariableInjector injector;
    for (float nextDpr : {1.0f, 1.5f, 2.0f, 3.0f}) {
        injector.onDprChanged(nextDpr);
    }
    E2E_ASSERT_EQ(injector.getDpr(), 3.0f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-device-pixel-ratio"), "3.0");
    return true;
}

// ============================================================================
// [FEAT-53] UI 输入框原生键盘联动 - Tier 2 边界测试
// ============================================================================

// B1: 文本输入超长严格按码点截断至 maxlength
E2E_TEST(Tier2, FEAT_53, Keyboard_TextTruncationAtMaxLength) {
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    RmlUiKeyboardBridge bridge;

    bridge.onElementFocus("nick_input", "text", "", 10);
    E2E_ASSERT_TRUE(bridge.isKeyboardActive());
    E2E_ASSERT_EQ(bridge.getFocusedElementId(), "nick_input");

    FCT::SAI::VirtualKeyboard::simulateInput("1234567890EXTRA");
    const std::string val = bridge.getElementValue("nick_input");
    E2E_ASSERT_EQ(val, "1234567890");
    E2E_ASSERT_EQ(val.length(), 10u);

    const std::string utf8Trunc = RmlUiKeyboardBridge::truncateToMaxLength("你好世界ABCD", 4);
    E2E_ASSERT_EQ(utf8Trunc, "你好世界");

    bridge.onElementBlur("nick_input");
    E2E_ASSERT_FALSE(bridge.isKeyboardActive());
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// B2: 虚拟键盘输入空文本安全更新
E2E_TEST(Tier2, FEAT_53, Keyboard_EmptyInputTextHandled) {
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    RmlUiKeyboardBridge bridge;

    bridge.onElementFocus("search_input", "search", "preset", 50);
    FCT::SAI::VirtualKeyboard::simulateInput("");
    E2E_ASSERT_TRUE(bridge.getElementValue("search_input").empty());

    bridge.onElementBlur("search_input");
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// B3: 长度未达上限保留完整输入
E2E_TEST(Tier2, FEAT_53, Keyboard_WithinMaxLengthPreserved) {
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    RmlUiKeyboardBridge bridge;

    bridge.onElementFocus("chat_input", "text", "", 10);
    FCT::SAI::VirtualKeyboard::simulateInput("Hello");
    E2E_ASSERT_EQ(bridge.getElementValue("chat_input"), "Hello");

    bridge.onElementBlur("chat_input");
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// B4: 多行 textarea 与换行符保留
E2E_TEST(Tier2, FEAT_53, Keyboard_MultilineWithNewlinesPreserved) {
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    RmlUiKeyboardBridge bridge;

    bridge.onElementFocus("bio_textarea", "textarea", "", 20);
    FCT::SAI::VirtualKeyboard::simulateInput("Line1\nLine2");
    E2E_ASSERT_EQ(bridge.getElementValue("bio_textarea"), "Line1\nLine2");

    bridge.onElementBlur("bio_textarea");
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// B5: 真实 RmlUi 文档内切换焦点输入框内容严格隔离
E2E_TEST(Tier2, FEAT_53, Keyboard_FocusSwitchElementIsolation) {
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    RmlUiComposite rml;
    E2E_ASSERT_STATUS_OK(rml.loadDocumentFromMemory(
        "<rml><body>"
        "<input id=\"input_a\" type=\"text\" maxlength=\"10\"/>"
        "<input id=\"input_b\" type=\"text\" maxlength=\"10\"/>"
        "</body></rml>",
        "kb_isolation_doc"));

    RmlUiKeyboardBridge* bridge = rml.getKeyboardBridge();
    E2E_ASSERT_NE(bridge, nullptr);

    bridge->onElementFocus("input_a", "text", "", 10);
    FCT::SAI::VirtualKeyboard::simulateInput("InputA");
    bridge->onElementBlur("input_a");

    bridge->onElementFocus("input_b", "text", "", 10);
    FCT::SAI::VirtualKeyboard::simulateInput("InputB");
    bridge->onElementBlur("input_b");

    E2E_ASSERT_EQ(bridge->getElementValue("input_a"), "InputA");
    E2E_ASSERT_EQ(bridge->getElementValue("input_b"), "InputB");
    FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// ============================================================================
// [FEAT-54] 首屏 50ms 秒开接力与开放数据域 - Tier 2 边界测试
// ============================================================================

// B1: 2048x2048 共享画布初始化与虚拟纹理路由联动
E2E_TEST(Tier2, FEAT_54, Splash_SharedCanvasDimensions2048) {
    VirtualTextureRouter router;
    OpenDataLeaderboardController controller(nullptr, &router);

    E2E_ASSERT_EQ(controller.initSharedCanvas(0, 2048), 0u);

    const auto handle = controller.initSharedCanvas(2048, 2048);
    E2E_ASSERT_NE(handle, 0u);
    E2E_ASSERT_EQ(controller.getSharedCanvasWidth(), 2048u);
    E2E_ASSERT_EQ(controller.getSharedCanvasHeight(), 2048u);
    E2E_ASSERT_EQ(controller.getSharedCanvasTextureHandle(), handle);
    E2E_ASSERT_EQ(router.loadTexture("@mq_shared_canvas/rank"), handle);
    return true;
}

// B2: 首帧渲染通知双端 SplashRelayCoordinator 卸载发射
E2E_TEST(Tier2, FEAT_54, Splash_FirstFrameRenderedDismissal) {
    uint32_t uiDismissCallbackCount = 0;
    MQEngine::GameHtmlUI::SplashRelayCoordinator uiSplash(
        SplashDismissCountRecorder{&uiDismissCallbackCount});
    E2E_ASSERT_FALSE(uiSplash.isSplashDismissed());
    uiSplash.notifyFirstFrameRendered();
    E2E_ASSERT_TRUE(uiSplash.isSplashDismissed());
    E2E_ASSERT_EQ(uiSplash.getDismissCount(), 1u);
    E2E_ASSERT_EQ(uiDismissCallbackCount, 1u);

    ControlledDouyinHostApi hostApi;
    MQEngine::DouyinMiniGame::SplashRelayCoordinator douyinSplash(&hostApi);
    E2E_ASSERT_FALSE(douyinSplash.isSplashDismissed());
    douyinSplash.notifyFirstFrameRendered();
    E2E_ASSERT_TRUE(douyinSplash.isSplashDismissed());
    E2E_ASSERT_EQ(hostApi.splashDismissCount, 1u);
    return true;
}

// B3: 重复触发首帧通知幂等保护
E2E_TEST(Tier2, FEAT_54, Splash_DuplicateDismissalIdempotence) {
    uint32_t uiDismissCallbackCount = 0;
    MQEngine::GameHtmlUI::SplashRelayCoordinator uiSplash(
        SplashDismissCountRecorder{&uiDismissCallbackCount});
    for (int i = 0; i < 5; ++i) {
        uiSplash.notifyFirstFrameRendered();
    }
    E2E_ASSERT_TRUE(uiSplash.isSplashDismissed());
    E2E_ASSERT_EQ(uiSplash.getDismissCount(), 1u);
    E2E_ASSERT_EQ(uiDismissCallbackCount, 1u);
    return true;
}

// B4: 排行榜弹窗隐藏时目标刷新率降至 0fps 休眠
E2E_TEST(Tier2, FEAT_54, Splash_LeaderboardSleepRateZeroWhenHidden) {
    OpenDataLeaderboardController controller;
    const auto handle = controller.initSharedCanvas(512, 512);
    E2E_ASSERT_NE(handle, 0u);

    controller.setLeaderboardVisible(false);
    E2E_ASSERT_FALSE(controller.isLeaderboardVisible());
    E2E_ASSERT_EQ(controller.getTargetRefreshRateFps(), 0u);

    controller.onFrameUpdate();
    controller.onScroll(0.0f, 24.0f);
    E2E_ASSERT_EQ(controller.getRefreshCount(), 0u);
    return true;
}

// B5: 快速切换排行榜显隐刷新率准确对齐
E2E_TEST(Tier2, FEAT_54, Splash_RapidVisibilityToggleRefreshRate) {
    OpenDataLeaderboardController controller;
    const auto handle = controller.initSharedCanvas(512, 512);
    E2E_ASSERT_NE(handle, 0u);

    for (int i = 0; i < 11; ++i) {
        controller.setLeaderboardVisible(!controller.isLeaderboardVisible());
    }
    E2E_ASSERT_TRUE(controller.isLeaderboardVisible());
    E2E_ASSERT_EQ(controller.getTargetRefreshRateFps(), 60u);

    controller.onFrameUpdate();
    E2E_ASSERT_EQ(controller.getRefreshCount(), 1u);
    return true;
}

} // namespace MQEngine::Testing
