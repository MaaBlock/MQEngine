//
// Created by Administrator on 2025/9/12.
//

#include "GameApplication.h"

namespace MQEngine {
    void GameApplication::init()
    {
        std::string uuid = g_engineGlobal.dataManager->getInitialSceneUuid();
        g_engineGlobal.dataManager->loadScene(uuid);
        g_engineGlobal.dataManager->openScene(uuid);
        for (auto registry : g_engineGlobal.dataManager->currentRegistries())
        {
            auto cameraView = registry->view<CameraComponent>();
            for (auto entity : cameraView)
            {
                auto& camera = cameraView.get<CameraComponent>(entity);
                camera.active = true;
            }
        }
    }

    void GameApplication::logicTick()
    {
    }
} // MQEngine