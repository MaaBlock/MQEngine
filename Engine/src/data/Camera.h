//
// Created by Administrator on 2025/8/24.
//

#ifndef CAMERA_H
#define CAMERA_H
#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
namespace MQEngine {
    constexpr FCT::UniformSlot CameraUniformSlot {
        "CameraUniform",
        FCT::UniformVar{FCT::UniformType::ProjectionMatrix},
        FCT::UniformVar{FCT::UniformType::ViewMatrix}
    };
    struct ENGINE_API PositionComponent {
        FCT::Vec3 position;
    };

    struct ENGINE_API RotationComponent {
        FCT::Vec3 rotation;
    };

    struct ENGINE_API ScaleComponent {
        FCT::Vec3 scale = FCT::Vec3(1.0f, 1.0f, 1.0f);
    };

    struct ENGINE_API CameraComponent {
        bool active = true;
        float fov = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

} // MQEngine

#endif //CAMERA_H
