//
// Created by Administrator on 2025/8/24.
//

#include "./CameraSystem.h"
#include "./MatrixCacheSystem.h"
#include "../manager/TechManager.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
namespace MQEngine
{
    CameraSystem::CameraSystem(FCT::Context* ctx, DataManager* dataManager)
    {
        m_ctx = ctx;
        m_dataManager = dataManager;
        m_cameraUniform = FCT::Uniform(m_ctx, CameraUniformSlot);
        m_viewPosUniform = FCT::Uniform(m_ctx, ViewPosUniformSlot);
    }

    float CameraSystem::resolveViewportAspectRatio() const
    {
        if (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->platform->getWindow())
        {
            float w = static_cast<float>(Runtime::Engine::getInstance()->platform->getWindow()->getWidth());
            float h = static_cast<float>(Runtime::Engine::getInstance()->platform->getWindow()->getHeight());
            if (w > 1.0f && h > 1.0f)
            {
                return w / h;
            }
        }
        return 800.0f / 600.0f;
    }

    void CameraSystem::updateLogic()
    {
        ZoneScoped();
        float aspect = resolveViewportAspectRatio();
        auto registries = m_dataManager->currentRegistries();
        for (auto& registry : registries)
        {
            if (!registry)
            {
                continue;
            }
            auto view = registry->view<PositionComponent, RotationComponent, CameraComponent>();
            for (auto entity : view)
            {
                auto& position = view.get<PositionComponent>(entity);
                auto& rotation = view.get<RotationComponent>(entity);
                auto& camera = view.get<CameraComponent>(entity);
                if (camera.active)
                {
                    m_activeReverseZ = camera.reverseZ;
                    FCT::Mat4 projMat = camera.reverseZ
                        ? FCT::Mat4::PerspectiveReverseZ(camera.fov, aspect, camera.nearPlane, camera.farPlane)
                        : FCT::Mat4::Perspective(camera.fov, aspect, camera.nearPlane, camera.farPlane);
                    m_cameraUniform.setValue(FCT::UniformType::ProjectionMatrix, projMat);
                    FCT::Vec3<> worldPos = position.position;
                    FCT::Mat4 viewMat = calculateViewMatrix(registry, entity, position, rotation, worldPos);
                    m_cameraUniform.setValue(FCT::UniformType::ViewMatrix, viewMat);
                    m_viewPosUniform.setValue("viewPosition", worldPos);
                }
            }
        }
    }

    void CameraSystem::updateRender()
    {
        ZoneScoped();
        if (g_engineGlobal.techManager)
        {
            g_engineGlobal.techManager->setReverseZ(m_activeReverseZ);
        }
        m_cameraUniform.update();
        m_viewPosUniform.update();
    }

    void CameraSystem::bindUniforms(FCT::Layout* layout)
    {
        layout->bindUniform(m_cameraUniform);
        layout->bindUniform(m_viewPosUniform);
    }

    std::vector<FCT::UniformSlot> CameraSystem::getUniformSlots() const
    {
        return {
            CameraUniformSlot,
            ViewPosUniformSlot
        };
    }

    FCT::Vec3<> CameraSystem::calculateForward(const RotationComponent& rotation)
    {
        const float PI = 3.14159265359f;
        float pitch = rotation.rotation.x * PI / 180.0f;
        float yaw = rotation.rotation.y * PI / 180.0f;

        FCT::Vec3<> forward;
        forward.x = cos(yaw) * cos(pitch);
        forward.y = sin(pitch);
        forward.z = sin(yaw) * cos(pitch);

        return FCT::normalize(forward);
    }

    RotationComponent CameraSystem::calculateRotationFromDirection(const FCT::Vec3<>& direction)
    {
        const float PI = 3.14159265359f;
        FCT::Vec3<> dir = FCT::normalize(direction);
        float pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f)) * 180.0f / PI;
        float yaw = std::atan2(dir.z, dir.x) * 180.0f / PI;
        return RotationComponent{FCT::Vec3<>(pitch, yaw, 0.0f)};
    }

    void CameraSystem::computeEntityCameraBasis(
        entt::registry* registry,
        entt::entity entity,
        const PositionComponent& position,
        const RotationComponent& rotation,
        FCT::Vec3<>& outWorldPos,
        FCT::Vec3<>& outForward,
        FCT::Vec3<>& outUp) const
    {
        outWorldPos = position.position;
        FCT::Vec3<> localForward = calculateForward(rotation);
        FCT::Vec3<> worldUp(0.0f, 1.0f, 0.0f);

        if (registry && g_engineGlobal.matrixCacheSystem && registry->all_of<HierarchyComponent>(entity))
        {
            const auto& hierarchy = registry->get<HierarchyComponent>(entity);
            entt::entity parent = static_cast<entt::entity>(hierarchy.parentEntity);
            if (parent != entt::null && registry->valid(parent))
            {
                outWorldPos = g_engineGlobal.matrixCacheSystem->computeEntityWorldPosition(registry, entity);
                FCT::Mat4 parentRot = g_engineGlobal.matrixCacheSystem->computeEntityWorldRotationMatrix(registry, parent);
                FCT::Vec4 transformedFwd = parentRot * FCT::Vec4(localForward.x, localForward.y, localForward.z, 0.0f);
                localForward = FCT::normalize(FCT::Vec3<>(transformedFwd.x, transformedFwd.y, transformedFwd.z));
                FCT::Vec4 transformedUp = parentRot * FCT::Vec4(0.0f, 1.0f, 0.0f, 0.0f);
                worldUp = FCT::normalize(FCT::Vec3<>(transformedUp.x, transformedUp.y, transformedUp.z));
            }
        }

        outForward = localForward;
        FCT::Vec3<> right = FCT::cross(outForward, worldUp);
        if (right.length() < 1e-5f)
        {
            right = FCT::cross(outForward, FCT::Vec3<>(0.0f, 0.0f, 1.0f));
        }
        right = FCT::normalize(right);
        outUp = FCT::normalize(FCT::cross(right, outForward));
    }

    FCT::Mat4 CameraSystem::calculateViewMatrix(
        entt::registry* registry,
        entt::entity entity,
        const PositionComponent& position,
        const RotationComponent& rotation,
        FCT::Vec3<>& outWorldPos) const
    {
        FCT::Vec3<> forward;
        FCT::Vec3<> up;
        computeEntityCameraBasis(registry, entity, position, rotation, outWorldPos, forward, up);
        FCT::Vec3<> target = outWorldPos + forward;
        return FCT::Mat4::LookAt(outWorldPos, target, up);
    }

    bool CameraSystem::getActiveCameraWorldTransform(
        FCT::Vec3<>& outPosition,
        FCT::Vec3<>& outForward,
        FCT::Vec3<>& outUp,
        CameraComponent& outCamera) const
    {
        if (!m_dataManager)
        {
            return false;
        }
        auto registries = m_dataManager->currentRegistries();
        if (Scene::Scene* currentScene = m_dataManager->getCurrentScene())
        {
            if (entt::registry* mainReg = currentScene->getRegistry())
            {
                if (std::find(registries.begin(), registries.end(), mainReg) == registries.end())
                {
                    registries.push_back(mainReg);
                }
            }
            if (SceneTrunk* defaultTrunk = currentScene->getLoadedTrunk("default"))
            {
                if (entt::registry* trunkReg = defaultTrunk->getRegistry())
                {
                    if (std::find(registries.begin(), registries.end(), trunkReg) == registries.end())
                    {
                        registries.push_back(trunkReg);
                    }
                }
            }
        }
        for (auto* registry : registries)
        {
            if (!registry)
            {
                continue;
            }
            auto view = registry->view<PositionComponent, RotationComponent, CameraComponent>();
            for (auto entity : view)
            {
                const auto& camera = view.get<CameraComponent>(entity);
                if (!camera.active)
                {
                    continue;
                }
                const auto& position = view.get<PositionComponent>(entity);
                const auto& rotation = view.get<RotationComponent>(entity);
                computeEntityCameraBasis(registry, entity, position, rotation, outPosition, outForward, outUp);
                outCamera = camera;
                return true;
            }
        }
        return false;
    }

    bool CameraSystem::activateFirstSceneCamera()
    {
        if (!m_dataManager)
        {
            return false;
        }
        Scene::Scene* currentScene = m_dataManager->getCurrentScene();
        if (!currentScene)
        {
            return false;
        }

        auto tryActivateInRegistry = [this](entt::registry* reg) -> bool {
            if (!reg)
            {
                return false;
            }
            auto view = reg->view<CameraComponent>();
            for (auto entity : view)
            {
                setActiveCamera(reg, entity);
                return true;
            }
            return false;
        };

        if (tryActivateInRegistry(currentScene->getRegistry()))
        {
            return true;
        }
        for (const auto& trunkName : currentScene->getTrunkList())
        {
            if (currentScene->isLoad(trunkName))
            {
                if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName))
                {
                    if (tryActivateInRegistry(trunk->getRegistry()))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void CameraSystem::setActiveCamera(entt::registry* registry, entt::entity cameraEntity)
    {
        if (!registry) return;

        auto deactivateRegistryCameras = [](entt::registry* reg)
        {
            if (!reg) return;
            auto view = reg->view<CameraComponent>();
            for (auto entity : view)
            {
                auto& camera = view.get<CameraComponent>(entity);
                camera.active = false;
            }
        };

        auto registries = m_dataManager->currentRegistries();
        for (auto& currentRegistry : registries)
        {
            deactivateRegistryCameras(currentRegistry);
        }

        if (Scene::Scene* currentScene = m_dataManager->getCurrentScene())
        {
            deactivateRegistryCameras(currentScene->getRegistry());
            for (const auto& trunkName : currentScene->getTrunkList())
            {
                if (currentScene->isLoad(trunkName))
                {
                    if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName))
                    {
                        deactivateRegistryCameras(trunk->getRegistry());
                    }
                }
            }
        }
        deactivateRegistryCameras(registry);

        if (registry->valid(cameraEntity) && registry->all_of<CameraComponent>(cameraEntity))
        {
            auto& targetCamera = registry->get<CameraComponent>(cameraEntity);
            targetCamera.active = true;
        }
    }
}
