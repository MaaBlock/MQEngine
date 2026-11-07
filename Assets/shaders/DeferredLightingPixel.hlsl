static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float numerator = a2;
    float denominator = (NdotH2 * (a2 - 1.0) + 1.0);
    denominator = PI * denominator * denominator;

    return numerator / max(denominator, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float numerator = NdotV;
    float denominator = NdotV * (1.0 - k) + k;

    return numerator / max(denominator, 0.0001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calculateDirectionalShadow(float4 shadowPos, float3 normal, float3 lightDir)
{
    float3 projCoords = shadowPos.xyz / max(shadowPos.w, 0.0001);
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = projCoords.y * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;
    float bias = max(0.025 * (1.0 - dot(normal, lightDir)), 0.0005);

    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            float closestDepth = DepthFromLigth0Image.Sample(shadowSampler, projCoords.xy + offset).r;
            shadow += (currentDepth - bias) <= closestDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float3 evaluateCookTorrance(
    float3 N, float3 V, float3 L,
    float3 albedo, float metallic, float roughness, float3 F0)
{
    float3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    float3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);

    return (kD * albedo / PI + specular) * NdotL;
}

ShaderOut main(ShaderIn sIn)
{
    ShaderOut sOut;

    float4 retSample      = RetTarget.Sample(textureSampler, sIn.texCoord);
    float4 posSample      = PosTarget.Sample(textureSampler, sIn.texCoord);
    float4 normalSample   = NormalTarget.Sample(textureSampler, sIn.texCoord);
    float4 emissiveSample = EmissiveTarget.Sample(textureSampler, sIn.texCoord);

    float shadingMode = emissiveSample.a;
    if (shadingMode < 0.5)
    {
        sOut.target0 = float4(0.0, 0.0, 0.0, 1.0);
        return sOut;
    }

    if (shadingMode > 1.5)
    {
        sOut.target0 = float4(emissiveSample.rgb, 1.0);
        return sOut;
    }

    float3 albedo    = retSample.rgb;
    float  ao        = retSample.a;
    float3 worldPos  = posSample.xyz;
    float  metallic  = saturate(posSample.w);
    float3 N         = normalize(normalSample.xyz);
    float  roughness = clamp(normalSample.w, 0.04, 1.0);
    float3 emissive  = emissiveSample.rgb;

    float3 V  = normalize(viewPosition - worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);

    if (directionalLightEnable)
    {
        float3 L = normalize(-directionalLightDirection.xyz);
        float3 radiance = directionalLightColor * directionalLightIntensity;
        float4 shadowPos = directionalLightMvp * float4(worldPos, 1.0);
        float shadow = calculateDirectionalShadow(shadowPos, N, L);
        Lo += shadow * evaluateCookTorrance(N, V, L, albedo, metallic, roughness, F0) * radiance;
    }

    for (int i = 0; i < pointLightCount; ++i)
    {
        float3 lightPos       = pointLightPositions[i].xyz;
        float  lightRange     = pointLightPositions[i].w;
        float3 lightColor     = pointLightColors[i].xyz;
        float  lightIntensity = pointLightColors[i].w;
        float3 attenParams    = pointLightAttenuations[i].xyz;

        float3 toLight = lightPos - worldPos;
        float  dist    = length(toLight);
        if (lightRange > 0.0 && dist >= lightRange)
            continue;

        float3 Lp = toLight / max(dist, 0.0001);
        float attenuation = 1.0 / max(attenParams.x + attenParams.y * dist + attenParams.z * dist * dist, 0.0001);
        if (lightRange > 0.0)
        {
            float ratio = saturate(dist / lightRange);
            float window = max(1.0 - ratio * ratio * ratio * ratio, 0.0);
            attenuation *= window * window;
        }

        float3 radiance = lightColor * lightIntensity * attenuation;
        Lo += evaluateCookTorrance(N, V, Lp, albedo, metallic, roughness, F0) * radiance;
    }

    float3 ambient = float3(0.08, 0.08, 0.08) * albedo * ao;
    float3 finalColor = ambient + Lo + emissive;

    sOut.target0 = float4(finalColor, 1.0);
    return sOut;
}
