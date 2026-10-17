//
// Created by Administrator on 2025/9/12.
//

#include "GameApplication.h"
#include "Engine/manager/ProjectManager.h"

namespace MQEngine {

    void GameApplication::init()
    {
        ZoneScoped();
        g_engineGlobal.dataManager->appendRegistry(&m_registry);
        m_tempCameraEntity = m_registry.create();
        m_registry.emplace<CameraComponent>(m_tempCameraEntity);
        m_registry.emplace<PositionComponent>(m_tempCameraEntity);
        m_registry.emplace<RotationComponent>(m_tempCameraEntity);
        g_engineGlobal.cameraSystem->setActiveCamera(&m_registry, m_tempCameraEntity);
    }

    void GameApplication::logicTick()
    {
        ZoneScoped();
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
            if (uuid.empty() && Engine::getInstance().getProjectManager() && Engine::getInstance().getProjectManager()->isProjectOpen())
                uuid = Engine::getInstance().getProjectManager()->getCurrentProject().initialSceneUuid;

            if (!uuid.empty())
            {
                g_engineGlobal.dataManager->loadScene(uuid);
                g_engineGlobal.dataManager->openScene(uuid);
            }

            bool hasSceneCamera = false;
            for (auto registry : g_engineGlobal.dataManager->currentRegistries())
            {
                if (registry == &m_registry)
                    continue;
                auto cameraView = registry->view<CameraComponent>();
                for (auto entity : cameraView)
                {
                    auto& camera = cameraView.get<CameraComponent>(entity);
                    camera.active = true;
                    hasSceneCamera = true;
                }
            }
            
            if (hasSceneCamera && m_registry.valid(m_tempCameraEntity))
            {
                auto& tempCam = m_registry.get<CameraComponent>(m_tempCameraEntity);
                tempCam.active = false;
            }
        }
    }
} // MQEngine