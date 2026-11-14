/**
 * @file ProjectPackaging.h
 * @brief 工程 4 阶段无损独立游戏打包流水线 Composite（tags: [Editor, ImGui]，独立编译为 ProjectPackaging.dll）
 */
#ifndef MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_H
#define MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_H

#include "../../MQEditorApi.h"
#include "ProjectPackagingTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 独立工程打包流水线 Composite：
         *        挂载到 "文件 -> 打包工程 (Package Project...)" 菜单，
         *        执行：1. 依赖遍历与未保存校验；2. 按 tags 裁剪 Composite DLL；
         *        3. 多线程资产烘焙与场景二进制固化；4. 产出 Dist/<ProjectName>/ 独立运行目录
         */
        class PROJECT_PACKAGING_COMPOSITE_API ProjectPackaging : public Asset::Composite
        {
        public:
            ProjectPackaging();
            ~ProjectPackaging() override;

            ProjectPackaging(const ProjectPackaging&) = delete;
            ProjectPackaging& operator=(const ProjectPackaging&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "ProjectPackaging"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 打包流水线控制 ====================

            void openPackagingModal() noexcept { m_showModal = true; }
            Status startPackaging();
            void cancelPackaging();
            bool isPackagingRunning() const noexcept { return m_telemetry.isRunning.load(); }

        private:
            // 打包 UI 面板与构建日志查看器 (ProjectPackaging.cpp)
            void renderPackagingModal();
            void renderBuildLogViewer();
            void logPackaging(const std::string& msg);

            // 4 阶段打包流水线执行实现 (ProjectPackagingPipeline.cpp)
            void executeStage1_DependencyWalk();
            void executeStage2_DllStripping();
            void executeStage3_AssetCooking();
            void executeStage4_DistGeneration();

            std::string locateBinarySourceDir();
            void copyDirectoryRecursive(const std::string& srcDir, const std::string& dstDir);
            bool verifyStagedPackage();

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;

            PackageBuildConfig m_config;
            PackageJobState m_telemetry;
            bool m_showModal = false;
            float m_stageTimer = 0.0f;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_H
