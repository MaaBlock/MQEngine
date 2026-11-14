#ifndef MQENGINE_DOMAIN_RENDERER_RENDERPACKET_H
#define MQENGINE_DOMAIN_RENDERER_RENDERPACKET_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
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
             * @brief 渲染通道标识
             */
            enum class RenderPassType : uint8_t
            {
                DepthPrepass,
                GBuffer,
                DeferredLighting,
                ForwardOpaque,
                ForwardTransparent,
                ShadowDepth,
                PostProcess,
                OverlayUi
            };

            /**
             * @brief 单个可渲染物体的提取数据包 (POD/免锁拷贝)
             * Note: 包含绘制所需的完整几何、材质与变换矩阵，完全与 ECS 解耦
             */
            struct RenderDrawPacket
            {
                uint64_t entityId = 0;
                uint32_t materialId = 0;
                uint32_t submeshIndex = 0;
                FCT::StaticMesh<uint32_t>* mesh = nullptr;

                FCT::Mat4 modelMatrix{};
                FCT::Mat4 modelInverseTransposeMatrix{};

                FCT::Vec4 baseColor = FCT::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
                float metallic = 0.0f;
                float roughness = 0.5f;
                float specular = 0.5f;
                float normalScale = 1.0f;

                uint32_t albedoTexSlot = 0;
                uint32_t normalTexSlot = 1;
                uint32_t metallicRoughnessTexSlot = 0;
                uint32_t emissiveTexSlot = 0;

                FCT::Vec3<float> aabbMin = FCT::Vec3<float>{-0.5f, -0.5f, -0.5f};
                FCT::Vec3<float> aabbMax = FCT::Vec3<float>{0.5f, 0.5f, 0.5f};

                bool castShadows = true;
                bool receiveShadows = true;
                bool doubleSided = false;
            };

            /**
             * @brief 摄像机视口与投影数据包
             */
            struct RenderViewPacket
            {
                FCT::Mat4 viewMatrix{};
                FCT::Mat4 projectionMatrix{};
                FCT::Mat4 viewProjectionMatrix{};
                FCT::Mat4 inverseViewProjectionMatrix{};

                FCT::Vec3<float> cameraPosition = FCT::Vec3<float>{0.0f, 0.0f, 0.0f};
                float nearPlane = 0.1f;
                FCT::Vec3<float> cameraDirection = FCT::Vec3<float>{0.0f, 0.0f, -1.0f};
                float farPlane = 1000.0f;

                uint32_t viewportWidth = 1920;
                uint32_t viewportHeight = 1080;
                float fovY = 60.0f;
                float aspectRatio = 16.0f / 9.0f;
            };

            /**
             * @brief 场景光源数据包
             */
            struct RenderLightPacket
            {
                enum class LightType : uint32_t
                {
                    Directional = 0,
                    Point = 1,
                    Spot = 2
                };

                LightType type = LightType::Directional;
                FCT::Vec3<float> position = FCT::Vec3<float>{0.0f, 0.0f, 0.0f};
                float range = 10.0f;

                FCT::Vec3<float> direction = FCT::Vec3<float>{0.0f, -1.0f, 0.0f};
                float intensity = 1.0f;

                FCT::Vec3<float> color = FCT::Vec3<float>{1.0f, 1.0f, 1.0f};
                float spotAngleCos = 0.707f; // cos(45 deg)

                bool castShadows = false;
                uint32_t shadowMapIndex = 0;
            };

            /**
             * @brief 完整渲染帧的数据封包 (提交线程独立读取，实现与主逻辑线程完全免锁解耦)
             */
            struct RenderFramePacket
            {
                uint64_t frameNumber = 0;
                double frameDeltaTime = 0.016667;
                double totalTime = 0.0;

                RenderViewPacket mainView;
                std::vector<RenderViewPacket> shadowViews;
                std::vector<RenderLightPacket> lights;
                std::vector<RenderDrawPacket> drawCommands;

                // 全局场景环境配置
                FCT::Vec4 ambientColor = FCT::Vec4{0.05f, 0.05f, 0.05f, 1.0f};
                float exposure = 1.0f;
                float bloomIntensity = 0.04f;
                bool enableDlss = false;
                bool enableRayTracing = false;

                void clear()
                {
                    shadowViews.clear();
                    lights.clear();
                    drawCommands.clear();
                }
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_RENDERPACKET_H
