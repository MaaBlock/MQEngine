//
// Created by Administrator on 2025/8/24.
//

#include "./CameraSystem.h"
#include "../core/engine.h"
namespace MQEngine
{
    CameraSystem::CameraSystem(FCT::Context* ctx, DataManager* dataManager)
    {
        m_ctx = ctx;
        m_dataManager = dataManager;
        m_cameraUniform = FCT::Uniform(m_ctx,CameraUniformSlot);
        m_viewPosUniform = FCT::Uniform(m_ctx, ViewPosUniformSlot);
    }

    void CameraSystem::updateLogic()
    {
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            auto view = registry->view<PositionComponent, RotationComponent, CameraComponent>();
            for (auto entity : view)
            {
                auto& position = view.get<PositionComponent>(entity);
                auto& rotation = view.get<RotationComponent>(entity);
                auto& camera = view.get<CameraComponent>(entity);
                if (camera.active)
                {
                    m_cameraUniform.setValue(FCT::UniformType::ProjectionMatrix,
                    FCT::Mat4::Perspective(
                         camera.fov,
                         800 / 600,
                         camera.nearPlane,
                         camera.farPlane
                    ));
                    m_cameraUniform.setValue(FCT::UniformType::ViewMatrix,
                        calculateViewMatrix(position, rotation));
                    m_viewPosUniform.setValue("viewPosition", position.position);
                }
            }
        }
    }
    void CameraSystem::updateRender()
    {
        m_cameraUniform.update();
        m_viewPosUniform.update();
    }

    void CameraSystem::bindUniforms(FCT::Layout* layout)
    {
        layout->bindUniform(m_cameraUniform);
        layout->bindUniform(m_viewPosUniform);
    }
    std::vector<FCT::UniformSlot> CameraSystem::getUniformSlots()
    {
        return {
            CameraUniformSlot,
            ViewPosUniformSlot
        };
    }

    FCT::Vec3 CameraSystem::calculateForward(const RotationComponent& rotation)
    {
        const float PI = 3.14159265359f;
        float pitch = rotation.rotation.x * PI / 180.0f;
        float yaw = rotation.rotation.y * PI / 180.0f;

        FCT::Vec3 forward;
        forward.x = cos(yaw) * cos(pitch);
        forward.y = sin(pitch);
        forward.z = sin(yaw) * cos(pitch);

        return FCT::normalize(forward);
    }

    FCT::Mat4 CameraSystem::calculateViewMatrix(const PositionComponent& position, const RotationComponent& rotation)
    {
        FCT::Vec3 forward = calculateForward(rotation);
        FCT::Vec3 worldUp = FCT::Vec3(0.0f, 1.0f, 0.0f);
        FCT::Vec3 right = FCT::normalize(FCT::cross(forward, worldUp));
        FCT::Vec3 up = FCT::normalize(FCT::cross(right, forward));

        FCT::Vec3 target = position.position + forward;

        return FCT::Mat4::LookAt(position.position, target, up);
    }

    void CameraSystem::setActiveCamera(entt::registry* registry, entt::entity cameraEntity)
    {
        if (!registry) return;
        
        auto registries = m_dataManager->currentRegistries();
        for (auto& currentRegistry : registries)
        {
            auto view = currentRegistry->view<CameraComponent>();
            for (auto entity : view)
            {
                auto& camera = view.get<CameraComponent>(entity);
                camera.active = false;
            }
        }
        
        if (registry->valid(cameraEntity) && registry->all_of<CameraComponent>(cameraEntity))
        {
            auto& targetCamera = registry->get<CameraComponent>(cameraEntity);
            targetCamera.active = true;
        }
    }
}
