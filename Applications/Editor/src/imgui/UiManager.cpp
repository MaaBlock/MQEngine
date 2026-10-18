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
        ZoneScoped();
        m_logWindow = new LogWindow();

        auto& engine = Engine::getInstance();
        Status pluginSt = engine.loadPlugin("ImguiPlugin");
        if (!pluginSt.ok())
        {
            spdlog::error("UiManager: 加载 ImguiPlugin 失败: {}", pluginSt.message());
        }
        if (engine.getPluginManager())
        {
            m_imguiPlugin = engine.getPluginManager()->getPluginAs<ImguiPlugin>("ImguiPlugin");
        }
        if (m_imguiPlugin)
        {
            m_imguiPlugin->setClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
            m_imguiPlugin->syncCallerContext();
            m_imguiCtx = m_imguiPlugin->getImguiContext();
            g_editorGlobal.imguiModule = m_imguiPlugin->getImguiModule();
        }

        setupCustomDarkTheme();
        ImNodes::StyleColorsDark();
        
        g_editorGlobal.imguiContext = m_imguiCtx;
        m_graphView = FCT_NEW(GraphViewInsight,m_imguiCtx);
        m_passGenerator = FCT_NEW(RenderGraphViewer,g_editorGlobal.ctx,g_editorGlobal.wnd);
        m_editorCameraManager = new EditorCameraManager();
        g_editorGlobal.cameraManager = m_editorCameraManager;
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
        if (m_imguiPlugin)
        {
            m_imguiPlugin->applyDefaultDarkTheme();
        }
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
        delete m_logWindow;
        m_logWindow = nullptr;
    }


    void UiManager::registerRenderCallback(
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& callback)
    {
        if (m_imguiPlugin)
        {
            m_imguiPlugin->registerKeepImageCallback([this](RenderGraph* graph)
            {
                m_graphView->keepImage(graph);
            });
        }

        callback.subscribe<RenderCallBack::WindowClose>(
            [this](const RenderCallBack::WindowClose& event)
            {
                g_editorGlobal.dataManager->saveProjectSetting(g_editorGlobal.dataManager->getProjectSetting());
                auto scene = g_editorGlobal.dataManager->getCurrentScene();
                if (scene)
                {
                    Status st = scene->save();
                    if (!st.ok()) {
                        spdlog::error("保存场景失败: {}", st.message());
                    }
                }
            });
    }

    void UiManager::setupDefaultLayout(ImGuiID dockspaceId)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = 0;
        ImGuiID dockRight = 0;
        ImGuiID dockBottom = 0;
        ImGuiID dockBottomLeft = 0;
        ImGuiID dockBottomRight = 0;
        ImGuiID dockLeftBottom = 0;
        ImGuiID dockRightBottom = 0;

        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, &dockLeft, &dockMain);
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.30f, &dockRight, &dockMain);
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, &dockBottom, &dockMain);
        ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Left, 0.55f, &dockBottomLeft, &dockBottomRight);
        ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.38f, &dockLeftBottom, &dockLeft);
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.36f, &dockRightBottom, &dockRight);

        ImGui::DockBuilderDockWindow(TEXT("场景实体查看器"), dockLeft);
        ImGui::DockBuilderDockWindow(TEXT("相机控制"), dockLeftBottom);
#ifdef FCT_DEBUG
        ImGui::DockBuilderDockWindow(TEXT("编辑器内存分析"), dockLeftBottom);
#endif

        ImGui::DockBuilderDockWindow(TEXT("实体检查器"), dockRight);
        ImGui::DockBuilderDockWindow(TEXT("细节"), dockRight);

        ImGui::DockBuilderDockWindow(TEXT("贴图: SceneColorTarget"), dockRightBottom);
        ImGui::DockBuilderDockWindow(TEXT("贴图: PosTarget"), dockRightBottom);
        ImGui::DockBuilderDockWindow(TEXT("贴图: RetTarget"), dockRightBottom);

        ImGui::DockBuilderDockWindow(TEXT("内容浏览器"), dockBottomLeft);
        ImGui::DockBuilderDockWindow(TEXT("脚本管理器"), dockBottomLeft);
        ImGui::DockBuilderDockWindow(TEXT("日志"), dockBottomRight);

        ImGui::DockBuilderDockWindow(TEXT("场景视口"), dockMain);
        ImGui::DockBuilderDockWindow("Shader Graph", dockMain);
        ImGui::DockBuilderDockWindow(TEXT("着色器编辑器"), dockMain);
        ImGui::DockBuilderDockWindow("RenderGraph", dockMain);

        ImGui::DockBuilderFinish(dockspaceId);
        m_focusDefaultTabs = true;
    }

    void UiManager::logicTick()
    {
        ZoneScoped();
        if (!m_imguiCtx)
        {
            return;
        }

        m_imguiCtx->push([this]()
        {
            ZoneScopedN("UiManager::RenderUI");
            if (m_imguiPlugin)
            {
                m_imguiPlugin->syncCallerContext();
            }
            renderMainMenuBar();
            ImGuiID dockspaceId = ImguiContext::createMainDockSpace("MQEngine");
            ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspaceId);
            bool logNotDocked = false;
            if (!m_layoutChecked)
            {
                m_layoutChecked = true;
                ImGuiWindowSettings* logSettings = ImGui::FindWindowSettingsByID(ImHashStr(TEXT("日志")));
                logNotDocked = (!logSettings || logSettings->DockId == 0);
            }
            if (m_needResetLayout || !dockNode || (!dockNode->IsSplitNode() && dockNode->Windows.empty()) || logNotDocked)
            {
                m_needResetLayout = false;
                setupDefaultLayout(dockspaceId);
            }
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
            if (m_logWindow)
                m_logWindow->render();

            if (m_focusDefaultTabs)
            {
                m_focusDefaultTabs = false;
                ImGui::SetWindowFocus(TEXT("相机控制"));
                ImGui::SetWindowFocus(TEXT("贴图: SceneColorTarget"));
                ImGui::SetWindowFocus(TEXT("实体检查器"));
                ImGui::SetWindowFocus(TEXT("内容浏览器"));
                ImGui::SetWindowFocus(TEXT("日志"));
                ImGui::SetWindowFocus(TEXT("场景实体查看器"));
                ImGui::SetWindowFocus(TEXT("场景视口"));
                if (ImGui::GetIO().IniFilename)
                {
                    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
                }
            }
        });
    }

    void UiManager::renderScene()
    {
        ZoneScoped();

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
            if (ImGui::BeginMenu(TEXT("布局")))
            {
                if (ImGui::MenuItem(TEXT("重置默认排版")))
                {
                    m_needResetLayout = true;
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
