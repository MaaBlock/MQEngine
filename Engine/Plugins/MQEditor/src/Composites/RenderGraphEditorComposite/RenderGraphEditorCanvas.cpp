/**
 * @file RenderGraphEditorCanvas.cpp
 * @brief ImNodes 渲染图画布交互、PassNode 与 ImageNode 节点内联控件绘制及右键上下文菜单实现
 */
#include "RenderGraphEditorComposite.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace MQEngine
{
    namespace MQEditor
    {
        void RenderGraphEditorComposite::ensureImNodesInitialized()
        {
            ImGuiContext* imguiCtx = ImGui::GetCurrentContext();
            if (!imguiCtx)
                return;
            ImNodes::SetImGuiContext(imguiCtx);

            if (!m_imnodesCtx)
            {
                m_imnodesCtx = ImNodes::CreateContext();
                ImNodes::SetCurrentContext(m_imnodesCtx);
                ImNodes::StyleColorsDark();
                m_context = ImNodes::EditorContextCreate();
                ImNodes::EditorContextSet(m_context);

                for (const auto& [id, pass] : m_passes)
                {
                    ImNodes::SetNodeGridSpacePos(pass.id, ImVec2(pass.pos[0], pass.pos[1]));
                }
                for (const auto& [id, img] : m_images)
                {
                    ImNodes::SetNodeGridSpacePos(img.id, ImVec2(img.pos[0], img.pos[1]));
                }
            }
            else
            {
                ImNodes::SetCurrentContext(m_imnodesCtx);
                if (m_context)
                {
                    ImNodes::EditorContextSet(m_context);
                }
            }
        }

        void RenderGraphEditorComposite::renderNodeGraphCanvas()
        {
            ensureImNodesInitialized();

            ImNodes::BeginNodeEditor();

            // 1. 渲染所有 Pass 节点
            for (auto& [id, pass] : m_passes)
            {
                if (m_searchFilter[0] != '\0' && pass.name.find(m_searchFilter) == std::string::npos)
                {
                    continue;
                }
                renderPassNode(pass);
            }

            // 2. 渲染所有 Image / ResourceGroup 节点
            for (auto& [id, image] : m_images)
            {
                if (m_searchFilter[0] != '\0' && image.name.find(m_searchFilter) == std::string::npos)
                {
                    continue;
                }
                renderImageNode(image);
            }

            // 3. 渲染所有连线
            for (const auto& [pinId, link] : m_passOutputlinks)
            {
                ImNodes::Link(link.id, link.startPinId, link.endPinId);
            }
            for (const auto& [pinId, link] : m_passInputLinks)
            {
                ImNodes::Link(link.id, link.startPinId, link.endPinId);
            }

            const bool isEditorHovered = ImNodes::IsEditorHovered();
            ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
            ImNodes::EndNodeEditor();

            // 4. 交互连线形成与断开事件
            int startAttr = 0;
            int endAttr = 0;
            if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
            {
                addLink(startAttr, endAttr);
            }

            int droppedLinkAttr = 0;
            if (ImNodes::IsLinkDropped(&droppedLinkAttr, true))
            {
                if (m_pinInfoMap.count(droppedLinkAttr))
                {
                    auto info = m_pinInfoMap[droppedLinkAttr];
                    if (m_passes.count(info.nodeId))
                    {
                        m_passOutputlinks.erase(droppedLinkAttr);
                        if (info.pinType == "texture")
                        {
                            if (m_passInputLinks.count(droppedLinkAttr))
                            {
                                m_passes[info.nodeId].removeTexturePin(droppedLinkAttr);
                                m_passInputLinks.erase(droppedLinkAttr);
                                m_pinInfoMap.erase(droppedLinkAttr);
                            }
                        }
                    }
                }
            }

            int destroyedLinkId = 0;
            if (ImNodes::IsLinkDestroyed(&destroyedLinkId))
            {
                for (auto it = m_passOutputlinks.begin(); it != m_passOutputlinks.end(); ++it)
                {
                    if (it->second.id == destroyedLinkId)
                    {
                        m_passOutputlinks.erase(it);
                        break;
                    }
                }
                for (auto it = m_passInputLinks.begin(); it != m_passInputLinks.end(); ++it)
                {
                    if (it->second.id == destroyedLinkId)
                    {
                        int pinId = it->first;
                        if (m_pinInfoMap.count(pinId))
                        {
                            auto info = m_pinInfoMap[pinId];
                            if (m_passes.count(info.nodeId))
                            {
                                m_passes[info.nodeId].removeTexturePin(pinId);
                            }
                            m_pinInfoMap.erase(pinId);
                        }
                        m_passInputLinks.erase(it);
                        break;
                    }
                }
            }

            // 5. 选中节点交互检测
            int hoveredNodeId = -1;
            if (ImNodes::IsNodeHovered(&hoveredNodeId) && ImGui::IsMouseClicked(0))
            {
                m_selectedNodeId = hoveredNodeId;
            }

            // 6. 右键菜单交互
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isEditorHovered)
            {
                if (!ImNodes::IsNodeHovered(&hoveredNodeId))
                {
                    ImGui::OpenPopup("AddNodePopup");
                }
                else
                {
                    m_contextMenuNodeId = hoveredNodeId;
                    ImGui::OpenPopup("NodeContextMenu");
                }
            }

            if (ImGui::BeginPopup("AddNodePopup"))
            {
                if (ImGui::MenuItem(tr("添加Pass节点", "Add Pass Node")))
                {
                    newPassNode();
                }
                if (ImGui::MenuItem(tr("添加Image/ResourceGroup节点", "Add Image/ResourceGroup Node")))
                {
                    newImageNode();
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("NodeContextMenu"))
            {
                if (m_passes.count(m_contextMenuNodeId))
                {
                    ImGui::Text("%s: %s", tr("Pass节点", "Pass Node"), m_passes[m_contextMenuNodeId].name.c_str());
                }
                else if (m_images.count(m_contextMenuNodeId))
                {
                    ImGui::Text("%s: %s", tr("Image节点", "Image Node"), m_images[m_contextMenuNodeId].name.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("删除节点", "Delete Node")))
                {
                    deleteNode(m_contextMenuNodeId);
                    m_contextMenuNodeId = -1;
                }
                ImGui::EndPopup();
            }
        }

        void RenderGraphEditorComposite::renderPassNode(PassNode& pass)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

            ImNodes::BeginNode(pass.id);

            // 标题栏：可编辑 Pass 名称输入框
            ImNodes::BeginNodeTitleBar();
            char nameBuffer[256];
            snprintf(nameBuffer, sizeof(nameBuffer), "%s", pass.name.c_str());
            float titleWidth = std::max(ImGui::CalcTextSize(nameBuffer).x + 30.0f, 130.0f);
            ImGui::SetNextItemWidth(titleWidth);
            if (ImGui::InputText("##PassName", nameBuffer, sizeof(nameBuffer)))
            {
                pass.name = nameBuffer;
            }
            ImNodes::EndNodeTitleBar();

            const float contentWidth = std::max(titleWidth, 160.0f);
            ImGui::PushItemWidth(contentWidth);

            // 1. 动态自动增长 Texture 输入引脚
            for (int texturePin : pass.texturePins)
            {
                ImNodes::BeginInputAttribute(texturePin);
                ImGui::Text("%s", tr("Texture (输入纹理)", "Texture (Input)"));
                ImNodes::EndInputAttribute();
            }

            ImGui::Separator();

            // 2. Clear 设置区域
            ImGui::Checkbox(tr("开启Clear", "Enable Clear"), &pass.enableClear);
            if (pass.enableClear)
            {
                ImGui::Checkbox(tr("Clear Target", "Clear Target"), &pass.enableClearTarget);
                if (pass.enableClearTarget)
                {
                    ImGui::ColorEdit4(tr("颜色", "Color"), pass.clearColor, ImGuiColorEditFlags_NoInputs);
                }

                ImGui::Checkbox(tr("Clear Depth", "Clear Depth"), &pass.enableClearDepth);
                if (pass.enableClearDepth)
                {
                    ImGui::SliderFloat(tr("深度值", "Depth Value"), &pass.clearDepth, 0.0f, 1.0f, "%.2f");
                }

                ImGui::Checkbox(tr("Clear Stencil", "Clear Stencil"), &pass.enableClearStencil);
                if (pass.enableClearStencil)
                {
                    ImGui::SliderInt(tr("模板值", "Stencil Value"), &pass.clearStencil, 0, 255);
                }
            }

            ImGui::Separator();

            // 3. 输出引脚：Target 0..8
            for (int i = 0; i < 9; ++i)
            {
                auto& targetConfig = pass.targetDesc[i];
                uint32_t pinId = pass.targetPinId(i);

                ImNodes::BeginOutputAttribute(pinId);
                ImGui::Dummy(ImVec2(50, 0));
                ImGui::SameLine();
                ImGui::Text("Target %d", i);
                ImNodes::EndOutputAttribute();

                ImGui::PushID(i);
                ImGui::Checkbox(tr("配置属性", "Configure"), &targetConfig.enabled);
                if (targetConfig.enabled)
                {
                    ImGui::Checkbox(tr("直连窗口 (isWindow)", "Direct Window (isWindow)"), &targetConfig.isWindow);
                    if (targetConfig.isWindow)
                    {
                        targetConfig.useCustomFormat = false;
                        targetConfig.useCustomSize = false;
                    }
                    else
                    {
                        ImGui::Checkbox(tr("自定义格式", "Custom Format"), &targetConfig.useCustomFormat);
                        if (targetConfig.useCustomFormat)
                        {
                            const char* colorFormats[] = {
                                "R8G8B8A8_UNORM",
                                "R8G8B8A8_SRGB",
                                "B8G8R8A8_UNORM",
                                "B8G8R8A8_SRGB",
                                "R16G16B16A16_SFLOAT",
                                "R32G32B32A32_SFLOAT",
                                "R16G16_SFLOAT",
                                "R32G32_SFLOAT",
                                "R16_SFLOAT",
                                "R32_SFLOAT",
                                "R8_UNORM",
                                "R16_UNORM",
                                "R8G8_UNORM",
                                "R16G16_UNORM"
                            };

                            int currentFormatIndex = 0;
                            for (int j = 0; j < IM_ARRAYSIZE(colorFormats); ++j)
                            {
                                if (targetConfig.format == colorFormats[j])
                                {
                                    currentFormatIndex = j;
                                    break;
                                }
                            }

                            ImGui::SetNextItemWidth(140.0f);
                            if (ImGui::Combo("##format", &currentFormatIndex, colorFormats, IM_ARRAYSIZE(colorFormats)))
                            {
                                targetConfig.format = colorFormats[currentFormatIndex];
                            }
                        }

                        ImGui::Checkbox(tr("自定义尺寸", "Custom Size"), &targetConfig.useCustomSize);
                        if (targetConfig.useCustomSize)
                        {
                            ImGui::SetNextItemWidth(65.0f);
                            ImGui::InputInt("W##w", &targetConfig.customWidth);
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(65.0f);
                            ImGui::InputInt("H##h", &targetConfig.customHeight);
                        }
                    }
                }
                ImGui::PopID();
            }

            // 4. 深度模板输出引脚
            uint32_t depthPinId = pass.depthStencilPinId();
            ImNodes::BeginOutputAttribute(depthPinId);
            ImGui::Dummy(ImVec2(50, 0));
            ImGui::SameLine();
            ImGui::Text("DepthStencil");
            ImNodes::EndOutputAttribute();

            auto& depthConfig = pass.depthStencilDesc;
            ImGui::Checkbox(tr("配置Depth属性", "Configure Depth"), &depthConfig.enabled);
            if (depthConfig.enabled)
            {
                ImGui::Checkbox(tr("窗口##depth", "Window##depth"), &depthConfig.isWindow);
                if (depthConfig.isWindow)
                {
                    depthConfig.useCustomFormat = false;
                    depthConfig.useCustomSize = false;
                }
                else
                {
                    ImGui::Checkbox(tr("自定义格式##depth", "Custom Format##depth"), &depthConfig.useCustomFormat);
                    if (depthConfig.useCustomFormat)
                    {
                        const char* depthFormats[] = {
                            "D32_SFLOAT",
                            "D32_SFLOAT_S8_UINT",
                            "D24_UNORM_S8_UINT",
                            "D16_UNORM",
                            "D16_UNORM_S8_UINT"
                        };

                        int currentDepthFormatIndex = 0;
                        for (int j = 0; j < IM_ARRAYSIZE(depthFormats); ++j)
                        {
                            if (depthConfig.format == depthFormats[j])
                            {
                                currentDepthFormatIndex = j;
                                break;
                            }
                        }

                        ImGui::SetNextItemWidth(140.0f);
                        if (ImGui::Combo("##depthformat", &currentDepthFormatIndex, depthFormats, IM_ARRAYSIZE(depthFormats)))
                        {
                            depthConfig.format = depthFormats[currentDepthFormatIndex];
                        }
                    }

                    ImGui::Checkbox(tr("自定义尺寸##depth", "Custom Size##depth"), &depthConfig.useCustomSize);
                    if (depthConfig.useCustomSize)
                    {
                        ImGui::SetNextItemWidth(65.0f);
                        ImGui::InputInt("W##dw", &depthConfig.customWidth);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(65.0f);
                        ImGui::InputInt("H##dh", &depthConfig.customHeight);
                    }
                }
            }

            ImGui::PopItemWidth();
            ImNodes::EndNode();
            ImGui::PopStyleVar(2);
        }

        void RenderGraphEditorComposite::renderImageNode(ImageNode& image)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

            ImNodes::BeginNode(image.id);

            // 标题栏：可编辑 Image 名称输入框
            ImNodes::BeginNodeTitleBar();
            char nameBuffer[256];
            snprintf(nameBuffer, sizeof(nameBuffer), "%s", image.name.c_str());
            float titleWidth = std::max(ImGui::CalcTextSize(nameBuffer).x + 30.0f, 110.0f);
            ImGui::SetNextItemWidth(titleWidth);
            if (ImGui::InputText("##ImageName", nameBuffer, sizeof(nameBuffer)))
            {
                image.name = nameBuffer;
            }
            ImNodes::EndNodeTitleBar();

            ImGui::PushItemWidth(std::max(titleWidth - 20.0f, 90.0f));

            // 输入插槽：Target 与 DepthStencil
            uint32_t targetInputPinId = image.targetPinId();
            ImNodes::BeginInputAttribute(targetInputPinId);
            ImGui::Text("Target");
            ImNodes::EndInputAttribute();

            uint32_t depthInputPinId = image.depthStencilPinId();
            ImNodes::BeginInputAttribute(depthInputPinId);
            ImGui::Text("DepthStencil");
            ImNodes::EndInputAttribute();

            ImGui::Separator();

            // 图像元数据展示
            ImGui::TextDisabled("%s", image.format.c_str());
            ImGui::TextDisabled("%d x %d", image.width, image.height);

            // 输出插槽：Texture
            uint32_t texturePinId = image.texturePinId();
            ImNodes::BeginOutputAttribute(texturePinId);
            ImGui::Dummy(ImVec2(50, 0));
            ImGui::SameLine();
            ImGui::Text("Texture");
            ImNodes::EndOutputAttribute();

            ImGui::PopItemWidth();
            ImNodes::EndNode();
            ImGui::PopStyleVar(2);
        }

    } // namespace MQEditor
} // namespace MQEngine
