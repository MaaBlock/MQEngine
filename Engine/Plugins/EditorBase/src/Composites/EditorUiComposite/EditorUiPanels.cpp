/**
 * @file EditorUiPanels.cpp
 * @brief 编辑器内置控制台日志面板、偏好设置窗口、浮动通知渲染以及场景/项目持久化实现
 */
#include "EditorUiComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>

#include <algorithm>
#include <sstream>

namespace MQEngine
{
    namespace EditorBase
    {
        Status EditorUiComposite::saveActiveScene()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            auto* domain = Domain::Domain::InstancePtr();
            if (!loader || !domain)
            {
                addLog(LogEntry::Level::Error, "保存失败：ResourceLoader 或 Domain 未就绪", "Persistence");
                return FailedPreconditionError("Persistence dependencies unavailable");
            }

            (void)loader->makeDir("Saved");
            auto& registry = domain->scene.getRegistry();
            std::ostringstream oss;
            oss << "# MQEngine Active Scene v2\n";

            uint32_t entityCount = 0;
            auto view = registry.view<Domain::Scene::LocalTransform>();
            for (auto entity : view)
            {
                const auto& local = view.get<Domain::Scene::LocalTransform>(entity);
                const auto* rel = registry.try_get<Domain::Scene::HierarchyRelation>(entity);
                const auto* world = registry.try_get<Domain::Scene::WorldTransform>(entity);

                uint32_t entId = static_cast<uint32_t>(entity);
                uint32_t parentId = (rel && rel->parent != FCT::ECS::null)
                    ? static_cast<uint32_t>(rel->parent)
                    : UINT32_MAX;
                bool visible = world ? world->isVisible : true;

                oss << "ENTITY " << entId << " " << parentId << " " << (visible ? 1 : 0) << " "
                    << local.position.x << " " << local.position.y << " " << local.position.z << " "
                    << local.rotation.x << " " << local.rotation.y << " " << local.rotation.z << " "
                    << local.scale.x << " " << local.scale.y << " " << local.scale.z << "\n";
                ++entityCount;
            }

            const std::string scenePath = "Saved/ActiveScene.mqscene";
            Status st = loader->saveFile(scenePath, oss.str());
            if (!st.ok())
            {
                addLog(LogEntry::Level::Error, "场景保存失败: " + std::string(st.message()), "Persistence");
                return st;
            }

            addLog(LogEntry::Level::Info,
                   "场景已保存至 " + scenePath + " (共 " + std::to_string(entityCount) + " 个实体)",
                   "Persistence");
            return OkStatus();
        }

        Status EditorUiComposite::loadActiveScene()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            auto* domain = Domain::Domain::InstancePtr();
            if (!loader || !domain)
            {
                return FailedPreconditionError("Persistence dependencies unavailable");
            }

            const std::string scenePath = "Saved/ActiveScene.mqscene";
            if (!loader->exists(scenePath))
            {
                addLog(LogEntry::Level::Warn, "未找到已保存的场景文件: " + scenePath, "Persistence");
                return NotFoundError("Saved scene file not found");
            }

            auto contentOr = loader->loadFile(scenePath);
            if (!contentOr.ok())
            {
                addLog(LogEntry::Level::Error, "读取场景文件失败: " + scenePath, "Persistence");
                return contentOr.status();
            }

            auto& scene = domain->scene;
            auto& registry = scene.getRegistry();

            std::vector<FCT::ECS::entity> existingEntities;
            auto view = registry.view<Domain::Scene::LocalTransform>();
            for (auto ent : view)
            {
                existingEntities.push_back(ent);
            }
            for (auto ent : existingEntities)
            {
                scene.destroyEntity(ent);
            }

            std::istringstream iss(*contentOr);
            std::string line;
            std::unordered_map<uint32_t, FCT::ECS::entity> idMap;
            std::vector<std::pair<uint32_t, uint32_t>> pendingLinks;
            uint32_t loadedCount = 0;

            while (std::getline(iss, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;
                std::istringstream ls(line);
                std::string tag;
                ls >> tag;
                if (tag == "ENTITY")
                {
                    uint32_t oldId = 0;
                    uint32_t oldParentId = UINT32_MAX;
                    int visible = 1;
                    Domain::Scene::LocalTransform local{};
                    if (ls >> oldId >> oldParentId >> visible
                           >> local.position.x >> local.position.y >> local.position.z
                           >> local.rotation.x >> local.rotation.y >> local.rotation.z
                           >> local.scale.x >> local.scale.y >> local.scale.z)
                    {
                        auto newEnt = scene.createEntity();
                        local.isDirty = true;
                        local.dirtyVersion = 1;
                        registry.replace<Domain::Scene::LocalTransform>(newEnt, local);
                        if (auto* world = registry.try_get<Domain::Scene::WorldTransform>(newEnt))
                        {
                            world->isVisible = (visible != 0);
                        }
                        idMap[oldId] = newEnt;
                        if (oldParentId != UINT32_MAX)
                        {
                            pendingLinks.emplace_back(oldParentId, oldId);
                        }
                        ++loadedCount;
                    }
                }
            }

            for (const auto& [oldParent, oldChild] : pendingLinks)
            {
                auto itP = idMap.find(oldParent);
                auto itC = idMap.find(oldChild);
                if (itP != idMap.end() && itC != idMap.end())
                {
                    scene.attachChild(itP->second, itC->second);
                }
            }

            addLog(LogEntry::Level::Info,
                   "已从 " + scenePath + " 加载场景 (共恢复 " + std::to_string(loadedCount) + " 个实体)",
                   "Persistence");
            return OkStatus();
        }

        Status EditorUiComposite::saveProject()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader unavailable");
            }

            (void)loader->makeDir("Saved");
            std::ostringstream oss;
            oss << "{\n";
            oss << "  \"engineVersion\": \"2.0.0\",\n";
            oss << "  \"dockWindows\": [\n";
            for (size_t i = 0; i < m_dockWindows.size(); ++i)
            {
                const auto& wnd = m_dockWindows[i];
                oss << "    { \"name\": \"" << wnd.name << "\", \"slot\": "
                    << static_cast<int>(wnd.slot) << ", \"isOpen\": "
                    << (wnd.isOpen ? "true" : "false") << " }"
                    << (i + 1 < m_dockWindows.size() ? "," : "") << "\n";
            }
            oss << "  ]\n";
            oss << "}\n";

            const std::string projPath = "Saved/ProjectSettings.mqproj";
            Status st = loader->saveFile(projPath, oss.str());
            if (!st.ok())
            {
                addLog(LogEntry::Level::Error, "项目配置保存失败: " + std::string(st.message()), "Persistence");
                return st;
            }

            (void)m_codeEditorService.savePreferences(loader);
            addLog(LogEntry::Level::Info, "项目全局配置与面板状态已保存至 " + projPath, "Persistence");
            return OkStatus();
        }

        void EditorUiComposite::renderLogEntryRow(const LogEntry& entry, size_t rowIndex)
        {
            ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
            if (entry.level == LogEntry::Level::Warn)
                color = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
            else if (entry.level >= LogEntry::Level::Error)
                color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (entry.level == LogEntry::Level::Debug)
                color = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);

            ImGui::PushID(static_cast<int>(rowIndex));
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("[%s] [%s] %s", entry.timestamp.c_str(), entry.source.c_str(), entry.message.c_str());
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
                SourceFileLocation srcLoc;
                if (ExternalCodeEditorService::tryExtractSourceLocation(entry.message, srcLoc))
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const std::string ideName = getActiveCodeEditorDisplayName();
                    ImGui::SetTooltip(
                        "%s %s: %s:%d",
                        tr("点击在", "Click to open in"),
                        ideName.c_str(),
                        srcLoc.filePath.c_str(),
                        srcLoc.line > 0 ? srcLoc.line : 1);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        (void)openFileInCodeEditor(srcLoc.filePath, srcLoc.line, srcLoc.column, true);
                    }
                }
            }
            ImGui::PopID();
        }

        void EditorUiComposite::renderLogWindow()
        {
            ZoneScoped();
            flushPendingLogs();
            auto& logState = m_uiContext.mutableState<LogWindowViewState>();

            const std::string logWndTitle = getDockWindowTitle("控制台日志 (Console Log)");
            ImGui::Begin(logWndTitle.c_str());

            if (ImGui::Button(tr("清空", "Clear")))
            {
                logState.logs.clear();
            }
            ImGui::SameLine();
            ImGui::Checkbox(tr("自动滚屏", "Auto Scroll"), &logState.autoScroll);
            ImGui::SameLine();

            const char* levelsZh[] = {"全部", "信息及以上", "警告及以上", "仅错误"};
            const char* levelsEn[] = {"All", "Info+", "Warn+", "Error+"};
            ImGui::SetNextItemWidth(120);
            ImGui::Combo(
                tr("级别筛选", "Filter Level"),
                &logState.selectedLevelFilter,
                (m_language == EditorLanguage::EnUS) ? levelsEn : levelsZh,
                4);

            ImGui::Separator();

            ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t i = 0; i < logState.logs.size(); ++i)
            {
                const auto& entry = logState.logs[i];
                if (logState.selectedLevelFilter == 1 && entry.level < LogEntry::Level::Info)
                    continue;
                if (logState.selectedLevelFilter == 2 && entry.level < LogEntry::Level::Warn)
                    continue;
                if (logState.selectedLevelFilter == 3 && entry.level < LogEntry::Level::Error)
                    continue;

                renderLogEntryRow(entry, i);
            }

            if (logState.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            ImGui::End();
        }

        void EditorUiComposite::renderPreferencesGeneralSection()
        {
            ImGui::TextColored(
                ImVec4(0.45f, 0.82f, 0.98f, 1.0f),
                "%s",
                tr("通用与语言设置", "General & Language Settings"));
            ImGui::Separator();

            int currentLangIdx = (m_language == EditorLanguage::EnUS) ? 1 : 0;
            const char* langOptions[] = {"简体中文", "English"};
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo(tr("界面语言", "Language"), &currentLangIdx, langOptions, IM_ARRAYSIZE(langOptions)))
            {
                setLanguage(currentLangIdx == 1 ? EditorLanguage::EnUS : EditorLanguage::ZhCN);
            }

            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.45f, 0.82f, 0.98f, 1.0f),
                "%s",
                tr("撤销与重做设置", "Undo / Redo Settings"));
            ImGui::Separator();

            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::SliderInt(tr("最大历史记录步数", "Max Undo History Steps"), &m_undoHistoryLimit, 16, 512))
            {
                m_commandStack.setMaxHistorySize(static_cast<size_t>(m_undoHistoryLimit));
            }
        }

        void EditorUiComposite::renderDetectedCodeEditorsTable()
        {
            constexpr ImGuiTableFlags kTableFlags =
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
            if (!ImGui::BeginTable("DetectedCodeEditorsTable", 3, kTableFlags))
            {
                return;
            }

            ImGui::TableSetupColumn(tr("编辑器", "IDE"), ImGuiTableColumnFlags_WidthFixed, 128.0f);
            ImGui::TableSetupColumn(tr("本机探测路径与版本", "Detected Path & Version"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("操作", "Action"), ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableHeadersRow();

            for (const auto& info : m_codeEditorService.getDetectedEditors())
            {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(info.kind));

                ImGui::TableSetColumnIndex(0);
                if (info.available)
                {
                    ImGui::TextColored(ImVec4(0.35f, 0.92f, 0.52f, 1.0f), "%s", info.displayName.c_str());
                }
                else
                {
                    ImGui::TextDisabled("%s", info.displayName.c_str());
                }

                ImGui::TableSetColumnIndex(1);
                if (info.available)
                {
                    if (!info.versionHint.empty())
                    {
                        ImGui::TextWrapped("[%s] %s", info.versionHint.c_str(), info.executablePath.c_str());
                    }
                    else
                    {
                        ImGui::TextWrapped("%s", info.executablePath.c_str());
                    }
                }
                else
                {
                    ImGui::TextDisabled("%s", tr("未检测到安装", "Not detected"));
                }

                ImGui::TableSetColumnIndex(2);
                const bool isSelected = (m_codeEditorService.getPreferences().preferredKind == info.kind);
                ImGui::BeginDisabled(!info.available || isSelected);
                if (ImGui::SmallButton(isSelected ? tr("当前", "Active") : tr("选用", "Use")))
                {
                    m_codeEditorService.setPreferredKind(info.kind);
                    (void)m_codeEditorService.savePreferences();
                }
                ImGui::EndDisabled();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void EditorUiComposite::renderCodeEditorPathAndActions()
        {
            auto& prefs = m_codeEditorService.mutablePreferences();
            const bool isCustom = (prefs.preferredKind == CodeEditorKind::Custom);

            std::string displayExePath = isCustom
                ? prefs.customExecutablePath
                : m_codeEditorService.getEffectiveExecutablePath();
            char exeBuf[512] = {};
            std::snprintf(exeBuf, sizeof(exeBuf), "%s", displayExePath.c_str());

            ImGui::SetNextItemWidth(-1.0f);
            ImGuiInputTextFlags pathFlags = isCustom ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
            ImGui::TextUnformatted(
                isCustom
                    ? tr("自定义可执行文件路径 (.exe / .cmd):", "Custom Executable Path (.exe / .cmd):")
                    : tr("当前生效可执行文件路径:", "Active Executable Path:"));
            if (ImGui::InputText("##CodeEditorExePath", exeBuf, sizeof(exeBuf), pathFlags))
            {
                if (isCustom)
                {
                    prefs.customExecutablePath = exeBuf;
                    (void)m_codeEditorService.savePreferences();
                }
            }

            if (isCustom)
            {
                char projArgsBuf[256] = {};
                std::snprintf(projArgsBuf, sizeof(projArgsBuf), "%s", prefs.customOpenProjectArgs.c_str());
                ImGui::TextUnformatted(tr("打开工程参数模板 ({project}):", "Open Project Args ({project}):"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##CustomOpenProjArgs", projArgsBuf, sizeof(projArgsBuf)))
                {
                    prefs.customOpenProjectArgs = projArgsBuf;
                    (void)m_codeEditorService.savePreferences();
                }

                char fileArgsBuf[384] = {};
                std::snprintf(fileArgsBuf, sizeof(fileArgsBuf), "%s", prefs.customOpenFileArgs.c_str());
                ImGui::TextUnformatted(
                    tr("打开文件参数模板 ({project} {file} {line} {col}):",
                       "Open File Args ({project} {file} {line} {col}):"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##CustomOpenFileArgs", fileArgsBuf, sizeof(fileArgsBuf)))
                {
                    prefs.customOpenFileArgs = fileArgsBuf;
                    (void)m_codeEditorService.savePreferences();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button(tr("重新自动检测", "Re-detect IDEs")))
            {
                m_codeEditorService.refreshDetectedEditors();
                showToast(
                    tr("代码编辑器探测", "IDE Detection"),
                    tr("已完成本机 CLion / Rider / VS Code 重新扫描", "Completed scanning local IDEs"),
                    LogEntry::Level::Info,
                    2.2f);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_codeEditorService.hasAvailableEditor());
            if (ImGui::Button(tr("测试打开当前工程", "Test Open Current Project")))
            {
                (void)m_codeEditorService.savePreferences();
                (void)openProjectInCodeEditor("", true);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(tr("同步 IDE 构建预设", "Sync IDE CMakePresets")))
            {
                (void)generateProjectCMakePresets("", true);
            }
        }

        void EditorUiComposite::renderPreferencesCodeEditorSection()
        {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.45f, 0.82f, 0.98f, 1.0f),
                "%s",
                tr("外部代码编辑器 (External Code Editor)", "External Code Editor"));
            ImGui::Separator();

            const bool isEn = (m_language == EditorLanguage::EnUS);
            const std::string autoResolved = m_codeEditorService.getEffectiveDisplayName(isEn);
            const std::string autoOptionLabel = std::string(
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::Auto, isEn)) +
                " -> " + autoResolved;

            const char* presetLabels[6] = {
                autoOptionLabel.c_str(),
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::CLion, isEn),
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::Rider, isEn),
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::VSCode, isEn),
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::VisualStudio, isEn),
                ExternalCodeEditorService::getPresetDisplayName(CodeEditorKind::Custom, isEn)
            };

            int currentKindIdx = static_cast<int>(m_codeEditorService.getPreferences().preferredKind);
            ImGui::SetNextItemWidth(340.0f);
            if (ImGui::Combo(tr("首选代码编辑器", "Preferred Editor"), &currentKindIdx, presetLabels, 6))
            {
                m_codeEditorService.setPreferredKind(static_cast<CodeEditorKind>(currentKindIdx));
                (void)m_codeEditorService.savePreferences();
            }

            ImGui::Spacing();
            renderDetectedCodeEditorsTable();
            ImGui::Spacing();
            renderCodeEditorPathAndActions();
        }

        void EditorUiComposite::renderPreferencesWindow()
        {
            ZoneScoped();
            if (!m_showPreferencesWindow)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2(620.0f, 520.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    tr("偏好设置###EditorPreferencesWindow", "Preferences###EditorPreferencesWindow"),
                    &m_showPreferencesWindow,
                    ImGuiWindowFlags_NoCollapse))
            {
                renderPreferencesGeneralSection();
                renderPreferencesCodeEditorSection();

                ImGui::Spacing();
                ImGui::Separator();
                if (ImGui::Button(tr("确定并关闭", "OK & Close"), ImVec2(140.0f, 0.0f)))
                {
                    (void)m_codeEditorService.savePreferences();
                    m_showPreferencesWindow = false;
                }
            }
            ImGui::End();
        }

        void EditorUiComposite::renderToastNotifications()
        {
            ZoneScoped();
            if (m_toasts.empty())
            {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
            if (!fgDrawList)
            {
                return;
            }

            const float cardWidth = 340.0f;
            const float cardHeight = 58.0f;
            const float cardSpacing = 10.0f;
            const float rightMargin = 20.0f;
            const float bottomMargin = 24.0f;

            float currentBottomY = viewport->WorkPos.y + viewport->WorkSize.y - bottomMargin;

            for (int i = static_cast<int>(m_toasts.size()) - 1; i >= 0; --i)
            {
                const auto& toast = m_toasts[static_cast<size_t>(i)];
                const float fadeAlpha = std::clamp(toast.remainingSec / 0.45f, 0.0f, 1.0f);
                const auto alphaByte = static_cast<uint8_t>(fadeAlpha * 238.0f);

                const ImVec2 pMin(
                    viewport->WorkPos.x + viewport->WorkSize.x - cardWidth - rightMargin,
                    currentBottomY - cardHeight);
                const ImVec2 pMax(
                    viewport->WorkPos.x + viewport->WorkSize.x - rightMargin,
                    currentBottomY);

                ImU32 accentCol = IM_COL32(70, 175, 255, alphaByte);
                if (toast.level == LogEntry::Level::Warn)
                {
                    accentCol = IM_COL32(255, 195, 55, alphaByte);
                }
                else if (toast.level >= LogEntry::Level::Error)
                {
                    accentCol = IM_COL32(245, 75, 75, alphaByte);
                }

                // 阴影与背景卡片
                fgDrawList->AddRectFilled(
                    ImVec2(pMin.x + 3.0f, pMin.y + 4.0f),
                    ImVec2(pMax.x + 3.0f, pMax.y + 4.0f),
                    IM_COL32(0, 0, 0, static_cast<uint8_t>(fadeAlpha * 110.0f)),
                    6.0f);
                fgDrawList->AddRectFilled(
                    pMin,
                    pMax,
                    IM_COL32(26, 30, 38, alphaByte),
                    6.0f);
                fgDrawList->AddRect(
                    pMin,
                    pMax,
                    IM_COL32(75, 86, 108, static_cast<uint8_t>(fadeAlpha * 185.0f)),
                    6.0f,
                    0,
                    1.2f);

                // 左侧彩色状态指示条
                fgDrawList->AddRectFilled(
                    pMin,
                    ImVec2(pMin.x + 5.0f, pMax.y),
                    accentCol,
                    6.0f,
                    ImDrawFlags_RoundCornersLeft);

                // 标题与正文
                fgDrawList->AddText(
                    ImVec2(pMin.x + 14.0f, pMin.y + 8.0f),
                    accentCol,
                    toast.title.c_str());
                fgDrawList->AddText(
                    ImVec2(pMin.x + 14.0f, pMin.y + 29.0f),
                    IM_COL32(230, 235, 245, static_cast<uint8_t>(fadeAlpha * 255.0f)),
                    toast.message.c_str());

                // 底部倒计时进度条
                const float progress = std::clamp(toast.remainingSec / toast.totalSec, 0.0f, 1.0f);
                fgDrawList->AddRectFilled(
                    ImVec2(pMin.x + 5.0f, pMax.y - 3.0f),
                    ImVec2(pMin.x + 5.0f + (cardWidth - 5.0f) * progress, pMax.y),
                    accentCol);

                currentBottomY -= (cardHeight + cardSpacing);
            }
        }

    } // namespace EditorBase
} // namespace MQEngine
