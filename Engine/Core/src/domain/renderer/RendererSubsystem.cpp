/**
 * @file RendererSubsystem.cpp
 * @brief 渲染子域实现：真实 Vulkan 3D 场景渲染管线（ObjectPass + ToneMappingPass）
 */
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/system/CullingSystem.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/runtime/interface/Application.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            namespace
            {
                constexpr const char* kShadowMapVertexShader = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    uint instanceId = sIn.instanceID;
    GpuInstanceRow inst = g_Instances[instanceId];

    float4 localPos = float4(sIn.position.xyz, 1.0f);
    float4 worldPos = mul(inst.modelMatrix, localPos);
    sOut.position = mul(g_LightViewProjectionMatrix, worldPos);
    sOut.worldPos = worldPos;
    sOut.shadowPos = worldPos;
    sOut.color = sIn.color;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.tangent = sIn.tangent;
    sOut.bitangent = sIn.bitangent;
    sOut.boneIds = sIn.boneIds;
    sOut.boneWeights = sIn.boneWeights;
    sOut.instanceId = instanceId;
    return sOut;
}
)";

                constexpr const char* kDefaultObjectVertexShader = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    uint instanceId = sIn.instanceID;
    GpuInstanceRow inst = g_Instances[instanceId];

    float4 localPos = float4(sIn.position.xyz, 1.0f);
    float4 worldPos = mul(inst.modelMatrix, localPos);
    sOut.position = mul(g_ViewProjectionMatrix, worldPos);
    sOut.worldPos = worldPos;
    sOut.shadowPos = mul(g_LightViewProjectionMatrix, worldPos);
    sOut.color = sIn.color;
    sOut.texCoord = sIn.texCoord;
    float3x3 normalMat = (float3x3)inst.invTransposeMatrix;
    sOut.normal = normalize(mul(normalMat, sIn.normal));
    float3x3 modelMat3 = (float3x3)inst.modelMatrix;
    sOut.tangent = normalize(mul(modelMat3, sIn.tangent));
    sOut.bitangent = normalize(mul(modelMat3, sIn.bitangent));
    sOut.boneIds = int4(instanceId, sIn.boneIds.y, sIn.boneIds.z, sIn.boneIds.w);
    sOut.boneWeights = sIn.boneWeights;
    sOut.instanceId = instanceId;
    return sOut;
}
)";

                constexpr const char* kDefaultObjectPixelShader = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;

    uint instanceId = sIn.instanceId;
    GpuInstanceRow inst = g_Instances[instanceId];
    GpuMaterialRecord mat = g_Materials[inst.materialSlot];

    float3 vtxColor = (dot(sIn.color.rgb, sIn.color.rgb) > 0.0001f) ? sIn.color.rgb : float3(1.0f, 1.0f, 1.0f);
    float4 sampledColor = SampleBindlessTexture(mat.albedoTexIndex, sIn.texCoord);
    float3 albedo = mat.baseColorFactor.rgb * vtxColor * sampledColor.rgb;
    float metallic = saturate(mat.metallicRoughnessNormalOcclusion.x);
    float roughness = clamp(mat.metallicRoughnessNormalOcclusion.y, 0.045f, 1.0f);
    float ao = saturate(mat.metallicRoughnessNormalOcclusion.w);

    ApplyProceduralGroundGridLinear(albedo, roughness, sIn.worldPos.xyz, inst.flags);

    float3 N = normalize(sIn.normal);
    if (mat.normalTexIndex != 1u) {
        float3 normalMap = SampleBindlessTexture(mat.normalTexIndex, sIn.texCoord).xyz * 2.0f - 1.0f;
        normalMap.xy *= mat.metallicRoughnessNormalOcclusion.z;
        float3x3 TBN = float3x3(normalize(sIn.tangent), normalize(sIn.bitangent), N);
        N = normalize(mul(normalMap, TBN));
    }

    float directionalShadow = 1.0f;
    MQ_SAMPLE_DIRECTIONAL_SHADOW(DepthFromLigth0Image, sIn.worldPos.xyz, N, directionalShadow);

    float3 emissive = mat.emissiveFactor.rgb * mat.emissiveFactor.w;
    float3 litHdrColor = EvaluateStandardPbrShading(
        sIn.worldPos.xyz, N, albedo, metallic, roughness, ao, emissive, directionalShadow, instanceId);

    sOut.target0 = float4(litHdrColor, 1.0f);
    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);
    sOut.target2 = float4(N * 0.5f + 0.5f, roughness);
    return sOut;
}
)";

                constexpr const char* kToneMappingVertexShader = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    sOut.position = sIn.position;
    sOut.color = sIn.color;
    sOut.texCoord = sIn.texCoord;
    sOut.normal = sIn.normal;
    sOut.tangent = sIn.tangent;
    sOut.bitangent = sIn.bitangent;
    sOut.boneIds = sIn.boneIds;
    sOut.boneWeights = sIn.boneWeights;
    sOut.worldPos = sIn.position;
    sOut.shadowPos = float4(0.0f, 0.0f, 0.0f, 1.0f);
    sOut.instanceId = 0;
    return sOut;
}
)";

                constexpr const char* kToneMappingPixelShader = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    if (g_EnvironmentParams.w < 0.5f) {
        // Note: 当前呈现目标没有激活相机时仅输出清屏背景，避免凭空伪造天空射线
        float3 clearLinear = float3(0.04f, 0.06f, 0.10f);
        float clearLuma = sqrt(saturate(dot(clearLinear, float3(0.2126f, 0.7152f, 0.0722f))));
        sOut.target0 = float4(clearLinear, clearLuma);
        return sOut;
    }

    float4 hdrSample = SceneHDRColor.Sample(g_LinearSampler, sIn.texCoord);

    // Note: 负高度视口下顶部屏幕对应 texCoord.y = 1，而相机投影矩阵中上方 (+up) 对应 ndc.y = -1
    float2 ndc = float2(sIn.texCoord.x * 2.0f - 1.0f, 1.0f - sIn.texCoord.y * 2.0f);
    float4 worldNearH = mul(g_InvViewProjectionMatrix, float4(ndc, 0.0f, 1.0f));
    float4 worldFarH  = mul(g_InvViewProjectionMatrix, float4(ndc, 1.0f, 1.0f));
    float3 worldNear = worldNearH.xyz / max(abs(worldNearH.w), 0.00001f);
    float3 worldFar  = worldFarH.xyz  / max(abs(worldFarH.w),  0.00001f);
    float3 skyRayDir = normalize(worldFar - worldNear);
    if (g_ProjectionMatrix[2][2] < 0.0f) {
        skyRayDir = -skyRayDir;
    }

    float3 skyHdr = EvaluateProceduralHdrSky(skyRayDir, true);
    float3 combinedHdr = lerp(skyHdr, hdrSample.rgb, saturate(hdrSample.a));
    combinedHdr *= max(g_EnvironmentParams.x, 0.01f);

    // Note: 输出纯线性 [0,1] RGB 并在 Alpha 通道写入感知亮度，供下游 FXAA 3.11 边缘抗锯齿通道采样
    float3 mappedLinear = ACESFittedToneMap(combinedHdr);
    float perceptualLuma = sqrt(saturate(dot(mappedLinear, float3(0.2126f, 0.7152f, 0.0722f))));
    sOut.target0 = float4(mappedLinear, perceptualLuma);
    if (sIn.instanceId > 999999) sOut.target0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return sOut;
}
)";

                constexpr const char* kAntiAliasingPixelShader = R"(
#include "BindlessCommon.hlsli"

float SampleToneMappedLuma(float2 uv)
{
    return ToneMappedColor.SampleLevel(g_LinearSampler, uv, 0.0f).a;
}

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    // Note: 负高度视口下全屏网格顶部顶点 texCoord.y = 1 对应目标纹理 v = 0，采样前翻转 Y 以保持正立图像方向
    float2 posM = float2(sIn.texCoord.x, 1.0f - sIn.texCoord.y);
    float4 rgbyM = ToneMappedColor.SampleLevel(g_LinearSampler, posM, 0.0f);

    if (g_CameraWorldPos.w < 0.5f) {
        sOut.target0 = float4(rgbyM.rgb, 1.0f);
        return sOut;
    }

    uint texW = 0u;
    uint texH = 0u;
    ToneMappedColor.GetDimensions(texW, texH);
    float2 rcpFrame = (texW > 0u && texH > 0u)
        ? float2(1.0f / float(texW), 1.0f / float(texH))
        : g_ViewportSize.zw;

    float lumaM  = rgbyM.a;
    float lumaN  = SampleToneMappedLuma(posM + float2( 0.0f, -rcpFrame.y));
    float lumaS  = SampleToneMappedLuma(posM + float2( 0.0f,  rcpFrame.y));
    float lumaW  = SampleToneMappedLuma(posM + float2(-rcpFrame.x,  0.0f));
    float lumaE  = SampleToneMappedLuma(posM + float2( rcpFrame.x,  0.0f));

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float range = lumaMax - lumaMin;

    // FXAA 3.11 Quality Preset 29 对比度门限剔除
    if (range < max(0.0312f, lumaMax * 0.125f)) {
        sOut.target0 = float4(rgbyM.rgb, 1.0f);
        return sOut;
    }

    float lumaNW = SampleToneMappedLuma(posM + float2(-rcpFrame.x, -rcpFrame.y));
    float lumaNE = SampleToneMappedLuma(posM + float2( rcpFrame.x, -rcpFrame.y));
    float lumaSW = SampleToneMappedLuma(posM + float2(-rcpFrame.x,  rcpFrame.y));
    float lumaSE = SampleToneMappedLuma(posM + float2( rcpFrame.x,  rcpFrame.y));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float lumaSWSE = lumaSW + lumaSE;

    // 3x3 亚像素抗锯齿混合权重 (subpixQuality = 0.75)
    float subpixAvg = (2.0f * (lumaNS + lumaWE) + lumaNWSW + lumaNESE) * (1.0f / 12.0f);
    float subpixFactor = saturate(abs(subpixAvg - lumaM) / max(range, 0.0001f));
    float subpixSmooth = smoothstep(0.0f, 1.0f, subpixFactor);
    float subpixBlend = subpixSmooth * subpixSmooth * 0.75f;

    float edgeHorz = abs(-2.0f * lumaW + lumaNWSW) +
                     abs(-2.0f * lumaM + lumaNS) * 2.0f +
                     abs(-2.0f * lumaE + lumaNESE);
    float edgeVert = abs(-2.0f * lumaN + lumaNWNE) +
                     abs(-2.0f * lumaM + lumaWE) * 2.0f +
                     abs(-2.0f * lumaS + lumaSWSE);
    bool horzSpan = (edgeHorz >= edgeVert);

    float lengthSign = horzSpan ? rcpFrame.y : rcpFrame.x;
    float lumaNeg = horzSpan ? lumaN : lumaW;
    float lumaPos = horzSpan ? lumaS : lumaE;
    float gradNeg = abs(lumaNeg - lumaM);
    float gradPos = abs(lumaPos - lumaM);

    if (gradNeg >= gradPos) {
        lengthSign = -lengthSign;
    }

    float lumaPairAvg = 0.5f * (lumaM + ((gradNeg >= gradPos) ? lumaNeg : lumaPos));
    float gradientScaled = 0.25f * max(gradNeg, gradPos);

    float2 posB = posM;
    if (horzSpan) {
        posB.y += lengthSign * 0.5f;
    } else {
        posB.x += lengthSign * 0.5f;
    }

    float2 offNP = horzSpan ? float2(rcpFrame.x, 0.0f) : float2(0.0f, rcpFrame.y);
    float2 posN = posB - offNP;
    float2 posP = posB + offNP;
    float lumaEndN = SampleToneMappedLuma(posN) - lumaPairAvg;
    float lumaEndP = SampleToneMappedLuma(posP) - lumaPairAvg;
    bool doneN = (abs(lumaEndN) >= gradientScaled);
    bool doneP = (abs(lumaEndP) >= gradientScaled);

    static const float kStepScales[11] = {
        1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 8.0f
    };

    [unroll]
    for (int stepIdx = 0; stepIdx < 11; ++stepIdx) {
        if (!doneN) {
            posN -= offNP * kStepScales[stepIdx];
            lumaEndN = SampleToneMappedLuma(posN) - lumaPairAvg;
            doneN = (abs(lumaEndN) >= gradientScaled);
        }
        if (!doneP) {
            posP += offNP * kStepScales[stepIdx];
            lumaEndP = SampleToneMappedLuma(posP) - lumaPairAvg;
            doneP = (abs(lumaEndP) >= gradientScaled);
        }
    }

    float dstN = horzSpan ? (posM.x - posN.x) : (posM.y - posN.y);
    float dstP = horzSpan ? (posP.x - posM.x) : (posP.y - posM.y);
    bool directionN = (dstN < dstP);
    float dstMin = min(dstN, dstP);
    float spanLength = max(dstN + dstP, 0.00001f);
    float lumaEndClosest = directionN ? lumaEndN : lumaEndP;
    bool lumaMLessZero = ((lumaM - lumaPairAvg) < 0.0f);
    bool goodSpan = ((lumaEndClosest < 0.0f) != lumaMLessZero);

    float edgeOffset = goodSpan ? (0.5f - dstMin / spanLength) : 0.0f;
    float finalOffset = max(edgeOffset, subpixBlend);

    float2 finalUv = posM;
    if (horzSpan) {
        finalUv.y += finalOffset * lengthSign;
    } else {
        finalUv.x += finalOffset * lengthSign;
    }

    float3 aaLinear = ToneMappedColor.SampleLevel(g_LinearSampler, finalUv, 0.0f).rgb;
    sOut.target0 = float4(aaLinear, 1.0f);
    if (sIn.instanceId > 999999) sOut.target0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return sOut;
}
)";

                float srgbChannelToLinear(float c) noexcept
                {
                    const float clamped = std::clamp(c, 0.0f, 1.0f);
                    if (clamped <= 0.04045f) {
                        return clamped / 12.92f;
                    }
                    return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
                }

                FCT::Vec3<> srgbColorToLinear(const FCT::Vec3<>& srgb) noexcept
                {
                    return FCT::Vec3<>(
                        srgbChannelToLinear(srgb.x),
                        srgbChannelToLinear(srgb.y),
                        srgbChannelToLinear(srgb.z));
                }

                FCT::Mat4 buildVulkanOrthoMatrix(
                    float halfWidth,
                    float halfHeight,
                    float zNear,
                    float zFar,
                    bool reverseZ = false)
                {
                    const float safeHalfW = std::max(halfWidth, 0.05f);
                    const float safeHalfH = std::max(halfHeight, 0.05f);
                    const float range = std::max(zFar - zNear, 0.01f);
                    const float zScale = reverseZ ? (-1.0f / range) : (1.0f / range);
                    const float zOffset = reverseZ ? (zFar / range) : (-zNear / range);

                    return FCT::Mat4(
                        1.0f / safeHalfW, 0.0f,             0.0f,   0.0f,
                        0.0f,             1.0f / safeHalfH, 0.0f,   0.0f,
                        0.0f,             0.0f,             zScale, zOffset,
                        0.0f,             0.0f,             0.0f,   1.0f
                    );
                }

                FCT::Mat4 buildLeftHandedViewMatrix(
                    const FCT::Vec3<>& eye,
                    const FCT::Vec3<>& target,
                    const FCT::Vec3<>& worldUp)
                {
                    FCT::Vec3<> forward = target - eye;
                    if (forward.lengthSquared() < 1e-6f) {
                        forward = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
                    } else {
                        forward = forward.normalize();
                    }
                    FCT::Vec3<> safeUp = worldUp;
                    if (std::abs(forward.dot(safeUp)) > 0.99f) {
                        safeUp = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
                    }
                    FCT::Vec3<> right = safeUp.cross(forward).normalize();
                    FCT::Vec3<> up = forward.cross(right).normalize();

                    // Note: Vulkan NDC 空间 +X 向右、-Y 向上、+Z 向前 [0, 1]，因此在视图矩阵第 2 行翻转 Y 轴
                    return FCT::Mat4(
                        right.x,    right.y,    right.z,    -right.dot(eye),
                        -up.x,      -up.y,      -up.z,      up.dot(eye),
                        forward.x,  forward.y,  forward.z,  -forward.dot(eye),
                        0.0f,       0.0f,       0.0f,       1.0f
                    );
                }

                FCT::Mat4 buildVulkanPerspectiveMatrix(
                    float fovYDeg,
                    float aspect,
                    float zNear,
                    float zFar,
                    bool reverseZ = false)
                {
                    const float safeAspect = std::max(aspect, 0.05f);
                    const float halfRad = (fovYDeg * 3.14159265f / 180.0f) * 0.5f;
                    const float tanHalfFov = std::max(std::tan(halfRad), 0.001f);
                    const float yScale = 1.0f / tanHalfFov;
                    const float xScale = yScale / safeAspect;
                    const float range = std::max(zFar - zNear, 0.01f);
                    const float zScale = reverseZ ? (-zNear / range) : (zFar / range);
                    const float zOffset = reverseZ ? ((zFar * zNear) / range) : (-(zFar * zNear) / range);

                    return FCT::Mat4(
                        xScale, 0.0f,   0.0f,   0.0f,
                        0.0f,   yScale, 0.0f,   0.0f,
                        0.0f,   0.0f,   zScale, zOffset,
                        0.0f,   0.0f,   1.0f,   0.0f
                    );
                }

                void applyPassViewportAndScissor(FCT::RHI::CommandBuffer* cmdBuf,
                                                 const FCT::OutputInfo& outputInfo,
                                                 uint32_t fallbackWidth,
                                                 uint32_t fallbackHeight)
                {
                    if (!cmdBuf) {
                        return;
                    }
                    uint32_t w = (outputInfo.width > 0) ? outputInfo.width : fallbackWidth;
                    uint32_t h = (outputInfo.height > 0) ? outputInfo.height : fallbackHeight;
                    if (outputInfo.isWindow && outputInfo.window) {
                        if (outputInfo.window->getWidth() > 0 && outputInfo.window->getHeight() > 0) {
                            w = static_cast<uint32_t>(outputInfo.window->getWidth());
                            h = static_cast<uint32_t>(outputInfo.window->getHeight());
                        }
                    }
                    cmdBuf->viewport(FCT::Vec2(0.0f, 0.0f), FCT::Vec2(static_cast<float>(w), static_cast<float>(h)));
                    cmdBuf->scissor(FCT::Vec2(0.0f, 0.0f), FCT::Vec2(static_cast<float>(w), static_cast<float>(h)));
                }

                FCT::Mat4 buildEntityCameraViewMatrix(
                    const FCT::ECS::registry& reg,
                    FCT::ECS::entity entity,
                    const Scene::LocalTransform& tf,
                    FCT::Vec3<>& outEyePos)
                {
                    outEyePos = tf.position;
                    const FCT::Mat4 localRot =
                        FCT::Mat4::RotateZ(tf.rotation.z) *
                        FCT::Mat4::RotateY(tf.rotation.y) *
                        FCT::Mat4::RotateX(tf.rotation.x);
                    FCT::Vec3<> forward(localRot.m[2], localRot.m[6], localRot.m[10]);
                    FCT::Vec3<> upAxis(localRot.m[1], localRot.m[5], localRot.m[9]);

                    if (const auto* wtf = reg.try_get<Scene::WorldTransform>(entity); wtf && !tf.isDirty) {
                        outEyePos = FCT::Vec3<>(
                            wtf->worldMatrix.m[3],
                            wtf->worldMatrix.m[7],
                            wtf->worldMatrix.m[11]);
                        const FCT::Vec3<> worldUp(
                            wtf->worldMatrix.m[1],
                            wtf->worldMatrix.m[5],
                            wtf->worldMatrix.m[9]);
                        const FCT::Vec3<> worldFwd(
                            wtf->worldMatrix.m[2],
                            wtf->worldMatrix.m[6],
                            wtf->worldMatrix.m[10]);
                        if (worldFwd.lengthSquared() > 1e-8f) {
                            forward = worldFwd.normalize();
                        }
                        if (worldUp.lengthSquared() > 1e-8f) {
                            upAxis = worldUp.normalize();
                        }
                    } else {
                        if (forward.lengthSquared() > 1e-8f) {
                            forward = forward.normalize();
                        } else {
                            forward = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
                        }
                        if (upAxis.lengthSquared() > 1e-8f) {
                            upAxis = upAxis.normalize();
                        } else {
                            upAxis = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
                        }
                    }

                    return buildLeftHandedViewMatrix(outEyePos, outEyePos + forward, upAxis);
                }
            } // namespace

            RendererSubsystem::RendererSubsystem()
            {
                m_viewMatrix = buildLeftHandedViewMatrix(
                    FCT::Vec3<>(0.0f, 4.5f, -9.5f),
                    FCT::Vec3<>(0.0f, 1.0f, 0.0f),
                    FCT::Vec3<>(0.0f, 1.0f, 0.0f));
                m_projMatrix = buildVulkanPerspectiveMatrix(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
                publishTargetBindingSnapshot();
                publishCameraSnapshot({}, m_viewMatrix, m_projMatrix, m_cameraWorldPos, false);
                publishFrameStatsSnapshot(m_frameStats);
                initDefaultPipelineObjects();
            }

            void RendererSubsystem::publishTargetBindingSnapshot()
            {
                TargetBindingSnapshot snap;
                snap.bindings = m_logicTargetBindings;
                snap.primaryPresentTargetName = m_logicPrimaryPresentTargetName;
                publishRingSnapshot(m_targetBindingSnapshots, m_publishedTargetBindingSlot, std::move(snap));
            }

            void RendererSubsystem::publishCameraSnapshot(
                std::vector<ActiveCameraEntry> cameras,
                const FCT::Mat4& activeView,
                const FCT::Mat4& activeProj,
                const FCT::Vec3<>& camPos,
                bool hasActivePrimary)
            {
                CameraFrameSnapshot snap;
                snap.cameras = std::move(cameras);
                snap.activeViewMatrix = activeView;
                snap.activeProjMatrix = activeProj;
                snap.cameraWorldPos = camPos;
                snap.hasActivePrimaryCamera = hasActivePrimary;
                publishRingSnapshot(m_cameraSnapshots, m_publishedCameraSlot, std::move(snap));
            }

            void RendererSubsystem::publishPipelineObjectSnapshot()
            {
                publishRingSnapshot(m_pipelineObjectSnapshots, m_publishedPipelineObjectSlot, m_logicPipelineObjects);
            }

            void RendererSubsystem::publishFrameStatsSnapshot(const RenderFrameStats& stats)
            {
                publishRingSnapshot(m_frameStatsSnapshots, m_publishedFrameStatsSlot, stats);
            }

            Mat4 RendererSubsystem::buildViewMatrix(
                const Vec3<>& eye,
                const Vec3<>& target,
                const Vec3<>& worldUp) noexcept
            {
                return buildLeftHandedViewMatrix(eye, target, worldUp);
            }

            Mat4 RendererSubsystem::buildPerspectiveProjection(
                float fovYDeg,
                float aspect,
                float zNear,
                float zFar,
                bool reverseZ) noexcept
            {
                return buildVulkanPerspectiveMatrix(fovYDeg, aspect, zNear, zFar, reverseZ);
            }

            FCT::Mat4 RendererSubsystem::buildOrthoProjection(
                float halfWidth,
                float halfHeight,
                float zNear,
                float zFar,
                bool reverseZ) noexcept
            {
                return buildVulkanOrthoMatrix(halfWidth, halfHeight, zNear, zFar, reverseZ);
            }

            void RendererSubsystem::bindRenderTarget(
                const std::string& targetName,
                FCT::Image* colorTarget,
                FCT::Image* depthStencilTarget,
                uint32_t width,
                uint32_t height,
                int32_t viewportX,
                int32_t viewportY)
            {
                ZoneScoped();
                const std::string key = targetName.empty() ? std::string("MainWindow") : targetName;
                RenderTargetBindingDesc& desc = m_logicTargetBindings[key];
                desc.targetName = key;
                desc.colorTarget = colorTarget;
                desc.depthStencilTarget = depthStencilTarget;
                desc.width = width;
                desc.height = height;
                desc.viewportX = viewportX;
                desc.viewportY = viewportY;
                const bool isPrimary = (key == m_logicPrimaryPresentTargetName);
                publishTargetBindingSnapshot();

                if (isPrimary && (colorTarget || depthStencilTarget)) {
                    m_externalColorTarget.store(colorTarget, std::memory_order_release);
                    m_externalDepthStencilTarget.store(depthStencilTarget, std::memory_order_release);
                }
            }

            void RendererSubsystem::unbindRenderTarget(std::string_view targetName)
            {
                ZoneScoped();
                const std::string key = targetName.empty() ? std::string("MainWindow") : std::string(targetName);
                auto it = m_logicTargetBindings.find(key);
                if (it == m_logicTargetBindings.end()) {
                    return;
                }

                const bool wasPrimary = (key == m_logicPrimaryPresentTargetName);
                FCT::Image* boundColor = it->second.colorTarget;
                FCT::Image* boundDepth = it->second.depthStencilTarget;
                m_logicTargetBindings.erase(it);
                publishTargetBindingSnapshot();

                if (wasPrimary) {
                    FCT::Image* expectedColor = boundColor;
                    if (boundColor) {
                        (void)m_externalColorTarget.compare_exchange_strong(
                            expectedColor, nullptr, std::memory_order_acq_rel);
                    }
                    FCT::Image* expectedDepth = boundDepth;
                    if (boundDepth) {
                        (void)m_externalDepthStencilTarget.compare_exchange_strong(
                            expectedDepth, nullptr, std::memory_order_acq_rel);
                    }
                }
            }

            void RendererSubsystem::setPrimaryPresentTargetName(std::string targetName)
            {
                ZoneScoped();
                const std::string resolved = targetName.empty() ? std::string("MainWindow") : std::move(targetName);
                m_logicPrimaryPresentTargetName = resolved;
                FCT::Image* matchedColor = nullptr;
                FCT::Image* matchedDepth = nullptr;
                if (auto it = m_logicTargetBindings.find(resolved); it != m_logicTargetBindings.end()) {
                    matchedColor = it->second.colorTarget;
                    matchedDepth = it->second.depthStencilTarget;
                }
                publishTargetBindingSnapshot();

                if (matchedColor || matchedDepth) {
                    m_externalColorTarget.store(matchedColor, std::memory_order_release);
                    m_externalDepthStencilTarget.store(matchedDepth, std::memory_order_release);
                }
            }

            std::string RendererSubsystem::getPrimaryPresentTargetName() const
            {
                const uint32_t slot = m_publishedTargetBindingSlot.load(std::memory_order_acquire);
                return m_targetBindingSnapshots[slot].primaryPresentTargetName;
            }

            std::optional<RenderTargetBindingDesc> RendererSubsystem::getRenderTargetBinding(std::string_view targetName) const
            {
                const std::string key = targetName.empty() ? std::string("MainWindow") : std::string(targetName);
                const uint32_t slot = m_publishedTargetBindingSlot.load(std::memory_order_acquire);
                const auto& bindings = m_targetBindingSnapshots[slot].bindings;
                if (auto it = bindings.find(key); it != bindings.end()) {
                    return it->second;
                }
                return std::nullopt;
            }

            bool RendererSubsystem::findCameraInSortedList(
                const std::vector<ActiveCameraEntry>& entries,
                std::string_view targetName,
                ActiveCameraEntry& outEntry)
            {
                const std::string_view queryTarget = targetName.empty() ? std::string_view("MainWindow") : targetName;
                for (const auto& entry : entries) {
                    if (entry.targetName == queryTarget) {
                        outEntry = entry;
                        return true;
                    }
                }
                return false;
            }

            void RendererSubsystem::collectSortedActiveCameras(
                uint32_t fallbackWidth,
                uint32_t fallbackHeight,
                const std::unordered_map<std::string, RenderTargetBindingDesc>& bindings,
                std::vector<ActiveCameraEntry>& outEntries) const
            {
                ZoneScoped();
                outEntries.clear();
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized()) {
                    return;
                }

                const std::vector<std::string> regNames = scene->getRegistryNames();
                for (const std::string& regName : regNames) {
                    const FCT::ECS::registry* curReg = scene->getRegistryByName(regName);
                    if (!curReg) {
                        continue;
                    }
                    auto camView = curReg->view<Scene::LocalTransform, Scene::CameraComponent>(
                        FCT::ECS::exclude<Scene::DisabledTag>);
                    for (auto entity : camView) {
                        const auto& cam = camView.get<Scene::CameraComponent>(entity);
                        if (!cam.active) {
                            continue;
                        }

                        const std::string target = cam.targetName.empty() ? std::string("MainWindow") : cam.targetName;
                        uint32_t targetW = fallbackWidth;
                        uint32_t targetH = fallbackHeight;
                        if (auto it = bindings.find(target); it != bindings.end()) {
                            if (it->second.width > 0 && it->second.height > 0) {
                                targetW = it->second.width;
                                targetH = it->second.height;
                            }
                        }
                        const float camAspect =
                            static_cast<float>(std::max(targetW, 1u)) /
                            static_cast<float>(std::max(targetH, 1u));

                        ActiveCameraEntry entry{};
                        entry.registryName = regName;
                        entry.entityId = static_cast<uint32_t>(FCT::ECS::to_integral(entity));
                        entry.targetName = target;
                        entry.priority = cam.priority;
                        entry.orthographic = cam.orthographic;
                        entry.orthoSize = cam.orthoSize;
                        entry.fov = cam.fov;
                        entry.nearPlane = cam.nearPlane;
                        entry.farPlane = cam.farPlane;
                        entry.reverseZ = cam.reverseZ;
                        entry.clearColor = cam.clearColor;

                        const auto& tf = camView.get<Scene::LocalTransform>(entity);
                        entry.viewMatrix = buildEntityCameraViewMatrix(*curReg, entity, tf, entry.worldPosition);
                        if (cam.orthographic) {
                            const float safeHalfH = std::max(cam.orthoSize, 0.1f);
                            entry.projMatrix = buildVulkanOrthoMatrix(
                                safeHalfH * std::max(camAspect, 0.05f),
                                safeHalfH,
                                cam.nearPlane,
                                cam.farPlane,
                                cam.reverseZ);
                        } else {
                            entry.projMatrix = buildVulkanPerspectiveMatrix(
                                cam.fov,
                                camAspect,
                                cam.nearPlane,
                                cam.farPlane,
                                cam.reverseZ);
                        }
                        outEntries.push_back(std::move(entry));
                    }
                }

                std::sort(outEntries.begin(), outEntries.end(), [](const ActiveCameraEntry& a, const ActiveCameraEntry& b) {
                    if (a.targetName != b.targetName) {
                        return a.targetName < b.targetName;
                    }
                    if (a.priority != b.priority) {
                        return a.priority > b.priority;
                    }
                    const bool aMain = (a.registryName == "Main");
                    const bool bMain = (b.registryName == "Main");
                    if (aMain != bMain) {
                        return aMain;
                    }
                    if (a.registryName != b.registryName) {
                        return a.registryName < b.registryName;
                    }
                    return a.entityId < b.entityId;
                });
            }

            void RendererSubsystem::refreshActiveCameras()
            {
                ZoneScoped();
                const uint32_t vpW = m_viewportWidth.load(std::memory_order_acquire);
                const uint32_t vpH = m_viewportHeight.load(std::memory_order_acquire);
                const uint32_t outW = (vpW > 0)
                    ? vpW
                    : ((m_objectPassOutputInfo.width > 0) ? m_objectPassOutputInfo.width : 1280u);
                const uint32_t outH = (vpH > 0)
                    ? vpH
                    : ((m_objectPassOutputInfo.height > 0) ? m_objectPassOutputInfo.height : 720u);

                std::vector<ActiveCameraEntry> refreshed;
                collectSortedActiveCameras(outW, outH, m_logicTargetBindings, refreshed);

                const bool cameraOverride = m_hasCameraOverride.load(std::memory_order_acquire);
                ActiveCameraEntry chosen{};
                const bool found = !cameraOverride &&
                    findCameraInSortedList(refreshed, m_logicPrimaryPresentTargetName, chosen);
                const bool hasPrimary = cameraOverride || found;
                m_hasActivePrimaryCamera.store(hasPrimary, std::memory_order_release);

                if (cameraOverride) {
                    publishCameraSnapshot(std::move(refreshed), m_viewMatrix, m_projMatrix, m_cameraWorldPos, true);
                } else if (found) {
                    m_cameraWorldPos = chosen.worldPosition;
                    publishCameraSnapshot(
                        std::move(refreshed),
                        chosen.viewMatrix,
                        chosen.projMatrix,
                        chosen.worldPosition,
                        true);
                } else {
                    const uint32_t curSlot = m_publishedCameraSlot.load(std::memory_order_acquire);
                    const auto& prev = m_cameraSnapshots[curSlot];
                    publishCameraSnapshot(
                        std::move(refreshed),
                        prev.activeViewMatrix,
                        prev.activeProjMatrix,
                        prev.cameraWorldPos,
                        false);
                }
            }

            std::vector<ActiveCameraEntry> RendererSubsystem::getActiveCameras() const
            {
                if (!m_hasSyncedAtLeastOnce.load(std::memory_order_acquire)) {
                    const uint32_t vpW = m_viewportWidth.load(std::memory_order_acquire);
                    const uint32_t vpH = m_viewportHeight.load(std::memory_order_acquire);
                    const uint32_t outW = (vpW > 0)
                        ? vpW
                        : ((m_objectPassOutputInfo.width > 0) ? m_objectPassOutputInfo.width : 1280u);
                    const uint32_t outH = (vpH > 0)
                        ? vpH
                        : ((m_objectPassOutputInfo.height > 0) ? m_objectPassOutputInfo.height : 720u);
                    const uint32_t bindingSlot = m_publishedTargetBindingSlot.load(std::memory_order_acquire);
                    std::vector<ActiveCameraEntry> immediate;
                    collectSortedActiveCameras(outW, outH, m_targetBindingSnapshots[bindingSlot].bindings, immediate);
                    return immediate;
                }
                const uint32_t slot = m_publishedCameraSlot.load(std::memory_order_acquire);
                return m_cameraSnapshots[slot].cameras;
            }

            std::optional<ActiveCameraEntry> RendererSubsystem::findActiveCameraForTarget(std::string_view targetName) const
            {
                const std::vector<ActiveCameraEntry> snapshot = getActiveCameras();
                ActiveCameraEntry found{};
                if (findCameraInSortedList(snapshot, targetName, found)) {
                    return found;
                }
                return std::nullopt;
            }

            FCT::Mat4 RendererSubsystem::getActiveViewMatrix() const noexcept
            {
                if (m_hasCameraOverride.load(std::memory_order_acquire)) {
                    return m_viewMatrix;
                }
                const uint32_t slot = m_publishedCameraSlot.load(std::memory_order_acquire);
                return m_cameraSnapshots[slot].activeViewMatrix;
            }

            FCT::Mat4 RendererSubsystem::getActiveProjectionMatrix() const noexcept
            {
                if (m_hasCameraOverride.load(std::memory_order_acquire)) {
                    return m_projMatrix;
                }
                const uint32_t slot = m_publishedCameraSlot.load(std::memory_order_acquire);
                return m_cameraSnapshots[slot].activeProjMatrix;
            }

            RendererSubsystem::~RendererSubsystem()
            {
                term();
            }

            void RendererSubsystem::registerInfo()
            {
                if (auto* res = Resource::Resource::InstancePtr()) {
                    res->registerCommandLineHandler([this](const Resource::CommandLineArg& arg) -> bool {
                        if (arg.matchesKey("screenshot") || arg.matchesKey("capture-output")) {
                            m_cliScreenshotPath = std::string(arg.value);
                            return true;
                        }
                        if (arg.matchesKey("screenshot-target") || arg.matchesKey("capture-target")) {
                            m_cliScreenshotTarget = std::string(arg.value);
                            return true;
                        }
                        if (arg.matchesKey("screenshot-frame") || arg.matchesKey("capture-frame")) {
                            m_cliScreenshotFrame = static_cast<uint32_t>(std::max<int64_t>(1, arg.asInt(20)));
                            return true;
                        }
                        return false;
                    });
                }
            }

            Status RendererSubsystem::init()
            {
                if (m_initialized.load(std::memory_order_relaxed)) {
                    return OkStatus();
                }

                auto* renderHost = Host::RenderHost::InstancePtr();
                if (renderHost && renderHost->getContext()) {
                    m_renderGraph = renderHost->getContext()->getModule<FCT::RenderGraph>();
                }

                m_isGraphBuilt = false;
                m_isGraphCompiled = false;
                m_tickersRegistered = false;
                m_hasSyncedAtLeastOnce.store(false, std::memory_order_release);
                m_initialized.store(true, std::memory_order_release);

                return compileAndBindGraph();
            }

            void RendererSubsystem::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_relaxed)) {
                    return;
                }

                m_initialized.store(false, std::memory_order_release);
                if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                    if (FCT::Context* ctx = renderHost->getContext()) {
                        if (m_tickersRegistered) {
                            ctx->syncTickers()["RendererSyncFrameSnapshot"] = {[]() {}, {}, {}};
                            (void)ctx->syncTickers().update();
                        }
                    }
                }
                releaseGpuResources();

                m_extensions.clear();
                m_customPasses.clear();
                m_keepImageCallbacks.clear();
                m_passSubscriptions.clear();
                m_syncNodes.clear();
                m_renderGraph = nullptr;
                m_isGraphBuilt = false;
                m_isGraphCompiled = false;
                m_tickersRegistered = false;

                m_externalColorTarget.store(nullptr, std::memory_order_release);
                m_externalDepthStencilTarget.store(nullptr, std::memory_order_release);
                m_hasCameraOverride.store(false, std::memory_order_release);
                m_hasActivePrimaryCamera.store(false, std::memory_order_release);
                m_hasSyncedAtLeastOnce.store(false, std::memory_order_release);
                m_logicTargetBindings.clear();
                m_logicPrimaryPresentTargetName = "MainWindow";
                publishTargetBindingSnapshot();
                publishCameraSnapshot({}, m_viewMatrix, m_projMatrix, m_cameraWorldPos, false);
                m_submitFrameSnapshot = SubmitSceneFrameSnapshot{};
                m_frameStats = RenderFrameStats{};
                publishFrameStatsSnapshot(m_frameStats);
            }

            RendererSubsystem* RendererSubsystem::InstancePtr() noexcept
            {
                auto* domain = Domain::InstancePtr();
                return domain ? domain->getRenderer() : nullptr;
            }

            void RendererSubsystem::initGpuResources(FCT::Context* ctx)
            {
                if (!ctx) {
                    return;
                }

                if (!m_depthStencilState) {
                    m_depthStencilState = ctx->createResource<FCT::DepthStencilState>();
                    if (m_depthStencilState) {
                        m_depthStencilState->depthTestEnable(true);
                        m_depthStencilState->depthWriteEnable(true);
                        m_depthStencilState->depthCompareOp(FCT::CompareOp::LessOrEqual);
                        m_depthStencilState->stencilTestEnable(false);
                        m_depthStencilState->create();
                    }
                }

                if (!m_noDepthState) {
                    m_noDepthState = ctx->createResource<FCT::DepthStencilState>();
                    if (m_noDepthState) {
                        m_noDepthState->depthTestEnable(false);
                        m_noDepthState->depthWriteEnable(false);
                        m_noDepthState->stencilTestEnable(false);
                        m_noDepthState->create();
                    }
                }

                rebuildDirtyPipelineStates(ctx);

                if (!m_linearSampler) {
                    m_linearSampler = ctx->createResource<FCT::Sampler>();
                    if (m_linearSampler) {
                        m_linearSampler->setFilter(
                            FCT::FilterMode::Linear,
                            FCT::FilterMode::Linear,
                            FCT::FilterMode::Nearest);
                        m_linearSampler->setAddressMode(
                            FCT::AddressMode::ClampToEdge,
                            FCT::AddressMode::ClampToEdge,
                            FCT::AddressMode::ClampToEdge);
                        m_linearSampler->create();
                        if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr()) {
                            gpuTable->registerResource(
                                m_linearSampler,
                                Resource::GpuResourceType::Sampler,
                                64,
                                "Renderer_LinearSampler");
                        }
                    }
                }

                createBuiltinMeshes(ctx);

                if (!m_cameraUniform) {
                    m_cameraUniform = makeUnique<FCT::Uniform>(ctx, CameraUniformSlot);
                }
                if (!m_lightUniform) {
                    m_lightUniform = makeUnique<FCT::Uniform>(ctx, DirectionalLightUniformSlot);
                }
                if (!m_viewPosUniform) {
                    m_viewPosUniform = makeUnique<FCT::Uniform>(ctx, ViewPosUniformSlot);
                }

                if (!m_bindlessManager) {
                    m_bindlessManager = makeUnique<BindlessSet0Manager>();
                    (void)m_bindlessManager->init(ctx);
                }

                // 预热初始化核心 built-in 资源组 (Docs 06)
                (void)getOrCreateResourceGroup("ShadowMapGroup");
                (void)getOrCreateResourceGroup("GBufferGroup");
                (void)getOrCreateResourceGroup("SceneColorGroup");
                (void)getOrCreateResourceGroup("FinalOutputGroup");
            }

            void RendererSubsystem::createBuiltinMeshes(FCT::Context* ctx)
            {
                auto* gpuTable = Resource::GpuResourceTable::InstancePtr();

                // 1. 24 顶点标准立方体网格（逐面独立法线、切线、副切线与 UV）
                if (!m_cubeMesh) {
                    m_cubeMesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, StandardMeshVertexLayout);
                    struct FaceDef
                    {
                        FCT::Vec3<> normal;
                        FCT::Vec3<> tangent;
                        FCT::Vec3<> bitangent;
                        FCT::Vec3<> corners[4];
                    };
                    const FaceDef faces[6] = {
                        // Front (-Z)
                        {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                         {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}},
                        // Back (+Z)
                        {{0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                         {{0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}}},
                        // Top (+Y)
                        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                         {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
                        // Bottom (-Y)
                        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                         {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}}},
                        // Right (+X)
                        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f},
                         {{0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}}},
                        // Left (-X)
                        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f},
                         {{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}}}
                    };
                    const FCT::Vec2 uvs[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
                    std::vector<uint32_t> cubeIndices;
                    cubeIndices.reserve(36);
                    for (uint32_t f = 0; f < 6; ++f) {
                        const uint32_t baseIdx = f * 4;
                        for (uint32_t c = 0; c < 4; ++c) {
                            const auto& p = faces[f].corners[c];
                            m_cubeMesh->addVertex(
                                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                FCT::Vec4(p.x, p.y, p.z, 1.0f),
                                uvs[c],
                                faces[f].normal,
                                faces[f].tangent,
                                faces[f].bitangent);
                        }
                        cubeIndices.push_back(baseIdx + 0);
                        cubeIndices.push_back(baseIdx + 1);
                        cubeIndices.push_back(baseIdx + 2);
                        cubeIndices.push_back(baseIdx + 0);
                        cubeIndices.push_back(baseIdx + 2);
                        cubeIndices.push_back(baseIdx + 3);
                    }
                    m_cubeMesh->setIndices(cubeIndices);
                    m_cubeMesh->create();
                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_cubeMesh,
                            Resource::GpuResourceType::Buffer,
                            24 * 96 + 36 * sizeof(uint32_t),
                            "Renderer_CubeMesh");
                    }
                }

                // 2. XZ 细分地面平面网格 [-0.5, 0.5] (16x16 细分网格，法线朝上 (0, 1, 0))
                if (!m_planeMesh) {
                    m_planeMesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, StandardMeshVertexLayout);
                    constexpr uint32_t kSubdivs = 16;
                    for (uint32_t iz = 0; iz <= kSubdivs; ++iz) {
                        const float v = static_cast<float>(iz) / static_cast<float>(kSubdivs);
                        const float z = -0.5f + v;
                        for (uint32_t ix = 0; ix <= kSubdivs; ++ix) {
                            const float u = static_cast<float>(ix) / static_cast<float>(kSubdivs);
                            const float x = -0.5f + u;
                            m_planeMesh->addVertex(
                                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                FCT::Vec4(x, 0.0f, z, 1.0f),
                                FCT::Vec2(u, v),
                                FCT::Vec3<>(0.0f, 1.0f, 0.0f),
                                FCT::Vec3<>(1.0f, 0.0f, 0.0f),
                                FCT::Vec3<>(0.0f, 0.0f, 1.0f));
                        }
                    }
                    std::vector<uint32_t> planeIndices;
                    planeIndices.reserve(kSubdivs * kSubdivs * 6);
                    for (uint32_t iz = 0; iz < kSubdivs; ++iz) {
                        for (uint32_t ix = 0; ix < kSubdivs; ++ix) {
                            const uint32_t row0 = iz * (kSubdivs + 1);
                            const uint32_t row1 = (iz + 1) * (kSubdivs + 1);
                            planeIndices.push_back(row0 + ix);
                            planeIndices.push_back(row0 + ix + 1);
                            planeIndices.push_back(row1 + ix + 1);
                            planeIndices.push_back(row0 + ix);
                            planeIndices.push_back(row1 + ix + 1);
                            planeIndices.push_back(row1 + ix);
                        }
                    }
                    m_planeMesh->setIndices(planeIndices);
                    m_planeMesh->create();
                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_planeMesh,
                            Resource::GpuResourceType::Buffer,
                            (kSubdivs + 1) * (kSubdivs + 1) * 96 + planeIndices.size() * sizeof(uint32_t),
                            "Renderer_PlaneMesh");
                    }
                }

                // 3. 64x64 高精度平滑法线 UV 球体网格（半径 0.5）
                if (!m_sphereMesh) {
                    m_sphereMesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, StandardMeshVertexLayout);
                    constexpr uint32_t kRings = 64;
                    constexpr uint32_t kSegments = 64;
                    constexpr float kPi = 3.1415926535f;
                    constexpr float kRadius = 0.5f;

                    for (uint32_t r = 0; r <= kRings; ++r) {
                        const float v = static_cast<float>(r) / static_cast<float>(kRings);
                        const float phi = v * kPi;
                        const float sinPhi = std::sin(phi);
                        const float cosPhi = std::cos(phi);

                        for (uint32_t s = 0; s <= kSegments; ++s) {
                            const float u = static_cast<float>(s) / static_cast<float>(kSegments);
                            const float theta = u * 2.0f * kPi;
                            const float sinTheta = std::sin(theta);
                            const float cosTheta = std::cos(theta);

                            const FCT::Vec3<> normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
                            const FCT::Vec3<> pos = normal * kRadius;
                            const FCT::Vec3<> tangent(-sinTheta, 0.0f, cosTheta);
                            const FCT::Vec3<> bitangent = normal.cross(tangent).normalize();

                            m_sphereMesh->addVertex(
                                FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                                FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),
                                FCT::Vec2(u, v),
                                normal,
                                tangent,
                                bitangent);
                        }
                    }

                    std::vector<uint32_t> sphereIndices;
                    sphereIndices.reserve(kRings * kSegments * 6);
                    for (uint32_t r = 0; r < kRings; ++r) {
                        for (uint32_t s = 0; s < kSegments; ++s) {
                            const uint32_t curRow = r * (kSegments + 1);
                            const uint32_t nextRow = (r + 1) * (kSegments + 1);
                            sphereIndices.push_back(curRow + s);
                            sphereIndices.push_back(nextRow + s);
                            sphereIndices.push_back(nextRow + s + 1);
                            sphereIndices.push_back(curRow + s);
                            sphereIndices.push_back(nextRow + s + 1);
                            sphereIndices.push_back(curRow + s + 1);
                        }
                    }
                    m_sphereMesh->setIndices(sphereIndices);
                    m_sphereMesh->create();
                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_sphereMesh,
                            Resource::GpuResourceType::Buffer,
                            (kRings + 1) * (kSegments + 1) * 96 + sphereIndices.size() * sizeof(uint32_t),
                            "Renderer_SphereMesh");
                    }
                }

                // 4. 全屏四边形网格（用于 ToneMappingPass）
                if (!m_fullScreenMesh) {
                    m_fullScreenMesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, StandardMeshVertexLayout);
                    const FCT::Vec3<> quadPositions[4] = {
                        {-1.0f, -1.0f, 0.0f},
                        { 1.0f, -1.0f, 0.0f},
                        { 1.0f,  1.0f, 0.0f},
                        {-1.0f,  1.0f, 0.0f}
                    };
                    for (const auto& pos : quadPositions) {
                        m_fullScreenMesh->addVertex(
                            FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f),
                            FCT::Vec4(pos.x, pos.y, pos.z, 1.0f),
                            FCT::Vec2(pos.x * 0.5f + 0.5f, pos.y * 0.5f + 0.5f),
                            FCT::Vec3<>(0.0f, 0.0f, 1.0f),
                            FCT::Vec3<>(1.0f, 0.0f, 0.0f),
                            FCT::Vec3<>(0.0f, 1.0f, 0.0f));
                    }
                    m_fullScreenMesh->setIndices({0, 2, 1, 0, 3, 2});
                    m_fullScreenMesh->create();
                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_fullScreenMesh,
                            Resource::GpuResourceType::Buffer,
                            4 * 96 + 6 * sizeof(uint32_t),
                            "Renderer_FullScreenMesh");
                    }
                }

                ensureSampleModelAssetsOnDisk();
            }

            void RendererSubsystem::initPassLayouts(FCT::Context* ctx)
            {
                if (!ctx) {
                    return;
                }

                if (!m_shadowPassLayout) {
                    m_shadowPassLayout = makeUnique<FCT::Layout>(
                        ctx,
                        FCT::PassName("ShadowMapPass"),
                        StandardMeshVertexLayout,
                        StandardPixelLayout);
                }

                if (!m_objectPassLayout) {
                    m_objectPassLayout = makeUnique<FCT::Layout>(
                        ctx,
                        FCT::PassName("ObjectPass"),
                        StandardMeshVertexLayout,
                        StandardPixelLayout);
                }

                if (!m_toneMappingLayout) {
                    m_toneMappingLayout = makeUnique<FCT::Layout>(
                        ctx,
                        FCT::PassName("ToneMappingPass"),
                        StandardMeshVertexLayout,
                        StandardPixelLayout);
                }

                if (!m_antiAliasingLayout) {
                    m_antiAliasingLayout = makeUnique<FCT::Layout>(
                        ctx,
                        FCT::PassName("AntiAliasingPass"),
                        StandardMeshVertexLayout,
                        StandardPixelLayout);
                }
            }

            void RendererSubsystem::cachePassShaders()
            {
                if (m_shadowPassLayout) {
                    m_shadowVsRef = m_shadowPassLayout->cacheVertexShader(kShadowMapVertexShader);
                }

                if (m_objectPassLayout) {
                    m_objectVsRef = m_objectPassLayout->cacheVertexShader(kDefaultObjectVertexShader);
                    const std::string& psCode = m_logicCustomObjectPsHlsl.empty()
                        ? std::string(kDefaultObjectPixelShader)
                        : m_logicCustomObjectPsHlsl;
                    m_objectPsRef = m_objectPassLayout->cachePixelShader(psCode);
                }

                if (m_toneMappingLayout) {
                    m_toneMappingVsRef = m_toneMappingLayout->cacheVertexShader(kToneMappingVertexShader);
                    m_toneMappingPsRef = m_toneMappingLayout->cachePixelShader(kToneMappingPixelShader);
                }

                if (m_antiAliasingLayout) {
                    m_antiAliasingVsRef = m_antiAliasingLayout->cacheVertexShader(kToneMappingVertexShader);
                    m_antiAliasingPsRef = m_antiAliasingLayout->cachePixelShader(kAntiAliasingPixelShader);
                }
            }

            void RendererSubsystem::registerRenderTickers(FCT::Context* ctx)
            {
                if (!ctx || m_tickersRegistered) {
                    return;
                }

                ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>(
                    "ShadowMapPass",
                    [this](FCT::PassInfo& passInfo) {
                        m_shadowPassOutputInfo = passInfo.outputInfo;
                    });

                ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>(
                    "ObjectPass",
                    [this](FCT::PassInfo& passInfo) {
                        m_objectPassOutputInfo = passInfo.outputInfo;
                    });

                ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>(
                    "ToneMappingPass",
                    [this](FCT::PassInfo& passInfo) {
                        m_toneMappingPassOutputInfo = passInfo.outputInfo;
                    });

                ctx->pipeHub().passPipe.subscribe<FCT::PassInfo>(
                    "AntiAliasingPass",
                    [this](FCT::PassInfo& passInfo) {
                        m_antiAliasingPassOutputInfo = passInfo.outputInfo;
                    });

                auto& syncTickers = ctx->syncTickers();
                syncTickers["RendererSyncFrameSnapshot"] = {
                    [this, ctx]() {
                        if (!m_initialized.load(std::memory_order_acquire)) {
                            return;
                        }
                        syncFrameToSubmitThread(ctx);
                    },
                    {},
                    {}
                };
                (void)syncTickers.update();

                auto& submitTickers = ctx->submitTickers();
                submitTickers["RendererSubmitUpdateTicker"] = {
                    [this, ctx]() {
                        if (!m_initialized.load(std::memory_order_acquire)) {
                            return;
                        }
                        updateRenderUniformsOnSubmitThread(ctx);
                    },
                    {},
                    {FCT::RenderGraphTickers::RenderGraphSubmit}
                };
                submitTickers["ScreenshotCaptureSubmitTicker"] = {
                    [this, ctx]() {
                        if (!m_initialized.load(std::memory_order_acquire)) {
                            return;
                        }
                        const uint64_t frameNum = m_submitFrameCounter.fetch_add(1, std::memory_order_acq_rel) + 1;
                        if (!m_cliScreenshotTriggered && !m_cliScreenshotPath.empty() && frameNum >= m_cliScreenshotFrame) {
                            m_cliScreenshotTriggered = true;
                            const uint64_t reqId = m_nextCaptureId.fetch_add(1, std::memory_order_relaxed);
                            m_latestCaptureId.store(reqId, std::memory_order_release);
                            m_pendingCaptures.push_back(PendingFrameCapture{
                                .requestId = reqId,
                                .outputPath = m_cliScreenshotPath,
                                .targetName = m_cliScreenshotTarget.empty() ? "mainWindowColor" : m_cliScreenshotTarget
                            });
                        }
                        auto* uiHost = Host::UIHost::InstancePtr();
                        FCT::Window* wnd = uiHost ? uiHost->getWindow() : nullptr;
                        this->processPendingCaptures(ctx, wnd);
                    },
                    {FCT::RenderGraphTickers::RenderGraphSubmit},
                    {}
                };
                (void)submitTickers.update();

                m_tickersRegistered = true;
            }

            void RendererSubsystem::extractSceneLightingAndDrawItems(
                bool hasRenderCamera,
                const FCT::Mat4& activeView,
                const FCT::Mat4& activeProj,
                bool activeReverseZ,
                SubmitSceneFrameSnapshot& outSnapshot)
            {
                ZoneScoped();
                auto* domain = Domain::InstancePtr();
                auto* scene = domain ? domain->getScene() : nullptr;
                if (!scene || !scene->isInitialized() || !hasRenderCamera) {
                    return;
                }

                outSnapshot.lightEnable = 0.0f;
                outSnapshot.shadowEnable = 0.0f;
                const std::vector<std::string> regNames = scene->getRegistryNames();

                for (const std::string& regName : regNames) {
                    if (!scene->isRegistryVisible(regName)) {
                        continue;
                    }
                    FCT::ECS::registry* curReg = scene->getRegistryByName(regName);
                    if (!curReg) {
                        continue;
                    }
                    auto lightView = curReg->view<Scene::DirectionalLightComponent>(
                        FCT::ECS::exclude<Scene::DisabledTag>);
                    bool foundDirLight = false;
                    for (auto entity : lightView) {
                        const auto& dl = lightView.get<Scene::DirectionalLightComponent>(entity);
                        if (dl.direction.lengthSquared() > 1e-6f) {
                            outSnapshot.lightDir = dl.direction.normalize();
                        }
                        const FCT::Vec3<> linearSunCol = srgbColorToLinear(dl.color);
                        outSnapshot.lightColorIntensity = FCT::Vec4(
                            linearSunCol.x,
                            linearSunCol.y,
                            linearSunCol.z,
                            std::max(dl.intensity, 0.0f));
                        outSnapshot.lightEnable = 1.0f;
                        outSnapshot.shadowEnable = dl.castShadows ? 1.0f : 0.0f;
                        foundDirLight = true;
                        break;
                    }
                    if (foundDirLight) {
                        break;
                    }
                }

                size_t ptIndex = 0;
                for (const std::string& regName : regNames) {
                    if (ptIndex >= 2 || !scene->isRegistryVisible(regName)) {
                        continue;
                    }
                    FCT::ECS::registry* curReg = scene->getRegistryByName(regName);
                    if (!curReg) {
                        continue;
                    }
                    auto ptView = curReg->view<Scene::LocalTransform, Scene::PointLightComponent>(
                        FCT::ECS::exclude<Scene::DisabledTag>);
                    for (auto entity : ptView) {
                        if (ptIndex >= 2) {
                            break;
                        }
                        const auto& ltf = ptView.get<Scene::LocalTransform>(entity);
                        const auto& pl = ptView.get<Scene::PointLightComponent>(entity);
                        FCT::Vec3<> worldPos = ltf.position;
                        if (const auto* wtf = curReg->try_get<Scene::WorldTransform>(entity)) {
                            worldPos = FCT::Vec3<>(wtf->worldMatrix.m[3], wtf->worldMatrix.m[7], wtf->worldMatrix.m[11]);
                        }
                        const FCT::Vec3<> linearPtCol = srgbColorToLinear(pl.color);
                        outSnapshot.ptPosRange[ptIndex] = FCT::Vec4(worldPos.x, worldPos.y, worldPos.z, std::max(pl.range, 0.1f));
                        outSnapshot.ptColorIntensity[ptIndex] = FCT::Vec4(
                            linearPtCol.x,
                            linearPtCol.y,
                            linearPtCol.z,
                            std::max(pl.intensity, 0.0f));
                        ++ptIndex;
                    }
                }

                const FCT::Mat4 activeViewProj = activeProj * activeView;
                Scene::Frustum frustum;
                frustum.extractFromViewProj(activeViewProj, activeReverseZ);

                auto appendEntityDrawItem = [this, &outSnapshot](
                    FCT::ECS::registry& curReg,
                    FCT::ECS::entity entity,
                    bool isCamVisible) {
                    auto& meshComp = curReg.get<Scene::StaticMeshComponent>(entity);
                    const auto& worldTf = curReg.get<Scene::WorldTransform>(entity);

                    if (!meshComp.customMesh && !meshComp.meshAssetPath.empty() &&
                        meshComp.meshAssetPath.rfind("Builtin_", 0) != 0) {
                        meshComp.customMesh = loadOrGetModelMesh(meshComp.meshAssetPath);
                    }
                    if (meshComp.primitiveType < 0 && !meshComp.customMesh) {
                        return;
                    }

                    RenderEntityDrawItem item;
                    item.primitiveType = meshComp.primitiveType;
                    item.customMesh = meshComp.customMesh;
                    item.cameraVisible = isCamVisible;
                    item.castShadows = meshComp.castShadows && (meshComp.primitiveType != 1);
                    if (meshComp.customMesh && !meshComp.meshAssetPath.empty()) {
                        LoadedModelMeshInfo modelInfo{};
                        if (getLoadedModelMeshInfo(meshComp.meshAssetPath, modelInfo)) {
                            item.localHalfExtents = FCT::Vec3<>(
                                std::max((modelInfo.localAabbMax.x - modelInfo.localAabbMin.x) * 0.5f, 0.05f),
                                std::max((modelInfo.localAabbMax.y - modelInfo.localAabbMin.y) * 0.5f, 0.05f),
                                std::max((modelInfo.localAabbMax.z - modelInfo.localAabbMin.z) * 0.5f, 0.05f));
                        }
                    }
                    item.modelMatrix = worldTf.worldMatrix;
                    item.modelInverseTransposeMatrix = worldTf.worldMatrix.inverse().transpose();

                    const float isGroundGrid = (meshComp.primitiveType == 1) ? 1.0f : 0.0f;
                    if (const auto* mat = curReg.try_get<Scene::PbrMaterialComponent>(entity)) {
                        const FCT::Vec3<> linearBase = srgbColorToLinear(mat->baseColor);
                        const FCT::Vec3<> linearEmissive = srgbColorToLinear(mat->emissiveColor);
                        item.baseColorFactor = FCT::Vec4(linearBase.x, linearBase.y, linearBase.z, 1.0f);
                        item.pbrFactors = FCT::Vec4(mat->metallic, mat->roughness, mat->ao, isGroundGrid);
                        item.emissiveFactor = FCT::Vec4(
                            linearEmissive.x,
                            linearEmissive.y,
                            linearEmissive.z,
                            mat->emissiveIntensity);
                    } else {
                        const FCT::Vec3<> linearFallback = srgbColorToLinear(FCT::Vec3<>(0.8f, 0.8f, 0.85f));
                        item.baseColorFactor = FCT::Vec4(linearFallback.x, linearFallback.y, linearFallback.z, 1.0f);
                        item.pbrFactors = FCT::Vec4(0.1f, 0.45f, 1.0f, isGroundGrid);
                        item.emissiveFactor = FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    }

                    outSnapshot.drawItems.push_back(item);
                    if (isCamVisible) {
                        outSnapshot.frameStats.drawCalls++;
                    }
                };

                for (const std::string& regName : regNames) {
                    if (!scene->isRegistryVisible(regName)) {
                        continue;
                    }
                    FCT::ECS::registry* curReg = scene->getRegistryByName(regName);
                    if (!curReg) {
                        continue;
                    }

                    std::vector<FCT::ECS::entity> visibleEntities;
                    Scene::CullingStats cullStats{};
                    Scene::SpatialGraph* spatialPtr = (regName == "Main") ? &scene->getSpatialGraph() : nullptr;
                    scene->getCullingSystem().cullScene(*curReg, frustum, visibleEntities, spatialPtr, &cullStats);

                    outSnapshot.frameStats.totalSceneEntities += cullStats.totalEntities;
                    outSnapshot.frameStats.visibleEntities += cullStats.visibleEntities;
                    outSnapshot.frameStats.culledEntities += cullStats.culledEntities;
                    outSnapshot.frameStats.cullingDurationMs += cullStats.cullingDurationMs;

                    for (auto entity : visibleEntities) {
                        if (curReg->all_of<Scene::DisabledTag>(entity)) {
                            continue;
                        }
                        appendEntityDrawItem(*curReg, entity, true);
                    }

                    if (outSnapshot.shadowEnable > 0.5f) {
                        auto allMeshView = curReg->view<Scene::StaticMeshComponent, Scene::WorldTransform>(
                            FCT::ECS::exclude<Scene::DisabledTag>);
                        for (auto entity : allMeshView) {
                            const auto& mc = allMeshView.get<Scene::StaticMeshComponent>(entity);
                            if (!mc.visible || !mc.castShadows || mc.primitiveType == 1) {
                                continue;
                            }
                            if (std::find(visibleEntities.begin(), visibleEntities.end(), entity) != visibleEntities.end()) {
                                continue;
                            }
                            appendEntityDrawItem(*curReg, entity, false);
                        }
                    }
                }
            }

            void RendererSubsystem::syncFrameToSubmitThread(FCT::Context* ctx)
            {
                ZoneScoped();
                (void)ctx;
                // Note: SyncTicker 在 FCT 帧栅栏中执行（submitThread 阻塞等待下一帧信号），此处可安全无锁读取 ECS 并移交帧快照
                const uint32_t vpW = m_viewportWidth.load(std::memory_order_acquire);
                const uint32_t vpH = m_viewportHeight.load(std::memory_order_acquire);
                const uint32_t outW = (vpW > 0)
                    ? vpW
                    : ((m_objectPassOutputInfo.width > 0) ? m_objectPassOutputInfo.width : 1280u);
                const uint32_t outH = (vpH > 0)
                    ? vpH
                    : ((m_objectPassOutputInfo.height > 0) ? m_objectPassOutputInfo.height : 720u);

                std::vector<ActiveCameraEntry> frameCameras;
                collectSortedActiveCameras(outW, outH, m_logicTargetBindings, frameCameras);

                FCT::Mat4 activeView = m_viewMatrix;
                FCT::Mat4 activeProj = m_projMatrix;
                FCT::Vec3<> activeCamPos = m_cameraWorldPos;
                bool activeReverseZ = false;
                const bool cameraOverride = m_hasCameraOverride.load(std::memory_order_acquire);
                bool hasRenderCamera = false;

                if (cameraOverride) {
                    hasRenderCamera = true;
                    activeReverseZ = (activeProj.m[10] < 0.0f);
                } else {
                    ActiveCameraEntry chosenCam{};
                    if (findCameraInSortedList(frameCameras, m_logicPrimaryPresentTargetName, chosenCam)) {
                        hasRenderCamera = true;
                        activeView = chosenCam.viewMatrix;
                        activeProj = chosenCam.projMatrix;
                        activeCamPos = chosenCam.worldPosition;
                        activeReverseZ = chosenCam.reverseZ;
                    }
                }

                m_hasActivePrimaryCamera.store(hasRenderCamera, std::memory_order_release);
                publishCameraSnapshot(std::move(frameCameras), activeView, activeProj, activeCamPos, hasRenderCamera);
                m_hasSyncedAtLeastOnce.store(true, std::memory_order_release);

                m_submitFrameSnapshot = SubmitSceneFrameSnapshot{};
                m_submitFrameSnapshot.viewportWidth = outW;
                m_submitFrameSnapshot.viewportHeight = outH;
                m_submitFrameSnapshot.hasRenderCamera = hasRenderCamera;
                m_submitFrameSnapshot.activeReverseZ = activeReverseZ;
                m_submitFrameSnapshot.enableAntiAliasing = m_enableAntiAliasing.load(std::memory_order_acquire);
                m_submitFrameSnapshot.activeView = activeView;
                m_submitFrameSnapshot.activeProj = activeProj;
                m_submitFrameSnapshot.activeCamPos = activeCamPos;
                m_submitFrameSnapshot.lightDir = m_submitFrameSnapshot.lightDir.normalize();

                extractSceneLightingAndDrawItems(
                    hasRenderCamera,
                    activeView,
                    activeProj,
                    activeReverseZ,
                    m_submitFrameSnapshot);

                m_submitFrameSnapshot.frameStats.cachedModelCount = static_cast<uint32_t>(m_loadedModelMeshes.size());
                publishFrameStatsSnapshot(m_submitFrameSnapshot.frameStats);

                if (m_logicPipelineObjectsDirty.exchange(false, std::memory_order_acq_rel)) {
                    m_submitPipelineObjectsPending = m_logicPipelineObjects;
                    m_submitPipelineObjectsDirty = true;
                }
                if (m_logicCustomObjectPsDirty.exchange(false, std::memory_order_acq_rel)) {
                    m_submitPendingCustomObjectPsHlsl = m_logicCustomObjectPsHlsl;
                    m_submitCustomObjectPsDirty = true;
                }
            }

            void RendererSubsystem::updateRenderUniformsOnSubmitThread(FCT::Context* ctx)
            {
                ZoneScoped();
                if (!ctx || !m_cameraUniform || !m_lightUniform || !m_viewPosUniform) {
                    return;
                }

                rebuildDirtyPipelineStates(ctx);

                // 在提交线程安全消费 SyncTicker 移交的自定义像素着色器热更新
                if (m_submitCustomObjectPsDirty && !m_submitPendingCustomObjectPsHlsl.empty()) {
                    m_submitCustomObjectPsDirty = false;
                    if (m_objectPassLayout && m_isGraphCompiled) {
                        m_objectPsRef = m_objectPassLayout->cachePixelShader(m_submitPendingCustomObjectPsHlsl);
                    }
                }

                const auto& snap = m_submitFrameSnapshot;
                m_renderDrawItems = snap.drawItems;
                m_frameStats = snap.frameStats;

                // 更新传统兼容槽位 Uniform
                m_cameraUniform->setValue(FCT::UniformType::ProjectionMatrix, snap.activeProj);
                m_cameraUniform->setValue(FCT::UniformType::ViewMatrix, snap.activeView);
                m_cameraUniform->update();

                m_lightUniform->setValue("directionalLightDirection", snap.lightDir);
                m_lightUniform->setValue("directionalLightColorIntensity", snap.lightColorIntensity);
                m_lightUniform->setValue("directionalLightEnable", snap.lightEnable);
                m_lightUniform->setValue("pointLightPosRange0", snap.ptPosRange[0]);
                m_lightUniform->setValue("pointLightColorIntensity0", snap.ptColorIntensity[0]);
                m_lightUniform->setValue("pointLightPosRange1", snap.ptPosRange[1]);
                m_lightUniform->setValue("pointLightColorIntensity1", snap.ptColorIntensity[1]);
                m_lightUniform->update();

                m_viewPosUniform->setValue("viewPos", snap.activeCamPos);
                m_viewPosUniform->update();

                // 构建纹素对齐的主方向光正交阴影视图投影矩阵 (消除镜头移动时的阴影边缘抖动)
                constexpr float kShadowOrthoHalfExtent = 12.0f;
                constexpr float kShadowNear = 0.5f;
                constexpr float kShadowFar = 48.0f;
                constexpr float kShadowMapResolution = 2048.0f;
                const FCT::Vec3<> shadowFocus(0.0f, 0.5f, 0.0f);
                const FCT::Vec3<> lightEye = shadowFocus - snap.lightDir * 22.0f;
                FCT::Mat4 lightView = buildLeftHandedViewMatrix(
                    lightEye,
                    shadowFocus,
                    FCT::Vec3<>(0.0f, 1.0f, 0.0f));
                const float worldUnitsPerTexel = (2.0f * kShadowOrthoHalfExtent) / kShadowMapResolution;
                lightView.m[3] = std::floor(lightView.m[3] / worldUnitsPerTexel) * worldUnitsPerTexel;
                lightView.m[7] = std::floor(lightView.m[7] / worldUnitsPerTexel) * worldUnitsPerTexel;
                const FCT::Mat4 lightProj = buildVulkanOrthoMatrix(
                    kShadowOrthoHalfExtent,
                    kShadowOrthoHalfExtent,
                    kShadowNear,
                    kShadowFar);
                const FCT::Mat4 lightViewProj = lightProj * lightView;

                // 组装并上传全局 Bindless Set 0 表数据 (SSBO 与 512B UBO)
                std::vector<GpuInstanceRow> instanceRows;
                std::vector<GpuMaterialRecord> materialRows;
                instanceRows.reserve(m_renderDrawItems.size());
                materialRows.reserve(m_renderDrawItems.size());

                for (size_t i = 0; i < m_renderDrawItems.size(); ++i) {
                    const auto& item = m_renderDrawItems[i];
                    GpuInstanceRow inst{};
                    inst.modelMatrix = item.modelMatrix;
                    inst.invTransposeMatrix = item.modelInverseTransposeMatrix;
                    const float isSphereShape = (item.primitiveType == 2 && !item.customMesh) ? 1.0f : 0.0f;
                    inst.worldSphereBounds = FCT::Vec4{
                        item.localHalfExtents.x,
                        item.localHalfExtents.y,
                        item.localHalfExtents.z,
                        isSphereShape
                    };
                    inst.meshSlot = static_cast<uint32_t>(item.primitiveType);
                    inst.materialSlot = static_cast<uint32_t>(i);
                    inst.entityId = static_cast<uint32_t>(i);
                    inst.flags = ((item.primitiveType == 1) ? 3u : 1u) | (item.castShadows ? 4u : 0u);
                    instanceRows.push_back(inst);

                    GpuMaterialRecord mat{};
                    mat.baseColorFactor = item.baseColorFactor;
                    mat.metallicRoughnessNormalOcclusion = FCT::Vec4(
                        item.pbrFactors.x,
                        item.pbrFactors.y,
                        1.0f,
                        item.pbrFactors.z);
                    mat.emissiveFactor = item.emissiveFactor;
                    mat.albedoTexIndex = 0;
                    mat.normalTexIndex = 1;
                    mat.ormTexIndex = 0;
                    mat.shadingModelId = 0;
                    mat.pipelineBucket = 0;
                    materialRows.push_back(mat);
                }

                const FCT::Mat4 viewProj = snap.activeProj * snap.activeView;
                GpuSceneUniformData sceneUniform{};
                sceneUniform.viewMatrix = snap.activeView;
                sceneUniform.projectionMatrix = snap.activeProj;
                sceneUniform.viewProjectionMatrix = viewProj;
                sceneUniform.cameraWorldPos = FCT::Vec4{
                    snap.activeCamPos.x,
                    snap.activeCamPos.y,
                    snap.activeCamPos.z,
                    snap.enableAntiAliasing ? 1.0f : 0.0f
                };
                sceneUniform.directionalLightDirection = FCT::Vec4{
                    snap.lightDir.x,
                    snap.lightDir.y,
                    snap.lightDir.z,
                    snap.lightEnable
                };
                sceneUniform.directionalLightColorIntensity = snap.lightColorIntensity;
                sceneUniform.viewportSize = FCT::Vec4{
                    static_cast<float>(snap.viewportWidth),
                    static_cast<float>(snap.viewportHeight),
                    1.0f / static_cast<float>(std::max(snap.viewportWidth, 1u)),
                    1.0f / static_cast<float>(std::max(snap.viewportHeight, 1u))
                };
                sceneUniform.lightViewProjectionMatrix = lightViewProj;
                sceneUniform.invViewProjectionMatrix = viewProj.inverse();
                sceneUniform.shadowParams = FCT::Vec4{
                    snap.shadowEnable,
                    0.00055f,
                    0.014f,
                    1.0f / kShadowMapResolution
                };
                sceneUniform.pointLightPosRange0 = snap.ptPosRange[0];
                sceneUniform.pointLightColorIntensity0 = snap.ptColorIntensity[0];
                sceneUniform.pointLightPosRange1 = snap.ptPosRange[1];
                sceneUniform.pointLightColorIntensity1 = snap.ptColorIntensity[1];
                sceneUniform.environmentParams = FCT::Vec4{1.05f, 1.0f, 1.0f, snap.hasRenderCamera ? 1.0f : 0.0f};
                sceneUniform.skyZenithColor = FCT::Vec4{
                    0.042f,
                    0.132f,
                    0.38f,
                    static_cast<float>(instanceRows.size())
                };
                sceneUniform.skyHorizonGroundColor = FCT::Vec4{0.28f, 0.39f, 0.55f, 0.065f};

                if (m_bindlessManager) {
                    const uint32_t submitFrameIdx = ctx->currentSubmitFrameIndex();
                    m_bindlessManager->updateFrameData(submitFrameIdx, instanceRows, materialRows, {}, sceneUniform);
                    m_bindlessManager->uploadFrameData(submitFrameIdx);
                }
            }

            void RendererSubsystem::recordShadowMapPass(FCT::PassSubmitEvent env)
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire) || !m_shadowPassLayout || !env.cmdBuf) {
                    return;
                }

                auto* cmdBuf = env.cmdBuf;
                applyPassViewportAndScissor(cmdBuf, m_shadowPassOutputInfo, 2048u, 2048u);

                m_shadowPassLayout->begin();
                if (m_shadowPassStates.rasterState) {
                    m_shadowPassLayout->bindRasterizationState(m_shadowPassStates.rasterState);
                }
                if (m_shadowPassStates.depthStencilState) {
                    m_shadowPassLayout->bindDepthStencilState(m_shadowPassStates.depthStencilState);
                } else if (m_depthStencilState) {
                    m_shadowPassLayout->bindDepthStencilState(m_depthStencilState);
                }
                m_shadowPassLayout->bindVertexShader(m_shadowVsRef);

                auto* currentPipeline = m_shadowPassLayout->getCurrentPipeline();
                if (!currentPipeline || currentPipeline->getNativeHandle() == 0) {
                    m_shadowPassLayout->end();
                    return;
                }
                currentPipeline->bind(cmdBuf);

                if (m_bindlessManager && m_bindlessManager->isInitialized()) {
                    uint32_t submitFrameIdx = 0;
                    if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                        if (FCT::Context* ctx = renderHost->getContext()) {
                            submitFrameIdx = ctx->currentSubmitFrameIndex();
                        }
                    }
                    void* nativeLayout = reinterpret_cast<void*>(currentPipeline->getNativeLayoutHandle());
                    m_bindlessManager->bind(cmdBuf, submitFrameIdx, nativeLayout);
                }

                const size_t drawCount = m_renderDrawItems.size();
                for (size_t i = 0; i < drawCount; ++i) {
                    const auto& item = m_renderDrawItems[i];
                    if (!item.castShadows) {
                        continue;
                    }

                    FCT::StaticMesh<uint32_t>* mesh = item.customMesh;
                    if (!mesh) {
                        if (item.primitiveType == 0) {
                            mesh = m_cubeMesh;
                        } else if (item.primitiveType == 1) {
                            mesh = m_planeMesh;
                        } else if (item.primitiveType == 2) {
                            mesh = m_sphereMesh;
                        }
                    }
                    if (!mesh) {
                        continue;
                    }

                    mesh->bind(cmdBuf);
                    const uint32_t instanceBegin = static_cast<uint32_t>(i);
                    const uint32_t indexCount = static_cast<uint32_t>(mesh->getIndexCount());
                    if (indexCount > 0) {
                        cmdBuf->drawIndex(0, instanceBegin, indexCount, 1, 0);
                    } else {
                        cmdBuf->draw(0, instanceBegin, mesh->getVertexCount(), 1);
                    }
                }

                m_shadowPassLayout->end();
            }

            void RendererSubsystem::recordObjectPass(FCT::PassSubmitEvent env)
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire) || !m_objectPassLayout || !env.cmdBuf) {
                    return;
                }

                auto* cmdBuf = env.cmdBuf;
                applyPassViewportAndScissor(
                    cmdBuf,
                    m_objectPassOutputInfo,
                    m_viewportWidth.load(std::memory_order_acquire),
                    m_viewportHeight.load(std::memory_order_acquire));

                m_objectPassLayout->begin();
                if (m_objectPassStates.rasterState) {
                    m_objectPassLayout->bindRasterizationState(m_objectPassStates.rasterState);
                }
                if (m_objectPassStates.depthStencilState) {
                    m_objectPassLayout->bindDepthStencilState(m_objectPassStates.depthStencilState);
                } else if (m_depthStencilState) {
                    m_objectPassLayout->bindDepthStencilState(m_depthStencilState);
                }
                if (m_objectPassStates.blendState) {
                    m_objectPassLayout->bindBlendState(m_objectPassStates.blendState);
                }
                m_objectPassLayout->bindVertexShader(m_objectVsRef);
                m_objectPassLayout->bindPixelShader(m_objectPsRef);

                auto* currentPipeline = m_objectPassLayout->getCurrentPipeline();
                if (!currentPipeline || currentPipeline->getNativeHandle() == 0) {
                    m_objectPassLayout->end();
                    return;
                }
                currentPipeline->bind(cmdBuf);

                // 1. 绑定全局 Bindless Set 0
                if (m_bindlessManager && m_bindlessManager->isInitialized()) {
                    uint32_t submitFrameIdx = 0;
                    if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                        if (FCT::Context* ctx = renderHost->getContext()) {
                            submitFrameIdx = ctx->currentSubmitFrameIndex();
                        }
                    }
                    void* nativeLayout = reinterpret_cast<void*>(currentPipeline->getNativeLayoutHandle());
                    m_bindlessManager->bind(cmdBuf, submitFrameIdx, nativeLayout);
                }

                // 2. 绑定 Pass 级 Set 1 资源 (如 DepthFromLigth0Image 阴影贴图)
                if (auto* passRes = m_objectPassLayout->getCurrentPassResource()) {
                    passRes->bind(cmdBuf, currentPipeline);
                }

                // 3. 现代 Bindless 绘制循环
                const size_t drawCount = m_renderDrawItems.size();
                for (size_t i = 0; i < drawCount; ++i) {
                    const auto& item = m_renderDrawItems[i];
                    if (!item.cameraVisible) {
                        continue;
                    }

                    FCT::StaticMesh<uint32_t>* mesh = item.customMesh;
                    if (!mesh) {
                        if (item.primitiveType == 0) {
                            mesh = m_cubeMesh;
                        } else if (item.primitiveType == 1) {
                            mesh = m_planeMesh;
                        } else if (item.primitiveType == 2) {
                            mesh = m_sphereMesh;
                        }
                    }
                    if (!mesh) {
                        continue;
                    }

                    mesh->bind(cmdBuf);
                    const uint32_t instanceBegin = static_cast<uint32_t>(i);
                    const uint32_t indexCount = static_cast<uint32_t>(mesh->getIndexCount());

                    if (indexCount > 0) {
                        cmdBuf->drawIndex(0, instanceBegin, indexCount, 1, 0);
                    } else {
                        cmdBuf->draw(0, instanceBegin, mesh->getVertexCount(), 1);
                    }
                }

                m_objectPassLayout->end();
            }

            void RendererSubsystem::recordToneMappingPass(FCT::PassSubmitEvent env)
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire) || !m_toneMappingLayout || !env.cmdBuf) {
                    return;
                }

                auto* cmdBuf = env.cmdBuf;
                applyPassViewportAndScissor(
                    cmdBuf,
                    m_toneMappingPassOutputInfo,
                    m_viewportWidth.load(std::memory_order_acquire),
                    m_viewportHeight.load(std::memory_order_acquire));

                m_toneMappingLayout->begin();
                if (m_toneMappingPassStates.rasterState) {
                    m_toneMappingLayout->bindRasterizationState(m_toneMappingPassStates.rasterState);
                }
                if (m_toneMappingPassStates.depthStencilState) {
                    m_toneMappingLayout->bindDepthStencilState(m_toneMappingPassStates.depthStencilState);
                } else if (m_noDepthState) {
                    m_toneMappingLayout->bindDepthStencilState(m_noDepthState);
                }
                if (m_toneMappingPassStates.blendState) {
                    m_toneMappingLayout->bindBlendState(m_toneMappingPassStates.blendState);
                }
                m_toneMappingLayout->bindVertexShader(m_toneMappingVsRef);
                m_toneMappingLayout->bindPixelShader(m_toneMappingPsRef);

                auto* currentPipeline = m_toneMappingLayout->getCurrentPipeline();
                if (!currentPipeline || currentPipeline->getNativeHandle() == 0) {
                    m_toneMappingLayout->end();
                    return;
                }
                currentPipeline->bind(cmdBuf);

                if (m_bindlessManager && m_bindlessManager->isInitialized()) {
                    uint32_t submitFrameIdx = 0;
                    if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                        if (FCT::Context* ctx = renderHost->getContext()) {
                            submitFrameIdx = ctx->currentSubmitFrameIndex();
                        }
                    }
                    void* nativeLayout = reinterpret_cast<void*>(currentPipeline->getNativeLayoutHandle());
                    m_bindlessManager->bind(cmdBuf, submitFrameIdx, nativeLayout);
                }

                if (auto* passRes = m_toneMappingLayout->getCurrentPassResource()) {
                    passRes->bind(cmdBuf, currentPipeline);
                }

                if (m_fullScreenMesh) {
                    m_fullScreenMesh->bind(cmdBuf);
                    m_fullScreenMesh->draw(cmdBuf);
                }
                m_toneMappingLayout->end();
            }

            void RendererSubsystem::recordAntiAliasingPass(FCT::PassSubmitEvent env)
            {
                ZoneScoped();
                if (!m_initialized.load(std::memory_order_acquire) || !m_antiAliasingLayout || !env.cmdBuf) {
                    return;
                }

                auto* cmdBuf = env.cmdBuf;
                applyPassViewportAndScissor(
                    cmdBuf,
                    m_antiAliasingPassOutputInfo,
                    m_viewportWidth.load(std::memory_order_acquire),
                    m_viewportHeight.load(std::memory_order_acquire));

                m_antiAliasingLayout->begin();
                if (m_antiAliasingPassStates.rasterState) {
                    m_antiAliasingLayout->bindRasterizationState(m_antiAliasingPassStates.rasterState);
                }
                if (m_antiAliasingPassStates.depthStencilState) {
                    m_antiAliasingLayout->bindDepthStencilState(m_antiAliasingPassStates.depthStencilState);
                } else if (m_noDepthState) {
                    m_antiAliasingLayout->bindDepthStencilState(m_noDepthState);
                }
                if (m_antiAliasingPassStates.blendState) {
                    m_antiAliasingLayout->bindBlendState(m_antiAliasingPassStates.blendState);
                }
                m_antiAliasingLayout->bindVertexShader(m_antiAliasingVsRef);
                m_antiAliasingLayout->bindPixelShader(m_antiAliasingPsRef);

                auto* currentPipeline = m_antiAliasingLayout->getCurrentPipeline();
                if (!currentPipeline || currentPipeline->getNativeHandle() == 0) {
                    m_antiAliasingLayout->end();
                    return;
                }
                currentPipeline->bind(cmdBuf);

                if (m_bindlessManager && m_bindlessManager->isInitialized()) {
                    uint32_t submitFrameIdx = 0;
                    if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                        if (FCT::Context* ctx = renderHost->getContext()) {
                            submitFrameIdx = ctx->currentSubmitFrameIndex();
                        }
                    }
                    void* nativeLayout = reinterpret_cast<void*>(currentPipeline->getNativeLayoutHandle());
                    m_bindlessManager->bind(cmdBuf, submitFrameIdx, nativeLayout);
                }

                if (auto* passRes = m_antiAliasingLayout->getCurrentPassResource()) {
                    passRes->bind(cmdBuf, currentPipeline);
                }

                if (m_fullScreenMesh) {
                    m_fullScreenMesh->bind(cmdBuf);
                    m_fullScreenMesh->draw(cmdBuf);
                }
                m_antiAliasingLayout->end();
            }

            void RendererSubsystem::releaseGpuResources() noexcept
            {
                if (auto* renderHost = Host::RenderHost::InstancePtr()) {
                    if (FCT::Context* ctx = renderHost->getContext()) {
                        ctx->waitGpu();
                    }
                }

                // 1. 先销毁 Layout（内部释放 VK_PassResource 描述符集与管线缓存）
                m_shadowPassLayout.reset();
                m_objectPassLayout.reset();
                m_toneMappingLayout.reset();
                m_antiAliasingLayout.reset();

                // 2. 释放 Uniform 常量缓冲区池与全局 Uniform
                m_modelUniformPool.clear();
                m_materialUniformPool.clear();
                m_cameraUniform.reset();
                m_lightUniform.reset();
                m_viewPosUniform.reset();
                m_renderDrawItems.clear();

                // 3. 释放 ResourceGroups 与 BindlessSet0Manager 及动态模型缓存
                auto* gpuTable = Resource::GpuResourceTable::InstancePtr();
                auto* gpuMem = Resource::GpuMemoryManager::InstancePtr();
                m_resourceGroups.clear();
                for (auto& kv : m_loadedModelMeshes) {
                    if (kv.second.mesh) {
                        if (gpuTable) {
                            gpuTable->unregisterResource(kv.second.mesh);
                        }
                        FCT_DELETE(kv.second.mesh);
                        kv.second.mesh = nullptr;
                    }
                }
                m_loadedModelMeshes.clear();
                m_cachedModelCount.store(0, std::memory_order_release);

                if (m_bindlessManager) {
                    m_bindlessManager->term();
                    m_bindlessManager.reset();
                }

                // 4. 4步延迟释放流水线：注销并销毁内置网格与采样器/深度状态

                if (m_cubeMesh) {
                    if (gpuMem) {
                        gpuMem->deferReleaseResource(m_cubeMesh, [](const void* ptr) {
                            auto* mesh = const_cast<FCT::StaticMesh<uint32_t>*>(static_cast<const FCT::StaticMesh<uint32_t>*>(ptr));
                            FCT_DELETE(mesh);
                        }, 0);
                    } else {
                        if (gpuTable) gpuTable->unregisterResource(m_cubeMesh);
                        FCT_DELETE(m_cubeMesh);
                    }
                    m_cubeMesh = nullptr;
                }
                if (m_planeMesh) {
                    if (gpuMem) {
                        gpuMem->deferReleaseResource(m_planeMesh, [](const void* ptr) {
                            auto* mesh = const_cast<FCT::StaticMesh<uint32_t>*>(static_cast<const FCT::StaticMesh<uint32_t>*>(ptr));
                            FCT_DELETE(mesh);
                        }, 0);
                    } else {
                        if (gpuTable) gpuTable->unregisterResource(m_planeMesh);
                        FCT_DELETE(m_planeMesh);
                    }
                    m_planeMesh = nullptr;
                }
                if (m_sphereMesh) {
                    if (gpuMem) {
                        gpuMem->deferReleaseResource(m_sphereMesh, [](const void* ptr) {
                            auto* mesh = const_cast<FCT::StaticMesh<uint32_t>*>(static_cast<const FCT::StaticMesh<uint32_t>*>(ptr));
                            FCT_DELETE(mesh);
                        }, 0);
                    } else {
                        if (gpuTable) gpuTable->unregisterResource(m_sphereMesh);
                        FCT_DELETE(m_sphereMesh);
                    }
                    m_sphereMesh = nullptr;
                }
                if (m_fullScreenMesh) {
                    if (gpuMem) {
                        gpuMem->deferReleaseResource(m_fullScreenMesh, [](const void* ptr) {
                            auto* mesh = const_cast<FCT::StaticMesh<uint32_t>*>(static_cast<const FCT::StaticMesh<uint32_t>*>(ptr));
                            FCT_DELETE(mesh);
                        }, 0);
                    } else {
                        if (gpuTable) gpuTable->unregisterResource(m_fullScreenMesh);
                        FCT_DELETE(m_fullScreenMesh);
                    }
                    m_fullScreenMesh = nullptr;
                }
                if (m_linearSampler) {
                    if (gpuTable) gpuTable->unregisterResource(m_linearSampler);
                    m_linearSampler->release();
                    m_linearSampler = nullptr;
                }
                if (m_depthStencilState) {
                    m_depthStencilState->release();
                    m_depthStencilState = nullptr;
                }
                if (m_noDepthState) {
                    m_noDepthState->release();
                    m_noDepthState = nullptr;
                }
                for (FCT::IPipelineResource* res : m_ownedPipelineStateResources) {
                    if (res) {
                        res->release();
                    }
                }
                m_ownedPipelineStateResources.clear();
                m_shadowPassStates = {};
                m_objectPassStates = {};
                m_toneMappingPassStates = {};
                m_antiAliasingPassStates = {};
                m_submitPipelineObjectsDirty = true;
                m_logicPipelineObjectsDirty.store(true, std::memory_order_release);
            }

            void RendererSubsystem::registerRenderGraphExtension(RenderGraphExtensionDesc extension)
            {
                assert(!extension.name.empty() && "RenderGraphExtensionDesc 名称不可为空");

                if (m_renderGraph) {
                    if (auto subIt = m_extensionSubscribeIds.find(extension.name); subIt != m_extensionSubscribeIds.end()) {
                        for (FCT::SubscribeId subId : subIt->second) {
                            if (subId != 0) {
                                m_renderGraph->unsubscribe(subId);
                            }
                        }
                        m_extensionSubscribeIds.erase(subIt);
                    }
                }

                auto it = std::find_if(
                    m_extensions.begin(),
                    m_extensions.end(),
                    [&extension](const RenderGraphExtensionDesc& item) {
                        return item.name == extension.name;
                    });
                if (it != m_extensions.end()) {
                    *it = extension;
                } else {
                    m_extensions.push_back(extension);
                }

                // 若 RenderGraph 已完成编译，则立即应用编译后回调、同步节点与 Pass 提交订阅（支持运行期热重载）
                if (m_isGraphCompiled && m_renderGraph) {
                    if (extension.onPostCompile) {
                        extension.onPostCompile(m_renderGraph);
                    }
                    auto* renderHost = Host::RenderHost::InstancePtr();
                    if (FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr) {
                        for (const auto& syncNode : extension.syncNodes) {
                            if (!syncNode.name.empty() && syncNode.callback) {
                                ctx->syncTickers()[syncNode.name] = {
                                    syncNode.callback,
                                    syncNode.predecessors,
                                    syncNode.successors
                                };
                            }
                        }
                        if (!extension.syncNodes.empty()) {
                            (void)ctx->syncTickers().update();
                        }
                    }
                    auto& subIds = m_extensionSubscribeIds[extension.name];
                    for (const auto& sub : extension.passSubscribers) {
                        if (!sub.passName.empty() && sub.callback) {
                            subIds.push_back(m_renderGraph->subscribe(sub.passName, sub.callback));
                        }
                    }
                }
            }

            void RendererSubsystem::unregisterRenderGraphExtension(const std::string& name)
            {
                auto it = std::find_if(
                    m_extensions.begin(),
                    m_extensions.end(),
                    [&name](const RenderGraphExtensionDesc& item) {
                        return item.name == name;
                    });
                if (it == m_extensions.end()) {
                    return;
                }

                if (m_renderGraph) {
                    if (auto subIt = m_extensionSubscribeIds.find(name); subIt != m_extensionSubscribeIds.end()) {
                        for (FCT::SubscribeId subId : subIt->second) {
                            if (subId != 0) {
                                m_renderGraph->unsubscribe(subId);
                            }
                        }
                        m_extensionSubscribeIds.erase(subIt);
                    }
                } else {
                    m_extensionSubscribeIds.erase(name);
                }

                auto* renderHost = Host::RenderHost::InstancePtr();
                if (FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr) {
                    for (const auto& syncNode : it->syncNodes) {
                        if (!syncNode.name.empty()) {
                            ctx->syncTickers()[syncNode.name] = {[]() {}, {}, {}};
                        }
                    }
                    if (!it->syncNodes.empty()) {
                        (void)ctx->syncTickers().update();
                    }
                }

                m_extensions.erase(it);
            }

            void RendererSubsystem::requestAddCustomPass(const std::string& name, CustomPassCallback callback)
            {
                for (auto& item : m_customPasses) {
                    if (item.name == name) {
                        item.callback = std::move(callback);
                        return;
                    }
                }
                m_customPasses.push_back({name, std::move(callback)});
            }

            void RendererSubsystem::requestKeepImage(KeepImageCallback callback)
            {
                if (m_isGraphCompiled && m_renderGraph && callback) {
                    callback(m_renderGraph);
                }
                m_keepImageCallbacks.push_back(std::move(callback));
            }

            void RendererSubsystem::requestSubscribePass(const std::string& passName, PassSubmitCallback callback)
            {
                if (m_isGraphCompiled && m_renderGraph && callback) {
                    m_renderGraph->subscribe(passName, callback);
                }
                m_passSubscriptions.push_back({passName, std::move(callback)});
            }

            void RendererSubsystem::requestSyncTicker(
                const std::string& name,
                RenderSyncCallback callback,
                std::vector<std::string> predecessors,
                std::vector<std::string> successors)
            {
                RenderSyncNodeDesc node{
                    .name = name,
                    .callback = std::move(callback),
                    .predecessors = std::move(predecessors),
                    .successors = std::move(successors)
                };
                auto it = std::find_if(
                    m_syncNodes.begin(),
                    m_syncNodes.end(),
                    [&name](const RenderSyncNodeDesc& item) { return item.name == name; });
                if (it != m_syncNodes.end()) {
                    *it = node;
                } else {
                    m_syncNodes.push_back(node);
                }

                if (m_isGraphCompiled) {
                    auto* renderHost = Host::RenderHost::InstancePtr();
                    if (FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr) {
                        ctx->syncTickers()[node.name] = {node.callback, node.predecessors, node.successors};
                        (void)ctx->syncTickers().update();
                    }
                }
            }

            void RendererSubsystem::unregisterSyncTicker(const std::string& name)
            {
                m_syncNodes.erase(
                    std::remove_if(
                        m_syncNodes.begin(),
                        m_syncNodes.end(),
                        [&name](const RenderSyncNodeDesc& item) { return item.name == name; }),
                    m_syncNodes.end());

                auto* renderHost = Host::RenderHost::InstancePtr();
                if (FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr) {
                    ctx->syncTickers()[name] = {[]() {}, {}, {}};
                    (void)ctx->syncTickers().update();
                }
            }

            Status RendererSubsystem::loadRenderGraphPreset(std::string_view presetName)
            {
                if (presetName.empty()) {
                    return InvalidArgumentError("帧图预设名称不能为空");
                }

                const std::string candidatePath = presetName.ends_with(".pgraph")
                    ? std::string(presetName)
                    : StrCat("Assets/RenderGraphs/", presetName, ".pgraph");
                if (auto fileAssetOr = RenderGraphAsset::LoadFromFile(candidatePath); fileAssetOr.ok()) {
                    return setRenderGraphAsset(std::move(*fileAssetOr));
                }

                if (presetName == "Desktop_DeferredHDR") {
                    return setRenderGraphAsset(RenderGraphAsset::CreateDesktopDeferredHdrPreset());
                } else if (presetName == "Desktop_HybridRayTracing") {
                    return setRenderGraphAsset(RenderGraphAsset::CreateDesktopHybridRayTracingPreset());
                } else if (presetName == "MiniGame_ForwardPlus") {
                    return setRenderGraphAsset(RenderGraphAsset::CreateMiniGameForwardPlusPreset());
                } else if (presetName == "MiniGame_ForwardLite") {
                    return setRenderGraphAsset(RenderGraphAsset::CreateMiniGameForwardLitePreset());
                }
                return NotFoundError(StrCat("未知帧图预设: ", presetName));
            }

            Status RendererSubsystem::setRenderGraphAsset(RenderGraphAssetDesc desc)
            {
                if (!desc.isAcyclic()) {
                    return InvalidArgumentError("传入的 RenderGraphAsset 包含环路依赖");
                }
                m_activeGraphAsset = std::move(desc);
                m_useDataDrivenGraph = true;
                m_isGraphBuilt = false;
                m_isGraphCompiled = false;
                if (m_renderGraph) {
                    m_renderGraph->clearOriginalPasses();
                }
                return compileAndBindGraph();
            }

            void RendererSubsystem::submitDrawBucket(DrawBucket bucket)
            {
                m_frameDrawBuckets.push_back(std::move(bucket));
            }

            Status RendererSubsystem::compileAndBindGraph()
            {
                auto* renderHost = Host::RenderHost::InstancePtr();
                FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
                if (!m_renderGraph && ctx) {
                    m_renderGraph = ctx->getModule<FCT::RenderGraph>();
                }

                if (!m_renderGraph || !ctx) {
                    return OkStatus(); // 无头模式或无 RHI 环境正常通过
                }

                initGpuResources(ctx);
                initPassLayouts(ctx);
                registerRenderTickers(ctx);

                auto* uiHost = Host::UIHost::InstancePtr();
                FCT::Window* wnd = uiHost ? uiHost->getWindow() : nullptr;

                auto* engine = Runtime::Engine::getInstance();
                Runtime::Application* application = engine ? engine->application : nullptr;
                RenderTarget targetType = application
                    ? application->applicationRegisterInfo.platformRequire.target
                    : RenderTarget::Window;

                FCT::Image* extColorTarget = m_externalColorTarget.load(std::memory_order_acquire);
                FCT::Image* extDepthTarget = m_externalDepthStencilTarget.load(std::memory_order_acquire);

                if (m_useDataDrivenGraph) {
                    if (!m_isGraphBuilt) {
                        CHECK_STATUS(RenderGraphAsset::ApplyToFctRenderGraph(
                            m_activeGraphAsset,
                            m_renderGraph,
                            wnd,
                            extColorTarget,
                            extDepthTarget));
                        m_isGraphBuilt = true;
                    }
                } else if (!m_isGraphBuilt) {
                    // 1. 阴影贴图通道 (ShadowMapPass)
                    m_renderGraph->addPass(
                        "ShadowMapPass",
                        FCT::EnablePassClear(FCT::ClearType::depth | FCT::ClearType::stencil, 1.0f, static_cast<uint8_t>(0)),
                        FCT::DepthStencil("DepthFromLigth0Image", FCT::Format::D32_SFLOAT, 2048u, 2048u));

                    // 2. 几何/不透明物体通道 (ObjectPass，离屏默认采用 1280x720 16:9 高清目标)
                    FCT::DepthStencil sceneDs("SceneDepthTarget", FCT::Format::D32_SFLOAT, 1280u, 720u);
                    if (targetType == RenderTarget::Window && wnd) {
                        sceneDs = FCT::DepthStencil("SceneDepthTarget", wnd, FCT::Format::D32_SFLOAT);
                    } else if (extDepthTarget) {
                        sceneDs = FCT::DepthStencil("SceneDepthTarget", extDepthTarget);
                    }

                    m_renderGraph->addPass(
                        "ObjectPass",
                        FCT::EnablePassClear(
                            FCT::ClearType::color | FCT::ClearType::depth | FCT::ClearType::stencil,
                            FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f),
                            1.0f,
                            static_cast<uint8_t>(0)),
                        FCT::Texture("DepthFromLigth0Image", FCT::ShaderStage::Fragment),
                        FCT::Target("SceneHDRColor", FCT::Format::R16G16B16A16_SFLOAT),
                        FCT::Target("PosTarget", FCT::Format::R16G16B16A16_SFLOAT),
                        FCT::Target("RetTarget", FCT::Format::R8G8B8A8_UNORM),
                        sceneDs);

                    // 3. ACES 色调映射与大气天空合成通道 (ToneMappingPass -> ToneMappedColor，线性 RGB + 感知亮度 Alpha)
                    m_renderGraph->addPass(
                        "ToneMappingPass",
                        FCT::EnablePassClear(FCT::ClearType::color, FCT::Vec4(0.04f, 0.06f, 0.10f, 1.0f)),
                        FCT::Texture("SceneHDRColor", FCT::ShaderStage::Fragment),
                        FCT::Target("ToneMappedColor", FCT::Format::R16G16B16A16_SFLOAT));

                    // 4. FXAA 3.11 屏幕空间后处理抗锯齿通道 (AntiAliasingPass -> SceneColorTarget)
                    FCT::Target sceneColorTarget("SceneColorTarget", FCT::Format::R8G8B8A8_SRGB, 1280u, 720u);
                    if (targetType == RenderTarget::Window && wnd) {
                        sceneColorTarget = FCT::Target("SceneColorTarget", wnd);
                    } else if (extColorTarget) {
                        sceneColorTarget = FCT::Target("SceneColorTarget", extColorTarget);
                    }

                    m_renderGraph->addPass(
                        "AntiAliasingPass",
                        FCT::EnablePassClear(FCT::ClearType::color, FCT::Vec4(0.04f, 0.06f, 0.10f, 1.0f)),
                        FCT::Texture("ToneMappedColor", FCT::ShaderStage::Fragment),
                        sceneColorTarget);

                    m_isGraphBuilt = true;
                }

                // 执行动态注册的自定义 Pass 回调与扩展 Pass 声明
                for (const auto& cp : m_customPasses) {
                    if (cp.callback) {
                        cp.callback(m_renderGraph);
                    }
                }
                for (const auto& ext : m_extensions) {
                    if (ext.onSetupPass) {
                        ext.onSetupPass(m_renderGraph);
                    }
                }

                if (!m_isGraphCompiled) {
                    m_renderGraph->compile();
                    m_isGraphCompiled = true;

                    // RenderGraph::compile() 触发 passPipe 后，为各 Layout 编译并缓存 Shader
                    cachePassShaders();

                    // 注册 ShadowMapPass、ObjectPass、ToneMappingPass 与 AntiAliasingPass 真实 GPU 绘制提交回调
                    m_renderGraph->subscribe("ShadowMapPass", [this](FCT::PassSubmitEvent env) {
                        recordShadowMapPass(env);
                    });
                    m_renderGraph->subscribe("ObjectPass", [this](FCT::PassSubmitEvent env) {
                        recordObjectPass(env);
                    });
                    m_renderGraph->subscribe("ToneMappingPass", [this](FCT::PassSubmitEvent env) {
                        recordToneMappingPass(env);
                    });
                    m_renderGraph->subscribe("AntiAliasingPass", [this](FCT::PassSubmitEvent env) {
                        recordAntiAliasingPass(env);
                    });

                    // 执行图像保留与编译后回调
                    for (const auto& cb : m_keepImageCallbacks) {
                        if (cb) {
                            cb(m_renderGraph);
                        }
                    }
                    for (const auto& ext : m_extensions) {
                        if (ext.onPostCompile) {
                            ext.onPostCompile(m_renderGraph);
                        }
                    }

                    // 注册跨线程 SyncTicker 栅栏节点
                    bool syncUpdated = false;
                    for (const auto& syncNode : m_syncNodes) {
                        if (!syncNode.name.empty() && syncNode.callback) {
                            ctx->syncTickers()[syncNode.name] = {
                                syncNode.callback,
                                syncNode.predecessors,
                                syncNode.successors
                            };
                            syncUpdated = true;
                        }
                    }
                    for (const auto& ext : m_extensions) {
                        for (const auto& syncNode : ext.syncNodes) {
                            if (!syncNode.name.empty() && syncNode.callback) {
                                ctx->syncTickers()[syncNode.name] = {
                                    syncNode.callback,
                                    syncNode.predecessors,
                                    syncNode.successors
                                };
                                syncUpdated = true;
                            }
                        }
                    }
                    if (syncUpdated) {
                        (void)ctx->syncTickers().update();
                    }

                    // 注册自定义 Pass 命令录制订阅
                    for (const auto& sub : m_passSubscriptions) {
                        if (sub.callback) {
                            m_renderGraph->subscribe(sub.passName, sub.callback);
                        }
                    }
                    m_extensionSubscribeIds.clear();
                    for (const auto& ext : m_extensions) {
                        auto& subIds = m_extensionSubscribeIds[ext.name];
                        for (const auto& sub : ext.passSubscribers) {
                            if (!sub.passName.empty() && sub.callback) {
                                subIds.push_back(m_renderGraph->subscribe(sub.passName, sub.callback));
                            }
                        }
                    }
                }

                return OkStatus();
            }

            Status RendererSubsystem::updateCustomPixelShader(const std::string& hlslBody)
            {
                if (hlslBody.empty()) {
                    return InvalidArgumentError("HLSL 像素着色器源码不能为空");
                }

                auto* renderHost = Host::RenderHost::InstancePtr();
                FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
                if (ctx && ctx->getGenerator() && ctx->getCompiler()) {
                    std::vector<FCT::ConstLayout> uniformLayouts;
                    FCT::RHI::ShaderBinary binaryCode;
                    FCT::ResourceLayout resourceLayout;
                    resourceLayout.addTexture(FCT::TextureElement(
                        FCT::TextureType::Texture2D,
                        "DepthFromLigth0Image",
                        FCT::ShaderStage::Fragment,
                        FCT::UpdateFrequency::PerFrame));

                    std::string fullSource = ctx->getGenerator()->generatePixelShader(
                        StandardPixelLayout,
                        uniformLayouts,
                        binaryCode,
                        resourceLayout,
                        hlslBody);

                    std::vector<char> spv = ctx->getCompiler()->compile(
                        fullSource,
                        "FCTEntry",
                        FCT::ShaderKind::FragmentShader);
                    if (spv.empty()) {
                        return InvalidArgumentError("自定义像素着色器 HLSL 编译为 SPIR-V 失败");
                    }
                }

                m_logicCustomObjectPsHlsl = hlslBody;
                m_logicCustomObjectPsDirty.store(true, std::memory_order_release);
                return OkStatus();
            }

            void RendererSubsystem::setExternalTargets(FCT::Image* colorTarget, FCT::Image* depthStencilTarget)
            {
                m_externalColorTarget.store(colorTarget, std::memory_order_release);
                m_externalDepthStencilTarget.store(depthStencilTarget, std::memory_order_release);
            }

            uint64_t RendererSubsystem::requestFrameCapture(const std::string& outputPath, const std::string& targetName)
            {
                if (outputPath.empty()) {
                    return 0;
                }

                const uint64_t reqId = m_nextCaptureId.fetch_add(1, std::memory_order_relaxed);
                m_latestCaptureId.store(reqId, std::memory_order_release);
                m_pendingCaptures.push_back(PendingFrameCapture{
                    .requestId = reqId,
                    .outputPath = outputPath,
                    .targetName = targetName.empty() ? "SceneColorTarget" : targetName
                });

                // 若 RenderHost 处于运行态，触发当前帧刷新并等待写盘完成
                auto* renderHost = Host::RenderHost::InstancePtr();
                if (renderHost && renderHost->getContext()) {
                    renderHost->flushLastFrame();
                    (void)waitForPendingCaptures(5000);
                }

                return reqId;
            }

            void RendererSubsystem::scheduleFrameCapture(const std::string& outputPath, const std::string& targetName, uint64_t captureAtFrame)
            {
                if (outputPath.empty()) {
                    return;
                }
                m_cliScreenshotPath = outputPath;
                m_cliScreenshotTarget = targetName.empty() ? "mainWindowColor" : targetName;
                m_cliScreenshotFrame = static_cast<uint32_t>(
                    std::max<uint64_t>(m_submitFrameCounter.load(std::memory_order_acquire) + 2, captureAtFrame));
                m_cliScreenshotTriggered = false;
            }

            bool RendererSubsystem::hasPendingCaptures() const
            {
                return !m_pendingCaptures.empty() || (m_completedCaptureId.load(std::memory_order_acquire) < m_latestCaptureId.load(std::memory_order_acquire));
            }

            bool RendererSubsystem::waitForPendingCaptures(uint32_t timeoutMs)
            {
                const uint64_t targetId = m_latestCaptureId.load(std::memory_order_acquire);
                if (targetId == 0 || m_completedCaptureId.load(std::memory_order_acquire) >= targetId) {
                    return true;
                }
                auto start = std::chrono::steady_clock::now();
                while (m_completedCaptureId.load(std::memory_order_acquire) < targetId) {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > timeoutMs) {
                        return false;
                    }
                    std::this_thread::yield();
                }
                return true;
            }

            void RendererSubsystem::processPendingCaptures(FCT::Context* ctx, FCT::Window* wnd)
            {
                if (m_pendingCaptures.empty()) {
                    return;
                }
                std::vector<PendingFrameCapture> requests = std::move(m_pendingCaptures);
                m_pendingCaptures.clear();

                uint64_t highestCompletedId = 0;
                for (const auto& req : requests) {
                    highestCompletedId = std::max(highestCompletedId, req.requestId);
                    executeSingleTargetCapture(ctx, wnd, req.outputPath, req.targetName);
                }

                uint64_t curr = m_completedCaptureId.load(std::memory_order_relaxed);
                while (curr < highestCompletedId &&
                       !m_completedCaptureId.compare_exchange_weak(curr, highestCompletedId, std::memory_order_release, std::memory_order_relaxed)) {
                }
            }

            bool RendererSubsystem::executeSingleTargetCapture(
                FCT::Context* ctx,
                FCT::Window* wnd,
                const std::string& outputPath,
                const std::string& targetName)
            {
                auto* vkCtx = dynamic_cast<FCT::VK_Context*>(ctx);
                if (!vkCtx) {
                    return false;
                }

                FCT::Image* targetImg = nullptr;
                vk::ImageLayout currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                std::string resolvedName = targetName.empty() ? "SceneColorTarget" : targetName;

                if (resolvedName == "window" || resolvedName == "swapchain" || resolvedName == "mainWindowColor") {
                    if (m_renderGraph) {
                        targetImg = m_renderGraph->getImage("mainWindowColor");
                    }
                    if (!targetImg && wnd && wnd->getCurrentTarget()) {
                        targetImg = wnd->getCurrentTarget()->targetImage();
                    }
                    if (targetImg) {
                        resolvedName = "mainWindowColor";
                        currentLayout = vk::ImageLayout::ePresentSrcKHR;
                    } else if (m_renderGraph) {
                        targetImg = m_renderGraph->getImage("SceneColorTarget");
                        if (targetImg) {
                            resolvedName = "SceneColorTarget";
                            currentLayout = (targetImg->getType() == FCT::RenderTargetType::WindowTarget)
                                ? vk::ImageLayout::ePresentSrcKHR
                                : vk::ImageLayout::eColorAttachmentOptimal;
                        }
                    }
                } else {
                    if (m_renderGraph) {
                        targetImg = m_renderGraph->getImage(resolvedName);
                    }
                    if (!targetImg) {
                        targetImg = m_externalColorTarget.load(std::memory_order_acquire);
                    }
                    if (!targetImg && m_renderGraph && resolvedName != "SceneColorTarget") {
                        targetImg = m_renderGraph->getImage("SceneColorTarget");
                        if (targetImg) {
                            resolvedName = "SceneColorTarget";
                        }
                    }
                    if (!targetImg && wnd && wnd->getCurrentTarget()) {
                        targetImg = wnd->getCurrentTarget()->targetImage();
                        resolvedName = "mainWindowColor";
                    }
                    if (targetImg) {
                        const bool hasMainWindowComposite = (m_renderGraph && m_renderGraph->getImage("mainWindowColor") != nullptr);
                        if (targetImg->getType() == FCT::RenderTargetType::WindowTarget || resolvedName == "mainWindowColor") {
                            currentLayout = vk::ImageLayout::ePresentSrcKHR;
                        } else if (resolvedName == "SceneColorTarget" && !hasMainWindowComposite) {
                            currentLayout = vk::ImageLayout::eColorAttachmentOptimal;
                        } else {
                            currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        }
                    }
                }

                if (!targetImg) {
                    return false;
                }

                auto* rhiImg = dynamic_cast<FCT::RHI::VK_Image*>(targetImg->currentImage());
                if (!rhiImg || !rhiImg->getVkImage()) {
                    return false;
                }

                const uint32_t width = static_cast<uint32_t>(std::max(1, targetImg->width()));
                const uint32_t height = static_cast<uint32_t>(std::max(1, targetImg->height()));
                const FCT::Format fmt = targetImg->format();
                const bool isDepth = (fmt == FCT::Format::D32_SFLOAT ||
                                      fmt == FCT::Format::D16_UNORM ||
                                      fmt == FCT::Format::D24_UNORM_S8_UINT ||
                                      fmt == FCT::Format::D32_SFLOAT_S8_UINT);

                uint32_t bpp = 4;
                switch (fmt) {
                    case FCT::Format::R16G16B16A16_SFLOAT:
                        bpp = 8;
                        break;
                    case FCT::Format::R32G32B32A32_SFLOAT:
                    case FCT::Format::R32G32B32A32_UINT:
                        bpp = 16;
                        break;
                    default:
                        bpp = 4;
                        break;
                }

                vk::Device device = vkCtx->getDevice();
                if (!device) {
                    return false;
                }

                (void)vkCtx->getGraphicsQueue().waitIdle();

                const vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(width) * height * bpp;
                vk::BufferCreateInfo bufInfo{};
                bufInfo.setSize(bufferSize);
                bufInfo.setUsage(vk::BufferUsageFlagBits::eTransferDst);
                bufInfo.setSharingMode(vk::SharingMode::eExclusive);

                vk::Buffer stagingBuffer{};
                if (device.createBuffer(&bufInfo, nullptr, &stagingBuffer) != vk::Result::eSuccess) {
                    return false;
                }

                vk::MemoryRequirements memReq = device.getBufferMemoryRequirements(stagingBuffer);
                vk::MemoryAllocateInfo allocInfo{};
                allocInfo.setAllocationSize(memReq.size);
                allocInfo.setMemoryTypeIndex(vkCtx->findMemoryType(
                    memReq.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

                vk::DeviceMemory stagingMemory{};
                if (device.allocateMemory(&allocInfo, nullptr, &stagingMemory) != vk::Result::eSuccess) {
                    device.destroyBuffer(stagingBuffer);
                    return false;
                }
                device.bindBufferMemory(stagingBuffer, stagingMemory, 0);

                vk::ImageAspectFlags aspectMask = isDepth
                    ? vk::ImageAspectFlagBits::eDepth
                    : vk::ImageAspectFlagBits::eColor;

                vk::CommandBuffer cmd = vkCtx->beginSingleTimeCommands();

                vk::ImageMemoryBarrier toTransferSrc{};
                toTransferSrc.setOldLayout(currentLayout);
                toTransferSrc.setNewLayout(vk::ImageLayout::eTransferSrcOptimal);
                toTransferSrc.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
                toTransferSrc.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
                toTransferSrc.setImage(rhiImg->getVkImage());
                toTransferSrc.subresourceRange.setAspectMask(aspectMask);
                toTransferSrc.subresourceRange.setBaseMipLevel(0);
                toTransferSrc.subresourceRange.setLevelCount(1);
                toTransferSrc.subresourceRange.setBaseArrayLayer(0);
                toTransferSrc.subresourceRange.setLayerCount(1);
                toTransferSrc.setSrcAccessMask(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eColorAttachmentWrite);
                toTransferSrc.setDstAccessMask(vk::AccessFlagBits::eTransferRead);

                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::DependencyFlags{},
                    0, nullptr,
                    0, nullptr,
                    1, &toTransferSrc);

                vk::BufferImageCopy copyRegion{};
                copyRegion.setBufferOffset(0);
                copyRegion.setBufferRowLength(0);
                copyRegion.setBufferImageHeight(0);
                copyRegion.imageSubresource.setAspectMask(aspectMask);
                copyRegion.imageSubresource.setMipLevel(0);
                copyRegion.imageSubresource.setBaseArrayLayer(0);
                copyRegion.imageSubresource.setLayerCount(1);
                copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
                copyRegion.imageExtent = vk::Extent3D{width, height, 1};

                cmd.copyImageToBuffer(rhiImg->getVkImage(), vk::ImageLayout::eTransferSrcOptimal, stagingBuffer, 1, &copyRegion);

                vk::ImageMemoryBarrier restoreBarrier{};
                restoreBarrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal);
                restoreBarrier.setNewLayout(currentLayout);
                restoreBarrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
                restoreBarrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
                restoreBarrier.setImage(rhiImg->getVkImage());
                restoreBarrier.subresourceRange = toTransferSrc.subresourceRange;
                restoreBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead);
                restoreBarrier.setDstAccessMask(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eShaderRead);

                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::PipelineStageFlagBits::eAllCommands,
                    vk::DependencyFlags{},
                    0, nullptr,
                    0, nullptr,
                    1, &restoreBarrier);

                vkCtx->endSingleTimeCommands(cmd);

                std::vector<uint8_t> rawBytes;
                void* mapped = nullptr;
                if (device.mapMemory(stagingMemory, 0, bufferSize, vk::MemoryMapFlags{}, &mapped) == vk::Result::eSuccess && mapped) {
                    rawBytes.resize(static_cast<size_t>(bufferSize));
                    std::memcpy(rawBytes.data(), mapped, static_cast<size_t>(bufferSize));
                    device.unmapMemory(stagingMemory);
                }
                device.destroyBuffer(stagingBuffer);
                device.freeMemory(stagingMemory);

                if (rawBytes.empty()) {
                    return false;
                }

                // 解码为 RGBA8 像素缓冲
                const size_t pixelCount = static_cast<size_t>(width) * height;
                std::vector<uint8_t> rgbaPixels(pixelCount * 4, 255);
                uint8_t minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0;
                for (size_t i = 0; i < pixelCount; ++i) {
                    const size_t srcOff = i * bpp;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    if (srcOff + bpp <= rawBytes.size()) {
                        switch (fmt) {
                            case FCT::Format::B8G8R8A8_UNORM:
                            case FCT::Format::B8G8R8A8_SRGB:
                                b = rawBytes[srcOff + 0];
                                g = rawBytes[srcOff + 1];
                                r = rawBytes[srcOff + 2];
                                a = rawBytes[srcOff + 3];
                                break;
                            default:
                                r = rawBytes[srcOff + 0];
                                g = (bpp > 1) ? rawBytes[srcOff + 1] : r;
                                b = (bpp > 2) ? rawBytes[srcOff + 2] : r;
                                a = (bpp > 3) ? rawBytes[srcOff + 3] : 255;
                                break;
                        }
                    }
                    minR = std::min(minR, r); maxR = std::max(maxR, r);
                    minG = std::min(minG, g); maxG = std::max(maxG, g);
                    minB = std::min(minB, b); maxB = std::max(maxB, b);
                    rgbaPixels[i * 4 + 0] = r;
                    rgbaPixels[i * 4 + 1] = g;
                    rgbaPixels[i * 4 + 2] = b;
                    rgbaPixels[i * 4 + 3] = a;
                }

                spdlog::info(
                    "[Renderer] Capture '{}'(resolved='{}') {}x{} fmt={} | drawItems={} visible={}/{} objPass={}x{} | RGB min=({},{},{}) max=({},{},{})",
                    targetName, resolvedName, width, height, static_cast<int>(fmt),
                    m_renderDrawItems.size(), m_frameStats.visibleEntities, m_frameStats.totalSceneEntities,
                    m_objectPassOutputInfo.width, m_objectPassOutputInfo.height,
                    minR, minG, minB, maxR, maxG, maxB);

                // 内存编码为图片字节
                std::string encodedFileBuffer;
                auto writeFunc = [](void* context, void* data, int size) {
                    auto* str = static_cast<std::string*>(context);
                    str->append(static_cast<const char*>(data), static_cast<size_t>(size));
                };

                std::string ext;
                const auto lastDot = outputPath.find_last_of('.');
                if (lastDot != std::string::npos) {
                    ext = outputPath.substr(lastDot);
                }

                if (ext == ".bmp") {
                    stbi_write_bmp_to_func(writeFunc, &encodedFileBuffer, static_cast<int>(width), static_cast<int>(height), 4, rgbaPixels.data());
                } else if (ext == ".tga") {
                    stbi_write_tga_to_func(writeFunc, &encodedFileBuffer, static_cast<int>(width), static_cast<int>(height), 4, rgbaPixels.data());
                } else {
                    stbi_write_png_to_func(writeFunc, &encodedFileBuffer, static_cast<int>(width), static_cast<int>(height), 4, rgbaPixels.data(), static_cast<int>(width * 4));
                }

                if (encodedFileBuffer.empty()) {
                    return false;
                }

                // 统一通过 ResourceLoader 进行磁盘 I/O 写入
                auto* resMod = Resource::Resource::InstancePtr();
                auto* resourceLoader = resMod ? resMod->getResourceLoader() : nullptr;
                if (!resourceLoader) {
                    return false;
                }
                Status status = resourceLoader->saveFile(outputPath, encodedFileBuffer);
                return status.ok();
            }

            FCT::ResourceGroup* RendererSubsystem::getOrCreateResourceGroup(const std::string& groupName)
            {
                auto it = m_resourceGroups.find(groupName);
                if (it != m_resourceGroups.end()) {
                    return it->second.get();
                }
                auto group = makeUnique<FCT::ResourceGroup>(groupName);
                auto* ptr = group.get();
                m_resourceGroups.emplace(groupName, std::move(group));
                return ptr;
            }

            FCT::Image* RendererSubsystem::requireGroupResource(const std::string& groupName, const FCT::ResourceRequireDesc& desc)
            {
                auto* group = getOrCreateResourceGroup(groupName);
                if (!group) {
                    return nullptr;
                }
                auto* renderHost = Host::RenderHost::InstancePtr();
                FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
                return group->require(ctx, desc);
            }

            void RendererSubsystem::releaseGroupResource(const std::string& groupName, const std::string& resourceId)
            {
                auto it = m_resourceGroups.find(groupName);
                if (it != m_resourceGroups.end() && it->second) {
                    auto* renderHost = Host::RenderHost::InstancePtr();
                    FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
                    it->second->release(resourceId, ctx);
                }
            }

            std::vector<FCT::Image*> RendererSubsystem::getActiveGroupResources(const std::string& groupName) const
            {
                auto it = m_resourceGroups.find(groupName);
                if (it != m_resourceGroups.end() && it->second) {
                    return it->second->getActiveImages();
                }
                return {};
            }

            void RendererSubsystem::ensureSampleModelAssetsOnDisk()
            {
                auto* resMod = Resource::Resource::InstancePtr();
                auto* resourceLoader = resMod ? resMod->getResourceLoader() : nullptr;
                if (!resourceLoader) {
                    return;
                }

                const std::string sampleModelPath = "Assets/Models/SampleCube.obj";
                if (!resourceLoader->hasFile(sampleModelPath)) {
                    std::string sampleObj =
                        "# Sample Wavefront OBJ Cube\n"
                        "v -0.5 -0.5  0.5\n"
                        "v  0.5 -0.5  0.5\n"
                        "v -0.5  0.5  0.5\n"
                        "v  0.5  0.5  0.5\n"
                        "v -0.5  0.5 -0.5\n"
                        "v  0.5  0.5 -0.5\n"
                        "v -0.5 -0.5 -0.5\n"
                        "v  0.5 -0.5 -0.5\n"
                        "vn  0.0  0.0  1.0\n"
                        "vn  0.0  1.0  0.0\n"
                        "vn  0.0  0.0 -1.0\n"
                        "vn  0.0 -1.0  0.0\n"
                        "vn  1.0  0.0  0.0\n"
                        "vn -1.0  0.0  0.0\n"
                        "vt 0.0 0.0\n"
                        "vt 1.0 0.0\n"
                        "vt 1.0 1.0\n"
                        "vt 0.0 1.0\n"
                        "f 1/1/1 2/2/1 4/3/1 3/4/1\n"
                        "f 3/1/2 4/2/2 6/3/2 5/4/2\n"
                        "f 5/1/3 6/2/3 8/3/3 7/4/3\n"
                        "f 7/1/4 8/2/4 2/3/4 1/4/4\n"
                        "f 2/1/5 8/2/5 6/3/5 4/4/5\n"
                        "f 7/1/6 1/2/6 3/3/6 5/4/6\n";
                    (void)resourceLoader->saveFile(sampleModelPath, sampleObj);
                }
            }

            FCT::StaticMesh<uint32_t>* RendererSubsystem::loadOrGetModelMesh(
                const std::string& assetPath,
                LoadedModelMeshInfo* outInfo)
            {
                ZoneScoped();
                if (assetPath.empty()) {
                    return nullptr;
                }

                auto it = m_loadedModelMeshes.find(assetPath);
                if (it != m_loadedModelMeshes.end() && it->second.mesh) {
                    if (outInfo) {
                        *outInfo = it->second;
                    }
                    return it->second.mesh;
                }

                auto* renderHost = Host::RenderHost::InstancePtr();
                FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
                if (!ctx) {
                    return nullptr;
                }

                // Note: 使用 Assimp_ModelLoader 解析模型数据并构建顶点/索引缓冲区
                FCT::Assimp_ModelLoader loader;
                auto modelData = loader.loadModel(assetPath);
                if (!modelData || modelData->meshes.empty()) {
                    return nullptr;
                }

                auto* staticMesh = FCT_NEW(FCT::StaticMesh<uint32_t>, ctx, StandardMeshVertexLayout);
                FCT::Vec3<> aabbMin(1e9f, 1e9f, 1e9f);
                FCT::Vec3<> aabbMax(-1e9f, -1e9f, -1e9f);
                uint32_t totalVertices = 0;
                uint32_t totalIndices = 0;
                std::vector<uint32_t> allIndices;

                for (auto* subMesh : modelData->meshes) {
                    if (!subMesh) {
                        continue;
                    }
                    // 若模型未提供有效顶点法线，则基于共享顶点索引计算面积加权平滑顶点法线
                    bool hasValidNormals = false;
                    for (const auto& v : subMesh->vertices) {
                        if (v.normal.lengthSquared() > 1e-6f) {
                            hasValidNormals = true;
                            break;
                        }
                    }
                    if (!hasValidNormals && !subMesh->vertices.empty() && subMesh->indices.size() >= 3) {
                        std::vector<FCT::Vec3<>> smoothNormals(subMesh->vertices.size(), FCT::Vec3<>(0.0f, 0.0f, 0.0f));
                        for (size_t i = 0; i + 2 < subMesh->indices.size(); i += 3) {
                            const uint32_t i0 = subMesh->indices[i];
                            const uint32_t i1 = subMesh->indices[i + 1];
                            const uint32_t i2 = subMesh->indices[i + 2];
                            if (i0 < subMesh->vertices.size() && i1 < subMesh->vertices.size() && i2 < subMesh->vertices.size()) {
                                const FCT::Vec3<> e1 = subMesh->vertices[i1].position - subMesh->vertices[i0].position;
                                const FCT::Vec3<> e2 = subMesh->vertices[i2].position - subMesh->vertices[i0].position;
                                const FCT::Vec3<> faceWeightedNormal = e1.cross(e2);
                                smoothNormals[i0] = smoothNormals[i0] + faceWeightedNormal;
                                smoothNormals[i1] = smoothNormals[i1] + faceWeightedNormal;
                                smoothNormals[i2] = smoothNormals[i2] + faceWeightedNormal;
                            }
                        }
                        for (size_t vi = 0; vi < subMesh->vertices.size(); ++vi) {
                            if (smoothNormals[vi].lengthSquared() > 1e-8f) {
                                subMesh->vertices[vi].normal = smoothNormals[vi].normalize();
                            } else {
                                subMesh->vertices[vi].normal = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
                            }
                        }
                    }

                    const uint32_t baseVertex = totalVertices;
                    for (const auto& v : subMesh->vertices) {
                        aabbMin.x = std::min(aabbMin.x, v.position.x);
                        aabbMin.y = std::min(aabbMin.y, v.position.y);
                        aabbMin.z = std::min(aabbMin.z, v.position.z);
                        aabbMax.x = std::max(aabbMax.x, v.position.x);
                        aabbMax.y = std::max(aabbMax.y, v.position.y);
                        aabbMax.z = std::max(aabbMax.z, v.position.z);

                        const FCT::Vec4 vCol = (v.colors[0].x == 0.0f && v.colors[0].y == 0.0f && v.colors[0].z == 0.0f && v.colors[0].w == 0.0f)
                            ? FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f)
                            : v.colors[0];

                        staticMesh->addVertex(
                            vCol,
                            FCT::Vec4(v.position.x, v.position.y, v.position.z, 1.0f),
                            v.texCoords[0],
                            v.normal,
                            v.tangent,
                            v.bitangent,
                            FCT::IVec4(v.boneIds[0], v.boneIds[1], v.boneIds[2], v.boneIds[3]),
                            FCT::Vec4(v.boneWeights[0], v.boneWeights[1], v.boneWeights[2], v.boneWeights[3]));
                        totalVertices++;
                    }
                    for (uint32_t idx : subMesh->indices) {
                        allIndices.push_back(baseVertex + idx);
                        totalIndices++;
                    }
                }

                if (totalVertices == 0) {
                    FCT_DELETE(staticMesh);
                    return nullptr;
                }

                staticMesh->setIndices(allIndices);
                staticMesh->create();

                if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr()) {
                    gpuTable->registerResource(
                        staticMesh,
                        Resource::GpuResourceType::Buffer,
                        totalVertices * 96 + totalIndices * sizeof(uint32_t),
                        assetPath);
                }

                LoadedModelMeshInfo info;
                info.assetPath = assetPath;
                info.mesh = staticMesh;
                info.vertexCount = totalVertices;
                info.indexCount = totalIndices;
                info.triangleCount = totalIndices / 3;
                info.localAabbMin = aabbMin;
                info.localAabbMax = aabbMax;

                m_loadedModelMeshes[assetPath] = info;
                m_cachedModelCount.store(static_cast<uint32_t>(m_loadedModelMeshes.size()), std::memory_order_release);
                if (outInfo) {
                    *outInfo = info;
                }
                return staticMesh;
            }

            bool RendererSubsystem::getLoadedModelMeshInfo(const std::string& assetPath, LoadedModelMeshInfo& outInfo) const
            {
                auto it = m_loadedModelMeshes.find(assetPath);
                if (it != m_loadedModelMeshes.end()) {
                    outInfo = it->second;
                    return true;
                }
                return false;
            }

            std::vector<LoadedModelMeshInfo> RendererSubsystem::getAllLoadedModelMeshes() const
            {
                std::vector<LoadedModelMeshInfo> result;
                result.reserve(m_loadedModelMeshes.size());
                for (const auto& kv : m_loadedModelMeshes) {
                    result.push_back(kv.second);
                }
                return result;
            }

            RenderFrameStats RendererSubsystem::getFrameStats() const noexcept
            {
                const uint32_t slot = m_publishedFrameStatsSlot.load(std::memory_order_acquire);
                RenderFrameStats stats = m_frameStatsSnapshots[slot];
                stats.cachedModelCount = m_cachedModelCount.load(std::memory_order_acquire);
                return stats;
            }

            void RendererSubsystem::initDefaultPipelineObjects()
            {
                std::vector<PipelineObjectDesc> customItems;
                for (const auto& existing : m_logicPipelineObjects) {
                    if (!existing.isBuiltin) {
                        customItems.push_back(existing);
                    }
                }

                m_logicPipelineObjects.clear();
                m_logicPipelineObjects.reserve(4 + customItems.size());

                PipelineObjectDesc shadowPso{};
                shadowPso.name = "PSO_ShadowMap";
                shadowPso.passName = "ShadowMapPass";
                shadowPso.isBuiltin = true;
                shadowPso.hasColorAttachment = false;
                shadowPso.hasVertexShader = true;
                shadowPso.hasPixelShader = false;
                shadowPso.vertexLayoutSummary = "StandardMeshVertexLayout (96B)";
                shadowPso.pixelLayoutSummary = "DepthOnly (D32_SFLOAT)";
                shadowPso.polygonMode = FCT::PolygonMode::Fill;
                shadowPso.cullMode = FCT::CullMode::None;
                shadowPso.frontFace = FCT::FrontFace::CounterClockwise;
                shadowPso.depthTestEnable = true;
                shadowPso.depthWriteEnable = true;
                shadowPso.depthCompareOp = FCT::CompareOp::LessOrEqual;
                m_logicPipelineObjects.push_back(shadowPso);

                PipelineObjectDesc objectPso{};
                objectPso.name = "PSO_StandardPBR";
                objectPso.passName = "ObjectPass";
                objectPso.isBuiltin = true;
                objectPso.hasColorAttachment = true;
                objectPso.hasVertexShader = true;
                objectPso.hasPixelShader = true;
                objectPso.vertexLayoutSummary = "StandardMeshVertexLayout (96B)";
                objectPso.pixelLayoutSummary = "StandardPixelLayout -> RGBA16F";
                objectPso.polygonMode = FCT::PolygonMode::Fill;
                objectPso.cullMode = FCT::CullMode::None;
                objectPso.frontFace = FCT::FrontFace::CounterClockwise;
                objectPso.depthTestEnable = true;
                objectPso.depthWriteEnable = true;
                objectPso.depthCompareOp = FCT::CompareOp::LessOrEqual;
                objectPso.blendEnable = false;
                m_logicPipelineObjects.push_back(objectPso);

                PipelineObjectDesc toneMapPso{};
                toneMapPso.name = "PSO_ToneMapping";
                toneMapPso.passName = "ToneMappingPass";
                toneMapPso.isBuiltin = true;
                toneMapPso.hasColorAttachment = true;
                toneMapPso.hasVertexShader = true;
                toneMapPso.hasPixelShader = true;
                toneMapPso.vertexLayoutSummary = "FullScreenQuad (96B)";
                toneMapPso.pixelLayoutSummary = "ACES Fitted -> RGBA8";
                toneMapPso.polygonMode = FCT::PolygonMode::Fill;
                toneMapPso.cullMode = FCT::CullMode::None;
                toneMapPso.frontFace = FCT::FrontFace::CounterClockwise;
                toneMapPso.depthTestEnable = false;
                toneMapPso.depthWriteEnable = false;
                toneMapPso.depthCompareOp = FCT::CompareOp::Always;
                toneMapPso.blendEnable = false;
                m_logicPipelineObjects.push_back(toneMapPso);

                PipelineObjectDesc fxaaPso{};
                fxaaPso.name = "PSO_FXAA";
                fxaaPso.passName = "AntiAliasingPass";
                fxaaPso.isBuiltin = true;
                fxaaPso.hasColorAttachment = true;
                fxaaPso.hasVertexShader = true;
                fxaaPso.hasPixelShader = true;
                fxaaPso.vertexLayoutSummary = "FullScreenQuad (96B)";
                fxaaPso.pixelLayoutSummary = "FXAA 3.11 -> RGBA8";
                fxaaPso.polygonMode = FCT::PolygonMode::Fill;
                fxaaPso.cullMode = FCT::CullMode::None;
                fxaaPso.frontFace = FCT::FrontFace::CounterClockwise;
                fxaaPso.depthTestEnable = false;
                fxaaPso.depthWriteEnable = false;
                fxaaPso.depthCompareOp = FCT::CompareOp::Always;
                fxaaPso.blendEnable = false;
                m_logicPipelineObjects.push_back(fxaaPso);

                for (auto& custom : customItems) {
                    m_logicPipelineObjects.push_back(std::move(custom));
                }
                publishPipelineObjectSnapshot();
                if (!m_hasSyncedAtLeastOnce.load(std::memory_order_acquire)) {
                    m_submitPipelineObjectsPending = m_logicPipelineObjects;
                    m_submitPipelineObjectsDirty = true;
                }
                m_logicPipelineObjectsDirty.store(true, std::memory_order_release);
            }

            RendererSubsystem::PassRuntimeStates RendererSubsystem::buildRuntimeStatesForDesc(
                FCT::Context* ctx,
                const PipelineObjectDesc& desc)
            {
                PassRuntimeStates states{};
                if (!ctx) {
                    return states;
                }

                auto* rs = ctx->createResource<FCT::RasterizationState>();
                if (rs) {
                    rs->polygonMode(desc.polygonMode);
                    rs->cullMode(desc.cullMode);
                    rs->frontFace(desc.frontFace);
                    rs->lineWidth(std::clamp(desc.lineWidth, 0.5f, 8.0f));
                    rs->depthClampEnable(desc.depthClampEnable);
                    rs->depthBiasEnable(desc.depthBiasEnable);
                    rs->depthBiasConstantFactor(desc.depthBiasConstant);
                    rs->depthBiasClamp(desc.depthBiasClamp);
                    rs->depthBiasSlopeFactor(desc.depthBiasSlope);
                    rs->multisampleEnable(false);
                    rs->sampleCount(1u);
                    rs->create();
                    m_ownedPipelineStateResources.push_back(rs);
                    states.rasterState = rs;
                }

                auto* ds = ctx->createResource<FCT::DepthStencilState>();
                if (ds) {
                    ds->depthTestEnable(desc.depthTestEnable);
                    ds->depthWriteEnable(desc.depthWriteEnable);
                    ds->depthCompareOp(desc.depthCompareOp);
                    ds->depthBoundsTestEnable(desc.depthBoundsTestEnable);
                    ds->stencilTestEnable(desc.stencilTestEnable);
                    ds->create();
                    m_ownedPipelineStateResources.push_back(ds);
                    states.depthStencilState = ds;
                }

                if (desc.hasColorAttachment) {
                    auto* bs = ctx->createResource<FCT::BlendState>();
                    if (bs) {
                        bs->enable(desc.blendEnable, 0)
                          .colorFactors(desc.srcColorBlend, desc.dstColorBlend, 0)
                          .alphaFactors(desc.srcAlphaBlend, desc.dstAlphaBlend, 0)
                          .op(desc.colorBlendOp, 0)
                          .mask(desc.colorWriteMask & 0xFu, 0);
                        bs->create();
                        m_ownedPipelineStateResources.push_back(bs);
                        states.blendState = bs;
                    }
                }

                return states;
            }

            void RendererSubsystem::rebuildDirtyPipelineStates(FCT::Context* ctx)
            {
                ZoneScoped();
                if (!ctx || !m_submitPipelineObjectsDirty) {
                    return;
                }
                m_submitPipelineObjectsDirty = false;

                for (const auto& desc : m_submitPipelineObjectsPending) {
                    if (desc.passName == "ShadowMapPass") {
                        m_shadowPassStates = buildRuntimeStatesForDesc(ctx, desc);
                    } else if (desc.passName == "ObjectPass") {
                        m_objectPassStates = buildRuntimeStatesForDesc(ctx, desc);
                    } else if (desc.passName == "ToneMappingPass") {
                        m_toneMappingPassStates = buildRuntimeStatesForDesc(ctx, desc);
                    } else if (desc.passName == "AntiAliasingPass") {
                        m_antiAliasingPassStates = buildRuntimeStatesForDesc(ctx, desc);
                    }
                }
            }

            std::vector<PipelineObjectDesc> RendererSubsystem::getPipelineObjects() const
            {
                const uint32_t slot = m_publishedPipelineObjectSlot.load(std::memory_order_acquire);
                return m_pipelineObjectSnapshots[slot];
            }

            bool RendererSubsystem::getPipelineObject(std::string_view name, PipelineObjectDesc& outDesc) const
            {
                const uint32_t slot = m_publishedPipelineObjectSlot.load(std::memory_order_acquire);
                for (const auto& item : m_pipelineObjectSnapshots[slot]) {
                    if (item.name == name) {
                        outDesc = item;
                        return true;
                    }
                }
                return false;
            }

            void RendererSubsystem::upsertPipelineObject(const PipelineObjectDesc& desc)
            {
                assert(!desc.name.empty() && "PipelineObjectDesc 名称不可为空");
                auto it = std::find_if(
                    m_logicPipelineObjects.begin(),
                    m_logicPipelineObjects.end(),
                    [&desc](const PipelineObjectDesc& existing) { return existing.name == desc.name; });
                if (it != m_logicPipelineObjects.end()) {
                    const bool keepBuiltin = it->isBuiltin;
                    *it = desc;
                    it->isBuiltin = keepBuiltin;
                } else {
                    m_logicPipelineObjects.push_back(desc);
                }
                publishPipelineObjectSnapshot();
                if (!m_hasSyncedAtLeastOnce.load(std::memory_order_acquire)) {
                    m_submitPipelineObjectsPending = m_logicPipelineObjects;
                    m_submitPipelineObjectsDirty = true;
                }
                m_logicPipelineObjectsDirty.store(true, std::memory_order_release);
            }

            bool RendererSubsystem::removePipelineObject(std::string_view name)
            {
                auto it = std::find_if(
                    m_logicPipelineObjects.begin(),
                    m_logicPipelineObjects.end(),
                    [name](const PipelineObjectDesc& existing) { return existing.name == name; });
                if (it == m_logicPipelineObjects.end() || it->isBuiltin) {
                    return false;
                }
                m_logicPipelineObjects.erase(it);
                publishPipelineObjectSnapshot();
                if (!m_hasSyncedAtLeastOnce.load(std::memory_order_acquire)) {
                    m_submitPipelineObjectsPending = m_logicPipelineObjects;
                    m_submitPipelineObjectsDirty = true;
                }
                m_logicPipelineObjectsDirty.store(true, std::memory_order_release);
                return true;
            }

            void RendererSubsystem::resetBuiltinPipelineObjects()
            {
                initDefaultPipelineObjects();
            }

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine
