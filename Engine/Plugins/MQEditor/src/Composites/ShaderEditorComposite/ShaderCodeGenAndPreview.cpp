/**
 * @file ShaderCodeGenAndPreview.cpp
 * @brief 着色器节点图 DAG 拓扑转译 HLSL、Shaderc SPIR-V 实时编译、PBR 材质球预览与 .mqshader/.sgedt 序列化实现
 */
#include "ShaderEditorComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/resource/core/Resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/thirdparty/TomlBackend.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace MQEngine
{
    namespace MQEditor
    {
        void ShaderEditorComposite::compileShaderCode()
        {
            // 1. Kahn 拓扑排序与环路检测
            std::unordered_map<int, size_t> pinToNodeIdx;
            for (size_t i = 0; i < m_nodes.size(); ++i)
            {
                for (const auto& pin : m_nodes[i].pins)
                {
                    pinToNodeIdx[pin.id] = i;
                }
            }

            std::vector<std::vector<size_t>> adj(m_nodes.size());
            std::vector<int> inDegree(m_nodes.size(), 0);
            for (const auto& link : m_links)
            {
                auto itSrc = pinToNodeIdx.find(link.startPin);
                auto itDst = pinToNodeIdx.find(link.endPin);
                if (itSrc != pinToNodeIdx.end() && itDst != pinToNodeIdx.end())
                {
                    adj[itSrc->second].push_back(itDst->second);
                    inDegree[itDst->second]++;
                }
            }

            std::queue<size_t> readyQueue;
            for (size_t i = 0; i < m_nodes.size(); ++i)
            {
                if (inDegree[i] == 0)
                {
                    readyQueue.push(i);
                }
            }

            std::vector<size_t> topoOrder;
            topoOrder.reserve(m_nodes.size());
            while (!readyQueue.empty())
            {
                const size_t curr = readyQueue.front();
                readyQueue.pop();
                topoOrder.push_back(curr);
                for (size_t nxt : adj[curr])
                {
                    if (--inDegree[nxt] == 0)
                    {
                        readyQueue.push(nxt);
                    }
                }
            }

            if (topoOrder.size() != m_nodes.size())
            {
                m_compileSuccess = false;
                m_compileOutput = "[ShaderGraph 编译器错误] 检测到节点依赖环路 (Cycle Detected)，拓扑排序失败！\n";
                return;
            }

            // 2. 构建输入引脚到上游输出引脚表达式的映射，并按拓扑序生成节点级 HLSL 语句
            std::unordered_map<int, int> inputPinToSourcePin;
            for (const auto& link : m_links)
            {
                inputPinToSourcePin[link.endPin] = link.startPin;
            }

            auto resolveInputExpr = [&](int inPinId, const std::string& fallbackExpr) -> std::string {
                auto it = inputPinToSourcePin.find(inPinId);
                if (it != inputPinToSourcePin.end())
                {
                    return "pin_" + std::to_string(it->second);
                }
                return fallbackExpr;
            };

            std::ostringstream nodeStatements;
            nodeStatements << std::fixed << std::setprecision(4);
            std::string masterAlbedoExpr = "float4(mat.baseColorFactor.rgb * sampledAlbedo.rgb * vtxColor, sampledAlbedo.a * mat.baseColorFactor.a)";
            std::string masterMetallicExpr = "mat.metallicRoughnessNormalOcclusion.x";
            std::string masterRoughnessExpr = "mat.metallicRoughnessNormalOcclusion.y";
            float roughnessBias = 0.0f;
            std::string topoSummary;

            for (size_t idx : topoOrder)
            {
                const auto& node = m_nodes[idx];
                if (!topoSummary.empty())
                {
                    topoSummary += " -> ";
                }
                topoSummary += node.title;

                if (node.title.find("Texture") != std::string::npos)
                {
                    std::string uvExpr = "sIn.texCoord";
                    for (const auto& pin : node.pins)
                    {
                        if (pin.isInput && pin.name == "UV")
                        {
                            auto itUv = inputPinToSourcePin.find(pin.id);
                            if (itUv != inputPinToSourcePin.end())
                            {
                                uvExpr = "pin_" + std::to_string(itUv->second) + ".xy";
                            }
                        }
                    }
                    for (const auto& pin : node.pins)
                    {
                        if (!pin.isInput)
                        {
                            nodeStatements << "    float4 texSample_" << pin.id
                                           << " = SampleBindlessTexture(mat.albedoTexIndex, " << uvExpr << ");\n"
                                           << "    float4 pin_" << pin.id
                                           << " = float4(mat.baseColorFactor.rgb * texSample_" << pin.id
                                           << ".rgb * vtxColor, texSample_" << pin.id << ".a * mat.baseColorFactor.a);\n";
                        }
                    }
                }
                else if (node.title.find("Color") != std::string::npos)
                {
                    for (const auto& pin : node.pins)
                    {
                        if (!pin.isInput)
                        {
                            nodeStatements << "    float4 pin_" << pin.id << " = float4("
                                           << node.color[0] << "f, "
                                           << node.color[1] << "f, "
                                           << node.color[2] << "f, "
                                           << node.color[3] << "f);\n";
                        }
                    }
                }
                else if (node.title.find("Multiply") != std::string::npos)
                {
                    std::string exprA = "float4(1.0f, 1.0f, 1.0f, 1.0f)";
                    std::string exprB = "float4(1.0f, 1.0f, 1.0f, 1.0f)";
                    int outPinId = -1;
                    int inIdx = 0;
                    for (const auto& pin : node.pins)
                    {
                        if (pin.isInput)
                        {
                            if (inIdx == 0) exprA = resolveInputExpr(pin.id, exprA);
                            else if (inIdx == 1) exprB = resolveInputExpr(pin.id, exprB);
                            ++inIdx;
                        }
                        else
                        {
                            outPinId = pin.id;
                        }
                    }
                    if (outPinId >= 0)
                    {
                        nodeStatements << "    float4 pin_" << outPinId
                                       << " = (" << exprA << ") * (" << exprB << ");\n";
                    }
                }
                else if (node.title.find("Add") != std::string::npos)
                {
                    std::string exprA = "float4(0.0f, 0.0f, 0.0f, 0.0f)";
                    std::string exprB = "float4(0.0f, 0.0f, 0.0f, 0.0f)";
                    int outPinId = -1;
                    int inIdx = 0;
                    for (const auto& pin : node.pins)
                    {
                        if (pin.isInput)
                        {
                            if (inIdx == 0) exprA = resolveInputExpr(pin.id, exprA);
                            else if (inIdx == 1) exprB = resolveInputExpr(pin.id, exprB);
                            ++inIdx;
                        }
                        else
                        {
                            outPinId = pin.id;
                        }
                    }
                    if (outPinId >= 0)
                    {
                        nodeStatements << "    float4 pin_" << outPinId
                                       << " = (" << exprA << ") + (" << exprB << ");\n";
                    }
                }
                else if (node.title.find("Master") != std::string::npos)
                {
                    roughnessBias = 0.0f;
                    for (const auto& pin : node.pins)
                    {
                        if (!pin.isInput) continue;
                        if (pin.name == "Albedo")
                        {
                            masterAlbedoExpr = resolveInputExpr(pin.id, masterAlbedoExpr);
                        }
                        else if (pin.name == "Metallic")
                        {
                            auto it = inputPinToSourcePin.find(pin.id);
                            if (it != inputPinToSourcePin.end())
                            {
                                masterMetallicExpr = "pin_" + std::to_string(it->second) + ".x";
                            }
                        }
                        else if (pin.name == "Roughness")
                        {
                            auto it = inputPinToSourcePin.find(pin.id);
                            if (it != inputPinToSourcePin.end())
                            {
                                masterRoughnessExpr = "pin_" + std::to_string(it->second) + ".x";
                            }
                        }
                    }
                }
            }

            std::ostringstream hlsl;
            hlsl << std::fixed << std::setprecision(4);
            hlsl << "// Auto-generated by MQEngine ShaderGraph Compiler (Topological Order: " << topoSummary << ")\n"
                 << "#include \"BindlessCommon.hlsli\"\n\n"
                 << "ShaderOut main(ShaderIn sIn) {\n"
                 << "    ShaderOut sOut;\n"
                 << "    uint instanceId = sIn.instanceId;\n"
                 << "    GpuInstanceRow inst = g_Instances[instanceId];\n"
                 << "    GpuMaterialRecord mat = g_Materials[inst.materialSlot];\n"
                 << "    float3 vtxColor = (dot(sIn.color.rgb, sIn.color.rgb) > 0.0001f) ? sIn.color.rgb : float3(1.0f, 1.0f, 1.0f);\n"
                 << "    float4 sampledAlbedo = SampleBindlessTexture(mat.albedoTexIndex, sIn.texCoord);\n"
                 << nodeStatements.str()
                 << "    float4 masterAlbedo = " << masterAlbedoExpr << ";\n"
                 << "    float3 albedo = masterAlbedo.rgb;\n"
                 << "    float metallic = saturate(" << masterMetallicExpr << ");\n"
                 << "    float roughness = clamp((" << masterRoughnessExpr << ") + (" << roughnessBias << "f), 0.045f, 1.0f);\n"
                 << "    float ao = saturate(mat.metallicRoughnessNormalOcclusion.w);\n\n"
                 << "    ApplyProceduralGroundGridLinear(albedo, roughness, sIn.worldPos.xyz, inst.flags);\n\n"
                 << "    float3 N = normalize(sIn.normal);\n"
                 << "    if (mat.normalTexIndex != 1u) {\n"
                 << "        float3 normalMap = SampleBindlessTexture(mat.normalTexIndex, sIn.texCoord).xyz * 2.0f - 1.0f;\n"
                 << "        normalMap.xy *= mat.metallicRoughnessNormalOcclusion.z;\n"
                 << "        float3x3 TBN = float3x3(normalize(sIn.tangent), normalize(sIn.bitangent), N);\n"
                 << "        N = normalize(mul(normalMap, TBN));\n"
                 << "    }\n\n"
                 << "    float directionalShadow = 1.0f;\n"
                 << "    MQ_SAMPLE_DIRECTIONAL_SHADOW(DepthFromLigth0Image, sIn.worldPos.xyz, N, directionalShadow);\n\n"
                 << "    float3 emissive = mat.emissiveFactor.rgb * mat.emissiveFactor.w;\n"
                 << "    float3 litHdrColor = EvaluateStandardPbrShading(\n"
                 << "        sIn.worldPos.xyz, N, albedo, metallic, roughness, ao, emissive, directionalShadow);\n\n"
                 << "    sOut.target0 = float4(litHdrColor, 1.0f);\n"
                 << "    sOut.target1 = float4(sIn.worldPos.xyz * 0.1f + 0.5f, 1.0f);\n"
                 << "    sOut.target2 = float4(N * 0.5f + 0.5f, roughness);\n"
                 << "    return sOut;\n"
                 << "}\n";

            m_hlslSource = hlsl.str();
            m_compileOutput = "[ShaderGraph -> HLSL 编译器] Kahn 拓扑排序完成 (" +
                              std::to_string(m_nodes.size()) + " 个节点, " +
                              std::to_string(m_links.size()) + " 条连线): " + topoSummary + "\n";

            compileCustomHlslSource();
        }

        void ShaderEditorComposite::compileCustomHlslSource()
        {
            if (m_hlslSource.empty())
            {
                m_compileSuccess = false;
                m_compileOutput = "[HLSL 编译器错误] HLSL 源码为空！\n";
                return;
            }

            auto* renderHost = Host::RenderHost::InstancePtr();
            FCT::Context* ctx = renderHost ? renderHost->getContext() : nullptr;
            if (ctx && ctx->getGenerator() && ctx->getCompiler())
            {
                std::vector<FCT::ConstLayout> uniformLayouts;
                FCT::RHI::ShaderBinary binaryCode;
                FCT::ResourceLayout resourceLayout;
                resourceLayout.addTexture(FCT::TextureElement(
                    FCT::TextureType::Texture2D,
                    "DepthFromLigth0Image",
                    FCT::ShaderStage::Fragment,
                    FCT::UpdateFrequency::PerFrame));

                std::string fullSource = ctx->getGenerator()->generatePixelShader(
                    Domain::Renderer::StandardPixelLayout,
                    uniformLayouts,
                    binaryCode,
                    resourceLayout,
                    m_hlslSource);

                std::vector<char> spv = ctx->getCompiler()->compile(
                    fullSource,
                    "FCTEntry",
                    FCT::ShaderKind::FragmentShader);

                if (spv.empty())
                {
                    m_compileSuccess = false;
                    m_compileOutput += "[Shaderc SPIR-V 编译器] 编译失败：HLSL 语法错误，生成的 SPIR-V 字节码为空！\n";
                    return;
                }

                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (MQEngine::ResourceLoader* loader = res->getResourceLoader())
                    {
                        (void)loader->makeDir("Assets");
                        (void)loader->makeDir("Assets/Shaders");
                        (void)loader->saveFile("Assets/Shaders/PBR_Standard.hlsl", m_hlslSource);
                    }
                }

                m_compileSuccess = true;
                m_compileOutput += "[Shaderc SPIR-V 编译器] 编译成功！真实 SPIR-V 字节码大小: " +
                                   std::to_string(spv.size()) + " bytes (" +
                                   std::to_string(spv.size() / 4) + " words)，已保存至 Assets/Shaders/PBR_Standard.hlsl。\n";

                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    (void)domain->renderer.updateCustomPixelShader(m_hlslSource);
                }
            }
            else
            {
                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (MQEngine::ResourceLoader* loader = res->getResourceLoader())
                    {
                        (void)loader->makeDir("Assets");
                        (void)loader->makeDir("Assets/Shaders");
                        (void)loader->saveFile("Assets/Shaders/PBR_Standard.hlsl", m_hlslSource);
                    }
                }
                m_compileSuccess = true;
                m_compileOutput += "[HLSL 编译器 (Headless)] 代码生成成功 (" +
                                   std::to_string(m_hlslSource.size()) + " bytes)，已保存至 Assets/Shaders/PBR_Standard.hlsl。\n";
            }
        }

        void ShaderEditorComposite::renderPbrMaterialPreview()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::BeginChild("PbrMaterialParamsLeft", ImVec2(260.0f, 0.0f), true);
            ImGui::TextDisabled("%s", L("PBR 材质球参数调节", "PBR Material Ball Parameters"));
            ImGui::Separator();

            if (ImGui::ColorEdit3(L("反照率 (Albedo)", "Albedo"), m_previewParams.baseColor))
            {
                for (auto& node : m_nodes)
                {
                    if (node.title.find("Color") != std::string::npos)
                    {
                        node.color[0] = m_previewParams.baseColor[0];
                        node.color[1] = m_previewParams.baseColor[1];
                        node.color[2] = m_previewParams.baseColor[2];
                    }
                }
                compileShaderCode();
            }
            ImGui::SliderFloat(L("金属度 (Metallic)", "Metallic"), &m_previewParams.metallic, 0.0f, 1.0f, "%.2f");
            if (ImGui::SliderFloat(L("粗糙度 (Roughness)", "Roughness"), &m_previewParams.roughness, 0.05f, 1.0f, "%.2f"))
            {
                for (auto& node : m_nodes)
                {
                    if (node.title.find("Master") != std::string::npos)
                    {
                        node.value = m_previewParams.roughness;
                    }
                }
                compileShaderCode();
            }
            ImGui::SliderFloat(L("环境光遮蔽 (AO)", "Ambient Occlusion"), &m_previewParams.ao, 0.1f, 1.0f, "%.2f");
            ImGui::ColorEdit3(L("自发光 (Emissive)", "Emissive"), m_previewParams.emissive);
            ImGui::Separator();
            ImGui::SliderFloat(L("光源方位角", "Light Azimuth"), &m_previewParams.lightAzimuthDeg, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat(L("光源仰角", "Light Elevation"), &m_previewParams.lightElevationDeg, 5.0f, 85.0f, "%.0f deg");
            ImGui::SliderFloat(L("光源强度", "Light Intensity"), &m_previewParams.lightIntensity, 0.5f, 6.0f, "%.2f");
            ImGui::Checkbox(L("材质球自动旋转", "Auto Rotate Sphere"), &m_previewParams.autoRotateSphere);
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("PbrMaterialBallRight", ImVec2(0.0f, 0.0f), true);
            ImGui::TextDisabled("%s", L("Cook-Torrance GGX 实时材质球预览视口", "Cook-Torrance GGX Material Ball Preview"));
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const float radius = std::max(40.0f, std::min(avail.x, avail.y) * 0.42f);
            const ImVec2 center(p0.x + avail.x * 0.5f, p0.y + avail.y * 0.5f);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), IM_COL32(20, 24, 30, 255), 6.0f);

            const float az = m_previewParams.lightAzimuthDeg * 3.14159265f / 180.0f + m_previewParams.sphereYawRad;
            const float el = m_previewParams.lightElevationDeg * 3.14159265f / 180.0f;
            const float lx = std::cos(el) * std::sin(az);
            const float ly = std::sin(el);
            const float lz = std::cos(el) * std::cos(az);

            const int gridRes = 40;
            const float step = (radius * 2.0f) / static_cast<float>(gridRes);
            for (int gy = 0; gy < gridRes; ++gy)
            {
                const float ny = 1.0f - (static_cast<float>(gy) + 0.5f) * (2.0f / static_cast<float>(gridRes));
                for (int gx = 0; gx < gridRes; ++gx)
                {
                    const float nx = (static_cast<float>(gx) + 0.5f) * (2.0f / static_cast<float>(gridRes)) - 1.0f;
                    const float r2 = nx * nx + ny * ny;
                    if (r2 > 1.0f)
                        continue;

                    const float nz = std::sqrt(std::max(0.0f, 1.0f - r2));
                    const float ndotl = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                    const float ndotv = std::max(0.001f, nz);
                    float hx = lx;
                    float hy = ly;
                    float hz = lz + 1.0f;
                    const float hLen = std::sqrt(std::max(0.0001f, hx * hx + hy * hy + hz * hz));
                    hx /= hLen;
                    hy /= hLen;
                    hz /= hLen;
                    const float ndoth = std::max(0.0f, nx * hx + ny * hy + nz * hz);

                    const float alpha = m_previewParams.roughness * m_previewParams.roughness;
                    const float alpha2 = std::max(0.001f, alpha * alpha);
                    const float denom = ndoth * ndoth * (alpha2 - 1.0f) + 1.0f;
                    const float D = alpha2 / (3.14159265f * denom * denom);
                    const float spec = D * 0.25f * ndotl;

                    const float kD = 1.0f - m_previewParams.metallic;
                    const float r = std::clamp(
                        (m_previewParams.baseColor[0] * (0.18f * m_previewParams.ao + kD * ndotl * m_previewParams.lightIntensity * 0.45f) +
                         spec * (0.04f + 0.96f * m_previewParams.baseColor[0] * m_previewParams.metallic)) +
                            m_previewParams.emissive[0],
                        0.0f,
                        1.0f);
                    const float g = std::clamp(
                        (m_previewParams.baseColor[1] * (0.18f * m_previewParams.ao + kD * ndotl * m_previewParams.lightIntensity * 0.45f) +
                         spec * (0.04f + 0.96f * m_previewParams.baseColor[1] * m_previewParams.metallic)) +
                            m_previewParams.emissive[1],
                        0.0f,
                        1.0f);
                    const float b = std::clamp(
                        (m_previewParams.baseColor[2] * (0.18f * m_previewParams.ao + kD * ndotl * m_previewParams.lightIntensity * 0.45f) +
                         spec * (0.04f + 0.96f * m_previewParams.baseColor[2] * m_previewParams.metallic)) +
                            m_previewParams.emissive[2],
                        0.0f,
                        1.0f);

                    const ImVec2 cellMin(center.x - radius + gx * step, center.y - radius + gy * step);
                    const ImVec2 cellMax(cellMin.x + step + 0.8f, cellMin.y + step + 0.8f);
                    drawList->AddRectFilled(
                        cellMin,
                        cellMax,
                        IM_COL32(
                            static_cast<uint8_t>(r * 255.0f),
                            static_cast<uint8_t>(g * 255.0f),
                            static_cast<uint8_t>(b * 255.0f),
                            255));
                }
            }
            drawList->AddCircle(center, radius, IM_COL32(90, 110, 140, 200), 64, 1.5f);
            ImGui::Dummy(avail);
            ImGui::EndChild();
        }

        void ShaderEditorComposite::saveShaderGraph(const std::string& name)
        {
            const std::string actualName = name.empty() ? "DefaultPBR" : name;
            const std::string sgedtPath = "Assets/ShaderGraph/" + actualName + ".sgedt";
            const std::string mqShaderPath = "Assets/ShaderGraph/" + actualName + ".mqshader";

            std::ostringstream ss;
            ss << "# MQEngine ShaderGraph (.mqshader / .sgedt TOML format)\n";
            ss << "[graph]\n";
            ss << "name = \"" << actualName << "\"\n";
            ss << "version = \"2.0\"\n";
            ss << "next_node_id = " << m_nextNodeId << "\n";
            ss << "next_pin_id = " << m_nextPinId << "\n";
            ss << "next_link_id = " << m_nextLinkId << "\n\n";

            for (auto& node : m_nodes)
            {
                if (m_imnodesCtx && m_shaderEditorCtx)
                {
                    ImVec2 pos = ImNodes::GetNodeEditorSpacePos(node.id);
                    node.pos[0] = pos.x;
                    node.pos[1] = pos.y;
                }

                ss << "[[nodes]]\n";
                ss << "id = " << node.id << "\n";
                ss << "title = \"" << node.title << "\"\n";
                ss << "pos_x = " << node.pos[0] << "\n";
                ss << "pos_y = " << node.pos[1] << "\n";
                ss << "value = " << node.value << "\n";
                ss << "color = [" << node.color[0] << ", " << node.color[1] << ", "
                   << node.color[2] << ", " << node.color[3] << "]\n";

                for (const auto& pin : node.pins)
                {
                    ss << "[[nodes.pins]]\n";
                    ss << "id = " << pin.id << "\n";
                    ss << "name = \"" << pin.name << "\"\n";
                    ss << "is_input = " << (pin.isInput ? "true" : "false") << "\n";
                }
                ss << "\n";
            }

            for (const auto& link : m_links)
            {
                ss << "[[links]]\n";
                ss << "id = " << link.id << "\n";
                ss << "start_pin = " << link.startPin << "\n";
                ss << "end_pin = " << link.endPin << "\n";
            }

            auto* res = Resource::Resource::InstancePtr();
            if (res && res->getResourceLoader())
            {
                auto* loader = res->getResourceLoader();
                (void)loader->ensureDir("Assets/ShaderGraph");
                (void)loader->saveFile(sgedtPath, ss.str());
                Status st = loader->saveFile(mqShaderPath, ss.str());
                if (st.ok() && m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "ShaderGraph",
                        "着色器图已保存至 " + mqShaderPath,
                        EditorBase::LogEntry::Level::Info);
                }
            }
        }

        void ShaderEditorComposite::loadShaderGraph(const std::string& name)
        {
            const std::string actualName = name.empty() ? "DefaultPBR" : name;
            std::string path = "Assets/ShaderGraph/" + actualName + ".mqshader";

            auto* res = Resource::Resource::InstancePtr();
            if (!res || !res->getResourceLoader())
                return;

            auto* loader = res->getResourceLoader();
            if (!loader->exists(path))
            {
                path = "Assets/ShaderGraph/" + actualName + ".sgedt";
            }

            auto contentOr = loader->loadFile(path);
            if (!contentOr.ok())
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "ShaderGraph",
                        "读取着色器图失败: " + std::string(contentOr.status().message()),
                        EditorBase::LogEntry::Level::Warn);
                }
                return;
            }

            toml::parse_result parsed = toml::parse(contentOr.value());
            if (!parsed)
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "ShaderGraph",
                        "解析着色器图 TOML 失败",
                        EditorBase::LogEntry::Level::Error);
                }
                return;
            }

            m_nodes.clear();
            m_links.clear();

            const auto& tbl = parsed.table();
            if (auto gTbl = tbl["graph"].as_table())
            {
                m_nextNodeId = gTbl->get("next_node_id")->value_or(10);
                m_nextPinId = gTbl->get("next_pin_id")->value_or(100);
                m_nextLinkId = gTbl->get("next_link_id")->value_or(1000);
            }

            if (auto nodesArr = tbl["nodes"].as_array())
            {
                for (const auto& item : *nodesArr)
                {
                    if (auto nTbl = item.as_table())
                    {
                        ShaderGraphNode node;
                        node.id = nTbl->get("id")->value_or(0);
                        node.title = nTbl->get("title")->value_or("");
                        node.pos[0] = static_cast<float>(nTbl->get("pos_x")->value_or(100.0));
                        node.pos[1] = static_cast<float>(nTbl->get("pos_y")->value_or(100.0));
                        node.value = static_cast<float>(nTbl->get("value")->value_or(1.0));

                        if (auto cArr = nTbl->get("color")->as_array())
                        {
                            for (size_t c = 0; c < 4 && c < cArr->size(); ++c)
                            {
                                node.color[c] = static_cast<float>(cArr->get(c)->value_or(1.0));
                            }
                        }

                        if (auto pinsArr = nTbl->get("pins")->as_array())
                        {
                            for (const auto& pItem : *pinsArr)
                            {
                                if (auto pTbl = pItem.as_table())
                                {
                                    ShaderPin pin;
                                    pin.id = pTbl->get("id")->value_or(0);
                                    pin.name = pTbl->get("name")->value_or("");
                                    pin.isInput = pTbl->get("is_input")->value_or(true);
                                    node.pins.push_back(pin);
                                }
                            }
                        }

                        m_nodes.push_back(node);
                        if (m_imnodesCtx && m_shaderEditorCtx)
                        {
                            ImNodes::SetNodeEditorSpacePos(node.id, ImVec2(node.pos[0], node.pos[1]));
                        }
                    }
                }
            }

            if (auto linksArr = tbl["links"].as_array())
            {
                for (const auto& item : *linksArr)
                {
                    if (auto lTbl = item.as_table())
                    {
                        ShaderGraphLink link;
                        link.id = lTbl->get("id")->value_or(0);
                        link.startPin = lTbl->get("start_pin")->value_or(0);
                        link.endPin = lTbl->get("end_pin")->value_or(0);
                        m_links.push_back(link);
                    }
                }
            }

            m_shaderNodesPositioned = true;
            compileShaderCode();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "ShaderGraph",
                    "着色器图已加载: " + actualName,
                    EditorBase::LogEntry::Level::Info);
            }
        }

    } // namespace MQEditor
} // namespace MQEngine
