#ifndef MQENGINE_DOMAIN_RENDERER_SHADERGENERATOR_H
#define MQENGINE_DOMAIN_RENDERER_SHADERGENERATOR_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief 着色器特性标志位
             */
            enum class ShaderFeatureBits : uint32_t
            {
                None = 0,
                HasAlbedoMap = 1 << 0,
                HasNormalMap = 1 << 1,
                HasMetallicRoughnessMap = 1 << 2,
                HasEmissiveMap = 1 << 3,
                HasOcclusionMap = 1 << 4,
                EnableSkinning = 1 << 5,
                EnableInstancing = 1 << 6,
                EnableShadowReceive = 1 << 7,
                EnableAlphaTest = 1 << 8
            };

            inline ShaderFeatureBits operator|(ShaderFeatureBits a, ShaderFeatureBits b)
            {
                return static_cast<ShaderFeatureBits>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
            }

            inline bool hasFeature(ShaderFeatureBits mask, ShaderFeatureBits bit)
            {
                return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0;
            }

            /**
             * @brief 着色器生成与模版管理器
             * Note: 严格声明 row_major float4x4 矩阵，并采用标准线性代数 * 乘法
             */
            class ENGINE_API ShaderGenerator
            {
            public:
                /**
                 * @brief 生成符合 Bindless / Standard PBR 规范的顶点着色器 HLSL 代码
                 */
                static std::string generatePbrVertexShader(ShaderFeatureBits features);

                /**
                 * @brief 生成符合 Bindless / Standard PBR 规范的像素着色器 HLSL 代码
                 */
                static std::string generatePbrPixelShader(ShaderFeatureBits features);

                /**
                 * @brief 生成延迟渲染 G-Buffer 阶段的顶点着色器
                 */
                static std::string generateGBufferVertexShader();

                /**
                 * @brief 生成延迟渲染 G-Buffer 阶段的像素着色器
                 */
                static std::string generateGBufferPixelShader();

                /**
                 * @brief 生成阴影深度图通道的顶点着色器
                 */
                static std::string generateShadowDepthVertexShader();

                /**
                 * @brief 生成后处理 ToneMapping / Bloom 合成着色器
                 */
                static std::string generatePostProcessPixelShader();
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_SHADERGENERATOR_H
