#ifndef ENGINE_VERTEX_LAYOUTS_H
#define ENGINE_VERTEX_LAYOUTS_H

#include "../thirdparty/ThirdParty.h"

namespace MQEngine
{
    
    constexpr FCT::VertexLayout StandardMeshVertexLayout {
            FCT::VertexElement{FCT::VtxType::Tangent3f,"tangent"},
            FCT::VertexElement{FCT::VtxType::Bitangent3f,"bitangent"},
            FCT::VertexElement{FCT::VtxType::Color4f,"color"},
            FCT::VertexElement{FCT::VtxType::Position4f,"position"},
            FCT::VertexElement{FCT::VtxType::TexCoord2f,"texCoord"},
            FCT::VertexElement{FCT::VtxType::Normal3f,"normal"},
    };
}

#endif // ENGINE_VERTEX_LAYOUTS_H