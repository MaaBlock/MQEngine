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
namespace MQEngine {
    class UiManager {
    public:
        void init();
        void term();
        void registerRenderCallback(FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& renderCallBack);
        void logicTick();
        void renderScene();
    private:
        FCT::ImguiContext* m_imguiCtx;
        GraphViewInsight* m_graphView;
        RenderGraphViewer* m_passGenerator;
        ModelManager* m_modelManager;
    };
}



#endif //UIMANAGER_H
