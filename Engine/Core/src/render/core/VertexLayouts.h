#ifndef ENGINE_VERTEX_LAYOUTS_H
#define ENGINE_VERTEX_LAYOUTS_H

#include "../../thirdparty/thirdparty.h"

namespace MQEngine
{
    constexpr FCT::VertexLayout StandardMeshVertexLayout{
        FCT::VertexElement{FCT::VtxType::Color4f, "color"},
        FCT::VertexElement{FCT::VtxType::Position4f, "position"},
        FCT::VertexElement{FCT::VtxType::TexCoord2f, "texCoord"},
        FCT::VertexElement{FCT::VtxType::Normal3f, "normal"},
        FCT::VertexElement{FCT::VtxType::Tangent3f, "tangent"},
        FCT::VertexElement{FCT::VtxType::Bitangent3f, "bitangent"},
        FCT::VertexElement{FCT::VtxType::BoneIds4i, "boneIds"},
        FCT::VertexElement{FCT::VtxType::BoneWeights4f, "boneWeights"}
    };

    constexpr FCT::PixelLayout StandardPixelLayout{
        FCT::VertexElement{FCT::VtxType::Custom, "worldPos", FCT::Format::R32G32B32A32_SFLOAT},
        FCT::VertexElement{FCT::VtxType::Custom, "shadowPos", FCT::Format::R32G32B32A32_SFLOAT},
        FCT::VertexElement{FCT::VtxType::Color4f, "color"},
        FCT::VertexElement{FCT::VtxType::Position4f, "position"},
        FCT::VertexElement{FCT::VtxType::TexCoord2f, "texCoord"},
        FCT::VertexElement{FCT::VtxType::Normal3f, "normal"},
        FCT::VertexElement{FCT::VtxType::Tangent3f, "tangent"},
        FCT::VertexElement{FCT::VtxType::Bitangent3f, "bitangent"}
    };
}

#endif // ENGINE_VERTEX_LAYOUTS_H
