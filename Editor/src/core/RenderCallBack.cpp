#include "EditorApplication.h"
using namespace FCT;
#define TEXT(str) (const char*)u8##str
namespace MQEngine {
    void EditorApplication::settingRenderCallBack()
    {
        renderCallBackDispatcher.subscribe<RenderCallBack::SettingUpPass>(
            [this](const RenderCallBack::SettingUpPass& event) {
                auto graph = event.graph;
                graph->addPass(
                    "ImguiPass",
                    EnablePassClear(ClearType::color | ClearType::depthStencil,
                        Vec4(0,0,0,1)),
                    Target("mainWindowColor",global.wnd),
                    Texture("SceneColorTarget"),
                    Texture("PosTarget"),
                    Texture("RetTarget"),
                    DepthStencil("mainWindowDS",global.wnd)
                    );
            }
        );
        renderCallBackDispatcher.subscribe<RenderCallBack::KeepImage>(
            [this](const RenderCallBack::KeepImage& event)
            {
                auto graph = event.graph;

                m_imguiCtx = imguiModule.createContext(global.wnd,global.ctx);
                m_imguiCtx->pass(graph->getPass("ImguiPass"));
                m_imguiCtx->create(ImguiContextCreateFlag::Docking);
                m_imguiCtx->enableChinese();
                m_imguiCtx->addTexture("shadowPos",graph->getImage("PosTarget"));
                m_imguiCtx->addTexture("shadowDepth",graph->getImage("RetTarget"));
                m_imguiCtx->addTexture("lightWorld",graph->getImage("DepthFromLigth0Image"));
                m_imguiCtx->addTexture("SceneView",graph->getImage("SceneColorTarget"));
                global.wnd->swapchain()->subscribe<SwapchainEvent::Recreate>([this](SwapchainEvent::Recreate)
                {
                    m_imguiCtx->updateTexture("shadowPos");
                    m_imguiCtx->updateTexture("shadowDepth");
                    m_imguiCtx->updateTexture("SceneView");
                });
            }
        );
        renderCallBackDispatcher.subscribe<RenderCallBack::SettingSync>(
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
        renderCallBackDispatcher.subscribe<RenderCallBack::SubscribePass>(
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

    void EditorApplication::logicTicker()
    {
        imguiLogicTick();
    }

    void EditorApplication::imguiLogicTick()
    {

        m_imguiCtx->push([this]()
        {
            ImguiContext::createMainDockSpace("MQEngine");
            ImGui::Begin(TEXT("场景视口"));

            auto sceneTextureId = m_imguiCtx->getTexture("SceneView");
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
      } else {
          ImGui::Text(TEXT("场景纹理未找到"));
          ImGui::Text(TEXT("请检查SceneView纹理是否正确创建"));
      }

      ImGui::End();

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
        });
    }
}
