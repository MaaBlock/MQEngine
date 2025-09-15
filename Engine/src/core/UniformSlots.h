#ifndef UNIFORMSLOTS_H
#define UNIFORMSLOTS_H

#include "../thirdparty/ThirdParty.h"

namespace MQEngine {

constexpr FCT::UniformSlot DirectionalLightUniformSlot {
    "DirectionalLightUniform",
    FCT::UniformVar{FCT::UniformType::Vec4,"directionalLightDirection"},
    FCT::UniformVar{FCT::UniformType::Vec3,"directionalLightColor"},
    FCT::UniformVar{FCT::UniformType::Float,"directionalLightIntensity"},
    FCT::UniformVar{FCT::UniformType::Bool,"directionalLightEnable"}
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