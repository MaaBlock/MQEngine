//
// Created by Administrator on 2025/8/18.
//

#include "UiManager.h"
#include <imnodes.h>

#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine
{
    void UiManager::init()
    {
        ImNodes::CreateContext();
        m_imguiCtx = g_global.imguiModule->createContext(g_global.wnd,g_global.ctx);
        m_imguiCtx->attachPass("ImguiPass");
        m_imguiCtx->create(ImguiContextCreateFlag::Docking);
        m_graphView = new GraphViewInsight(m_imguiCtx);
        m_passGenerator = new PassGenerator();
    }

    void UiManager::term()
    {
        delete m_graphView;
        ImNodes::DestroyContext();
    }


    void UiManager::registerRenderCallback(
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& callback)
    {
        callback.subscribe<RenderCallBack::SettingUpPass>(
        [this](const RenderCallBack::SettingUpPass& event) {
             auto graph = event.graph;
             graph->addPass(
                 "ImguiPass",
                 EnablePassClear(ClearType::color | ClearType::depthStencil,
                     Vec4(0,0,0,1)),
                 Target("mainWindowColor",g_global.wnd),
                 Texture("SceneColorTarget"),
                 Texture("PosTarget"),
                 Texture("RetTarget"),
                 DepthStencil("mainWindowDS",g_global.wnd)
                 );
         });
        callback.subscribe<RenderCallBack::KeepImage>(
            [this](const RenderCallBack::KeepImage& event)
            {
                auto graph = event.graph;

                m_imguiCtx->enableChinese();
                //m_imguiCtx->addTexture("SceneView",graph->getImage("SceneColorTarget"));
                m_graphView->keepImage(graph);
            }
        );
        callback.subscribe<RenderCallBack::SettingSync>(
            [this](const RenderCallBack::SettingSync& event)
            {
                auto& graph = event.graph;
                graph["syncImgui"] = {
                    [this]()
                    {
                        m_imguiCtx->swapBuffer();
                    },
                    {},
                    {}
                };
            });
        callback.subscribe<RenderCallBack::SubscribePass>(
            [this](const RenderCallBack::SubscribePass& event)
            {
                auto graph = event.graph;
                graph->subscribe("ImguiPass",[this](PassSubmitEvent env)
                {
                    auto cmdBuf = env.cmdBuf;
                    m_imguiCtx->submit(cmdBuf);
                });
            });
    }

    void UiManager::logicTick()
    {
        m_imguiCtx->push([this]()
        {
            ImguiContext::createMainDockSpace("MQEngine");
            renderScene();
            m_graphView->render();
            m_passGenerator->render();
        });
    }

    void UiManager::renderScene()
    {

        ImGui::Begin(TEXT("场景视口"));

        auto sceneTextureId = m_imguiCtx->getTexture("SceneColorTarget");
        if (sceneTextureId) {
            ImVec2 windowSize = ImGui::GetContentRegionAvail();

            // 保持场景的宽高比（假设是800x600）
            float aspectRatio = 800.0f / 600.0f;
            float imageWidth = windowSize.x;
            float imageHeight = imageWidth / aspectRatio;

            // 如果高度超出窗口，则按高度缩放
            if (imageHeight > windowSize.y) {
                imageHeight = windowSize.y;
                imageWidth = imageHeight * aspectRatio;
            }

            // 居中显示
            float offsetX = (windowSize.x - imageWidth) * 0.5f;
            float offsetY = (windowSize.y - imageHeight) * 0.5f;

            if (offsetX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            if (offsetY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);

            ImGui::Image(sceneTextureId, ImVec2(imageWidth, imageHeight));

            // 显示一些场景信息
            ImGui::Separator();
            ImGui::Text(TEXT("场景分辨率: %.0fx%.0f"), imageWidth, imageHeight);
            ImGui::Text(TEXT("窗口大小: %.0fx%.0f"), windowSize.x, windowSize.y);
        } else
        {
            ImGui::Text(TEXT("场景纹理未找到"));
            ImGui::Text(TEXT("请检查SceneColorTarget纹理是否正确创建"));
        }
        ImGui::End();
    }
}
