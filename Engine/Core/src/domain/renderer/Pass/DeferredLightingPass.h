#ifndef MQENGINE_DOMAIN_RENDERER_PASS_DEFERREDLIGHTINGPASS_H
#define MQENGINE_DOMAIN_RENDERER_PASS_DEFERREDLIGHTINGPASS_H

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
             * @brief 延迟着色光照计算通道描述
             */
            struct DeferredLightingPassDesc
            {
                uint32_t width = 1920;
                uint32_t height = 1080;
                FCT::Format hdrTargetFormat = FCT::Format::R16G16B16A16_SFLOAT;
                bool enableShadows = true;
                bool enableSsao = true;
            };

            /**
             * @brief 延迟光照计算通道节点
             */
            class ENGINE_API DeferredLightingPass
            {
            public:
                explicit DeferredLightingPass(const DeferredLightingPassDesc& desc = {})
                    : m_desc(desc)
                {
                }

                const DeferredLightingPassDesc& getDesc() const noexcept { return m_desc; }
                void setDimensions(uint32_t width, uint32_t height) noexcept
                {
                    m_desc.width = width;
                    m_desc.height = height;
                }

                const char* getPassName() const noexcept { return "DeferredLightingPass"; }

            private:
                DeferredLightingPassDesc m_desc;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_PASS_DEFERREDLIGHTINGPASS_H
