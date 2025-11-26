ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.position = sIn.position;
    sOut.texCoord = sIn.texCoord;
    sOut.color = float4(1,1,1,1);
    sOut.normal = float4(0,0,0,0);
    sOut.tangent = float4(0,0,0,0);
    sOut.bitangent = float4(0,0,0,0);
    sOut.srcpos = sIn.position;
    sOut.shadowPos = float4(0,0,0,0);
    return sOut;
}