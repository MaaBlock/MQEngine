//
// Created by Administrator on 2025/8/17.
//

#ifndef LAYOUT_H
#define LAYOUT_H
#include "Uniform.h"
namespace MQEngine{
    inline FCT::ShaderStages getAllAfterTheStage(FCT::ShaderStage stages)
    {
        FCT::ShaderStages ret;
        switch (stages)
        {
        case FCT::ShaderStage::Vertex:
            ret |= FCT::ShaderStage::Vertex;
        case FCT::ShaderStage::Fragment:
            ret |= FCT::ShaderStage::Fragment;
        default:
            break;
        }
        return ret;
    }
    class Layout {
    public:
        void ctx(FCT::Context* ctx)
        {
            m_ctx = ctx;
        }
        void setFixedImage(std::string name, FCT::Image* image)
        {
            m_fixedImages[name] = image;
        }
        void addTextureSlot(FCT::TextureElement element)
        {
            m_resourceLayout.addTexture(element);
        }
        void attachPass(FCT::RenderGraph* graph,std::string passName)
        {
            auto edges = graph->getTextureEdges(passName);
            for (auto edge : edges)
            {
                FCT::TextureElement element(FCT::TextureType::Texture2D,edge->fromImage.c_str(),getAllAfterTheStage(edge->stage),FCT::UpdateFrequency::PerFrame);
                addTextureSlot(element);
                setFixedImage(edge->fromImage,graph->getImage(edge->fromImage));
            }
        }
        Uniform allocateUniform(std::string name)
        {
            return Uniform(m_ctx,m_uniformLayouts[name]);
        }
        FCT::VertexShader* allocateVertexShader(std::string code)
        {
            auto ret = m_ctx->createResource<FCT::ContextResource::VertexShader>();
            for (auto& layout : m_vertexLayouts)
            {
                ret->addLayout(layout.first,layout.second);
            }
            ret->pixelLayout(m_pixelLayout);
            for (auto& uniform : m_uniformLayouts)
            {
                ret->addUniform(uniform.second);
            }
            ret->resourceLayout(m_vertexResourceLayout);
            ret->code(code);
            return ret;
        }
        FCT::PixelShader* allocatePixelShader(std::string code)
        {
            auto ret = m_ctx->createResource<FCT::ContextResource::PixelShader>();

            for (auto& layout : m_uniformLayouts)
            {
                ret->addUniform(layout.second);
            }
            ret->resourceLayout(m_pixelResourceLayout);
            ret->code(code);
            return ret;
        }
    private:
        FCT::Context* m_ctx;
        std::map<uint32_t, FCT::VertexLayout> m_vertexLayouts;
        std::map<std::string, FCT::ConstLayout> m_uniformLayouts;
        FCT::PixelLayout m_pixelLayout;
        FCT::ResourceLayout m_resourceLayout;
        FCT::ResourceLayout m_vertexResourceLayout;
        FCT::ResourceLayout m_pixelResourceLayout;
        std::unordered_map<std::string, FCT::Image*> m_fixedImages;
    };

}
#endif //LAYOUT_H
