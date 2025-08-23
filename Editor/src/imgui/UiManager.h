//
// Created by Administrator on 2025/8/18.
//

#ifndef UIMANAGER_H
#define UIMANAGER_H
#include <Engine/headers.h>
#include "GraphViewInsight.h"
#include "../core/Global.h"
#include "../RenderGraphView/PassGenerator.h"
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
        PassGenerator* m_passGenerator;
    };
}



#endif //UIMANAGER_H
