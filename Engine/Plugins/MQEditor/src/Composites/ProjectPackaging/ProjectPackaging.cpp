/**
 * @file ProjectPackaging.cpp
 * @brief 工程 4 阶段独立游戏打包流水线生命周期、打包面板 UI 与构建日志查看器实现
 */
#include "ProjectPackaging.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <cstdio>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kWindowTitle = "工程独立发布与打包 (Package Project)";
        }

        ProjectPackaging::ProjectPackaging() = default;

        ProjectPackaging::~ProjectPackaging()
        {
            onUnload();
        }

        Status ProjectPackaging::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerMenuAction(
                    "文件",
                    "打包工程 (Package Project...)",
                    "",
                    [this]() {
                        openPackagingModal();
                        if (m_editorUiComposite)
                        {
                            m_editorUiComposite->setDockWindowOpen(kWindowTitle, true);
                        }
                    });

                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderPackagingModal();
                    },
                    false);
            }

            return OkStatus();
        }

        Status ProjectPackaging::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void ProjectPackaging::onTick(float deltaTime)
        {
            if (!m_telemetry.isRunning.load())
                return;

            m_stageTimer += deltaTime;
            if (m_stageTimer < 0.2f)
                return;
            m_stageTimer = 0.0f;

            switch (m_telemetry.stage)
            {
            case PackageStage::Stage1_DependencyWalk:
                executeStage1_DependencyWalk();
                break;
            case PackageStage::Stage2_DllStripping:
                executeStage2_DllStripping();
                break;
            case PackageStage::Stage3_AssetCooking:
                executeStage3_AssetCooking();
                break;
            case PackageStage::Stage4_DistGeneration:
                executeStage4_DistGeneration();
                break;
            case PackageStage::Completed:
            case PackageStage::Failed:
            case PackageStage::Idle:
                break;
            }
        }

        void ProjectPackaging::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
            }
            m_editorUiComposite = nullptr;
            Asset::Composite::onUnload();
        }

        Status ProjectPackaging::startPackaging()
        {
            m_telemetry.isRunning.store(true);
            m_telemetry.progressRatio.store(0.05f);
            m_telemetry.stage = PackageStage::Stage1_DependencyWalk;
            m_telemetry.currentTaskDesc = "阶段 1：遍历工程依赖图与未保存项检查...";
            m_telemetry.logMessages.clear();
            logPackaging(">>> 启动工程打包流水线: " + m_config.projectName + " (目标: " + m_config.targetPlatform + ")");

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "打包流水线",
                    "启动独立游戏工程 4 阶段打包与烘焙流水线",
                    EditorBase::LogEntry::Level::Info,
                    2.5f);
            }

            return OkStatus();
        }

        void ProjectPackaging::cancelPackaging()
        {
            m_telemetry.isRunning.store(false);
            m_telemetry.stage = PackageStage::Idle;
            m_telemetry.currentTaskDesc = "已中止打包任务";
            logPackaging("### 用户主动取消了打包流水线");
        }

        void ProjectPackaging::logPackaging(const std::string& msg)
        {
            m_telemetry.logMessages.push_back(msg);
            if (m_editorUiComposite)
            {
                m_editorUiComposite->addLog(EditorBase::LogEntry::Level::Info, msg, "Packaging");
            }
        }

        void ProjectPackaging::renderBuildLogViewer()
        {
            ImGui::BeginChild("PackagingLogBox", ImVec2(0.0f, 160.0f), true);
            for (const auto& line : m_telemetry.logMessages)
            {
                ImGui::TextUnformatted(line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }

        void ProjectPackaging::renderPackagingModal()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kWindowTitle)
                : "工程独立发布与打包###工程独立发布与打包 (Package Project)";

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            ImGui::Text(L("MQEngine 独立游戏 4 阶段无损打包导出中枢",
                          "MQEngine Standalone Game 4-Stage Distribution Packaging Hub"));
            ImGui::Separator();

            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_config.projectName.c_str());
            if (ImGui::InputText(L("发布项目名", "Project Name"), nameBuf, sizeof(nameBuf)))
            {
                m_config.projectName = nameBuf;
                m_config.outputDistDir = "Dist/" + m_config.projectName;
            }

            char outDirBuf[256];
            std::snprintf(outDirBuf, sizeof(outDirBuf), "%s", m_config.outputDistDir.c_str());
            if (ImGui::InputText(L("输出发布目录", "Output Directory"), outDirBuf, sizeof(outDirBuf)))
            {
                m_config.outputDistDir = outDirBuf;
            }

            char sceneBuf[256];
            std::snprintf(sceneBuf, sizeof(sceneBuf), "%s", m_config.initialScene.c_str());
            if (ImGui::InputText(L("启动默认场景", "Initial Scene"), sceneBuf, sizeof(sceneBuf)))
            {
                m_config.initialScene = sceneBuf;
            }

            ImGui::Checkbox(L("归档打包为 .mqpak 虚拟文件系统", "Package into .mqpak Archive"), &m_config.packagePakArchive);
            ImGui::Checkbox(L("允许运行期外部文件拖入 (Tool 模式)", "Allow Runtime External Drop Import"), &m_config.includeExternalDropSupport);

            ImGui::Separator();

            float ratio = m_telemetry.progressRatio.load();
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), (std::to_string(static_cast<int>(ratio * 100.0f)) + "%").c_str());
            ImGui::Text(L("当前任务: %s", "Current Task: %s"), m_telemetry.currentTaskDesc.c_str());

            renderBuildLogViewer();

            ImGui::Separator();

            const bool running = m_telemetry.isRunning.load();
            if (!running)
            {
                if (ImGui::Button(L("开始一键打包 (Package Now)", "Package Now"), ImVec2(200.0f, 32.0f)))
                {
                    (void)startPackaging();
                }
            }
            else
            {
                if (ImGui::Button(L("中止打包 (Cancel)", "Cancel"), ImVec2(140.0f, 32.0f)))
                {
                    cancelPackaging();
                }
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::ProjectPackaging)
