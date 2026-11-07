//
// Created by Administrator on 2025/8/24.
//

#ifndef CAMERASYSTEM_H
#define CAMERASYSTEM_H
#include "../../scene/manager/DataManager.h"
#include "./BindedSystem.h"
namespace MQEngine {
    class ENGINE_API CameraSystem : public BindedSystem {
    public:
        CameraSystem(FCT::Context* ctx, DataManager* dataManager);
        void updateLogic() override;
        void updateRender() override;
        void bindUniforms(FCT::Layout* layout) override;
        std::vector<FCT::UniformSlot> getUniformSlots() const override;
        void setActiveCamera(entt::registry* registry, entt::entity cameraEntity);
        bool activateFirstSceneCamera();
        bool getActiveCameraWorldTransform(
            FCT::Vec3<>& outPosition,
            FCT::Vec3<>& outForward,
            FCT::Vec3<>& outUp,
            CameraComponent& outCamera) const;

        static FCT::Vec3<> calculateForward(const RotationComponent& rotation);
        static RotationComponent calculateRotationFromDirection(const FCT::Vec3<>& direction);

    private:
        void computeEntityCameraBasis(
            entt::registry* registry,
            entt::entity entity,
            const PositionComponent& position,
            const RotationComponent& rotation,
            FCT::Vec3<>& outWorldPos,
            FCT::Vec3<>& outForward,
            FCT::Vec3<>& outUp) const;
        FCT::Mat4 calculateViewMatrix(
            entt::registry* registry,
            entt::entity entity,
            const PositionComponent& position,
            const RotationComponent& rotation,
            FCT::Vec3<>& outWorldPos) const;
        float resolveViewportAspectRatio() const;

        FCT::Context* m_ctx;
        FCT::Uniform m_cameraUniform;
        FCT::Uniform m_viewPosUniform;
        DataManager* m_dataManager;
        bool m_activeReverseZ = true;
    };
}

#endif //CAMERASYSTEM_H
