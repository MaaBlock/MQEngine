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
        m_imguiCtx = g_editorGlobal.imguiModule->createContext(g_editorGlobal.wnd,g_editorGlobal.ctx);
        m_imguiCtx->attachPass("ImguiPass");
        m_imguiCtx->create(ImguiContextCreateFlag::Docking);
        g_editorGlobal.imguiContext = m_imguiCtx;
        m_graphView = FCT_NEW(GraphViewInsight,m_imguiCtx);
        m_passGenerator = FCT_NEW(RenderGraphViewer,g_editorGlobal.ctx,g_editorGlobal.wnd);
        m_modelManager = FCT_NEW(ModelManager,g_editorGlobal.dataManager);
        m_editorCameraManager = new EditorCameraManager();
        m_sceneManager = new SceneManager();
        m_scriptManager = new ScriptManager();
        m_sceneEntityViewer = new SceneEntityViewer();
        m_entityInspector = new EntityInspector();
    }

    void UiManager::term()
    {
        delete m_graphView;
        delete m_passGenerator;
        delete m_modelManager;
        delete m_editorCameraManager;
        delete m_sceneManager;
        delete m_scriptManager;
        delete m_sceneEntityViewer;
        delete m_entityInspector;
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
                 Target("mainWindowColor",g_editorGlobal.wnd),
                 Texture("SceneColorTarget"),
                 Texture("PosTarget"),
                 Texture("RetTarget"),
                 DepthStencil("mainWindowDS",g_editorGlobal.wnd)
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
#ifdef FCT_DEBUG
            ImGui::Begin("编辑器内存分析");
            if (ImGui::Button("输出对象"))
            {
                _output_object(fout);
                OutputDebugObject();
            }
            if (ImGui::Button("记录并输出距离上次记录新增"))
            {
                fout << "输出距离上次记录新增:" << std::endl;
                static std::map<std::string,int> objectCount;
                std::map<std::string, int> currentCount;
                auto list = GetDebugObject();
                for (auto& obj : list)
                {
                    currentCount[obj->describe]++;
                }
                for (auto& pair : currentCount)
                {
                    if (pair.second > objectCount[pair.first])
                    {
                        fout << std::oct << "新增:" << pair.first << ", 新增数量:" << pair.second - objectCount[pair.first] << std::endl;
                    }
                }
                objectCount = currentCount;
            }
            ImGui::End();
#endif
            m_graphView->render();
            m_passGenerator->render();
            m_modelManager->render();
            m_sceneManager->render();
            m_scriptManager->render();
            m_sceneEntityViewer->render();
            m_entityInspector->render();
        });
    }

    void UiManager::renderScene()
    {

        ImGui::Begin("场景视口");

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
            ImGui::Text("场景分辨率: %.0fx%.0f", imageWidth, imageHeight);
            ImGui::Text("窗口大小: %.0fx%.0f", windowSize.x, windowSize.y);
        } else
        {
            ImGui::Text("场景纹理未找到");
            ImGui::Text("请检查SceneColorTarget纹理是否正确创建");
        }
        ImGui::End();
    }
}
