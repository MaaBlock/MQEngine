float calculateShadow(float4 shadowPos, float3 normal, float3 directionalLightDir) {
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    //projCoords = projCoords * 0.5 + 0.5;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;

    float cosTheta = dot(normalize(normal), normalize(directionalLightDir));
    cosTheta = clamp(cosTheta, 0.0, 1.0);

    float bias = max(0.025 * (1.0 - dot(normal, directionalLightDir)), 0.0005);

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

    float3 ambient = float3(0.2, 0.2, 0.2);
    float3 finalColor = sIn.color.xyz * ambient;

    if (directionalLightEnable) {
        float4 lightDir = -directionalLightDirection;
        float3 normal = normalize(sIn.normal.xyz);
        float3 lightDirection = normalize(lightDir.xyz);
        
        float3 viewDir = normalize(viewPosition - sIn.srcpos.xyz);

        float NdotL = max(dot(normal, lightDirection), 0.0);
        float3 diffuse = NdotL * float3(0.8, 0.8, 0.8);

        float3 halfwayDir = normalize(lightDirection + viewDir);
        float NdotH = max(dot(normal, halfwayDir), 0.0);
        float shininess = 32.0;
        float spec = pow(NdotH, shininess);
        float3 specular = spec * float3(0.3, 0.3, 0.3);

        float shadow = calculateShadow(directionalLightMvp * sIn.srcpos, sIn.normal.xyz, lightDirection);
        
        finalColor = sIn.color.xyz * (ambient + shadow * (diffuse + specular));
    }
    
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