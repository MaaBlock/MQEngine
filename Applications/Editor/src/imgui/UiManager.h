//
// Created by Administrator on 2025/8/18.
//

#ifndef UIMANAGER_H
#define UIMANAGER_H
#include <Engine/headers.h>
#include "GraphViewInsight.h"
#include "../AssetsManager/ModelManager.h"
#include "../core/Global.h"
#include "../RenderGraphView/RenderGraphViewer.h"
#include "./EditorCameraManager.h"
#include "../AssetsManager/SceneManager.h"
#include "../AssetsManager/ScriptManager.h"
#include "../SceneViewer/SceneEntityViewer.h"
#include "../SceneViewer/EntityInspector.h"
#include "../SceneViewer/Inspector.h"
#include "../ShaderEditor/ShaderEditor.h"
#include "../ShaderEditor/ShaderGraph.h"
#include "ContentBrowser.h"
#include "LogWindow.h"

namespace MQEngine {
    class UiManager {
    public:
        void init();
        void term();
        void registerRenderCallback(FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& renderCallBack);
        void logicTick();
        void renderScene();
        void renderMainMenuBar();
        void renderCameraWindow();
    private:
        void setupCustomDarkTheme();
        void setupDefaultLayout(ImGuiID dockspaceId);
        bool m_needResetLayout = false;
        bool m_focusDefaultTabs = false;
        bool m_layoutChecked = false;
        std::shared_ptr<ImguiPlugin> m_imguiPlugin;
        FCT::ImguiContext* m_imguiCtx = nullptr;
        GraphViewInsight* m_graphView;
        RenderGraphViewer* m_passGenerator;
        ModelManager* m_modelManager;
        EditorCameraManager* m_editorCameraManager;
        SceneManager* m_sceneManager;
        ScriptManager* m_scriptManager;
        SceneEntityViewer* m_sceneEntityViewer;
        EntityInspector* m_entityInspector;
        Inspector* m_inspector;
        ShaderEditor* m_shaderEditor;
        ShaderGraph* m_shaderGraph;
        ContentBrowser* m_contentBrowser;
        LogWindow* m_logWindow = nullptr;
    };
}



#endif //UIMANAGER_H
