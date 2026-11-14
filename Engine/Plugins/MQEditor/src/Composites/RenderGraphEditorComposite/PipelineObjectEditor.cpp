/**
 * @file PipelineObjectEditor.cpp
 * @brief 管线编辑组件 — [管线对象] 页签实现：PSO 列表管理、光栅化/深度模板/颜色混合状态编辑与 .pso 序列化
 */
#include "RenderGraphEditorComposite.h"

#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/thirdparty/TomlBackend.h>

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            template <typename EnumT>
            struct EnumOption
            {
                EnumT value;
                const char* key;
                const char* labelZh;
                const char* labelEn;
            };

            constexpr std::array<EnumOption<FCT::PolygonMode>, 3> kPolygonModeTable = {{
                {FCT::PolygonMode::Fill, "Fill", "实体填充 (Fill)", "Fill"},
                {FCT::PolygonMode::Line, "Line", "线框模式 (Line)", "Line (Wireframe)"},
                {FCT::PolygonMode::Point, "Point", "点云模式 (Point)", "Point"}
            }};

            constexpr std::array<EnumOption<FCT::CullMode>, 4> kCullModeTable = {{
                {FCT::CullMode::None, "None", "不剔除 (None / 双面)", "None (Two-Sided)"},
                {FCT::CullMode::Back, "Back", "背面剔除 (Back)", "Back"},
                {FCT::CullMode::Front, "Front", "正面剔除 (Front)", "Front"},
                {FCT::CullMode::FrontAndBack, "FrontAndBack", "正背面全剔除 (FrontAndBack)", "Front & Back"}
            }};

            constexpr std::array<EnumOption<FCT::FrontFace>, 2> kFrontFaceTable = {{
                {FCT::FrontFace::CounterClockwise, "CounterClockwise", "逆时针 (CCW)", "Counter-Clockwise (CCW)"},
                {FCT::FrontFace::Clockwise, "Clockwise", "顺时针 (CW)", "Clockwise (CW)"}
            }};

            constexpr std::array<EnumOption<FCT::CompareOp>, 8> kCompareOpTable = {{
                {FCT::CompareOp::Never, "Never", "从不通过 (Never)", "Never"},
                {FCT::CompareOp::Less, "Less", "小于 (Less)", "Less"},
                {FCT::CompareOp::Equal, "Equal", "等于 (Equal)", "Equal"},
                {FCT::CompareOp::LessOrEqual, "LessOrEqual", "小于等于 (LessOrEqual)", "LessOrEqual"},
                {FCT::CompareOp::Greater, "Greater", "大于 (Greater)", "Greater"},
                {FCT::CompareOp::NotEqual, "NotEqual", "不等于 (NotEqual)", "NotEqual"},
                {FCT::CompareOp::GreaterOrEqual, "GreaterOrEqual", "大于等于 (GreaterOrEqual)", "GreaterOrEqual"},
                {FCT::CompareOp::Always, "Always", "总是通过 (Always)", "Always"}
            }};

            constexpr std::array<EnumOption<FCT::BlendFactor>, 10> kBlendFactorTable = {{
                {FCT::BlendFactor::Zero, "Zero", "Zero (0)", "Zero"},
                {FCT::BlendFactor::One, "One", "One (1)", "One"},
                {FCT::BlendFactor::SrcColor, "SrcColor", "SrcColor", "SrcColor"},
                {FCT::BlendFactor::OneMinusSrcColor, "OneMinusSrcColor", "OneMinusSrcColor", "OneMinusSrcColor"},
                {FCT::BlendFactor::DstColor, "DstColor", "DstColor", "DstColor"},
                {FCT::BlendFactor::OneMinusDstColor, "OneMinusDstColor", "OneMinusDstColor", "OneMinusDstColor"},
                {FCT::BlendFactor::SrcAlpha, "SrcAlpha", "SrcAlpha", "SrcAlpha"},
                {FCT::BlendFactor::OneMinusSrcAlpha, "OneMinusSrcAlpha", "OneMinusSrcAlpha", "OneMinusSrcAlpha"},
                {FCT::BlendFactor::DstAlpha, "DstAlpha", "DstAlpha", "DstAlpha"},
                {FCT::BlendFactor::OneMinusDstAlpha, "OneMinusDstAlpha", "OneMinusDstAlpha", "OneMinusDstAlpha"}
            }};

            constexpr std::array<EnumOption<FCT::BlendOp>, 5> kBlendOpTable = {{
                {FCT::BlendOp::Add, "Add", "相加 (Add)", "Add"},
                {FCT::BlendOp::Subtract, "Subtract", "源减目标 (Subtract)", "Subtract"},
                {FCT::BlendOp::ReverseSubtract, "ReverseSubtract", "目标减源 (ReverseSubtract)", "ReverseSubtract"},
                {FCT::BlendOp::Min, "Min", "最小值 (Min)", "Min"},
                {FCT::BlendOp::Max, "Max", "最大值 (Max)", "Max"}
            }};

            template <typename EnumT, size_t N>
            const char* enumToKey(EnumT val, const std::array<EnumOption<EnumT>, N>& table)
            {
                for (const auto& opt : table)
                {
                    if (opt.value == val)
                    {
                        return opt.key;
                    }
                }
                return table[0].key;
            }

            template <typename EnumT, size_t N>
            EnumT keyToEnum(std::string_view key, const std::array<EnumOption<EnumT>, N>& table, EnumT fallback)
            {
                for (const auto& opt : table)
                {
                    if (key == opt.key)
                    {
                        return opt.value;
                    }
                }
                return fallback;
            }

            template <typename EnumT, size_t N>
            bool drawEnumCombo(
                const char* label,
                EnumT& currentVal,
                const std::array<EnumOption<EnumT>, N>& table,
                bool useEnglish)
            {
                const char* preview = useEnglish ? table[0].labelEn : table[0].labelZh;
                for (const auto& opt : table)
                {
                    if (opt.value == currentVal)
                    {
                        preview = useEnglish ? opt.labelEn : opt.labelZh;
                        break;
                    }
                }

                bool changed = false;
                if (ImGui::BeginCombo(label, preview))
                {
                    for (const auto& opt : table)
                    {
                        const bool isSelected = (opt.value == currentVal);
                        const char* itemText = useEnglish ? opt.labelEn : opt.labelZh;
                        if (ImGui::Selectable(itemText, isSelected))
                        {
                            currentVal = opt.value;
                            changed = true;
                        }
                        if (isSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }
        } // namespace

        void RenderGraphEditorComposite::syncPipelineObjectsFromRenderer(bool forceReload)
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain)
            {
                return;
            }

            auto liveList = domain->renderer.getPipelineObjects();
            if (forceReload || !m_pipelineObjectsSynced || m_pipelineObjectDrafts.empty())
            {
                m_pipelineObjectDrafts = std::move(liveList);
                m_pipelineObjectsSynced = true;
                if (m_selectedPipelineObjectIdx >= static_cast<int>(m_pipelineObjectDrafts.size()))
                {
                    m_selectedPipelineObjectIdx = 0;
                }
                return;
            }

            for (const auto& liveItem : liveList)
            {
                const bool exists = std::any_of(
                    m_pipelineObjectDrafts.begin(),
                    m_pipelineObjectDrafts.end(),
                    [&liveItem](const Domain::Renderer::PipelineObjectDesc& d) {
                        return d.name == liveItem.name;
                    });
                if (!exists)
                {
                    m_pipelineObjectDrafts.push_back(liveItem);
                }
            }
        }

        void RenderGraphEditorComposite::applyPipelineObjectToRenderer(
            const Domain::Renderer::PipelineObjectDesc& pso,
            bool showFeedback)
        {
            auto* domain = Domain::Domain::InstancePtr();
            if (!domain)
            {
                return;
            }

            domain->renderer.upsertPipelineObject(pso);
            if (showFeedback && m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    tr("管线对象", "Pipeline Object"),
                    std::string(tr("已更新管线对象: ", "Applied PSO: ")) + pso.name,
                    EditorBase::LogEntry::Level::Info);
            }
        }

        void RenderGraphEditorComposite::renderPipelineObjectsTab()
        {
            syncPipelineObjectsFromRenderer(false);
            renderPipelineObjectsToolbar();
            ImGui::Separator();

            const float listWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.27f, 210.0f, 340.0f);
            renderPipelineObjectListPane(listWidth);
            ImGui::SameLine();
            renderPipelineObjectDetailPane();
        }

        void RenderGraphEditorComposite::renderPipelineObjectsToolbar()
        {
            if (ImGui::Button(tr("+ 新建管线对象", "+ New PSO")))
            {
                Domain::Renderer::PipelineObjectDesc customPso{};
                customPso.name = "PSO_Custom_" + std::to_string(m_customPsoCounter++);
                customPso.passName = "ObjectPass";
                customPso.isBuiltin = false;
                customPso.hasColorAttachment = true;
                customPso.vertexLayoutSummary = "StandardMeshVertexLayout (96B)";
                customPso.pixelLayoutSummary = "StandardPixelLayout -> RGBA16F";
                m_pipelineObjectDrafts.push_back(customPso);
                m_selectedPipelineObjectIdx = static_cast<int>(m_pipelineObjectDrafts.size()) - 1;
                applyPipelineObjectToRenderer(customPso, true);
            }

            ImGui::SameLine();
            const bool hasValidSel =
                (m_selectedPipelineObjectIdx >= 0 &&
                 m_selectedPipelineObjectIdx < static_cast<int>(m_pipelineObjectDrafts.size()));

            if (ImGui::Button(tr("复制当前", "Duplicate")) && hasValidSel)
            {
                auto copyPso = m_pipelineObjectDrafts[m_selectedPipelineObjectIdx];
                copyPso.name += "_Copy" + std::to_string(m_customPsoCounter++);
                copyPso.isBuiltin = false;
                m_pipelineObjectDrafts.push_back(copyPso);
                m_selectedPipelineObjectIdx = static_cast<int>(m_pipelineObjectDrafts.size()) - 1;
                applyPipelineObjectToRenderer(copyPso, true);
            }

            ImGui::SameLine();
            const bool canDelete = hasValidSel && !m_pipelineObjectDrafts[m_selectedPipelineObjectIdx].isBuiltin;
            ImGui::BeginDisabled(!canDelete);
            if (ImGui::Button(tr("删除", "Delete")) && canDelete)
            {
                const std::string removedName = m_pipelineObjectDrafts[m_selectedPipelineObjectIdx].name;
                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    (void)domain->renderer.removePipelineObject(removedName);
                }
                m_pipelineObjectDrafts.erase(m_pipelineObjectDrafts.begin() + m_selectedPipelineObjectIdx);
                if (m_selectedPipelineObjectIdx >= static_cast<int>(m_pipelineObjectDrafts.size()))
                {
                    m_selectedPipelineObjectIdx = std::max(0, static_cast<int>(m_pipelineObjectDrafts.size()) - 1);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(tr("应用到渲染器", "Apply Now")) && hasValidSel)
            {
                applyPipelineObjectToRenderer(m_pipelineObjectDrafts[m_selectedPipelineObjectIdx], true);
            }

            ImGui::SameLine();
            if (ImGui::Button(tr("重置内置默认", "Reset Builtins")))
            {
                if (auto* domain = Domain::Domain::InstancePtr())
                {
                    domain->renderer.resetBuiltinPipelineObjects();
                    syncPipelineObjectsFromRenderer(true);
                }
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        tr("管线对象", "Pipeline Object"),
                        tr("已恢复内置管线对象默认状态", "Reset built-in PSOs to defaults"),
                        EditorBase::LogEntry::Level::Info);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(tr("保存 (.pso)", "Save (.pso)")))
            {
                savePipelineObjectsToFile("Assets/RenderGraph/DefaultPipelineObjects.pso");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("加载 (.pso)", "Load (.pso)")))
            {
                loadPipelineObjectsFromFile("Assets/RenderGraph/DefaultPipelineObjects.pso");
            }

            ImGui::SameLine();
            ImGui::Checkbox(tr("实时同步", "Live Sync"), &m_autoApplyPipelineObject);

            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s: %zu",
                tr("对象总数", "Total PSOs"),
                m_pipelineObjectDrafts.size());
        }

        void RenderGraphEditorComposite::renderPipelineObjectListPane(float paneWidth)
        {
            ImGui::BeginChild("PsoListChild", ImVec2(paneWidth, 0), true);

            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##PsoFilter",
                tr("搜索管线对象 / Pass...", "Filter PSO / Pass..."),
                m_psoSearchFilter,
                sizeof(m_psoSearchFilter));
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(m_pipelineObjectDrafts.size()); ++i)
            {
                const auto& item = m_pipelineObjectDrafts[i];
                if (m_psoSearchFilter[0] != '\0' &&
                    item.name.find(m_psoSearchFilter) == std::string::npos &&
                    item.passName.find(m_psoSearchFilter) == std::string::npos)
                {
                    continue;
                }

                const bool isSelected = (m_selectedPipelineObjectIdx == i);
                std::string label = item.name + "###PsoItem_" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), isSelected))
                {
                    m_selectedPipelineObjectIdx = i;
                }
                ImGui::Indent(10.0f);
                ImGui::TextDisabled("Pass: %s", item.passName.empty() ? "Unbound" : item.passName.c_str());
                ImGui::Unindent(10.0f);
                ImGui::Spacing();
            }

            ImGui::EndChild();
        }

        void RenderGraphEditorComposite::renderPipelineObjectDetailPane()
        {
            ImGui::BeginChild("PsoDetailChild", ImVec2(0, 0), true);

            if (m_selectedPipelineObjectIdx < 0 ||
                m_selectedPipelineObjectIdx >= static_cast<int>(m_pipelineObjectDrafts.size()))
            {
                ImGui::TextDisabled("%s", tr("请在左侧选择或新建一个管线对象。", "Select or create a Pipeline Object on the left."));
                ImGui::EndChild();
                return;
            }

            auto& pso = m_pipelineObjectDrafts[m_selectedPipelineObjectIdx];
            bool dirty = false;

            dirty |= renderPsoHeaderAndShaderSection(pso);
            ImGui::Spacing();
            dirty |= renderPsoRasterizerSection(pso);
            ImGui::Spacing();
            dirty |= renderPsoDepthStencilSection(pso);
            ImGui::Spacing();
            dirty |= renderPsoBlendSection(pso);

            if (dirty && m_autoApplyPipelineObject)
            {
                applyPipelineObjectToRenderer(pso, false);
            }

            ImGui::EndChild();
        }

        bool RenderGraphEditorComposite::renderPsoHeaderAndShaderSection(Domain::Renderer::PipelineObjectDesc& pso)
        {
            bool changed = false;

            if (!ImGui::CollapsingHeader(
                    tr("关联 Pass 与管线布局 (Pass & Layout)", "Bound Pass & Pipeline Layout"),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return false;
            }

            if (pso.isBuiltin)
            {
                ImGui::TextColored(
                    ImVec4(0.45f, 0.82f, 1.0f, 1.0f),
                    "%s: %s  [%s]",
                    tr("管线对象", "PSO Name"),
                    pso.name.c_str(),
                    tr("内置运行时管线", "Built-in Runtime PSO"));
            }
            else
            {
                char nameBuf[128] = {};
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", pso.name.c_str());
                if (ImGui::InputText(tr("对象名称", "PSO Name"), nameBuf, sizeof(nameBuf)))
                {
                    if (nameBuf[0] != '\0')
                    {
                        pso.name = nameBuf;
                        changed = true;
                    }
                }
            }

            // 收集可选 Pass 列表（内置 Pass + 当前 RenderGraph 画布中的 Pass）
            std::vector<std::string> passCandidates = {
                "ShadowMapPass",
                "ObjectPass",
                "ToneMappingPass",
                "AntiAliasingPass"
            };
            for (const auto& [id, passNode] : m_passes)
            {
                if (std::find(passCandidates.begin(), passCandidates.end(), passNode.name) == passCandidates.end())
                {
                    passCandidates.push_back(passNode.name);
                }
            }

            ImGui::BeginDisabled(pso.isBuiltin);
            if (ImGui::BeginCombo(tr("关联 Pass", "Target Pass"), pso.passName.c_str()))
            {
                for (const auto& candidate : passCandidates)
                {
                    const bool selected = (pso.passName == candidate);
                    if (ImGui::Selectable(candidate.c_str(), selected))
                    {
                        pso.passName = candidate;
                        pso.hasColorAttachment = (candidate != "ShadowMapPass");
                        changed = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox(tr("包含颜色附件输出", "Has Color Attachment"), &pso.hasColorAttachment))
            {
                changed = true;
            }
            ImGui::EndDisabled();

            ImGui::BulletText("%s: %s", tr("顶点输入布局", "Vertex Layout"), pso.vertexLayoutSummary.c_str());
            ImGui::BulletText("%s: %s", tr("片元输出布局", "Pixel Layout"), pso.pixelLayoutSummary.c_str());
            ImGui::BulletText(
                "%s: VS=%s | PS=%s",
                tr("着色器阶段", "Shader Stages"),
                pso.hasVertexShader ? "Active" : "None",
                pso.hasPixelShader ? "Active" : "None (Depth-Only)");
            ImGui::BulletText(
                "%s: Set 0 (Bindless Global UBO/SSBO/Textures) + Set 1 (Pass Attachments)",
                tr("描述符集布局", "Descriptor Layout"));

            return changed;
        }

        bool RenderGraphEditorComposite::renderPsoRasterizerSection(Domain::Renderer::PipelineObjectDesc& pso)
        {
            bool changed = false;
            const bool isEn = m_editorUiComposite &&
                              (m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS);

            if (!ImGui::CollapsingHeader(
                    tr("光栅化状态 (Rasterizer State)", "Rasterizer State"),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return false;
            }

            changed |= drawEnumCombo(tr("填充模式", "Polygon Mode"), pso.polygonMode, kPolygonModeTable, isEn);
            changed |= drawEnumCombo(tr("剔除模式", "Cull Mode"), pso.cullMode, kCullModeTable, isEn);
            changed |= drawEnumCombo(tr("前向面绕序", "Front Face"), pso.frontFace, kFrontFaceTable, isEn);

            if (pso.polygonMode == FCT::PolygonMode::Line)
            {
                changed |= ImGui::SliderFloat(tr("线宽", "Line Width"), &pso.lineWidth, 0.5f, 4.0f, "%.2f");
            }

            changed |= ImGui::Checkbox(tr("深度钳制 (Depth Clamp)", "Depth Clamp"), &pso.depthClampEnable);
            ImGui::SameLine();
            changed |= ImGui::Checkbox(tr("深度偏移 (Depth Bias)", "Depth Bias"), &pso.depthBiasEnable);

            if (pso.depthBiasEnable)
            {
                changed |= ImGui::DragFloat(
                    tr("常量偏移 (Constant Factor)", "Constant Factor"),
                    &pso.depthBiasConstant,
                    0.05f,
                    -16.0f,
                    16.0f,
                    "%.3f");
                changed |= ImGui::DragFloat(
                    tr("斜率偏移 (Slope Factor)", "Slope Factor"),
                    &pso.depthBiasSlope,
                    0.05f,
                    -16.0f,
                    16.0f,
                    "%.3f");
                changed |= ImGui::DragFloat(
                    tr("偏移上限 (Bias Clamp)", "Bias Clamp"),
                    &pso.depthBiasClamp,
                    0.01f,
                    -4.0f,
                    4.0f,
                    "%.3f");
            }

            return changed;
        }

        bool RenderGraphEditorComposite::renderPsoDepthStencilSection(Domain::Renderer::PipelineObjectDesc& pso)
        {
            bool changed = false;
            const bool isEn = m_editorUiComposite &&
                              (m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS);

            if (!ImGui::CollapsingHeader(
                    tr("深度与模板状态 (Depth-Stencil State)", "Depth-Stencil State"),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return false;
            }

            changed |= ImGui::Checkbox(tr("深度测试 (Depth Test)", "Depth Test"), &pso.depthTestEnable);
            ImGui::SameLine();
            changed |= ImGui::Checkbox(tr("深度写入 (Depth Write)", "Depth Write"), &pso.depthWriteEnable);
            ImGui::SameLine();
            changed |= ImGui::Checkbox(tr("模板测试 (Stencil Test)", "Stencil Test"), &pso.stencilTestEnable);

            ImGui::BeginDisabled(!pso.depthTestEnable);
            changed |= drawEnumCombo(tr("深度比较函数", "Depth Compare Op"), pso.depthCompareOp, kCompareOpTable, isEn);
            ImGui::EndDisabled();

            return changed;
        }

        bool RenderGraphEditorComposite::renderPsoBlendSection(Domain::Renderer::PipelineObjectDesc& pso)
        {
            bool changed = false;
            const bool isEn = m_editorUiComposite &&
                              (m_editorUiComposite->getLanguage() == EditorBase::EditorLanguage::EnUS);

            if (!ImGui::CollapsingHeader(
                    tr("颜色混合状态 (Color Blend State)", "Color Blend State"),
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                return false;
            }

            if (!pso.hasColorAttachment)
            {
                ImGui::TextDisabled("%s", tr("当前 Pass 为纯深度通道，无颜色混合附件。", "Depth-only Pass has no color attachments."));
                return false;
            }

            ImGui::TextDisabled("%s:", tr("快速预设", "Presets"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("不透明 (Opaque)", "Opaque")))
            {
                pso.blendEnable = false;
                pso.srcColorBlend = FCT::BlendFactor::One;
                pso.dstColorBlend = FCT::BlendFactor::Zero;
                pso.srcAlphaBlend = FCT::BlendFactor::One;
                pso.dstAlphaBlend = FCT::BlendFactor::Zero;
                pso.colorBlendOp = FCT::BlendOp::Add;
                pso.alphaBlendOp = FCT::BlendOp::Add;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("标准透明 (Alpha)", "Alpha Blend")))
            {
                pso.blendEnable = true;
                pso.srcColorBlend = FCT::BlendFactor::SrcAlpha;
                pso.dstColorBlend = FCT::BlendFactor::OneMinusSrcAlpha;
                pso.srcAlphaBlend = FCT::BlendFactor::One;
                pso.dstAlphaBlend = FCT::BlendFactor::OneMinusSrcAlpha;
                pso.colorBlendOp = FCT::BlendOp::Add;
                pso.alphaBlendOp = FCT::BlendOp::Add;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("叠加发光 (Additive)", "Additive")))
            {
                pso.blendEnable = true;
                pso.srcColorBlend = FCT::BlendFactor::SrcAlpha;
                pso.dstColorBlend = FCT::BlendFactor::One;
                pso.srcAlphaBlend = FCT::BlendFactor::Zero;
                pso.dstAlphaBlend = FCT::BlendFactor::One;
                pso.colorBlendOp = FCT::BlendOp::Add;
                pso.alphaBlendOp = FCT::BlendOp::Add;
                changed = true;
            }

            changed |= ImGui::Checkbox(tr("启用颜色混合 (Blend Enable)", "Blend Enable"), &pso.blendEnable);

            ImGui::BeginDisabled(!pso.blendEnable);
            changed |= drawEnumCombo(tr("源颜色因子 (Src Color)", "Src Color Factor"), pso.srcColorBlend, kBlendFactorTable, isEn);
            changed |= drawEnumCombo(tr("目标颜色因子 (Dst Color)", "Dst Color Factor"), pso.dstColorBlend, kBlendFactorTable, isEn);
            changed |= drawEnumCombo(tr("颜色混合操作 (Color Op)", "Color Blend Op"), pso.colorBlendOp, kBlendOpTable, isEn);
            changed |= drawEnumCombo(tr("源 Alpha 因子 (Src Alpha)", "Src Alpha Factor"), pso.srcAlphaBlend, kBlendFactorTable, isEn);
            changed |= drawEnumCombo(tr("目标 Alpha 因子 (Dst Alpha)", "Dst Alpha Factor"), pso.dstAlphaBlend, kBlendFactorTable, isEn);
            changed |= drawEnumCombo(tr("Alpha 混合操作 (Alpha Op)", "Alpha Blend Op"), pso.alphaBlendOp, kBlendOpTable, isEn);
            ImGui::EndDisabled();

            bool writeR = (pso.colorWriteMask & 0x1u) != 0;
            bool writeG = (pso.colorWriteMask & 0x2u) != 0;
            bool writeB = (pso.colorWriteMask & 0x4u) != 0;
            bool writeA = (pso.colorWriteMask & 0x8u) != 0;
            ImGui::Text("%s:", tr("颜色写入掩码", "Color Write Mask"));
            ImGui::SameLine();
            bool maskChanged = false;
            maskChanged |= ImGui::Checkbox("R##MaskR", &writeR);
            ImGui::SameLine();
            maskChanged |= ImGui::Checkbox("G##MaskG", &writeG);
            ImGui::SameLine();
            maskChanged |= ImGui::Checkbox("B##MaskB", &writeB);
            ImGui::SameLine();
            maskChanged |= ImGui::Checkbox("A##MaskA", &writeA);
            if (maskChanged)
            {
                pso.colorWriteMask = static_cast<uint8_t>(
                    (writeR ? 0x1u : 0u) |
                    (writeG ? 0x2u : 0u) |
                    (writeB ? 0x4u : 0u) |
                    (writeA ? 0x8u : 0u));
                changed = true;
            }

            return changed;
        }

        void RenderGraphEditorComposite::savePipelineObjectsToFile(const std::string& filename)
        {
            std::string path = filename;
            if (path.find(".pso") == std::string::npos)
            {
                path += ".pso";
            }

            std::ostringstream ss;
            ss << "# MQEngine Pipeline Objects (.pso TOML)\n";
            ss << "version = \"1.0\"\n\n";

            for (const auto& pso : m_pipelineObjectDrafts)
            {
                ss << "[[pipeline_objects]]\n";
                ss << "name = \"" << pso.name << "\"\n";
                ss << "pass_name = \"" << pso.passName << "\"\n";
                ss << "is_builtin = " << (pso.isBuiltin ? "true" : "false") << "\n";
                ss << "has_color_attachment = " << (pso.hasColorAttachment ? "true" : "false") << "\n";
                ss << "polygon_mode = \"" << enumToKey(pso.polygonMode, kPolygonModeTable) << "\"\n";
                ss << "cull_mode = \"" << enumToKey(pso.cullMode, kCullModeTable) << "\"\n";
                ss << "front_face = \"" << enumToKey(pso.frontFace, kFrontFaceTable) << "\"\n";
                ss << "line_width = " << pso.lineWidth << "\n";
                ss << "depth_clamp = " << (pso.depthClampEnable ? "true" : "false") << "\n";
                ss << "depth_bias = " << (pso.depthBiasEnable ? "true" : "false") << "\n";
                ss << "depth_bias_constant = " << pso.depthBiasConstant << "\n";
                ss << "depth_bias_slope = " << pso.depthBiasSlope << "\n";
                ss << "depth_bias_clamp = " << pso.depthBiasClamp << "\n";
                ss << "depth_test = " << (pso.depthTestEnable ? "true" : "false") << "\n";
                ss << "depth_write = " << (pso.depthWriteEnable ? "true" : "false") << "\n";
                ss << "depth_compare = \"" << enumToKey(pso.depthCompareOp, kCompareOpTable) << "\"\n";
                ss << "stencil_test = " << (pso.stencilTestEnable ? "true" : "false") << "\n";
                ss << "blend_enable = " << (pso.blendEnable ? "true" : "false") << "\n";
                ss << "src_color_blend = \"" << enumToKey(pso.srcColorBlend, kBlendFactorTable) << "\"\n";
                ss << "dst_color_blend = \"" << enumToKey(pso.dstColorBlend, kBlendFactorTable) << "\"\n";
                ss << "color_blend_op = \"" << enumToKey(pso.colorBlendOp, kBlendOpTable) << "\"\n";
                ss << "src_alpha_blend = \"" << enumToKey(pso.srcAlphaBlend, kBlendFactorTable) << "\"\n";
                ss << "dst_alpha_blend = \"" << enumToKey(pso.dstAlphaBlend, kBlendFactorTable) << "\"\n";
                ss << "alpha_blend_op = \"" << enumToKey(pso.alphaBlendOp, kBlendOpTable) << "\"\n";
                ss << "color_write_mask = " << static_cast<int>(pso.colorWriteMask) << "\n\n";
            }

            auto* res = Resource::Resource::InstancePtr();
            if (!res || !res->getResourceLoader())
            {
                return;
            }

            Status st = res->getResourceLoader()->saveFile(path, ss.str());
            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    tr("管线对象", "Pipeline Object"),
                    st.ok()
                        ? (std::string(tr("已保存管线配置: ", "Saved PSO config: ")) + path)
                        : (std::string(tr("保存管线配置失败: ", "Failed to save PSO config: ")) + std::string(st.message())),
                    st.ok() ? EditorBase::LogEntry::Level::Info : EditorBase::LogEntry::Level::Error);
            }
        }

        void RenderGraphEditorComposite::loadPipelineObjectsFromFile(const std::string& filename)
        {
            std::string path = filename;
            if (path.find(".pso") == std::string::npos)
            {
                path += ".pso";
            }

            auto* res = Resource::Resource::InstancePtr();
            if (!res || !res->getResourceLoader())
            {
                return;
            }

            auto contentOr = res->getResourceLoader()->loadFile(path);
            if (!contentOr.ok())
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        tr("管线对象", "Pipeline Object"),
                        std::string(tr("读取管线配置失败: ", "Failed to load PSO config: ")) +
                            std::string(contentOr.status().message()),
                        EditorBase::LogEntry::Level::Warn);
                }
                return;
            }

            toml::parse_result parsed = toml::parse(contentOr.value());
            if (!parsed)
            {
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        tr("管线对象", "Pipeline Object"),
                        tr("解析 .pso TOML 文件失败", "Failed to parse .pso TOML file"),
                        EditorBase::LogEntry::Level::Error);
                }
                return;
            }

            const auto& tbl = parsed.table();
            if (auto arr = tbl["pipeline_objects"].as_array())
            {
                for (const auto& elem : *arr)
                {
                    const auto* itemTbl = elem.as_table();
                    if (!itemTbl)
                    {
                        continue;
                    }

                    Domain::Renderer::PipelineObjectDesc pso{};
                    pso.name = (*itemTbl)["name"].value_or(std::string(""));
                    if (pso.name.empty())
                    {
                        continue;
                    }
                    pso.passName = (*itemTbl)["pass_name"].value_or(std::string("ObjectPass"));
                    pso.isBuiltin = (*itemTbl)["is_builtin"].value_or(false);
                    pso.hasColorAttachment = (*itemTbl)["has_color_attachment"].value_or(pso.passName != "ShadowMapPass");
                    pso.polygonMode = keyToEnum(
                        (*itemTbl)["polygon_mode"].value_or(std::string_view("Fill")),
                        kPolygonModeTable,
                        FCT::PolygonMode::Fill);
                    pso.cullMode = keyToEnum(
                        (*itemTbl)["cull_mode"].value_or(std::string_view("None")),
                        kCullModeTable,
                        FCT::CullMode::None);
                    pso.frontFace = keyToEnum(
                        (*itemTbl)["front_face"].value_or(std::string_view("CounterClockwise")),
                        kFrontFaceTable,
                        FCT::FrontFace::CounterClockwise);
                    pso.lineWidth = (*itemTbl)["line_width"].value_or(1.0f);
                    pso.depthClampEnable = (*itemTbl)["depth_clamp"].value_or(false);
                    pso.depthBiasEnable = (*itemTbl)["depth_bias"].value_or(false);
                    pso.depthBiasConstant = (*itemTbl)["depth_bias_constant"].value_or(0.0f);
                    pso.depthBiasSlope = (*itemTbl)["depth_bias_slope"].value_or(0.0f);
                    pso.depthBiasClamp = (*itemTbl)["depth_bias_clamp"].value_or(0.0f);
                    pso.depthTestEnable = (*itemTbl)["depth_test"].value_or(true);
                    pso.depthWriteEnable = (*itemTbl)["depth_write"].value_or(true);
                    pso.depthCompareOp = keyToEnum(
                        (*itemTbl)["depth_compare"].value_or(std::string_view("LessOrEqual")),
                        kCompareOpTable,
                        FCT::CompareOp::LessOrEqual);
                    pso.stencilTestEnable = (*itemTbl)["stencil_test"].value_or(false);
                    pso.blendEnable = (*itemTbl)["blend_enable"].value_or(false);
                    pso.srcColorBlend = keyToEnum(
                        (*itemTbl)["src_color_blend"].value_or(std::string_view("One")),
                        kBlendFactorTable,
                        FCT::BlendFactor::One);
                    pso.dstColorBlend = keyToEnum(
                        (*itemTbl)["dst_color_blend"].value_or(std::string_view("Zero")),
                        kBlendFactorTable,
                        FCT::BlendFactor::Zero);
                    pso.colorBlendOp = keyToEnum(
                        (*itemTbl)["color_blend_op"].value_or(std::string_view("Add")),
                        kBlendOpTable,
                        FCT::BlendOp::Add);
                    pso.srcAlphaBlend = keyToEnum(
                        (*itemTbl)["src_alpha_blend"].value_or(std::string_view("One")),
                        kBlendFactorTable,
                        FCT::BlendFactor::One);
                    pso.dstAlphaBlend = keyToEnum(
                        (*itemTbl)["dst_alpha_blend"].value_or(std::string_view("Zero")),
                        kBlendFactorTable,
                        FCT::BlendFactor::Zero);
                    pso.alphaBlendOp = keyToEnum(
                        (*itemTbl)["alpha_blend_op"].value_or(std::string_view("Add")),
                        kBlendOpTable,
                        FCT::BlendOp::Add);
                    pso.colorWriteMask = static_cast<uint8_t>(
                        std::clamp((*itemTbl)["color_write_mask"].value_or(15), 0, 15));

                    applyPipelineObjectToRenderer(pso, false);
                }

                syncPipelineObjectsFromRenderer(true);
                if (m_editorUiComposite)
                {
                    m_editorUiComposite->showToast(
                        tr("管线对象", "Pipeline Object"),
                        std::string(tr("已加载管线配置: ", "Loaded PSO config: ")) + path,
                        EditorBase::LogEntry::Level::Info);
                }
            }
        }

    } // namespace MQEditor
} // namespace MQEngine
