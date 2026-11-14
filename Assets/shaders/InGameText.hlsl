// 32B 紧凑实例流游戏内 MSDF 战斗跳字合批着色器 (PRD 08 §2.2, FEAT-56)
cbuffer InGameTextUniforms : register(b0)
{
    float4x4 uViewProjMatrix;
    float3   uCameraRight;
    float    uMsdfPixelRange;
    float3   uCameraUp;
    float    uPad0;
};

Texture2D<float4> g_BakedFontAtlas : register(t0);
SamplerState g_LinearClampSampler : register(s0);

struct VSInput
{
    float2 quadCorner   : POSITION;   // [-0.5, 0.5] 单位矩形顶点
    float3 worldAnchor  : TEXCOORD0;  // PerInstance 12B
    float  uniformScale : TEXCOORD1;  // PerInstance 4B
    float4 uvMinMax     : TEXCOORD2;  // PerInstance UNORM16x4 归一化纹理坐标 (8B)
    float4 colorRgba8   : COLOR0;     // PerInstance RGBA8 归一化颜色 (4B)
    float2 localOffset  : TEXCOORD3;  // PerInstance SNORM16x2 偏移 (4B)
};

struct PSInput
{
    float4 svPosition : SV_Position;
    float2 uv         : TEXCOORD0;
    float4 color      : COLOR0;
};

float Median3(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

PSInput VSMain(VSInput input)
{
    PSInput o;
    float2 scaledLocal = (input.quadCorner + input.localOffset) * input.uniformScale;
    float3 worldPos = input.worldAnchor
                    + uCameraRight * scaledLocal.x
                    + uCameraUp * scaledLocal.y;
    o.svPosition = mul(uViewProjMatrix, float4(worldPos, 1.0));
    o.uv = lerp(input.uvMinMax.xy, input.uvMinMax.zw, input.quadCorner + float2(0.5, 0.5));
    o.color = input.colorRgba8;
    return o;
}

float4 PSMain(PSInput input) : SV_Target0
{
    float3 msdfSample = g_BakedFontAtlas.Sample(g_LinearClampSampler, input.uv).rgb;
    float signedDist = Median3(msdfSample.r, msdfSample.g, msdfSample.b) - 0.5;
    float screenPxDist = signedDist * max(uMsdfPixelRange, 2.0);
    float coverage = saturate(screenPxDist + 0.5);
    return float4(input.color.rgb, input.color.a * coverage);
}
