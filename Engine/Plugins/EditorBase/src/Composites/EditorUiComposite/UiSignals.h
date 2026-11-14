/**
 * @file UiSignals.h
 * @brief 编辑器 UI 强类型视图状态与信号契约定义
 */
#ifndef MQENGINE_EDITOR_BASE_UI_SIGNALS_H
#define MQENGINE_EDITOR_BASE_UI_SIGNALS_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <string>
#include <vector>
#include <concepts>
#include <cstdint>

namespace MQEngine
{
    namespace EditorBase
    {
        struct UiSignalTag {};
        struct UiViewStateTag {};

        template <typename T>
        concept UiSignalConcept = std::derived_from<std::decay_t<T>, UiSignalTag>;

        template <typename T>
        concept UiViewStateConcept = std::derived_from<std::decay_t<T>, UiViewStateTag>;

        // ==================== 1. 编辑器通用操作信号 ====================

        enum class EditorLanguage : uint8_t
        {
            ZhCN = 0, // 简体中文
            EnUS = 1  // English (US)
        };

        struct LanguageChangedSignal : UiSignalTag
        {
            EditorLanguage language = EditorLanguage::ZhCN;
        };

        struct SaveProjectSignal : UiSignalTag {};
        struct SaveActiveSceneSignal : UiSignalTag {};
        struct RequestResetLayoutSignal : UiSignalTag {};
        struct UndoCommandSignal : UiSignalTag {};
        struct RedoCommandSignal : UiSignalTag {};
        struct RequestExitSignal : UiSignalTag {};

        struct SelectEntitySignal : UiSignalTag
        {
            uint32_t entityId = 0;
            std::string entityName;
        };

        struct CreateEntitySignal : UiSignalTag
        {
            std::string name = "GameObject";
            uint32_t parentEntityId = 0;
        };

        struct DeleteEntitySignal : UiSignalTag
        {
            uint32_t entityId = 0;
        };

        struct DuplicateEntitySignal : UiSignalTag
        {
            uint32_t entityId = 0;
        };

        struct SelectAssetSignal : UiSignalTag
        {
            std::string assetPath;
            std::string assetExtension;
        };

        struct OpenProjectInCodeEditorSignal : UiSignalTag
        {
            std::string projectDir;
        };

        struct OpenFileInCodeEditorSignal : UiSignalTag
        {
            std::string filePath;
            int line = 0;
            int column = 0;
        };

        struct GenerateProjectCMakePresetsSignal : UiSignalTag
        {
            std::string projectDir;
        };

        struct NewSceneSignal : UiSignalTag {};

        struct OpenSceneSignal : UiSignalTag
        {
            std::string scenePath;
        };

        struct SaveSceneAsSignal : UiSignalTag
        {
            std::string scenePath;
        };

        struct SetCurrentSceneAsMainSignal : UiSignalTag {};

        struct OpenProjectSignal : UiSignalTag
        {
            std::string projectDir;
        };

        struct CreateProjectFromTemplateSignal : UiSignalTag
        {
            std::string templateId;
            std::string targetParentDir;
            std::string projectName;
        };

        // ==================== 2. 视图状态快照定义 ====================

        struct MainMenuViewState : UiViewStateTag
        {
            bool canUndo = false;
            bool canRedo = false;
            std::string undoActionName;
            std::string redoActionName;
            std::string activeProjectName = "MQProject";
            std::string activeSceneName;
            std::string activeScenePath;
            std::string mainScenePath;
        };

        struct LogEntry
        {
            enum class Level : uint8_t { Trace = 0, Debug, Info, Warn, Error, Critical };
            Level level = Level::Info;
            std::string message;
            std::string timestamp;
            std::string source;
        };

        struct LogWindowViewState : UiViewStateTag
        {
            bool autoScroll = true;
            int selectedLevelFilter = 0; // 0: All, 1: Info+, 2: Warn+, 3: Error+
            std::string searchFilter;
            std::vector<LogEntry> logs;
        };

        struct ProfilerStatsViewState : UiViewStateTag
        {
            float fps = 60.0f;
            float frameTimeMs = 16.6f;
            uint64_t frameIndex = 0;
            uint32_t workerThreadCount = 0;
            uint32_t ioThreadCount = 0;
            uint64_t totalBytesRead = 0;
            uint64_t totalBytesWritten = 0;
            size_t memoryUsageBytes = 0;
            std::vector<float> frameHistory;
        };

        struct DockSpaceViewState : UiViewStateTag
        {
            bool isDockingInitialized = false;
            bool showLogWindow = true;
            bool showProfilerWindow = true;
            bool showSceneViewport = true;
            bool showHierarchyTree = true;
            bool showInspector = true;
            bool showAssetBrowser = true;
            bool showShaderEditor = false;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_UI_SIGNALS_H
