//
// Created by Administrator on 2025/8/24.
//

#ifndef CAMERASYSTEM_H
#define CAMERASYSTEM_H
#include "../data/DataManager.h"

namespace MQEngine {
    class ENGINE_API CameraSystem {
    public:
        CameraSystem(FCT::Context* ctx,DataManager* dataManager);
        void update();
        void bind(FCT::Layout* layout);
        void setActiveCamera(entt::registry* registry, entt::entity cameraEntity);
    private:
        FCT::Mat4 calculateViewMatrix(const PositionComponent& position, const RotationComponent& rotation);
        FCT::Vec3 calculateForward(const RotationComponent& rotation);
        FCT::Context* m_ctx;
        FCT::Uniform m_cameraUniform;
        FCT::Uniform m_viewPosUniform;
        DataManager* m_dataManager;
    };
}



#endif //CAMERASYSTEM_H
