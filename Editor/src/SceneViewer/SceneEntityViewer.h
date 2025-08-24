//
// Created by Administrator on 2025/8/25.
//

#ifndef SCENEENTITYVIEWER_H
#define SCENEENTITYVIEWER_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine {

    class SceneEntityViewer {
    public:
        SceneEntityViewer();
        void render();
    private:
        DataManager* m_dataManager;
    };

} // MQEngine

#endif //SCENEENTITYVIEWER_H
