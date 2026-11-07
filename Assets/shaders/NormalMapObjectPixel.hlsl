ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float4 diffuseColor = diffuseTexture.Sample(textureSampler, sIn.texCoord);
    float3 normalSample = normalTexture.Sample(textureSampler, sIn.texCoord).xyz;
    normalSample = normalize(normalSample * 2.0 - 1.0);

    float3 N = normalize(sIn.normal.xyz);
    float3 T = normalize(sIn.tangent.xyz);
    float3 B = normalize(sIn.bitangent.xyz);
    float3x3 TBN = float3x3(T, B, N);
    float3 normal = normalize(mul(normalSample, TBN));

    float roughness = clamp(sqrt(2.0 / (max(shininess, 1.0) + 2.0)), 0.05, 1.0);
    float3 albedo = sIn.color.xyz * diffuseColor.xyz;

    sOut.target0 = float4(albedo, 1.0);
    sOut.target1 = float4(sIn.worldPos.xyz, 0.0);
    sOut.target2 = float4(normal, roughness);
    sOut.target3 = float4(0.0, 0.0, 0.0, 1.0);

    return sOut;
}