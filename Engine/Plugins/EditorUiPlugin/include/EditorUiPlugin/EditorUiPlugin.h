#ifndef MQENGINE_EDITOR_UI_PLUGIN_H
#define MQENGINE_EDITOR_UI_PLUGIN_H

#include "EditorUiPluginApi.h"
#include "IEditorUiService.h"
#include <string>
#include <vector>

namespace MQEngine {

/**
 * @brief 编辑器 UI 基础插件包：提供主 DockSpace 动态布局服务、工程与场景保存、顶栏菜单扩展等编辑器底座能力
 */
class EDITOR_UI_PLUGIN_API EditorUiPlugin : public Runtime::IPlugin, public IEditorUiService {
public:
    EditorUiPlugin();
    EditorUiPlugin(const EditorUiPlugin&) = delete;
    EditorUiPlugin& operator=(const EditorUiPlugin&) = delete;
    ~EditorUiPlugin() override;

    std::string getName() const override { return "EditorUiPlugin"; }
    std::string getVersion() const override { return "1.0.0"; }
    Runtime::PluginScope getScope() const override { return Runtime::PluginScope::Engine; }
    std::vector<std::string> getDependencies() const override { return {"ImguiPlugin"}; }

    // IEditorUiService 接口实现
    ImguiPlugin* getImguiPlugin() const override { return m_imguiPlugin; }
    FCT::ImguiContext* getImguiContext() const override { return m_imguiCtx; }

    void registerDockWindow(const std::string& windowTitle,
                            EditorDockSlot slot,
                            bool focusOnReset = false,
                            const std::string& ownerComposite = "") override;

    void registerMenuAction(const std::string& menuCategory,
                            const std::string& itemLabel,
                            const std::string& shortcut,
                            std::function<void()> callback,
                            const std::string& ownerComposite = "") override;

    void registerPanelCallback(const std::string& panelName,
                               PanelRenderCallback callback,
                               const std::string& ownerComposite = "") override;

    void unregisterPanelCallback(const std::string& panelName) override;

    void unregisterAllByOwner(const std::string& ownerComposite) override;

    void requestResetLayout() override { m_needResetLayout = true; }

    void applyDockSpaceLayout(const char* dockspaceName = "MQEngine") override;
    void applyDockSpaceLayout(ImGuiID dockspaceId) override;
    void applyDefaultTabFocusIfNeeded() override;

    Status saveActiveScene() override;

    Status saveProjectSettings() override;

    const std::vector<EditorDockWindowDesc>& getRegisteredDockWindows() const override {
        return m_dockWindows;
    }

    const std::vector<EditorMenuActionDesc>& getRegisteredMenuActions() const override {
        return m_menuActions;
    }

    // 内部供 EditorShellComposite 与 EditorMenuAndSaveComposite 驱动的辅助方法
    Status initShell(Runtime::Engine* engine);
    void tickShell();
    void shutdownShell();

    Status initPersistenceAndMenu(Runtime::Engine* engine);
    void shutdownPersistenceAndMenu();

    bool consumeResetLayoutRequest() {
        bool val = m_needResetLayout;
        m_needResetLayout = false;
        return val;
    }

    bool consumeFocusDefaultTabsRequest() {
        bool val = m_focusDefaultTabs;
        m_focusDefaultTabs = false;
        return val;
    }

    void renderRegisteredPanels();

private:
    struct PanelEntry {
        std::string panelName;
        PanelRenderCallback callback;
        std::string ownerComposite;
    };

    void registerBuiltInDefaultDockWindows();

    Runtime::Engine* m_engine = nullptr;
    ImguiPlugin* m_imguiPlugin = nullptr;
    FCT::ImguiContext* m_imguiCtx = nullptr;
    bool m_needResetLayout = false;
    bool m_focusDefaultTabs = false;
    bool m_layoutChecked = false;

    std::vector<EditorDockWindowDesc> m_dockWindows;
    std::vector<EditorMenuActionDesc> m_menuActions;
    std::vector<PanelEntry> m_panels;
};

} // namespace MQEngine

#ifdef BUILD_EDITORUIPLUGIN_PLUGIN
extern "C" EDITOR_UI_PLUGIN_API MQEngine::Runtime::IPlugin* CreatePlugin();
#endif

#endif // MQENGINE_EDITOR_UI_PLUGIN_H
