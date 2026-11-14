/**
 * @file RenderGraphEditorComposite.cpp
 * @brief 管线编辑组件生命周期、Dock 窗口注册、RenderGraph 页签与节点检视实现
 */
#include "RenderGraphEditorComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>

#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/ImGuiComposite/ImGuiComposite.h>

#include <cstdlib>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kPipelineEditorWindowTitle = "管线编辑 (Pipeline Editor)";
        }

        RenderGraphEditorComposite::RenderGraphEditorComposite() = default;

        RenderGraphEditorComposite::~RenderGraphEditorComposite()
        {
            onUnload();
        }

        Status RenderGraphEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            bool openOnBoot = false;
            if (const char* envOpen = std::getenv("MQ_EDITOR_OPEN_PIPELINE"))
            {
                if (envOpen[0] == '1')
                {
                    openOnBoot = true;
                    m_requestedMainTab = 0;
                }
                else if (envOpen[0] == '2')
                {
                    openOnBoot = true;
                    m_requestedMainTab = 1;
                }
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kPipelineEditorWindowTitle,
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderEditorPanel();
                    },
                    openOnBoot);

                m_editorUiComposite->getUiContext().registerSignalHandler<EditorBase::SelectAssetSignal>(
                    [this](const EditorBase::SelectAssetSignal& sig) {
                        if (sig.assetExtension == ".pgraph")
                        {
                            if (m_editorUiComposite)
                            {
                                m_editorUiComposite->setDockWindowOpen(kPipelineEditorWindowTitle, true);
                            }
                            m_requestedMainTab = 0;
                            loadFromFile(sig.assetPath);
                        }
                        else if (sig.assetExtension == ".pso")
                        {
                            if (m_editorUiComposite)
                            {
                                m_editorUiComposite->setDockWindowOpen(kPipelineEditorWindowTitle, true);
                            }
                            m_requestedMainTab = 1;
                            loadPipelineObjectsFromFile(sig.assetPath);
                        }
                    });
            }

            auto* domain = Domain::Domain::InstancePtr();
            FCT::RenderGraph* rg = domain ? domain->renderer.getRenderGraph() : nullptr;
            if (rg && !rg->getOriginalPasses().empty())
            {
                createGraphFromPassDescs(rg->getOriginalPasses());
            }
            else
            {
                int pShadow = newPassNode("ShadowMapPass");
                int pObject = newPassNode("ObjectPass");
                int pTone = newPassNode("ToneMappingPass");
                int pAa = newPassNode("AntiAliasingPass");
                int pImgui = newPassNode("ImguiPass");

                int imgShadow = newImageNode("DepthFromLigth0");
                int imgHdr = newImageNode("SceneHDRColor");
                int imgLdr = newImageNode("ToneMappedColor");
                int imgFinal = newImageNode("SceneColorTarget");

                addDepthStencilLink(pShadow, imgShadow);
                addTextureLink(imgShadow, pObject);
                addTargetLink(pObject, 0, imgHdr);
                addTextureLink(imgHdr, pTone);
                addTargetLink(pTone, 0, imgLdr);
                addTextureLink(imgLdr, pAa);
                addTargetLink(pAa, 0, imgFinal);
                addTextureLink(imgFinal, pImgui);

                m_passes[pImgui].targetDesc[0].enabled = true;
                m_passes[pImgui].targetDesc[0].isWindow = true;

                autoLayoutGraph();
            }

            syncPipelineObjectsFromRenderer(true);
            return OkStatus();
        }

        Status RenderGraphEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            if (m_passes.empty())
            {
                auto* domain = Domain::Domain::InstancePtr();
                FCT::RenderGraph* rg = domain ? domain->renderer.getRenderGraph() : nullptr;
                if (rg && !rg->getOriginalPasses().empty())
                {
                    createGraphFromPassDescs(rg->getOriginalPasses());
                }
            }
            syncPipelineObjectsFromRenderer(false);

            if (const char* envOpen = std::getenv("MQ_EDITOR_OPEN_PIPELINE"))
            {
                if ((envOpen[0] == '1' || envOpen[0] == '2') && m_editorUiComposite)
                {
                    m_editorUiComposite->setDockWindowOpen("3D 视口 (Scene Viewport)", false);
                    m_editorUiComposite->setDockWindowOpen(kPipelineEditorWindowTitle, true);
                }
            }
            return OkStatus();
        }

        void RenderGraphEditorComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
            if (!ImGui::GetCurrentContext())
            {
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F11))
            {
                restoreEditorTopology();
            }
        }

        void RenderGraphEditorComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kPipelineEditorWindowTitle);
                m_editorUiComposite = nullptr;
            }

            if (m_context)
            {
                ImNodes::EditorContextFree(m_context);
                m_context = nullptr;
            }
            if (m_imnodesCtx)
            {
                ImNodes::DestroyContext(m_imnodesCtx);
                m_imnodesCtx = nullptr;
            }

            m_passes.clear();
            m_images.clear();
            m_pinInfoMap.clear();
            m_passOutputlinks.clear();
            m_passInputLinks.clear();
            m_pipelineObjectDrafts.clear();
            m_pipelineObjectsSynced = false;

            Asset::Composite::onUnload();
        }

        const char* RenderGraphEditorComposite::tr(const char* zh, const char* en) const
        {
            return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
        }

        void RenderGraphEditorComposite::restoreEditorTopology()
        {
            auto* domain = Domain::Domain::InstancePtr();
            FCT::RenderGraph* rg = domain ? domain->renderer.getRenderGraph() : nullptr;
            if (rg && m_hasSnapshot && !m_editorTopologySnapshot.empty())
            {
                rg->clearOriginalPasses();
                for (const auto& p : m_editorTopologySnapshot)
                {
                    rg->addPassDesc(p);
                }
                rg->recompile();
                createGraphFromPassDescs(m_editorTopologySnapshot);
                m_isDirectGameMode = false;
                m_hasSnapshot = false;

                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        "RenderGraph",
                        tr("已恢复编辑器默认管线", "Restored editor RenderGraph"),
                        EditorBase::LogEntry::Level::Info);
                }
            }
        }

        void RenderGraphEditorComposite::renderEditorPanel()
        {
            ZoneScopedN("RenderGraphEditorComposite::renderEditorPanel");

            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kPipelineEditorWindowTitle)
                : "管线编辑###PipelineEditorWindow";
            bool* openPtr = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowOpenPtr(kPipelineEditorWindowTitle)
                : nullptr;

            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            bool inRenderGraphTab = false;
            if (ImGui::BeginTabBar("##PipelineEditorMainTabs"))
            {
                const ImGuiTabItemFlags rgFlags =
                    (m_requestedMainTab == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                const ImGuiTabItemFlags psoFlags =
                    (m_requestedMainTab == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                m_requestedMainTab = -1;

                if (ImGui::BeginTabItem("RenderGraph###MainTabRenderGraph", nullptr, rgFlags))
                {
                    inRenderGraphTab = true;
                    renderRenderGraphTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(
                        tr("管线对象###MainTabPipelineObjects", "Pipeline Objects###MainTabPipelineObjects"),
                        nullptr,
                        psoFlags))
                {
                    renderPipelineObjectsTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();

            if (inRenderGraphTab && m_showTextureInsight)
            {
                renderTextureInsightWindows();
            }
        }

        void RenderGraphEditorComposite::renderRenderGraphTab()
        {
            renderRenderGraphToolbar();
            ImGui::Separator();

            const float leftWidth = ImGui::GetContentRegionAvail().x * 0.72f;

            ImGui::BeginChild("GraphCanvasChild", ImVec2(leftWidth, 0), true);
            renderNodeGraphCanvas();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("InspectorAndCodeChild", ImVec2(0, 0), true);
            if (ImGui::BeginTabBar("RightTabBar"))
            {
                if (ImGui::BeginTabItem(tr("节点属性###TabNodeInsp", "Node Properties###TabNodeInsp")))
                {
                    renderInspectorPanel();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(tr("代码生成###TabCodeGen", "Code Gen###TabCodeGen")))
                {
                    renderGeneratedCodePanel();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();
        }

        void RenderGraphEditorComposite::renderRenderGraphToolbar()
        {
            if (ImGui::Button(tr("保存 (.pgraph)", "Save (.pgraph)")))
            {
                saveToFile("Assets/RenderGraph/DefaultRenderGraph.pgraph");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("加载 (.pgraph)", "Load (.pgraph)")))
            {
                loadFromFile("Assets/RenderGraph/DefaultRenderGraph.pgraph");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("清空", "Clear")))
            {
                clearGraph();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("自动布局", "Auto Layout")))
            {
                autoLayoutGraph();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("同步当前帧图", "Pull Live RG")))
            {
                auto* domain = Domain::Domain::InstancePtr();
                FCT::RenderGraph* rg = domain ? domain->renderer.getRenderGraph() : nullptr;
                if (rg)
                {
                    const auto passes = rg->getOriginalPasses();
                    if (!passes.empty())
                    {
                        createGraphFromPassDescs(passes);
                        if (m_editorUiComposite)
                        {
                            m_editorUiComposite->showToast(
                                "RenderGraph",
                                tr("已同步当前运行 RenderGraph", "Synced live RenderGraph"),
                                EditorBase::LogEntry::Level::Info);
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("编译应用", "Compile & Apply")))
            {
                auto* domain = Domain::Domain::InstancePtr();
                FCT::RenderGraph* rg = domain ? domain->renderer.getRenderGraph() : nullptr;
                if (rg)
                {
                    auto passDescs = convertCurrentGraphToPassDescs();
                    if (!passDescs.empty())
                    {
                        bool hasWindowTarget = false;
                        for (const auto& p : passDescs)
                        {
                            for (const auto& t : p.targets)
                            {
                                if (t.isWindow)
                                {
                                    hasWindowTarget = true;
                                    break;
                                }
                            }
                            if (hasWindowTarget)
                            {
                                break;
                            }
                        }

                        if (hasWindowTarget && !m_isDirectGameMode)
                        {
                            m_editorTopologySnapshot = rg->getOriginalPasses();
                            m_hasSnapshot = true;
                            m_isDirectGameMode = true;
                        }

                        rg->clearOriginalPasses();
                        for (const auto& passDesc : passDescs)
                        {
                            rg->addPassDesc(passDesc);
                        }
                        rg->recompile();

                        if (m_editorUiComposite)
                        {
                            m_editorUiComposite->showToast(
                                "RenderGraph",
                                tr("RenderGraph 已编译生效", "Compiled graph to live RenderGraph"),
                                EditorBase::LogEntry::Level::Info);
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("生成代码", "Gen C++")))
            {
                m_generatedCode = generatorCode();
            }

            if (m_isDirectGameMode || m_hasSnapshot)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.40f, 0.10f, 1.0f));
                if (ImGui::Button(tr("恢复默认管线 (F11)", "Restore Default (F11)")))
                {
                    restoreEditorTopology();
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::Checkbox(tr("贴图检视", "Texture Insight"), &m_showTextureInsight);

            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint(
                "##FilterInput",
                tr("筛选 Pass/Image...", "Filter Pass/Image..."),
                m_searchFilter,
                sizeof(m_searchFilter));

            ImGui::SameLine();
            ImGui::TextDisabled(
                "Pass: %zu | Image: %zu | %s: %zu",
                m_passes.size(),
                m_images.size(),
                tr("连线", "Links"),
                m_passOutputlinks.size() + m_passInputLinks.size());
        }

        void RenderGraphEditorComposite::renderInspectorPanel()
        {
            if (m_selectedNodeId > 0 && m_passes.count(m_selectedNodeId))
            {
                auto& pass = m_passes[m_selectedNodeId];
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Pass: %s", pass.name.c_str());
                ImGui::Separator();
                ImGui::Text("ID: %u", pass.id);
                ImGui::Checkbox(tr("启用 Clear", "Enable Clear"), &pass.enableClear);
                if (pass.enableClear)
                {
                    ImGui::Checkbox(tr("清除颜色缓冲", "Clear Color"), &pass.enableClearTarget);
                    if (pass.enableClearTarget)
                    {
                        ImGui::ColorEdit4(tr("清除颜色", "Clear RGBA"), pass.clearColor);
                    }
                    ImGui::Checkbox(tr("清除深度缓冲", "Clear Depth"), &pass.enableClearDepth);
                    if (pass.enableClearDepth)
                    {
                        ImGui::SliderFloat(tr("深度值", "Depth"), &pass.clearDepth, 0.0f, 1.0f);
                    }
                    ImGui::Checkbox(tr("清除模板缓冲", "Clear Stencil"), &pass.enableClearStencil);
                }

                ImGui::Spacing();
                ImGui::Text("%s (%zu):", tr("输入纹理", "Input Textures"), pass.texturePins.size());
                for (int pin : pass.texturePins)
                {
                    if (m_passInputLinks.count(pin))
                    {
                        auto& link = m_passInputLinks[pin];
                        if (m_pinInfoMap.count(link.startPinId))
                        {
                            auto& pinInfo = m_pinInfoMap[link.startPinId];
                            if (m_images.count(pinInfo.nodeId))
                            {
                                ImGui::BulletText("%s (Pin %d)", m_images[pinInfo.nodeId].name.c_str(), pin);
                            }
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::Text("%s:", tr("输出目标", "Output Targets"));
                for (int i = 0; i < 9; ++i)
                {
                    if (pass.targetDesc[i].enabled)
                    {
                        ImGui::BulletText(
                            "RT%d -> %s (%dx%d)",
                            i,
                            pass.targetDesc[i].isWindow ? tr("窗口交换链", "Swapchain") : pass.targetDesc[i].format.c_str(),
                            pass.targetDesc[i].customWidth,
                            pass.targetDesc[i].customHeight);
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button(tr("跳转到对应管线对象 ->", "Edit Bound PSO ->")))
                {
                    syncPipelineObjectsFromRenderer(false);
                    for (size_t i = 0; i < m_pipelineObjectDrafts.size(); ++i)
                    {
                        if (m_pipelineObjectDrafts[i].passName == pass.name)
                        {
                            m_selectedPipelineObjectIdx = static_cast<int>(i);
                            break;
                        }
                    }
                    m_requestedMainTab = 1;
                }
                return;
            }

            if (m_selectedNodeId > 0 && m_images.count(m_selectedNodeId))
            {
                auto& img = m_images[m_selectedNodeId];
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "Image: %s", img.name.c_str());
                ImGui::Separator();
                ImGui::Text("ID: %u", img.id);
                ImGui::Text("%s: %s", tr("像素格式", "Format"), img.format.c_str());
                ImGui::Text("%s: %d x %d", tr("分辨率", "Resolution"), img.width, img.height);
                ImGui::Text(
                    "%s: %.2f MB",
                    tr("显存估算", "Est. Memory"),
                    static_cast<double>(img.width * img.height * 4) / (1024.0 * 1024.0));
                return;
            }

            ImGui::TextDisabled(
                "%s",
                tr("在左侧画布点击 Pass 或 Image 节点查看属性。",
                   "Click a Pass or Image node on the canvas to inspect its properties."));
        }

        void RenderGraphEditorComposite::renderGeneratedCodePanel()
        {
            if (ImGui::Button(tr("复制到剪贴板", "Copy To Clipboard")))
            {
                if (!m_generatedCode.empty())
                {
                    ImGui::SetClipboardText(m_generatedCode.c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("清空代码", "Clear")))
            {
                m_generatedCode.clear();
            }
            ImGui::Separator();
            ImGui::BeginChild("CodeDisplayChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            if (m_generatedCode.empty())
            {
                ImGui::TextDisabled(
                    "%s",
                    tr("点击工具栏 [生成代码] 生成 C++ graph->addPass(...) 代码。",
                       "Click [Gen C++] in the toolbar to generate C++ code."));
            }
            else
            {
                ImGui::TextUnformatted(m_generatedCode.c_str());
            }
            ImGui::EndChild();
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::RenderGraphEditorComposite)
