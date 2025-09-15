ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = projectionMatrix * viewMatrix * modelMatrix * sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.tangent = float3(0.0, 0.0, 0.0);
    sOut.bitangent = float3(0.0, 0.0, 0.0);
    sOut.srcpos = modelMatrix * sIn.position;
    sOut.shadowPos = directionalLightMvp * modelMatrix * sIn.position;
    return sOut;
}