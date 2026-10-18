ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = projectionMatrix * viewMatrix * modelMatrix * sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = modelInverseTransposeMatrix * float4(sIn.normal.xyz, 0.0f);
    sOut.tangent = modelMatrix * float4(sIn.tangent.xyz, 0.0f);
    sOut.bitangent = float4(cross(sOut.normal.xyz, sOut.tangent.xyz), 0.0f);
    sOut.worldPos = modelMatrix * sIn.position;
    sOut.shadowPos = directionalLightMvp * modelMatrix * sIn.position;
    return sOut;
}