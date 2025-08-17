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
    struct TextureSlot
    {
        std::string name;
    };
    struct PassName
    {
        std::string name;
    };
    using UniformSlot = FCT::ConstLayout;
    using UniformVar = FCT::ConstElement;
    using UniformType = FCT::ConstType;
    using SamplerSlot = FCT::SamplerElement;
    class Layout {
    public:
        template<typename... Args>
        void proccessArgs(FCT::VertexLayout vertexLayout, Args... args)
        {
            uint32_t index = findNextAvailableIndex();
            m_vertexLayouts[index] = vertexLayout;
            m_hasVertexLayout = true;
            processUnhandledTextureSlots();
            proccessArgs(args...);
        }
        template<typename... Args>
        void proccessArgs(SamplerSlot samplerSlot, Args... args)
        {
            m_resourceLayout.addSampler(samplerSlot);

            if (samplerSlot.getShaderStages() & FCT::ShaderStage::Vertex)
            {
                m_vertexResourceLayout.addSampler(samplerSlot);
            }
            if (samplerSlot.getShaderStages() & FCT::ShaderStage::Fragment)
            {
                m_pixelResourceLayout.addSampler(samplerSlot);
            }
            m_resourceLayout.addSampler(samplerSlot);

            proccessArgs(args...);
        }
        template<typename... Args>
        void proccessArgs(PassName passName, Args... args)
        {
            if (m_ctx && m_ctx->getModule<FCT::RenderGraph>()) {
                attachPass(m_ctx->getModule<FCT::RenderGraph>(), passName.name);
            }
            proccessArgs(args...);
        }

        template<typename... Args>
        void proccessArgs(uint32_t index, FCT::VertexLayout vertexLayout, Args... args)
        {
            m_vertexLayouts[index] = vertexLayout;
            m_hasVertexLayout = true;
            processUnhandledTextureSlots();
            proccessArgs(args...);
        }


        template<typename... Args>
        void proccessArgs(FCT::PixelLayout pixelLayout, Args... args)
        {
            m_pixelLayout = pixelLayout;
            m_hasPixelLayout = true;
            processUnhandledTextureSlots();
            proccessArgs(args...);
        }

        template<typename... Args>
        void proccessArgs(TextureSlot textureSlot, Args... args)
        {
            m_unhandledTextureSlots.push_back(textureSlot);
            processUnhandledTextureSlots();
            proccessArgs(args...);
        }
        template<typename... Args>
        void proccessArgs(UniformSlot uniformSlot, Args... args)
        {
            m_uniformLayouts[uniformSlot.getName()] = uniformSlot;
            proccessArgs(args...);
        }

        void proccessArgs()
        {
            if (!m_hasPixelLayout && m_hasVertexLayout)
            {
                m_pixelLayout = m_vertexLayouts.begin()->second;
            }
        }

        template<typename... Args>
        Layout(FCT::Context* ctx,Args... args) : m_ctx(ctx)
        {
            proccessArgs(args...);
        }
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
            if (element.getShaderStages() & FCT::ShaderStage::Vertex)
            {
                m_vertexResourceLayout.addTexture(element);
            }
            if (element.getShaderStages() & FCT::ShaderStage::Fragment)
            {
                m_pixelResourceLayout.addTexture(element);
            }
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
            m_pass = graph->getPass(passName);
        }
        Uniform allocateUniform(std::string name)
        {
            return std::move(Uniform(m_ctx,m_uniformLayouts[name]));
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
            ret->create();
            return ret;
        }
        FCT::PixelShader* allocatePixelShader(std::string code)
        {
            auto ret = m_ctx->createResource<FCT::ContextResource::PixelShader>();

            for (auto& layout : m_uniformLayouts)
            {
                ret->addUniform(layout.second);
            }
            ret->pixelLayout(m_pixelLayout);
            ret->resourceLayout(m_pixelResourceLayout);
            ret->code(code);
            ret->create();
            return ret;
        }
        void begin()
        {
            m_passResourceState.clear();

            for (const auto& fixedImage : m_fixedImages)
            {
                m_passResourceState.bindTexture(fixedImage.first, fixedImage.second);
            }

            m_pipelineState = TraditionPipelineState();
        }

        //passresource
        void bindUniform(const Uniform& uniform)
        {
            std::string name = (static_cast<FCT::RHI::ConstBuffer*>(uniform))->layout().getName(); // 你可能需要在 Uniform 类中添加这个方法
            m_passResourceState.bindUniform(name, uniform);
        }
        void bindTexture(std::string name, FCT::Image* image)
        {
            m_passResourceState.bindTexture(name, image);
        }
        void bindSampler(std::string name, FCT::Sampler* sampler)
        {
            m_passResourceState.bindSampler(name, sampler);
        }
        //pipeline
        void bindVertexShader(FCT::VertexShader* shader)
        {
            m_pipelineState.vertexShader = shader;
        }

        void bindPixelShader(FCT::PixelShader* shader)
        {
            m_pipelineState.pixelShader = shader;
        }
        void drawMesh(FCT::RHI::CommandBuffer* cmdBuffer,FCT::StaticMesh<uint32_t>* mesh)
        {
            auto resource = getCurrentPassResource();
            auto pipeline = getCurrentPipeline();

            pipeline->bind(cmdBuffer);
            resource->bind(cmdBuffer,pipeline);
            mesh->bind(cmdBuffer);
            mesh->draw(cmdBuffer);
        }
        void end()
        {

        }
    private:
        struct TraditionPipelineState
        {
            FCT::VertexShader* vertexShader = nullptr;
            FCT::PixelShader* pixelShader = nullptr;
            FCT::BlendState* blendState = nullptr;
            FCT::RasterizationState* rasterizationState = nullptr;
            FCT::DepthStencilState* depthStencilState = nullptr;

            size_t hash() const
            {
                size_t hash = 0;
                boost::hash_combine(hash, vertexShader);
                boost::hash_combine(hash, pixelShader);
                boost::hash_combine(hash, blendState);
                boost::hash_combine(hash, rasterizationState);
                boost::hash_combine(hash, depthStencilState);
                return hash;
            }

            bool operator==(const TraditionPipelineState& other) const
            {
                return vertexShader == other.vertexShader &&
                       pixelShader == other.pixelShader &&
                       blendState == other.blendState &&
                       rasterizationState == other.rasterizationState &&
                       depthStencilState == other.depthStencilState;
            }
        };
        struct PassResourceState
        {
            std::unordered_map<std::string, FCT::Image*> boundTextures;
            std::unordered_map<std::string, FCT::Sampler*> boundSamplers;
            std::unordered_map<std::string, FCT::RHI::ConstBuffer*> boundUniforms;

            void clear()
            {
                boundTextures.clear();
                boundSamplers.clear();
                boundUniforms.clear();
            }

            void bindTexture(const std::string& name, FCT::Image* image)
            {
                boundTextures[name] = image;
            }

            void bindSampler(const std::string& name, FCT::Sampler* sampler)
            {
                boundSamplers[name] = sampler;
            }

            void bindUniform(const std::string& name, const Uniform& uniform)
            {
                boundUniforms[name] = uniform;
            }

            size_t hash() const
            {
                size_t hash = 0;

                for (const auto& tex : boundTextures) {
                    boost::hash_combine(hash, tex.first);
                    boost::hash_combine(hash, tex.second);
                }

                for (const auto& samp : boundSamplers) {
                    boost::hash_combine(hash, samp.first);
                    boost::hash_combine(hash, samp.second);
                }

                for (const auto& uni : boundUniforms) {
                    boost::hash_combine(hash, uni.first);
                    boost::hash_combine(hash, uni.second);
                }

                return hash;
            }
        };

        struct PassResourceCache
        {
            std::unordered_map<size_t,FCT::PassResource*> m_passResources;
            FCT::PassResource* get(const PassResourceState& state, const std::function<FCT::PassResource*(const PassResourceState& state)>& creator)
            {
                auto hash = state.hash();
                if (m_passResources.count(hash))
                {
                    return m_passResources[hash];
                } else
                {
                    auto passResource = creator(state);
                    m_passResources[hash] = passResource;
                    return passResource;
                }
            }
        };
        struct PipelineCache
        {
            std::unordered_map<size_t, FCT::RHI::RasterizationPipeline*> m_pipelines;

            FCT::RHI::RasterizationPipeline* get(const TraditionPipelineState& state, const std::function<FCT::RHI::RasterizationPipeline*(const TraditionPipelineState& state)>& creator)
            {
                auto hash = state.hash();
                if (m_pipelines.count(hash))
                {
                    return m_pipelines[hash];
                } else
                {
                    auto pipeline = creator(state);
                    m_pipelines[hash] = pipeline;
                    return pipeline;
                }
            }
        };
        void processUnhandledTextureSlots()
        {
            auto it = m_unhandledTextureSlots.begin();
            while (it != m_unhandledTextureSlots.end())
            {
                FCT::ShaderStages usage;

                if (!m_hasVertexLayout && !m_hasPixelLayout)
                {
                    ++it;
                    continue;
                }
                else if (m_hasVertexLayout && !m_hasPixelLayout)
                {
                    usage = FCT::ShaderStage::Vertex | FCT::ShaderStage::Fragment;
                }
                else if (!m_hasVertexLayout && m_hasPixelLayout)
                {
                    usage = FCT::ShaderStage::Vertex | FCT::ShaderStage::Fragment;
                }
                else
                {
                    usage = FCT::ShaderStage::Fragment;
                }

                FCT::TextureElement element(FCT::TextureType::Texture2D, it->name.c_str(), usage, FCT::UpdateFrequency::PerFrame);
                addTextureSlot(element);

                it = m_unhandledTextureSlots.erase(it);
            }
        }
        uint32_t findNextAvailableIndex()
        {
            while (m_vertexLayouts.find(m_nextAvailableIndex) != m_vertexLayouts.end()) {
                ++m_nextAvailableIndex;
            }
            return m_nextAvailableIndex++;
        }
        FCT::Context* m_ctx;
        std::map<uint32_t, FCT::VertexLayout> m_vertexLayouts;
        std::map<std::string, FCT::ConstLayout> m_uniformLayouts;
        FCT::PixelLayout m_pixelLayout;
        FCT::ResourceLayout m_resourceLayout;
        FCT::ResourceLayout m_vertexResourceLayout;
        FCT::ResourceLayout m_pixelResourceLayout;
        std::unordered_map<std::string, FCT::Image*> m_fixedImages;
        uint32_t m_nextAvailableIndex = 0;
        std::vector<TextureSlot> m_unhandledTextureSlots;
        bool m_hasVertexLayout = false;
        bool m_hasPixelLayout = false;
        FCT::PassResource* getCurrentPassResource()
        {
            return m_passResourceCache.get(m_passResourceState, [this](const PassResourceState& state)
            {
                auto ret = m_ctx->createResource<FCT::PassResource>();
                for (const auto& tex : state.boundTextures) {
                    ret->addTexture( tex.second,m_resourceLayout.findTexture(tex.first.c_str()));
                }
                for (const auto& samp : state.boundSamplers) {
                    ret->addSampler(samp.second,m_resourceLayout.findSampler(samp.first.c_str()));
                }
                for (const auto& uni : state.boundUniforms) {
                    ret->addConstBuffer(uni.second);
                }
                ret->create();
                return ret;
            });
        }
        PassResourceState m_passResourceState;
        PassResourceCache m_passResourceCache;
        FCT::RHI::Pass* m_pass;
        TraditionPipelineState m_pipelineState;
        PipelineCache m_pipelineCache;
        FCT::RHI::RasterizationPipeline* getCurrentPipeline()
        {
            return m_pipelineCache.get(m_pipelineState, [this](const TraditionPipelineState& state)
            {
                auto ret = m_ctx->createTraditionPipeline();

                for (auto& layout : m_vertexLayouts)
                {
                    ret->vertexLayout(layout.second);
                }

                ret->pixelLayout(m_pixelLayout);

                if (state.vertexShader) {
                    ret->addResources(state.vertexShader);
                }
                if (state.pixelShader) {
                    ret->addResources(state.pixelShader);
                }

                if (state.blendState) {
                    ret->addResources(state.blendState);
                }
                if (state.rasterizationState) {
                    ret->addResources(state.rasterizationState);
                }
                if (state.depthStencilState) {
                    ret->addResources(state.depthStencilState);
                }

                if (m_pass) {
                    ret->bindPass(m_pass);
                }

                ret->create();
                return ret;
            });
        }
    };

}
#endif //LAYOUT_H
