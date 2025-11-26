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

    return numerator / denominator;
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



ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    float3 albedo     = albedoTexture.Sample(textureSampler, sIn.texCoord).rgb;
    float3 ormSample  = ormTexture.Sample(textureSampler, sIn.texCoord).rgb;
    float  ao         = ormSample.r;
    float  roughness  = ormSample.g;
    float  metallic   = ormSample.b;

    float3 normalSample = normalTexture.Sample(textureSampler, sIn.texCoord).xyz;
    normalSample = normalize(normalSample * 2.0 - 1.0);

    float3 srcN = normalize(sIn.normal.xyz);
    float3 T    = normalize(sIn.tangent.xyz);
    float3 B    = normalize(sIn.bitangent.xyz);
    float3x3 TBN = float3x3(T, B, srcN);

    float3 N = normalize(mul(normalSample, TBN));

    float3 V = normalize(viewPosition - sIn.srcpos.xyz);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);


    float3 L = normalize(-directionalLightDirection);
    float3 H = normalize(V + L);

    float3 radiance = directionalLightColor;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    float3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = 1.0 - kS;
    kD *= (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    float3 ambient = float3(0.03, 0.03, 0.03) * albedo * ao;
    float3 finalColor = Lo + ambient;

    sOut.target0 = float4(finalColor, 1.0);

    return sOut;
}