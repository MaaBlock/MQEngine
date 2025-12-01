#include "engine.h"
#include "../manager/ShaderSnippetManager.h"
#include <spdlog/spdlog.h>

namespace MQEngine {
    void Engine::registerShaderSnippets() {
        auto registerSnippet = [this](const std::string& uuid, const std::string& name, const std::string& source) {
            Status status = m_shaderSnippetManager->registerSnippet(uuid, name, source);
            if (!status.ok()) {
                spdlog::error("注册片段 {} 失败: {}", name, status.message());
            }
        };

        registerSnippet(
                "550e8400-e29b-41d4-a716-446655440007",
                "ParseOrmTexture",
                R"(
void main(SamplerState textureSampler,in float2 uv,out float ao,
    out float roughness,
    out float metallic
){
    float3 ormSample  = ormTexture.Sample(textureSampler, sIn.texCoord).rgb;
    ao         = ormSample.r;
    roughness  = ormSample.g;
    metallic   = ormSample.b;
}
)");

        // Register common PBR snippets
        registerSnippet("550e8400-e29b-41d4-a716-446655440000", "DistributionGGX", R"(
            void main(in float3 N, in float3 H, in float roughness, out float D)
            {
                float a      = roughness*roughness;
                float a2     = a*a;
                float NdotH  = max(dot(N, H), 0.0);
                float NdotH2 = NdotH*NdotH;
            
                float num   = a2;
                float denom = (NdotH2 * (a2 - 1.0) + 1.0);
                denom = 3.14159265359 * denom * denom;
            
                D = num / denom;
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440001", "GeometrySchlickGGX", R"(
            float GeometrySchlickGGX_Internal(float NdotV, float roughness)
            {
                float r = (roughness + 1.0);
                float k = (r*r) / 8.0;
                float num   = NdotV;
                float denom = NdotV * (1.0 - k) + k;
                return num / denom;
            }

            void main(in float NdotV, in float roughness, out float G)
            {
                G = GeometrySchlickGGX_Internal(NdotV, roughness);
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440002", "GeometrySmith", R"(
            float GeometrySchlickGGX_Internal(float NdotV, float roughness)
            {
                float r = (roughness + 1.0);
                float k = (r*r) / 8.0;
                float num   = NdotV;
                float denom = NdotV * (1.0 - k) + k;
                return num / denom;
            }

            void main(in float3 N, in float3 V, in float3 L, in float roughness, out float G)
            {
                float NdotV = max(dot(N, V), 0.0);
                float NdotL = max(dot(N, L), 0.0);
                float ggx2  = GeometrySchlickGGX_Internal(NdotV, roughness);
                float ggx1  = GeometrySchlickGGX_Internal(NdotL, roughness);
            
                G = ggx1 * ggx2;
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440003", "FresnelSchlick", R"(
            void main(in float cosTheta, in float3 F0, out float3 F)
            {
                F = F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
            }
        )");

        // Register Vertex Transformation Snippets
        registerSnippet("550e8400-e29b-41d4-a716-446655440004", "TransformVertex", R"(
            void main(
                in float3 positionOS, 
                in float4x4 modelMatrix, 
                in float4x4 viewProjMatrix, 
                out float4 positionCS, 
                out float3 positionWS)
            {
                float4 posWS = mul(modelMatrix, float4(positionOS, 1.0));
                positionWS = posWS.xyz;
                positionCS = mul(viewProjMatrix, posWS);
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440005", "TransformNormal", R"(
            void main(
                in float3 normalOS, 
                in float4x4 modelMatrix, 
                out float3 normalWS)
            {
                // Assuming uniform scaling, we can use the model matrix directly.
                // For non-uniform scaling, we should use the inverse-transpose.
                normalWS = normalize(mul((float3x3)modelMatrix, normalOS));
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440006", "TextureSample", R"(
            void main(
                in Texture2D tex, 
                in SamplerState samp, 
                in float2 uv, 
                out float4 color)
            {
                color = tex.Sample(samp, uv);
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440008", "ToFloat3", R"(
            void main(in float4 input, out float3 output) {
                output = input.xyz;
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440009", "Normalize", R"(
            void main(in float3 input, out float3 output) {
                output = normalize(input);
            }
        )");

        registerSnippet("550e8400-e29b-41d4-a716-446655440010", "CalculateNormalTBN", R"(
            void main(in float3 T, in float3 B, in float3 N, in float3 normalTS, out float3 normalWS) {
                // Unpack normal from [0, 1] to [-1, 1]
                float3 unpackedNormal = normalTS * 2.0 - 1.0;
                
                float3x3 TBN = float3x3(normalize(T), normalize(B), normalize(N));
                normalWS = normalize(mul(unpackedNormal, TBN));
            }
        )");
    }
}
