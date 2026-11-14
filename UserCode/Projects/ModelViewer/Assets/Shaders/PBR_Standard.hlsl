#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    uint instanceIdx = sIn.instanceId;
    GpuInstanceRow inst = g_Instances[instanceIdx];
    GpuMaterialRecord mat = g_Materials[inst.materialSlot];

    float3 vtxColor = (dot(sIn.color.rgb, sIn.color.rgb) > 0.0001f) ? sIn.color.rgb : float3(1.0f, 1.0f, 1.0f);
    float4 sampledAlbedo = SampleBindlessTexture(mat.albedoTexIndex, sIn.texCoord);
    float3 albedo = mat.baseColorFactor.rgb * sampledAlbedo.rgb * vtxColor;
    float metallic = saturate(mat.metallicRoughnessNormalOcclusion.x);
    float roughness = clamp(mat.metallicRoughnessNormalOcclusion.y, 0.045f, 1.0f);
    float ao = saturate(mat.metallicRoughnessNormalOcclusion.w);

    ApplyProceduralGroundGridLinear(albedo, roughness, sIn.worldPos.xyz, inst.flags);

    float3 N = normalize(sIn.normal);
    if (mat.normalTexIndex != 1u) {
        float3 normalMap = SampleBindlessTexture(mat.normalTexIndex, sIn.texCoord).xyz * 2.0f - 1.0f;
        normalMap.xy *= mat.metallicRoughnessNormalOcclusion.z;
        float3x3 TBN = float3x3(normalize(sIn.tangent), normalize(sIn.bitangent), N);
        N = normalize(mul(normalMap, TBN));
    }

    float directionalShadow = 1.0f;
    MQ_SAMPLE_DIRECTIONAL_SHADOW(DepthFromLigth0Image, sIn.worldPos.xyz, N, directionalShadow);

    float3 emissive = mat.emissiveFactor.rgb * mat.emissiveFactor.w;
    float3 litHdrColor = EvaluateStandardPbrShading(
        sIn.worldPos.xyz, N, albedo, metallic, roughness, ao, emissive, directionalShadow);

    sOut.target0 = float4(litHdrColor, 1.0f);
    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);
    sOut.target2 = float4(N * 0.5f + 0.5f, roughness);
    return sOut;
}
