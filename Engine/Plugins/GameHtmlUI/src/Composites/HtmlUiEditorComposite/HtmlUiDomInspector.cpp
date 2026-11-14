/**
 * @file HtmlUiDomInspector.cpp
 * @brief DOM 节点层级树、拖拽父子重排 (Reparenting)、CSS 盒模型可视化与样式属性检视面板实现
 */
#include "HtmlUiEditorComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cstdio>

namespace MQEngine
{
    namespace GameHtmlUI
    {
        namespace
        {
            ImVec4 col32ToFloat4(ImU32 col)
            {
                return ImVec4(
                    static_cast<float>((col) & 0xFF) / 255.0f,
                    static_cast<float>((col >> 8) & 0xFF) / 255.0f,
                    static_cast<float>((col >> 16) & 0xFF) / 255.0f,
                    static_cast<float>((col >> 24) & 0xFF) / 255.0f);
            }

            ImU32 float4ToCol32(const ImVec4& c)
            {
                return IM_COL32(
                    std::clamp(static_cast<int>(c.x * 255.0f + 0.5f), 0, 255),
                    std::clamp(static_cast<int>(c.y * 255.0f + 0.5f), 0, 255),
                    std::clamp(static_cast<int>(c.z * 255.0f + 0.5f), 0, 255),
                    std::clamp(static_cast<int>(c.w * 255.0f + 0.5f), 0, 255));
            }
        } // namespace

        void HtmlUiEditorComposite::reparentElement(uint32_t childId, uint32_t newParentId)
        {
            if (childId == newParentId)
            {
                return;
            }

            uint32_t check = newParentId;
            while (check != 0)
            {
                if (check == childId)
                {
                    return;
                }
                const HtmlUiElement* checkElem = findElementById(check);
                check = checkElem ? checkElem->parentId : 0;
            }

            auto before = m_elements;

            if (HtmlUiElement* child = findElementById(childId))
            {
                const uint32_t oldParent = child->parentId;
                child->parentId = newParentId;
                if (HtmlUiElement* oldParentElem = findElementById(oldParent))
                {
                    oldParentElem->childIds.erase(
                        std::remove(oldParentElem->childIds.begin(), oldParentElem->childIds.end(), childId),
                        oldParentElem->childIds.end());
                }
            }

            if (newParentId != 0)
            {
                if (HtmlUiElement* newParentElem = findElementById(newParentId))
                {
                    newParentElem->childIds.push_back(childId);
                }
            }

            auto updateDepth = [this](auto& self, uint32_t eid, int d) -> void {
                if (HtmlUiElement* e = findElementById(eid))
                {
                    e->depth = d;
                    for (uint32_t cid : e->childIds)
                    {
                        self(self, cid, d + 1);
                    }
                }
            };

            for (auto& e : m_elements)
            {
                if (e.parentId == 0)
                {
                    updateDepth(updateDepth, e.id, 0);
                }
            }

            layoutElementsHierarchically();
            pushCommand("DOM 节点重排 (Reparent)", std::move(before));
            regenerateRmlAndRcssFromElements();
        }

        void HtmlUiEditorComposite::renderDomHierarchyPane()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("DOM 节点树 (点击选中 / 拖拽重排)", "DOM Hierarchy"));
            ImGui::Separator();

            for (auto& elem : m_elements)
            {
                if (elem.parentId == 0)
                {
                    renderDomNodeRecursive(elem);
                }
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.y > 20.0f)
            {
                ImGui::Dummy(avail);
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RML_DOM_REPARENT"))
                    {
                        const uint32_t draggedId = *static_cast<const uint32_t*>(p->Data);
                        reparentElement(draggedId, 0);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }

        void HtmlUiEditorComposite::renderDomNodeRecursive(HtmlUiElement& elem)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (elem.childIds.empty())
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (static_cast<int32_t>(elem.id) == m_selectedElementId)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            std::string label = "<" + elem.tag + ">";
            if (elem.emitDomIdAttr && !elem.domId.empty())
            {
                label += " #" + elem.domId;
            }
            else if (!elem.classes.empty())
            {
                label += " ." + elem.classes;
            }
            else
            {
                label += " (" + elem.domId + ")";
            }

            const bool nodeOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(elem.id)), flags, "%s", label.c_str());

            if (ImGui::IsItemClicked())
            {
                m_selectedElementId = static_cast<int32_t>(elem.id);
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload("RML_DOM_REPARENT", &elem.id, sizeof(uint32_t));
                ImGui::SetDragDropPayload("RML_DOM_ELEMENT", elem.domId.c_str(), elem.domId.size() + 1);
                ImGui::Text("DOM: <%s> #%s", elem.tag.c_str(), elem.domId.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RML_DOM_REPARENT"))
                {
                    const uint32_t draggedId = *static_cast<const uint32_t*>(p->Data);
                    reparentElement(draggedId, elem.id);
                }
                ImGui::EndDragDropTarget();
            }

            if (nodeOpen && !elem.childIds.empty())
            {
                for (uint32_t cid : elem.childIds)
                {
                    if (HtmlUiElement* child = findElementById(cid))
                    {
                        renderDomNodeRecursive(*child);
                    }
                }
                ImGui::TreePop();
            }
        }

        void HtmlUiEditorComposite::renderBoxModelVisualizer(HtmlUiElement& elem)
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("CSS 盒模型可视化 (Box Model)", "CSS Box Model Visualizer"));

            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            if (w < 180.0f) w = 180.0f;
            const float h = 112.0f;
            const ImVec2 p1(p0.x + w, p0.y + h);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, p1, IM_COL32(230, 126, 34, 40), 4.0f);
            dl->AddRect(p0, p1, IM_COL32(230, 126, 34, 200), 4.0f);
            dl->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f), IM_COL32(230, 126, 34, 255), "margin");

            const ImVec2 bp0(p0.x + 14.0f, p0.y + 14.0f);
            const ImVec2 bp1(p1.x - 14.0f, p1.y - 14.0f);
            dl->AddRectFilled(bp0, bp1, IM_COL32(241, 196, 15, 50), 3.0f);
            dl->AddRect(bp0, bp1, IM_COL32(241, 196, 15, 220), 3.0f);
            dl->AddText(ImVec2(bp0.x + 4.0f, bp0.y + 2.0f), IM_COL32(241, 196, 15, 255), "border");

            const ImVec2 pp0(bp0.x + 14.0f, bp0.y + 14.0f);
            const ImVec2 pp1(bp1.x - 14.0f, bp1.y - 14.0f);
            dl->AddRectFilled(pp0, pp1, IM_COL32(46, 204, 113, 60), 2.0f);
            dl->AddRect(pp0, pp1, IM_COL32(46, 204, 113, 240), 2.0f);
            dl->AddText(ImVec2(pp0.x + 4.0f, pp0.y + 2.0f), IM_COL32(46, 204, 113, 255), "padding");

            const ImVec2 cp0(pp0.x + 14.0f, pp0.y + 14.0f);
            const ImVec2 cp1(pp1.x - 14.0f, pp1.y - 14.0f);
            dl->AddRectFilled(cp0, cp1, IM_COL32(52, 152, 219, 100), 2.0f);
            dl->AddRect(cp0, cp1, IM_COL32(52, 152, 219, 255), 2.0f);

            char dimBuf[32];
            std::snprintf(dimBuf, sizeof(dimBuf), "%.0f x %.0f", elem.width, elem.height);
            const ImVec2 ts = ImGui::CalcTextSize(dimBuf);
            dl->AddText(
                ImVec2(cp0.x + (cp1.x - cp0.x - ts.x) * 0.5f, cp0.y + (cp1.y - cp0.y - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 255),
                dimBuf);

            ImGui::Dummy(ImVec2(w, h));

            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), "%s:", L("Margin (外边距)", "Margin"));
            float margin[4] = {elem.boxModel.marginTop, elem.boxModel.marginRight, elem.boxModel.marginBottom, elem.boxModel.marginLeft};
            if (ImGui::DragFloat4("T/R/B/L##Margin", margin, 1.0f, 0.0f, 100.0f, "%.0f"))
            {
                auto before = m_elements;
                elem.boxModel.marginTop = margin[0];
                elem.boxModel.marginRight = margin[1];
                elem.boxModel.marginBottom = margin[2];
                elem.boxModel.marginLeft = margin[3];
                elem.emitDomIdAttr = true;
                elem.layoutModifiedByUser = true;
                pushCommand("调整 Margin 盒模型", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.06f, 1.0f), "%s:", L("Border (边框粗细)", "Border"));
            float border[4] = {elem.boxModel.borderTop, elem.boxModel.borderRight, elem.boxModel.borderBottom, elem.boxModel.borderLeft};
            if (ImGui::DragFloat4("T/R/B/L##Border", border, 1.0f, 0.0f, 32.0f, "%.0f"))
            {
                auto before = m_elements;
                elem.boxModel.borderTop = border[0];
                elem.boxModel.borderRight = border[1];
                elem.boxModel.borderBottom = border[2];
                elem.boxModel.borderLeft = border[3];
                elem.emitDomIdAttr = true;
                elem.layoutModifiedByUser = true;
                pushCommand("调整 Border 盒模型", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            ImGui::TextColored(ImVec4(0.18f, 0.8f, 0.44f, 1.0f), "%s:", L("Padding (内边距)", "Padding"));
            float pad[4] = {elem.boxModel.paddingTop, elem.boxModel.paddingRight, elem.boxModel.paddingBottom, elem.boxModel.paddingLeft};
            if (ImGui::DragFloat4("T/R/B/L##Padding", pad, 1.0f, 0.0f, 100.0f, "%.0f"))
            {
                auto before = m_elements;
                elem.boxModel.paddingTop = pad[0];
                elem.boxModel.paddingRight = pad[1];
                elem.boxModel.paddingBottom = pad[2];
                elem.boxModel.paddingLeft = pad[3];
                elem.emitDomIdAttr = true;
                elem.layoutModifiedByUser = true;
                layoutContainerChildren(elem);
                pushCommand("调整 Padding 盒模型", std::move(before));
                regenerateRmlAndRcssFromElements();
            }
        }

        void HtmlUiEditorComposite::renderElementInspectorPane()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("选中元素样式检视", "Selected Element Inspector"));
            ImGui::Separator();

            HtmlUiElement* selectedElem = (m_selectedElementId != -1)
                ? findElementById(static_cast<uint32_t>(m_selectedElementId))
                : nullptr;

            if (!selectedElem)
            {
                ImGui::TextDisabled("%s", L("请在画布或左侧 DOM 树中点击选择元素", "Click an element to inspect"));
                return;
            }

            char idBuf[64];
            std::snprintf(idBuf, sizeof(idBuf), "%s", selectedElem->domId.c_str());
            if (ImGui::InputText(L("DOM ID", "DOM ID"), idBuf, sizeof(idBuf)))
            {
                auto before = m_elements;
                selectedElem->domId = idBuf;
                selectedElem->emitDomIdAttr = !selectedElem->domId.empty();
                pushCommand("修改 DOM ID", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            char classBuf[128];
            std::snprintf(classBuf, sizeof(classBuf), "%s", selectedElem->classes.c_str());
            if (ImGui::InputText(L("CSS Class", "CSS Class"), classBuf, sizeof(classBuf)))
            {
                auto before = m_elements;
                selectedElem->classes = classBuf;
                pushCommand("修改 CSS Class", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            char textBuf[256];
            std::snprintf(textBuf, sizeof(textBuf), "%s", selectedElem->innerText.c_str());
            if (ImGui::InputText(L("文本内容", "Inner Text"), textBuf, sizeof(textBuf)))
            {
                auto before = m_elements;
                selectedElem->innerText = textBuf;
                pushCommand("修改文本内容", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            if (selectedElem->tag == "img")
            {
                char srcBuf[256];
                std::snprintf(srcBuf, sizeof(srcBuf), "%s", selectedElem->srcAttr.c_str());
                if (ImGui::InputText(L("图片路径", "Image Src"), srcBuf, sizeof(srcBuf)))
                {
                    auto before = m_elements;
                    selectedElem->srcAttr = srcBuf;
                    pushCommand("修改图片路径", std::move(before));
                    regenerateRmlAndRcssFromElements();
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM"))
                    {
                        auto before = m_elements;
                        selectedElem->srcAttr = toProjectRelativeUiPath(static_cast<const char*>(p->Data));
                        pushCommand("拖放贴图资产", std::move(before));
                        regenerateRmlAndRcssFromElements();
                    }
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                    {
                        auto before = m_elements;
                        selectedElem->srcAttr = toProjectRelativeUiPath(static_cast<const char*>(p->Data));
                        pushCommand("拖放贴图资产", std::move(before));
                        regenerateRmlAndRcssFromElements();
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            float pos[2] = {selectedElem->x, selectedElem->y};
            if (ImGui::DragFloat2(L("坐标 (dp)", "Position"), pos, 1.0f))
            {
                auto before = m_elements;
                const float dx = pos[0] - selectedElem->x;
                const float dy = pos[1] - selectedElem->y;
                translateElementSubtree(selectedElem->id, dx, dy);
                selectedElem->positionMode = "absolute";
                selectedElem->hasExplicitLeft = true;
                selectedElem->hasExplicitTop = true;
                selectedElem->hasExplicitRight = false;
                selectedElem->hasExplicitBottom = false;
                selectedElem->hasExplicitWidth = true;
                selectedElem->hasExplicitHeight = true;
                selectedElem->emitDomIdAttr = true;
                selectedElem->layoutModifiedByUser = true;
                pushCommand("移动坐标", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            float size[2] = {selectedElem->width, selectedElem->height};
            if (ImGui::DragFloat2(L("尺寸 (dp)", "Size"), size, 1.0f, 10.0f, 2000.0f))
            {
                auto before = m_elements;
                selectedElem->width = size[0];
                selectedElem->height = size[1];
                selectedElem->positionMode = "absolute";
                selectedElem->hasExplicitLeft = true;
                selectedElem->hasExplicitTop = true;
                selectedElem->hasExplicitWidth = true;
                selectedElem->hasExplicitHeight = true;
                selectedElem->emitDomIdAttr = true;
                selectedElem->layoutModifiedByUser = true;
                layoutContainerChildren(*selectedElem);
                pushCommand("调整尺寸", std::move(before));
                regenerateRmlAndRcssFromElements();
            }

            ImVec4 bgColF = col32ToFloat4(selectedElem->bgColor);
            if (ImGui::ColorEdit4(L("背景颜色", "Background"), &bgColF.x, ImGuiColorEditFlags_AlphaBar))
            {
                selectedElem->bgColor = float4ToCol32(bgColF);
                selectedElem->emitDomIdAttr = true;
                selectedElem->layoutModifiedByUser = true;
                m_isDirty = true;
                regenerateRmlAndRcssFromElements();
            }

            ImVec4 txtColF = col32ToFloat4(selectedElem->textColor);
            if (ImGui::ColorEdit4(L("文字颜色", "Text Color"), &txtColF.x, ImGuiColorEditFlags_AlphaBar))
            {
                selectedElem->textColor = float4ToCol32(txtColF);
                selectedElem->emitDomIdAttr = true;
                selectedElem->layoutModifiedByUser = true;
                m_isDirty = true;
                regenerateRmlAndRcssFromElements();
            }

            if (ImGui::SliderFloat(L("圆角半径", "Radius"), &selectedElem->borderRadius, 0.0f, 32.0f, "%.0f dp"))
            {
                selectedElem->emitDomIdAttr = true;
                selectedElem->layoutModifiedByUser = true;
                m_isDirty = true;
                regenerateRmlAndRcssFromElements();
            }

            ImGui::Separator();
            renderBoxModelVisualizer(*selectedElem);

            ImGui::Separator();
            if (ImGui::Button(L("[+ 绑定蓝图事件]", "[+ Bind in Blueprint]"), ImVec2(-1.0f, 26.0f)))
            {
                bindElementEventToBlueprint(*selectedElem, "click");
            }

            if (ImGui::Button(L("删除该元素", "Delete Element"), ImVec2(-1.0f, 24.0f)))
            {
                auto before = m_elements;
                const uint32_t targetId = selectedElem->id;
                m_selectedElementId = -1;
                deleteElementSubtree(targetId);
                pushCommand("删除 DOM 元素", std::move(before));
                regenerateRmlAndRcssFromElements();
            }
        }

    } // namespace GameHtmlUI
} // namespace MQEngine
