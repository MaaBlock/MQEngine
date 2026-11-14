/**
 * @file DouyinPackagerEditorComposite.h
 * @brief 桌面编辑器可视化抖音小游戏配置持久化、预算分析与一键五步烘焙预览面板（FEAT-01, FEAT-02）
 */
#ifndef MQENGINE_DOUYIN_PACKAGER_EDITOR_COMPOSITE_H
#define MQENGINE_DOUYIN_PACKAGER_EDITOR_COMPOSITE_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "Composites/DouyinPackagerCliComposite/DouyinPackagerCliComposite.h"

#include <MQEngine/asset/interface/Composite.h>

namespace MQEngine::EditorBase
{
    class EditorUiComposite;
}

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 编辑器可视化抖音小游戏打包配置与预览面板 Composite（仅在 Editor + ImGui 宿主激活）
     */
    class DOUYIN_PACKAGER_EDITOR_API DouyinPackagerEditorComposite : public Asset::Composite
    {
    public:
        DouyinPackagerEditorComposite();
        ~DouyinPackagerEditorComposite() override;

        Status onLoad(Runtime::Engine* engine) override;
        Status onInit(Runtime::Engine* engine) override;
        void onTick(float deltaTime) override;
        void onUnload() override;

        std::string getName() const override { return "DouyinPackagerEditorComposite"; }
        std::string getPluginName() const override { return "DouyinMiniGame"; }
        std::vector<std::string> getDependencies() const override { return {"EditorUiComposite", "DouyinPackagerCliComposite"}; }
        std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

        DouyinPackagerConfig& getEditingConfig() noexcept { return m_config; }
        const DouyinPackagerConfig& getEditingConfig() const noexcept { return m_config; }

        Status loadConfigFromCurrentProject();
        Status saveConfigToCurrentProject();
        Status triggerOneClickCook();
        Status previewInDouyinDevTools();

        const std::vector<std::string>& getBuildLogs() const noexcept { return m_buildLogs; }
        float getLastProgressRatio() const noexcept { return m_progressRatio; }
        const std::string& getOutputDir() const noexcept { return m_outputDir; }
        void setOutputDir(std::string outDir) { m_outputDir = std::move(outDir); }

    private:
        std::string resolveActiveProjectDir() const;
        void appendLog(const std::string& message, bool isError = false);

        void renderPackagerWindow();
        void renderIdentitySection();
        void renderRenderBudgetSection();
        void renderStreamingBudgetSection();
        void renderPlatformEcoSection();
        void renderActionToolbarAndLogs();

    private:
        EditorBase::EditorUiComposite* m_editorUiComposite{nullptr};
        DouyinPackagerConfig m_config;
        std::string m_outputDir{"Dist/DouyinMiniGame"};
        std::string m_lastMainPackageDir;
        std::string m_currentStepLabel{"就绪"};
        float m_progressRatio{0.0f};
        std::vector<std::string> m_buildLogs;
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_PACKAGER_EDITOR_COMPOSITE_H
