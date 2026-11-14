/**
 * @file DouyinPackagerEditorComposite.cpp
 * @brief 抖音小游戏可视化打包面板生命周期、TOML 配置同步与一键五步烘焙交互实现
 */
#include "DouyinPackagerEditorComposite.h"

#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/resource.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cstdio>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        constexpr const char* kDouyinWindowTitle = "抖音小游戏发布中心 (Douyin MiniGame Packager)";
    }

    DouyinPackagerEditorComposite::DouyinPackagerEditorComposite() = default;

    DouyinPackagerEditorComposite::~DouyinPackagerEditorComposite()
    {
        onUnload();
    }

    Status DouyinPackagerEditorComposite::onLoad(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onLoad(engine));

        if (auto* assetMod = Asset::Asset::InstancePtr())
        {
            m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                assetMod->compositeManager.getComposite("EditorUiComposite"));
        }

        if (m_editorUiComposite)
        {
            m_editorUiComposite->registerMenuAction(
                "文件",
                "抖音小游戏打包面板 (Douyin MiniGame...)",
                "",
                [this]() {
                    (void)loadConfigFromCurrentProject();
                    if (m_editorUiComposite)
                    {
                        m_editorUiComposite->setDockWindowOpen(kDouyinWindowTitle, true);
                    }
                });

            m_editorUiComposite->registerDockWindow(
                kDouyinWindowTitle,
                EditorBase::EditorDockSlot::Center,
                [this]() {
                    if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                    {
                        m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                    }
                    renderPackagerWindow();
                },
                false);
        }

        return OkStatus();
    }

    Status DouyinPackagerEditorComposite::onInit(Runtime::Engine* engine)
    {
        CHECK_STATUS(Asset::Composite::onInit(engine));
        (void)loadConfigFromCurrentProject();
        return OkStatus();
    }

    void DouyinPackagerEditorComposite::onTick(float /*deltaTime*/)
    {
    }

    void DouyinPackagerEditorComposite::onUnload()
    {
        if (m_editorUiComposite)
        {
            m_editorUiComposite->unregisterDockWindow(kDouyinWindowTitle);
            m_editorUiComposite = nullptr;
        }
        Asset::Composite::onUnload();
    }

    std::string DouyinPackagerEditorComposite::resolveActiveProjectDir() const
    {
        if (auto* res = Resource::Resource::InstancePtr())
        {
            if (!res->getProjectDir().empty())
            {
                return res->getProjectDir();
            }
            if (!res->getEngineDir().empty())
            {
                return res->getEngineDir();
            }
        }
        return ".";
    }

    void DouyinPackagerEditorComposite::appendLog(const std::string& message, bool isError)
    {
        m_buildLogs.push_back(message);
        if (m_editorUiComposite)
        {
            m_editorUiComposite->addLog(
                isError ? EditorBase::LogEntry::Level::Error : EditorBase::LogEntry::Level::Info,
                message,
                "DouyinPackager");
        }
    }

    Status DouyinPackagerEditorComposite::loadConfigFromCurrentProject()
    {
        auto* res = Resource::Resource::InstancePtr();
        ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;
        const std::string projDir = resolveActiveProjectDir();
        const std::string cfgPath = DouyinConfigManager::resolveConfigPath(projDir);
        if (loader && !loader->exists(cfgPath))
        {
            return OkStatus();
        }

        auto loadedOr = DouyinConfigManager::loadFromProject(loader, projDir);
        if (!loadedOr.ok())
        {
            appendLog("加载配置失败: " + std::string(loadedOr.status().message()), true);
            return loadedOr.status();
        }

        m_config = *loadedOr;
        appendLog("已从 Config/douyin_minigame.toml 同步工程配置");
        return OkStatus();
    }

    Status DouyinPackagerEditorComposite::saveConfigToCurrentProject()
    {
        auto* res = Resource::Resource::InstancePtr();
        ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;
        const std::string projDir = resolveActiveProjectDir();

        Status st = DouyinConfigManager::saveToProject(loader, projDir, m_config);
        if (!st.ok())
        {
            appendLog("保存配置失败: " + std::string(st.message()), true);
            return st;
        }

        appendLog("配置已持久化至 " + DouyinConfigManager::resolveConfigPath(projDir));
        return OkStatus();
    }

    Status DouyinPackagerEditorComposite::triggerOneClickCook()
    {
        CHECK_STATUS(saveConfigToCurrentProject());

        auto* res = Resource::Resource::InstancePtr();
        ResourceLoader* loader = res ? res->getResourceLoader() : nullptr;
        const std::string projDir = resolveActiveProjectDir();

        DouyinCookRequest req;
        req.projectDir = projDir;
        req.outputDir = SAI::Path::isAbsolute(m_outputDir)
            ? m_outputDir
            : SAI::Path::join(projDir, m_outputDir);
        req.useOverrideConfig = true;
        req.overrideConfig = m_config;

        m_buildLogs.clear();
        m_progressRatio = 0.05f;
        appendLog(">>> 启动抖音小游戏五步全自动烘焙流水线: " + m_config.projectName);

        auto cookOr = DouyinPackagerCliComposite::runCookPipeline(
            loader,
            req,
            [this](int /*step*/, float ratio, const std::string& msg) {
                m_progressRatio = ratio;
                m_currentStepLabel = msg;
                appendLog(msg);
            });

        if (!cookOr.ok())
        {
            m_currentStepLabel = "构建失败";
            appendLog("烘焙失败: " + std::string(cookOr.status().message()), true);
            return cookOr.status();
        }

        m_lastMainPackageDir = cookOr->exportReport.mainPackageDir;
        m_progressRatio = 1.0f;
        m_currentStepLabel = "构建成功";
        appendLog("MainPackage 导出目录: " + m_lastMainPackageDir +
                  " (" + std::to_string(cookOr->exportReport.mainPackageRawBytes / 1024ull) + " KB)");
        return OkStatus();
    }

    Status DouyinPackagerEditorComposite::previewInDouyinDevTools()
    {
        if (m_lastMainPackageDir.empty())
        {
            const std::string projDir = resolveActiveProjectDir();
            const std::string baseOut = SAI::Path::isAbsolute(m_outputDir)
                ? m_outputDir
                : SAI::Path::join(projDir, m_outputDir);
            m_lastMainPackageDir = SAI::Path::join(baseOut, "MainPackage");
        }

        Status st = DouyinPackagerCliComposite::launchDouyinDevTools(m_config, m_lastMainPackageDir);
        if (!st.ok())
        {
            appendLog("启动抖音开发者工具提示: " + std::string(st.message()), true);
            return st;
        }
        appendLog("已拉起抖音开发者工具加载 MainPackage");
        return OkStatus();
    }

    void DouyinPackagerEditorComposite::renderPackagerWindow()
    {
        const std::string winTitle = m_editorUiComposite
            ? m_editorUiComposite->getDockWindowTitle(kDouyinWindowTitle)
            : std::string(kDouyinWindowTitle);
        bool* openPtr = m_editorUiComposite
            ? m_editorUiComposite->getDockWindowOpenPtr(kDouyinWindowTitle)
            : nullptr;

        if (!ImGui::Begin(winTitle.c_str(), openPtr))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("MQEngine 抖音小游戏五步全自动烘焙与分包发布工作台");
        ImGui::Separator();

        renderIdentitySection();
        renderRenderBudgetSection();
        renderStreamingBudgetSection();
        renderPlatformEcoSection();
        renderActionToolbarAndLogs();

        ImGui::End();
    }

    void DouyinPackagerEditorComposite::renderIdentitySection()
    {
        if (!ImGui::CollapsingHeader("1. 基础标识与启动场景", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        char appIdBuf[128];
        std::snprintf(appIdBuf, sizeof(appIdBuf), "%s", m_config.appId.c_str());
        if (ImGui::InputText("抖音 AppID", appIdBuf, sizeof(appIdBuf)))
        {
            m_config.appId = appIdBuf;
        }

        char projBuf[128];
        std::snprintf(projBuf, sizeof(projBuf), "%s", m_config.projectName.c_str());
        if (ImGui::InputText("项目名称", projBuf, sizeof(projBuf)))
        {
            m_config.projectName = projBuf;
        }

        const bool isPortrait = (m_config.orientation == "portrait");
        if (ImGui::RadioButton("竖屏 (Portrait)", isPortrait))
        {
            m_config.orientation = "portrait";
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("横屏 (Landscape)", !isPortrait))
        {
            m_config.orientation = "landscape";
        }

        char bootSceneBuf[256];
        std::snprintf(bootSceneBuf, sizeof(bootSceneBuf), "%s", m_config.bootScenePath.c_str());
        if (ImGui::InputText("首屏启动场景 (.mqscene)", bootSceneBuf, sizeof(bootSceneBuf)))
        {
            m_config.bootScenePath = bootSceneBuf;
        }
    }

    void DouyinPackagerEditorComposite::renderRenderBudgetSection()
    {
        if (!ImGui::CollapsingHeader("2. 移动端渲染管线与画质预算", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        char graphBuf[128];
        std::snprintf(graphBuf, sizeof(graphBuf), "%s", m_config.renderGraphPreset.c_str());
        if (ImGui::InputText("主目标帧图 (.pgraph)", graphBuf, sizeof(graphBuf)))
        {
            m_config.renderGraphPreset = graphBuf;
        }

        ImGui::SliderFloat("DPR 缩放上限 [1.0 - 3.0]", &m_config.devicePixelRatioCap, 1.0f, 3.0f, "%.2f");
        ImGui::Checkbox("启用移动端半分辨率泛光 (Bloom)", &m_config.enableBloom);

        const bool isAstc4x4 = (m_config.astcBlockQuality == "4x4");
        if (ImGui::RadioButton("ASTC 4x4 (高画质 UI/法线)", isAstc4x4))
        {
            m_config.astcBlockQuality = "4x4";
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("ASTC 6x6 (推荐高压缩比)", !isAstc4x4))
        {
            m_config.astcBlockQuality = "6x6";
        }
    }

    void DouyinPackagerEditorComposite::renderStreamingBudgetSection()
    {
        if (!ImGui::CollapsingHeader("3. 首包预算与 CDN 流式分发", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::SliderFloat("MainPackage 总体积上限 (MB)", &m_config.mainPackageMaxMB, 2.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Wasm+JS 代码包上限 (MB)", &m_config.codePackageMaxMB, 1.0f, 10.0f, "%.1f");
        m_config.codePackageMaxMB = std::min(m_config.codePackageMaxMB, m_config.mainPackageMaxMB);

        char cdnBuf[256];
        std::snprintf(cdnBuf, sizeof(cdnBuf), "%s", m_config.cdnBaseUrl.c_str());
        if (ImGui::InputText("远程 CDN 根地址", cdnBuf, sizeof(cdnBuf)))
        {
            m_config.cdnBaseUrl = cdnBuf;
        }

        ImGui::SliderInt("宿主本地缓存上限 (MB)", &m_config.localCacheQuotaMB, 32, 200);

        char outBuf[256];
        std::snprintf(outBuf, sizeof(outBuf), "%s", m_outputDir.c_str());
        if (ImGui::InputText("构建输出目录", outBuf, sizeof(outBuf)))
        {
            m_outputDir = outBuf;
        }
    }

    void DouyinPackagerEditorComposite::renderPlatformEcoSection()
    {
        if (!ImGui::CollapsingHeader("4. 抖音宿主生态服务与工具链"))
        {
            return;
        }

        ImGui::Checkbox("启用首页侧边栏复访领奖服务 (021036)", &m_config.sidebarRewardEnabled);

        char adBuf[128];
        std::snprintf(adBuf, sizeof(adBuf), "%s", m_config.rewardedAdUnitId.c_str());
        if (ImGui::InputText("激励视频广告位 ID", adBuf, sizeof(adBuf)))
        {
            m_config.rewardedAdUnitId = adBuf;
        }

        char emsdkBuf[256];
        std::snprintf(emsdkBuf, sizeof(emsdkBuf), "%s", m_config.emsdkRoot.c_str());
        if (ImGui::InputText("Emscripten SDK 路径", emsdkBuf, sizeof(emsdkBuf)))
        {
            m_config.emsdkRoot = emsdkBuf;
        }

        char ideBuf[256];
        std::snprintf(ideBuf, sizeof(ideBuf), "%s", m_config.douyinIdeExecutable.c_str());
        if (ImGui::InputText("抖音开发者工具路径", ideBuf, sizeof(ideBuf)))
        {
            m_config.douyinIdeExecutable = ideBuf;
        }
    }

    void DouyinPackagerEditorComposite::renderActionToolbarAndLogs()
    {
        ImGui::Separator();
        if (ImGui::Button("保存配置 (douyin_minigame.toml)"))
        {
            (void)saveConfigToCurrentProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("重新加载"))
        {
            (void)loadConfigFromCurrentProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("烘焙并构建抖音小游戏"))
        {
            (void)triggerOneClickCook();
        }
        ImGui::SameLine();
        if (ImGui::Button("在抖音开发者工具中预览"))
        {
            (void)previewInDouyinDevTools();
        }

        ImGui::ProgressBar(m_progressRatio, ImVec2(-1.0f, 0.0f), m_currentStepLabel.c_str());

        ImGui::BeginChild("DouyinCookLogPanel", ImVec2(0.0f, 150.0f), true);
        for (const auto& line : m_buildLogs)
        {
            ImGui::TextUnformatted(line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
} // namespace MQEngine::DouyinMiniGame

MQ_EXPORT_COMPOSITE(MQEngine::DouyinMiniGame::DouyinPackagerEditorComposite)
