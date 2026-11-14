/**
 * @file EditorUiComposite.cpp
 * @brief 编辑器 UI 底座生命周期、窗口/菜单注册与多语言字典实现
 */
#include "EditorUiComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/asset/loader/AsyncLoader.h>

#include <algorithm>

namespace MQEngine
{
    namespace EditorBase
    {
        EditorUiComposite::EditorUiComposite() = default;

        EditorUiComposite::~EditorUiComposite()
        {
            onUnload();
        }

        Status EditorUiComposite::onLoad(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_imguiComposite = dynamic_cast<ImGuiComposite*>(assetModule->compositeManager.getComposite("ImGuiComposite"));
            }

            initDefaultMenuActions();

            m_codeEditorService.refreshDetectedEditors();
            (void)m_codeEditorService.loadPreferences();
            (void)m_projectLauncher.loadRecentProjects();
            const std::string startupProjectDir = getActiveProjectDirectory();
            if (!startupProjectDir.empty() && ProjectLauncherService::isValidProjectDir(startupProjectDir))
            {
                (void)m_projectLauncher.recordRecentProject(startupProjectDir);
            }
            if (m_codeEditorService.hasAvailableEditor())
            {
                addLog(
                    LogEntry::Level::Info,
                    "已探测外部代码编辑器: " + m_codeEditorService.getEffectiveDisplayName(false) +
                        " -> " + m_codeEditorService.getEffectiveExecutablePath(),
                    "EditorBase");
            }

            // 注册默认底座面板（统一控制台日志窗口）
            registerDockWindow("控制台日志 (Console Log)", EditorDockSlot::Bottom, [this]() {
                renderLogWindow();
            });

            // 绑定命令栈事件回调，对 Undo / Redo / 命令执行提供即时消息提示与日志
            m_commandStack.setEventCallback([this](const std::string& actionType, const std::string& commandName) {
                if (actionType == "Undo")
                {
                    const std::string msg = std::string(tr("已撤销: ", "Undone: ")) + commandName;
                    addLog(LogEntry::Level::Info, msg, "CommandStack");
                    showToast(tr("撤销操作", "Undo"), msg, LogEntry::Level::Info, 2.6f);
                }
                else if (actionType == "Redo")
                {
                    const std::string msg = std::string(tr("已重做: ", "Redone: ")) + commandName;
                    addLog(LogEntry::Level::Info, msg, "CommandStack");
                    showToast(tr("重做操作", "Redo"), msg, LogEntry::Level::Info, 2.6f);
                }
                else if (actionType == "UndoEmpty")
                {
                    const std::string msg = tr("当前没有可撤销的历史记录", "Nothing to undo in history");
                    addLog(LogEntry::Level::Warn, msg, "CommandStack");
                    showToast(tr("撤销提示", "Undo Notice"), msg, LogEntry::Level::Warn, 2.0f);
                }
                else if (actionType == "RedoEmpty")
                {
                    const std::string msg = tr("当前没有可重做的历史记录", "Nothing to redo in history");
                    addLog(LogEntry::Level::Warn, msg, "CommandStack");
                    showToast(tr("重做提示", "Redo Notice"), msg, LogEntry::Level::Warn, 2.0f);
                }
                else if (actionType == "Execute")
                {
                    m_lastStatusMessage = std::string(tr("执行命令: ", "Executed: ")) + commandName;
                }
            });

            addLog(LogEntry::Level::Info, "EditorUiComposite 成功装载，UI 核心底座已就绪", "EditorBase");
            return OkStatus();
        }

        Status EditorUiComposite::onInit(Runtime::Engine* engine)
        {
            ZoneScoped();
            CHECK_STATUS(Asset::Composite::onInit(engine));

            if (m_imguiComposite)
            {
                m_imguiComposite->registerUiCallback("EditorBase_MainLayout", [this]() {
                    ZoneScopedN("EditorUiComposite::renderFrame");
                    if (m_imguiComposite)
                    {
                        m_imguiComposite->syncCallerContext();
                    }
                    renderDockSpace();
                    renderMainMenuBar();
                    renderRegisteredPanels();
                    renderPreferencesWindow();
                    renderProjectAndSceneModals();
                    renderToastNotifications();
                });
            }

            // 注册常用快捷信号响应
            m_uiContext.registerSignalHandler<UndoCommandSignal>([this](const UndoCommandSignal&) {
                m_commandStack.undo();
            });
            m_uiContext.registerSignalHandler<RedoCommandSignal>([this](const RedoCommandSignal&) {
                m_commandStack.redo();
            });
            m_uiContext.registerSignalHandler<RequestResetLayoutSignal>([this](const RequestResetLayoutSignal&) {
                requestResetLayout();
            });
            m_uiContext.registerSignalHandler<SaveActiveSceneSignal>([this](const SaveActiveSceneSignal&) {
                if (m_uiContext.getSignalHandlerCount<SaveActiveSceneSignal>() <= 1)
                {
                    (void)saveActiveScene();
                }
            });
            m_uiContext.registerSignalHandler<SaveProjectSignal>([this](const SaveProjectSignal&) {
                (void)saveProject();
            });
            m_uiContext.registerSignalHandler<OpenProjectSignal>([this](const OpenProjectSignal& sig) {
                if (!sig.projectDir.empty())
                {
                    (void)openProjectInNewEditor(sig.projectDir, true);
                }
            });
            m_uiContext.registerSignalHandler<CreateProjectFromTemplateSignal>([this](const CreateProjectFromTemplateSignal& sig) {
                CreateProjectFromTemplateRequest req;
                req.templateId = sig.templateId;
                req.targetParentDir = sig.targetParentDir;
                req.projectName = sig.projectName;
                (void)createProjectFromTemplate(req, true);
            });
            m_uiContext.registerSignalHandler<OpenProjectInCodeEditorSignal>([this](const OpenProjectInCodeEditorSignal& sig) {
                (void)openProjectInCodeEditor(sig.projectDir, true);
            });
            m_uiContext.registerSignalHandler<OpenFileInCodeEditorSignal>([this](const OpenFileInCodeEditorSignal& sig) {
                (void)openFileInCodeEditor(sig.filePath, sig.line, sig.column, true);
            });
            m_uiContext.registerSignalHandler<GenerateProjectCMakePresetsSignal>([this](const GenerateProjectCMakePresetsSignal& sig) {
                (void)generateProjectCMakePresets(sig.projectDir, true);
            });

            return OkStatus();
        }

        void EditorUiComposite::onTick(float deltaTime)
        {
            ZoneScoped();
            const auto nowClock = std::chrono::steady_clock::now();
            float measuredDt = deltaTime;
            if (m_hasTickClock)
            {
                const float wallDt = std::chrono::duration<float>(nowClock - m_lastTickClock).count();
                if (wallDt > 0.00005f && wallDt < 0.5f)
                {
                    measuredDt = wallDt;
                }
            }
            m_lastTickClock = nowClock;
            m_hasTickClock = true;
            m_lastFrameTime = measuredDt;

            const float instFps = (measuredDt > 0.00005f) ? (1.0f / measuredDt) : 60.0f;
            m_smoothedFps = (m_smoothedFps <= 0.1f) ? instFps : (m_smoothedFps * 0.88f + instFps * 0.12f);

            // 0. 刷新并发无锁队列待处理日志至主线程 UI 日志状态
            flushPendingLogs();

            // 1. 同步 Profiler 状态快照
            auto& profilerState = m_uiContext.mutableState<ProfilerStatsViewState>();
            profilerState.frameTimeMs = measuredDt * 1000.0f;
            profilerState.fps = m_smoothedFps;
            profilerState.frameIndex++;

            profilerState.frameHistory.push_back(profilerState.frameTimeMs);
            if (profilerState.frameHistory.size() > 120)
            {
                profilerState.frameHistory.erase(profilerState.frameHistory.begin());
            }

            auto* workHost = Host::WorkHost::InstancePtr();
            if (workHost)
            {
                profilerState.workerThreadCount = workHost->getWorkerCount();
                profilerState.ioThreadCount = workHost->getIoThreadCount();
            }

            auto* asyncLoader = Asset::AsyncLoader::InstancePtr();
            if (asyncLoader)
            {
                profilerState.totalBytesRead = asyncLoader->getTotalBytesRead();
                profilerState.totalBytesWritten = asyncLoader->getTotalBytesWritten();
            }

            // 2. 同步主菜单状态
            auto& menuState = m_uiContext.mutableState<MainMenuViewState>();
            menuState.canUndo = m_commandStack.canUndo();
            menuState.canRedo = m_commandStack.canRedo();
            menuState.undoActionName = m_commandStack.getUndoName();
            menuState.redoActionName = m_commandStack.getRedoName();

            // 3. 同步并派发 UI 信号
            m_uiContext.syncAllStates();
            m_uiContext.dispatchPendingSignals();

            // 4. 更新浮动消息提示生命周期
            for (auto& toast : m_toasts)
            {
                toast.remainingSec -= deltaTime;
            }
            m_toasts.erase(
                std::remove_if(m_toasts.begin(), m_toasts.end(),
                               [](const ToastNotification& t) { return t.remainingSec <= 0.0f; }),
                m_toasts.end());
        }

        void EditorUiComposite::onUnload()
        {
            if (m_imguiComposite)
            {
                m_imguiComposite->unregisterUiCallback("EditorBase_MainLayout");
            }

            m_dockWindows.clear();
            m_menuActions.clear();
            m_uiContext.clear();
            m_commandStack.clear();
            m_imguiComposite = nullptr;

            Asset::Composite::onUnload();
        }

        void EditorUiComposite::registerDockWindow(
            const std::string& name,
            EditorDockSlot slot,
            std::function<void()> renderCallback,
            bool defaultOpen)
        {
            assert(!name.empty() && "停靠窗口名称不可为空");
            assert(renderCallback != nullptr && "渲染回调不可为空");

            auto it = std::find_if(m_dockWindows.begin(), m_dockWindows.end(),
                                   [&name](const DockWindowEntry& entry) { return entry.name == name; });
            if (it != m_dockWindows.end())
            {
                it->slot = slot;
                it->renderCallback = std::move(renderCallback);
                it->isOpen = defaultOpen;
            }
            else
            {
                m_dockWindows.push_back(DockWindowEntry{
                    .name = name,
                    .slot = slot,
                    .renderCallback = std::move(renderCallback),
                    .isOpen = defaultOpen
                });
            }
        }

        void EditorUiComposite::unregisterDockWindow(const std::string& name)
        {
            auto it = std::remove_if(m_dockWindows.begin(), m_dockWindows.end(),
                                     [&name](const DockWindowEntry& entry) { return entry.name == name; });
            m_dockWindows.erase(it, m_dockWindows.end());
        }

        void EditorUiComposite::setDockWindowOpen(const std::string& name, bool isOpen)
        {
            for (auto& entry : m_dockWindows)
            {
                if (entry.name == name)
                {
                    entry.isOpen = isOpen;
                    return;
                }
            }
        }

        bool* EditorUiComposite::getDockWindowOpenPtr(const std::string& name)
        {
            for (auto& entry : m_dockWindows)
            {
                if (entry.name == name)
                {
                    return &entry.isOpen;
                }
            }
            return nullptr;
        }

        void EditorUiComposite::registerMenuAction(
            const std::string& category,
            const std::string& item,
            const std::string& shortcut,
            std::function<void()> callback)
        {
            assert(!category.empty() && !item.empty() && callback != nullptr);
            m_menuActions.push_back(MenuActionEntry{category, item, shortcut, std::move(callback)});
        }

        void EditorUiComposite::addLog(LogEntry::Level level, const std::string& message, const std::string& source)
        {
            m_pendingLogs.enqueue(LogEntry{
                .level = level,
                .message = message,
                .timestamp = "Now",
                .source = source
            });
        }

        void EditorUiComposite::flushPendingLogs()
        {
            LogEntry entry;
            bool hasNewLog = false;
            auto& logState = m_uiContext.mutableState<LogWindowViewState>();

            while (m_pendingLogs.try_dequeue(entry))
            {
                hasNewLog = true;
                m_lastStatusMessage = "[" + entry.source + "] " + entry.message;
                if (entry.source != "CommandStack" && entry.source != "EditorBase" && entry.source != "CodeEditor")
                {
                    showToast(entry.source, entry.message, entry.level, 2.5f);
                }

                logState.logs.push_back(std::move(entry));
            }

            if (hasNewLog && logState.logs.size() > 500)
            {
                logState.logs.erase(logState.logs.begin(), logState.logs.begin() + 50);
            }
        }

        void EditorUiComposite::showToast(const std::string& title, const std::string& message, LogEntry::Level level, float durationSec)
        {
            m_lastStatusMessage = title + ": " + message;
            m_toasts.push_back(ToastNotification{
                .title = title,
                .message = message,
                .level = level,
                .remainingSec = durationSec,
                .totalSec = std::max(0.5f, durationSec)
            });

            if (m_toasts.size() > 6)
            {
                m_toasts.erase(m_toasts.begin());
            }
        }

        void EditorUiComposite::setLanguage(EditorLanguage lang)
        {
            if (m_language == lang)
            {
                return;
            }
            m_language = lang;
            if (auto* eng = Runtime::Engine::getInstance(); eng && eng->application)
            {
                eng->application->applicationRegisterInfo.local =
                    (lang == EditorLanguage::EnUS) ? FCT::Local::en_US : FCT::Local::zh_CN;
            }
            SetLocal(lang == EditorLanguage::EnUS ? "en_US" : "zh_CN");

            LanguageChangedSignal sig;
            sig.language = lang;
            m_uiContext.emit(std::move(sig));

            showToast(
                tr("语言设置", "Language Switched"),
                tr("已切换界面语言为: 简体中文", "Editor language switched to: English"),
                LogEntry::Level::Info,
                2.5f);
        }

        void EditorUiComposite::registerTranslation(
            const std::string& key,
            const std::string& zhVal,
            const std::string& enVal)
        {
            if (!key.empty())
            {
                m_translationDict[key] = {zhVal, enVal};
            }
        }

        const char* EditorUiComposite::trKey(
            const std::string& key,
            const char* fallbackZh,
            const char* fallbackEn) const
        {
            auto it = m_translationDict.find(key);
            if (it != m_translationDict.end())
            {
                return (m_language == EditorLanguage::EnUS)
                    ? it->second.second.c_str()
                    : it->second.first.c_str();
            }
            return tr(fallbackZh, fallbackEn);
        }

        std::string EditorUiComposite::localizeLabel(std::string_view label) const
        {
            const size_t openPos = label.rfind(" (");
            if (openPos != std::string_view::npos && !label.empty() && label.back() == ')')
            {
                const std::string_view zhPart = label.substr(0, openPos);
                const std::string_view enPart = label.substr(openPos + 2, label.size() - openPos - 3);
                if (!zhPart.empty() && !enPart.empty())
                {
                    return std::string(m_language == EditorLanguage::EnUS ? enPart : zhPart);
                }
            }
            return std::string(label);
        }

        std::string EditorUiComposite::getDockWindowTitle(std::string_view registeredName) const
        {
            return localizeLabel(registeredName) + "###" + std::string(registeredName);
        }

        void EditorUiComposite::initDefaultMenuActions()
        {
            // 初始化内置命令
        }

        Status EditorUiComposite::openProjectInCodeEditor(const std::string& projectDir, bool showToastFeedback)
        {
            const std::string resolvedProj = projectDir.empty() ? getActiveProjectDirectory() : projectDir;
            const std::string ideName = getActiveCodeEditorDisplayName();
            Status st = m_codeEditorService.openProject(resolvedProj);
            if (!st.ok())
            {
                const std::string errMsg = std::string(st.message());
                addLog(LogEntry::Level::Warn, errMsg, "CodeEditor");
                if (showToastFeedback)
                {
                    showToast(tr("代码编辑器启动失败", "Code Editor Error"), errMsg, LogEntry::Level::Warn, 3.6f);
                }
                m_showPreferencesWindow = true;
                return st;
            }

            const std::string okMsg = std::string(tr("已在 ", "Opened project in ")) + ideName +
                tr(" 中打开工程: ", ": ") + SAI::Path::fileName(resolvedProj);
            addLog(LogEntry::Level::Info, okMsg + " (" + resolvedProj + ")", "CodeEditor");
            if (showToastFeedback)
            {
                showToast(ideName, okMsg, LogEntry::Level::Info, 2.8f);
            }
            return OkStatus();
        }

        Status EditorUiComposite::openFileInCodeEditor(
            const std::string& filePath,
            int line,
            int column,
            bool showToastFeedback)
        {
            const std::string ideName = getActiveCodeEditorDisplayName();
            Status st = m_codeEditorService.openFile(filePath, line, column);
            if (!st.ok())
            {
                const std::string errMsg = std::string(st.message());
                addLog(LogEntry::Level::Warn, errMsg, "CodeEditor");
                if (showToastFeedback)
                {
                    showToast(tr("打开源码失败", "Open Source Failed"), errMsg, LogEntry::Level::Warn, 3.6f);
                }
                if (!m_codeEditorService.hasAvailableEditor())
                {
                    m_showPreferencesWindow = true;
                }
                return st;
            }

            std::string fileLabel = SAI::Path::fileName(filePath);
            if (line > 0)
            {
                fileLabel += ":" + std::to_string(line);
            }
            const std::string okMsg = std::string(tr("已在 ", "Opened in ")) + ideName +
                tr(" 中打开: ", ": ") + fileLabel;
            addLog(LogEntry::Level::Info, okMsg, "CodeEditor");
            if (showToastFeedback)
            {
                showToast(ideName, okMsg, LogEntry::Level::Info, 2.6f);
            }
            return OkStatus();
        }

        StatusOr<IdeProjectSyncResult> EditorUiComposite::generateProjectCMakePresets(
            const std::string& projectDir,
            bool showToastFeedback)
        {
            const std::string resolvedProj = projectDir.empty() ? getActiveProjectDirectory() : projectDir;
            auto syncOr = m_codeEditorService.syncProjectIdeFiles(resolvedProj);
            if (!syncOr.ok())
            {
                const std::string errMsg = std::string(syncOr.status().message());
                addLog(LogEntry::Level::Warn, errMsg, "CodeEditor");
                if (showToastFeedback)
                {
                    showToast(
                        tr("IDE 预设同步失败", "IDE Preset Sync Failed"),
                        errMsg,
                        LogEntry::Level::Warn,
                        3.4f);
                }
                return syncOr.status();
            }

            const auto& res = syncOr.value();
            const bool anyUpdated = res.generatedCMakePresets || res.generatedIdeaCMake ||
                                    res.generatedRunConfigs || res.generatedGitignore || res.updatedWorkspaceXml;
            const std::string msg = anyUpdated
                ? std::string(tr("已生成/同步工程 IDE 构建预设与防误构建配置: ", "Synced IDE CMakePresets & configs: ")) +
                      res.projectName
                : std::string(tr("工程 IDE 构建预设已是最新状态: ", "IDE CMakePresets already up-to-date: ")) +
                      res.projectName;
            addLog(
                LogEntry::Level::Info,
                msg + " (" + res.projectDir + ", MQENGINE_BUILD_DIR=" + res.engineBuildDir + ")",
                "CodeEditor");
            if (showToastFeedback)
            {
                showToast(
                    tr("IDE 构建预设", "IDE CMakePresets"),
                    msg,
                    LogEntry::Level::Info,
                    3.0f);
            }
            return syncOr;
        }

        std::string EditorUiComposite::getActiveCodeEditorDisplayName() const
        {
            return m_codeEditorService.getEffectiveDisplayName(m_language == EditorLanguage::EnUS);
        }

        bool EditorUiComposite::hasAvailableCodeEditor() const noexcept
        {
            return m_codeEditorService.hasAvailableEditor();
        }

        std::string EditorUiComposite::getActiveProjectDirectory() const
        {
            return ExternalCodeEditorService::resolveActiveProjectDir();
        }

        bool EditorUiComposite::isCodeEditorSourceFile(std::string_view filePath) noexcept
        {
            return ExternalCodeEditorService::isSourceCodeFile(filePath);
        }

    } // namespace EditorBase
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::EditorBase::EditorUiComposite)
