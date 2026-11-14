#include "BindlessCommon.hlsli"

struct VSInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct PSInput {
    float4 positionCS : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialSlot : TEXCOORD1;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    GpuInstanceRow inst = g_Instances[input.instanceId];
    output.positionCS = mul(g_ViewProjectionMatrix, mul(inst.modelMatrix, float4(input.position, 1.0f)));
    output.uv = input.uv;
    output.materialSlot = inst.materialSlot;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    GpuMaterialRecord mat = g_Materials[input.materialSlot];
    float4 color = SampleBindlessTexture(mat.albedoTexIndex, input.uv);
    return color * mat.baseColorFactor;
}
