#include "AndroidApplication.h"

namespace MQEngine
{
    void AndroidApplication::init()
    {
        setupFallbackCamera();
    }

    void AndroidApplication::setupFallbackCamera()
    {
        g_engineGlobal.dataManager->appendRegistry(&m_registry);
        m_tempCameraEntity = m_registry.create();
        m_registry.emplace<CameraComponent>(m_tempCameraEntity);
        m_registry.emplace<PositionComponent>(m_tempCameraEntity);
        m_registry.emplace<RotationComponent>(m_tempCameraEntity);
        g_engineGlobal.cameraSystem->setActiveCamera(&m_registry, m_tempCameraEntity);
    }

    void AndroidApplication::logicTick()
    {
        // Note: 延迟帧数等待管线与缓存状态稳定后再挂载初始场景
        if (m_frame < 3)
        {
            m_frame++;
            return;
        }

        if (!m_sceneLoaded)
        {
            m_sceneLoaded = true;
            loadInitialScene();
        }
    }

    void AndroidApplication::loadInitialScene()
    {
        std::string uuid = resolveInitialSceneUuid();
        if (!uuid.empty())
        {
            g_engineGlobal.dataManager->loadScene(uuid);
            g_engineGlobal.dataManager->openScene(uuid);
        }

        if (activateSceneCameras() && m_registry.valid(m_tempCameraEntity))
        {
            auto& tempCam = m_registry.get<CameraComponent>(m_tempCameraEntity);
            tempCam.active = false;
        }
    }

    std::string AndroidApplication::resolveInitialSceneUuid() const
    {
        std::string uuid = g_engineGlobal.dataManager->getInitialSceneUuid();
        if (uuid.empty() && Engine::getInstance().getProjectManager() &&
            Engine::getInstance().getProjectManager()->isProjectOpen())
        {
            uuid = Engine::getInstance().getProjectManager()->getCurrentProject().initialSceneUuid;
        }
        return uuid;
    }

    bool AndroidApplication::activateSceneCameras()
    {
        bool hasSceneCamera = false;
        for (auto* registry : g_engineGlobal.dataManager->currentRegistries())
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
        return hasSceneCamera;
    }
}
