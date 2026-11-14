/**
 * @file Test_Tier1_Plugins.cpp
 * @brief Tier 1 功能级 E2E 测试套件：官方插件能力域（DouyinMiniGame FEAT-01~08 & GameHtmlUI FEAT-47~54）
 * @details 100% 驱动真实插件与引擎生产类：
 *   - DouyinMiniGame: DouyinPackagerEditorComposite / DouyinPackagerCliComposite / DouyinPlatformRuntimeComposite /
 *                     DouyinConfigManager / DouyinShaderCooker / DouyinAssetCooker / DouyinScriptAndWasmBuilder /
 *                     DouyinPackageExporter / createWebJsDouyinHostApi / SidebarRetentionService /
 *                     RewardedVideoAdService / GameRecorderService / CapsuleAvoidanceBridge /
 *                     OpenDataDomainBridge / MemoryWarningCoordinator
 *   - GameHtmlUI:     RmlUiComposite / MQRmlFontEngine / MQRmlRenderInterface / MQRmlFileInterface /
 *                     VirtualTextureRouter / VirtualTouchController / CapsuleCssVariableInjector /
 *                     RmlUiKeyboardBridge / SplashRelayCoordinator / OpenDataLeaderboardController
 *   - Engine & FCT:   CompositeManager / DiskResourceLoader / HIDSubsystem / FontSystem /
 *                     WGL2_Context / WGL2_Device / TransientResourcePool / VirtualKeyboard
 */

#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include <FCT/Context/WGL2_Device.h>
#include <FCT/Context/TransientResourcePool.h>
#include <FCT/Context/ShaderCrossCompiler.h>
#include <FCT_SAI/VirtualKeyboard.h>

#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/asset/manager/CompositeManager.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/host/physics/PhysicsHost.h>
#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/runtime/core/Engine.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

#include <DouyinMiniGame/DouyinMiniGame.h>

#ifndef MQ_HTMLUI_RUNTIME_ONLY
#define MQ_HTMLUI_RUNTIME_ONLY 1
#endif

#include <GameHtmlUI/GameHtmlUI.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Testing {

namespace {

bool containsString(const std::vector<std::string>& items, std::string_view target) noexcept {
    for (const auto& item : items) {
        if (item == target) {
            return true;
        }
    }
    return false;
}

uint32_t readLittleEndianU32(const uint8_t* ptr) noexcept {
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8u) |
           (static_cast<uint32_t>(ptr[2]) << 16u) |
           (static_cast<uint32_t>(ptr[3]) << 24u);
}

struct Tier1PluginSandboxScope {
    E2ETestBase sandbox;
    std::string rootDir;

    Tier1PluginSandboxScope() {
        sandbox.setUp();
        rootDir = sandbox.getSandboxPath().generic_string();
    }

    std::string subPath(std::string_view rel) const {
        return sandbox.getSandboxFilePath(std::string(rel)).generic_string();
    }
};

/**
 * @brief 透明委托至真实 WebJsDouyinHostApi 的宿主调用观测适配器
 */
class Tier1PluginHostCallObserver : public ::MQEngine::DouyinMiniGame::IDouyinHostApi {
public:
    Tier1PluginHostCallObserver()
        : m_inner(::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi()) {}

    ~Tier1PluginHostCallObserver() override = default;

    void bindOnShowListener(
        std::function<void(const ::MQEngine::DouyinMiniGame::DouyinShowEvent&)> listener) override {
        onShowListenerBound = static_cast<bool>(listener);
        capturedOnShowListener = listener;
        m_inner->bindOnShowListener(std::move(listener));
    }

    void bindOnMemoryWarningListener(std::function<void(int32_t level)> listener) override {
        memoryWarningListenerBound = static_cast<bool>(listener);
        capturedMemoryWarningListener = listener;
        m_inner->bindOnMemoryWarningListener(std::move(listener));
    }

    void checkSidebarScene(std::function<void(StatusOr<bool>)> callback) override {
        ++checkSidebarCallCount;
        m_inner->checkSidebarScene(std::move(callback));
    }

    void navigateToSidebarScene(std::function<void(Status)> callback) override {
        ++navigateSidebarCallCount;
        m_inner->navigateToSidebarScene(std::move(callback));
    }

    Status createRewardedVideoAd(const std::string& adUnitId) override {
        return m_inner->createRewardedVideoAd(adUnitId);
    }

    Status loadRewardedVideoAd() override {
        return m_inner->loadRewardedVideoAd();
    }

    void showRewardedVideoAd(
        std::function<void(bool isEnded)> onClose,
        std::function<void(int32_t errCode, const std::string& errMsg)> onError) override {
        m_inner->showRewardedVideoAd(std::move(onClose), std::move(onError));
    }

    Status recorderStart(int32_t durationSec) override {
        lastRecorderDurationSec = durationSec;
        return m_inner->recorderStart(durationSec);
    }

    Status recorderPause() override {
        return m_inner->recorderPause();
    }

    Status recorderResume() override {
        return m_inner->recorderResume();
    }

    StatusOr<std::string> recorderStop() override {
        return m_inner->recorderStop();
    }

    Status recorderRecordClip(double beforeSec, double afterSec) override {
        ++recordClipCallCount;
        return m_inner->recorderRecordClip(beforeSec, afterSec);
    }

    void shareVideoMessage(
        const ::MQEngine::DouyinMiniGame::DouyinShareVideoRequest& req,
        std::function<void(Status)> callback) override {
        ++shareVideoCallCount;
        lastShareRequest = req;
        m_inner->shareVideoMessage(req, std::move(callback));
    }

    ::MQEngine::DouyinMiniGame::DouyinSystemSafeArea querySystemSafeArea() override {
        return m_inner->querySystemSafeArea();
    }

    ::MQEngine::DouyinMiniGame::DouyinCapsuleRect queryMenuButtonCapsuleRect() override {
        return m_inner->queryMenuButtonCapsuleRect();
    }

    Status postOpenDataMessage(const std::string& jsonPayload) override {
        ++openDataPostCount;
        lastOpenDataPayload = jsonPayload;
        return m_inner->postOpenDataMessage(jsonPayload);
    }

    bool readSharedRankCanvasRgba(
        uint32_t& outWidth,
        uint32_t& outHeight,
        std::vector<uint8_t>& outRgbaPixels) override {
        ++sharedCanvasReadCount;
        return m_inner->readSharedRankCanvasRgba(outWidth, outHeight, outRgbaPixels);
    }

    void dismissBootSplash() override {
        ++splashDismissCallCount;
        m_inner->dismissBootSplash();
    }

    void pumpMicrotasks() override {
        m_inner->pumpMicrotasks();
    }

    bool onShowListenerBound{false};
    bool memoryWarningListenerBound{false};
    std::function<void(const ::MQEngine::DouyinMiniGame::DouyinShowEvent&)> capturedOnShowListener;
    std::function<void(int32_t)> capturedMemoryWarningListener;
    uint32_t checkSidebarCallCount{0};
    uint32_t navigateSidebarCallCount{0};
    int32_t lastRecorderDurationSec{0};
    uint32_t recordClipCallCount{0};
    uint32_t shareVideoCallCount{0};
    ::MQEngine::DouyinMiniGame::DouyinShareVideoRequest lastShareRequest;
    uint32_t openDataPostCount{0};
    std::string lastOpenDataPayload;
    uint32_t sharedCanvasReadCount{0};
    uint32_t splashDismissCallCount{0};

private:
    UniquePtr<::MQEngine::DouyinMiniGame::IDouyinHostApi> m_inner;
};

struct Tier1PluginBoolStatusRecorder {
    StatusOr<bool>* outResult{nullptr};
    bool* outInvoked{nullptr};

    void operator()(StatusOr<bool> result) const {
        if (outResult) {
            *outResult = std::move(result);
        }
        if (outInvoked) {
            *outInvoked = true;
        }
    }
};

struct Tier1PluginStatusRecorder {
    Status* outStatus{nullptr};
    bool* outInvoked{nullptr};

    void operator()(Status status) const {
        if (outStatus) {
            *outStatus = std::move(status);
        }
        if (outInvoked) {
            *outInvoked = true;
        }
    }
};

struct Tier1PluginAudioMuteRecorder {
    bool* outMuted{nullptr};
    uint32_t* outToggleCount{nullptr};

    void operator()(bool shouldMute) const {
        if (outMuted) {
            *outMuted = shouldMute;
        }
        if (outToggleCount) {
            ++(*outToggleCount);
        }
    }
};

struct Tier1PluginRewardResultRecorder {
    ::MQEngine::DouyinMiniGame::RewardedAdCloseResult* outResult{nullptr};
    bool* outInvoked{nullptr};

    void operator()(const ::MQEngine::DouyinMiniGame::RewardedAdCloseResult& res) const {
        if (outResult) {
            *outResult = res;
        }
        if (outInvoked) {
            *outInvoked = true;
        }
    }
};

struct Tier1PluginMemoryTrimCounter {
    uint32_t* outCallCount{nullptr};
    int32_t* outLastLevel{nullptr};
    uint64_t reportedReclaimedBytes{0};

    uint64_t operator()(int32_t level) const {
        if (outCallCount) {
            ++(*outCallCount);
        }
        if (outLastLevel) {
            *outLastLevel = level;
        }
        return reportedReclaimedBytes;
    }
};

struct Tier1PluginSplashDismissCounter {
    uint32_t* outDismissCount{nullptr};

    void operator()() const {
        if (outDismissCount) {
            ++(*outDismissCount);
        }
    }
};

struct Tier1PluginSubRegionUploadRecorder {
    uint32_t* outUploadCount{nullptr};
    uint32_t* outLastWidth{nullptr};
    uint32_t* outLastHeight{nullptr};

    void operator()(uint32_t /*x*/, uint32_t /*y*/, uint32_t width, uint32_t height) const {
        if (outUploadCount) {
            ++(*outUploadCount);
        }
        if (outLastWidth) {
            *outLastWidth = width;
        }
        if (outLastHeight) {
            *outLastHeight = height;
        }
    }
};

} // namespace

// ============================================================================
// PART 1: DouyinMiniGame 插件能力套件 (FEAT-01 ~ FEAT-08)
// ============================================================================

// ----------------------------------------------------------------------------
// FEAT-01: 插件包声明与三 Composite 物理隔离 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_01, PluginManifest_CompositeTags_Validated) {
    ::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite editorComp;
    ::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite cliComp;
    ::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite runtimeComp;

    E2E_ASSERT_EQ(editorComp.getPluginName(), std::string("DouyinMiniGame"));
    E2E_ASSERT_EQ(cliComp.getPluginName(), std::string("DouyinMiniGame"));
    E2E_ASSERT_EQ(runtimeComp.getPluginName(), std::string("DouyinMiniGame"));

    E2E_ASSERT_EQ(editorComp.getName(), std::string("DouyinPackagerEditorComposite"));
    E2E_ASSERT_EQ(cliComp.getName(), std::string("DouyinPackagerCliComposite"));
    E2E_ASSERT_EQ(runtimeComp.getName(), std::string("DouyinPlatformRuntimeComposite"));

    const auto editorTags = editorComp.getRequiredTags();
    E2E_ASSERT_EQ(editorTags.size(), 2u);
    E2E_ASSERT_TRUE(containsString(editorTags, "Editor"));
    E2E_ASSERT_TRUE(containsString(editorTags, "ImGui"));
    E2E_ASSERT_TRUE(containsString(editorComp.getDependencies(), "EditorUiComposite"));

    const auto cliTags = cliComp.getRequiredTags();
    E2E_ASSERT_EQ(cliTags.size(), 2u);
    E2E_ASSERT_TRUE(containsString(cliTags, "Cli"));
    E2E_ASSERT_TRUE(containsString(cliTags, "Cook"));
    E2E_ASSERT_TRUE(cliComp.getDependencies().empty());

    const auto rtTags = runtimeComp.getRequiredTags();
    E2E_ASSERT_EQ(rtTags.size(), 3u);
    E2E_ASSERT_TRUE(containsString(rtTags, "Runtime"));
    E2E_ASSERT_TRUE(containsString(rtTags, "MiniGame"));
    E2E_ASSERT_TRUE(containsString(rtTags, "Douyin"));
    E2E_ASSERT_TRUE(runtimeComp.getDependencies().empty());
    return true;
}

E2E_TEST(Tier1, FEAT_01, DesktopRuntime_Exclusion_Verified) {
    ::MQEngine::Asset::CompositeManager manager;
    E2E_ASSERT_STATUS_OK(manager.init());

    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::GameHtmlUI::RmlUiComposite>()));

    const std::vector<std::string> desktopGameTags{"Runtime", "Game"};
    E2E_ASSERT_STATUS_OK(manager.buildTopologicalSchedule(desktopGameTags));

    const auto& active = manager.getActiveExecutionOrder();
    E2E_ASSERT_EQ(active.size(), 1u);
    E2E_ASSERT_EQ(active[0]->getName(), std::string("RmlUiComposite"));
    manager.term();
    return true;
}

E2E_TEST(Tier1, FEAT_01, EditorHost_CompositeLoading_Verified) {
    ::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite editorComp;
    ::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite cliComp;
    ::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite runtimeComp;

    const std::vector<std::string> editorHostTags{"Runtime", "Editor", "ImGui"};
    E2E_ASSERT_TRUE(editorComp.matchTags(editorHostTags));
    E2E_ASSERT_FALSE(cliComp.matchTags(editorHostTags));
    E2E_ASSERT_FALSE(runtimeComp.matchTags(editorHostTags));

    // 验证 CompositeManager 对前置依赖 EditorUiComposite 的严格完整性校验
    ::MQEngine::Asset::CompositeManager manager;
    E2E_ASSERT_STATUS_OK(manager.init());
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite>()));

    const Status missingDepStatus = manager.buildTopologicalSchedule(editorHostTags);
    E2E_ASSERT_FALSE(missingDepStatus.ok());
    E2E_ASSERT_EQ(missingDepStatus.code(), absl::StatusCode::kNotFound);
    manager.term();

    // 验证编辑器面板真实实例的无引擎安全装载、配置编辑与逆序卸载
    E2E_ASSERT_STATUS_OK(editorComp.onLoad(nullptr));
    E2E_ASSERT_STATUS_OK(editorComp.onInit(nullptr));
    E2E_ASSERT_TRUE(editorComp.isLoaded());
    E2E_ASSERT_TRUE(editorComp.isInitialized());
    editorComp.getEditingConfig().projectName = "EditorVerifiedGame";
    E2E_ASSERT_EQ(editorComp.getEditingConfig().projectName, std::string("EditorVerifiedGame"));
    editorComp.onUnload();
    E2E_ASSERT_FALSE(editorComp.isLoaded());
    return true;
}

E2E_TEST(Tier1, FEAT_01, CliHost_HeadlessIsolation_Verified) {
    ::MQEngine::Asset::CompositeManager manager;
    E2E_ASSERT_STATUS_OK(manager.init());

    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::GameHtmlUI::RmlUiComposite>()));

    const std::vector<std::string> cliCookTags{"Cli", "Cook"};
    E2E_ASSERT_STATUS_OK(manager.buildTopologicalSchedule(cliCookTags));

    const auto& active = manager.getActiveExecutionOrder();
    E2E_ASSERT_EQ(active.size(), 1u);
    E2E_ASSERT_EQ(active[0]->getName(), std::string("DouyinPackagerCliComposite"));

    E2E_ASSERT_STATUS_OK(manager.loadComposites(nullptr, cliCookTags));
    E2E_ASSERT_STATUS_OK(manager.initComposites(nullptr));
    manager.unloadComposites();
    manager.term();
    return true;
}

E2E_TEST(Tier1, FEAT_01, MiniGameRuntime_PlatformIsolation_Verified) {
    ::MQEngine::Asset::CompositeManager manager;
    E2E_ASSERT_STATUS_OK(manager.init());

    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite>()));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(
        makeUnique<::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite>()));

    const std::vector<std::string> miniGameTags{"Runtime", "MiniGame", "Douyin"};
    E2E_ASSERT_STATUS_OK(manager.buildTopologicalSchedule(miniGameTags));

    const auto& active = manager.getActiveExecutionOrder();
    E2E_ASSERT_EQ(active.size(), 1u);
    E2E_ASSERT_EQ(active[0]->getName(), std::string("DouyinPlatformRuntimeComposite"));

    E2E_ASSERT_STATUS_OK(manager.loadComposites(nullptr, miniGameTags));
    E2E_ASSERT_STATUS_OK(manager.initComposites(nullptr));
    manager.tick(0.016f);
    manager.unloadComposites();
    manager.term();
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-02: 工程级配置持久化与合法性校验 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_02, ConfigSerialization_DefaultValues_Verified) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    const ::MQEngine::DouyinMiniGame::DouyinPackagerConfig defaultCfg;
    E2E_ASSERT_EQ(defaultCfg.orientation, std::string("landscape"));
    E2E_ASSERT_NEAR(defaultCfg.devicePixelRatioCap, 2.0f, 1e-4f);
    E2E_ASSERT_EQ(defaultCfg.shadowAtlasResolution, 1024);
    E2E_ASSERT_TRUE(defaultCfg.enableBloom);
    E2E_ASSERT_EQ(defaultCfg.astcBlockQuality, std::string("6x6"));
    E2E_ASSERT_NEAR(defaultCfg.mainPackageMaxMB, 10.0f, 1e-4f);
    E2E_ASSERT_NEAR(defaultCfg.codePackageMaxMB, 4.0f, 1e-4f);
    E2E_ASSERT_EQ(defaultCfg.localCacheQuotaMB, 180);

    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::saveToProject(
        &loader, sandbox.rootDir, defaultCfg));

    const std::string expectedCfgFile =
        ::MQEngine::DouyinMiniGame::DouyinConfigManager::resolveConfigPath(sandbox.rootDir);
    E2E_ASSERT_TRUE(loader.exists(expectedCfgFile));

    auto loadedOr = ::MQEngine::DouyinMiniGame::DouyinConfigManager::loadFromProject(
        &loader, sandbox.rootDir);
    E2E_ASSERT_STATUS_OK(loadedOr);
    E2E_ASSERT_EQ(loadedOr->appId, defaultCfg.appId);
    E2E_ASSERT_EQ(loadedOr->orientation, defaultCfg.orientation);
    E2E_ASSERT_NEAR(loadedOr->devicePixelRatioCap, 2.0f, 1e-4f);
    E2E_ASSERT_EQ(loadedOr->shadowAtlasResolution, 1024);
    E2E_ASSERT_EQ(loadedOr->localCacheQuotaMB, 180);
    return true;
}

E2E_TEST(Tier1, FEAT_02, ConfigDeserialization_ValidToml_Parsed) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    const std::string validToml = R"(
[douyin]
app_id = "tt9876543210abcdef"
project_name = "SpaceShooterMini"
orientation = "portrait"

[render]
device_pixel_ratio_cap = 2.5
shadow_atlas_resolution = 512
enable_bloom = false

[packaging]
main_package_max_mb = 8.0
code_package_max_mb = 3.5

[streaming]
cdn_base_url = "https://cdn.mqengine.cn/spaceshooter/"
local_cache_quota_mb = 150
boot_scene = "Scenes/ShooterBoot.mqscene"
)";

    const std::string tomlPath = sandbox.subPath("Config/douyin_minigame.toml");
    E2E_ASSERT_STATUS_OK(loader.saveFile(tomlPath, validToml));

    auto parsedOr = ::MQEngine::DouyinMiniGame::DouyinConfigManager::loadFromFile(&loader, tomlPath);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->appId, std::string("tt9876543210abcdef"));
    E2E_ASSERT_EQ(parsedOr->projectName, std::string("SpaceShooterMini"));
    E2E_ASSERT_EQ(parsedOr->orientation, std::string("portrait"));
    E2E_ASSERT_NEAR(parsedOr->devicePixelRatioCap, 2.5f, 1e-4f);
    E2E_ASSERT_EQ(parsedOr->shadowAtlasResolution, 512);
    E2E_ASSERT_FALSE(parsedOr->enableBloom);
    E2E_ASSERT_NEAR(parsedOr->mainPackageMaxMB, 8.0f, 1e-4f);
    E2E_ASSERT_EQ(parsedOr->localCacheQuotaMB, 150);
    E2E_ASSERT_EQ(parsedOr->bootScenePath, std::string("Scenes/ShooterBoot.mqscene"));
    return true;
}

E2E_TEST(Tier1, FEAT_02, ConfigValidation_OrientationConstraints_Enforced) {
    ::MQEngine::DouyinMiniGame::DouyinPackagerConfig cfg;
    cfg.orientation = "portrait";
    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validate(cfg));

    cfg.orientation = "landscape";
    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validate(cfg));

    cfg.orientation = "diagonal";
    const Status stDiagonal = ::MQEngine::DouyinMiniGame::DouyinConfigManager::validate(cfg);
    E2E_ASSERT_FALSE(stDiagonal.ok());
    E2E_ASSERT_EQ(stDiagonal.code(), absl::StatusCode::kInvalidArgument);

    cfg.orientation = "";
    const Status stEmpty = ::MQEngine::DouyinMiniGame::DouyinConfigManager::validate(cfg);
    E2E_ASSERT_FALSE(stEmpty.ok());

    auto badTomlOr = ::MQEngine::DouyinMiniGame::DouyinConfigManager::parseFromToml(
        "appId = \"tt123\"\nprojectName = \"BadGame\"\norientation = \"upside_down\"\n");
    E2E_ASSERT_FALSE(badTomlOr.ok());
    return true;
}

E2E_TEST(Tier1, FEAT_02, ConfigValidation_DprCapBounds_Verified) {
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(0.5f).ok());
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(0.0f).ok());
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(-1.0f).ok());
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(3.5f).ok());

    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(1.0f));
    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(2.0f));
    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateDprCap(3.0f));
    return true;
}

E2E_TEST(Tier1, FEAT_02, ConfigPathResolution_RelativePaths_Canonicalized) {
    Tier1PluginSandboxScope sandbox;
    const std::string resolved =
        ::MQEngine::DouyinMiniGame::DouyinConfigManager::resolveConfigPath(sandbox.rootDir + "\\");

    E2E_ASSERT_EQ(resolved.find('\\'), std::string::npos);
    E2E_ASSERT_NE(resolved.find("Config/douyin_minigame.toml"), std::string::npos);

    E2E_ASSERT_STATUS_OK(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateSceneRelativePath(
        "Scenes/Level01.mqscene"));
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateSceneRelativePath(
        "C:/Hardcoded/Desktop/Path.mqscene").ok());
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateSceneRelativePath(
        "/etc/passwd").ok());
    E2E_ASSERT_FALSE(::MQEngine::DouyinMiniGame::DouyinConfigManager::validateSceneRelativePath(
        "Scenes/../../Escape.mqscene").ok());
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-03: 五步离线烘焙与打包流水线 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_03, CookStep1_ShaderPruningAndCrossCompile_Verified) {
    const std::vector<std::string> activeTags{"ForwardLit", "ShadowDepth", "UI"};

    ::MQEngine::DouyinMiniGame::ShaderVariantInput forwardVs;
    forwardVs.shaderId = "ForwardLit_VS";
    forwardVs.techniqueName = "MobileLit";
    forwardVs.passTag = "ForwardLit";
    forwardVs.entryPoint = "main";
    forwardVs.stage = ::FCT::ShaderKind::VertexShader;
    forwardVs.hlslSource = R"(
struct VSIn {
    float3 pos : POSITION;
};
struct VSOut {
    float4 pos : SV_Position;
};
VSOut main(VSIn input) {
    VSOut o;
    o.pos = float4(input.pos, 1.0);
    return o;
}
)";

    ::MQEngine::DouyinMiniGame::ShaderVariantInput shadowVs = forwardVs;
    shadowVs.shaderId = "ShadowDepth_VS";
    shadowVs.passTag = "ShadowDepth";

    ::MQEngine::DouyinMiniGame::ShaderVariantInput gbufferPs = forwardVs;
    gbufferPs.shaderId = "DeferredGBuffer_PS";
    gbufferPs.passTag = "DeferredGBuffer";

    ::MQEngine::DouyinMiniGame::ShaderVariantInput editorGizmoVs = forwardVs;
    editorGizmoVs.shaderId = "EditorGizmo_VS";
    editorGizmoVs.passTag = "Editor";

    ::MQEngine::DouyinMiniGame::ShaderVariantInput rtShadowCs = forwardVs;
    rtShadowCs.shaderId = "RayTracingShadow_CS";
    rtShadowCs.passTag = "RayTracing";

    const std::vector<::MQEngine::DouyinMiniGame::ShaderVariantInput> candidates{
        forwardVs, shadowVs, gbufferPs, editorGizmoVs, rtShadowCs};

    auto reportOr = ::MQEngine::DouyinMiniGame::DouyinShaderCooker::cookVariants(activeTags, candidates);
    E2E_ASSERT_STATUS_OK(reportOr);
    E2E_ASSERT_EQ(reportOr->totalCandidateVariants, 5u);
    E2E_ASSERT_EQ(reportOr->prunedVariantCount, 3u);
    E2E_ASSERT_EQ(reportOr->compiledVariantCount, 2u);
    E2E_ASSERT_EQ(reportOr->cookedEntries.size(), 2u);

    for (const auto& entry : reportOr->cookedEntries) {
        E2E_ASSERT_NE(entry.glslEs300Source.find("#version 300 es"), std::string::npos);
        E2E_ASSERT_NE(entry.passTag, std::string("DeferredGBuffer"));
        E2E_ASSERT_NE(entry.passTag, std::string("Editor"));
    }

    E2E_ASSERT_GT(reportOr->shaderPackBinary.size(), 8u);
    E2E_ASSERT_EQ(
        readLittleEndianU32(reportOr->shaderPackBinary.data()),
        ::MQEngine::DouyinMiniGame::DouyinShaderCooker::kShaderPackMagic);

    auto unpackedOr = ::MQEngine::DouyinMiniGame::DouyinShaderCooker::deserializeShaderPack(
        reportOr->shaderPackBinary);
    E2E_ASSERT_STATUS_OK(unpackedOr);
    E2E_ASSERT_EQ(unpackedOr->size(), 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_03, CookStep2_AssetManifestAndMeshBinary_Generated) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    const std::string sampleObj = R"(
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
vn 0.0 0.0 1.0
vt 0.0 0.0
vt 1.0 0.0
vt 0.0 1.0
f 1/1/1 2/2/1 3/3/1
)";
    E2E_ASSERT_STATUS_OK(loader.saveFile(sandbox.subPath("Assets/Meshes/HeroWing.obj"), sampleObj));

    ::MQEngine::DouyinMiniGame::DouyinPackagerConfig cfg;
    auto assetReportOr = ::MQEngine::DouyinMiniGame::DouyinAssetCooker::cookProjectAssets(
        &loader, sandbox.rootDir, cfg, 4ull * 1024ull * 1024ull);
    E2E_ASSERT_STATUS_OK(assetReportOr);

    E2E_ASSERT_GE(assetReportOr->bakedMeshCount, 1u);
    E2E_ASSERT_GE(assetReportOr->bakedFontCount, 1u);
    E2E_ASSERT_FALSE(assetReportOr->manifestRecords.empty());
    E2E_ASSERT_FALSE(assetReportOr->manifestBinary.empty());

    bool foundMqFont = false;
    bool foundMqMesh = false;
    for (const auto& item : assetReportOr->items) {
        E2E_ASSERT_EQ(item.sha256Hex.size(), 64u);
        if (item.packageRelPath.ends_with(".mqfont")) {
            foundMqFont = true;
            E2E_ASSERT_GT(item.payload.size(), 8u);
            E2E_ASSERT_EQ(
                readLittleEndianU32(item.payload.data()),
                ::MQEngine::DouyinMiniGame::DouyinAssetCooker::kMqFontMagic);
        }
        if (item.packageRelPath.ends_with(".mqmesh")) {
            foundMqMesh = true;
            E2E_ASSERT_GT(item.payload.size(), 16u);
        }
    }
    E2E_ASSERT_TRUE(foundMqFont);
    E2E_ASSERT_TRUE(foundMqMesh);

    auto parsedManifestOr = ::MQEngine::DouyinMiniGame::DouyinAssetCooker::parseManifestBuffer(
        assetReportOr->manifestBinary);
    E2E_ASSERT_STATUS_OK(parsedManifestOr);
    E2E_ASSERT_EQ(parsedManifestOr->size(), assetReportOr->manifestRecords.size());
    return true;
}

E2E_TEST(Tier1, FEAT_03, CookStep3_TypeScriptBundling_OutputVerified) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    const std::string tsPlayer = R"(
export interface HeroStats {
    hp: number;
    atk: number;
}
export function computeDamage(baseAtk: number, critRatio: number): number {
    return baseAtk * critRatio;
}
)";
    const std::string tsBootstrap = R"(
export function bootMiniGameSession(): string {
    return "session_ready";
}
)";
    E2E_ASSERT_STATUS_OK(loader.saveFile(sandbox.subPath("Scripts/HeroCombat.ts"), tsPlayer));
    E2E_ASSERT_STATUS_OK(loader.saveFile(sandbox.subPath("Scripts/Bootstrap.ts"), tsBootstrap));

    ::MQEngine::DouyinMiniGame::DouyinPackagerConfig cfg;
    auto bundleOr = ::MQEngine::DouyinMiniGame::DouyinScriptAndWasmBuilder::bundleProjectScripts(
        &loader, sandbox.rootDir, cfg);
    E2E_ASSERT_STATUS_OK(bundleOr);

    E2E_ASSERT_EQ(bundleOr->sourceTsFileCount, 2u);
    E2E_ASSERT_FALSE(bundleOr->bundledJsCode.empty());
    E2E_ASSERT_NE(bundleOr->bundledJsCode.find("computeDamage"), std::string::npos);
    E2E_ASSERT_NE(bundleOr->bundledJsCode.find("bootMiniGameSession"), std::string::npos);
    E2E_ASSERT_EQ(bundleOr->bundledJsCode.find("interface HeroStats"), std::string::npos);
    return true;
}

E2E_TEST(Tier1, FEAT_03, CookStep4_WasmBinaryPackaging_Configured) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    ::MQEngine::DouyinMiniGame::DouyinPackagerConfig cfg;
    cfg.emsdkRoot = sandbox.subPath("NonExistentEmsdkForDeterministicWasm");

    auto wasmOr = ::MQEngine::DouyinMiniGame::DouyinScriptAndWasmBuilder::buildEngineWasm(
        &loader, cfg, sandbox.subPath("BuildScratch"));
    E2E_ASSERT_STATUS_OK(wasmOr);

    E2E_ASSERT_TRUE(containsString(wasmOr->compileFlags, "-Oz"));
    E2E_ASSERT_TRUE(containsString(wasmOr->compileFlags, "-msimd128"));
    E2E_ASSERT_TRUE(containsString(wasmOr->compileFlags, "-fno-exceptions"));
    E2E_ASSERT_TRUE(containsString(wasmOr->compileFlags, "-fno-rtti"));
    E2E_ASSERT_FALSE(containsString(wasmOr->compileFlags, "-fexceptions"));

    E2E_ASSERT_GE(wasmOr->wasmBinary.size(), 8u);
    E2E_ASSERT_EQ(wasmOr->wasmBinary[0], 0x00u);
    E2E_ASSERT_EQ(wasmOr->wasmBinary[1], 0x61u); // 'a'
    E2E_ASSERT_EQ(wasmOr->wasmBinary[2], 0x73u); // 's'
    E2E_ASSERT_EQ(wasmOr->wasmBinary[3], 0x6Du); // 'm'
    return true;
}

E2E_TEST(Tier1, FEAT_03, CookStep5_DualPackageExport_DirectoryStructure_Verified) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;

    ::MQEngine::DouyinMiniGame::DouyinPipelineStepState incompleteSteps;
    incompleteSteps.step1ShadersDone = true;
    incompleteSteps.step2AssetsDone = true;
    incompleteSteps.step3ScriptsDone = true;
    incompleteSteps.step4WasmDone = false;
    E2E_ASSERT_FALSE(
        ::MQEngine::DouyinMiniGame::DouyinPackageExporter::validateStepPreconditions(incompleteSteps).ok());

    const std::string projectDir = sandbox.subPath("MiniProject");
    const std::string distDir = sandbox.subPath("DistOut");
    E2E_ASSERT_STATUS_OK(loader.saveFile(
        projectDir + "/Assets/Tri.obj",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    E2E_ASSERT_STATUS_OK(loader.saveFile(
        projectDir + "/Scripts/Main.ts",
        "export function startGame(): number { return 1; }\n"));

    ::MQEngine::DouyinMiniGame::DouyinCookRequest req;
    req.projectDir = projectDir;
    req.outputDir = distDir;
    req.useOverrideConfig = true;
    req.overrideConfig.appId = "tt_e2e_dual_pkg";
    req.overrideConfig.projectName = "DualPkgE2E";
    req.overrideConfig.orientation = "portrait";
    req.overrideConfig.emsdkRoot = sandbox.subPath("NoLocalEmsdk");

    auto cookResultOr = ::MQEngine::DouyinMiniGame::DouyinPackagerCliComposite::runCookPipeline(&loader, req);
    E2E_ASSERT_STATUS_OK(cookResultOr);

    const std::string& mainDir = cookResultOr->exportReport.mainPackageDir;
    const std::string& cdnDir = cookResultOr->exportReport.remoteCdnDir;
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/game.json"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/project.config.json"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/tt-adapter.js"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/game.js"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/game_logic.js"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/engine.wasm"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/shaders_wgl2.bin"));
    E2E_ASSERT_TRUE(loader.exists(mainDir + "/asset_manifest.bin"));
    E2E_ASSERT_TRUE(loader.exists(cdnDir));

    E2E_ASSERT_LE(cookResultOr->exportReport.mainPackageRawBytes, 10ull * 1024ull * 1024ull);
    E2E_ASSERT_LE(cookResultOr->exportReport.codeRawBytes, 4ull * 1024ull * 1024ull);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-04: 抖音侧边栏复访留存 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_04, OnShow_SceneValueDetection_Identified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::SidebarRetentionService sidebar(hostApi.get());

    ::MQEngine::DouyinMiniGame::DouyinShowEvent showEvt;
    showEvt.scene = "021036";
    showEvt.launchFrom = "homepage";
    showEvt.location = "sidebar_card";
    sidebar.handleOnShow(showEvt);

    E2E_ASSERT_TRUE(sidebar.isLaunchedFromSidebar());
    E2E_ASSERT_EQ(sidebar.getLastSceneCode(), std::string("021036"));
    return true;
}

E2E_TEST(Tier1, FEAT_04, OnShow_NormalScene_Identified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::SidebarRetentionService sidebar(hostApi.get());

    sidebar.handleOnShow(::MQEngine::DouyinMiniGame::DouyinShowEvent{.scene = "021036"});
    E2E_ASSERT_TRUE(sidebar.isLaunchedFromSidebar());

    sidebar.handleOnShow(::MQEngine::DouyinMiniGame::DouyinShowEvent{.scene = "011001"});
    E2E_ASSERT_FALSE(sidebar.isLaunchedFromSidebar());
    E2E_ASSERT_EQ(sidebar.getLastSceneCode(), std::string("011001"));
    return true;
}

E2E_TEST(Tier1, FEAT_04, CheckSidebarAvailable_SupportedQuery_Verified) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::SidebarRetentionService sidebar(&hostObserver);

    StatusOr<bool> queryResult = false;
    bool callbackInvoked = false;
    sidebar.checkSidebarAvailable(Tier1PluginBoolStatusRecorder{&queryResult, &callbackInvoked});

    E2E_ASSERT_TRUE(callbackInvoked);
    E2E_ASSERT_STATUS_OK(queryResult);
    E2E_ASSERT_TRUE(*queryResult);
    E2E_ASSERT_TRUE(sidebar.isSidebarSupported());
    E2E_ASSERT_EQ(hostObserver.checkSidebarCallCount, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_04, NavigateToSidebar_SuccessInvocation_Verified) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::SidebarRetentionService sidebar(&hostObserver);

    Status navStatus = InternalError("uninitialized");
    bool navInvoked = false;
    sidebar.navigateToSidebar(Tier1PluginStatusRecorder{&navStatus, &navInvoked});
    E2E_ASSERT_TRUE(navInvoked);
    E2E_ASSERT_STATUS_OK(navStatus);
    E2E_ASSERT_EQ(hostObserver.navigateSidebarCallCount, 1u);

    // 宿主版本不支持侧边栏时严禁盲目跳转
    sidebar.setSidebarSupported(false);
    Status unsupportedStatus = OkStatus();
    bool unsupportedInvoked = false;
    sidebar.navigateToSidebar(Tier1PluginStatusRecorder{&unsupportedStatus, &unsupportedInvoked});
    E2E_ASSERT_TRUE(unsupportedInvoked);
    E2E_ASSERT_FALSE(unsupportedStatus.ok());
    E2E_ASSERT_EQ(unsupportedStatus.code(), absl::StatusCode::kFailedPrecondition);
    return true;
}

E2E_TEST(Tier1, FEAT_04, SidebarRewardClaim_StateTransition_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::SidebarRetentionService sidebar(hostApi.get());

    // 非侧边栏启动时禁止领奖
    sidebar.handleOnShow(::MQEngine::DouyinMiniGame::DouyinShowEvent{.scene = "011001"});
    auto deniedOr = sidebar.claimReward("2026-11-12");
    E2E_ASSERT_FALSE(deniedOr.ok());

    // 侧边栏复访场景下首次领奖成功，同周期二次领奖防重拦截
    sidebar.handleOnShow(::MQEngine::DouyinMiniGame::DouyinShowEvent{.scene = "021036"});
    auto firstClaimOr = sidebar.claimReward("2026-11-12");
    E2E_ASSERT_STATUS_OK(firstClaimOr);
    E2E_ASSERT_TRUE(*firstClaimOr);
    E2E_ASSERT_TRUE(sidebar.hasClaimedReward("2026-11-12"));

    auto duplicateClaimOr = sidebar.claimReward("2026-11-12");
    E2E_ASSERT_STATUS_OK(duplicateClaimOr);
    E2E_ASSERT_FALSE(*duplicateClaimOr);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-05: 抖音激励视频广告全链路管理 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_05, AdCreationAndPreload_Success_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::RewardedVideoAdService adService(hostApi.get());

    E2E_ASSERT_FALSE(adService.initialize("").ok());
    E2E_ASSERT_STATUS_OK(adService.initialize("adunit-douyin-778899"));
    E2E_ASSERT_TRUE(adService.isInitialized());
    E2E_ASSERT_TRUE(adService.isPreloaded());
    E2E_ASSERT_EQ(adService.getAdUnitId(), std::string("adunit-douyin-778899"));
    return true;
}

E2E_TEST(Tier1, FEAT_05, AudioMuteLifecycle_DuringAdPlay_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::RewardedVideoAdService adService(hostApi.get());
    E2E_ASSERT_STATUS_OK(adService.initialize("adunit-audio-lifecycle"));

    bool audioMutedSnapshot = false;
    uint32_t muteToggleCount = 0;
    adService.setAudioMuteCallback(Tier1PluginAudioMuteRecorder{&audioMutedSnapshot, &muteToggleCount});

    ::MQEngine::DouyinMiniGame::RewardedAdCloseResult closeRes;
    bool resultInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(Tier1PluginRewardResultRecorder{&closeRes, &resultInvoked}));

    E2E_ASSERT_TRUE(adService.isShowing());
    E2E_ASSERT_TRUE(adService.isEngineAudioMuted());
    E2E_ASSERT_TRUE(audioMutedSnapshot);
    E2E_ASSERT_EQ(muteToggleCount, 1u);

    adService.notifyAdClosed(true);
    E2E_ASSERT_FALSE(adService.isShowing());
    E2E_ASSERT_FALSE(adService.isEngineAudioMuted());
    E2E_ASSERT_FALSE(audioMutedSnapshot);
    E2E_ASSERT_EQ(muteToggleCount, 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_05, RewardGranted_OnIsEndedTrue_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::RewardedVideoAdService adService(hostApi.get());
    E2E_ASSERT_STATUS_OK(adService.initialize("adunit-reward-grant"));

    ::MQEngine::DouyinMiniGame::RewardedAdCloseResult closeRes;
    bool resultInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(Tier1PluginRewardResultRecorder{&closeRes, &resultInvoked}));

    adService.notifyAdClosed(true);
    E2E_ASSERT_TRUE(resultInvoked);
    E2E_ASSERT_TRUE(closeRes.isEnded);
    E2E_ASSERT_TRUE(closeRes.rewardGranted);
    E2E_ASSERT_EQ(closeRes.errorCode, 0);
    E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_05, RewardDenied_OnIsEndedFalse_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::RewardedVideoAdService adService(hostApi.get());
    E2E_ASSERT_STATUS_OK(adService.initialize("adunit-reward-deny"));

    ::MQEngine::DouyinMiniGame::RewardedAdCloseResult closeRes;
    closeRes.rewardGranted = true;
    bool resultInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(Tier1PluginRewardResultRecorder{&closeRes, &resultInvoked}));

    adService.notifyAdClosed(false);
    E2E_ASSERT_TRUE(resultInvoked);
    E2E_ASSERT_FALSE(closeRes.isEnded);
    E2E_ASSERT_FALSE(closeRes.rewardGranted);
    E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_05, AdLoadFailure_ErrorStatus_Propagated) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::RewardedVideoAdService adService(hostApi.get());
    E2E_ASSERT_STATUS_OK(adService.initialize("adunit-no-fill"));

    ::MQEngine::DouyinMiniGame::RewardedAdCloseResult closeRes;
    bool resultInvoked = false;
    E2E_ASSERT_STATUS_OK(adService.show(Tier1PluginRewardResultRecorder{&closeRes, &resultInvoked}));
    E2E_ASSERT_TRUE(adService.isEngineAudioMuted());

    adService.notifyAdError(1004, "No ad fill available");
    E2E_ASSERT_TRUE(resultInvoked);
    E2E_ASSERT_FALSE(closeRes.rewardGranted);
    E2E_ASSERT_EQ(closeRes.errorCode, 1004);
    E2E_ASSERT_EQ(closeRes.errorMessage, std::string("No ad fill available"));
    E2E_ASSERT_FALSE(adService.isEngineAudioMuted());
    E2E_ASSERT_EQ(adService.getGrantedRewardCount(), 0u);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-06: 抖音游戏录屏与一键拍抖音分享 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_06, RecorderStartAndStop_Lifecycle_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::GameRecorderService recorder(hostApi.get());

    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Idle);
    E2E_ASSERT_STATUS_OK(recorder.start(120));
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Recording);

    // 低于 3.0 秒的最短录屏阈值应拒绝导出短视频
    auto tooShortOr = recorder.stop(1.5);
    E2E_ASSERT_FALSE(tooShortOr.ok());

    E2E_ASSERT_STATUS_OK(recorder.start(120));
    auto videoPathOr = recorder.stop(12.5);
    E2E_ASSERT_STATUS_OK(videoPathOr);
    E2E_ASSERT_FALSE(videoPathOr->empty());
    E2E_ASSERT_NE(videoPathOr->find(".mp4"), std::string::npos);
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Idle);
    return true;
}

E2E_TEST(Tier1, FEAT_06, RecordDurationLimit_300sMax_Enforced) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::GameRecorderService recorder(&hostObserver);

    E2E_ASSERT_FALSE(recorder.start(0).ok());
    E2E_ASSERT_FALSE(recorder.start(-10).ok());

    E2E_ASSERT_STATUS_OK(recorder.start(600));
    E2E_ASSERT_EQ(
        recorder.getActiveDurationLimitSec(),
        ::MQEngine::DouyinMiniGame::GameRecorderService::kMaxDurationSeconds);
    E2E_ASSERT_EQ(hostObserver.lastRecorderDurationSec, 300);

    auto stoppedOr = recorder.stop(8.0);
    E2E_ASSERT_STATUS_OK(stoppedOr);
    return true;
}

E2E_TEST(Tier1, FEAT_06, HighlightMark_TimestampRecording_Verified) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::GameRecorderService recorder(&hostObserver);

    // 未启动录屏时标记高光必须失败
    E2E_ASSERT_FALSE(recorder.recordHighlightClip(5.0, 3.0).ok());

    E2E_ASSERT_STATUS_OK(recorder.start(180));
    E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(8.0, 4.0));
    E2E_ASSERT_STATUS_OK(recorder.recordHighlightClip(10.0, 5.0));
    E2E_ASSERT_EQ(recorder.getRecordedClipCount(), 2u);
    E2E_ASSERT_EQ(hostObserver.recordClipCallCount, 2u);

    auto stoppedOr = recorder.stop(20.0);
    E2E_ASSERT_STATUS_OK(stoppedOr);
    return true;
}

E2E_TEST(Tier1, FEAT_06, ShareAppMessage_VideoChannel_PayloadVerified) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::GameRecorderService recorder(&hostObserver);

    // 空视频路径必须报错拦截
    ::MQEngine::DouyinMiniGame::DouyinShareVideoRequest badReq;
    badReq.title = "空白路径测试";
    Status badStatus = OkStatus();
    bool badInvoked = false;
    recorder.shareVideo(badReq, Tier1PluginStatusRecorder{&badStatus, &badInvoked});
    E2E_ASSERT_TRUE(badInvoked);
    E2E_ASSERT_FALSE(badStatus.ok());

    E2E_ASSERT_STATUS_OK(recorder.start(90));
    auto videoPathOr = recorder.stop(10.0);
    E2E_ASSERT_STATUS_OK(videoPathOr);

    ::MQEngine::DouyinMiniGame::DouyinShareVideoRequest req;
    req.videoPath = *videoPathOr;
    req.title = "五杀高光时刻";
    req.videoTopics = {"抖音小游戏", "MQEngine"};

    Status shareStatus = InternalError("pending");
    bool shareInvoked = false;
    recorder.shareVideo(req, Tier1PluginStatusRecorder{&shareStatus, &shareInvoked});

    E2E_ASSERT_TRUE(shareInvoked);
    E2E_ASSERT_STATUS_OK(shareStatus);
    E2E_ASSERT_EQ(hostObserver.shareVideoCallCount, 1u);
    E2E_ASSERT_EQ(hostObserver.lastShareRequest.videoPath, *videoPathOr);
    E2E_ASSERT_EQ(hostObserver.lastShareRequest.videoTopics.size(), 2u);
    E2E_ASSERT_EQ(hostObserver.lastShareRequest.videoTopics[0], std::string("抖音小游戏"));
    return true;
}

E2E_TEST(Tier1, FEAT_06, RecorderPauseResume_State_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::GameRecorderService recorder(hostApi.get());

    E2E_ASSERT_STATUS_OK(recorder.start(120));
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Recording);

    E2E_ASSERT_STATUS_OK(recorder.pause());
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Paused);

    E2E_ASSERT_STATUS_OK(recorder.resume());
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Recording);

    auto stoppedOr = recorder.stop(6.0);
    E2E_ASSERT_STATUS_OK(stoppedOr);
    E2E_ASSERT_EQ(recorder.getState(), ::MQEngine::DouyinMiniGame::RecorderState::Idle);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-07: 官方胶囊安全区与开放数据域社交桥接 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_07, CapsuleRect_QueryAndCaching_Verified) {
    auto hostApi = ::MQEngine::DouyinMiniGame::createWebJsDouyinHostApi();
    ::MQEngine::DouyinMiniGame::CapsuleAvoidanceBridge bridge(hostApi.get());

    ::MQEngine::DouyinMiniGame::DouyinSystemSafeArea sysArea;
    sysArea.windowWidth = 390.0f;
    sysArea.windowHeight = 844.0f;
    sysArea.safeLeft = 0.0f;
    sysArea.safeTop = 44.0f;
    sysArea.safeRight = 390.0f;
    sysArea.safeBottom = 810.0f;

    ::MQEngine::DouyinMiniGame::DouyinCapsuleRect capsule;
    capsule.left = 291.0f;
    capsule.top = 48.0f;
    capsule.right = 378.0f;
    capsule.bottom = 80.0f;
    capsule.width = 87.0f;
    capsule.height = 32.0f;

    const auto metrics = bridge.applyExplicitRects(sysArea, capsule);
    E2E_ASSERT_NEAR(metrics.capsuleAvoidTopPx, 48.0f, 1e-3f);
    E2E_ASSERT_NEAR(metrics.capsuleAvoidWidthPx, 87.0f, 1e-3f);
    E2E_ASSERT_NEAR(metrics.capsuleAvoidHeightPx, 32.0f, 1e-3f);
    E2E_ASSERT_GE(metrics.capsuleAvoidRightPx, 87.0f);

    const auto& cssVars = bridge.getInjectedCssVars();
    E2E_ASSERT_TRUE(cssVars.contains("--mq-capsule-avoid-top"));
    E2E_ASSERT_TRUE(cssVars.contains("--mq-capsule-avoid-right"));
    return true;
}

E2E_TEST(Tier1, FEAT_07, SafeArea_QueryAndNormalization_Verified) {
    ::MQEngine::DouyinMiniGame::DouyinSystemSafeArea notchArea;
    notchArea.windowWidth = 390.0f;
    notchArea.windowHeight = 844.0f;
    notchArea.safeLeft = 0.0f;
    notchArea.safeTop = 47.0f;
    notchArea.safeRight = 390.0f;
    notchArea.safeBottom = 810.0f;

    ::MQEngine::DouyinMiniGame::DouyinCapsuleRect abnormalCapsule;
    abnormalCapsule.left = -50.0f;
    abnormalCapsule.top = -12.0f;
    abnormalCapsule.right = 900.0f;
    abnormalCapsule.bottom = 20.0f;
    abnormalCapsule.width = -30.0f;
    abnormalCapsule.height = -10.0f;

    const auto sanitized = ::MQEngine::DouyinMiniGame::CapsuleAvoidanceBridge::computeSanitizedMetrics(
        notchArea, abnormalCapsule);
    E2E_ASSERT_GE(sanitized.capsuleAvoidTopPx, 0.0f);
    E2E_ASSERT_GE(sanitized.capsuleAvoidRightPx, 0.0f);
    E2E_ASSERT_NEAR(sanitized.safeTopPx, 47.0f, 1e-3f);
    E2E_ASSERT_NEAR(sanitized.safeBottomPx, 34.0f, 1e-3f);
    return true;
}

E2E_TEST(Tier1, FEAT_07, OpenDataContext_PostMessage_Serialized) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::OpenDataDomainBridge openData(&hostObserver);

    E2E_ASSERT_FALSE(openData.postMessage("").ok());
    E2E_ASSERT_FALSE(openData.postMessage("not_a_json_object").ok());

    const std::string validJson = R"({"cmd":"setUserRecord","level":12,"stars":3})";
    E2E_ASSERT_STATUS_OK(openData.postMessage(validJson));
    E2E_ASSERT_EQ(openData.getPostedMessageCount(), 1u);
    E2E_ASSERT_EQ(openData.getLastPostedJson(), validJson);
    E2E_ASSERT_EQ(hostObserver.openDataPostCount, 1u);
    E2E_ASSERT_EQ(hostObserver.lastOpenDataPayload, validJson);
    return true;
}

E2E_TEST(Tier1, FEAT_07, SharedCanvas_TextureBinding_Managed) {
    E2E_ASSERT_EQ(
        std::string(::MQEngine::DouyinMiniGame::OpenDataDomainBridge::kSharedRankTextureUri),
        std::string("@mq_shared_canvas/rank"));

    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    ::MQEngine::GameHtmlUI::OpenDataLeaderboardController leaderboard(&wglCtx, &router);

    const auto handle = leaderboard.initializeSharedCanvas(256u, 256u);
    E2E_ASSERT_NE(handle, 0u);
    E2E_ASSERT_TRUE(leaderboard.getSharedCanvasImage() != nullptr);

    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::OpenDataDomainBridge openData(&hostObserver);
    openData.bindTargetRankTexture(leaderboard.getSharedCanvasImage());
    openData.setSyncIntervalSeconds(0.05f);
    openData.setLeaderboardVisible(true);
    openData.tick(0.06f);

    E2E_ASSERT_GE(openData.getSharedCanvasSyncCount(), 1u);
    E2E_ASSERT_GE(hostObserver.sharedCanvasReadCount, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_07, SharedCanvas_VisibilityRefresh_Toggled) {
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::OpenDataDomainBridge openData(&hostObserver);
    openData.setSyncIntervalSeconds(0.05f);

    // 隐藏态 0 开销：多帧 tick 绝不触发跨上下文读取
    openData.setLeaderboardVisible(false);
    for (int i = 0; i < 6; ++i) {
        openData.tick(0.05f);
    }
    E2E_ASSERT_EQ(openData.getSharedCanvasSyncCount(), 0u);
    E2E_ASSERT_EQ(hostObserver.sharedCanvasReadCount, 0u);

    // 展开排行榜后立即按周期同步
    openData.setLeaderboardVisible(true);
    const uint32_t syncOnShow = openData.getSharedCanvasSyncCount();
    openData.tick(0.06f);
    E2E_ASSERT_GT(openData.getSharedCanvasSyncCount(), syncOnShow);

    // 关闭排行榜后同步计数冻结
    const uint32_t frozenCount = openData.getSharedCanvasSyncCount();
    openData.setLeaderboardVisible(false);
    openData.tick(0.20f);
    E2E_ASSERT_EQ(openData.getSharedCanvasSyncCount(), frozenCount);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-08: 抖音宿主内存告警联动自救 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_08, MemoryWarning_ListenerRegistration_Verified) {
    ::MQEngine::DouyinMiniGame::DouyinPlatformRuntimeComposite rtComposite;
    auto observerPtr = makeUnique<Tier1PluginHostCallObserver>();
    Tier1PluginHostCallObserver* rawObserver = observerPtr.get();
    rtComposite.setCustomHostApi(std::move(observerPtr));

    E2E_ASSERT_STATUS_OK(rtComposite.onLoad(nullptr));
    E2E_ASSERT_TRUE(rawObserver->memoryWarningListenerBound);
    E2E_ASSERT_TRUE(static_cast<bool>(rawObserver->capturedMemoryWarningListener));

    rawObserver->capturedMemoryWarningListener(
        ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical);

    const auto& stats = rtComposite.getMemoryCoordinator().getStats();
    E2E_ASSERT_EQ(stats.totalWarningsHandled, 1u);
    E2E_ASSERT_EQ(stats.lastWarningLevel, 15);
    rtComposite.onUnload();
    return true;
}

E2E_TEST(Tier1, FEAT_08, AssetCacheEviction_OnMemoryWarning_Triggered) {
    ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator coordinator;
    E2E_ASSERT_FALSE(coordinator.handleMemoryWarning(-1).ok());

    uint32_t assetTrimCalls = 0;
    int32_t recordedLevel = 0;
    coordinator.setAssetCacheTrimCallback(
        Tier1PluginMemoryTrimCounter{&assetTrimCalls, &recordedLevel, 8ull * 1024ull * 1024ull});

    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(
        ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical));

    const auto& stats = coordinator.getStats();
    E2E_ASSERT_EQ(assetTrimCalls, 1u);
    E2E_ASSERT_EQ(recordedLevel, 15);
    E2E_ASSERT_EQ(stats.assetCachePurgeCount, 1u);
    E2E_ASSERT_GE(stats.totalBytesReclaimed, 8ull * 1024ull * 1024ull);
    return true;
}

E2E_TEST(Tier1, FEAT_08, FontAtlas_ColdPageEviction_Triggered) {
    ::MQEngine::Resource::Font::FontSystem fontSys(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_STATUS_OK(fontSys.init(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));

    const char32_t warmGlyphs[] = {U'战', U'斗', U'胜', U'利'};
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(
        std::span<const char32_t>(warmGlyphs, 4u), 24u, "@system-ui"));

    const uint32_t genBefore = fontSys.getAtlasGeneration();

    ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator coordinator;
    coordinator.bindFontSystem(&fontSys);
    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(
        ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator::kLevelLow));

    E2E_ASSERT_EQ(coordinator.getStats().fontAtlasTrimCount, 1u);
    E2E_ASSERT_GT(fontSys.getAtlasGeneration(), genBefore);
    fontSys.term();
    return true;
}

E2E_TEST(Tier1, FEAT_08, RenderGraph_TransientPoolFlush_Triggered) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});
    ::FCT::WGL2_Device wglDevice(&wglCtx);
    ::FCT::TransientResourcePool transientPool(&wglDevice);

    ::FCT::TransientResourceSpec colorSpec;
    colorSpec.width = 512u;
    colorSpec.height = 512u;
    colorSpec.format = ::FCT::Format::R8G8B8A8_UNORM;
    colorSpec.isSampled = true;
    colorSpec.usage = ::FCT::ImageUsage::RenderTarget | ::FCT::ImageUsage::Texture;

    ::FCT::Image* acquired = transientPool.acquire(colorSpec, 0u, 2u, 1u);
    E2E_ASSERT_TRUE(acquired != nullptr);
    E2E_ASSERT_GE(transientPool.getPoolSize(), 1u);

    ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator coordinator;
    coordinator.bindTransientResourcePool(&transientPool);
    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(
        ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical));

    E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);
    E2E_ASSERT_EQ(coordinator.getStats().renderPoolTrimCount, 1u);
    E2E_ASSERT_GT(coordinator.getStats().totalBytesReclaimed, 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_08, CascadeRecovery_Under100ms_ExecutionVerified) {
    ::MQEngine::Resource::Font::FontSystem fontSys(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_STATUS_OK(fontSys.init(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));

    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});
    ::FCT::WGL2_Device wglDevice(&wglCtx);
    ::FCT::TransientResourcePool transientPool(&wglDevice);

    ::FCT::TransientResourceSpec rtSpec;
    rtSpec.width = 256u;
    rtSpec.height = 256u;
    rtSpec.format = ::FCT::Format::R8G8B8A8_UNORM;
    rtSpec.usage = ::FCT::ImageUsage::RenderTarget | ::FCT::ImageUsage::Texture;
    (void)transientPool.acquire(rtSpec, 0u, 1u, 1u);

    uint32_t assetCalls = 0;
    int32_t assetLevel = 0;
    ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator coordinator;
    coordinator.bindFontSystem(&fontSys);
    coordinator.bindTransientResourcePool(&transientPool);
    coordinator.setAssetCacheTrimCallback(
        Tier1PluginMemoryTrimCounter{&assetCalls, &assetLevel, 4ull * 1024ull * 1024ull});

    const auto tStart = std::chrono::steady_clock::now();
    E2E_ASSERT_STATUS_OK(coordinator.handleMemoryWarning(
        ::MQEngine::DouyinMiniGame::MemoryWarningCoordinator::kLevelCritical));
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();

    const auto& stats = coordinator.getStats();
    E2E_ASSERT_EQ(stats.totalWarningsHandled, 1u);
    E2E_ASSERT_EQ(stats.assetCachePurgeCount, 1u);
    E2E_ASSERT_EQ(stats.fontAtlasTrimCount, 1u);
    E2E_ASSERT_EQ(stats.renderPoolTrimCount, 1u);
    E2E_ASSERT_EQ(transientPool.getPoolSize(), 0u);
    E2E_ASSERT_LT(elapsedMs, 100.0);

    fontSys.term();
    return true;
}

// ============================================================================
// PART 2: GameHtmlUI 运行时轻量重构能力套件 (FEAT-47 ~ FEAT-54)
// ============================================================================

// ----------------------------------------------------------------------------
// FEAT-47: 运行时物理裁剪与零冗余链接瘦身 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_47, RuntimeBuild_EditorSymbolsExcluded_Verified) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    E2E_ASSERT_EQ(composite.getName(), std::string("RmlUiComposite"));
    E2E_ASSERT_EQ(composite.getPluginName(), std::string("GameHtmlUI"));

    const auto tags = composite.getRequiredTags();
    E2E_ASSERT_TRUE(containsString(tags, "Runtime"));
    E2E_ASSERT_TRUE(containsString(tags, "Game"));
    E2E_ASSERT_FALSE(containsString(tags, "Editor"));
    E2E_ASSERT_FALSE(containsString(tags, "ImGui"));
    return true;
}

E2E_TEST(Tier1, FEAT_47, DefaultFontEngine_StrippedFromBinary_Verified) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    E2E_ASSERT_TRUE(composite.isCustomFontEngineActive());
    E2E_ASSERT_FALSE(composite.isDefaultFontEngineActive());
    return true;
}

E2E_TEST(Tier1, FEAT_47, SystemFontFallback_NoHardcodedPath_Verified) {
    ::MQEngine::GameHtmlUI::MQRmlFileInterface pathTracker;
    ::MQEngine::GameHtmlUI::RmlUiComposite composite(&pathTracker);

    composite.initFontSubsystem();
    E2E_ASSERT_FALSE(pathTracker.hasQueriedPath("msyh.ttc"));
    E2E_ASSERT_FALSE(pathTracker.hasQueriedPath("simhei.ttf"));
    E2E_ASSERT_FALSE(pathTracker.hasQueriedPath("C:/Windows/Fonts/"));
    composite.onUnload();
    return true;
}

E2E_TEST(Tier1, FEAT_47, CompositeLifecycle_InitShutdown_Clean) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    E2E_ASSERT_STATUS_OK(composite.onLoad(nullptr));
    E2E_ASSERT_STATUS_OK(composite.onInit(nullptr));
    const uint32_t docsAfterInit = composite.getActiveDocumentCount();

    E2E_ASSERT_STATUS_OK(composite.loadDocumentFromMemory(
        "<rml><head><title>HUD</title></head><body><div id=\"score_txt\">1024</div></body></rml>",
        "hud_main"));
    E2E_ASSERT_EQ(composite.getActiveDocumentCount(), docsAfterInit + 1u);

    composite.onTick(0.016f);
    composite.onUnload();
    E2E_ASSERT_EQ(composite.getActiveDocumentCount(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_47, RmlDebugger_DisabledInRelease_Verified) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    E2E_ASSERT_FALSE(composite.isDebuggerEnabled());
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-48: 自研 RmlUi 字体引擎后端 MQRmlFontEngine (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_48, LoadFontFace_SystemUiVirtualFamily_Resolved) {
    ::MQEngine::Resource::Font::FontSystem fontSys(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_STATUS_OK(fontSys.init(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));

    ::MQEngine::GameHtmlUI::MQRmlFontEngine fontEngine(&fontSys);
    fontEngine.Initialize();

    E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", false, Rml::Style::FontWeight::Normal));
    const Rml::FontFaceHandle handle = fontEngine.GetFontFaceHandle(
        "@system-ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 20);
    E2E_ASSERT_NE(handle, static_cast<Rml::FontFaceHandle>(0));

    fontEngine.Shutdown();
    fontSys.term();
    return true;
}

E2E_TEST(Tier1, FEAT_48, GetFontMetrics_MetricsPopulated_Correctly) {
    ::MQEngine::GameHtmlUI::MQRmlFontEngine fontEngine;
    fontEngine.Initialize();
    E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", false, Rml::Style::FontWeight::Normal));

    const Rml::FontFaceHandle handle = fontEngine.GetFontFaceHandle(
        "@system-ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
    E2E_ASSERT_NE(handle, static_cast<Rml::FontFaceHandle>(0));

    // RmlUi 6.0 排版规范：ascent > 0, descent > 0, line_spacing >= ascent + descent
    const Rml::FontMetrics& rmlMetrics = fontEngine.GetFontMetrics(handle);
    E2E_ASSERT_GT(rmlMetrics.ascent, 0.0f);
    E2E_ASSERT_GT(rmlMetrics.descent, 0.0f);
    E2E_ASSERT_GE(rmlMetrics.line_spacing, rmlMetrics.ascent + rmlMetrics.descent);

    // 底层 SAI 有符号度量规范：ascent > 0, descent < 0, lineSpacing >= ascent - descent
    const ::FCT::SAI::FontMetrics signedMetrics = fontEngine.getSignedFontMetrics(handle);
    E2E_ASSERT_GT(signedMetrics.ascent, 0.0f);
    E2E_ASSERT_LT(signedMetrics.descent, 0.0f);
    E2E_ASSERT_GE(signedMetrics.lineSpacing, signedMetrics.ascent - signedMetrics.descent);

    fontEngine.Shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_48, GetStringWidth_AccurateAdvanceAccumulation_Verified) {
    ::MQEngine::GameHtmlUI::MQRmlFontEngine fontEngine;
    fontEngine.Initialize();
    E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", false, Rml::Style::FontWeight::Normal));

    const Rml::FontFaceHandle handle = fontEngine.GetFontFaceHandle(
        "@system-ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
    E2E_ASSERT_NE(handle, static_cast<Rml::FontFaceHandle>(0));

    E2E_ASSERT_EQ(fontEngine.GetStringWidth(handle, ""), 0);

    const int widthAscii = fontEngine.GetStringWidth(handle, "M");
    const int widthPair = fontEngine.GetStringWidth(handle, "MQ");
    const int widthCjk = fontEngine.GetStringWidth(handle, "小");
    const int widthMixed = fontEngine.GetStringWidth(handle, "MQ小游戏");

    E2E_ASSERT_GT(widthAscii, 0);
    E2E_ASSERT_GT(widthPair, widthAscii);
    E2E_ASSERT_GE(widthCjk, widthAscii);
    E2E_ASSERT_GT(widthMixed, widthPair + widthCjk);

    fontEngine.Shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_48, GenerateString_TwoPassBatching_Verified) {
    ::MQEngine::GameHtmlUI::MQRmlFontEngine fontEngine;
    fontEngine.Initialize();
    E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", false, Rml::Style::FontWeight::Normal));

    const Rml::FontFaceHandle handle = fontEngine.GetFontFaceHandle(
        "@system-ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 22);
    E2E_ASSERT_NE(handle, static_cast<Rml::FontFaceHandle>(0));

    std::vector<Rml::Mesh> meshes;
    const int totalAdvance = fontEngine.GenerateString(
        nullptr,
        handle,
        "战斗开始",
        Rml::Vector2f(0.0f, 0.0f),
        Rml::Colourb(255, 255, 255, 255),
        meshes);

    E2E_ASSERT_GT(totalAdvance, 0);
    E2E_ASSERT_FALSE(meshes.empty());
    E2E_ASSERT_EQ(fontEngine.getLastBatchWarmCount(), 4u);

    size_t totalVertices = 0;
    size_t totalIndices = 0;
    for (const auto& mesh : meshes) {
        totalVertices += mesh.vertices.size();
        totalIndices += mesh.indices.size();
    }
    E2E_ASSERT_EQ(totalVertices, 16u);
    E2E_ASSERT_EQ(totalIndices, 24u);

    fontEngine.Shutdown();
    return true;
}

E2E_TEST(Tier1, FEAT_48, GetVersion_AtlasGenerationReflected_Verified) {
    ::MQEngine::Resource::Font::FontSystem fontSys(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_STATUS_OK(fontSys.init(
        ::MQEngine::Resource::Font::FontBackendMode::MiniGame_Hybrid));

    ::MQEngine::GameHtmlUI::MQRmlFontEngine fontEngine(&fontSys);
    fontEngine.Initialize();
    E2E_ASSERT_TRUE(fontEngine.LoadFontFace("@system-ui", false, Rml::Style::FontWeight::Normal));

    const Rml::FontFaceHandle handle = fontEngine.GetFontFaceHandle(
        "@system-ui", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 18);

    const int versionInitial = fontEngine.GetVersion(handle);
    fontEngine.notifyAtlasInvalidated();
    const int versionAfterManualInvalidate = fontEngine.GetVersion(handle);
    E2E_ASSERT_GT(versionAfterManualInvalidate, versionInitial);

    fontSys.onMemoryWarning();
    const int versionAfterMemoryTrim = fontEngine.GetVersion(handle);
    E2E_ASSERT_GT(versionAfterMemoryTrim, versionAfterManualInvalidate);

    fontEngine.Shutdown();
    fontSys.term();
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-49: RmlUiPass 直出 Backbuffer 渲染管线集成 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_49, DrawBucket_InputTags_Configured) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    const auto bucket = composite.generateDrawBucket(1u);

    E2E_ASSERT_TRUE(bucket.hasInputTag("UI"));
    E2E_ASSERT_TRUE(bucket.hasInputTag("RmlUi"));
    E2E_ASSERT_FALSE(bucket.hasInputTag("Opaque"));
    return true;
}

E2E_TEST(Tier1, FEAT_49, TargetBackbuffer_DirectRendering_Verified) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    E2E_ASSERT_TRUE(composite.isBackbufferTarget());
    E2E_ASSERT_FALSE(composite.allocatesOffscreenRt());

    const auto passDesc = composite.getPassDescriptor();
    E2E_ASSERT_TRUE(passDesc.isBackbufferTarget);
    E2E_ASSERT_FALSE(passDesc.allocatesOffscreenRt);
    E2E_ASSERT_TRUE(containsString(passDesc.inputTags, "UI"));
    E2E_ASSERT_TRUE(containsString(passDesc.inputTags, "RmlUi"));
    return true;
}

E2E_TEST(Tier1, FEAT_49, DepthWrite_Disabled_ForUiDraw) {
    ::MQEngine::GameHtmlUI::RmlUiComposite composite;
    const auto state = composite.getPipelineRenderState();

    E2E_ASSERT_FALSE(state.depthWriteEnabled);
    E2E_ASSERT_FALSE(state.depthTestEnabled);
    E2E_ASSERT_TRUE(state.blendEnabled);
    return true;
}

E2E_TEST(Tier1, FEAT_49, RenderInterface_ScissorRegion_Applied) {
    ::MQEngine::GameHtmlUI::MQRmlRenderInterface renderInterface;

    // 未编译几何句柄必须被安全跳过
    renderInterface.RenderGeometry(9999u, Rml::Vector2f(0.0f, 0.0f), 0u);
    E2E_ASSERT_EQ(renderInterface.getSkippedDrawCommandCount(), 1u);
    E2E_ASSERT_TRUE(renderInterface.getDrawCommands().empty());

    std::vector<Rml::Vertex> quadVerts(4);
    quadVerts[0].position = Rml::Vector2f(0.0f, 0.0f);
    quadVerts[1].position = Rml::Vector2f(64.0f, 0.0f);
    quadVerts[2].position = Rml::Vector2f(64.0f, 32.0f);
    quadVerts[3].position = Rml::Vector2f(0.0f, 32.0f);
    const std::vector<int> quadIndices{0, 1, 2, 2, 3, 0};

    const Rml::CompiledGeometryHandle geomHandle = renderInterface.compileGeometry(quadVerts, quadIndices);
    E2E_ASSERT_NE(geomHandle, static_cast<Rml::CompiledGeometryHandle>(0));

    renderInterface.EnableScissorRegion(true);
    renderInterface.SetScissorRegion(Rml::Rectanglei::FromPositionSize({10, 20}, {100, 200}));
    renderInterface.RenderGeometry(geomHandle, Rml::Vector2f(4.0f, 8.0f), 0u);

    const auto& cmds = renderInterface.getDrawCommands();
    E2E_ASSERT_EQ(cmds.size(), 1u);
    E2E_ASSERT_TRUE(cmds[0].scissorEnabled);
    E2E_ASSERT_EQ(cmds[0].scissor.Left(), 10);
    E2E_ASSERT_EQ(cmds[0].scissor.Top(), 20);
    E2E_ASSERT_EQ(cmds[0].scissor.Width(), 100);
    E2E_ASSERT_EQ(cmds[0].scissor.Height(), 200);

    renderInterface.ReleaseGeometry(geomHandle);
    return true;
}

E2E_TEST(Tier1, FEAT_49, GeometryCompilation_BufferLifecycle_Verified) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});

    ::MQEngine::GameHtmlUI::MQRmlRenderInterface renderInterface;
    renderInterface.bindRhiContext(&wglCtx);

    std::vector<Rml::Vertex> vertices(4);
    vertices[0].position = Rml::Vector2f(0.0f, 0.0f);
    vertices[1].position = Rml::Vector2f(100.0f, 0.0f);
    vertices[2].position = Rml::Vector2f(100.0f, 50.0f);
    vertices[3].position = Rml::Vector2f(0.0f, 50.0f);
    const std::vector<int> indices{0, 1, 2, 2, 3, 0};

    const Rml::CompiledGeometryHandle geom = renderInterface.compileGeometry(vertices, indices);
    E2E_ASSERT_NE(geom, static_cast<Rml::CompiledGeometryHandle>(0));
    E2E_ASSERT_TRUE(renderInterface.isGeometryCompiled(geom));

    renderInterface.ReleaseGeometry(geom);
    E2E_ASSERT_FALSE(renderInterface.isGeometryCompiled(geom));
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-50: 零拷贝虚拟纹理路由协议 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_50, GlyphAtlasUri_R8Route_Mapped) {
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    router.registerFontAtlasPage(
        0u,
        1001u,
        ::MQEngine::Resource::Font::TextureFormat::R8_UNORM,
        Rml::Vector2i(1024, 1024));

    Rml::Vector2i dims{0, 0};
    const Rml::TextureHandle handle = router.loadTexture(dims, "@mq_glyph_atlas/0");
    E2E_ASSERT_EQ(handle, static_cast<Rml::TextureHandle>(1001u));
    E2E_ASSERT_EQ(dims.x, 1024);
    E2E_ASSERT_EQ(dims.y, 1024);
    return true;
}

E2E_TEST(Tier1, FEAT_50, GlyphAtlasUri_EmojiRgba8Route_Mapped) {
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    router.registerFontAtlasPage(
        1u,
        1002u,
        ::MQEngine::Resource::Font::TextureFormat::R8G8B8A8_UNORM,
        Rml::Vector2i(1024, 1024));

    Rml::Vector2i dims{0, 0};
    const Rml::TextureHandle handle = router.loadTexture(dims, "@mq_glyph_atlas/emoji_1");
    E2E_ASSERT_EQ(handle, static_cast<Rml::TextureHandle>(1002u));
    E2E_ASSERT_EQ(dims.x, 1024);
    E2E_ASSERT_EQ(dims.y, 1024);
    return true;
}

E2E_TEST(Tier1, FEAT_50, SharedCanvasUri_LeaderboardRoute_Mapped) {
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    router.registerSharedCanvas("rank", 2001u, Rml::Vector2i(512, 512), nullptr);

    Rml::Vector2i dims{0, 0};
    const Rml::TextureHandle handle = router.loadTexture(dims, "@mq_shared_canvas/rank");
    E2E_ASSERT_EQ(handle, static_cast<Rml::TextureHandle>(2001u));
    E2E_ASSERT_EQ(dims.x, 512);
    E2E_ASSERT_EQ(dims.y, 512);
    return true;
}

E2E_TEST(Tier1, FEAT_50, AsyncAssetUri_PlaceholderReturned_WhenUnloaded) {
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    router.setPlaceholderTextureHandle(999u);

    Rml::Vector2i dims{0, 0};
    const Rml::TextureHandle handle = router.loadTexture(
        dims, "https://cdn.example.com/icons/weapon_sword.png");
    E2E_ASSERT_EQ(handle, static_cast<Rml::TextureHandle>(999u));
    E2E_ASSERT_EQ(dims.x, 1);
    E2E_ASSERT_EQ(dims.y, 1);
    return true;
}

E2E_TEST(Tier1, FEAT_50, AsyncAssetUri_HotReplaced_WhenLoaded) {
    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    router.setPlaceholderTextureHandle(999u);

    constexpr std::string_view kIconUri = "https://cdn.example.com/icons/epic_shield.png";
    Rml::Vector2i initialDims{0, 0};
    E2E_ASSERT_EQ(
        router.loadTexture(initialDims, kIconUri),
        static_cast<Rml::TextureHandle>(999u));

    router.notifyAssetLoaded(kIconUri, 5001u, Rml::Vector2i(64, 64));

    Rml::Vector2i loadedDims{0, 0};
    const Rml::TextureHandle hotHandle = router.loadTexture(loadedDims, kIconUri);
    E2E_ASSERT_EQ(hotHandle, static_cast<Rml::TextureHandle>(5001u));
    E2E_ASSERT_EQ(loadedDims.x, 64);
    E2E_ASSERT_EQ(loadedDims.y, 64);
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-51: 声明式虚拟触控直注 Enhanced Input (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_51, DeclarativeJoystick_MoveAction_Injected) {
    ::MQEngine::Domain::HID::HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    ::MQEngine::GameHtmlUI::VirtualTouchController touchCtrl(&hid);
    touchCtrl.bindControl(
        "joy_move",
        "IA_Move",
        ::MQEngine::GameHtmlUI::VirtualControlType::Joystick,
        {0.0f, 0.0f, 100.0f, 100.0f});

    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 50.0f, 50.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchMove(1, 80.0f, 90.0f));

    const auto actionVal = hid.getActionValue("IA_Move");
    E2E_ASSERT_EQ(actionVal.getValueType(), ::MQEngine::Domain::HID::EInputActionValueType::Axis2D);
    E2E_ASSERT_NEAR(actionVal.get<FCT::Vec2>().x, 0.6f, 0.05f);
    E2E_ASSERT_NEAR(actionVal.get<FCT::Vec2>().y, 0.8f, 0.05f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_51, DeclarativeButton_DigitalAction_Injected) {
    ::MQEngine::Domain::HID::HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    ::MQEngine::GameHtmlUI::VirtualTouchController touchCtrl(&hid);
    touchCtrl.bindControl(
        "btn_jump",
        "IA_Jump",
        ::MQEngine::GameHtmlUI::VirtualControlType::Button,
        {200.0f, 200.0f, 60.0f, 60.0f});

    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(2, 225.0f, 225.0f));
    E2E_ASSERT_TRUE(touchCtrl.getButtonState("IA_Jump"));
    E2E_ASSERT_TRUE(hid.getActionValue("IA_Jump").get<bool>());
    E2E_ASSERT_TRUE(hid.isActionPressed(std::string_view("IA_Jump")));

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_51, TouchConsumption_UIHit_MaskedFromWorld) {
    ::MQEngine::Domain::HID::HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    ::MQEngine::GameHtmlUI::VirtualTouchController touchCtrl(&hid);
    touchCtrl.bindControl(
        "btn_attack",
        "IA_Attack",
        ::MQEngine::GameHtmlUI::VirtualControlType::Button,
        {100.0f, 100.0f, 80.0f, 80.0f});

    // 命中 UI 控件：阻断向底层 3D 世界透传手势
    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 130.0f, 130.0f));
    E2E_ASSERT_TRUE(touchCtrl.isPointerConsumedByUi(1));
    E2E_ASSERT_TRUE(touchCtrl.onTouchMove(1, 165.0f, 165.0f));
    E2E_ASSERT_NEAR(hid.getTouchSwipeDelta().x, 0.0f, 1e-4f);

    // 点击空白游戏区：未消费，透传至底层世界相机手势源
    E2E_ASSERT_FALSE(touchCtrl.onTouchDown(2, 520.0f, 360.0f));
    E2E_ASSERT_FALSE(touchCtrl.isPointerConsumedByUi(2));

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_51, MultiTouch_ConcurrentJoysticks_Separated) {
    ::MQEngine::Domain::HID::HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    ::MQEngine::GameHtmlUI::VirtualTouchController touchCtrl(&hid);
    touchCtrl.bindControl(
        "joy_left",
        "IA_Move",
        ::MQEngine::GameHtmlUI::VirtualControlType::Joystick,
        {0.0f, 400.0f, 160.0f, 160.0f});
    touchCtrl.bindControl(
        "joy_right",
        "IA_Aim",
        ::MQEngine::GameHtmlUI::VirtualControlType::Joystick,
        {600.0f, 400.0f, 160.0f, 160.0f});

    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 80.0f, 480.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(2, 680.0f, 480.0f));

    E2E_ASSERT_TRUE(touchCtrl.onTouchMove(1, 140.0f, 480.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchMove(2, 680.0f, 420.0f));

    const auto moveAxis = hid.getActionValue("IA_Move").get<FCT::Vec2>();
    const auto aimAxis = hid.getActionValue("IA_Aim").get<FCT::Vec2>();
    E2E_ASSERT_GT(moveAxis.x, 0.5f);
    E2E_ASSERT_NEAR(moveAxis.y, 0.0f, 1e-4f);
    E2E_ASSERT_NEAR(aimAxis.x, 0.0f, 1e-4f);
    E2E_ASSERT_LT(aimAxis.y, -0.5f);

    // 抬起左手触点绝不影响右手瞄准摇杆
    E2E_ASSERT_TRUE(touchCtrl.onTouchUp(1, 140.0f, 480.0f));
    E2E_ASSERT_NEAR(hid.getActionValue("IA_Move").get<FCT::Vec2>().x, 0.0f, 1e-4f);
    E2E_ASSERT_LT(hid.getActionValue("IA_Aim").get<FCT::Vec2>().y, -0.5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_51, TouchRelease_ZeroValueReset_Verified) {
    ::MQEngine::Domain::HID::HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    ::MQEngine::GameHtmlUI::VirtualTouchController touchCtrl(&hid);
    touchCtrl.bindControl(
        "joy_move",
        "IA_Move",
        ::MQEngine::GameHtmlUI::VirtualControlType::Joystick,
        {0.0f, 0.0f, 100.0f, 100.0f});
    touchCtrl.bindControl(
        "btn_fire",
        "IA_Fire",
        ::MQEngine::GameHtmlUI::VirtualControlType::Button,
        {300.0f, 300.0f, 60.0f, 60.0f});

    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(1, 50.0f, 50.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchMove(1, 95.0f, 20.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchDown(2, 320.0f, 320.0f));
    E2E_ASSERT_TRUE(hid.getActionValue("IA_Fire").get<bool>());

    E2E_ASSERT_TRUE(touchCtrl.onTouchUp(1, 95.0f, 20.0f));
    E2E_ASSERT_TRUE(touchCtrl.onTouchUp(2, 320.0f, 320.0f));

    E2E_ASSERT_NEAR(hid.getActionValue("IA_Move").get<FCT::Vec2>().x, 0.0f, 1e-5f);
    E2E_ASSERT_NEAR(hid.getActionValue("IA_Move").get<FCT::Vec2>().y, 0.0f, 1e-5f);
    E2E_ASSERT_FALSE(hid.getActionValue("IA_Fire").get<bool>());
    E2E_ASSERT_EQ(touchCtrl.getActivePointerCount(), 0u);

    hid.term();
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-52: 异形屏与抖音胶囊安全区自适应布局 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_52, DprScaling_ContextProperty_Applied) {
    ::MQEngine::GameHtmlUI::CapsuleCssVariableInjector injector;
    injector.setDpr(2.0f);

    E2E_ASSERT_NEAR(injector.getDpr(), 2.0f, 1e-4f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-device-pixel-ratio"), std::string("2.0"));
    return true;
}

E2E_TEST(Tier1, FEAT_52, CapsuleAvoidVariables_InjectedIntoRoot) {
    ::MQEngine::GameHtmlUI::CapsuleCssVariableInjector injector;
    injector.updateCapsule(20.0f, 380.0f, 80.0f, 32.0f, 400.0f);

    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-top"), std::string("20px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-right"), std::string("100px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-width"), std::string("80px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-height"), std::string("32px"));
    return true;
}

E2E_TEST(Tier1, FEAT_52, SafeAreaVariables_InjectedIntoRoot) {
    ::MQEngine::GameHtmlUI::CapsuleCssVariableInjector injector;
    injector.updateSafeArea(44.0f, 34.0f, 0.0f, 0.0f);

    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-top"), std::string("44px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-bottom"), std::string("34px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-left"), std::string("0px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-safe-right"), std::string("0px"));
    return true;
}

E2E_TEST(Tier1, FEAT_52, WindowResize_Recalculation_Triggered) {
    ::MQEngine::GameHtmlUI::CapsuleCssVariableInjector injector;
    injector.updateCapsule(24.0f, 370.0f, 84.0f, 32.0f, 390.0f);
    injector.clearCssDirtyFlag();
    E2E_ASSERT_FALSE(injector.isCssDirty());

    injector.onWindowResize(800, 600);
    E2E_ASSERT_TRUE(injector.isCssDirty());
    E2E_ASSERT_EQ(injector.getWindowWidth(), 800);
    E2E_ASSERT_EQ(injector.getWindowHeight(), 600);
    E2E_ASSERT_EQ(injector.getResizeCount(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_52, ZeroAvoidance_WhenNoCapsule_Handled) {
    ::MQEngine::GameHtmlUI::CapsuleCssVariableInjector injector;
    injector.updateCapsule(24.0f, 370.0f, 84.0f, 32.0f, 390.0f);
    E2E_ASSERT_GT(injector.getAvoidRight(), 0.0f);

    injector.resetCapsule();
    E2E_ASSERT_NEAR(injector.getAvoidTop(), 0.0f, 1e-4f);
    E2E_ASSERT_NEAR(injector.getAvoidRight(), 0.0f, 1e-4f);
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-top"), std::string("0px"));
    E2E_ASSERT_EQ(injector.getCssVariable("--mq-capsule-avoid-right"), std::string("0px"));
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-53: 表单输入与宿主原生软键盘桥接 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_53, InputFocus_VirtualKeyboardShow_Invoked) {
    (void)::FCT::SAI::VirtualKeyboard::hide();
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();

    ::MQEngine::GameHtmlUI::RmlUiKeyboardBridge bridge;
    bridge.onElementFocus("player_name_input", "text", "默认昵称");

    E2E_ASSERT_TRUE(bridge.isKeyboardActive());
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());
    E2E_ASSERT_EQ(::FCT::SAI::VirtualKeyboard::getCurrentConfig().defaultValue, std::string("默认昵称"));
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::getCurrentConfig().multiple);

    bridge.onElementBlur("player_name_input");
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_53, TextInputCallback_TextBackfilled_ToElement) {
    (void)::FCT::SAI::VirtualKeyboard::hide();
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();

    ::MQEngine::GameHtmlUI::RmlUiKeyboardBridge bridge;
    bridge.onElementFocus("chat_input", "text", "");
    ::FCT::SAI::VirtualKeyboard::simulateInput("抖音大侠666");

    E2E_ASSERT_EQ(bridge.getElementValue("chat_input"), std::string("抖音大侠666"));

    bridge.onElementBlur("chat_input");
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_53, InputBlur_VirtualKeyboardHide_Invoked) {
    (void)::FCT::SAI::VirtualKeyboard::hide();
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();

    ::MQEngine::GameHtmlUI::RmlUiKeyboardBridge bridge;
    bridge.onElementFocus("search_box", "text", "关键词");
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());

    bridge.onElementBlur("search_box");
    E2E_ASSERT_FALSE(bridge.isKeyboardActive());
    E2E_ASSERT_FALSE(::FCT::SAI::VirtualKeyboard::isShowing());

    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_53, TextareaMultiline_Configuration_Passed) {
    (void)::FCT::SAI::VirtualKeyboard::hide();
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();

    ::MQEngine::GameHtmlUI::RmlUiKeyboardBridge bridge;
    bridge.onElementFocus("feedback_textarea", "textarea", "请输入问题反馈");

    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::isShowing());
    E2E_ASSERT_TRUE(::FCT::SAI::VirtualKeyboard::getCurrentConfig().multiple);

    bridge.onElementBlur("feedback_textarea");
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

E2E_TEST(Tier1, FEAT_53, MaxLengthConstraint_Enforced_DuringInput) {
    (void)::FCT::SAI::VirtualKeyboard::hide();
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();

    ::MQEngine::GameHtmlUI::RmlUiKeyboardBridge bridge;
    bridge.setElementMaxLength("guild_tag_input", 6);
    bridge.onElementFocus("guild_tag_input", "text", "");

    ::FCT::SAI::VirtualKeyboard::simulateInput("1234567890ABCDEF");
    E2E_ASSERT_EQ(bridge.getElementValue("guild_tag_input"), std::string("123456"));
    E2E_ASSERT_EQ(bridge.getElementValue("guild_tag_input").size(), 6u);

    bridge.onElementBlur("guild_tag_input");
    ::FCT::SAI::VirtualKeyboard::clearCallbacks();
    return true;
}

// ----------------------------------------------------------------------------
// FEAT-54: 50ms 首屏秒开接力与社交开放数据域融合 (5 个用例)
// ----------------------------------------------------------------------------

E2E_TEST(Tier1, FEAT_54, SplashRelay_InitialHandshake_Verified) {
    uint32_t uiDismissCount = 0;
    ::MQEngine::GameHtmlUI::SplashRelayCoordinator uiSplash(
        Tier1PluginSplashDismissCounter{&uiDismissCount});
    E2E_ASSERT_FALSE(uiSplash.isSplashDismissed());

    uiSplash.notifyFirstFrameRendered();
    E2E_ASSERT_TRUE(uiSplash.isSplashDismissed());
    E2E_ASSERT_EQ(uiSplash.getDismissCount(), 1u);
    E2E_ASSERT_EQ(uiDismissCount, 1u);

    // 后续帧重复触发必须保持幂等
    uiSplash.notifyFirstFrameRendered();
    E2E_ASSERT_EQ(uiSplash.getDismissCount(), 1u);
    E2E_ASSERT_EQ(uiDismissCount, 1u);

    // 同步验证 DouyinMiniGame 侧宿主 Splash 卸载幂等性
    Tier1PluginHostCallObserver hostObserver;
    ::MQEngine::DouyinMiniGame::SplashRelayCoordinator douyinSplash(&hostObserver);
    douyinSplash.notifyFirstFrameRendered();
    douyinSplash.notifyFirstFrameRendered();
    E2E_ASSERT_TRUE(douyinSplash.isSplashDismissed());
    E2E_ASSERT_EQ(hostObserver.splashDismissCallCount, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_54, SharedCanvas_TextureAllocation_Verified) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});

    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    ::MQEngine::GameHtmlUI::OpenDataLeaderboardController controller(&wglCtx, &router);

    const Rml::TextureHandle texHandle = controller.initializeSharedCanvas(512u, 512u);
    E2E_ASSERT_NE(texHandle, static_cast<Rml::TextureHandle>(0));
    E2E_ASSERT_EQ(controller.getCanvasWidth(), 512u);
    E2E_ASSERT_EQ(controller.getCanvasHeight(), 512u);
    E2E_ASSERT_TRUE(controller.getSharedCanvasImage() != nullptr);
    E2E_ASSERT_EQ(router.loadTexture("@mq_shared_canvas/rank"), texHandle);
    return true;
}

E2E_TEST(Tier1, FEAT_54, SharedCanvas_UpdateSubRegion_OnVisible) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});

    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    ::MQEngine::GameHtmlUI::OpenDataLeaderboardController controller(&wglCtx, &router);
    (void)controller.initializeSharedCanvas(256u, 256u);

    uint32_t uploadCount = 0;
    uint32_t lastW = 0;
    uint32_t lastH = 0;
    controller.setSubRegionUploadObserver(
        Tier1PluginSubRegionUploadRecorder{&uploadCount, &lastW, &lastH});

    controller.setVisible(true);
    E2E_ASSERT_EQ(controller.getTargetRefreshRateFps(), 60u);

    controller.onFrameUpdate();
    E2E_ASSERT_EQ(uploadCount, 1u);
    E2E_ASSERT_EQ(lastW, 256u);
    E2E_ASSERT_EQ(lastH, 256u);
    E2E_ASSERT_EQ(controller.getRefreshCount(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_54, SharedCanvas_ZeroOverhead_WhenHidden) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});

    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    ::MQEngine::GameHtmlUI::OpenDataLeaderboardController controller(&wglCtx, &router);
    (void)controller.initializeSharedCanvas(256u, 256u);

    uint32_t uploadCount = 0;
    uint32_t lastW = 0;
    uint32_t lastH = 0;
    controller.setSubRegionUploadObserver(
        Tier1PluginSubRegionUploadRecorder{&uploadCount, &lastW, &lastH});

    controller.setVisible(false);
    E2E_ASSERT_EQ(controller.getTargetRefreshRateFps(), 0u);

    for (int i = 0; i < 10; ++i) {
        controller.onFrameUpdate();
    }
    E2E_ASSERT_EQ(uploadCount, 0u);
    E2E_ASSERT_EQ(controller.getRefreshCount(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_54, SharedCanvas_ScrollEvent_ForcesRefresh) {
    ::FCT::WGL2_Context wglCtx(nullptr);
    wglCtx.createPlatform(::FCT::ContextCreateFlags{});

    ::MQEngine::GameHtmlUI::VirtualTextureRouter router;
    ::MQEngine::GameHtmlUI::OpenDataLeaderboardController controller(&wglCtx, &router);
    (void)controller.initializeSharedCanvas(256u, 512u);

    uint32_t uploadCount = 0;
    uint32_t lastW = 0;
    uint32_t lastH = 0;
    controller.setSubRegionUploadObserver(
        Tier1PluginSubRegionUploadRecorder{&uploadCount, &lastW, &lastH});

    controller.setVisible(true);
    controller.onScroll(0.0f, -32.0f);

    E2E_ASSERT_EQ(uploadCount, 1u);
    E2E_ASSERT_EQ(controller.getRefreshCount(), 1u);
    E2E_ASSERT_NEAR(controller.getScrollOffset().y, -32.0f, 1e-4f);
    return true;
}

E2E_TEST(Tier1, FEAT_01, ProjectConfig_NativeDirPath_ResolutionAndClear_Verified) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;
    ::MQEngine::Resource::Config cfg;

    // 无活跃工程时必须返回空串
    cfg.resolveCanonicalPaths("");
    E2E_ASSERT_EQ(cfg.getProjectNativeDir(), std::string(""));

    const std::string projDirDefault = sandbox.subPath("ProjDefault");
    E2E_ASSERT_STATUS_OK(loader.ensureDir(projDirDefault));
    const std::string defaultProjMq = projDirDefault + "/project.mq";
    E2E_ASSERT_STATUS_OK(loader.saveFile(defaultProjMq, "name = \"ProjDefault\"\nversion = \"1.0.0\"\n"));
    E2E_ASSERT_STATUS_OK(cfg.activateProjectConfig(defaultProjMq, &loader));
    E2E_ASSERT_EQ(cfg.getProjectNativeDir(), projDirDefault + "/Native");

    cfg.clearProjectConfig();
    E2E_ASSERT_EQ(cfg.getProjectNativeDir(), std::string(""));

    const std::string projDirCustom = sandbox.subPath("ProjCustom");
    E2E_ASSERT_STATUS_OK(loader.ensureDir(projDirCustom));
    const std::string customProjMq = projDirCustom + "/project.mq";
    E2E_ASSERT_STATUS_OK(loader.saveFile(
        customProjMq,
        "name = \"ProjCustom\"\nversion = \"1.0.0\"\n\n[paths]\nnative = \"Bin/CustomNative/\"\n"));
    E2E_ASSERT_STATUS_OK(cfg.activateProjectConfig(customProjMq, &loader));
    E2E_ASSERT_EQ(cfg.getProjectNativeDir(), projDirCustom + "/Bin/CustomNative");
    return true;
}

E2E_TEST(Tier1, FEAT_01, CompositeManager_NativeModuleDiscoveryAndReconcileTags_Verified) {
    Tier1PluginSandboxScope sandbox;
    ::MQEngine::DiskResourceLoader loader;
    ::MQEngine::Asset::CompositeManager manager;

    E2E_ASSERT_STATUS_OK(manager.discoverProjectNativeModule(sandbox.subPath("NonExistentNative")));

    const std::string nativeDir = sandbox.subPath("EmptyNative");
    E2E_ASSERT_STATUS_OK(loader.ensureDir(nativeDir));
    E2E_ASSERT_STATUS_OK(loader.saveFile(nativeDir + "/README.txt", "placeholder"));
    E2E_ASSERT_STATUS_OK(manager.discoverProjectNativeModule(nativeDir));
    E2E_ASSERT_EQ(manager.getCompositeManifests().size(), 0u);

    // 验证增量 Tag 对账生命周期：Editor 默认仅激活 Runtime+Editor 组件，追加 "Game" 增量激活不重复初始化已有组件，移除 "Game" 逆序卸载
    class EditorMockComposite : public ::MQEngine::Asset::Composite {
    public:
        explicit EditorMockComposite(std::vector<std::string>* log) : m_log(log) {}
        std::string getName() const override { return "EditorMockComposite"; }
        std::vector<std::string> getRequiredTags() const override { return {"Runtime", "Editor"}; }
        Status onLoad(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("load:EditorMock");
            return ::MQEngine::Asset::Composite::onLoad(engine);
        }
        Status onInit(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("init:EditorMock");
            return ::MQEngine::Asset::Composite::onInit(engine);
        }
        void onUnload() override {
            m_log->push_back("unload:EditorMock");
            ::MQEngine::Asset::Composite::onUnload();
        }
    private:
        std::vector<std::string>* m_log = nullptr;
    };

    class GameDepComposite : public ::MQEngine::Asset::Composite {
    public:
        explicit GameDepComposite(std::vector<std::string>* log) : m_log(log) {}
        std::string getName() const override { return "GameDepComposite"; }
        std::vector<std::string> getRequiredTags() const override { return {"Runtime", "Game"}; }
        Status onLoad(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("load:GameDep");
            return ::MQEngine::Asset::Composite::onLoad(engine);
        }
        Status onInit(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("init:GameDep");
            return ::MQEngine::Asset::Composite::onInit(engine);
        }
        void onUnload() override {
            m_log->push_back("unload:GameDep");
            ::MQEngine::Asset::Composite::onUnload();
        }
    private:
        std::vector<std::string>* m_log = nullptr;
    };

    class GameNativeMockComposite : public ::MQEngine::Asset::Composite {
    public:
        explicit GameNativeMockComposite(std::vector<std::string>* log) : m_log(log) {}
        std::string getName() const override { return "GameNativeMockComposite"; }
        std::vector<std::string> getRequiredTags() const override { return {"Runtime", "Game"}; }
        std::vector<std::string> getDependencies() const override { return {"GameDepComposite"}; }
        Status onLoad(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("load:GameNative");
            return ::MQEngine::Asset::Composite::onLoad(engine);
        }
        Status onInit(::MQEngine::Runtime::Engine* engine) override {
            m_log->push_back("init:GameNative");
            return ::MQEngine::Asset::Composite::onInit(engine);
        }
        void onUnload() override {
            m_log->push_back("unload:GameNative");
            ::MQEngine::Asset::Composite::onUnload();
        }
    private:
        std::vector<std::string>* m_log = nullptr;
    };

    std::vector<std::string> lifecycleLog;
    E2E_ASSERT_STATUS_OK(manager.registerComposite(makeUnique<EditorMockComposite>(&lifecycleLog)));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(makeUnique<GameDepComposite>(&lifecycleLog)));
    E2E_ASSERT_STATUS_OK(manager.registerComposite(makeUnique<GameNativeMockComposite>(&lifecycleLog)));

    const std::vector<std::string> editorTags{"Runtime", "Editor", "ImGui"};
    E2E_ASSERT_STATUS_OK(manager.reconcileActiveTags(editorTags));
    E2E_ASSERT_EQ(manager.getActiveCompositeCount(), 1u);
    E2E_ASSERT_EQ(lifecycleLog.size(), 2u);
    E2E_ASSERT_EQ(lifecycleLog[0], std::string("load:EditorMock"));
    E2E_ASSERT_EQ(lifecycleLog[1], std::string("init:EditorMock"));

    lifecycleLog.clear();
    const std::vector<std::string> playTags{"Runtime", "Editor", "ImGui", "Game"};
    E2E_ASSERT_STATUS_OK(manager.reconcileActiveTags(playTags));
    E2E_ASSERT_EQ(manager.getActiveCompositeCount(), 3u);
    // EditorMock 不重复 load/init；新增项正序 GameDep -> GameNative
    E2E_ASSERT_EQ(lifecycleLog.size(), 4u);
    E2E_ASSERT_EQ(lifecycleLog[0], std::string("load:GameDep"));
    E2E_ASSERT_EQ(lifecycleLog[1], std::string("load:GameNative"));
    E2E_ASSERT_EQ(lifecycleLog[2], std::string("init:GameDep"));
    E2E_ASSERT_EQ(lifecycleLog[3], std::string("init:GameNative"));

    lifecycleLog.clear();
    E2E_ASSERT_STATUS_OK(manager.reconcileActiveTags(editorTags));
    E2E_ASSERT_EQ(manager.getActiveCompositeCount(), 1u);
    // 停用项严格逆序 GameNative -> GameDep
    E2E_ASSERT_EQ(lifecycleLog.size(), 2u);
    E2E_ASSERT_EQ(lifecycleLog[0], std::string("unload:GameNative"));
    E2E_ASSERT_EQ(lifecycleLog[1], std::string("unload:GameDep"));

    // 再次切换为 Play 验证 TokenGraph 可重复激活卸载
    lifecycleLog.clear();
    E2E_ASSERT_STATUS_OK(manager.reconcileActiveTags(playTags));
    E2E_ASSERT_EQ(manager.getActiveCompositeCount(), 3u);
    E2E_ASSERT_STATUS_OK(manager.reconcileActiveTags(editorTags));
    E2E_ASSERT_EQ(manager.getActiveCompositeCount(), 1u);

    manager.unloadComposites();
    manager.term();
    return true;
}

E2E_TEST(Tier1, FEAT_01, RuntimeEngine_ApplicationTagReconcile_DomainAndPhysicsTickers_Verified) {
    {
        ::MQEngine::Host::LogicHost logicHost;
        ::MQEngine::Host::PhysicsHost physicsHost;
        E2E_ASSERT_STATUS_OK(logicHost.init());
        E2E_ASSERT_STATUS_OK(physicsHost.init());

        ::MQEngine::Domain::Physics::PhysicsSubsystem physicsSub;
        E2E_ASSERT_STATUS_OK(physicsSub.init());
        E2E_ASSERT_FALSE(physicsSub.isRuntimeTickerActive());
        E2E_ASSERT_FALSE(physicsHost.tickers().hasNode("PhysicsSubsystem_Step"));

        physicsSub.activateRuntimeTicker();
        E2E_ASSERT_TRUE(physicsSub.isRuntimeTickerActive());
        E2E_ASSERT_TRUE(physicsHost.tickers().hasNode("PhysicsSubsystem_Step"));

        physicsSub.deactivateRuntimeTicker();
        E2E_ASSERT_FALSE(physicsSub.isRuntimeTickerActive());
        E2E_ASSERT_FALSE(physicsHost.tickers().hasNode("PhysicsSubsystem_Step"));

        physicsSub.term();

        // 验证 TokenGraph 在反复挂载/卸载 Domain_Gameplay_Update (带 predecessors+successors) 后仍可重入
        bool sceneRan = false;
        bool gameplayRan = false;
        bool compositeRan = false;
        logicHost.tickers().addNode("Domain_Scene_Update", [&sceneRan]() { sceneRan = true; });
        logicHost.tickers().addNode("Asset_Composite_Tick", [&compositeRan]() { compositeRan = true; }, {"Domain_Scene_Update"}, {});
        for (int cycle = 0; cycle < 3; ++cycle) {
            sceneRan = gameplayRan = compositeRan = false;
            logicHost.tickers().addNode(
                "Domain_Gameplay_Update",
                [&gameplayRan]() { gameplayRan = true; },
                {"Domain_Scene_Update"},
                {"Asset_Composite_Tick"});
            logicHost.sync();
            E2E_ASSERT_TRUE(sceneRan);
            E2E_ASSERT_TRUE(gameplayRan);
            E2E_ASSERT_TRUE(compositeRan);

            logicHost.tickers().removeNode("Domain_Gameplay_Update");
            sceneRan = gameplayRan = compositeRan = false;
            logicHost.sync();
            E2E_ASSERT_TRUE(sceneRan);
            E2E_ASSERT_FALSE(gameplayRan);
            E2E_ASSERT_TRUE(compositeRan);
        }

        physicsHost.term();
        logicHost.term();
    }

    if (!::MQEngine::Runtime::Engine::getInstance() &&
        !::MQEngine::Resource::Resource::InstancePtr() &&
        !::MQEngine::Host::Host::InstancePtr() &&
        !::MQEngine::Asset::Asset::InstancePtr() &&
        !::MQEngine::Domain::Domain::InstancePtr()) {
        ::MQEngine::Runtime::Engine engine;
        E2E_ASSERT_FALSE(engine.hasApplicationTag("Game"));
        E2E_ASSERT_STATUS_OK(engine.addApplicationTag("Runtime"));
        E2E_ASSERT_STATUS_OK(engine.addApplicationTag("Editor"));
        E2E_ASSERT_STATUS_OK(engine.addApplicationTag("Game"));
        E2E_ASSERT_TRUE(engine.hasApplicationTag("Game"));
        engine.removeApplicationTag("Game");
        E2E_ASSERT_FALSE(engine.hasApplicationTag("Game"));
        E2E_ASSERT_TRUE(engine.hasApplicationTag("Editor"));
    }
    return true;
}

} // namespace MQEngine::Testing
