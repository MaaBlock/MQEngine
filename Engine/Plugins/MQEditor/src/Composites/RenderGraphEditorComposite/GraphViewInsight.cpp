/**
 * @file GraphViewInsight.cpp
 * @brief GraphViewInsight 渲染目标与中间纹理实时悬浮预览窗口实现
 */
#include "GraphViewInsight.h"
#include "RenderGraphEditorComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/ImGuiComposite/ImGuiComposite.h>
#include <FCT/Context/Image.h>
#include <imgui.h>

#include <algorithm>
#include <string>

namespace MQEngine
{
    namespace MQEditor
    {
        void GraphViewInsight::renderWindows(
            EditorBase::EditorUiComposite* editorUi,
            std::unordered_map<std::string, TextureInsightSettings>& settingsMap)
        {
            FCT::ImguiContext* imguiCtx = nullptr;
            if (editorUi && editorUi->getImGuiComposite())
            {
                imguiCtx = editorUi->getImGuiComposite()->getImguiContext();
            }
            if (!imguiCtx)
            {
                return;
            }

            auto L = [editorUi](const char* zh, const char* en) -> const char* {
                return editorUi ? editorUi->tr(zh, en) : zh;
            };

            auto textures = imguiCtx->getTexturesFromPass();
            for (const auto& [textureName, imagePtr] : textures)
            {
                std::string winTitle = std::string(L("贴图预览: ", "Texture Preview: ")) + textureName + "###TexInsight_" + textureName;
                if (ImGui::Begin(winTitle.c_str(), nullptr))
                {
                    if (imagePtr)
                    {
                        auto textureId = imguiCtx->getTexture(textureName);
                        if (textureId)
                        {
                            auto& settings = settingsMap[textureName];

                            ImGui::Text("%s: %s", L("贴图", "Texture"), textureName.c_str());
                            float imgW = static_cast<float>(imagePtr->width());
                            float imgH = static_cast<float>(imagePtr->height());
                            ImGui::Text("%s: %.0fx%.0f", L("分辨率", "Resolution"), imgW, imgH);
                            ImGui::Separator();

                            // 通道过滤与曝光滑块
                            ImGui::SetNextItemWidth(120.0f);
                            const char* channelModes[] = {"RGBA", "R", "G", "B", "A"};
                            ImGui::Combo(L("通道", "Channel"), &settings.channelMode, channelModes, IM_ARRAYSIZE(channelModes));
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(110.0f);
                            ImGui::SliderFloat(L("曝光", "Exposure"), &settings.exposure, 0.1f, 5.0f, "%.1fx");

                            ImVec4 tint(settings.exposure, settings.exposure, settings.exposure, 1.0f);
                            if (settings.channelMode == 1) tint = ImVec4(settings.exposure, 0.0f, 0.0f, 1.0f);
                            else if (settings.channelMode == 2) tint = ImVec4(0.0f, settings.exposure, 0.0f, 1.0f);
                            else if (settings.channelMode == 3) tint = ImVec4(0.0f, 0.0f, settings.exposure, 1.0f);
                            else if (settings.channelMode == 4) tint = ImVec4(settings.exposure, settings.exposure, settings.exposure, 1.0f);

                            ImVec2 avail = ImGui::GetContentRegionAvail();
                            float aspect = (imgH > 0) ? (imgW / imgH) : 1.777f;
                            float dispW = avail.x;
                            float dispH = dispW / aspect;
                            if (dispH > avail.y - 10.0f)
                            {
                                dispH = std::max(avail.y - 10.0f, 60.0f);
                                dispW = dispH * aspect;
                            }

                            ImGui::ImageWithBg(ImTextureRef(textureId), ImVec2(dispW, dispH), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s: %s\n%s: %.0fx%.0f",
                                    L("贴图", "Texture"), textureName.c_str(),
                                    L("分辨率", "Resolution"), imgW, imgH);
                            }
                        }
                        else
                        {
                            ImGui::TextDisabled("%s '%s'", L("无法获取贴图的纹理ID:", "No texture ID for:"), textureName.c_str());
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("%s '%s'", L("Image指针为空:", "Image pointer is null:"), textureName.c_str());
                    }
                }
                ImGui::End();
            }
        }

        void RenderGraphEditorComposite::renderTextureInsightWindows()
        {
            GraphViewInsight::renderWindows(m_editorUiComposite, m_textureInsightSettings);
        }

    } // namespace MQEditor
} // namespace MQEngine
