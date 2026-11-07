ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    float4 localPos = sIn.position;
    if (hasBones != 0) {
        float totalWeight = sIn.boneWeights.x + sIn.boneWeights.y + sIn.boneWeights.z + sIn.boneWeights.w;
        if (totalWeight > 0.0001f) {
            float4x4 skinMatrix =
                boneMatrices[sIn.boneIds.x] * sIn.boneWeights.x +
                boneMatrices[sIn.boneIds.y] * sIn.boneWeights.y +
                boneMatrices[sIn.boneIds.z] * sIn.boneWeights.z +
                boneMatrices[sIn.boneIds.w] * sIn.boneWeights.w;
            localPos = skinMatrix * float4(sIn.position.xyz, 1.0f);
        }
    }
    sOut.position = directionalLightMvp * modelMatrix * localPos;
    sOut.texCoord = sIn.texCoord;
    sOut.tangent = sIn.tangent;
    sOut.bitangent = sIn.bitangent;
    sOut.normal = sIn.normal;
    return sOut;
}
