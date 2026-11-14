#ifndef MQENGINE_DOMAIN_RENDERER_TECH_RENDERTECHNIQUE_H
#define MQENGINE_DOMAIN_RENDERER_TECH_RENDERTECHNIQUE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/Pass/DeferredLightingPass.h>
#include <MQEngine/domain/renderer/Pass/GBufferPass.h>
#include <MQEngine/domain/renderer/RenderPacket.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief 渲染管线技术策略模式
             */
            enum class PipelineTechnique : uint8_t
            {
                ForwardPlus,
                DeferredShading,
                HybridRayTracing
            };

            /**
             * @brief 渲染技术配置与通道组合器
             */
            class ENGINE_API RenderTechnique
            {
            public:
                explicit RenderTechnique(PipelineTechnique pipeline = PipelineTechnique::DeferredShading)
                    : m_pipeline(pipeline)
                {
                }

                PipelineTechnique getPipeline() const noexcept { return m_pipeline; }
                void setPipeline(PipelineTechnique pipeline) noexcept { m_pipeline = pipeline; }

                bool isDeferred() const noexcept
                {
                    return m_pipeline == PipelineTechnique::DeferredShading ||
                           m_pipeline == PipelineTechnique::HybridRayTracing;
                }

                bool isRayTracing() const noexcept
                {
                    return m_pipeline == PipelineTechnique::HybridRayTracing;
                }

                GBufferPass& getGBufferPass() noexcept { return m_gbufferPass; }
                const GBufferPass& getGBufferPass() const noexcept { return m_gbufferPass; }

                DeferredLightingPass& getDeferredLightingPass() noexcept { return m_lightingPass; }
                const DeferredLightingPass& getDeferredLightingPass() const noexcept { return m_lightingPass; }

            private:
                PipelineTechnique m_pipeline = PipelineTechnique::DeferredShading;
                GBufferPass m_gbufferPass;
                DeferredLightingPass m_lightingPass;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_TECH_RENDERTECHNIQUE_H
