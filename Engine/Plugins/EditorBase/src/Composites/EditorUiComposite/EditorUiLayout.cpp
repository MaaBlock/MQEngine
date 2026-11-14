/**
 * @file EditorUiLayout.cpp
 * @brief 编辑器 DockSpace 停靠布局构建、工作区预设切换、主菜单栏与已注册面板调度实现
 */
#include "EditorUiComposite.h"

#include <MQEngine/host/logic/LogicHost.h>
#include <imgui_internal.h>

#include <cstdio>
#include <unordered_map>

namespace MQEngine
{
    namespace EditorBase
    {
        void EditorUiComposite::applyWorkspacePreset(int presetIndex)
        {
            for (auto& entry : m_dockWindows)
            {
                if (presetIndex == 0) // 标准默认布局（次级专业编辑器默认不打开）
                {
                    const bool isSecondaryEditor =
                        (entry.name.find("着色器与材质编辑器") != std::string::npos ||
                         entry.name.find("管线编辑") != std::string::npos ||
                         entry.name.find("HTML UI 设计器") != std::string::npos);
                    entry.isOpen = !isSecondaryEditor;
                }
                else if (presetIndex == 1) // 场景专注模式
                {
                    entry.isOpen = (entry.name.find("视口") != std::string::npos ||
                                    entry.name.find("层级") != std::string::npos ||
                                    entry.name.find("检视器") != std::string::npos ||
                                    entry.name.find("相机") != std::string::npos);
                }
                else if (presetIndex == 2) // 着色器与管线
                {
                    entry.isOpen = (entry.name.find("着色器") != std::string::npos ||
                                    entry.name.find("管线编辑") != std::string::npos ||
                                    entry.name.find("视口") != std::string::npos ||
                                    entry.name.find("性能") != std::string::npos ||
                                    entry.name.find("日志") != std::string::npos);
                }
                else if (presetIndex == 3) // HTML UI 设计器模式
                {
                    entry.isOpen = (entry.name.find("HTML") != std::string::npos ||
                                    entry.name.find("资产") != std::string::npos ||
                                    entry.name.find("视口") != std::string::npos);
                }
            }
            requestResetLayout();
            addLog(LogEntry::Level::Info, "已切换工作区预设布局 #" + std::to_string(presetIndex), "Layout");
        }

        void EditorUiComposite::renderDockSpace()
        {
            ZoneScoped();
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);

            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                          ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

            ImGui::Begin("MQEngine_Master_DockSpace", nullptr, windowFlags);
            ImGui::PopStyleVar(3);

            ImGuiID dockspaceId = ImGui::GetID("MQEngine_DockSpace");
            ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockFlags);

            if (m_needResetLayout || !m_dockLayoutBuilt)
            {
                buildInitialDockLayout(dockspaceId);
                m_needResetLayout = false;
                m_dockLayoutBuilt = true;
            }

            ImGui::End();
        }

        void EditorUiComposite::buildInitialDockLayout(uint32_t dockspaceId)
        {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
            ImGuiID dockCenter = dockMain;

            // 停靠内置面板
            ImGui::DockBuilderDockWindow("###控制台日志 (Console Log)", dockBottom);
            ImGui::DockBuilderDockWindow("###Profiler", dockBottom);
            ImGui::DockBuilderDockWindow("###Connection", dockBottom);

            // 停靠动态注册的业务面板
            for (const auto& entry : m_dockWindows)
            {
                const std::string dockId = "###" + entry.name;
                switch (entry.slot)
                {
                case EditorDockSlot::Center:
                    ImGui::DockBuilderDockWindow(dockId.c_str(), dockCenter);
                    break;
                case EditorDockSlot::Left:
                    ImGui::DockBuilderDockWindow(dockId.c_str(), dockLeft);
                    break;
                case EditorDockSlot::Right:
                    ImGui::DockBuilderDockWindow(dockId.c_str(), dockRight);
                    break;
                case EditorDockSlot::Bottom:
                    ImGui::DockBuilderDockWindow(dockId.c_str(), dockBottom);
                    break;
                case EditorDockSlot::Top:
                    ImGui::DockBuilderDockWindow(dockId.c_str(), dockMain);
                    break;
                }
            }

            ImGui::DockBuilderFinish(dockspaceId);
        }

        void EditorUiComposite::renderMainMenuBar()
        {
            ZoneScoped();
            // 全局快捷键支持
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && !io.WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    m_commandStack.undo();
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                {
                    m_commandStack.redo();
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
                {
                    requestNewScene();
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                {
                    if (io.KeyAlt)
                        (void)openProjectInCodeEditor("", true);
                    else if (io.KeyShift)
                        openOpenProjectModal();
                    else
                        openOpenSceneModal();
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                {
                    if (io.KeyShift)
                        (void)saveProject();
                    else
                        m_uiContext.emit(SaveActiveSceneSignal{});
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_Comma, false))
                {
                    m_showPreferencesWindow = !m_showPreferencesWindow;
                }
            }

            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu(tr("文件", "File")))
                {
                    if (ImGui::MenuItem(tr("新建场景", "New Scene"), "Ctrl+N"))
                    {
                        requestNewScene();
                    }
                    if (ImGui::MenuItem(tr("打开场景...", "Open Scene..."), "Ctrl+O"))
                    {
                        openOpenSceneModal();
                    }
                    if (ImGui::MenuItem(tr("保存场景", "Save Scene"), "Ctrl+S"))
                    {
                        m_uiContext.emit(SaveActiveSceneSignal{});
                    }
                    if (ImGui::MenuItem(tr("场景另存为...", "Save Scene As...")))
                    {
                        openSaveSceneAsModal();
                    }
                    if (ImGui::MenuItem(tr("将当前场景设为主场景", "Set Current Scene as Main")))
                    {
                        requestSetCurrentSceneAsMain();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("从模板新建工程...", "New Project from Template...")))
                    {
                        openNewProjectFromTemplateModal();
                    }
                    if (ImGui::MenuItem(tr("打开工程...", "Open Project..."), "Ctrl+Shift+O"))
                    {
                        openOpenProjectModal();
                    }
                    if (ImGui::BeginMenu(tr("最近工程", "Recent Projects")))
                    {
                        const auto& recents = m_projectLauncher.getRecentProjects();
                        if (recents.empty())
                        {
                            ImGui::MenuItem(tr("(暂无最近打开的工程)", "(No recent projects)"), nullptr, false, false);
                        }
                        else
                        {
                            for (const auto& entry : recents)
                            {
                                const std::string itemLabel = entry.name + "  [" + entry.projectDir + "]";
                                if (ImGui::MenuItem(itemLabel.c_str(), nullptr, false, entry.existsOnDisk))
                                {
                                    (void)openProjectInNewEditor(entry.projectDir, true);
                                }
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem(tr("清空最近记录", "Clear Recent Projects")))
                            {
                                m_projectLauncher.clearRecentProjects();
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem(tr("保存工程设置", "Save Project Settings"), "Ctrl+Shift+S"))
                    {
                        (void)saveProject();
                    }
                    ImGui::Separator();
                    const std::string openInIdeLabel =
                        std::string(tr("在 ", "Open Project in ")) +
                        getActiveCodeEditorDisplayName() +
                        tr(" 中打开工程", "");
                    if (ImGui::MenuItem(openInIdeLabel.c_str(), "Ctrl+Alt+O"))
                    {
                        (void)openProjectInCodeEditor("", true);
                    }
                    if (ImGui::MenuItem(tr("生成/同步 IDE 构建预设", "Generate/Sync IDE CMakePresets")))
                    {
                        (void)generateProjectCMakePresets("", true);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("退出编辑器", "Exit Editor"), "Alt+F4"))
                    {
                        auto* logicHost = Host::LogicHost::InstancePtr();
                        if (logicHost)
                        {
                            logicHost->requestExit();
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu(tr("编辑", "Edit")))
                {
                    const bool canUndo = m_commandStack.canUndo();
                    const bool canRedo = m_commandStack.canRedo();
                    std::string undoLabel = canUndo
                        ? (std::string(tr("撤销: ", "Undo: ")) + m_commandStack.getUndoName())
                        : tr("撤销", "Undo");
                    std::string redoLabel = canRedo
                        ? (std::string(tr("重做: ", "Redo: ")) + m_commandStack.getRedoName())
                        : tr("重做", "Redo");

                    if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z"))
                    {
                        m_commandStack.undo();
                    }
                    if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y"))
                    {
                        m_commandStack.redo();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("偏好设置...", "Preferences..."), "Ctrl+,"))
                    {
                        m_showPreferencesWindow = true;
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu(tr("视图", "View")))
                {
                    for (auto& entry : m_dockWindows)
                    {
                        const std::string itemLabel = localizeLabel(entry.name);
                        ImGui::MenuItem(itemLabel.c_str(), nullptr, &entry.isOpen);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu(tr("窗口", "Window")))
                {
                    if (ImGui::MenuItem(tr("重置主排版布局", "Reset Main Layout")))
                    {
                        requestResetLayout();
                    }
                    if (ImGui::BeginMenu(tr("工作区布局预设", "Workspace Presets")))
                    {
                        if (ImGui::MenuItem(tr("标准默认工作区", "Standard Workspace")))
                            applyWorkspacePreset(0);
                        if (ImGui::MenuItem(tr("场景专注工作区", "Scene Focus Workspace")))
                            applyWorkspacePreset(1);
                        if (ImGui::MenuItem(tr("着色器与管线", "Shader & Pipeline")))
                            applyWorkspacePreset(2);
                        if (ImGui::MenuItem(tr("UI 设计器工作区", "HTML UI Designer")))
                            applyWorkspacePreset(3);
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("关闭扩展编辑器窗口", "Close Secondary Editors")))
                    {
                        setDockWindowOpen("着色器与材质编辑器 (Shader & Material Editor)", false);
                        setDockWindowOpen("管线编辑 (Pipeline Editor)", false);
                        setDockWindowOpen("HTML UI 设计器 (RmlUi Visual Editor)", false);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(tr("偏好设置...", "Preferences...")))
                    {
                        m_showPreferencesWindow = true;
                    }
                    ImGui::EndMenu();
                }

                renderToolsMenu();

                // 渲染外部动态注册的扩展菜单
                std::unordered_map<std::string, std::vector<MenuActionEntry*>> menuCategories;
                for (auto& entry : m_menuActions)
                {
                    menuCategories[entry.category].push_back(&entry);
                }

                for (const auto& [cat, items] : menuCategories)
                {
                    const std::string catLabel = localizeLabel(cat);
                    if (ImGui::BeginMenu(catLabel.c_str()))
                    {
                        for (auto* item : items)
                        {
                            const std::string itemLabel = localizeLabel(item->item);
                            if (ImGui::MenuItem(itemLabel.c_str(), item->shortcut.empty() ? nullptr : item->shortcut.c_str()))
                            {
                                if (item->callback)
                                {
                                    item->callback();
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                }

                if (ImGui::BeginMenu(tr("帮助", "Help")))
                {
                    if (ImGui::MenuItem(tr("关于 MQEngine", "About MQEngine")))
                    {
                        addLog(
                            LogEntry::Level::Info,
                            tr("MQEngine v2 — 现代多轨并发轻量游戏引擎", "MQEngine v2 — Modern Multi-Track Concurrent Game Engine"),
                            "About");
                    }
                    ImGui::EndMenu();
                }

                // 右侧状态栏实时显示最近一条编辑器消息与真实帧率
                char rightStatusBuf[256] = {};
                std::snprintf(
                    rightStatusBuf,
                    sizeof(rightStatusBuf),
                    "%s  |  %.1f FPS (%.2f ms)",
                    m_lastStatusMessage.c_str(),
                    m_smoothedFps,
                    m_lastFrameTime * 1000.0f);
                const float textWidth = ImGui::CalcTextSize(rightStatusBuf).x;
                const float availRight = ImGui::GetWindowWidth() - ImGui::GetCursorPosX() - 24.0f;
                if (availRight > textWidth + 20.0f)
                {
                    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth - 20.0f);
                    ImGui::TextColored(ImVec4(0.65f, 0.82f, 0.98f, 0.95f), "%s", rightStatusBuf);
                }

                ImGui::EndMainMenuBar();
            }
        }

        void EditorUiComposite::renderToolsMenu()
        {
            if (!ImGui::BeginMenu(tr("工具", "Tools")))
            {
                return;
            }

            const std::string ideName = getActiveCodeEditorDisplayName();
            const std::string openProjLabel =
                std::string(tr("在 ", "Open C++ Project in ")) + ideName + tr(" 中打开 C++ 工程", "");
            if (ImGui::MenuItem(openProjLabel.c_str(), "Ctrl+Alt+O"))
            {
                (void)openProjectInCodeEditor("", true);
            }
            if (ImGui::MenuItem(tr("生成/同步 IDE 构建预设 (CMakePresets)", "Generate/Sync IDE CMakePresets")))
            {
                (void)generateProjectCMakePresets("", true);
            }

            if (ImGui::BeginMenu(tr("切换外部代码编辑器", "Switch Code Editor")))
            {
                const bool isEn = (m_language == EditorLanguage::EnUS);
                const CodeEditorKind currentPref = m_codeEditorService.getPreferences().preferredKind;
                if (ImGui::MenuItem(
                        ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::Auto, isEn),
                        nullptr,
                        currentPref == CodeEditorKind::Auto))
                {
                    m_codeEditorService.setPreferredKind(CodeEditorKind::Auto);
                    (void)m_codeEditorService.savePreferences();
                }
                ImGui::Separator();
                for (const auto& info : m_codeEditorService.getDetectedEditors())
                {
                    std::string label = info.displayName;
                    if (!info.versionHint.empty())
                    {
                        label += " (" + info.versionHint + ")";
                    }
                    if (!info.available)
                    {
                        label += tr(" [未安装]", " [Not Found]");
                    }
                    if (ImGui::MenuItem(label.c_str(), nullptr, currentPref == info.kind, info.available))
                    {
                        m_codeEditorService.setPreferredKind(info.kind);
                        (void)m_codeEditorService.savePreferences();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem(tr("重新扫描本机代码编辑器", "Re-detect Local IDEs")))
            {
                m_codeEditorService.refreshDetectedEditors();
                showToast(
                    tr("代码编辑器探测", "IDE Detection"),
                    std::string(tr("当前优选编辑器: ", "Active editor: ")) + getActiveCodeEditorDisplayName(),
                    LogEntry::Level::Info,
                    2.4f);
            }

            ImGui::Separator();
            if (ImGui::MenuItem(tr("外部代码编辑器设置...", "External Code Editor Settings...")))
            {
                m_showPreferencesWindow = true;
            }

            ImGui::EndMenu();
        }

        void EditorUiComposite::renderRegisteredPanels()
        {
            ZoneScoped();
            for (auto& entry : m_dockWindows)
            {
                if (entry.isOpen && entry.renderCallback)
                {
                    entry.renderCallback();
                }
            }
        }

    } // namespace EditorBase
} // namespace MQEngine
