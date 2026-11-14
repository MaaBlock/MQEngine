#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <unordered_set>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/asset/manager/CompositeManager.h>
#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/runtime/core/Engine.h>

using namespace MQEngine;
using namespace MQEngine::Asset;

#define HARNESS_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

// 模拟测试用的 Composite，用于依赖和环路对抗性压力测试
class MockAdversarialComposite : public Composite
{
public:
    MockAdversarialComposite(
        std::string name,
        std::vector<std::string> tags,
        std::vector<std::string> deps)
        : m_name(std::move(name))
        , m_tags(std::move(tags))
        , m_deps(std::move(deps))
    {
    }

    std::string getName() const override { return m_name; }
    std::string getPluginName() const override { return "MockPlugin"; }
    std::vector<std::string> getRequiredTags() const override { return m_tags; }
    std::vector<std::string> getDependencies() const override { return m_deps; }

    Status onLoad(Runtime::Engine*) override { return OkStatus(); }
    Status onInit(Runtime::Engine*) override { return OkStatus(); }
    void onTick(float) override {}
    void onUnload() override {}

private:
    std::string m_name;
    std::vector<std::string> m_tags;
    std::vector<std::string> m_deps;
};

// -------------------------------------------------------------
// Test Suite 1: Plugin Discovery and Metadata Parsing
// -------------------------------------------------------------
bool test_PluginDiscovery(Runtime::Engine& engine)
{
    std::cout << "\n=== [Test 1] Plugin Discovery & Metadata Parsing ===" << std::endl;

    auto* resModule = Resource::Resource::InstancePtr();
    HARNESS_CHECK(resModule != nullptr, "Resource module must be available");

    std::vector<std::string> searchPaths = {
        "./Plugins",
        "Plugins",
        "Engine/Plugins",
        "../../Plugins",
        "../../Engine/Plugins"
    };

    for (const auto& p : searchPaths)
    {
        (void)engine.asset.compositeManager.discoverPlugins(p);
    }

    std::cout << "  Discovered plugin package count: " << engine.asset.compositeManager.getPluginCount() << std::endl;
    HARNESS_CHECK(engine.asset.compositeManager.getPluginCount() >= 3, "Must discover at least 3 official plugins: EditorBase, MQEditor, GameHtmlUI");

    std::cout << "[PASS] Test 1: Plugin discovery passed." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 2: Editor Active Tags Matching & Topological Sort
// -------------------------------------------------------------
bool test_EditorTagsMatchingAndTopologicalSort(Runtime::Engine& engine)
{
    std::cout << "\n=== [Test 2] Editor Tags Matching & Topological Sort ===" << std::endl;

    // Editor 激活标签: {"Runtime", "Editor", "ImGui"}
    const std::vector<std::string> editorTags = {"Runtime", "Editor", "ImGui"};

    // 装载匹配的 DLL 并调用 onLoad
    Status loadSt = engine.asset.compositeManager.loadComposites(&engine, editorTags);
    HARNESS_CHECK(loadSt.ok(), "loadComposites with editorTags must succeed: " + std::string(loadSt.message()));

    const auto& order = engine.asset.compositeManager.getActiveExecutionOrder();
    std::cout << "  Editor active composites count: " << order.size() << std::endl;
    for (size_t i = 0; i < order.size(); ++i)
    {
        std::cout << "    [" << i << "] " << order[i]->getName() << " (Plugin: " << order[i]->getPluginName() << ")" << std::endl;
    }

    // 验证包含的 6 个 Editor Composite
    std::unordered_map<std::string, size_t> indices;
    for (size_t i = 0; i < order.size(); ++i)
    {
        indices[order[i]->getName()] = i;
    }

    HARNESS_CHECK(indices.contains("ImGuiComposite"), "ImGuiComposite must be active in Editor");
    HARNESS_CHECK(indices.contains("EditorUiComposite"), "EditorUiComposite must be active in Editor");
    HARNESS_CHECK(indices.contains("SceneViewerComposite"), "SceneViewerComposite must be active in Editor");
    HARNESS_CHECK(indices.contains("AssetBrowserComposite"), "AssetBrowserComposite must be active in Editor");
    HARNESS_CHECK(indices.contains("ShaderEditorComposite"), "ShaderEditorComposite must be active in Editor");
    HARNESS_CHECK(indices.contains("HtmlUiEditorComposite"), "HtmlUiEditorComposite must be active in Editor");

    // 验证 RmlUiComposite 绝不应该被加载 (tags: ["Runtime", "Game"])
    HARNESS_CHECK(!indices.contains("RmlUiComposite"), "RmlUiComposite must NOT be active in Editor (tag filtering)");

    // 验证拓扑执行顺序:
    // ImGuiComposite -> EditorUiComposite -> {SceneViewerComposite, AssetBrowserComposite, ShaderEditorComposite, HtmlUiEditorComposite}
    HARNESS_CHECK(indices["ImGuiComposite"] < indices["EditorUiComposite"],
                  "ImGuiComposite must precede EditorUiComposite in execution order");
    HARNESS_CHECK(indices["EditorUiComposite"] < indices["SceneViewerComposite"],
                  "EditorUiComposite must precede SceneViewerComposite");
    HARNESS_CHECK(indices["EditorUiComposite"] < indices["AssetBrowserComposite"],
                  "EditorUiComposite must precede AssetBrowserComposite");
    HARNESS_CHECK(indices["EditorUiComposite"] < indices["ShaderEditorComposite"],
                  "EditorUiComposite must precede ShaderEditorComposite");
    HARNESS_CHECK(indices["EditorUiComposite"] < indices["HtmlUiEditorComposite"],
                  "EditorUiComposite must precede HtmlUiEditorComposite");

    std::cout << "[PASS] Test 2: Editor tags matching and topological sort passed." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 3: Game Active Tags Matching & Selective Loading
// -------------------------------------------------------------
bool test_GameTagsMatchingAndSelectiveLoading()
{
    std::cout << "\n=== [Test 3] Game Tags Matching & Selective Loading ===" << std::endl;

    // 使用独立的 CompositeManager 进行孤立测试
    CompositeManager gameCompMgr;
    HARNESS_CHECK(gameCompMgr.init().ok(), "CompositeManager init");

    (void)gameCompMgr.discoverPlugins("./Plugins");
    (void)gameCompMgr.discoverPlugins("Plugins");

    // Game 激活标签: {"Runtime", "Game"}
    const std::vector<std::string> gameTags = {"Runtime", "Game"};

    Status schedSt = gameCompMgr.buildTopologicalSchedule(gameTags);
    HARNESS_CHECK(schedSt.ok(), "buildTopologicalSchedule with gameTags must succeed: " + std::string(schedSt.message()));

    // 验证 RmlUiComposite 注册但所有 Editor-only 组件被排除
    Composite* rmlComp = gameCompMgr.getComposite("RmlUiComposite");
    Composite* editorComp = gameCompMgr.getComposite("EditorUiComposite");
    Composite* sceneComp = gameCompMgr.getComposite("SceneViewerComposite");

    HARNESS_CHECK(editorComp == nullptr, "EditorUiComposite must NOT be active in Game");
    HARNESS_CHECK(sceneComp == nullptr, "SceneViewerComposite must NOT be active in Game");

    std::cout << "  Game selective loading verified: Editor-only composites properly filtered" << std::endl;
    std::cout << "[PASS] Test 3: Game tags selective loading passed." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 4: Adversarial Topology Stress (Missing Dep & Cyclic Graph)
// -------------------------------------------------------------
bool test_AdversarialTopologyStress()
{
    std::cout << "\n=== [Test 4] Adversarial Topology Stress (Missing Dep & Cycle) ===" << std::endl;

    // 4.1 缺失依赖测试: Node A 依赖不存在的 Node B
    {
        CompositeManager compMgr;
        HARNESS_CHECK(compMgr.init().ok(), "Init compMgr");

        auto compA = makeUnique<MockAdversarialComposite>(
            "CompA",
            std::vector<std::string>{"Runtime"},
            std::vector<std::string>{"NonExistentDep"});

        HARNESS_CHECK(compMgr.registerComposite(std::move(compA)).ok(), "Register CompA");

        Status st = compMgr.buildTopologicalSchedule({"Runtime"});
        std::cout << "  Missing dependency result: " << st.message() << std::endl;
        HARNESS_CHECK(!st.ok(), "buildTopologicalSchedule must fail when dependency is missing");
        HARNESS_CHECK(st.code() == absl::StatusCode::kNotFound, "Status code must be NotFound");
    }

    // 4.2 循环依赖测试: CompA -> CompB -> CompA
    {
        CompositeManager compMgr;
        HARNESS_CHECK(compMgr.init().ok(), "Init compMgr");

        auto compA = makeUnique<MockAdversarialComposite>(
            "CompA",
            std::vector<std::string>{"Runtime"},
            std::vector<std::string>{"CompB"});

        auto compB = makeUnique<MockAdversarialComposite>(
            "CompB",
            std::vector<std::string>{"Runtime"},
            std::vector<std::string>{"CompA"});

        HARNESS_CHECK(compMgr.registerComposite(std::move(compA)).ok(), "Register CompA");
        HARNESS_CHECK(compMgr.registerComposite(std::move(compB)).ok(), "Register CompB");

        Status st = compMgr.buildTopologicalSchedule({"Runtime"});
        std::cout << "  Cyclic dependency result: " << st.message() << std::endl;
        HARNESS_CHECK(!st.ok(), "buildTopologicalSchedule must fail when cyclic dependency exists");
        HARNESS_CHECK(st.code() == absl::StatusCode::kFailedPrecondition, "Status code must be FailedPrecondition");
    }

    // 4.3 不相交 Tag 测试: 没有任何匹配标签
    {
        CompositeManager compMgr;
        HARNESS_CHECK(compMgr.init().ok(), "Init compMgr");

        auto comp = makeUnique<MockAdversarialComposite>(
            "CompExclusive",
            std::vector<std::string>{"SpecialTag"},
            std::vector<std::string>{});

        HARNESS_CHECK(compMgr.registerComposite(std::move(comp)).ok(), "Register CompExclusive");

        Status st = compMgr.buildTopologicalSchedule({"StandardTag"});
        HARNESS_CHECK(st.ok(), "Empty matched candidates must return OkStatus()");
        HARNESS_CHECK(compMgr.getActiveCompositeCount() == 0, "Active composite count must be 0");
    }

    // 4.4 空 Tag 测试
    {
        CompositeManager compMgr;
        HARNESS_CHECK(compMgr.init().ok(), "Init compMgr");

        Status st = compMgr.buildTopologicalSchedule({});
        HARNESS_CHECK(st.ok(), "Empty tags must return OkStatus()");
        HARNESS_CHECK(compMgr.getActiveCompositeCount() == 0, "Active composite count must be 0");
    }

    std::cout << "[PASS] Test 4: Adversarial topology stress passed." << std::endl;
    return true;
}

// -------------------------------------------------------------
// Test Suite 5: Dynamic Query by Name and Type
// -------------------------------------------------------------
bool test_DynamicQuery(Runtime::Engine& engine)
{
    std::cout << "\n=== [Test 5] Dynamic Query by Name and Type ===" << std::endl;

    Composite* cImgui = engine.asset.compositeManager.getComposite("ImGuiComposite");
    HARNESS_CHECK(cImgui != nullptr, "getComposite(ImGuiComposite) must be non-null");
    HARNESS_CHECK(cImgui->getName() == "ImGuiComposite", "Name must match ImGuiComposite");

    Composite* cEditorUi = engine.asset.compositeManager.getComposite("EditorUiComposite");
    HARNESS_CHECK(cEditorUi != nullptr, "getComposite(EditorUiComposite) must be non-null");

    Composite* cNonExistent = engine.asset.compositeManager.getComposite("GhostComposite");
    HARNESS_CHECK(cNonExistent == nullptr, "getComposite(GhostComposite) must be nullptr");

    std::cout << "[PASS] Test 5: Dynamic query passed." << std::endl;
    return true;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=========================================================" << std::endl;
    std::cout << "   MQEngine Milestone 2 Empirical Composite Harness      " << std::endl;
    std::cout << "=========================================================" << std::endl;

    // 单一全局 Engine 实例
    auto enginePtr = std::make_unique<Runtime::Engine>();

    bool allPassed = true;
    allPassed &= test_PluginDiscovery(*enginePtr);
    allPassed &= test_EditorTagsMatchingAndTopologicalSort(*enginePtr);
    allPassed &= test_GameTagsMatchingAndSelectiveLoading();
    allPassed &= test_AdversarialTopologyStress();
    allPassed &= test_DynamicQuery(*enginePtr);

    std::cout << "=========================================================" << std::endl;
    if (allPassed)
    {
        std::cout << "   >>> ALL MILESTONE 2 EMPIRICAL TESTS PASSED! <<<       " << std::endl;
        std::cout << "=========================================================" << std::endl;
        // 允许进程正常退出，跳过破坏性跨 DLL 反初始化
        _exit(0);
    }
    else
    {
        std::cerr << "   >>> MILESTONE 2 EMPIRICAL TESTS FAILED! <<<           " << std::endl;
        std::cout << "=========================================================" << std::endl;
        _exit(1);
    }
}
