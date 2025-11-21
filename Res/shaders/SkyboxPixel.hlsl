ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float3 texCoord = normalize(sIn.srcpos.xyz);
    float3 skyboxColor = skyboxTexture.Sample(textureSampler, texCoord).rgb;

    sOut.target0 = float4(skyboxColor, 1.0);
    sOut.target1 = float4(0, 0, 0, 0); 
    sOut.target2 = float4(0, 0, 0, 0);

    return sOut;
}
