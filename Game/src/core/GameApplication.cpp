//
// Created by Administrator on 2025/9/12.
//

#include "GameApplication.h"

namespace MQEngine {

    void GameApplication::init()
    {
        g_engineGlobal.dataManager->appendRegistry(&m_registry);
        m_tempCameraEntity = m_registry.create();
        m_registry.emplace<CameraComponent>(m_tempCameraEntity);
        m_registry.emplace<PositionComponent>(m_tempCameraEntity);
        m_registry.emplace<RotationComponent>(m_tempCameraEntity);
        g_engineGlobal.cameraSystem->setActiveCamera(&m_registry, m_tempCameraEntity);
    }

    void GameApplication::logicTick()
    {
        //延迟加载场景（为了临时避免ClearCahce带来的bug）
        /**
         *todo:正确处理Cache和场景加载
         **/
        if (m_frame < 3)
        {
            m_frame++;
            return;
        }
        
        static bool sceneLoaded = false;
        if (!sceneLoaded)
        {
            sceneLoaded = true;
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
            
            if (m_registry.valid(m_tempCameraEntity))
            {
                auto& tempCam = m_registry.get<CameraComponent>(m_tempCameraEntity);
                tempCam.active = false;
            }
        }
    }
} // MQEngine