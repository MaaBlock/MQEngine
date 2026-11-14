#ifndef MQENGINE_DOMAIN_RENDERER_PASS_GBUFFERPASS_H
#define MQENGINE_DOMAIN_RENDERER_PASS_GBUFFERPASS_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/RenderPacket.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief G-Buffer 渲染通道描述与资源布局
             */
            struct GBufferPassDesc
            {
                uint32_t width = 1920;
                uint32_t height = 1080;
                FCT::Format albedoFormat = FCT::Format::R8G8B8A8_UNORM;
                FCT::Format normalFormat = FCT::Format::R16G16B16A16_SFLOAT;
                FCT::Format positionFormat = FCT::Format::R16G16B16A16_SFLOAT;
                FCT::Format depthFormat = FCT::Format::D32_SFLOAT;
            };

            /**
             * @brief 延迟着色 G-Buffer 阶段通道节点
             */
            class ENGINE_API GBufferPass
            {
            public:
                explicit GBufferPass(const GBufferPassDesc& desc = {})
                    : m_desc(desc)
                {
                }

                const GBufferPassDesc& getDesc() const noexcept { return m_desc; }
                void setDimensions(uint32_t width, uint32_t height) noexcept
                {
                    m_desc.width = width;
                    m_desc.height = height;
                }

                const char* getPassName() const noexcept { return "GBufferPass"; }

            private:
                GBufferPassDesc m_desc;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_PASS_GBUFFERPASS_H
