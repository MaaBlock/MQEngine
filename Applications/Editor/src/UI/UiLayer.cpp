#include "UiLayer.h"
#include "../core/Global.h"
#include "../AssetsManager/ModelManager.h"
#include "../AssetsManager/SceneManager.h"
#include "../AssetsManager/ShaderSnippetBrowser.h"
#include "../AssetsManager/ShaderGraphBrowser.h"

#define TEXT(str) (const char*)u8##str

using namespace FCT;

namespace MQEngine {

    void UiLayer::init() {
        ZoneScoped();
        auto* logWindow = new LogWindow();

        initImguiPlugin();
        initEditorUiPlugin();
        setupCustomDarkTheme();
        ImNodes::StyleColorsDark();

        initEditorSubsystems();
        registerCoreUiModules();
        registerLegacyPanelModules();

        registerModule<UiLegacyPanelModule<LogWindow>>("LogWindowModule", logWindow, true);
        registerDefaultDockWindows();
    }

    void UiLayer::initImguiPlugin() {
        auto* engine = Runtime::Engine::getInstance();
        auto* pm = engine ? engine->host->getPluginManager() : nullptr;
        if (!pm) {
            return;
        }

        Status pluginSt = pm->resolveAndPreloadPlugin("ImguiPlugin", "", engine);
        if (!pluginSt.ok()) {
            spdlog::error("UiLayer: 预加载 ImguiPlugin 失败: {}", pluginSt.message());
        }
        Status uiPluginSt = pm->resolveAndPreloadPlugin("EditorUiPlugin", "", engine);
        if (!uiPluginSt.ok()) {
            spdlog::error("UiLayer: 预加载 EditorUiPlugin 失败: {}", uiPluginSt.message());
        }
        Status lbmSt = pm->resolveAndPreloadPlugin("HomeLBM", "", engine);
        if (!lbmSt.ok()) {
            spdlog::warn("UiLayer: 预加载 HomeLBM 插件失败: {}", lbmSt.message());
        }

        Status refreshSt = pm->refreshComposites(engine);
        if (!refreshSt.ok()) {
            spdlog::error("UiLayer: Composite TokenGraph 拓扑排序激发失败: {}", refreshSt.message());
        }

        m_imguiPlugin = pm->getPluginAs<ImguiPlugin>("ImguiPlugin");
        if (m_imguiPlugin) {
            m_imguiPlugin->setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
            m_imguiPlugin->syncCallerContext();
            m_imguiCtx = m_imguiPlugin->getImguiContext();
            g_editorGlobal.imguiModule = m_imguiPlugin->getImguiModule();
        }
        g_editorGlobal.imguiContext = m_imguiCtx;
    }

    void UiLayer::initEditorUiPlugin() {
        auto* engine = Runtime::Engine::getInstance();
        if (engine && engine->host && engine->host->getPluginManager()) {
            m_editorUiService = engine->host->getPluginManager()->getService<IEditorUiService>();
        }
    }

    void UiLayer::registerDefaultDockWindows() {
        if (!m_editorUiService) {
            return;
        }
        m_editorUiService->registerDockWindow(TEXT("场景实体查看器"), EditorDockSlot::Left, true, "EditorScene");
        m_editorUiService->registerDockWindow(TEXT("相机控制"), EditorDockSlot::LeftBottom, true, "EditorScene");
#ifdef FCT_DEBUG
        m_editorUiService->registerDockWindow(TEXT("编辑器内存分析"), EditorDockSlot::LeftBottom, false, "EditorDiagnostics");
#endif
        m_editorUiService->registerDockWindow(TEXT("实体检查器"), EditorDockSlot::Right, true, "EditorScene");
        m_editorUiService->registerDockWindow(TEXT("细节"), EditorDockSlot::Right, false, "EditorAsset");
        m_editorUiService->registerDockWindow(TEXT("HOME-LBM 自适应流体工作台"), EditorDockSlot::Right, false, "HomeLBM");
        m_editorUiService->registerDockWindow(TEXT("贴图: SceneColorTarget"), EditorDockSlot::RightBottom, true, "EditorShader");
        m_editorUiService->registerDockWindow(TEXT("贴图: PosTarget"), EditorDockSlot::RightBottom, false, "EditorShader");
        m_editorUiService->registerDockWindow(TEXT("贴图: RetTarget"), EditorDockSlot::RightBottom, false, "EditorShader");
        m_editorUiService->registerDockWindow(TEXT("内容浏览器"), EditorDockSlot::BottomLeft, true, "EditorAsset");
        m_editorUiService->registerDockWindow(TEXT("脚本管理器"), EditorDockSlot::BottomLeft, false, "EditorAsset");
        m_editorUiService->registerDockWindow(TEXT("日志"), EditorDockSlot::BottomRight, true, "EditorDiagnostics");
        m_editorUiService->registerDockWindow(TEXT("场景视口"), EditorDockSlot::Center, true, "EditorScene");
        m_editorUiService->registerDockWindow("Shader Graph", EditorDockSlot::Center, false, "EditorShader");
        m_editorUiService->registerDockWindow(TEXT("着色器编辑器"), EditorDockSlot::Center, false, "EditorShader");
        m_editorUiService->registerDockWindow("RenderGraph", EditorDockSlot::Center, false, "EditorShader");
    }

    void UiLayer::requestResetLayout() {
        if (m_editorUiService) {
            m_editorUiService->requestResetLayout();
        } else {
            m_needResetLayout = true;
        }
    }

    void UiLayer::initEditorSubsystems() {
        m_editorCameraManager = new EditorCameraManager();
        g_editorGlobal.cameraManager = m_editorCameraManager;
    }

    void UiLayer::registerCoreUiModules() {
        registerModules<
            MainMenuBarUiModule,
            SceneViewportUiModule,
            CameraControlUiModule,
            SceneEntityUiModule,
            EntityInspectorUiModule
        >();
    }

    void UiLayer::registerLegacyPanelModules() {
        m_graphView = FCT_NEW(GraphViewInsight, m_imguiCtx);
        auto* passGenerator = FCT_NEW(RenderGraphViewer, g_editorGlobal.ctx, g_editorGlobal.wnd);
        auto* scriptManager = new ScriptManager();
        m_inspector = new Inspector();
        g_editorGlobal.inspector = m_inspector;
        auto* shaderEditor = new ShaderEditor();
        m_shaderGraph = new ShaderGraph();
        m_contentBrowser = new ContentBrowser();
        g_editorGlobal.contentBrowser = m_contentBrowser;

        m_contentBrowser->registerProvider<SceneManager>();
        m_contentBrowser->registerProvider<ModelManager>();
        m_contentBrowser->registerProvider<ShaderSnippetBrowser>();
        m_contentBrowser->registerProvider<ShaderGraphBrowser>(m_shaderGraph);

        registerModule<UiLegacyPanelModule<GraphViewInsight>>("GraphViewInsightModule", m_graphView, true);
        registerModule<UiLegacyPanelModule<RenderGraphViewer>>("RenderGraphViewerModule", passGenerator, true);
        registerModule<UiLegacyPanelModule<ScriptManager>>("ScriptManagerModule", scriptManager, true);
        registerModule<UiLegacyPanelModule<Inspector>>("InspectorModule", m_inspector, true);
        registerModule<UiLegacyPanelModule<ShaderEditor>>("ShaderEditorModule", shaderEditor, true);
        registerModule<UiLegacyPanelModule<ShaderGraph>>("ShaderGraphModule", m_shaderGraph, true);
        registerModule<UiLegacyPanelModule<ContentBrowser>>("ContentBrowserModule", m_contentBrowser, true);
    }

    void UiLayer::setupCustomDarkTheme() {
        if (m_imguiPlugin) {
            m_imguiPlugin->applyDefaultDarkTheme();
        }
    }

    void UiLayer::term() {
        for (auto& modulePtr : m_modules) {
            if (modulePtr) {
                modulePtr->onTerm(m_uiContext);
            }
        }
        m_modules.clear();
        m_uiContext.clearBindings();

        delete m_editorCameraManager;
        m_editorCameraManager = nullptr;
        m_graphView = nullptr;
        m_shaderGraph = nullptr;
        m_inspector = nullptr;
        m_contentBrowser = nullptr;
    }

    void UiLayer::registerRenderCallback(
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& callback) {
        if (m_imguiPlugin) {
            m_imguiPlugin->registerKeepImageCallback([this](RenderGraph* graph) {
                if (m_graphView) {
                    m_graphView->keepImage(graph);
                }
            });
        }

        callback.subscribe<RenderCallBack::WindowClose>(
            [this](const RenderCallBack::WindowClose&) {
                m_uiContext.emitImmediate(WindowCloseSaveSignal{});
            });
    }

    void UiLayer::logicTick() {
        ZoneScoped();
        if (!m_imguiCtx) {
            return;
        }

        // Note: 严格在逻辑线程消费上一帧 UI 挂起的全部信号（实体创建、组件增删改等），
        // 避免在 ImguiPass 渲染提交线程中跨线程修改 entt::registry。
        m_uiContext.dispatchPendingSignals();
        m_uiContext.syncAllStates();

        m_imguiCtx->push([this]() {
            ZoneScopedN("UiLayer::RenderUI");
            if (m_imguiPlugin) {
                m_imguiPlugin->syncCallerContext();
            }

            updateDockSpaceLayout();
            renderAllModules();
            applyDefaultTabFocusIfNeeded();
        });
    }

    void UiLayer::updateDockSpaceLayout() {
        if (m_editorUiService) {
            m_editorUiService->applyDockSpaceLayout("MQEngine");
            return;
        }
        ImGuiID dockspaceId = ImguiContext::createMainDockSpace("MQEngine");
        ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
        bool logNotDocked = false;
        if (!m_layoutChecked) {
            m_layoutChecked = true;
            ImGuiWindowSettings* logSettings = ImGui::FindWindowSettingsByID(ImHashStr(TEXT("日志")));
            logNotDocked = (!logSettings || logSettings->DockId == 0);
        }
        if (m_needResetLayout || !dockNode || (!dockNode->IsSplitNode() && dockNode->Windows.empty()) || logNotDocked) {
            m_needResetLayout = false;
            setupDefaultLayout(dockspaceId);
        }
    }

    void UiLayer::setupDefaultLayout(ImGuiID dockspaceId) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = 0;
        ImGuiID dockRight = 0;
        ImGuiID dockBottom = 0;
        ImGuiID dockBottomLeft = 0;
        ImGuiID dockBottomRight = 0;
        ImGuiID dockLeftBottom = 0;
        ImGuiID dockRightBottom = 0;

        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, &dockLeft, &dockMain);
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.30f, &dockRight, &dockMain);
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, &dockBottom, &dockMain);
        ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Left, 0.55f, &dockBottomLeft, &dockBottomRight);
        ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.38f, &dockLeftBottom, &dockLeft);
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.36f, &dockRightBottom, &dockRight);

        ImGui::DockBuilderDockWindow(TEXT("场景实体查看器"), dockLeft);
        ImGui::DockBuilderDockWindow(TEXT("相机控制"), dockLeftBottom);
#ifdef FCT_DEBUG
        ImGui::DockBuilderDockWindow(TEXT("编辑器内存分析"), dockLeftBottom);
#endif

        ImGui::DockBuilderDockWindow(TEXT("实体检查器"), dockRight);
        ImGui::DockBuilderDockWindow(TEXT("细节"), dockRight);
        ImGui::DockBuilderDockWindow(TEXT("HOME-LBM 自适应流体工作台"), dockRight);

        ImGui::DockBuilderDockWindow(TEXT("贴图: SceneColorTarget"), dockRightBottom);
        ImGui::DockBuilderDockWindow(TEXT("贴图: PosTarget"), dockRightBottom);
        ImGui::DockBuilderDockWindow(TEXT("贴图: RetTarget"), dockRightBottom);

        ImGui::DockBuilderDockWindow(TEXT("内容浏览器"), dockBottomLeft);
        ImGui::DockBuilderDockWindow(TEXT("脚本管理器"), dockBottomLeft);
        ImGui::DockBuilderDockWindow(TEXT("日志"), dockBottomRight);

        ImGui::DockBuilderDockWindow(TEXT("场景视口"), dockMain);
        ImGui::DockBuilderDockWindow("Shader Graph", dockMain);
        ImGui::DockBuilderDockWindow(TEXT("着色器编辑器"), dockMain);
        ImGui::DockBuilderDockWindow("RenderGraph", dockMain);

        ImGui::DockBuilderFinish(dockspaceId);
        m_focusDefaultTabs = true;
    }

    void UiLayer::renderAllModules() {
        for (auto& modulePtr : m_modules) {
            if (modulePtr && modulePtr->isEnabled()) {
                modulePtr->onRender(m_uiContext);
            }
        }
    }

    void UiLayer::applyDefaultTabFocusIfNeeded() {
        if (m_editorUiService) {
            m_editorUiService->applyDefaultTabFocusIfNeeded();
            return;
        }
        if (!m_focusDefaultTabs) {
            return;
        }
        m_focusDefaultTabs = false;
        ImGui::SetWindowFocus(TEXT("相机控制"));
        ImGui::SetWindowFocus(TEXT("贴图: SceneColorTarget"));
        ImGui::SetWindowFocus(TEXT("实体检查器"));
        ImGui::SetWindowFocus(TEXT("内容浏览器"));
        ImGui::SetWindowFocus(TEXT("日志"));
        ImGui::SetWindowFocus(TEXT("场景实体查看器"));
        ImGui::SetWindowFocus(TEXT("场景视口"));
        if (ImGui::GetIO().IniFilename) {
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        }
    }

} // namespace MQEngine
