ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float3 albedo    = albedoTexture.Sample(textureSampler, sIn.texCoord).rgb;
    float3 ormSample = ormTexture.Sample(textureSampler, sIn.texCoord).rgb;
    float  ao        = ormSample.r;
    float  roughness = clamp(ormSample.g, 0.04, 1.0);
    float  metallic  = saturate(ormSample.b);

    float3 normalSample = normalTexture.Sample(textureSampler, sIn.texCoord).xyz;
    normalSample = normalize(normalSample * 2.0 - 1.0);

    float3 srcN = normalize(sIn.normal.xyz);
    float3 T    = normalize(sIn.tangent.xyz);
    float3 B    = normalize(sIn.bitangent.xyz);
    float3x3 TBN = float3x3(T, B, srcN);
    float3 N = normalize(mul(normalSample, TBN));

    sOut.target0 = float4(albedo, ao);
    sOut.target1 = float4(sIn.worldPos.xyz, metallic);
    sOut.target2 = float4(N, roughness);
    sOut.target3 = float4(0.0, 0.0, 0.0, 1.0);

    return sOut;
}