#ifndef MQENGINE_DOMAIN_RENDERER_DATA_GPUINSTANCE_H
#define MQENGINE_DOMAIN_RENDERER_DATA_GPUINSTANCE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief Vulkan 描述符索引与 Bindless 硬件能力档位
             */
            enum class DescriptorIndexingTier : uint8_t
            {
                Tier0_AtlasFallback = 0, // 纯 Vulkan 1.0 全局 Atlas 保底档（零 Bindless 扩展依赖）
                Tier1_BasicBindless = 1, // 基础动态索引档（支持 NonUniform 采样数组，无 UpdateAfterBind）
                Tier2_UpdateAfterBind = 2, // 满血 Bindless 档（支持 PartiallyBound + UpdateAfterBind）
                Tier3_UnboundedFull = 3   // 无界全功能档（支持动态 SSBO 数组 + RT 加速结构绑定）
            };

            /**
             * @brief 单实例 GPU 显存记录（对齐 std430，严格 160 字节）
             */
            struct alignas(16) GpuInstanceRow
            {
                FCT::Mat4 modelMatrix;        // 64B: 世界变换矩阵（严格复用空间计算结果）
                FCT::Mat4 invTransposeMatrix; // 64B: 法线逆转置矩阵
                FCT::Vec4 worldSphereBounds;  // 16B: xyz 世界球心, w 世界半径
                uint32_t  meshSlot = 0;       // 4B : 关联网格元数据表下标
                uint32_t  materialSlot = 0;   // 4B : 关联 GpuMaterialTable 下标
                uint32_t  entityId = 0;       // 4B : 对应 ECS Entity，供视口 GPU Picking 读回
                uint32_t  flags = 1;          // 4B : 标记位（bit 0: 可见, bit 1: 辅助网格）
            };
            static_assert(sizeof(GpuInstanceRow) == 160, "GpuInstanceRow must be 160 bytes");

            /**
             * @brief 紧凑材质 GPU 显存记录（对齐 std430，严格 64 字节）
             */
            struct alignas(16) GpuMaterialRecord
            {
                FCT::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};           // 16B: 基础颜色 RGBA
                FCT::Vec4 metallicRoughnessNormalOcclusion{0.0f, 0.5f, 1.0f, 1.0f}; // 16B: x=metallic, y=roughness, z=normalScale, w=ao
                FCT::Vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f};            // 16B: rgb=自发光, w=强度
                uint32_t  albedoTexIndex = 0;                                // 4B : 漫反射贴图槽位（0 为默认白）
                uint32_t  normalTexIndex = 1;                                // 4B : 法线贴图槽位（1 为默认平坦法线）
                uint32_t  ormTexIndex = 0;                                   // 4B : ORM 贴图槽位
                uint16_t  shadingModelId = 0;                                // 2B : 着色模型 ID
                uint16_t  pipelineBucket = 0;                                // 2B : 管线分桶 ID
            };
            static_assert(sizeof(GpuMaterialRecord) == 64, "GpuMaterialRecord must be 64 bytes");

            /**
             * @brief 纹理槽位元数据记录（对齐 std430，严格 32 字节）
             */
            struct alignas(16) GpuTextureRecord
            {
                FCT::Vec4 uvScaleOffset{1.0f, 1.0f, 0.0f, 0.0f}; // 16B: xy=UV缩放, zw=UV偏移
                uint32_t  bindlessOrPageIndex = 0;                // 4B : Tier 1 为 slot; Tier 0 为 Atlas 层号
                uint32_t  flags = 0;                              // 4B : 采样模式标记
                uint32_t  pad0 = 0;                               // 4B
                uint32_t  pad1 = 0;                               // 4B
            };
            static_assert(sizeof(GpuTextureRecord) == 32, "GpuTextureRecord must be 32 bytes");

            /**
             * @brief 全局帧/视口/光照/阴影/环境光 Uniform 常量（严格 512 字节）
             */
            struct alignas(16) GpuSceneUniformData
            {
                FCT::Mat4 viewMatrix;
                FCT::Mat4 projectionMatrix;
                FCT::Mat4 viewProjectionMatrix;
                FCT::Vec4 cameraWorldPos{0.0f, 0.0f, 0.0f, 1.0f};
                FCT::Vec4 directionalLightDirection{-0.42f, -0.74f, 0.52f, 1.0f};
                FCT::Vec4 directionalLightColorIntensity{1.0f, 0.95f, 0.86f, 4.2f};
                FCT::Vec4 viewportSize{1920.0f, 1080.0f, 1.0f / 1920.0f, 1.0f / 1080.0f};
                FCT::Mat4 lightViewProjectionMatrix;
                FCT::Mat4 invViewProjectionMatrix;
                FCT::Vec4 shadowParams{1.0f, 0.0009f, 0.032f, 1.0f / 2048.0f};
                FCT::Vec4 pointLightPosRange0{0.0f, 0.0f, 0.0f, 0.0f};
                FCT::Vec4 pointLightColorIntensity0{0.0f, 0.0f, 0.0f, 0.0f};
                FCT::Vec4 pointLightPosRange1{0.0f, 0.0f, 0.0f, 0.0f};
                FCT::Vec4 pointLightColorIntensity1{0.0f, 0.0f, 0.0f, 0.0f};
                FCT::Vec4 environmentParams{1.05f, 1.0f, 1.0f, 1.0f};
                FCT::Vec4 skyZenithColor{0.042f, 0.132f, 0.38f, 1.0f};
                FCT::Vec4 skyHorizonGroundColor{0.28f, 0.39f, 0.55f, 0.065f};
            };
            static_assert(sizeof(GpuSceneUniformData) == 512, "GpuSceneUniformData must be 512 bytes");

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_DATA_GPUINSTANCE_H
