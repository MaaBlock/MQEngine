//
// Created by Administrator on 2025/8/23.
//

#ifndef MODELMANAGER_H
#define MODELMANAGER_H
#include "../Thirdparty/thirdparty.h"

namespace MQEngine {
    class ModelManager {
    public:
        ModelManager(DataManager* dataManager);
        ~ModelManager();
        void render();
    private:
        DataManager* m_dataManager;
    };
}



#endif //MODELMANAGER_H
