/**
 * @file EditorUiProjectModals.cpp
 * @brief 工程打开/最近工程、模板新建工程以及场景打开/另存为/主场景信号弹窗与交互实现
 */
#include "EditorUiComposite.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace MQEngine
{
    namespace EditorBase
    {
        namespace
        {
            constexpr const char* kModalOpenProjectId = "打开工程###MQOpenProjectModal";
            constexpr const char* kModalNewProjectId = "从模板新建工程###MQNewProjectTemplateModal";
            constexpr const char* kModalOpenSceneId = "打开场景###MQOpenSceneModal";
            constexpr const char* kModalSaveSceneAsId = "场景另存为###MQSaveSceneAsModal";

            bool isStatusCancelled(const Status& status) noexcept
            {
                static const auto kCancelledCode = CancelledError("").code();
                return status.code() == kCancelledCode;
            }

            std::string trimCopy(std::string_view text)
            {
                size_t start = 0;
                while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
                {
                    ++start;
                }
                size_t end = text.size();
                while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
                {
                    --end;
                }
                return std::string(text.substr(start, end - start));
            }
        } // namespace

        void EditorUiComposite::requestNewScene()
        {
            auto& menuState = m_uiContext.mutableState<MainMenuViewState>();
            menuState.activeSceneName.clear();
            menuState.activeScenePath.clear();
            m_uiContext.emit(NewSceneSignal{});
            addLog(LogEntry::Level::Info, tr("已请求新建纯空场景", "Requested new empty scene"), "Scene");
            showToast(tr("新建场景", "New Scene"), tr("已切换为空场景", "Created empty scene"), LogEntry::Level::Info, 2.2f);
        }

        void EditorUiComposite::requestOpenScene(const std::string& scenePath)
        {
            const std::string trimmed = trimCopy(scenePath);
            if (trimmed.empty())
            {
                return;
            }
            m_uiContext.emit(OpenSceneSignal{{}, trimmed});
            addLog(LogEntry::Level::Info, std::string(tr("请求打开场景: ", "Requested open scene: ")) + trimmed, "Scene");
        }

        void EditorUiComposite::requestSaveSceneAs(const std::string& scenePath)
        {
            const std::string trimmed = trimCopy(scenePath);
            if (trimmed.empty())
            {
                return;
            }
            m_uiContext.emit(SaveSceneAsSignal{{}, trimmed});
            addLog(LogEntry::Level::Info, std::string(tr("请求场景另存为: ", "Requested save scene as: ")) + trimmed, "Scene");
        }

        void EditorUiComposite::requestSetCurrentSceneAsMain()
        {
            m_uiContext.emit(SetCurrentSceneAsMainSignal{});
            addLog(LogEntry::Level::Info, tr("已请求将当前场景设为工程主场景", "Requested setting current scene as main"), "Scene");
            showToast(
                tr("设为主场景", "Set Main Scene"),
                tr("正在将当前场景写入工程主场景配置", "Updating project startup_scene"),
                LogEntry::Level::Info,
                2.5f);
        }

        Status EditorUiComposite::openProjectInNewEditor(const std::string& projectDir, bool showToastFeedback)
        {
            ZoneScoped();
            const std::string trimmed = trimCopy(projectDir);
            Status st = m_projectLauncher.openProject(trimmed);
            if (!st.ok())
            {
                const std::string errMsg = std::string(st.message());
                addLog(LogEntry::Level::Error, "打开工程失败: " + errMsg, "Project");
                if (showToastFeedback)
                {
                    showToast(tr("打开工程失败", "Open Project Failed"), errMsg, LogEntry::Level::Error, 3.6f);
                }
                return st;
            }

            std::string projName;
            std::string startupScene;
            std::string desc;
            (void)ProjectLauncherService::readProjectMetadata(trimmed, projName, startupScene, desc);
            const std::string label = projName.empty() ? trimmed : projName;

            addLog(LogEntry::Level::Info, "正在启动工程实例: " + label + " (" + trimmed + ")", "Project");
            if (showToastFeedback)
            {
                showToast(
                    tr("已启动工程", "Project Launched"),
                    label + " -> Editor.exe",
                    LogEntry::Level::Info,
                    3.0f);
            }
            return OkStatus();
        }

        StatusOr<CreateProjectFromTemplateResult> EditorUiComposite::createProjectFromTemplate(
            const CreateProjectFromTemplateRequest& request,
            bool showToastFeedback)
        {
            ZoneScoped();
            auto resultOr = m_projectLauncher.createProjectFromTemplate(request);
            if (!resultOr.ok())
            {
                const std::string errMsg = std::string(resultOr.status().message());
                addLog(LogEntry::Level::Error, "从模板创建工程失败: " + errMsg, "Project");
                if (showToastFeedback)
                {
                    showToast(tr("创建工程失败", "Create Project Failed"), errMsg, LogEntry::Level::Error, 3.6f);
                }
                return resultOr.status();
            }

            const auto& res = resultOr.value();
            std::string msg = "已创建工程 " + res.projectName + " (" + std::to_string(res.copiedFilesCount) + " 个文件)";
            if (res.ideSynced)
            {
                msg += "，已生成 IDE 构建预设";
            }
            addLog(LogEntry::Level::Info, msg + " -> " + res.projectDir, "Project");
            if (showToastFeedback)
            {
                showToast(tr("工程创建成功", "Project Created"), res.projectDir, LogEntry::Level::Info, 3.5f);
            }
            return resultOr;
        }

        void EditorUiComposite::openOpenProjectModal()
        {
            m_projectLauncher.refreshRecentProjectsStatus();
            m_projectModals.discoveredProjects = ProjectLauncherService::discoverWorkspaceProjects();
            m_projectModals.openProjectError.clear();
            if (m_projectModals.openProjectPathInput.empty())
            {
                if (!m_projectLauncher.getRecentProjects().empty())
                {
                    m_projectModals.openProjectPathInput = m_projectLauncher.getRecentProjects().front().projectDir;
                }
                else if (!m_projectModals.discoveredProjects.empty())
                {
                    m_projectModals.openProjectPathInput = m_projectModals.discoveredProjects.front().projectDir;
                }
                else
                {
                    m_projectModals.openProjectPathInput = getActiveProjectDirectory();
                }
            }
            m_projectModals.triggerOpenProject = true;
        }

        void EditorUiComposite::openNewProjectFromTemplateModal()
        {
            m_projectModals.cachedTemplates = ProjectLauncherService::discoverTemplates();
            m_projectModals.newProjectError.clear();
            if (m_projectModals.selectedTemplateIndex < 0 ||
                m_projectModals.selectedTemplateIndex >= static_cast<int>(m_projectModals.cachedTemplates.size()))
            {
                m_projectModals.selectedTemplateIndex = 0;
            }
            if (m_projectModals.newProjectParentDir.empty())
            {
                m_projectModals.newProjectParentDir = ProjectLauncherService::resolveDefaultProjectsParentDir();
            }
            if (m_projectModals.newProjectName.empty())
            {
                m_projectModals.newProjectName = "MyGame";
            }
            m_projectModals.triggerNewProject = true;
        }

        void EditorUiComposite::openOpenSceneModal()
        {
            const std::string activeProj = getActiveProjectDirectory();
            m_projectModals.cachedScenes = ProjectLauncherService::discoverProjectScenes(activeProj);
            m_projectModals.openSceneError.clear();
            if (!m_projectModals.cachedScenes.empty())
            {
                m_projectModals.openScenePathInput = m_projectModals.cachedScenes.front().fullPath;
            }
            else
            {
                m_projectModals.openScenePathInput.clear();
            }
            m_projectModals.triggerOpenScene = true;
        }

        void EditorUiComposite::openSaveSceneAsModal()
        {
            m_projectModals.saveSceneAsError.clear();
            const std::string activeProj = getActiveProjectDirectory();
            const auto& menuState = m_uiContext.state<MainMenuViewState>();
            if (!menuState.activeScenePath.empty())
            {
                m_projectModals.saveSceneAsPathInput = menuState.activeScenePath;
            }
            else if (!activeProj.empty())
            {
                m_projectModals.saveSceneAsPathInput = SAI::Path::join(activeProj, "Scenes/NewScene.mqscene");
                std::replace(m_projectModals.saveSceneAsPathInput.begin(), m_projectModals.saveSceneAsPathInput.end(), '\\', '/');
            }
            else
            {
                m_projectModals.saveSceneAsPathInput = "Scenes/NewScene.mqscene";
            }
            m_projectModals.triggerSaveSceneAs = true;
        }

        void EditorUiComposite::renderProjectAndSceneModals()
        {
            ZoneScoped();
            if (m_projectModals.triggerOpenProject)
            {
                ImGui::OpenPopup(kModalOpenProjectId);
                m_projectModals.triggerOpenProject = false;
            }
            if (m_projectModals.triggerNewProject)
            {
                ImGui::OpenPopup(kModalNewProjectId);
                m_projectModals.triggerNewProject = false;
            }
            if (m_projectModals.triggerOpenScene)
            {
                ImGui::OpenPopup(kModalOpenSceneId);
                m_projectModals.triggerOpenScene = false;
            }
            if (m_projectModals.triggerSaveSceneAs)
            {
                ImGui::OpenPopup(kModalSaveSceneAsId);
                m_projectModals.triggerSaveSceneAs = false;
            }

            renderOpenProjectModal();
            renderNewProjectFromTemplateModal();
            renderOpenSceneModal();
            renderSaveSceneAsModal();
        }

        void EditorUiComposite::renderOpenProjectModal()
        {
            ImGui::SetNextWindowSize(ImVec2(640.0f, 430.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::BeginPopupModal(kModalOpenProjectId, nullptr, ImGuiWindowFlags_NoCollapse))
            {
                return;
            }

            ImGui::TextWrapped(
                "%s",
                tr("选择包含 project.mq 的工程目录，将在独立进程中以 --project 参数打开：",
                   "Choose a project folder containing project.mq to launch a clean Editor.exe --project instance:"));
            ImGui::Spacing();

            char dirBuf[512] = {};
            std::snprintf(dirBuf, sizeof(dirBuf), "%s", m_projectModals.openProjectPathInput.c_str());
            ImGui::SetNextItemWidth(-125.0f);
            if (ImGui::InputText("##OpenProjectPathInput", dirBuf, sizeof(dirBuf)))
            {
                m_projectModals.openProjectPathInput = dirBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("浏览目录...", "Browse..."), ImVec2(115.0f, 0.0f)))
            {
                auto pickOr = ProjectLauncherService::pickProjectDirectoryDialog(m_projectModals.openProjectPathInput);
                if (pickOr.ok())
                {
                    m_projectModals.openProjectPathInput = pickOr.value();
                    m_projectModals.openProjectError.clear();
                }
                else if (!isStatusCancelled(pickOr.status()))
                {
                    m_projectModals.openProjectError = std::string(pickOr.status().message());
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.98f, 1.0f), "%s", tr("最近与工作区工程:", "Recent & Workspace Projects:"));
            ImGui::BeginChild("ProjectQuickPickList", ImVec2(0.0f, 210.0f), true);

            auto drawEntryRow = [&](const RecentProjectEntry& item, const char* tag) {
                ImGui::PushID(item.projectDir.c_str());
                const bool selected = (m_projectModals.openProjectPathInput == item.projectDir);
                const std::string label = "[" + std::string(tag) + "] " + item.name + "   " + item.projectDir;
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_projectModals.openProjectPathInput = item.projectDir;
                    m_projectModals.openProjectError.clear();
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && item.existsOnDisk)
                    {
                        if (openProjectInNewEditor(item.projectDir, true).ok())
                        {
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::PopID();
            };

            for (const auto& entry : m_projectLauncher.getRecentProjects())
            {
                if (entry.existsOnDisk)
                {
                    drawEntryRow(entry, tr("最近", "Recent"));
                }
            }
            for (const auto& entry : m_projectModals.discoveredProjects)
            {
                drawEntryRow(entry, tr("工作区", "Workspace"));
            }
            ImGui::EndChild();

            if (!m_projectModals.openProjectError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_projectModals.openProjectError.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button(tr("打开工程", "Open Project"), ImVec2(140.0f, 0.0f)))
            {
                Status st = openProjectInNewEditor(m_projectModals.openProjectPathInput, true);
                if (st.ok())
                {
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    m_projectModals.openProjectError = std::string(st.message());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("取消", "Cancel"), ImVec2(110.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void EditorUiComposite::renderNewProjectFromTemplateModal()
        {
            ImGui::SetNextWindowSize(ImVec2(660.0f, 470.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::BeginPopupModal(kModalNewProjectId, nullptr, ImGuiWindowFlags_NoCollapse))
            {
                return;
            }

            ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.98f, 1.0f), "%s", tr("1. 选择工程模板:", "1. Select Project Template:"));
            if (m_projectModals.cachedTemplates.empty())
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.25f, 1.0f),
                    "%s",
                    tr("未在 Templates/ 下发现有效模板目录（需包含 project.mq）", "No valid template found under Templates/"));
            }
            else
            {
                ImGui::BeginChild("TemplateCardsRegion", ImVec2(0.0f, 155.0f), true);
                for (int i = 0; i < static_cast<int>(m_projectModals.cachedTemplates.size()); ++i)
                {
                    const auto& tpl = m_projectModals.cachedTemplates[static_cast<size_t>(i)];
                    const bool isSelected = (i == m_projectModals.selectedTemplateIndex);
                    std::string header = tpl.displayName.empty() ? tpl.id : (tpl.displayName + " (" + tpl.id + ")");
                    if (!tpl.startupScene.empty())
                    {
                        header += "  [" + tpl.startupScene + "]";
                    }
                    ImGui::PushID(i);
                    if (ImGui::Selectable(header.c_str(), isSelected))
                    {
                        m_projectModals.selectedTemplateIndex = i;
                        m_projectModals.newProjectError.clear();
                    }
                    ImGui::PopID();
                    if (!tpl.description.empty())
                    {
                        ImGui::Indent(16.0f);
                        ImGui::TextDisabled("%s", tpl.description.c_str());
                        ImGui::Unindent(16.0f);
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.98f, 1.0f), "%s", tr("2. 新工程位置与名称:", "2. Project Name & Location:"));

            char nameBuf[128] = {};
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_projectModals.newProjectName.c_str());
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::InputText(tr("工程名称", "Project Name"), nameBuf, sizeof(nameBuf)))
            {
                m_projectModals.newProjectName = nameBuf;
            }

            char parentBuf[512] = {};
            std::snprintf(parentBuf, sizeof(parentBuf), "%s", m_projectModals.newProjectParentDir.c_str());
            ImGui::SetNextItemWidth(-125.0f);
            if (ImGui::InputText("##NewProjParentDir", parentBuf, sizeof(parentBuf)))
            {
                m_projectModals.newProjectParentDir = parentBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("选择目录...", "Browse..."), ImVec2(115.0f, 0.0f)))
            {
                auto pickOr = ProjectLauncherService::pickProjectDirectoryDialog(m_projectModals.newProjectParentDir);
                if (pickOr.ok())
                {
                    m_projectModals.newProjectParentDir = pickOr.value();
                    m_projectModals.newProjectError.clear();
                }
                else if (!isStatusCancelled(pickOr.status()))
                {
                    m_projectModals.newProjectError = std::string(pickOr.status().message());
                }
            }

            std::string previewDir = m_projectModals.newProjectParentDir.empty()
                ? m_projectModals.newProjectName
                : SAI::Path::join(m_projectModals.newProjectParentDir, m_projectModals.newProjectName);
            std::replace(previewDir.begin(), previewDir.end(), '\\', '/');
            ImGui::TextDisabled("%s %s", tr("目标路径:", "Target Path:"), previewDir.c_str());

            ImGui::Checkbox(tr("自动生成 IDE 构建预设与 .run 启动项 (若含 CMakeLists.txt)", "Generate CMakePresets/.idea/.run if C++ project"), &m_projectModals.newProjectSyncIde);
            ImGui::Checkbox(tr("创建完成后立即在新编辑器实例中打开", "Open created project immediately"), &m_projectModals.newProjectOpenAfterCreate);

            if (!m_projectModals.newProjectError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_projectModals.newProjectError.c_str());
            }

            ImGui::Spacing();
            const bool canCreate = !m_projectModals.cachedTemplates.empty();
            ImGui::BeginDisabled(!canCreate);
            if (ImGui::Button(tr("创建工程", "Create Project"), ImVec2(140.0f, 0.0f)))
            {
                CreateProjectFromTemplateRequest req;
                req.templateId = m_projectModals.cachedTemplates[static_cast<size_t>(m_projectModals.selectedTemplateIndex)].id;
                req.targetParentDir = m_projectModals.newProjectParentDir;
                req.projectName = m_projectModals.newProjectName;
                req.syncIdeFiles = m_projectModals.newProjectSyncIde;
                req.openAfterCreate = m_projectModals.newProjectOpenAfterCreate;

                auto resOr = createProjectFromTemplate(req, true);
                if (resOr.ok())
                {
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    m_projectModals.newProjectError = std::string(resOr.status().message());
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(tr("取消", "Cancel"), ImVec2(110.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void EditorUiComposite::renderOpenSceneModal()
        {
            ImGui::SetNextWindowSize(ImVec2(600.0f, 380.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::BeginPopupModal(kModalOpenSceneId, nullptr, ImGuiWindowFlags_NoCollapse))
            {
                return;
            }

            char sceneBuf[512] = {};
            std::snprintf(sceneBuf, sizeof(sceneBuf), "%s", m_projectModals.openScenePathInput.c_str());
            ImGui::SetNextItemWidth(-125.0f);
            if (ImGui::InputText("##OpenScenePathInput", sceneBuf, sizeof(sceneBuf)))
            {
                m_projectModals.openScenePathInput = sceneBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("浏览文件...", "Browse..."), ImVec2(115.0f, 0.0f)))
            {
                const std::string scenesDir = SAI::Path::join(getActiveProjectDirectory(), "Scenes");
                const std::string initDir = SAI::FileSystem::isDirectory(scenesDir) ? scenesDir : getActiveProjectDirectory();
                auto pickOr = ProjectLauncherService::pickOpenSceneFileDialog(initDir);
                if (pickOr.ok())
                {
                    m_projectModals.openScenePathInput = pickOr.value();
                    m_projectModals.openSceneError.clear();
                }
                else if (!isStatusCancelled(pickOr.status()))
                {
                    m_projectModals.openSceneError = std::string(pickOr.status().message());
                }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.98f, 1.0f), "%s", tr("工程内场景列表:", "Project Scenes:"));
            ImGui::BeginChild("ProjectScenesQuickList", ImVec2(0.0f, 200.0f), true);
            for (const auto& sc : m_projectModals.cachedScenes)
            {
                std::string label = sc.displayRelPath;
                if (sc.isMainScene)
                {
                    label += tr("  [主场景]", "  [Main Scene]");
                }
                const bool selected = (m_projectModals.openScenePathInput == sc.fullPath);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_projectModals.openScenePathInput = sc.fullPath;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        requestOpenScene(sc.fullPath);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();

            if (!m_projectModals.openSceneError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_projectModals.openSceneError.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button(tr("打开场景", "Open Scene"), ImVec2(140.0f, 0.0f)))
            {
                const std::string target = trimCopy(m_projectModals.openScenePathInput);
                if (target.empty() || !SAI::FileSystem::isFile(target))
                {
                    m_projectModals.openSceneError = tr("指定的 .mqscene 文件不存在", "Selected .mqscene file does not exist");
                }
                else
                {
                    requestOpenScene(target);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("取消", "Cancel"), ImVec2(110.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void EditorUiComposite::renderSaveSceneAsModal()
        {
            ImGui::SetNextWindowSize(ImVec2(580.0f, 200.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::BeginPopupModal(kModalSaveSceneAsId, nullptr, ImGuiWindowFlags_NoCollapse))
            {
                return;
            }

            ImGui::TextUnformatted(tr("目标场景文件路径 (.mqscene):", "Target Scene File Path (.mqscene):"));
            char saveBuf[512] = {};
            std::snprintf(saveBuf, sizeof(saveBuf), "%s", m_projectModals.saveSceneAsPathInput.c_str());
            ImGui::SetNextItemWidth(-125.0f);
            if (ImGui::InputText("##SaveSceneAsPathInput", saveBuf, sizeof(saveBuf)))
            {
                m_projectModals.saveSceneAsPathInput = saveBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("浏览位置...", "Browse..."), ImVec2(115.0f, 0.0f)))
            {
                const std::string scenesDir = SAI::Path::join(getActiveProjectDirectory(), "Scenes");
                const std::string initDir = SAI::FileSystem::isDirectory(scenesDir) ? scenesDir : getActiveProjectDirectory();
                auto pickOr = ProjectLauncherService::pickSaveSceneFileDialog(initDir, "NewScene.mqscene");
                if (pickOr.ok())
                {
                    m_projectModals.saveSceneAsPathInput = pickOr.value();
                    m_projectModals.saveSceneAsError.clear();
                }
                else if (!isStatusCancelled(pickOr.status()))
                {
                    m_projectModals.saveSceneAsError = std::string(pickOr.status().message());
                }
            }

            if (!m_projectModals.saveSceneAsError.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_projectModals.saveSceneAsError.c_str());
            }

            ImGui::Spacing();
            if (ImGui::Button(tr("另存为", "Save As"), ImVec2(140.0f, 0.0f)))
            {
                const std::string target = trimCopy(m_projectModals.saveSceneAsPathInput);
                if (target.empty())
                {
                    m_projectModals.saveSceneAsError = tr("保存路径不能为空", "Scene file path cannot be empty");
                }
                else
                {
                    requestSaveSceneAs(target);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("取消", "Cancel"), ImVec2(110.0f, 0.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    } // namespace EditorBase
} // namespace MQEngine
