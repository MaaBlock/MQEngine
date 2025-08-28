float calculateShadow(float4 shadowPos, float3 normal, float3 lightDir) {
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    //projCoords = projCoords * 0.5 + 0.5;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;

    float cosTheta = dot(normalize(normal), normalize(lightDir));
    cosTheta = clamp(cosTheta, 0.0, 1.0);

    float bias = max(0.025 * (1.0 - dot(normal, lightDir)), 0.0005);

    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);

    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float2 offset = float2(x, y) * texelSize;
            float closestDepth = DepthFromLigth0Image.Sample(shadowSampler, projCoords.xy + offset).r;
            shadow += (currentDepth - bias) <= closestDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;  // 9个采样点的平均值

    return shadow;
}

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    float4 lightDir;
    float attenuation = 1.0;
    switch(lightType) {
    case 0:
        float distance = length(sIn.srcpos - lightPos);
        lightDir = normalize(lightPos - sIn.srcpos);
        attenuation = 1.0 / (constant + linearAttenuation * distance +
                quadratic * (distance * distance));
        break;
    case 1:
        lightDir = -lightDirection;
        break;
    case 2:
        lightDir = normalize(lightPos - sIn.srcpos);
        if (dot(-lightDir, lightDirection) < cutOff) {
            attenuation = 0;
        }
        break;
    }
    float4 viewDir = normalize(viewPos - sIn.srcpos);
    float3 halfDir = normalize(viewDir + lightDir).xyz;
    float3 diff = max(dot(sIn.normal.xyz, lightDir.xyz), 0.0) * diffuseColor;
    float3 spec = pow(max(dot(sIn.normal, halfDir), 0.0), shininess) * specularColor;
    float3 ambi = ambientColor;
    float shadow = calculateShadow(lightMvp * sIn.srcpos, sIn.normal.xyz, lightDir.xyz);
    float3 finalColor = (sIn.color.xyz * (ambi +  shadow * (spec  + diff) * attenuation));
    sOut.target0 = float4(finalColor, 1.0);
    float3 projCoords = sIn.shadowPos.xyz / sIn.shadowPos.w;

    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    sOut.target1 = float4(projCoords.x,projCoords.y,projCoords.z,1);

    float closestDepth = DepthFromLigth0Image.Sample(shadowSampler, projCoords.xy).r;
    float currentDepth = projCoords.z;
    sOut.target2 = float4(closestDepth, currentDepth, abs(currentDepth - closestDepth) * 10.0, 1.0);
    return sOut;
}