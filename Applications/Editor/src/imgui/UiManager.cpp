//
// Created by Administrator on 2025/8/18.
//

#include "UiManager.h"
#include <imnodes.h>
#include "../AssetsManager/ShaderSnippetBrowser.h"
#include "../AssetsManager/ShaderGraphBrowser.h"
#include "../SceneViewer/Inspector.h"

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

        setupCustomDarkTheme();
        ImNodes::StyleColorsDark();
        
        g_editorGlobal.imguiContext = m_imguiCtx;
        m_graphView = FCT_NEW(GraphViewInsight,m_imguiCtx);
        m_passGenerator = FCT_NEW(RenderGraphViewer,g_editorGlobal.ctx,g_editorGlobal.wnd);
        // m_modelManager = FCT_NEW(ModelManager,g_editorGlobal.dataManager); // Moved to ContentBrowser
        m_editorCameraManager = new EditorCameraManager();
        g_editorGlobal.cameraManager = m_editorCameraManager;
        // m_sceneManager = new SceneManager(); // Moved to ContentBrowser
        m_scriptManager = new ScriptManager();
        m_sceneEntityViewer = new SceneEntityViewer();
        m_entityInspector = new EntityInspector();
        m_inspector = new Inspector();
        g_editorGlobal.inspector = m_inspector;
        m_shaderEditor = new ShaderEditor();
        m_shaderGraph = new ShaderGraph();
        m_contentBrowser = new ContentBrowser();
        g_editorGlobal.contentBrowser = m_contentBrowser;

        m_contentBrowser->registerProvider<SceneManager>();
        m_contentBrowser->registerProvider<ModelManager>();
        m_contentBrowser->registerProvider<ShaderSnippetBrowser>();
        m_contentBrowser->registerProvider<ShaderGraphBrowser>(m_shaderGraph);
    }

    void UiManager::setupCustomDarkTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.07f, 0.07f, 0.07f, 0.94f);
        colors[ImGuiCol_Border]                 = ImVec4(0.25f, 0.25f, 0.25f, 0.88f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.07f, 0.07f, 0.07f, 0.75f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
        colors[ImGuiCol_Button]                 = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
        colors[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

        // Match active tab color to ChildBg/WindowBg for seamless look
        colors[ImGuiCol_TabActive]              = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

        style.WindowPadding                     = ImVec2(6.00f, 4.00f);
        style.FramePadding                      = ImVec2(6.00f, 4.00f);
        style.CellPadding                       = ImVec2(6.00f, 6.00f);
        style.ItemSpacing                       = ImVec2(6.00f, 6.00f);
        style.ItemInnerSpacing                  = ImVec2(6.00f, 6.00f);
        style.TouchExtraPadding                 = ImVec2(0.00f, 0.00f);
        style.IndentSpacing                     = 25;
        style.ScrollbarSize                     = 15;
        style.GrabMinSize                       = 10;
        style.WindowBorderSize                  = 1;
        style.ChildBorderSize                   = 1;
        style.PopupBorderSize                   = 1;
        style.FrameBorderSize                   = 1;
        style.TabBorderSize                     = 1;
        style.WindowRounding                    = 6.0f;
        style.ChildRounding                     = 6.0f;
        style.FrameRounding                     = 4.0f;
        style.PopupRounding                     = 4.0f;
        style.ScrollbarRounding                 = 9;
        style.GrabRounding                      = 4.0f;
        style.LogSliderDeadzone                 = 4;
        style.TabRounding                       = 4.0f;
    }

    void UiManager::term()
    {
        delete m_graphView;
        delete m_passGenerator;
        // delete m_modelManager; // Handled by ContentBrowser
        delete m_editorCameraManager;
        // delete m_sceneManager; // Handled by ContentBrowser
        delete m_scriptManager;
        delete m_sceneEntityViewer;
        delete m_entityInspector;
        delete m_inspector;
        delete m_shaderEditor;
        delete m_shaderGraph;
        delete m_contentBrowser;
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

        callback.subscribe<RenderCallBack::WindowClose>(
            [this](const RenderCallBack::WindowClose& event)
            {
                g_editorGlobal.dataManager->saveProjectSetting(g_editorGlobal.dataManager->getProjectSetting());
                auto scene = g_editorGlobal.dataManager->getCurrentScene();
                if (scene)
                {
                    scene->save();
                }
            });
    }

    void UiManager::logicTick()
    {

        m_imguiCtx->push([this]()
        {
            renderMainMenuBar();
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
            // m_modelManager->render(); // Handled by ContentBrowser
            // m_sceneManager->render(); // Handled by ContentBrowser
            m_scriptManager->render();
            m_sceneEntityViewer->render();
            m_entityInspector->render();
            m_inspector->render();
            m_shaderEditor->render();
            m_shaderGraph->render();
            m_contentBrowser->render();
        });
    }

    void UiManager::renderScene()
    {

        ImGui::Begin("场景视口");
        
        renderCameraWindow();
        
        ImGui::Separator();

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

            ImGui::Image(ImTextureRef(sceneTextureId), ImVec2(imageWidth, imageHeight));

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

    void UiManager::renderMainMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu(TEXT("文件")))
            {
                if (ImGui::MenuItem(TEXT("保存项目设置"), "Ctrl+S"))
                {
                    g_editorGlobal.dataManager->saveProjectSetting(g_editorGlobal.dataManager->getProjectSetting());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TEXT("退出"), "Alt+F4"))
                {

                }
                ImGui::EndMenu();
            }
            if (g_engineGlobal.isRunning)
            {
                if (ImGui::MenuItem(TEXT("停止运行")))
                {
                    g_engineGlobal.isRunning = false;
                    g_engineGlobal.systemManager->requestSetSystemEnabled("ScriptCacheSystem", false);
                }
            }
            else
            {
                if (ImGui::MenuItem(TEXT("开始运行")))
                {
                    g_engineGlobal.isRunning = true;
                    g_engineGlobal.systemManager->requestSetSystemEnabled("ScriptCacheSystem", true);
                }
            }

            ImGui::EndMainMenuBar();
        }
    }

    void UiManager::renderCameraWindow()
    {
        if (ImGui::Begin("相机控制"))
        {
        
        // 检查编辑器相机是否存在且活跃
        auto editorEntity = m_editorCameraManager->getEditorCameraEntity();
        auto editorRegistry = m_editorCameraManager->getEditorRegistry();
        bool usingEditorCamera = false;
        
        if (editorEntity != entt::null && editorRegistry && 
            editorRegistry->valid(editorEntity) && 
            editorRegistry->all_of<CameraComponent>(editorEntity))
        {
            auto& editorCamera = editorRegistry->get<CameraComponent>(editorEntity);
            usingEditorCamera = editorCamera.active;
        }
        
        // 编辑器相机选项
        if (ImGui::RadioButton("编辑器相机", usingEditorCamera))
        {
            if (!usingEditorCamera && editorEntity != entt::null && editorRegistry)
            {
                // 切换到编辑器相机
                if (g_engineGlobal.cameraSystem)
                 {
                     g_engineGlobal.cameraSystem->setActiveCamera(editorRegistry, editorEntity);
                 }
            }
        }
        
        // 场景相机选项
        ImGui::Separator();
        ImGui::Text("场景相机:");
        
        // 收集所有场景相机
        struct SceneCameraInfo {
            entt::entity entity;
            entt::registry* registry;
            std::string name;
            bool active;
        };
        
        std::vector<SceneCameraInfo> sceneCameras;
        auto registries = g_editorGlobal.dataManager->currentRegistries();
        int cameraIndex = 0;
        
        for (auto& registry : registries)
        {
            auto view = registry->view<CameraComponent>();
            for (auto entity : view)
            {
                auto& camera = view.get<CameraComponent>(entity);
                SceneCameraInfo info;
                info.entity = entity;
                info.registry = registry;
                info.active = camera.active;
                
                // 尝试获取实体名称
                if (registry->all_of<NameTag>(entity))
                {
                    auto& nameTag = registry->get<NameTag>(entity);
                    info.name = nameTag.name + " (相机 " + std::to_string(cameraIndex) + ")";
                }
                else
                {
                    info.name = "相机 " + std::to_string(cameraIndex);
                }
                
                sceneCameras.push_back(info);
                cameraIndex++;
            }
        }
        
        if (sceneCameras.empty())
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  无场景相机");
        }
        else
        {
            for (const auto& cameraInfo : sceneCameras)
            {
                bool isSelected = cameraInfo.active && !usingEditorCamera;
                if (ImGui::RadioButton(cameraInfo.name.c_str(), isSelected))
                {
                    if (!isSelected)
                    {
                        // 切换到这个场景相机
                        if (g_engineGlobal.cameraSystem)
                         {
                             g_engineGlobal.cameraSystem->setActiveCamera(cameraInfo.registry, cameraInfo.entity);
                         }
                    }
                }
            }
        }
        
        // 显示当前相机信息
        ImGui::Separator();
        if (usingEditorCamera)
        {
            ImGui::Text("当前使用: 编辑器相机");
        }
        else
        {
            // 找到当前活跃的场景相机
            std::string activeCameraName = "未知场景相机";
            for (const auto& cameraInfo : sceneCameras)
            {
                if (cameraInfo.active)
                {
                    activeCameraName = cameraInfo.name;
                    break;
                }
            }
            ImGui::Text(("当前使用: " + activeCameraName).c_str());
        }
        }
        ImGui::End();
    }
}
