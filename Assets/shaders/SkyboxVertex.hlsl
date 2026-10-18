ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float3x3 viewRot = (float3x3)viewMatrix;
    float3 viewPos = viewRot * sIn.position.xyz;
    float4 clipPos = projectionMatrix * float4(viewPos, 1.0);

    sOut.position = clipPos.xyww;

    sOut.worldPos = float4(sIn.position.xyz, 1.0);

    sOut.color = float4(1,1,1,1);
    sOut.texCoord = float2(0,0);
    sOut.normal = float4(0,0,0,0);
    sOut.tangent = float4(0,0,0,0);
    sOut.bitangent = float4(0,0,0,0);
    sOut.shadowPos = float4(0,0,0,0);

    return sOut;
}
