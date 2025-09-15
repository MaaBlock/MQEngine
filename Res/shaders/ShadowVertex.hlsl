ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = directionalLightMvp * modelMatrix * sIn.position ;
    sOut.texCoord = sIn.texCoord;
    sOut.tangent = sIn.tangent;
    sOut.bitangent = sIn.bitangent;
    sOut.normal = sIn.normal;
    return sOut;
}