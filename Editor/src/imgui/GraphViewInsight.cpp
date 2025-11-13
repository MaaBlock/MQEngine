//
// Created by Administrator on 2025/8/18.
//

#include "GraphViewInsight.h"

#define TEXT(str) (const char*)u8##str
namespace MQEngine
{
    GraphViewInsight::GraphViewInsight(FCT::ImguiContext* imguiCtx)
    {
        m_imguiCtx = imguiCtx;
    }

    void GraphViewInsight::keepImage(FCT::RenderGraph* graph)
    {
        /*
        m_imguiCtx->addTexture("shadowPos",graph->getImage("PosTarget"));
        m_imguiCtx->addTexture("shadowDepth",graph->getImage("RetTarget"));
        m_imguiCtx->addTexture("lightWorld",graph->getImage("DepthFromLigth0Image"));
*/
    }

    void GraphViewInsight::render()
    {
        auto textures = m_imguiCtx->getTexturesFromPass();
        for (const auto& [textureName, imagePtr] : textures) {
            std::string windowTitle = std::string("贴图: ") + textureName;

            ImGui::Begin(windowTitle.c_str());

            if (imagePtr) {
                auto textureId = m_imguiCtx->getTexture(textureName);

                if (textureId) {
                    ImVec2 windowSize = ImGui::GetContentRegionAvail();

                    float imageWidth = static_cast<float>(imagePtr->width());
                    float imageHeight = static_cast<float>(imagePtr->height());
                    float aspectRatio = imageWidth / imageHeight;

                    ImGui::Text("贴图名称: %s", textureName.c_str());
                    ImGui::Text("分辨率: %.0fx%.0f", imageWidth, imageHeight);
                    ImGui::Separator();

                    float displayWidth = windowSize.x;
                    float displayHeight = displayWidth / aspectRatio;

                    if (displayHeight > windowSize.y - 80) {
                        displayHeight = windowSize.y - 80;
                        displayWidth = displayHeight * aspectRatio;
                    }

                    ImGui::Image(ImTextureRef(textureId), ImVec2(displayWidth, displayHeight));

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("贴图: %s\n分辨率: %.0fx%.0f", textureName.c_str(), imageWidth, imageHeight);
                    }
                } else {
                    ImGui::Text("无法获取贴图 '%s' 的纹理ID", textureName.c_str());
                }
            } else {
                ImGui::Text("贴图 '%s' 的Image指针为空", textureName.c_str());
            }

            ImGui::End();
        }
    }
}
