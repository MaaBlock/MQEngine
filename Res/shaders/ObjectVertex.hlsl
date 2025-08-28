ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    sOut.position = projectionMatrix * viewMatrix * sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.srcpos = sIn.position;
    sOut.shadowPos = lightMvp * sIn.position;
    return sOut;
}