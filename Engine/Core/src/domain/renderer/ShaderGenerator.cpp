#include "ShaderGenerator.h"
#include <sstream>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            std::string ShaderGenerator::generatePbrVertexShader(ShaderFeatureBits features)
            {
                std::ostringstream ss;
                ss << "#include \"BindlessCommon.hlsli\"\n\n"
                   << "struct VSInput\n"
                   << "{\n"
                   << "    float3 position : POSITION;\n"
                   << "    float3 normal : NORMAL;\n"
                   << "    float2 uv : TEXCOORD0;\n";

                if (hasFeature(features, ShaderFeatureBits::HasNormalMap)) {
                    ss << "    float4 tangent : TANGENT;\n";
                }

                ss << "    uint instanceId : SV_InstanceID;\n"
                   << "};\n\n"
                   << "struct VSOutput\n"
                   << "{\n"
                   << "    float4 positionCS : SV_POSITION;\n"
                   << "    float3 worldPos : TEXCOORD0;\n"
                   << "    float3 normalWS : TEXCOORD1;\n"
                   << "    float2 uv : TEXCOORD2;\n";

                if (hasFeature(features, ShaderFeatureBits::HasNormalMap)) {
                    ss << "    float3 tangentWS : TEXCOORD3;\n"
                       << "    float3 bitangentWS : TEXCOORD4;\n";
                }

                ss << "    nointerpolation uint materialSlot : TEXCOORD5;\n"
                   << "    nointerpolation uint instanceId : TEXCOORD6;\n"
                   << "};\n\n"
                   << "VSOutput main(VSInput input)\n"
                   << "{\n"
                   << "    VSOutput output;\n"
                   << "    GpuInstanceRow instance = g_Instances[input.instanceId];\n"
                   << "    float4 localPos = float4(input.position, 1.0f);\n"
                   << "    float4 worldPos = mul(instance.modelMatrix, localPos);\n"
                   << "    output.worldPos = worldPos.xyz;\n"
                   << "    output.positionCS = mul(g_ViewProjectionMatrix, worldPos);\n"
                   << "    output.normalWS = normalize(mul((row_major float3x3)instance.invTransposeMatrix, input.normal));\n"
                   << "    output.uv = input.uv;\n";

                if (hasFeature(features, ShaderFeatureBits::HasNormalMap)) {
                    ss << "    output.tangentWS = normalize(mul((row_major float3x3)instance.modelMatrix, input.tangent.xyz));\n"
                       << "    output.bitangentWS = cross(output.normalWS, output.tangentWS) * input.tangent.w;\n";
                }

                ss << "    output.materialSlot = instance.materialSlot;\n"
                   << "    output.instanceId = input.instanceId;\n"
                   << "    return output;\n"
                   << "}\n";

                return ss.str();
            }

            std::string ShaderGenerator::generatePbrPixelShader(ShaderFeatureBits features)
            {
                std::ostringstream ss;
                ss << "#include \"BindlessCommon.hlsli\"\n\n"
                   << "struct PSInput\n"
                   << "{\n"
                   << "    float4 positionCS : SV_POSITION;\n"
                   << "    float3 worldPos : TEXCOORD0;\n"
                   << "    float3 normalWS : TEXCOORD1;\n"
                   << "    float2 uv : TEXCOORD2;\n";

                if (hasFeature(features, ShaderFeatureBits::HasNormalMap)) {
                    ss << "    float3 tangentWS : TEXCOORD3;\n"
                       << "    float3 bitangentWS : TEXCOORD4;\n";
                }

                ss << "    nointerpolation uint materialSlot : TEXCOORD5;\n"
                   << "    nointerpolation uint instanceId : TEXCOORD6;\n"
                   << "};\n\n"
                   << "static const float PI = 3.14159265359f;\n\n"
                   << "float distributionGGX(float3 N, float3 H, float roughness)\n"
                   << "{\n"
                   << "    float a = roughness * roughness;\n"
                   << "    float a2 = a * a;\n"
                   << "    float NdotH = max(dot(N, H), 0.0f);\n"
                   << "    float NdotH2 = NdotH * NdotH;\n"
                   << "    float nom = a2;\n"
                   << "    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);\n"
                   << "    denom = PI * denom * denom;\n"
                   << "    return nom / max(denom, 0.0000001f);\n"
                   << "}\n\n"
                   << "float geometrySchlickGGX(float NdotV, float roughness)\n"
                   << "{\n"
                   << "    float r = (roughness + 1.0f);\n"
                   << "    float k = (r * r) / 8.0f;\n"
                   << "    float nom = NdotV;\n"
                   << "    float denom = NdotV * (1.0f - k) + k;\n"
                   << "    return nom / max(denom, 0.0000001f);\n"
                   << "}\n\n"
                   << "float geometrySmith(float3 N, float3 V, float3 L, float roughness)\n"
                   << "{\n"
                   << "    float NdotV = max(dot(N, V), 0.0f);\n"
                   << "    float NdotL = max(dot(N, L), 0.0f);\n"
                   << "    float ggx2 = geometrySchlickGGX(NdotV, roughness);\n"
                   << "    float ggx1 = geometrySchlickGGX(NdotL, roughness);\n"
                   << "    return ggx1 * ggx2;\n"
                   << "}\n\n"
                   << "float3 fresnelSchlick(float cosTheta, float3 F0)\n"
                   << "{\n"
                   << "    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);\n"
                   << "}\n\n"
                   << "float4 main(PSInput input) : SV_TARGET\n"
                   << "{\n"
                   << "    GpuMaterialRecord mat = g_Materials[input.materialSlot];\n"
                   << "    float4 albedo = mat.baseColorFactor;\n"
                   << "    albedo *= SampleBindlessTexture(mat.albedoTexIndex, input.uv);\n\n"
                   << "    float3 N = normalize(input.normalWS);\n";

                if (hasFeature(features, ShaderFeatureBits::HasNormalMap)) {
                    ss << "    if (mat.normalTexIndex != 1u) {\n"
                       << "        float3 normalMap = SampleBindlessTexture(mat.normalTexIndex, input.uv).xyz * 2.0f - 1.0f;\n"
                       << "        normalMap.xy *= mat.metallicRoughnessNormalOcclusion.z;\n"
                       << "        float3x3 TBN = float3x3(normalize(input.tangentWS), normalize(input.bitangentWS), N);\n"
                       << "        N = normalize(mul(normalMap, TBN));\n"
                       << "    }\n";
                }

                ss << "    float metal = mat.metallicRoughnessNormalOcclusion.x;\n"
                   << "    float rough = mat.metallicRoughnessNormalOcclusion.y;\n"
                   << "    float ao = mat.metallicRoughnessNormalOcclusion.w;\n"
                   << "    if (mat.ormTexIndex != 0u) {\n"
                   << "        float4 mrSample = SampleBindlessTexture(mat.ormTexIndex, input.uv);\n"
                   << "        ao *= mrSample.r;\n"
                   << "        rough *= mrSample.g;\n"
                   << "        metal *= mrSample.b;\n"
                   << "    }\n\n"
                   << "    bool isOrtho = (abs(g_ProjectionMatrix[3][3]) > 0.5f);\n"
                   << "    float3 V = isOrtho ? normalize(-g_ViewMatrix[2].xyz) : normalize(g_CameraWorldPos.xyz - input.worldPos);\n"
                   << "    float3 L = normalize(-g_DirectionalLightDirection.xyz);\n"
                   << "    float3 H = normalize(V + L);\n\n"
                   << "    float3 F0 = float3(0.04f, 0.04f, 0.04f);\n"
                   << "    F0 = lerp(F0, albedo.rgb, metal);\n\n"
                   << "    float NDF = distributionGGX(N, H, rough);\n"
                   << "    float G = geometrySmith(N, V, L, rough);\n"
                   << "    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);\n\n"
                   << "    float3 numerator = NDF * G * F;\n"
                   << "    float denominator = 4.0f * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.0001f;\n"
                   << "    float3 specularBRDF = numerator / denominator;\n\n"
                   << "    float3 kS = F;\n"
                   << "    float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metal);\n\n"
                   << "    float NdotL = max(dot(N, L), 0.0f);\n"
                   << "    float3 lightRadiance = g_DirectionalLightColorIntensity.rgb * g_DirectionalLightColorIntensity.w * g_DirectionalLightDirection.w;\n"
                   << "    float3 Lo = (kD * albedo.rgb / PI + specularBRDF) * lightRadiance * NdotL;\n"
                   << "    float3 ambient = float3(0.03f, 0.03f, 0.03f) * albedo.rgb * ao;\n"
                   << "    float3 emissive = mat.emissiveFactor.rgb * mat.emissiveFactor.w;\n"
                   << "    float3 finalColor = ambient + Lo + emissive;\n\n"
                   << "    return float4(finalColor, albedo.a);\n"
                   << "}\n";

                return ss.str();
            }

            std::string ShaderGenerator::generateGBufferVertexShader()
            {
                std::ostringstream ss;
                ss << "cbuffer SceneUniform : register(b0, space0)\n"
                   << "{\n"
                   << "    row_major float4x4 viewMatrix;\n"
                   << "    row_major float4x4 projectionMatrix;\n"
                   << "    row_major float4x4 viewProjectionMatrix;\n"
                   << "    float4 cameraPosition;\n"
                   << "    float4 lightDirection;\n"
                   << "    float4 lightColor;\n"
                   << "};\n\n"
                   << "cbuffer ModelUniform : register(b1, space0)\n"
                   << "{\n"
                   << "    row_major float4x4 modelMatrix;\n"
                   << "    row_major float4x4 modelInverseTransposeMatrix;\n"
                   << "};\n\n"
                   << "struct VSInput\n"
                   << "{\n"
                   << "    float3 position : POSITION;\n"
                   << "    float3 normal : NORMAL;\n"
                   << "    float2 uv : TEXCOORD0;\n"
                   << "};\n\n"
                   << "struct VSOutput\n"
                   << "{\n"
                   << "    float4 positionCS : SV_POSITION;\n"
                   << "    float3 worldPos : TEXCOORD0;\n"
                   << "    float3 normalWS : TEXCOORD1;\n"
                   << "    float2 uv : TEXCOORD2;\n"
                   << "};\n\n"
                   << "VSOutput main(VSInput input)\n"
                   << "{\n"
                   << "    VSOutput output;\n"
                   << "    float4 localPos = float4(input.position, 1.0f);\n"
                   << "    float4 worldPos = modelMatrix * localPos;\n"
                   << "    output.worldPos = worldPos.xyz;\n"
                   << "    output.positionCS = projectionMatrix * (viewMatrix * worldPos);\n"
                   << "    output.normalWS = normalize((row_major float3x3)modelInverseTransposeMatrix * input.normal);\n"
                   << "    output.uv = input.uv;\n"
                   << "    return output;\n"
                   << "}\n";

                return ss.str();
            }

            std::string ShaderGenerator::generateGBufferPixelShader()
            {
                std::ostringstream ss;
                ss << "cbuffer MaterialUniform : register(b2, space0)\n"
                   << "{\n"
                   << "    float4 baseColor;\n"
                   << "    float metallic;\n"
                   << "    float roughness;\n"
                   << "    float specular;\n"
                   << "    float normalScale;\n"
                   << "};\n\n"
                   << "struct PSInput\n"
                   << "{\n"
                   << "    float4 positionCS : SV_POSITION;\n"
                   << "    float3 worldPos : TEXCOORD0;\n"
                   << "    float3 normalWS : TEXCOORD1;\n"
                   << "    float2 uv : TEXCOORD2;\n"
                   << "};\n\n"
                   << "struct PSOutput\n"
                   << "{\n"
                   << "    float4 gbuffer0_AlbedoAO : SV_Target0;\n"
                   << "    float4 gbuffer1_NormalRoughness : SV_Target1;\n"
                   << "    float4 gbuffer2_PositionMetallic : SV_Target2;\n"
                   << "};\n\n"
                   << "PSOutput main(PSInput input)\n"
                   << "{\n"
                   << "    PSOutput output;\n"
                   << "    output.gbuffer0_AlbedoAO = float4(baseColor.rgb, 1.0);\n"
                   << "    output.gbuffer1_NormalRoughness = float4(normalize(input.normalWS) * 0.5 + 0.5, roughness);\n"
                   << "    output.gbuffer2_PositionMetallic = float4(input.worldPos, metallic);\n"
                   << "    return output;\n"
                   << "}\n";

                return ss.str();
            }

            std::string ShaderGenerator::generateShadowDepthVertexShader()
            {
                std::ostringstream ss;
                ss << "cbuffer ShadowPassUniform : register(b0, space0)\n"
                   << "{\n"
                   << "    row_major float4x4 lightViewProjectionMatrix;\n"
                   << "};\n\n"
                   << "cbuffer ModelUniform : register(b1, space0)\n"
                   << "{\n"
                   << "    row_major float4x4 modelMatrix;\n"
                   << "    row_major float4x4 modelInverseTransposeMatrix;\n"
                   << "};\n\n"
                   << "struct VSInput\n"
                   << "{\n"
                   << "    float3 position : POSITION;\n"
                   << "};\n\n"
                   << "float4 main(VSInput input) : SV_POSITION\n"
                   << "{\n"
                   << "    float4 worldPos = modelMatrix * float4(input.position, 1.0f);\n"
                   << "    return lightViewProjectionMatrix * worldPos;\n"
                   << "}\n";

                return ss.str();
            }

            std::string ShaderGenerator::generatePostProcessPixelShader()
            {
                std::ostringstream ss;
                ss << "Texture2D t_SceneColor : register(t0, space0);\n"
                   << "Texture2D t_BloomBlur : register(t1, space0);\n"
                   << "SamplerState s_Linear : register(s0, space0);\n\n"
                   << "cbuffer PostProcessUniform : register(b0, space0)\n"
                   << "{\n"
                   << "    float exposure;\n"
                   << "    float bloomIntensity;\n"
                   << "    float gamma;\n"
                   << "    float padding;\n"
                   << "};\n\n"
                   << "struct PSInput\n"
                   << "{\n"
                   << "    float4 positionCS : SV_POSITION;\n"
                   << "    float2 uv : TEXCOORD0;\n"
                   << "};\n\n"
                   << "float3 acesTonemap(float3 x)\n"
                   << "{\n"
                   << "    const float a = 2.51f;\n"
                   << "    const float b = 0.03f;\n"
                   << "    const float c = 2.43f;\n"
                   << "    const float d = 0.59f;\n"
                   << "    const float e = 0.14f;\n"
                   << "    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);\n"
                   << "}\n\n"
                   << "float4 main(PSInput input) : SV_TARGET\n"
                   << "{\n"
                   << "    float3 hdrColor = t_SceneColor.Sample(s_Linear, input.uv).rgb;\n"
                   << "    float3 bloomColor = t_BloomBlur.Sample(s_Linear, input.uv).rgb;\n"
                   << "    hdrColor += bloomColor * bloomIntensity;\n"
                   << "    hdrColor *= exposure;\n"
                   << "    float3 ldrColor = acesTonemap(hdrColor);\n"
                   << "    ldrColor = pow(ldrColor, float3(1.0 / gamma, 1.0 / gamma, 1.0 / gamma));\n"
                   << "    return float4(ldrColor, 1.0);\n"
                   << "}\n";

                return ss.str();
            }

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine
