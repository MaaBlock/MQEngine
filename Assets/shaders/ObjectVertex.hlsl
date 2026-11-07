ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.color = sIn.color;
    float4 localPos = sIn.position;
    float3 localNormal = sIn.normal;
    float3 localTangent = sIn.tangent;
    float3 localBitangent = sIn.bitangent;
    if (hasBones != 0) {
        float totalWeight = sIn.boneWeights.x + sIn.boneWeights.y + sIn.boneWeights.z + sIn.boneWeights.w;
        if (totalWeight > 0.0001f) {
            float4x4 skinMatrix =
                boneMatrices[sIn.boneIds.x] * sIn.boneWeights.x +
                boneMatrices[sIn.boneIds.y] * sIn.boneWeights.y +
                boneMatrices[sIn.boneIds.z] * sIn.boneWeights.z +
                boneMatrices[sIn.boneIds.w] * sIn.boneWeights.w;
            localPos = skinMatrix * float4(sIn.position.xyz, 1.0f);
            localNormal = (skinMatrix * float4(sIn.normal.xyz, 0.0f)).xyz;
            localTangent = (skinMatrix * float4(sIn.tangent.xyz, 0.0f)).xyz;
            localBitangent = (skinMatrix * float4(sIn.bitangent.xyz, 0.0f)).xyz;
        }
    }
    sOut.position = projectionMatrix * viewMatrix * modelMatrix * localPos;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = localNormal;
    sOut.tangent = localTangent;
    sOut.bitangent = localBitangent;
    sOut.worldPos = modelMatrix * localPos;
    sOut.shadowPos = directionalLightMvp * modelMatrix * localPos;
    return sOut;
}
