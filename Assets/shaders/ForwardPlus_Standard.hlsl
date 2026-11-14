// 3D Clustered Forward+ 与 GPUScene 跨材质合批标准着色器 (PRD 02, FEAT-09~15)
cbuffer PerFrameUniforms : register(b0)
{
    float4x4 uViewMatrix;
    float4x4 uProjMatrix;
    float4x4 uViewProjMatrix;
    float4   uCameraWorldPos;
    float4   uDirLightDir;
    float4   uDirLightColorIntensity;
    float4   uFroxelZParams; // x = near, y = far, z = sliceScale, w = sliceBias
    float4x4 uShadowAtlasViewProj;
    float4   uShadowAtlasUvScaleOffset;
};

// 1024 宽 GL_RGBA32UI 2D 数据纹理承载 GPUScene 实例与材质表 (FEAT-14)
Texture2D<uint4> g_GpuSceneDataTex : register(t0);
Texture2DArray<float4> g_GlobalAtlasArray : register(t1);
Texture2D<float> g_ShadowAtlas : register(t2);

SamplerState g_LinearSampler : register(s0);
SamplerComparisonState g_ShadowCmpSampler : register(s1);

struct VSInput
{
    float3 position     : POSITION;
    float3 normal       : NORMAL;
    float2 texCoord     : TEXCOORD0;
    uint   instanceSlot : TEXCOORD1; // PerInstance R32_UINT 槽位流 (FEAT-15)
};

struct PSInput
{
    float4 svPosition   : SV_Position;
    float3 worldPos     : TEXCOORD0;
    float3 worldNormal  : TEXCOORD1;
    float2 uv           : TEXCOORD2;
    nointerpolation uint materialSlot : TEXCOORD3;
};

uint4 LoadSceneTexel(uint linearTexelIndex)
{
    int2 coord = int2(linearTexelIndex & 1023u, linearTexelIndex >> 10u);
    return g_GpuSceneDataTex.Load(int3(coord, 0));
}

PSInput VSMain(VSInput input)
{
    PSInput output;
    uint baseTexel = input.instanceSlot * 10u; // 160B = 10 * 16B texels
    float4 row0 = asfloat(LoadSceneTexel(baseTexel + 0u));
    float4 row1 = asfloat(LoadSceneTexel(baseTexel + 1u));
    float4 row2 = asfloat(LoadSceneTexel(baseTexel + 2u));
    uint4  meta = LoadSceneTexel(baseTexel + 9u);

    float3 worldPos;
    worldPos.x = dot(row0, float4(input.position, 1.0));
    worldPos.y = dot(row1, float4(input.position, 1.0));
    worldPos.z = dot(row2, float4(input.position, 1.0));

    output.worldPos = worldPos;
    output.worldNormal = normalize(input.normal);
    output.uv = input.texCoord;
    output.materialSlot = meta.y;
    output.svPosition = mul(uViewProjMatrix, float4(worldPos, 1.0));
    return output;
}

float4 PSMain(PSInput input) : SV_Target0
{
    uint matBaseTexel = 65536u + input.materialSlot * 4u; // 64B = 4 * 16B texels
    float4 baseColorFactor = asfloat(LoadSceneTexel(matBaseTexel + 0u));
    uint4  texIndices = LoadSceneTexel(matBaseTexel + 3u);

    float4 albedo = g_GlobalAtlasArray.Sample(g_LinearSampler, float3(input.uv, float(texIndices.x & 255u)));
    albedo *= baseColorFactor;

    float3 N = normalize(input.worldNormal);
    float3 L = normalize(-uDirLightDir.xyz);
    float ndl = saturate(dot(N, L));

    float4 shadowClip = mul(uShadowAtlasViewProj, float4(input.worldPos, 1.0));
    float3 shadowNdc = shadowClip.xyz / max(shadowClip.w, 1e-5);
    float2 localShadowUv = shadowNdc.xy * 0.5 + 0.5;
    float2 atlasUv = localShadowUv * uShadowAtlasUvScaleOffset.xy + uShadowAtlasUvScaleOffset.zw;
    float shadowFactor = g_ShadowAtlas.SampleCmpLevelZero(g_ShadowCmpSampler, atlasUv, shadowNdc.z * 0.5 + 0.5);

    float3 lit = albedo.rgb * (0.22 + ndl * shadowFactor * uDirLightColorIntensity.rgb * uDirLightColorIntensity.w);
    return float4(lit, albedo.a);
}
