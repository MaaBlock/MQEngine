/**
 * @file ExternalCodeEditorService.h
 * @brief 外部代码编辑器（CLion / JetBrains Rider / VS Code / Visual Studio / 自定义）自动探测、配置持久化与无阻塞进程启动服务
 */
#ifndef MQENGINE_EDITOR_BASE_EXTERNAL_CODE_EDITOR_SERVICE_H
#define MQENGINE_EDITOR_BASE_EXTERNAL_CODE_EDITOR_SERVICE_H

#include "../../EditorBaseApi.h"
#include "ProjectIdeConfigGenerator.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <unordered_set>

namespace MQEngine
{
    class ResourceLoader;

    namespace EditorBase
    {
        /**
         * @brief 官方支持的外部代码编辑器类型枚举
         */
        enum class CodeEditorKind : uint8_t
        {
            Auto = 0,       // 自动优选本机已安装的 IDE（CLion > Rider > VS Code > Visual Studio）
            CLion,          // JetBrains CLion
            Rider,          // JetBrains Rider
            VSCode,         // Visual Studio Code
            VisualStudio,   // Microsoft Visual Studio
            Custom          // 自定义可执行程序与命令行模板
        };

        /**
         * @brief 本机探测到的单个代码编辑器状态快照
         */
        struct DetectedCodeEditorInfo
        {
            CodeEditorKind kind = CodeEditorKind::CLion;
            std::string displayName;
            std::string executablePath;
            std::string versionHint;
            bool available = false;
        };

        /**
         * @brief 外部代码编辑器用户偏好配置（聚合结构体，支持 Boost.PFR 零宏反射持久化）
         */
        struct ExternalCodeEditorPreferences
        {
            CodeEditorKind preferredKind = CodeEditorKind::Auto;
            std::string customExecutablePath;
            std::string customOpenProjectArgs = "\"{project}\"";
            std::string customOpenFileArgs = "\"{project}\" --line {line} --column {col} \"{file}\"";
        };

        /**
         * @brief 源码文件行列定位信息（用于日志报错跳转与代码导航）
         */
        struct SourceFileLocation
        {
            std::string filePath;
            int line = 0;
            int column = 0;
        };

        /**
         * @brief 外部代码编辑器探测、偏好持久化与异步启动核心服务
         */
        class EDITOR_UI_COMPOSITE_API ExternalCodeEditorService
        {
        public:
            ExternalCodeEditorService();
            ~ExternalCodeEditorService() = default;

            ExternalCodeEditorService(const ExternalCodeEditorService&) = delete;
            ExternalCodeEditorService& operator=(const ExternalCodeEditorService&) = delete;

            void refreshDetectedEditors();

            Status loadPreferences(ResourceLoader* loader = nullptr);
            Status savePreferences(ResourceLoader* loader = nullptr) const;

            const ExternalCodeEditorPreferences& getPreferences() const noexcept { return m_preferences; }
            ExternalCodeEditorPreferences& mutablePreferences() noexcept { return m_preferences; }
            void setPreferences(const ExternalCodeEditorPreferences& preferences);
            void setPreferredKind(CodeEditorKind kind) noexcept { m_preferences.preferredKind = kind; }

            const std::vector<DetectedCodeEditorInfo>& getDetectedEditors() const noexcept { return m_detectedEditors; }
            const DetectedCodeEditorInfo* findDetectedEditor(CodeEditorKind kind) const noexcept;

            CodeEditorKind resolveEffectiveKind() const noexcept;
            std::string getEffectiveDisplayName(bool english = false) const;
            std::string getEffectiveExecutablePath() const;
            bool hasAvailableEditor() const noexcept;

            Status openProject(const std::string& projectDir = "") const;
            Status openFile(
                const std::string& filePath,
                int line = 0,
                int column = 0,
                const std::string& projectDir = "") const;
            StatusOr<IdeProjectSyncResult> syncProjectIdeFiles(
                const std::string& projectDir = "",
                ResourceLoader* loader = nullptr) const;

            static const char* getPresetDisplayName(CodeEditorKind kind, bool english = false) noexcept;
            static bool isSourceCodeFile(std::string_view filePath) noexcept;
            static std::string resolveActiveProjectDir();
            static bool tryExtractSourceLocation(std::string_view logText, SourceFileLocation& outLocation);

        private:
            DetectedCodeEditorInfo detectSingleEditor(CodeEditorKind kind) const;
            Status resolveLaunchTargetEditor(CodeEditorKind& outKind, std::string& outExePath) const;

        private:
            ExternalCodeEditorPreferences m_preferences{};
            std::vector<DetectedCodeEditorInfo> m_detectedEditors;
            mutable std::unordered_set<std::string> m_syncedProjectDirs;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_EXTERNAL_CODE_EDITOR_SERVICE_H
