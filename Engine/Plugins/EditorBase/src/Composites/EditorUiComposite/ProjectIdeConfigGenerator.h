/**
 * @file ProjectIdeConfigGenerator.h
 * @brief 为 MQEngine C++ 工程生成与同步 IDE 防错构建预设（CMakePresets.json、.idea/cmake.xml、.run/*.run.xml、.gitignore）
 */
#ifndef MQENGINE_EDITOR_BASE_PROJECT_IDE_CONFIG_GENERATOR_H
#define MQENGINE_EDITOR_BASE_PROJECT_IDE_CONFIG_GENERATOR_H

#include "../../EditorBaseApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine
{
    class ResourceLoader;

    namespace EditorBase
    {
        /**
         * @brief 单次工程 IDE 配置同步结果摘要
         */
        struct IdeProjectSyncResult
        {
            std::string projectDir;
            std::string projectName;
            std::string engineRootDir;
            std::string engineBuildDir;
            std::vector<std::string> targets;
            std::string editorCompositeTarget;
            std::string runtimeCompositeTarget;
            bool generatedCMakePresets = false;
            bool generatedIdeaCMake = false;
            bool generatedRunConfigs = false;
            bool generatedGitignore = false;
            bool updatedWorkspaceXml = false;
        };

        /**
         * @brief 工程 CMakePresets 与 CLion/Rider 防误构建配置生成器
         */
        class EDITOR_UI_COMPOSITE_API ProjectIdeConfigGenerator
        {
        public:
            /**
             * @brief 判断目录是否为包含 project.mq 与顶层 CMakeLists.txt 的 MQEngine C++ 工程
             */
            static bool isEngineCppProject(const std::string& projectDir, ResourceLoader* loader = nullptr);

            /**
             * @brief 为目标工程生成或对齐 CMakePresets.json、.idea/cmake.xml、.run/*.run.xml 与 .gitignore
             */
            static StatusOr<IdeProjectSyncResult> syncProjectIdeFiles(
                const std::string& projectDir,
                ResourceLoader* loader = nullptr);

            static std::string resolveEngineRootDir();
            static std::string resolveEngineBuildDir(const std::string& engineRootDir);
        };
    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_PROJECT_IDE_CONFIG_GENERATOR_H
