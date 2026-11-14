/**
 * @file BindlessCommon.hlsli
 * @brief MQEngine 全局 Bindless Set 0 统一着色器声明、PBR/IBL 物理光照、软阴影 PCF 与大气天空核心库
 * @note 与 C++ 端 GpuInstance.h (GpuInstanceRow, GpuMaterialRecord, GpuTextureRecord, GpuSceneUniformData)
 *       保持严格二进制 std430/std140 内存对齐 (alignas 16)。
 */

#ifndef MQENGINE_BINDLESS_COMMON_HLSLI
#define MQENGINE_BINDLESS_COMMON_HLSLI

#ifndef NonUniformResourceIndex
#define NonUniformResourceIndex(x) (x)
#endif

static const float MQ_PI = 3.14159265359f;

// =============================================================================
// 1. GPU 显存数据结构定义 (与 C++ 严格对齐)
// =============================================================================

struct GpuInstanceRow
{
    row_major float4x4 modelMatrix;        // 64B: 世界变换矩阵 (行主序)
    row_major float4x4 invTransposeMatrix; // 64B: 法线逆转置矩阵 (行主序，同时满足 rowVec * invTranspose = M^-1 * worldPt)
    float4 worldSphereBounds;              // 16B: xyz=局部半尺寸 (halfExtents), w=图元形状标记 (1.0=球体, 0.0=盒体)
    uint meshSlot;                         // 4B : 关联网格元数据表下标
    uint materialSlot;                     // 4B : 关联材质记录表下标 (g_Materials)
    uint entityId;                         // 4B : ECS 实体 ID (供 GPU Picking/视口选中读回)
    uint flags;                            // 4B : 标记位 (bit 0: 可见, bit 1: 地面参考网格, bit 2: 投射阴影/反射遮挡体)
};

struct GpuMaterialRecord
{
    float4 baseColorFactor;                  // 16B: 基础颜色 RGBA (线性空间)
    float4 metallicRoughnessNormalOcclusion; // 16B: x=metallic, y=roughness, z=normalScale, w=ao
    float4 emissiveFactor;                   // 16B: rgb=自发光颜色(线性), w=发光强度
    uint albedoTexIndex;                     // 4B : 漫反射/基色贴图槽位 (Slot 0 为纯白回退)
    uint normalTexIndex;                     // 4B : 法线贴图槽位 (Slot 1 为平坦法线回退)
    uint ormTexIndex;                        // 4B : 复合遮罩贴图槽位 (r=ao, g=rough, b=metal)
    uint packedShadingModelAndBucket;        // 4B : 低 16 位=着色模型 ID, 高 16 位=管线分桶 ID
};

struct GpuTextureRecord
{
    float4 uvScaleOffset;       // 16B: xy=UV 缩放 (scale), zw=UV 偏移 (offset)
    uint bindlessOrPageIndex;   // 4B : Tier 1-3 下为描述符槽位号; Tier 0 图集下为层号
    uint flags;                 // 4B : 采样标记 (bit 0: 使用 Atlas, bit 1: sRGB, bit 2: 虚拟纹理)
    uint pad0;                  // 4B : 内存对齐填充
    uint pad1;                  // 4B : 内存对齐填充
};

// =============================================================================
// 2. 全局 Set 0 资源声明 (空间 space0 独占，严格 512 字节 UBO)
// =============================================================================

cbuffer SceneUniforms : register(b0, space0)
{
    row_major float4x4 g_ViewMatrix;
    row_major float4x4 g_ProjectionMatrix;
    row_major float4x4 g_ViewProjectionMatrix;
    float4 g_CameraWorldPos;                 // xyz=相机世界坐标, w=抗锯齿启用开关 (1.0=开, 0.0=关)
    float4 g_DirectionalLightDirection;      // xyz=光线传播方向, w=光源启用标记 (1.0=开, 0.0=关)
    float4 g_DirectionalLightColorIntensity; // rgb=线性光照颜色, w=HDR 辐射照度
    float4 g_ViewportSize;                   // x=宽, y=高, z=1/宽, w=1/高
    row_major float4x4 g_LightViewProjectionMatrix;
    row_major float4x4 g_InvViewProjectionMatrix;
    float4 g_ShadowParams;                   // x=阴影强度, y=深度偏移, z=法线偏移, w=1/阴影图尺寸
    float4 g_PointLightPosRange0;            // xyz=位置, w=衰减半径
    float4 g_PointLightColorIntensity0;      // rgb=线性颜色, w=发光强度
    float4 g_PointLightPosRange1;
    float4 g_PointLightColorIntensity1;
    float4 g_EnvironmentParams;              // x=曝光度, y=天空强度, z=IBL强度, w=太阳圆盘强度
    float4 g_SkyZenithColor;                 // rgb=天顶线性辐射度, w=活动实例总数 (instanceCount)
    float4 g_SkyHorizonGroundColor;          // rgb=地平线线性辐射度, w=地面漫反射反弹率
};

StructuredBuffer<GpuInstanceRow> g_Instances : register(t1, space0);
StructuredBuffer<GpuMaterialRecord> g_Materials : register(t2, space0);
StructuredBuffer<GpuTextureRecord> g_TexturesMeta : register(t3, space0);
Texture2D g_Textures[] : register(t4, space0);
Texture2D g_AtlasTexture : register(t5, space0);
SamplerState g_LinearSampler : register(s6, space0);

struct PushConstants
{
    uint instanceIndex;
    uint materialSlot;
};

// =============================================================================
// 3. 统一 Bindless 动态采样辅助函数
// =============================================================================

float3 SrgbToLinearExact(float3 srgb)
{
    float3 lo = srgb / 12.92f;
    float3 hi = pow(max((srgb + 0.055f) / 1.055f, float3(0.0f, 0.0f, 0.0f)), 2.4f);
    return float3(
        (srgb.r <= 0.04045f) ? lo.r : hi.r,
        (srgb.g <= 0.04045f) ? lo.g : hi.g,
        (srgb.b <= 0.04045f) ? lo.b : hi.b);
}

float4 SampleBindlessTexture(uint textureSlot, float2 uv)
{
    GpuTextureRecord meta = g_TexturesMeta[textureSlot];
    float2 transformedUv = uv * meta.uvScaleOffset.xy + meta.uvScaleOffset.zw;

    float4 texel;
    if ((meta.flags & 0x1u) != 0u) {
        float2 atlasUv = frac(uv) * meta.uvScaleOffset.xy + meta.uvScaleOffset.zw;
        texel = g_AtlasTexture.Sample(g_LinearSampler, atlasUv);
    } else {
        texel = g_Textures[NonUniformResourceIndex(textureSlot)].Sample(g_LinearSampler, transformedUv);
    }

    if ((meta.flags & 0x2u) != 0u) {
        texel.rgb = SrgbToLinearExact(saturate(texel.rgb));
    }
    return texel;
}

float4 SampleAtlasTexture(uint textureSlot, float2 uv)
{
    GpuTextureRecord meta = g_TexturesMeta[textureSlot];
    float2 atlasUv = frac(uv) * meta.uvScaleOffset.xy + meta.uvScaleOffset.zw;
    return g_AtlasTexture.Sample(g_LinearSampler, atlasUv);
}

// =============================================================================
// 4. 局部空间精确几何线段软遮挡 (用于点光源 360° 软阴影与近场镜面反射遮挡)
// =============================================================================

float EvaluateBoxSignedDistance(float3 p, float3 halfExt)
{
    float3 d = abs(p) - halfExt;
    return length(max(d, float3(0.0f, 0.0f, 0.0f))) + min(max(d.x, max(d.y, d.z)), 0.0f);
}

float EvaluateSegmentInstanceShadow(
    float3 worldPos,
    float3 surfaceNormal,
    float3 targetWorld,
    uint selfInstanceId = 0xffffffffu)
{
    uint instanceCount = min(uint(max(g_SkyZenithColor.w, 0.0f) + 0.5f), 256u);
    if (instanceCount == 0u) {
        return 1.0f;
    }

    float3 startWorld = worldPos + surfaceNormal * 0.028f;
    float vis = 1.0f;

    [loop]
    for (uint idx = 0u; idx < instanceCount; ++idx) {
        if (idx == selfInstanceId) {
            continue;
        }

        GpuInstanceRow inst = g_Instances[idx];
        if ((inst.flags & 0x4u) == 0u) {
            continue;
        }

        // Note: invTransposeMatrix 为 (M^-1)^T，左乘行向量即精确逆变换 M^-1 * worldPt
        float3 p0 = mul(float4(startWorld, 1.0f), inst.invTransposeMatrix).xyz;
        float3 p1 = mul(float4(targetWorld, 1.0f), inst.invTransposeMatrix).xyz;
        float3 seg = p1 - p0;
        float segLenSq = dot(seg, seg);
        if (segLenSq < 1e-6f) {
            continue;
        }

        if (inst.worldSphereBounds.w > 0.5f) {
            // 局部球体遮挡体 (半径 0.5)
            if (selfInstanceId == 0xffffffffu) {
                float3 pSurf = mul(float4(worldPos, 1.0f), inst.invTransposeMatrix).xyz;
                if (dot(pSurf, pSurf) < 0.275f) {
                    continue;
                }
            }
            if (dot(p0, seg) < 0.0f) {
                float tStar = clamp(-dot(p0, seg) / segLenSq, 0.005f, 0.995f);
                float sdf = length(p0 + seg * tStar) - 0.50f;
                float coneWidth = 0.003f + 0.072f * tStar;
                vis = min(vis, smoothstep(0.0f, 1.0f, sdf / coneWidth));
            }
        } else {
            // 局部有向盒遮挡体 [-halfExt, +halfExt]，含旋转与非均匀缩放
            float3 halfExt = max(inst.worldSphereBounds.xyz, float3(0.05f, 0.05f, 0.05f));
            if (selfInstanceId == 0xffffffffu) {
                float3 pSurf = mul(float4(worldPos, 1.0f), inst.invTransposeMatrix).xyz;
                if (all(abs(pSurf) < halfExt * 1.03f)) {
                    continue;
                }
            }

            float3 outVec = p0 - clamp(p0, -halfExt, halfExt);
            float penumbraMax = 0.080f;
            float3 halfOuter = halfExt + penumbraMax;

            float3 invSeg = float3(
                abs(seg.x) > 1e-6f ? (1.0f / seg.x) : (seg.x >= 0.0f ? 1e6f : -1e6f),
                abs(seg.y) > 1e-6f ? (1.0f / seg.y) : (seg.y >= 0.0f ? 1e6f : -1e6f),
                abs(seg.z) > 1e-6f ? (1.0f / seg.z) : (seg.z >= 0.0f ? 1e6f : -1e6f));

            float3 tOut0 = (-halfOuter - p0) * invSeg;
            float3 tOut1 = ( halfOuter - p0) * invSeg;
            float3 tOutMin = min(tOut0, tOut1);
            float3 tOutMax = max(tOut0, tOut1);
            float tEnter = max(max(tOutMin.x, tOutMin.y), max(tOutMin.z, 0.002f));
            float tExit  = min(min(tOutMax.x, tOutMax.y), min(tOutMax.z, 0.995f));

            if (tExit > tEnter) {
                if (tEnter <= 0.0025f && dot(outVec, outVec) > 1e-6f && dot(outVec, seg) >= 0.0f) {
                    continue;
                }

                float3 tHit0 = (-halfExt - p0) * invSeg;
                float3 tHit1 = ( halfExt - p0) * invSeg;
                float3 tHitMin = min(tHit0, tHit1);
                float3 tHitMax = max(tHit0, tHit1);
                float tHitEnter = max(max(tHitMin.x, tHitMin.y), tHitMin.z);
                float tHitExit  = min(min(tHitMax.x, tHitMax.y), tHitMax.z);

                float tInner0 = max(tHitEnter, tEnter);
                float tInner1 = min(tHitExit, tExit);
                float tClosest = clamp(0.5f * (tInner0 + tInner1), tEnter, tExit);

                float tSamples[4] = {
                    tClosest,
                    lerp(tEnter, tExit, 0.25f),
                    0.5f * (tEnter + tExit),
                    lerp(tEnter, tExit, 0.75f)
                };

                [unroll]
                for (int s = 0; s < 4; ++s) {
                    float ts = tSamples[s];
                    float sdf = EvaluateBoxSignedDistance(p0 + seg * ts, halfExt);
                    float coneWidth = 0.003f + 0.072f * ts;
                    vis = min(vis, smoothstep(0.0f, 1.0f, sdf / coneWidth));
                }
            }
        }

        if (vis < 0.002f) {
            return 0.0f;
        }
    }

    return saturate(vis);
}

// =============================================================================
// 5. 物理 HDR 大气天空与阴影感知预滤波环境反射 (IBL)
// =============================================================================

float3 EvaluateProceduralHdrSky(float3 rayDir, bool includeSharpSunDisk, float sunVisibility = 1.0f)
{
    float3 dir = normalize(rayDir);
    float3 sunDir = normalize(-g_DirectionalLightDirection.xyz);
    float sunEnable = saturate(g_DirectionalLightDirection.w);
    float sunVis = saturate(sunVisibility);
    float3 sunColor = g_DirectionalLightColorIntensity.rgb;
    float sunIntensity = g_DirectionalLightColorIntensity.w * sunEnable;

    float3 zenith = g_SkyZenithColor.rgb * g_EnvironmentParams.y;
    float3 horizon = g_SkyHorizonGroundColor.rgb * g_EnvironmentParams.y;

    float upFactor = saturate(dir.y);
    float3 skyRadiance = lerp(horizon, zenith, pow(upFactor, 0.55f));

    float sunCos = dot(dir, sunDir);
    float horizonGlow = pow(saturate(sunCos * 0.5f + 0.5f), 5.0f) * exp(-abs(dir.y) * 3.2f);
    skyRadiance += sunColor * (horizonGlow * 0.22f * sunIntensity * sunVis);

    float mieHalo = pow(saturate(sunCos), 24.0f) * 0.12f + pow(saturate(sunCos), 128.0f) * 0.55f;
    skyRadiance += sunColor * (mieHalo * sunIntensity * sunVis);

    if (includeSharpSunDisk && sunEnable > 0.5f && sunVis > 0.01f) {
        float sunDisk = smoothstep(0.99915f, 0.99972f, sunCos);
        skyRadiance += sunColor * (sunDisk * sunIntensity * 6.5f * g_EnvironmentParams.w * sunVis);
    }

    if (dir.y < 0.0f) {
        float downT = saturate(pow(-dir.y, 0.78f) * 1.55f);
        float groundAlbedo = max(g_SkyHorizonGroundColor.w, 0.08f);
        float sunGroundScale = lerp(0.22f, 1.0f, sunVis);
        float3 sunlitGround = (horizon * 0.65f + sunColor * (sunIntensity * max(sunDir.y, 0.08f) * 0.36f * sunGroundScale)) *
                              (groundAlbedo * 2.8f);
        skyRadiance = lerp(skyRadiance, sunlitGround, downT);
    }

    return max(skyRadiance, float3(0.0f, 0.0f, 0.0f));
}

float3 EvaluateHemisphereIrradiance(float3 N)
{
    float3 sunDir = normalize(-g_DirectionalLightDirection.xyz);
    float sunEnable = saturate(g_DirectionalLightDirection.w);
    float3 sunColor = g_DirectionalLightColorIntensity.rgb;
    float sunIntensity = g_DirectionalLightColorIntensity.w * sunEnable;

    float3 skyTop = (g_SkyZenithColor.rgb * 0.55f + g_SkyHorizonGroundColor.rgb * 0.45f) * g_EnvironmentParams.y;
    float groundAlbedo = max(g_SkyHorizonGroundColor.w, 0.08f);
    float3 groundBounce = g_SkyHorizonGroundColor.rgb * (groundAlbedo * 2.4f) +
                          sunColor * (sunIntensity * groundAlbedo * 0.16f * max(sunDir.y, 0.0f));
    float skyWeight = saturate(N.y * 0.5f + 0.5f);
    return lerp(groundBounce, skyTop, skyWeight) * g_EnvironmentParams.z;
}

float3 SamplePrefilteredEnvironment(
    float3 R,
    float roughness,
    float sunVisibility = 1.0f,
    float metallic = 0.0f)
{
    float3 dir = normalize(R);
    float sunVis = saturate(sunVisibility);
    float metalFactor = saturate(metallic);

    // Note: 仅对金属导体保留适量地平线抬升采样，彻底移除不响应阴影的重复太阳高光瓣
    float3 liftedDir = normalize(float3(dir.x, abs(dir.y) * 0.45f + 0.14f * (1.0f - roughness), dir.z));
    float liftBlend = 0.62f * metalFactor * (1.0f - roughness * 0.5f);
    float3 sharpSky = lerp(
        EvaluateProceduralHdrSky(dir, false, sunVis),
        EvaluateProceduralHdrSky(liftedDir, false, sunVis),
        liftBlend);

    float3 sunDir = normalize(-g_DirectionalLightDirection.xyz);
    float metalStudioWeight = (metalFactor * metalFactor) * lerp(0.25f, 1.0f, sunVis);

    float horizonCard = exp(-pow((dir.y + 0.14f) * 2.8f, 2.0f)) * (1.0f - roughness * 0.70f);
    float azimVariation = 0.65f + 0.35f * cos(atan2(dir.z, dir.x) * 2.0f - 0.6f);
    float3 horizonSheen = lerp(g_SkyHorizonGroundColor.rgb, g_DirectionalLightColorIntensity.rgb, 0.38f * sunVis) *
                          (horizonCard * azimVariation * 1.10f * g_EnvironmentParams.y * metalStudioWeight);

    float3 studioRimDir = normalize(float3(-sunDir.x, 0.25f, -sunDir.z));
    float studioRim = pow(max(dot(dir, studioRimDir), 0.0f), lerp(16.0f, 3.0f, roughness)) * (1.0f - roughness * 0.65f);
    float3 studioRimLobe = g_SkyHorizonGroundColor.rgb *
                           (studioRim * 0.95f * g_EnvironmentParams.y * metalStudioWeight);

    float3 diffuseEnv = EvaluateHemisphereIrradiance(dir);
    float blurT = saturate(roughness * roughness * 0.82f + roughness * 0.18f);
    return (lerp(sharpSky, diffuseEnv, blurT) + horizonSheen + studioRimLobe) * g_EnvironmentParams.z;
}

float2 EnvBRDFApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// =============================================================================
// 6. 12-Tap 旋转 Vogel 圆盘软阴影 PCF (使用 OpImageFetch 规避深度线性采样限制)
// =============================================================================

#define MQ_SAMPLE_DIRECTIONAL_SHADOW(shadowTex, worldPosVal, normalVal, outShadowFactor) \
    do { \
        outShadowFactor = 1.0f; \
        if (g_ShadowParams.x > 0.001f && g_DirectionalLightDirection.w > 0.001f) { \
            float3 _sL = normalize(-g_DirectionalLightDirection.xyz); \
            float _rawNdotL = dot((normalVal), _sL); \
            float _facingVis = smoothstep(0.0f, 0.08f, _rawNdotL); \
            if (_facingVis <= 0.0001f) { \
                outShadowFactor = lerp(1.0f, 0.0f, saturate(g_ShadowParams.x)); \
            } else { \
                float _sNdotL = saturate(_rawNdotL); \
                float3 _biasedWorld = (worldPosVal) + \
                    (normalVal) * (g_ShadowParams.z * 0.45f * _facingVis) + \
                    _sL * (g_ShadowParams.z * 0.65f); \
                float4 _lightClip = mul(g_LightViewProjectionMatrix, float4(_biasedWorld, 1.0f)); \
                float3 _lightNdc = _lightClip.xyz / max(_lightClip.w, 0.0001f); \
                float2 _shadowUv = float2(_lightNdc.x * 0.5f + 0.5f, 0.5f - _lightNdc.y * 0.5f); \
                if (_shadowUv.x > 0.002f && _shadowUv.x < 0.998f && \
                    _shadowUv.y > 0.002f && _shadowUv.y < 0.998f && \
                    _lightNdc.z > 0.0f && _lightNdc.z < 0.9995f) { \
                    float _invMapSize = max(g_ShadowParams.w, 1.0f / 4096.0f); \
                    float _mapSize = round(1.0f / _invMapSize); \
                    int _maxCoord = int(_mapSize) - 1; \
                    float _slopeBias = g_ShadowParams.y * (1.0f + 1.2f * (1.0f - _sNdotL)); \
                    float _receiverZ = _lightNdc.z - _slopeBias; \
                    float _ign = frac(52.9829189f * frac(dot(_shadowUv * _mapSize, float2(0.06711056f, 0.00583715f)))); \
                    float _baseAngle = _ign * 6.2831853f; \
                    float _visAccum = 0.0f; \
                    const float _filterRadius = 2.25f; \
                    [unroll] \
                    for (int _tap = 0; _tap < 12; ++_tap) { \
                        float _r = sqrt((float(_tap) + 0.5f) / 12.0f) * _filterRadius; \
                        float _theta = float(_tap) * 2.3999632f + _baseAngle; \
                        float2 _offsetTexel = float2(cos(_theta), sin(_theta)) * _r; \
                        float2 _sampleF = _shadowUv * _mapSize + _offsetTexel; \
                        int2 _texelCoord = clamp(int2(_sampleF), int2(0, 0), int2(_maxCoord, _maxCoord)); \
                        float _closestZ = shadowTex.Load(int3(_texelCoord, 0)).r; \
                        _visAccum += (_receiverZ <= _closestZ) ? 1.0f : 0.0f; \
                    } \
                    float _rawVis = (_visAccum * (1.0f / 12.0f)) * _facingVis; \
                    float2 _edgeDist = min(_shadowUv, 1.0f - _shadowUv); \
                    float _edgeFade = smoothstep(0.004f, 0.055f, min(_edgeDist.x, _edgeDist.y)); \
                    outShadowFactor = lerp(1.0f, _rawVis, saturate(g_ShadowParams.x) * _edgeFade); \
                } else { \
                    outShadowFactor = lerp(1.0f, _facingVis, saturate(g_ShadowParams.x)); \
                } \
            } \
        } \
    } while (0)

// =============================================================================
// 7. 解析盒滤波抗锯齿地面棋盘格与阴影耦合 PBR 直接/间接光照
// =============================================================================

void ApplyProceduralGroundGridLinear(inout float3 albedo, inout float roughness, float3 worldPos, uint instanceFlags)
{
    if ((instanceFlags & 0x2u) == 0u) {
        return;
    }

    float2 gridUV = worldPos.xz;
    float2 gridDeriv = max(fwidth(gridUV), float2(0.0001f, 0.0001f));
    float2 gridDist = abs(frac(gridUV - 0.5f) - 0.5f) / gridDeriv;
    float minorLine = 1.0f - smoothstep(0.0f, 1.35f, min(gridDist.x, gridDist.y));

    float2 majorUV = gridUV * 0.25f;
    float2 majorDeriv = max(fwidth(majorUV), float2(0.0001f, 0.0001f));
    float2 majorDist = abs(frac(majorUV - 0.5f) - 0.5f) / majorDeriv;
    float majorLine = 1.0f - smoothstep(0.0f, 1.35f, min(majorDist.x, majorDist.y));

    // Note: 解析盒滤波棋盘格积分 (Inigo Quilez)，消除远景奈奎斯特摩尔纹与锯齿
    float2 fw = max(gridDeriv, float2(0.001f, 0.001f));
    float2 p0 = (gridUV - 0.5f * fw) * 0.5f;
    float2 p1 = (gridUV + 0.5f * fw) * 0.5f;
    float2 boxInt = (floor(p1) + min(frac(p1) * 2.0f, 1.0f) - floor(p0) - min(frac(p0) * 2.0f, 1.0f)) / fw;
    float tileMask = saturate(0.5f + 0.5f * (1.0f - 2.0f * boxInt.x) * (1.0f - 2.0f * boxInt.y));
    // 对齐原棋盘格奇偶相位
    tileMask = 1.0f - tileMask;

    albedo = lerp(albedo * 0.80f, albedo * 1.18f, tileMask);
    roughness = clamp(lerp(roughness + 0.06f, roughness - 0.10f, tileMask), 0.08f, 1.0f);

    float distFade = exp(-length(worldPos.xz) * 0.065f);
    albedo = lerp(albedo, float3(0.11f, 0.14f, 0.19f), minorLine * 0.35f * distFade);
    albedo = lerp(albedo, float3(0.05f, 0.24f, 0.45f), majorLine * 0.55f * distFade);
}

float3 EvaluateCookTorranceDirectLobe(
    float3 N,
    float3 V,
    float3 L,
    float3 lightRadiance,
    float3 albedo,
    float3 F0,
    float metallic,
    float roughness,
    float shadowVisibility = 1.0f)
{
    float diffVis = saturate(shadowVisibility);
    float NdotL = max(dot(N, L), 0.0f);
    if (NdotL <= 0.0001f || diffVis <= 0.0001f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float NdotV = max(dot(N, V), 0.001f);
    float3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);

    float alpha = max(roughness * roughness, 0.002f);
    float alpha2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / max(MQ_PI * denom * denom, 0.00001f);

    float Gv = NdotL * sqrt(NdotV * NdotV * (1.0f - alpha2) + alpha2);
    float Gl = NdotV * sqrt(NdotL * NdotL * (1.0f - alpha2) + alpha2);
    float Vis = 0.5f / max(Gv + Gl, 0.0001f);

    float3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    float2 envAb = EnvBRDFApprox(roughness, NdotV);
    float Ess = max(envAb.x + envAb.y, 0.01f);
    float3 multiScatterComp = 1.0f + F0 * ((1.0f - Ess) / Ess);

    float3 specular = (D * Vis) * F * multiScatterComp;
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
    float3 diffuse = kD * albedo * (1.0f / MQ_PI);

    // Note: 半影区域内紧缩高强度 GGX 镜面波瓣可见度，消除 PCF 边缘与软阴影内的刺眼高光渗漏
    float specVis = pow(diffVis, lerp(3.5f, 1.15f, roughness)) * smoothstep(0.04f, 0.28f, diffVis);
    return (diffuse * diffVis + specular * specVis) * lightRadiance * NdotL;
}

float3 EvaluatePointLightDirect(
    float4 posRange,
    float4 colorIntensity,
    float3 worldPos,
    float3 N,
    float3 V,
    float3 albedo,
    float3 F0,
    float metallic,
    float roughness,
    uint selfInstanceId = 0xffffffffu)
{
    if (posRange.w <= 0.01f || colorIntensity.w <= 0.0001f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 toLight = posRange.xyz - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(max(distSq, 0.0001f));
    if (dist >= posRange.w) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 L = toLight / dist;
    if (dot(N, L) <= 0.0001f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float ptShadow = EvaluateSegmentInstanceShadow(worldPos, N, posRange.xyz, selfInstanceId);
    if (ptShadow <= 0.001f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float rRatio = saturate(dist / posRange.w);
    float rRatio2 = rRatio * rRatio;
    float window = saturate(1.0f - rRatio2 * rRatio2);
    window *= window;
    float attenuation = window / (distSq + 0.35f);

    float3 radiance = colorIntensity.rgb * (colorIntensity.w * attenuation);
    return EvaluateCookTorranceDirectLobe(N, V, L, radiance, albedo, F0, metallic, roughness, ptShadow);
}

float3 EvaluateStandardPbrShading(
    float3 worldPos,
    float3 N,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    float3 emissive,
    float directionalShadow,
    uint selfInstanceId = 0xffffffffu)
{
    bool isOrtho = (abs(g_ProjectionMatrix[3][3]) > 0.5f);
    float3 V = isOrtho ? normalize(-g_ViewMatrix[2].xyz) : normalize(g_CameraWorldPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 0.001f);
    float clampedMetallic = saturate(metallic);

    // Note: 屏幕空间法线方差几何高光抗锯齿 (Kaplanyan Specular Geometric AA)，抑制曲面高光闪烁锯齿
    float3 dNdx = ddx(N);
    float3 dNdy = ddy(N);
    float normalVar = saturate(max(dot(dNdx, dNdx), dot(dNdy, dNdy)));
    float baseRoughness = clamp(roughness, 0.045f, 1.0f);
    float baseAlpha = baseRoughness * baseRoughness;
    float aaAlpha = saturate(sqrt(baseAlpha * baseAlpha + min(2.0f * normalVar, 0.18f)));
    float aaRoughness = clamp(sqrt(aaAlpha), baseRoughness, 1.0f);

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, clampedMetallic);
    float sunVis = saturate(directionalShadow);

    float3 directLighting = float3(0.0f, 0.0f, 0.0f);
    if (g_DirectionalLightDirection.w > 0.001f) {
        float3 sunL = normalize(-g_DirectionalLightDirection.xyz);
        float3 sunRadiance = g_DirectionalLightColorIntensity.rgb *
                             (g_DirectionalLightColorIntensity.w * g_DirectionalLightDirection.w);
        directLighting += EvaluateCookTorranceDirectLobe(
            N, V, sunL, sunRadiance, albedo, F0, clampedMetallic, aaRoughness, sunVis);
    }

    directLighting += EvaluatePointLightDirect(
        g_PointLightPosRange0, g_PointLightColorIntensity0,
        worldPos, N, V, albedo, F0, clampedMetallic, aaRoughness, selfInstanceId);
    directLighting += EvaluatePointLightDirect(
        g_PointLightPosRange1, g_PointLightColorIntensity1,
        worldPos, N, V, albedo, F0, clampedMetallic, aaRoughness, selfInstanceId);

    // 预滤波多散射能量守恒 IBL (Fdez-Aguera) + 阴影感知非金属高光收敛
    float2 brdfAb = EnvBRDFApprox(aaRoughness, NdotV);
    float3 FssEss = F0 * brdfAb.x + brdfAb.y;
    float Ess = brdfAb.x + brdfAb.y;
    float Ems = 1.0f - Ess;
    float3 Favg = F0 + (1.0f - F0) * (1.0f / 21.0f);
    float3 Fms = FssEss * Favg / max(1.0f - Ems * Favg, float3(0.001f, 0.001f, 0.001f));

    float3 R = reflect(-V, N);
    float3 prefilteredEnv = SamplePrefilteredEnvironment(R, aaRoughness, sunVis, clampedMetallic);
    float3 diffuseIrradiance = EvaluateHemisphereIrradiance(N);

    // Note: 仅对阴影内的非金属电介质收敛天空镜面反射，保持金属导体的连续环境反射，杜绝无着色假黑块遮挡
    float shadowSpecOcc = lerp(lerp(0.22f, 0.82f, clampedMetallic), 1.0f, sunVis);
    prefilteredEnv = lerp(diffuseIrradiance * 0.28f, prefilteredEnv, shadowSpecOcc);

    float horizonSpecOcc = saturate(1.0f + dot(R, N));
    horizonSpecOcc *= horizonSpecOcc;
    float specOcclusion = saturate(pow(NdotV + ao, exp2(-16.0f * aaRoughness - 1.0f)) - 1.0f + ao) * horizonSpecOcc;

    float shadowDiffuseOcc = lerp(0.76f, 1.0f, sunVis);
    float3 kD_ibl = albedo * (1.0f - Ess) * (1.0f - clampedMetallic);
    float3 iblDiffuse = (kD_ibl + Fms * Ems * (1.0f - clampedMetallic)) * diffuseIrradiance * (ao * shadowDiffuseOcc);
    float3 iblSpecular = (prefilteredEnv * FssEss + diffuseIrradiance * Fms * Ems * clampedMetallic * shadowSpecOcc) * specOcclusion;

    return iblDiffuse + iblSpecular + directLighting + emissive;
}

// =============================================================================
// 8. Stephen Hill 拟合 ACES RRT+ODT 电影级色调映射 (输出纯线性 [0,1]，严禁重复 Gamma)
// =============================================================================

float3 ACESFittedToneMap(float3 hdrLinear)
{
    float3 c = max(hdrLinear, float3(0.0f, 0.0f, 0.0f));
    float3 a = float3(
        0.59719f * c.r + 0.35458f * c.g + 0.04823f * c.b,
        0.07600f * c.r + 0.90834f * c.g + 0.01566f * c.b,
        0.02840f * c.r + 0.13383f * c.g + 0.83777f * c.b);

    float3 num = a * (a + 0.0245786f) - 0.000090537f;
    float3 den = a * (0.983729f * a + 0.4329510f) + 0.238081f;
    float3 rrt = num / max(den, float3(0.0001f, 0.0001f, 0.0001f));

    float3 odt = float3(
         1.60475f * rrt.r - 0.53108f * rrt.g - 0.07367f * rrt.b,
        -0.10208f * rrt.r + 1.10813f * rrt.g - 0.00605f * rrt.b,
        -0.00327f * rrt.r - 0.07276f * rrt.g + 1.07602f * rrt.b);
    return saturate(odt);
}

#endif // MQENGINE_BINDLESS_COMMON_HLSLI
