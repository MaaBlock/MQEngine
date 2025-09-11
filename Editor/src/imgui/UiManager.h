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

namespace MQEngine {
    class UiManager {
    public:
        void init();
        void term();
        void registerRenderCallback(FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& renderCallBack);
        void logicTick();
        void renderScene();
        void renderMainMenuBar();
    private:
        void setupCustomDarkTheme();
        FCT::ImguiContext* m_imguiCtx;
        GraphViewInsight* m_graphView;
        RenderGraphViewer* m_passGenerator;
        ModelManager* m_modelManager;
        EditorCameraManager* m_editorCameraManager;
        SceneManager* m_sceneManager;
        ScriptManager* m_scriptManager;
        SceneEntityViewer* m_sceneEntityViewer;
        EntityInspector* m_entityInspector;
    };
}



#endif //UIMANAGER_H
