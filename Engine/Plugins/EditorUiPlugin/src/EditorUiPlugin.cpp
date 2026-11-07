#include "EditorUiPlugin.h"
#include <algorithm>

#ifdef TEXT
#undef TEXT
#endif
#define TEXT(str) (const char*)u8##str

namespace MQEngine {

using absl::FailedPreconditionError;

namespace {

/**
 * @brief 编辑器外壳与动态 DockSpace 布局组合体（前驱依赖 ImguiComposite）
 */
class EditorShellComposite : public Runtime::Composite {
public:
    explicit EditorShellComposite(EditorUiPlugin* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "EditorShellComposite",
            .requiredTags = {"Editor", "ImGui"},
            .predecessors = {"ImguiComposite"},
        };
    }

    Status onInit(Runtime::Engine* engine) override {
        if (!m_owner) {
            return FailedPreconditionError("EditorShellComposite: owner 插件指针为空");
        }
        CHECK_STATUS(m_owner->initShell(engine));
        if (engine) {
            auto* pm = engine->host->getPluginManager();
            pm->registerService<IEditorUiService>(m_owner);
            pm->registerService<EditorUiPlugin>(m_owner);
        }
        return OkStatus();
    }

    void onLogicTick(float /*dt*/) override {
        if (m_owner) {
            m_owner->tickShell();
        }
    }

    void onUnload() override {
        if (m_owner) {
            m_owner->shutdownShell();
        }
    }

private:
    EditorUiPlugin* m_owner = nullptr;
};

/**
 * @brief 编辑器场景/工程保存与顶栏菜单服务组合体（前驱依赖 EditorShellComposite）
 */
class EditorMenuAndSaveComposite : public Runtime::Composite {
public:
    explicit EditorMenuAndSaveComposite(EditorUiPlugin* owner) : m_owner(owner) {}

    void registerInfo() override {
        compositeRegisterInfo = Runtime::CompositeRegisterInfo{
            .name = "EditorMenuAndSaveComposite",
            .requiredTags = {"Editor", "ImGui"},
            .predecessors = {"EditorShellComposite"},
        };
    }

    Status onInit(Runtime::Engine* engine) override {
        if (!m_owner) {
            return FailedPreconditionError("EditorMenuAndSaveComposite: owner 插件指针为空");
        }
        return m_owner->initPersistenceAndMenu(engine);
    }

    void onUnload() override {
        if (m_owner) {
            m_owner->shutdownPersistenceAndMenu();
        }
    }

private:
    EditorUiPlugin* m_owner = nullptr;
};

} // namespace

EditorUiPlugin::EditorUiPlugin() {
    registerBuiltInDefaultDockWindows();
    emplaceComposite<EditorShellComposite>(this);
    emplaceComposite<EditorMenuAndSaveComposite>(this);
}

EditorUiPlugin::~EditorUiPlugin() {
    shutdownPersistenceAndMenu();
    shutdownShell();
}

void EditorUiPlugin::registerBuiltInDefaultDockWindows() {
    registerDockWindow(TEXT("场景实体查看器"), EditorDockSlot::Left, true, "BuiltinDefault");
    registerDockWindow(TEXT("相机控制"), EditorDockSlot::LeftBottom, true, "BuiltinDefault");
#ifdef FCT_DEBUG
    registerDockWindow(TEXT("编辑器内存分析"), EditorDockSlot::LeftBottom, false, "BuiltinDefault");
#endif
    registerDockWindow(TEXT("实体检查器"), EditorDockSlot::Right, true, "BuiltinDefault");
    registerDockWindow(TEXT("细节"), EditorDockSlot::Right, false, "BuiltinDefault");
    registerDockWindow(TEXT("HOME-LBM 自适应流体工作台"), EditorDockSlot::Right, false, "BuiltinDefault");
    registerDockWindow(TEXT("贴图: SceneColorTarget"), EditorDockSlot::RightBottom, true, "BuiltinDefault");
    registerDockWindow(TEXT("贴图: PosTarget"), EditorDockSlot::RightBottom, false, "BuiltinDefault");
    registerDockWindow(TEXT("贴图: RetTarget"), EditorDockSlot::RightBottom, false, "BuiltinDefault");
    registerDockWindow(TEXT("内容浏览器"), EditorDockSlot::BottomLeft, true, "BuiltinDefault");
    registerDockWindow(TEXT("脚本管理器"), EditorDockSlot::BottomLeft, false, "BuiltinDefault");
    registerDockWindow(TEXT("日志"), EditorDockSlot::BottomRight, true, "BuiltinDefault");
    registerDockWindow(TEXT("场景视口"), EditorDockSlot::Center, true, "BuiltinDefault");
    registerDockWindow("Shader Graph", EditorDockSlot::Center, false, "BuiltinDefault");
    registerDockWindow(TEXT("着色器编辑器"), EditorDockSlot::Center, false, "BuiltinDefault");
    registerDockWindow("RenderGraph", EditorDockSlot::Center, false, "BuiltinDefault");
}

Status EditorUiPlugin::initShell(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    if (m_engine) {
        auto* pm = m_engine->host->getPluginManager();
        m_imguiPlugin = pm->getService<ImguiPlugin>();
        if (!m_imguiPlugin) {
            auto ptr = pm->getPluginAs<ImguiPlugin>("ImguiPlugin");
            m_imguiPlugin = ptr.get();
        }
    }

    if (m_imguiPlugin) {
        m_imguiPlugin->syncCallerContext();
        m_imguiCtx = m_imguiPlugin->getImguiContext();
    }
    return OkStatus();
}

void EditorUiPlugin::tickShell() {
    if (!m_imguiCtx || m_panels.empty()) {
        return;
    }
    m_imguiCtx->push([this]() {
        if (m_imguiPlugin) {
            m_imguiPlugin->syncCallerContext();
        }
        renderRegisteredPanels();
    });
}

void EditorUiPlugin::shutdownShell() {
    m_panels.clear();
    m_imguiCtx = nullptr;
    m_imguiPlugin = nullptr;
}

Status EditorUiPlugin::initPersistenceAndMenu(Runtime::Engine* engine) {
    m_engine = engine ? engine : Runtime::Engine::getInstance();
    return OkStatus();
}

void EditorUiPlugin::shutdownPersistenceAndMenu() {
    m_menuActions.clear();
}

void EditorUiPlugin::registerDockWindow(const std::string& windowTitle,
                                        EditorDockSlot slot,
                                        bool focusOnReset,
                                        const std::string& ownerComposite) {
    if (windowTitle.empty()) {
        return;
    }
    for (auto& item : m_dockWindows) {
        if (item.windowTitle == windowTitle) {
            item.slot = slot;
            item.focusOnReset = focusOnReset;
            if (!ownerComposite.empty()) {
                item.ownerComposite = ownerComposite;
            }
            return;
        }
    }
    m_dockWindows.push_back(EditorDockWindowDesc{
        windowTitle, slot, focusOnReset, ownerComposite
    });
}

void EditorUiPlugin::registerMenuAction(const std::string& menuCategory,
                                        const std::string& itemLabel,
                                        const std::string& shortcut,
                                        std::function<void()> callback,
                                        const std::string& ownerComposite) {
    if (itemLabel.empty() || !callback) {
        return;
    }
    for (auto& action : m_menuActions) {
        if (action.menuCategory == menuCategory && action.itemLabel == itemLabel) {
            action.shortcut = shortcut;
            action.callback = std::move(callback);
            action.ownerComposite = ownerComposite;
            return;
        }
    }
    m_menuActions.push_back(EditorMenuActionDesc{
        menuCategory, itemLabel, shortcut, std::move(callback), ownerComposite
    });
}

void EditorUiPlugin::registerPanelCallback(const std::string& panelName,
                                           PanelRenderCallback callback,
                                           const std::string& ownerComposite) {
    if (panelName.empty() || !callback) {
        return;
    }
    for (auto& panel : m_panels) {
        if (panel.panelName == panelName) {
            panel.callback = std::move(callback);
            panel.ownerComposite = ownerComposite;
            return;
        }
    }
    m_panels.push_back(PanelEntry{
        panelName, std::move(callback), ownerComposite
    });
}

void EditorUiPlugin::unregisterPanelCallback(const std::string& panelName) {
    m_panels.erase(
        std::remove_if(m_panels.begin(), m_panels.end(),
                       [&](const PanelEntry& entry) { return entry.panelName == panelName; }),
        m_panels.end());
}

void EditorUiPlugin::unregisterAllByOwner(const std::string& ownerComposite) {
    if (ownerComposite.empty()) {
        return;
    }
    m_dockWindows.erase(
        std::remove_if(m_dockWindows.begin(), m_dockWindows.end(),
                       [&](const EditorDockWindowDesc& d) { return d.ownerComposite == ownerComposite; }),
        m_dockWindows.end());

    m_menuActions.erase(
        std::remove_if(m_menuActions.begin(), m_menuActions.end(),
                       [&](const EditorMenuActionDesc& a) { return a.ownerComposite == ownerComposite; }),
        m_menuActions.end());

    m_panels.erase(
        std::remove_if(m_panels.begin(), m_panels.end(),
                       [&](const PanelEntry& p) { return p.ownerComposite == ownerComposite; }),
        m_panels.end());
}

void EditorUiPlugin::applyDockSpaceLayout(const char* dockspaceName) {
    if (m_imguiPlugin) {
        m_imguiPlugin->syncCallerContext();
    }
    const char* name = (dockspaceName && dockspaceName[0] != '\0') ? dockspaceName : "MQEngine";
    ImGuiID dockspaceId = FCT::ImguiContext::createMainDockSpace(name);
    ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
    bool logNotDocked = false;
    if (!m_layoutChecked) {
        m_layoutChecked = true;
        ImGuiWindowSettings* logSettings = ImGui::FindWindowSettingsByID(ImHashStr(TEXT("日志")));
        logNotDocked = (!logSettings || logSettings->DockId == 0);
    }
    if (m_needResetLayout || !dockNode || (!dockNode->IsSplitNode() && dockNode->Windows.empty()) || logNotDocked) {
        m_needResetLayout = false;
        applyDockSpaceLayout(dockspaceId);
    }
}

void EditorUiPlugin::applyDockSpaceLayout(ImGuiID dockspaceId) {
    if (m_imguiPlugin) {
        m_imguiPlugin->syncCallerContext();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

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

    for (const auto& win : m_dockWindows) {
        ImGuiID targetNode = dockMain;
        switch (win.slot) {
            case EditorDockSlot::Center:      targetNode = dockMain; break;
            case EditorDockSlot::Left:        targetNode = dockLeft; break;
            case EditorDockSlot::LeftBottom:  targetNode = dockLeftBottom; break;
            case EditorDockSlot::Right:       targetNode = dockRight; break;
            case EditorDockSlot::RightBottom: targetNode = dockRightBottom; break;
            case EditorDockSlot::BottomLeft:  targetNode = dockBottomLeft; break;
            case EditorDockSlot::BottomRight: targetNode = dockBottomRight; break;
        }
        ImGui::DockBuilderDockWindow(win.windowTitle.c_str(), targetNode);
    }

    ImGui::DockBuilderFinish(dockspaceId);
    m_focusDefaultTabs = true;
}

void EditorUiPlugin::applyDefaultTabFocusIfNeeded() {
    if (!m_focusDefaultTabs) {
        return;
    }
    m_focusDefaultTabs = false;
    for (const auto& win : m_dockWindows) {
        if (win.focusOnReset && win.slot != EditorDockSlot::Center) {
            ImGui::SetWindowFocus(win.windowTitle.c_str());
        }
    }
    for (const auto& win : m_dockWindows) {
        if (win.focusOnReset && win.slot == EditorDockSlot::Center) {
            ImGui::SetWindowFocus(win.windowTitle.c_str());
        }
    }
    if (ImGui::GetIO().IniFilename) {
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    }
}

void EditorUiPlugin::renderRegisteredPanels() {
    for (const auto& panel : m_panels) {
        if (panel.callback) {
            panel.callback();
        }
    }
}

Status EditorUiPlugin::saveActiveScene() {
    DataManager* dm = m_engine ? m_engine->scene->getDataManager() : g_engineGlobal.dataManager;
    if (!dm) {
        return FailedPreconditionError("DataManager 未就绪");
    }
    Scene::Scene* currentScene = dm->getCurrentScene();
    if (!currentScene) {
        return NotFoundError("当前没有打开的活动场景");
    }
    CHECK_STATUS(currentScene->save());
    return OkStatus();
}

Status EditorUiPlugin::saveProjectSettings() {
    DataManager* dm = m_engine ? m_engine->scene->getDataManager() : g_engineGlobal.dataManager;
    if (dm) {
        dm->saveProjectSetting(dm->getProjectSetting());
    }

    Runtime::Engine* eng = m_engine ? m_engine : Runtime::Engine::getInstance();
    if (eng && eng->foundation->isProjectOpen()) {
        ProjectConfig cfg = eng->foundation->getCurrentConfig();
        if (dm && !dm->getInitialSceneUuid().empty()) {
            cfg.initialSceneUuid = dm->getInitialSceneUuid();
        }
        auto* pm = eng->host->getPluginManager();
        if (pm) {
            std::vector<std::string> activeUserPlugins;
            for (const auto& plugName : pm->getPluginsInState(Runtime::PluginState::Initialized)) {
                const Runtime::PluginEntry* entry = pm->getPluginEntry(plugName);
                if (entry && entry->scope == Runtime::PluginScope::User) {
                    activeUserPlugins.push_back(plugName);
                }
            }
            cfg.plugins = std::move(activeUserPlugins);
        }
        if (!cfg.projectFilePath.empty()) {
            CHECK_STATUS(eng->foundation->saveProjectConfig(cfg.projectFilePath, cfg));
        }
    }
    return OkStatus();
}

} // namespace MQEngine

extern "C" EDITOR_UI_PLUGIN_API MQEngine::Runtime::IPlugin* CreatePlugin() {
    return new MQEngine::EditorUiPlugin();
}
