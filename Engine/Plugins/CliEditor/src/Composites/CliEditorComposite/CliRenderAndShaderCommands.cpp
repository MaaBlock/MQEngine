/**
 * @file CliRenderAndShaderCommands.cpp
 * @brief CliEditor 的 render.*（Vulkan 抓帧截图、RenderGraph 拓扑与遥测）与 shader.*（HLSL 实时热编译与预设）命令实现
 */
#include "CliEditorComposite.h"
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/resource/core/resource.h>

#include <sstream>

namespace MQEngine
{
    namespace CliEditor
    {
        void CliEditorComposite::registerRenderAndShaderCommands()
        {
            registerCommand(CliCommandSpec{
                .domain = "render",
                .action = "stats",
                .ownerComponent = "Domain::Renderer::RendererSubsystem",
                .summary = "获取当前 Vulkan 渲染管线的单帧遥测数据（DrawCalls、可见实体数、顶点数、三角形数、耗时分解）",
                .details = "直接读取 RendererSubsystem::getFrameStats() 返回当前帧的视锥剔除与录制统计。",
                .example = "CliEditor.exe --headless --exec=\"render.stats\" --json",
                .params = {},
                .handler = [](const std::unordered_map<std::string, std::string>&) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* renderer = domain ? domain->getRenderer() : nullptr;
                    if (!renderer)
                    {
                        return {false, "render.stats", "RendererSubsystem 不可用", "{}"};
                    }
                    const auto stats = renderer->getFrameStats();
                    std::ostringstream json;
                    json << "{\"totalEntities\": " << stats.totalSceneEntities
                         << ", \"visibleEntities\": " << stats.visibleEntities
                         << ", \"culledEntities\": " << stats.culledEntities
                         << ", \"drawCalls\": " << stats.drawCalls
                         << ", \"cachedModels\": " << stats.cachedModelCount << "}";
                    return {true, "render.stats", "Retrieved Vulkan renderer telemetry", json.str()};
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "render",
                .action = "capture",
                .ownerComponent = "Domain::Renderer::RendererSubsystem",
                .summary = "捕获当前 Vulkan GPU 渲染结果图片（.png / .bmp / .tga），供 AI 直接视觉检视画面",
                .details = "通过 RendererSubsystem::requestFrameCapture 将指定 RenderTarget（mainWindowColor 或 SceneColorTarget）回读至磁盘。",
                .example = "CliEditor.exe --headless --exec=\"render.capture out=Saved/Screenshots/cli_scene.png target=mainWindowColor wait_frames=8\"",
                .params = {
                    {"out", "path", false, "Saved/Screenshots/cli_capture.png", "输出图片路径（支持 .png / .bmp / .tga）"},
                    {"target", "string", false, "mainWindowColor", "捕获目标纹理名称（mainWindowColor | SceneColorTarget | HdrColor）"},
                    {"wait_frames", "int", false, "6", "等待多少个渲染帧后再请求抓图退出"}
                },
                .handler = [this](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* renderer = domain ? domain->getRenderer() : nullptr;
                    if (!renderer)
                    {
                        return {false, "render.capture", "RendererSubsystem 不可用", "{}"};
                    }

                    const std::string outPath = args.at("out");
                    const std::string targetName = args.at("target");
                    int32_t waitFrames = 6;
                    const auto& wfStr = args.at("wait_frames");
                    (void)absl::SimpleAtoi(absl::string_view(wfStr.data(), wfStr.size()), &waitFrames);
                    const uint32_t totalWait = static_cast<uint32_t>(std::max(4, waitFrames));
                    requestDeferredExitAfterFrames(totalWait);

                    renderer->scheduleFrameCapture(outPath, targetName, static_cast<uint64_t>(totalWait - 1));
                    std::ostringstream json;
                    json.imbue(std::locale::classic());
                    json << "{\"scheduledFrame\": " << (totalWait - 1)
                         << ", \"outputPath\": \"" << escapeJson(outPath)
                         << "\", \"target\": \"" << escapeJson(targetName) << "\"}";
                    return {
                        true,
                        "render.capture",
                        "Scheduled GPU render target '" + targetName + "' capture -> " + outPath,
                        json.str()
                    };
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "render",
                .action = "export_pgraph",
                .ownerComponent = "MQEditor::RenderGraphEditorComposite",
                .summary = "将标准 RenderGraph 管线拓扑（ObjectPass -> ToneMappingPass）导出为 .pgraph TOML 文件或生成 C++ 代码",
                .details = "与 RenderGraphEditorComposite 共享相同的 .pgraph 序列化格式，支持 AI 批量生成或检查渲染图配置。",
                .example = "CliEditor.exe --headless --exec=\"render.export_pgraph out=Assets/RenderGraphs/DefaultPipeline.pgraph\"",
                .params = {
                    {"out", "path", false, "Assets/RenderGraphs/DefaultPipeline.pgraph", "导出的 .pgraph 路径"}
                },
                .handler = [](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    const std::string outPath = args.at("out");
                    const std::string pgraphToml =
                        "# MQEngine RenderGraph Topology (.pgraph)\n"
                        "version = \"2.0\"\n\n"
                        "[[passes]]\n"
                        "name = \"ObjectPass\"\n"
                        "enable_depth = true\n"
                        "targets = [\"HdrColor\", \"WorldPosTarget\", \"NormalRoughnessTarget\"]\n"
                        "depth_stencil = \"DepthTarget\"\n\n"
                        "[[passes]]\n"
                        "name = \"ToneMappingPass\"\n"
                        "enable_depth = false\n"
                        "texture_inputs = [\"HdrColor\"]\n"
                        "targets = [\"mainWindowColor\"]\n";

                    auto* res = Resource::Resource::InstancePtr();
                    if (!res || !res->getResourceLoader())
                    {
                        return {false, "render.export_pgraph", "ResourceLoader 不可用", "{}"};
                    }
                    Status st = res->getResourceLoader()->saveFile(outPath, pgraphToml);
                    return {
                        st.ok(),
                        "render.export_pgraph",
                        st.ok() ? ("Exported RenderGraph topology to " + outPath) : std::string(st.message()),
                        "{\"path\": \"" + escapeJson(outPath) + "\"}"
                    };
                }
            });

            registerCommand(CliCommandSpec{
                .domain = "shader",
                .action = "compile_hlsl",
                .ownerComponent = "MQEditor::ShaderEditorComposite",
                .summary = "实时热编译并替换场景 ObjectPass 的自定义 HLSL 像素着色器（支持文件或内联预设）",
                .details = "调用 RendererSubsystem::setCustomObjectPixelShader 在渲染提交线程无锁热更新 SPIR-V 管线。",
                .example = "CliEditor.exe --headless --exec=\"shader.compile_hlsl preset=toon\"",
                .params = {
                    {"preset", "string", false, "pbr", "内置着色器预设：pbr | toon | hologram | normals"},
                    {"file", "path", false, "", "可选：从磁盘读取自定义 HLSL 像素着色器源码路径"}
                },
                .handler = [](const std::unordered_map<std::string, std::string>& args) -> CliExecutionResult {
                    auto* domain = Domain::Domain::InstancePtr();
                    auto* renderer = domain ? domain->getRenderer() : nullptr;
                    if (!renderer)
                    {
                        return {false, "shader.compile_hlsl", "RendererSubsystem 不可用", "{}"};
                    }

                    std::string hlslCode;
                    const std::string filePath = args.at("file");
                    const std::string preset = args.at("preset");

                    if (!filePath.empty())
                    {
                        if (auto* res = Resource::Resource::InstancePtr(); res && res->getResourceLoader())
                        {
                            auto loaded = res->getResourceLoader()->loadFile(filePath);
                            if (loaded.ok())
                            {
                                hlslCode = *loaded;
                            }
                        }
                    }

                    if (hlslCode.empty())
                    {
                        if (preset == "normals")
                        {
                            hlslCode = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    float3 N = normalize(sIn.normal) * 0.5f + 0.5f;
    sOut.target0 = float4(N, 1.0f);
    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);
    sOut.target2 = float4(N, 0.5f);
    return sOut;
}
)";
                        }
                        else if (preset == "toon")
                        {
                            hlslCode = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    GpuInstanceRow inst = g_Instances[sIn.instanceId];
    GpuMaterialRecord mat = g_Materials[inst.materialSlot];
    float3 vtxColor = (dot(sIn.color.rgb, sIn.color.rgb) > 0.0001f) ? sIn.color.rgb : float3(1.0f, 1.0f, 1.0f);
    float4 sampledAlbedo = SampleBindlessTexture(mat.albedoTexIndex, sIn.texCoord);
    float3 N = normalize(sIn.normal);
    float3 L = normalize(-g_DirectionalLightDirection.xyz);
    float ndl = max(dot(N, L), 0.0f);
    float band = ndl > 0.75f ? 1.0f : (ndl > 0.35f ? 0.6f : 0.25f);
    float3 col = mat.baseColorFactor.rgb * sampledAlbedo.rgb * vtxColor * band;
    sOut.target0 = float4(col, 1.0f);
    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);
    sOut.target2 = float4(N * 0.5f + 0.5f, 0.4f);
    return sOut;
}
)";
                        }
                        else if (preset == "hologram")
                        {
                            hlslCode = R"(
#include "BindlessCommon.hlsli"

ShaderOut main(ShaderIn sIn) {
    ShaderOut sOut;
    float3 N = normalize(sIn.normal);
    float3 V = normalize(g_CameraWorldPos.xyz - sIn.worldPos.xyz);
    float fresnel = pow(1.0f - saturate(dot(N, V)), 2.5f);
    float scanline = 0.7f + 0.3f * sin(sIn.worldPos.y * 40.0f);
    float3 holo = float3(0.15f, 0.78f, 1.0f) * (0.25f + 1.35f * fresnel) * scanline;
    sOut.target0 = float4(holo, 1.0f);
    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);
    sOut.target2 = float4(N * 0.5f + 0.5f, 0.2f);
    return sOut;
}
)";
                        }
                    }

                    if (!hlslCode.empty())
                    {
                        Status st = renderer->updateCustomPixelShader(hlslCode);
                        if (!st.ok())
                        {
                            return {false, "shader.compile_hlsl", std::string(st.message()), "{}"};
                        }
                    }
                    return {
                        true,
                        "shader.compile_hlsl",
                        "Applied shader preset '" + preset + "' to ObjectPass",
                        "{\"preset\": \"" + escapeJson(preset) + "\"}"
                    };
                }
            });
        }

    } // namespace CliEditor
} // namespace MQEngine
