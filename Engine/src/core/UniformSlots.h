#ifndef UNIFORMSLOTS_H
#define UNIFORMSLOTS_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine {

constexpr FCT::UniformSlot DirectionalLightUniformSlot {
    "DirectionalLightUniform",
    FCT::UniformVar{FCT::UniformType::Vec4,"directionalLightDirection"},
    FCT::UniformVar{FCT::UniformType::Vec3,"directionalLightColor"},
    FCT::UniformVar{FCT::UniformType::Float,"directionalLightIntensity"},
    FCT::UniformVar{FCT::UniformType::Bool,"directionalLightEnable"}
};
constexpr FCT::UniformSlot PointLightUniformSlot {
    "PointLightUniformSlot",
    FCT::UniformVar{FCT::UniformType::Vec3,"pointLightPosition"},
    FCT::UniformVar{FCT::UniformType::Vec3,"pointLightColor"},
    FCT::UniformVar{FCT::UniformType::Float,"pointLightConstant"},
    FCT::UniformVar{FCT::UniformType::Float,"pointLightLinear"},
    FCT::UniformVar{FCT::UniformType::Float,"pointLightQuadratic"},
    FCT::UniformVar{FCT::UniformType::Bool,"pointLightEnable"}
};

constexpr FCT::UniformSlot ViewPosUniformSlot {
    "ViewPosUniform",
    FCT::UniformVar{FCT::UniformType::Vec3,"viewPosition"}
};

constexpr FCT::UniformSlot ShadowUniformSlot {
    "ShadowUniform",
    FCT::UniformVar{FCT::UniformType::MVPMatrix,"directionalLightMvp"}
};

constexpr FCT::UniformSlot ShininessUniformSlot {
    "ShininessUniform",
    FCT::UniformVar{FCT::UniformType::Float,"shininess"}
};

}

#endif // UNIFORMSLOTS_H