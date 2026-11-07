ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float4 diffuseColor = diffuseTexture.Sample(textureSampler, sIn.texCoord);
    float3 albedo = sIn.color.xyz * diffuseColor.xyz;
    float3 normal = normalize(sIn.normal.xyz);

    sOut.target0 = float4(albedo, 1.0);
    sOut.target1 = float4(sIn.worldPos.xyz, 0.0);
    sOut.target2 = float4(normal, 0.5);
    if (sIn.color.w < 0.0) {
        sOut.target3 = float4(albedo, 2.0);
    } else {
        sOut.target3 = float4(0.0, 0.0, 0.0, 1.0);
    }

    return sOut;
}
