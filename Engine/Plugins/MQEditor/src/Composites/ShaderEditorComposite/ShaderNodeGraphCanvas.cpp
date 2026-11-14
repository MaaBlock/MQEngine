/**
 * @file ShaderNodeGraphCanvas.cpp
 * @brief ImNodes 着色器可视化节点图画布、引脚连接与右键上下文菜单实现
 */
#include "ShaderEditorComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace MQEngine
{
    namespace MQEditor
    {
        void ShaderEditorComposite::renderShaderNodeItems()
        {
            for (auto& node : m_nodes)
            {
                if (!m_shaderNodesPositioned)
                {
                    ImNodes::SetNodeEditorSpacePos(node.id, ImVec2(node.pos[0], node.pos[1]));
                }

                ImNodes::BeginNode(node.id);

                ImNodes::BeginNodeTitleBar();
                ImGui::TextUnformatted(node.title.c_str());
                ImNodes::EndNodeTitleBar();

                if (node.title.find("Color") != std::string::npos)
                {
                    ImGui::SetNextItemWidth(135.0f);
                    if (ImGui::ColorEdit4(("##color_" + std::to_string(node.id)).c_str(),
                                          node.color,
                                          ImGuiColorEditFlags_NoInputs))
                    {
                        m_previewParams.baseColor[0] = node.color[0];
                        m_previewParams.baseColor[1] = node.color[1];
                        m_previewParams.baseColor[2] = node.color[2];
                        m_previewParams.baseColor[3] = node.color[3];
                        compileShaderCode();
                    }
                }
                else if (node.title.find("Master") != std::string::npos)
                {
                    ImGui::SetNextItemWidth(110.0f);
                    if (ImGui::SliderFloat("Roughness", &node.value, 0.05f, 1.0f, "%.2f"))
                    {
                        m_previewParams.roughness = node.value;
                        compileShaderCode();
                    }
                }

                for (const auto& pin : node.pins)
                {
                    if (pin.isInput)
                    {
                        ImNodes::BeginInputAttribute(pin.id);
                        ImGui::TextUnformatted(pin.name.c_str());
                        ImNodes::EndInputAttribute();
                    }
                    else
                    {
                        ImNodes::BeginOutputAttribute(pin.id);
                        ImGui::Indent(60.0f);
                        ImGui::TextUnformatted(pin.name.c_str());
                        ImGui::Unindent(60.0f);
                        ImNodes::EndOutputAttribute();
                    }
                }

                ImNodes::EndNode();
            }
            m_shaderNodesPositioned = true;

            for (const auto& link : m_links)
            {
                ImNodes::Link(link.id, link.startPin, link.endPin);
            }
        }

        void ShaderEditorComposite::handleNodeGraphInteractions(bool isEditorHovered)
        {
            m_hoveredNodeId = -1;
            m_hoveredLinkId = -1;
            int hNode = -1;
            if (ImNodes::IsNodeHovered(&hNode))
            {
                m_hoveredNodeId = hNode;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    m_selectedNodeId = hNode;
                }
            }
            int hLink = -1;
            if (ImNodes::IsLinkHovered(&hLink))
            {
                m_hoveredLinkId = hLink;
            }

            int startAttr = 0;
            int endAttr = 0;
            if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
            {
                m_links.push_back(ShaderGraphLink{m_nextLinkId++, startAttr, endAttr});
                compileShaderCode();
            }

            int destroyedLinkId = 0;
            if (ImNodes::IsLinkDestroyed(&destroyedLinkId))
            {
                m_links.erase(
                    std::remove_if(m_links.begin(), m_links.end(),
                                   [destroyedLinkId](const ShaderGraphLink& l) { return l.id == destroyedLinkId; }),
                    m_links.end());
                compileShaderCode();
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isEditorHovered)
            {
                if (m_hoveredNodeId != -1)
                {
                    m_selectedNodeId = m_hoveredNodeId;
                    ImGui::OpenPopup("ShaderNodeContextMenu");
                }
                else if (m_hoveredLinkId != -1)
                {
                    ImGui::OpenPopup("ShaderLinkContextMenu");
                }
                else
                {
                    ImVec2 mPos = ImGui::GetMousePos();
                    m_contextMenuPos[0] = mPos.x;
                    m_contextMenuPos[1] = mPos.y;
                    ImGui::OpenPopup("ShaderGraphContextMenu");
                }
            }
        }

        void ShaderEditorComposite::renderNodeGraphContextMenus()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (ImGui::BeginPopup("ShaderNodeContextMenu"))
            {
                if (ImGui::MenuItem(L("删除节点", "Delete Node")))
                {
                    if (m_hoveredNodeId != -1)
                    {
                        m_links.erase(
                            std::remove_if(m_links.begin(), m_links.end(),
                                           [this](const ShaderGraphLink& l) {
                                               for (const auto& n : m_nodes)
                                               {
                                                   if (n.id == m_hoveredNodeId)
                                                   {
                                                       for (const auto& p : n.pins)
                                                       {
                                                           if (p.id == l.startPin || p.id == l.endPin) return true;
                                                       }
                                                   }
                                               }
                                               return false;
                                           }),
                            m_links.end());

                        m_nodes.erase(
                            std::remove_if(m_nodes.begin(), m_nodes.end(),
                                           [this](const ShaderGraphNode& n) { return n.id == m_hoveredNodeId; }),
                            m_nodes.end());
                        compileShaderCode();
                        m_hoveredNodeId = -1;
                    }
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("ShaderLinkContextMenu"))
            {
                if (ImGui::MenuItem(L("删除链接", "Delete Link")))
                {
                    if (m_hoveredLinkId != -1)
                    {
                        m_links.erase(
                            std::remove_if(m_links.begin(), m_links.end(),
                                           [this](const ShaderGraphLink& l) { return l.id == m_hoveredLinkId; }),
                            m_links.end());
                        compileShaderCode();
                        m_hoveredLinkId = -1;
                    }
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("ShaderGraphContextMenu"))
            {
                ImGui::Text("%s", L("添加着色器节点", "Add Shader Node"));
                ImGui::Separator();

                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::IsWindowAppearing())
                {
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::InputTextWithHint("##SearchFilter", L("搜索代码片段...", "Search snippets..."), m_nodeSearchFilter, sizeof(m_nodeSearchFilter));
                ImGui::Separator();

                if (ImGui::BeginMenu(L("绑定系统插槽 (Binded System Slots)", "Binded System Slots")))
                {
                    if (ImGui::BeginMenu("CameraSystem"))
                    {
                        if (ImGui::MenuItem("Uniform: ViewProj (float4x4)"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Camera.ViewProj";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "VP Matrix", false, ShaderPinType::Matrix4x4});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        if (ImGui::MenuItem("Uniform: CameraPos (float3)"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Camera.Pos";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "WorldPos", false, ShaderPinType::Float3});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("LightingSystem"))
                    {
                        if (ImGui::MenuItem("Uniform: LightDir (float3)"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Light.Direction";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Direction", false, ShaderPinType::Float3});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        if (ImGui::MenuItem("Uniform: LightColor (float4)"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Light.Color";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "ColorIntensity", false, ShaderPinType::Float4});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        if (ImGui::MenuItem("Texture: ShadowMapDepth"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "ShadowMapDepth";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "ShadowTex", false, ShaderPinType::Texture2D});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("TextureSamplerSystem"))
                    {
                        if (ImGui::MenuItem("Sampler: LinearClampSampler"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Sampler.LinearClamp";
                            n.category = ShaderNodeCategory::SystemSlot;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "SamplerState", false, ShaderPinType::SamplerState});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        if (ImGui::MenuItem("Texture: BaseColorMap"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Texture.BaseColor";
                            n.category = ShaderNodeCategory::Texture;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "UV", true, ShaderPinType::Float2});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "RGBA", false, ShaderPinType::Float4});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        if (ImGui::MenuItem("Texture: NormalMap"))
                        {
                            ShaderGraphNode n;
                            n.id = m_nextNodeId++;
                            n.title = "Texture.Normal";
                            n.category = ShaderNodeCategory::Texture;
                            n.pos[0] = m_contextMenuPos[0];
                            n.pos[1] = m_contextMenuPos[1];
                            n.pins.push_back(ShaderPin{m_nextPinId++, "UV", true, ShaderPinType::Float2});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "NormalTS", false, ShaderPinType::Float3});
                            m_nodes.push_back(n);
                            compileShaderCode();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                struct SnippetEntry
                {
                    const char* name;
                    ShaderNodeCategory category;
                };

                const SnippetEntry snippets[] = {
                    {"Texture2D Sample", ShaderNodeCategory::Texture},
                    {"Base Color (Vec4)", ShaderNodeCategory::Constant},
                    {"Multiply (A * B)", ShaderNodeCategory::Math},
                    {"Add (A + B)", ShaderNodeCategory::Math},
                    {"Lerp (A, B, T)", ShaderNodeCategory::Math},
                    {"Dot Product", ShaderNodeCategory::Vector},
                    {"Normalize (v)", ShaderNodeCategory::Vector},
                    {"Fresnel Schlick", ShaderNodeCategory::Pbr},
                    {"PBR Master Output", ShaderNodeCategory::Output}
                };

                ImGui::BeginChild("SnippetListChild", ImVec2(240, 180));
                for (const auto& snip : snippets)
                {
                    if (m_nodeSearchFilter[0] != '\0')
                    {
                        std::string lowerFilter = m_nodeSearchFilter;
                        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        std::string lowerName = snip.name;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        if (lowerName.find(lowerFilter) == std::string::npos)
                            continue;
                    }

                    if (ImGui::Selectable(snip.name))
                    {
                        ShaderGraphNode n;
                        n.id = m_nextNodeId++;
                        n.title = snip.name;
                        n.category = snip.category;
                        n.pos[0] = m_contextMenuPos[0];
                        n.pos[1] = m_contextMenuPos[1];

                        if (std::string(snip.name) == "Texture2D Sample")
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "UV", true, ShaderPinType::Float2});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "RGBA", false, ShaderPinType::Float4});
                        }
                        else if (std::string(snip.name) == "Base Color (Vec4)")
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Color Out", false, ShaderPinType::Float4});
                        }
                        else if (std::string(snip.name) == "Multiply (A * B)" || std::string(snip.name) == "Add (A + B)")
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "A", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "B", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Out", false, ShaderPinType::Float4});
                        }
                        else if (std::string(snip.name) == "Lerp (A, B, T)")
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "A", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "B", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "T", true, ShaderPinType::Float});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Out", false, ShaderPinType::Float4});
                        }
                        else if (std::string(snip.name) == "PBR Master Output")
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Albedo", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Metallic", true, ShaderPinType::Float});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Roughness", true, ShaderPinType::Float});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Normal", true, ShaderPinType::Float3});
                        }
                        else
                        {
                            n.pins.push_back(ShaderPin{m_nextPinId++, "In", true, ShaderPinType::Float4});
                            n.pins.push_back(ShaderPin{m_nextPinId++, "Out", false, ShaderPinType::Float4});
                        }

                        m_nodes.push_back(n);
                        m_selectedNodeId = n.id;
                        compileShaderCode();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndChild();

                ImGui::EndPopup();
            }
        }

        void ShaderEditorComposite::renderShaderGraphCanvas()
        {
            ensureImNodesInitialized();
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (ImGui::Button(L("+ 纹理采样", "+ Texture Sample")))
            {
                ShaderGraphNode n;
                n.id = m_nextNodeId++;
                n.title = "Texture2D Sample";
                n.category = ShaderNodeCategory::Texture;
                n.pos[0] = 80.0f;
                n.pos[1] = 80.0f;
                n.pins.push_back(ShaderPin{m_nextPinId++, "UV", true, ShaderPinType::Float2});
                n.pins.push_back(ShaderPin{m_nextPinId++, "RGBA", false, ShaderPinType::Float4});
                m_nodes.push_back(n);
                m_selectedNodeId = n.id;
                compileShaderCode();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("+ 常数颜色", "+ Constant Color")))
            {
                ShaderGraphNode n;
                n.id = m_nextNodeId++;
                n.title = "Uniform Color";
                n.category = ShaderNodeCategory::Constant;
                n.pos[0] = 80.0f;
                n.pos[1] = 200.0f;
                n.pins.push_back(ShaderPin{m_nextPinId++, "Color Out", false, ShaderPinType::Float4});
                m_nodes.push_back(n);
                m_selectedNodeId = n.id;
                compileShaderCode();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("+ 乘法节点", "+ Multiply")))
            {
                ShaderGraphNode n;
                n.id = m_nextNodeId++;
                n.title = "Multiply (A * B)";
                n.category = ShaderNodeCategory::Math;
                n.pos[0] = 260.0f;
                n.pos[1] = 140.0f;
                n.pins.push_back(ShaderPin{m_nextPinId++, "A", true, ShaderPinType::Float4});
                n.pins.push_back(ShaderPin{m_nextPinId++, "B", true, ShaderPinType::Float4});
                n.pins.push_back(ShaderPin{m_nextPinId++, "Out", false, ShaderPinType::Float4});
                m_nodes.push_back(n);
                m_selectedNodeId = n.id;
                compileShaderCode();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("重置默认", "Reset Default")))
            {
                initDefaultShaderNodes();
                compileShaderCode();
            }
            ImGui::SameLine();
            if (ImGui::Button(L("清空", "Clear")))
            {
                clearShaderGraph();
            }

            ImGui::Separator();

            if (!m_imnodesCtx || !m_shaderEditorCtx)
                return;

            const float availH = std::max(160.0f, ImGui::GetContentRegionAvail().y - 105.0f);
            ImGui::BeginChild("ShaderNodeCanvasRegion", ImVec2(0.0f, availH), false);

            ImNodes::EditorContextSet(m_shaderEditorCtx);
            ImNodes::BeginNodeEditor();

            renderShaderNodeItems();

            ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
            const bool isEditorHovered = ImNodes::IsEditorHovered();
            ImNodes::EndNodeEditor();

            handleNodeGraphInteractions(isEditorHovered);
            renderNodeGraphContextMenus();

            ImGui::EndChild();

            ImGui::Separator();
            renderNodePropertyInspector();
        }

    } // namespace MQEditor
} // namespace MQEngine
