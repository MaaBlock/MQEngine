#ifndef EDITOR_UI_LAYER_H
#define EDITOR_UI_LAYER_H

#include "IUiModule.h"
#include "MainMenuBarUiModule.h"
#include "SceneViewportUiModule.h"
#include "CameraControlUiModule.h"
#include "SceneEntityUiModule.h"
#include "EntityInspectorUiModule.h"
#include "../imgui/GraphViewInsight.h"
#include "../RenderGraphView/RenderGraphViewer.h"
#include "../imgui/EditorCameraManager.h"
#include "../AssetsManager/ScriptManager.h"
#include "../SceneViewer/Inspector.h"
#include "../ShaderEditor/ShaderEditor.h"
#include "../ShaderEditor/ShaderGraph.h"
#include "../imgui/ContentBrowser.h"
#include "../imgui/LogWindow.h"

namespace MQEngine {

    /**
     * @brief Editor 顶层 UI 层，统一管理 ImGui 上下文、DockSpace 布局、UI 模块生命周期与信号调度
     */
    class UiLayer {
    public:
        UiLayer() = default;
        ~UiLayer() = default;

        void init();
        void term();
        void registerRenderCallback(FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& renderCallBack);
        void logicTick();

        void requestResetLayout();

        UiContext& getUiContext() { return m_uiContext; }
        const UiContext& getUiContext() const { return m_uiContext; }

        FCT::ImguiContext* getImguiContext() const { return m_imguiCtx; }
        EditorCameraManager* getCameraManager() const { return m_editorCameraManager; }
        IEditorUiService* getEditorUiService() const { return m_editorUiService; }

        template <typename ModuleType, typename... Args>
            requires std::derived_from<ModuleType, IUiModule>
        ModuleType* registerModule(Args&&... args) {
            auto modulePtr = makeUnique<ModuleType>(std::forward<Args>(args)...);
            ModuleType* rawPtr = modulePtr.get();
            rawPtr->onInit(m_uiContext);
            m_modules.push_back(std::move(modulePtr));
            return rawPtr;
        }

        template <typename... ModuleTypes>
        void registerModules() {
            (registerModule<ModuleTypes>(), ...);
        }

    private:
        void initImguiPlugin();
        void initEditorUiPlugin();
        void registerDefaultDockWindows();
        void initEditorSubsystems();
        void registerCoreUiModules();
        void registerLegacyPanelModules();
        void setupCustomDarkTheme();
        void updateDockSpaceLayout();
        void setupDefaultLayout(ImGuiID dockspaceId);
        void renderAllModules();
        void applyDefaultTabFocusIfNeeded();

    private:
        UiContext m_uiContext;
        std::vector<UniquePtr<IUiModule>> m_modules;

        bool m_needResetLayout = false;
        bool m_focusDefaultTabs = false;
        bool m_layoutChecked = false;

        std::shared_ptr<ImguiPlugin> m_imguiPlugin;
        IEditorUiService* m_editorUiService = nullptr;
        FCT::ImguiContext* m_imguiCtx = nullptr;
        EditorCameraManager* m_editorCameraManager = nullptr;
        GraphViewInsight* m_graphView = nullptr;
        ShaderGraph* m_shaderGraph = nullptr;
        Inspector* m_inspector = nullptr;
        ContentBrowser* m_contentBrowser = nullptr;
    };

} // namespace MQEngine

#endif // EDITOR_UI_LAYER_H
