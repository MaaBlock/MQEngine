/**
 * @file EditorUiComposite.h
 * @brief 编辑器通用排版底座、DockSpace 调度与核心面板 Composite
 */
#ifndef MQENGINE_EDITOR_BASE_EDITOR_UI_COMPOSITE_H
#define MQENGINE_EDITOR_BASE_EDITOR_UI_COMPOSITE_H

#include "../../EditorBaseApi.h"
#include "EditorUiTypes.h"
#include "UiContext.h"
#include "UiSignals.h"
#include "ToastNotification.h"
#include "CommandStack.h"
#include "ExternalCodeEditorService.h"
#include "ProjectLauncherService.h"
#include "../ImGuiComposite/ImGuiComposite.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>

namespace MQEngine
{
    namespace EditorBase
    {
        /**
         * @brief 编辑器 UI 底座与排版调度 Composite：
         *        承载主 DockSpace 停靠布局、UiContext 状态与信号中心、撤销重做栈、
         *        主菜单栏、统一日志诊断窗口、偏好设置与浮动消息通知。
         */
        class EDITOR_UI_COMPOSITE_API EditorUiComposite : public Asset::Composite
        {
        public:
            EditorUiComposite();
            ~EditorUiComposite() override;

            EditorUiComposite(const EditorUiComposite&) = delete;
            EditorUiComposite& operator=(const EditorUiComposite&) = delete;

            // ==================== Composite 接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "EditorUiComposite"; }
            std::string getPluginName() const override { return "EditorBase"; }
            std::vector<std::string> getDependencies() const override { return {"ImGuiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 上下文与命令栈访问 ====================

            UiContext& getUiContext() noexcept { return m_uiContext; }
            const UiContext& getUiContext() const noexcept { return m_uiContext; }
            CommandStack& getCommandStack() noexcept { return m_commandStack; }
            ImGuiComposite* getImGuiComposite() const noexcept { return m_imguiComposite; }

            // ==================== 动态窗口与菜单挂载服务 ====================

            void registerDockWindow(
                const std::string& name,
                EditorDockSlot slot,
                std::function<void()> renderCallback,
                bool defaultOpen = true);

            void unregisterDockWindow(const std::string& name);

            void setDockWindowOpen(const std::string& name, bool isOpen);
            bool* getDockWindowOpenPtr(const std::string& name);

            void registerMenuAction(
                const std::string& category,
                const std::string& item,
                const std::string& shortcut,
                std::function<void()> callback);

            // ==================== 辅助功能 ====================

            void addLog(LogEntry::Level level, const std::string& message, const std::string& source = "Editor");
            void showToast(const std::string& title, const std::string& message, LogEntry::Level level = LogEntry::Level::Info, float durationSec = 2.8f);
            void requestResetLayout() noexcept { m_needResetLayout = true; }

            // ==================== 多语言本地化支持 ====================

            void setLanguage(EditorLanguage lang);
            EditorLanguage getLanguage() const noexcept { return m_language; }

            const char* tr(const char* zhText, const char* enText) const noexcept
            {
                return (m_language == EditorLanguage::EnUS) ? enText : zhText;
            }

            void registerTranslation(const std::string& key, const std::string& zhVal, const std::string& enVal);
            const char* trKey(const std::string& key, const char* fallbackZh, const char* fallbackEn) const;
            std::string localizeLabel(std::string_view label) const;
            std::string getDockWindowTitle(std::string_view registeredName) const;

            Status saveActiveScene();
            Status loadActiveScene();
            Status saveProject();
            void applyWorkspacePreset(int presetIndex);

            // ==================== 场景信号与工程/模板启动服务 ====================

            ProjectLauncherService& getProjectLauncherService() noexcept { return m_projectLauncher; }
            const ProjectLauncherService& getProjectLauncherService() const noexcept { return m_projectLauncher; }

            void requestNewScene();
            void requestOpenScene(const std::string& scenePath);
            void requestSaveSceneAs(const std::string& scenePath);
            void requestSetCurrentSceneAsMain();

            Status openProjectInNewEditor(const std::string& projectDir, bool showToastFeedback = true);
            StatusOr<CreateProjectFromTemplateResult> createProjectFromTemplate(
                const CreateProjectFromTemplateRequest& request,
                bool showToastFeedback = true);

            // ==================== 外部代码编辑器管理与启动服务 ====================

            ExternalCodeEditorService& getCodeEditorService() noexcept { return m_codeEditorService; }
            const ExternalCodeEditorService& getCodeEditorService() const noexcept { return m_codeEditorService; }

            Status openProjectInCodeEditor(const std::string& projectDir = "", bool showToastFeedback = true);
            Status openFileInCodeEditor(
                const std::string& filePath,
                int line = 0,
                int column = 0,
                bool showToastFeedback = true);
            StatusOr<IdeProjectSyncResult> generateProjectCMakePresets(
                const std::string& projectDir = "",
                bool showToastFeedback = true);

            std::string getActiveCodeEditorDisplayName() const;
            bool hasAvailableCodeEditor() const noexcept;
            std::string getActiveProjectDirectory() const;
            static bool isCodeEditorSourceFile(std::string_view filePath) noexcept;

        private:
            void renderDockSpace();
            void renderMainMenuBar();
            void renderToolsMenu();
            void renderLogWindow();
            void renderLogEntryRow(const LogEntry& entry, size_t rowIndex);
            void renderPreferencesWindow();
            void renderPreferencesGeneralSection();
            void renderPreferencesCodeEditorSection();
            void renderDetectedCodeEditorsTable();
            void renderCodeEditorPathAndActions();
            void renderProjectAndSceneModals();
            void renderOpenProjectModal();
            void renderNewProjectFromTemplateModal();
            void renderOpenSceneModal();
            void renderSaveSceneAsModal();
            void openOpenProjectModal();
            void openNewProjectFromTemplateModal();
            void openOpenSceneModal();
            void openSaveSceneAsModal();
            void renderRegisteredPanels();
            void renderToastNotifications();

            void initDefaultMenuActions();
            void buildInitialDockLayout(uint32_t dockspaceId);
            void flushPendingLogs();

            struct ProjectModalUiState
            {
                bool triggerOpenProject = false;
                bool triggerNewProject = false;
                bool triggerOpenScene = false;
                bool triggerSaveSceneAs = false;

                std::string openProjectPathInput;
                std::string openProjectError;
                std::vector<RecentProjectEntry> discoveredProjects;

                std::vector<ProjectTemplateInfo> cachedTemplates;
                int selectedTemplateIndex = 0;
                std::string newProjectName = "MyGame";
                std::string newProjectParentDir;
                bool newProjectSyncIde = true;
                bool newProjectOpenAfterCreate = true;
                std::string newProjectError;

                std::vector<ProjectSceneFileEntry> cachedScenes;
                std::string openScenePathInput;
                std::string openSceneError;

                std::string saveSceneAsPathInput;
                std::string saveSceneAsError;
            };

        private:
            UiContext m_uiContext;
            CommandStack m_commandStack;
            ExternalCodeEditorService m_codeEditorService;
            ProjectLauncherService m_projectLauncher;
            ImGuiComposite* m_imguiComposite = nullptr;
            ProjectModalUiState m_projectModals;

            std::vector<DockWindowEntry> m_dockWindows;
            std::vector<MenuActionEntry> m_menuActions;
            std::vector<ToastNotification> m_toasts;
            std::string m_lastStatusMessage = "就绪";
            moodycamel::ConcurrentQueue<LogEntry> m_pendingLogs;

            EditorLanguage m_language = EditorLanguage::ZhCN;
            std::unordered_map<std::string, std::pair<std::string, std::string>> m_translationDict;
            bool m_showPreferencesWindow = false;
            int m_undoHistoryLimit = 100;

            bool m_needResetLayout = false;
            bool m_dockLayoutBuilt = false;
            float m_lastFrameTime = 0.016f;
            float m_smoothedFps = 60.0f;
            std::chrono::steady_clock::time_point m_lastTickClock{};
            bool m_hasTickClock = false;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_EDITOR_UI_COMPOSITE_H
