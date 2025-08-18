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
        m_imguiCtx->addTexture("shadowPos",graph->getImage("PosTarget"));
        m_imguiCtx->addTexture("shadowDepth",graph->getImage("RetTarget"));
        m_imguiCtx->addTexture("lightWorld",graph->getImage("DepthFromLigth0Image"));
    }

    void GraphViewInsight::render()
    {
        ImGui::Begin(TEXT("阴影位置贴图"));
        auto shadowPosTextureId = m_imguiCtx->getTexture("shadowPos");
        if (shadowPosTextureId) {
            ImVec2 windowSize = ImGui::GetContentRegionAvail();
            float aspectRatio = 800.0f / 600.0f;
            float imageWidth = windowSize.x;
            float imageHeight = imageWidth / aspectRatio;

            if (imageHeight > windowSize.y) {
                imageHeight = windowSize.y;
                imageWidth = imageHeight * aspectRatio;
            }

            ImGui::Image(shadowPosTextureId, ImVec2(imageWidth, imageHeight));
        } else {
            ImGui::Text(TEXT("阴影位置贴图未找到"));
        }
        ImGui::End();

        ImGui::Begin(TEXT("阴影深度贴图"));
        auto shadowDepthTextureId = m_imguiCtx->getTexture("shadowDepth");
        if (shadowDepthTextureId) {
            ImVec2 windowSize = ImGui::GetContentRegionAvail();
            float aspectRatio = 800.0f / 600.0f;
            float imageWidth = windowSize.x;
            float imageHeight = imageWidth / aspectRatio;

            if (imageHeight > windowSize.y) {
                imageHeight = windowSize.y;
                imageWidth = imageHeight * aspectRatio;
            }

            ImGui::Image(shadowDepthTextureId, ImVec2(imageWidth, imageHeight));
        } else {
            ImGui::Text(TEXT("阴影深度贴图未找到"));
        }
        ImGui::End();
        ImGui::Begin(TEXT("光源深度贴图"));
        auto lightDepthTextureId = m_imguiCtx->getTexture("lightWorld");
        if (lightDepthTextureId) {
            ImVec2 windowSize = ImGui::GetContentRegionAvail();
            float imageSize = std::min(windowSize.x, windowSize.y);

            ImGui::Text(TEXT("从光源视角看到的深度图"));
            ImGui::Text(TEXT("分辨率: 2048x2048"));
            ImGui::Separator();

            ImGui::Image(lightDepthTextureId, ImVec2(imageSize, imageSize));

            ImGui::Separator();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TEXT("这是从光源视角渲染的深度图\n用于阴影映射计算"));
            }
        } else {
            ImGui::Text(TEXT("光源深度贴图未找到"));
            ImGui::Text(TEXT("请检查纹理是否正确创建和绑定"));
        }
        ImGui::End();
    }
}
