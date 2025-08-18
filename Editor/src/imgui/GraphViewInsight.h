//
// Created by Administrator on 2025/8/18.
//

#ifndef GRAPHVIEWINSIGHT_H
#define GRAPHVIEWINSIGHT_H
#include "../thirdparty/thirdparty.h"
#include <Engine/headers.h>


namespace MQEngine {
    class GraphViewInsight {
    public:
        GraphViewInsight(FCT::ImguiContext* imguiCtx);
        void keepImage(FCT::RenderGraph* graph);
        void render();
    private:
        FCT::ImguiContext* m_imguiCtx;
    };
}


#endif //GRAPHVIEWINSIGHT_H
