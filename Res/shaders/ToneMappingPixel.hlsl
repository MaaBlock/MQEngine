float3 ACESToneMapping(float3 color) {
    float A = 2.51f;
    float B = 0.03f;
    float C = 2.43f;
    float D = 0.59f;
    float E = 0.14f;
    return saturate((color * (A * color + B)) / (color * (C * color + D) + E));
}

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    float4 color = SceneHDRColor.Sample(textureSampler, sIn.texCoord);
    float3 mapped = ACESToneMapping(color.rgb);
    
    sOut.target0 = float4(mapped, color.a);
    return sOut;
}