/**
 * @file ScenePrimitiveMeshes.cpp
 * @brief 程序化几何基元网格生成器实现
 */
#include "ScenePrimitiveMeshes.h"

#include <algorithm>
#include <cmath>

namespace MQEngine
{
    namespace MQEditor
    {

        PrimitiveMeshData ScenePrimitiveMeshes::generateCubeMesh(float halfExtent)
        {
            PrimitiveMeshData mesh;
            const float h = halfExtent;

            // 6 个面，每面 4 个独立法线顶点
            const PrimitiveVertex kCubeVertices[24] = {
                // +Z 前面
                {{-h, -h,  h}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}},
                {{ h, -h,  h}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}},
                {{ h,  h,  h}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}},
                {{-h,  h,  h}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}},
                // -Z 后面
                {{ h, -h, -h}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}},
                {{-h, -h, -h}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}},
                {{-h,  h, -h}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}},
                {{ h,  h, -h}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}},
                // +Y 顶面
                {{-h,  h,  h}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}},
                {{ h,  h,  h}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}},
                {{ h,  h, -h}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}},
                {{-h,  h, -h}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}},
                // -Y 底面
                {{-h, -h, -h}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}},
                {{ h, -h, -h}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}},
                {{ h, -h,  h}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}},
                {{-h, -h,  h}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}},
                // +X 右面
                {{ h, -h,  h}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
                {{ h, -h, -h}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
                {{ h,  h, -h}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
                {{ h,  h,  h}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},
                // -X 左面
                {{-h, -h, -h}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
                {{-h, -h,  h}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
                {{-h,  h,  h}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
                {{-h,  h, -h}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}}
            };

            mesh.vertices.assign(std::begin(kCubeVertices), std::end(kCubeVertices));
            mesh.indices.reserve(36);
            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t base = face * 4;
                mesh.indices.push_back(base + 0);
                mesh.indices.push_back(base + 1);
                mesh.indices.push_back(base + 2);
                mesh.indices.push_back(base + 0);
                mesh.indices.push_back(base + 2);
                mesh.indices.push_back(base + 3);
            }
            return mesh;
        }

        PrimitiveMeshData ScenePrimitiveMeshes::generatePlaneMesh(float halfExtent, uint32_t subdivisions)
        {
            PrimitiveMeshData mesh;
            const uint32_t divs = std::max(1u, subdivisions);
            const uint32_t vertsPerRow = divs + 1;
            mesh.vertices.reserve(vertsPerRow * vertsPerRow);
            mesh.indices.reserve(divs * divs * 6);

            for (uint32_t z = 0; z < vertsPerRow; ++z)
            {
                const float v = static_cast<float>(z) / static_cast<float>(divs);
                const float posZ = (v * 2.0f - 1.0f) * halfExtent;
                for (uint32_t x = 0; x < vertsPerRow; ++x)
                {
                    const float u = static_cast<float>(x) / static_cast<float>(divs);
                    const float posX = (u * 2.0f - 1.0f) * halfExtent;
                    PrimitiveVertex vert{};
                    vert.position[0] = posX;
                    vert.position[1] = 0.0f;
                    vert.position[2] = posZ;
                    vert.normal[0] = 0.0f;
                    vert.normal[1] = 1.0f;
                    vert.normal[2] = 0.0f;
                    vert.uv[0] = u;
                    vert.uv[1] = v;
                    mesh.vertices.push_back(vert);
                }
            }

            for (uint32_t z = 0; z < divs; ++z)
            {
                for (uint32_t x = 0; x < divs; ++x)
                {
                    const uint32_t i0 = z * vertsPerRow + x;
                    const uint32_t i1 = i0 + 1;
                    const uint32_t i2 = (z + 1) * vertsPerRow + x;
                    const uint32_t i3 = i2 + 1;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }
            return mesh;
        }

        PrimitiveMeshData ScenePrimitiveMeshes::generateSphereMesh(float radius, uint32_t slices, uint32_t stacks)
        {
            PrimitiveMeshData mesh;
            const uint32_t safeSlices = std::max(4u, slices);
            const uint32_t safeStacks = std::max(4u, stacks);
            constexpr float kPi = 3.14159265358979323846f;

            for (uint32_t stack = 0; stack <= safeStacks; ++stack)
            {
                const float v = static_cast<float>(stack) / static_cast<float>(safeStacks);
                const float phi = v * kPi;
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                for (uint32_t slice = 0; slice <= safeSlices; ++slice)
                {
                    const float u = static_cast<float>(slice) / static_cast<float>(safeSlices);
                    const float theta = u * 2.0f * kPi;
                    const float sinTheta = std::sin(theta);
                    const float cosTheta = std::cos(theta);

                    const float nx = sinPhi * cosTheta;
                    const float ny = cosPhi;
                    const float nz = sinPhi * sinTheta;

                    PrimitiveVertex vert{};
                    vert.position[0] = nx * radius;
                    vert.position[1] = ny * radius;
                    vert.position[2] = nz * radius;
                    vert.normal[0] = nx;
                    vert.normal[1] = ny;
                    vert.normal[2] = nz;
                    vert.uv[0] = u;
                    vert.uv[1] = v;
                    mesh.vertices.push_back(vert);
                }
            }

            const uint32_t stride = safeSlices + 1;
            for (uint32_t stack = 0; stack < safeStacks; ++stack)
            {
                for (uint32_t slice = 0; slice < safeSlices; ++slice)
                {
                    const uint32_t i0 = stack * stride + slice;
                    const uint32_t i1 = i0 + 1;
                    const uint32_t i2 = (stack + 1) * stride + slice;
                    const uint32_t i3 = i2 + 1;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }
            return mesh;
        }

        PrimitiveMeshData ScenePrimitiveMeshes::generateSkyboxMesh(float halfExtent)
        {
            PrimitiveMeshData mesh = generateCubeMesh(halfExtent);
            // 翻转三角形绕序以供相机在立方体内部采样天空盒
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            {
                std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
            }
            return mesh;
        }

        const char* ScenePrimitiveMeshes::getDefaultViewportVertexShaderHlsl()
        {
            return R"(
cbuffer SceneConstants : register(b0)
{
    float4x4 uViewProj;
    float4x4 uWorld;
    float4   uCameraPos;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 svPos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

PSInput main(VSInput input)
{
    PSInput output;
    float4 wp = mul(uWorld, float4(input.position, 1.0));
    output.worldPos = wp.xyz;
    output.svPos = mul(uViewProj, wp);
    output.normal = normalize(mul((float3x3)uWorld, input.normal));
    output.uv = input.uv;
    return output;
}
)";
        }

        const char* ScenePrimitiveMeshes::getDefaultViewportPixelShaderHlsl()
        {
            return R"(
cbuffer MaterialConstants : register(b1)
{
    float4 uBaseColor;
    float4 uMetallicRoughness;
    float4 uLightDirection;
    float4 uLightColorIntensity;
};

struct PSInput
{
    float4 svPos    : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 L = normalize(-uLightDirection.xyz);
    float NdotL = max(dot(N, L), 0.08);
    float3 lit = uBaseColor.rgb * uLightColorIntensity.rgb * (NdotL * uLightColorIntensity.w);
    return float4(lit, 1.0);
}
)";
        }

    } // namespace MQEditor
} // namespace MQEngine
