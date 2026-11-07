#ifndef UNIFORMSLOTS_H
#define UNIFORMSLOTS_H

#include "../../thirdparty/thirdparty.h"

namespace MQEngine {

constexpr FCT::UniformSlot DirectionalLightUniformSlot {
    "DirectionalLightUniform",
    FCT::UniformVar{FCT::UniformType::Vec4,"directionalLightDirection"},
    FCT::UniformVar{FCT::UniformType::Vec3,"directionalLightColor"},
    FCT::UniformVar{FCT::UniformType::Float,"directionalLightIntensity"},
    FCT::UniformVar{FCT::UniformType::Bool,"directionalLightEnable"}
};
constexpr size_t MAX_POINT_LIGHTS = 16;

constexpr FCT::UniformSlot PointLightUniformSlot {
    "PointLightUniformSlot",
    FCT::UniformVar{FCT::UniformType::Vec4, "pointLightPositions", MAX_POINT_LIGHTS},
    FCT::UniformVar{FCT::UniformType::Vec4, "pointLightColors", MAX_POINT_LIGHTS},
    FCT::UniformVar{FCT::UniformType::Vec4, "pointLightAttenuations", MAX_POINT_LIGHTS},
    FCT::UniformVar{FCT::UniformType::Int, "pointLightCount"}
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

constexpr FCT::UniformSlot PBRMaterialUniformSlot {
    "PBRMaterialUniform",
    FCT::UniformVar{FCT::UniformType::Vec4, "baseColorFactor"},
    FCT::UniformVar{FCT::UniformType::Vec4, "pbrFactors"},
    FCT::UniformVar{FCT::UniformType::Vec4, "emissiveFactor"}
};

constexpr size_t MAX_BONES = 256;

constexpr FCT::UniformSlot BoneUniformSlot {
    "BoneUniform",
    FCT::UniformVar{FCT::UniformType::Mat4, "boneMatrices", MAX_BONES},
    FCT::UniformVar{FCT::UniformType::Int, "hasBones"}
};

}

#endif // UNIFORMSLOTS_H
