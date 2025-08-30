ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = lightMvp * modelMatrix * sIn.position ;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    return sOut;
}