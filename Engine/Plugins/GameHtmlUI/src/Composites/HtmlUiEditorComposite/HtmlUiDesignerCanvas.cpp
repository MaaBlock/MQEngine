/**
 * @file HtmlUiDesignerCanvas.cpp
 * @brief 1280x720 自适应缩放所见即所得 (WYSIWYG) 画布渲染、快照差分拖拽位移、八向缩放手柄与对齐参考线实现
 */
#include "HtmlUiEditorComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace MQEngine
{
    namespace GameHtmlUI
    {
        namespace
        {
            constexpr float kHandleSize = 7.0f;
            constexpr float kSnapThreshold = 6.0f;
            constexpr float kCanvasMargin = 14.0f;
        } // namespace

        void HtmlUiEditorComposite::insertElementFromPalette(
            const std::string& tag,
            float dropDesignX,
            float dropDesignY)
        {
            auto before = m_elements;

            HtmlUiElement elem;
            elem.id = ++m_elementIdCounter;
            elem.tag = tag;
            elem.domId = tag + "_" + std::to_string(elem.id);
            elem.emitDomIdAttr = true;
            elem.positionMode = "absolute";
            elem.hasExplicitLeft = true;
            elem.hasExplicitTop = true;
            elem.hasExplicitWidth = true;
            elem.hasExplicitHeight = true;
            elem.layoutModifiedByUser = true;
            elem.x = std::clamp(dropDesignX, 0.0f, kDesignCanvasWidth - 40.0f);
            elem.y = std::clamp(dropDesignY, 0.0f, kDesignCanvasHeight - 24.0f);

            if (m_snapToGrid && m_gridSize > 1.0f)
            {
                elem.x = std::round(elem.x / m_gridSize) * m_gridSize;
                elem.y = std::round(elem.y / m_gridSize) * m_gridSize;
            }
            elem.cssLeft = elem.x;
            elem.cssTop = elem.y;

            if (tag == "button")
            {
                elem.width = 128.0f;
                elem.height = 34.0f;
                elem.innerText = "新建按钮";
                elem.textAlign = "center";
                elem.bgColor = IM_COL32(41, 128, 185, 226);
                elem.borderRadius = 6.0f;
                elem.boxModel.borderTop = elem.boxModel.borderRight = elem.boxModel.borderBottom = elem.boxModel.borderLeft = 1.0f;
            }
            else if (tag == "p" || tag == "span" || tag == "label")
            {
                elem.width = 160.0f;
                elem.height = 26.0f;
                elem.innerText = "文本标签";
                elem.bgColor = IM_COL32(28, 40, 62, 180);
                elem.borderRadius = 4.0f;
                elem.boxModel.paddingLeft = elem.boxModel.paddingRight = 6.0f;
            }
            else if (tag == "h1")
            {
                elem.width = 240.0f;
                elem.height = 36.0f;
                elem.innerText = "标题文本";
                elem.fontSize = 18.0f;
                elem.bgColor = IM_COL32(0, 0, 0, 0);
                elem.textColor = IM_COL32(127, 214, 255, 255);
            }
            else if (tag == "progress")
            {
                elem.width = 220.0f;
                elem.height = 20.0f;
                elem.innerText = "75%";
                elem.textAlign = "center";
                elem.bgColor = IM_COL32(46, 204, 113, 220);
                elem.borderRadius = 4.0f;
            }
            else if (tag == "img")
            {
                elem.width = 64.0f;
                elem.height = 64.0f;
                elem.srcAttr = "Assets/Textures/Logo.png";
                elem.bgColor = IM_COL32(50, 60, 75, 220);
                elem.borderRadius = 4.0f;
            }
            else if (tag == "select" || tag == "input")
            {
                elem.width = 160.0f;
                elem.height = 28.0f;
                elem.bgColor = IM_COL32(30, 40, 55, 230);
                elem.borderRadius = 4.0f;
            }
            else
            {
                elem.width = 220.0f;
                elem.height = 96.0f;
                elem.bgColor = IM_COL32(14, 21, 34, 220);
                elem.borderRadius = 8.0f;
                elem.boxModel.borderTop = elem.boxModel.borderRight = elem.boxModel.borderBottom = elem.boxModel.borderLeft = 1.0f;
                elem.boxModel.paddingTop = elem.boxModel.paddingBottom = 8.0f;
                elem.boxModel.paddingLeft = elem.boxModel.paddingRight = 10.0f;
            }

            m_selectedElementId = static_cast<int32_t>(elem.id);
            m_elements.push_back(std::move(elem));
            pushCommand("添加 DOM 元素 <" + tag + ">", std::move(before));
            regenerateRmlAndRcssFromElements();
        }

        ResizeHandle HtmlUiEditorComposite::hitTestResizeHandles(
            const HtmlUiElement& elem,
            const ImVec2& mousePos,
            const ImVec2& viewOrigin,
            float viewScale)
        {
            const float ex = viewOrigin.x + elem.x * viewScale;
            const float ey = viewOrigin.y + elem.y * viewScale;
            const float ew = elem.width * viewScale;
            const float eh = elem.height * viewScale;

            auto inHandle = [&](float hx, float hy) -> bool {
                return (mousePos.x >= hx - kHandleSize && mousePos.x <= hx + kHandleSize &&
                        mousePos.y >= hy - kHandleSize && mousePos.y <= hy + kHandleSize);
            };

            if (inHandle(ex, ey)) return ResizeHandle::TopLeft;
            if (inHandle(ex + ew * 0.5f, ey)) return ResizeHandle::Top;
            if (inHandle(ex + ew, ey)) return ResizeHandle::TopRight;
            if (inHandle(ex + ew, ey + eh * 0.5f)) return ResizeHandle::Right;
            if (inHandle(ex + ew, ey + eh)) return ResizeHandle::BottomRight;
            if (inHandle(ex + ew * 0.5f, ey + eh)) return ResizeHandle::Bottom;
            if (inHandle(ex, ey + eh)) return ResizeHandle::BottomLeft;
            if (inHandle(ex, ey + eh * 0.5f)) return ResizeHandle::Left;

            return ResizeHandle::None;
        }

        void HtmlUiEditorComposite::applyResizeFromSnapshot(
            HtmlUiElement& elem,
            const HtmlUiElement& startElem,
            ResizeHandle handle,
            const ImVec2& totalDesignDelta)
        {
            float nx = startElem.x;
            float ny = startElem.y;
            float nw = startElem.width;
            float nh = startElem.height;

            const auto snapVal = [this](float v) -> float {
                if (m_snapToGrid && m_gridSize > 1.0f && !ImGui::GetIO().KeyAlt)
                {
                    return std::round(v / m_gridSize) * m_gridSize;
                }
                return v;
            };

            switch (handle)
            {
            case ResizeHandle::Right:
                nw = std::max(16.0f, snapVal(startElem.width + totalDesignDelta.x));
                break;
            case ResizeHandle::Bottom:
                nh = std::max(14.0f, snapVal(startElem.height + totalDesignDelta.y));
                break;
            case ResizeHandle::BottomRight:
                nw = std::max(16.0f, snapVal(startElem.width + totalDesignDelta.x));
                nh = std::max(14.0f, snapVal(startElem.height + totalDesignDelta.y));
                break;
            case ResizeHandle::Left:
            {
                const float rightEdge = startElem.x + startElem.width;
                nx = std::min(rightEdge - 16.0f, snapVal(startElem.x + totalDesignDelta.x));
                nw = rightEdge - nx;
                break;
            }
            case ResizeHandle::Top:
            {
                const float bottomEdge = startElem.y + startElem.height;
                ny = std::min(bottomEdge - 14.0f, snapVal(startElem.y + totalDesignDelta.y));
                nh = bottomEdge - ny;
                break;
            }
            case ResizeHandle::TopLeft:
            {
                const float rightEdge = startElem.x + startElem.width;
                const float bottomEdge = startElem.y + startElem.height;
                nx = std::min(rightEdge - 16.0f, snapVal(startElem.x + totalDesignDelta.x));
                ny = std::min(bottomEdge - 14.0f, snapVal(startElem.y + totalDesignDelta.y));
                nw = rightEdge - nx;
                nh = bottomEdge - ny;
                break;
            }
            case ResizeHandle::TopRight:
            {
                const float bottomEdge = startElem.y + startElem.height;
                ny = std::min(bottomEdge - 14.0f, snapVal(startElem.y + totalDesignDelta.y));
                nh = bottomEdge - ny;
                nw = std::max(16.0f, snapVal(startElem.width + totalDesignDelta.x));
                break;
            }
            case ResizeHandle::BottomLeft:
            {
                const float rightEdge = startElem.x + startElem.width;
                nx = std::min(rightEdge - 16.0f, snapVal(startElem.x + totalDesignDelta.x));
                nw = rightEdge - nx;
                nh = std::max(14.0f, snapVal(startElem.height + totalDesignDelta.y));
                break;
            }
            case ResizeHandle::None:
                return;
            }

            elem.x = nx;
            elem.y = ny;
            elem.width = nw;
            elem.height = nh;
            elem.positionMode = "absolute";
            elem.hasExplicitLeft = true;
            elem.hasExplicitTop = true;
            elem.hasExplicitRight = false;
            elem.hasExplicitBottom = false;
            elem.hasExplicitWidth = true;
            elem.hasExplicitHeight = true;
            elem.emitDomIdAttr = true;
            elem.layoutModifiedByUser = true;
        }

        void HtmlUiEditorComposite::computeAlignmentGuides(const HtmlUiElement& elem, float canvasW, float canvasH)
        {
            m_activeGuides.clear();

            const float centerCanvasX = canvasW * 0.5f;
            const float elemCenterX = elem.x + elem.width * 0.5f;
            if (std::abs(elemCenterX - centerCanvasX) < kSnapThreshold)
            {
                m_activeGuides.push_back(AlignmentGuide{.isVertical = true, .position = centerCanvasX, .start = 0.0f, .end = canvasH});
            }

            for (const auto& other : m_elements)
            {
                if (other.id == elem.id || other.parentId == elem.id)
                {
                    continue;
                }

                if (std::abs(elem.x - other.x) < kSnapThreshold)
                {
                    m_activeGuides.push_back(AlignmentGuide{
                        .isVertical = true,
                        .position = other.x,
                        .start = std::min(elem.y, other.y),
                        .end = std::max(elem.y + elem.height, other.y + other.height)});
                }
                if (std::abs((elem.x + elem.width) - (other.x + other.width)) < kSnapThreshold)
                {
                    m_activeGuides.push_back(AlignmentGuide{
                        .isVertical = true,
                        .position = other.x + other.width,
                        .start = std::min(elem.y, other.y),
                        .end = std::max(elem.y + elem.height, other.y + other.height)});
                }
                if (std::abs(elem.y - other.y) < kSnapThreshold)
                {
                    m_activeGuides.push_back(AlignmentGuide{
                        .isVertical = false,
                        .position = other.y,
                        .start = std::min(elem.x, other.x),
                        .end = std::max(elem.x + elem.width, other.x + other.width)});
                }
                if (std::abs((elem.y + elem.height) - (other.y + other.height)) < kSnapThreshold)
                {
                    m_activeGuides.push_back(AlignmentGuide{
                        .isVertical = false,
                        .position = other.y + other.height,
                        .start = std::min(elem.x, other.x),
                        .end = std::max(elem.x + elem.width, other.x + other.width)});
                }
            }
        }

        void HtmlUiEditorComposite::renderAlignmentGuides(
            ImDrawList* drawList,
            const ImVec2& viewOrigin,
            float viewScale)
        {
            for (const auto& guide : m_activeGuides)
            {
                if (guide.isVertical)
                {
                    drawList->AddLine(
                        ImVec2(viewOrigin.x + guide.position * viewScale, viewOrigin.y + guide.start * viewScale),
                        ImVec2(viewOrigin.x + guide.position * viewScale, viewOrigin.y + guide.end * viewScale),
                        IM_COL32(0, 230, 255, 230),
                        1.5f);
                }
                else
                {
                    drawList->AddLine(
                        ImVec2(viewOrigin.x + guide.start * viewScale, viewOrigin.y + guide.position * viewScale),
                        ImVec2(viewOrigin.x + guide.end * viewScale, viewOrigin.y + guide.position * viewScale),
                        IM_COL32(0, 230, 255, 230),
                        1.5f);
                }
            }
        }

        void HtmlUiEditorComposite::renderControlPalettePane()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("UI 控件库 (拖入右侧画布)", "UI Palette (Drag to Canvas)"));
            ImGui::Separator();

            auto paletteItem = [this, &L](const char* label, const char* tag) {
                ImGui::Button(label, ImVec2(-1.0f, 26.0f));
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    ImGui::SetDragDropPayload("RML_PALETTE_TAG", tag, std::strlen(tag) + 1);
                    ImGui::Text("%s <%s>", L("创建", "Create"), tag);
                    ImGui::EndDragDropSource();
                }
            };

            paletteItem(L("按钮 <button>", "Button <button>"), "button");
            paletteItem(L("面板容器 <div>", "Panel <div>"), "div");
            paletteItem(L("状态胶囊 <span>", "Pill <span>"), "span");
            paletteItem(L("大标题 <h1>", "Heading <h1>"), "h1");
            paletteItem(L("文本段落 <p>", "Label <p>"), "p");
            paletteItem(L("进度条 <progress>", "Progress"), "progress");
            paletteItem(L("图像 <img>", "Image <img>"), "img");

            ImGui::Separator();
            ImGui::Checkbox(L("网格对齐吸附 (Alt临时关闭)", "Grid Snapping (Hold Alt)"), &m_snapToGrid);
            ImGui::SliderFloat(L("网格步长", "Grid Size"), &m_gridSize, 2.0f, 24.0f, "%.0f dp");
            ImGui::SliderFloat(L("视口缩放", "Zoom"), &m_canvasZoom, 0.5f, 1.8f, "%.2fx");
            if (ImGui::SmallButton(L("重置适配缩放", "Reset Fit Zoom")))
            {
                m_canvasZoom = 1.0f;
            }
        }

        void HtmlUiEditorComposite::renderWysiwygCanvas()
        {
            const ImVec2 paneP0 = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x < 160.0f) avail.x = 160.0f;
            if (avail.y < 160.0f) avail.y = 160.0f;
            const ImVec2 paneP1(paneP0.x + avail.x, paneP0.y + avail.y);

            // 先放置 InvisibleButton 捕获画布鼠标拖拽与拖放负载，防止拖拽元素时误移动外层停靠窗口
            ImGui::SetCursorScreenPos(paneP0);
            ImGui::InvisibleButton(
                "WysiwygCanvasHitArea",
                avail,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool canvasHovered = ImGui::IsItemHovered();
            const bool canvasActive = ImGui::IsItemActive();

            // 计算 1280x720 逻辑设计画布在当前面板中的等比居中视口
            const float fitScaleX = (avail.x - kCanvasMargin * 2.0f) / kDesignCanvasWidth;
            const float fitScaleY = (avail.y - kCanvasMargin * 2.0f - 18.0f) / kDesignCanvasHeight;
            const float baseFitScale = std::clamp(std::min(fitScaleX, fitScaleY), 0.20f, 1.50f);
            const float viewScale = std::clamp(baseFitScale * m_canvasZoom, 0.20f, 2.50f);

            const float scaledW = kDesignCanvasWidth * viewScale;
            const float scaledH = kDesignCanvasHeight * viewScale;
            const ImVec2 viewOrigin(
                paneP0.x + (avail.x - scaledW) * 0.5f,
                paneP0.y + 18.0f + (avail.y - 18.0f - scaledH) * 0.5f);
            const ImVec2 viewMax(viewOrigin.x + scaledW, viewOrigin.y + scaledH);

            ImGuiIO& io = ImGui::GetIO();
            if (canvasHovered && io.KeyCtrl && std::abs(io.MouseWheel) > 0.01f)
            {
                m_canvasZoom = std::clamp(m_canvasZoom + io.MouseWheel * 0.1f, 0.5f, 1.8f);
            }

            const ImVec2 mousePos = io.MousePos;
            const ImVec2 mouseDesignPos(
                (mousePos.x - viewOrigin.x) / viewScale,
                (mousePos.y - viewOrigin.y) / viewScale);

            // 接收控件库及资产面板拖放（挂载在 WysiwygCanvasHitArea 上）
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RML_PALETTE_TAG"))
                {
                    const char* tag = static_cast<const char*>(payload->Data);
                    insertElementFromPalette(tag, mouseDesignPos.x, mouseDesignPos.y);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM"))
                {
                    const char* assetPath = static_cast<const char*>(payload->Data);
                    insertElementFromPalette("img", mouseDesignPos.x, mouseDesignPos.y);
                    if (!m_elements.empty())
                    {
                        m_elements.back().srcAttr = toProjectRelativeUiPath(assetPath);
                        regenerateRmlAndRcssFromElements();
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                {
                    const char* assetPath = static_cast<const char*>(payload->Data);
                    insertElementFromPalette("img", mouseDesignPos.x, mouseDesignPos.y);
                    if (!m_elements.empty())
                    {
                        m_elements.back().srcAttr = toProjectRelativeUiPath(assetPath);
                        regenerateRmlAndRcssFromElements();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(paneP0, paneP1, true);

            // 背景底板与 1280x720 模拟游戏视口框
            drawList->AddRectFilled(paneP0, paneP1, IM_COL32(13, 16, 22, 255), 4.0f);
            drawList->AddRectFilled(viewOrigin, viewMax, IM_COL32(28, 42, 34, 255), 2.0f);
            drawList->AddRect(viewOrigin, viewMax, IM_COL32(90, 140, 205, 190), 2.0f, 0, 1.5f);

            char headerBuf[128];
            std::snprintf(
                headerBuf,
                sizeof(headerBuf),
                "1280 x 720 dp (%.0f%%)  |  鼠标: (%.0f, %.0f) dp  |  左键点选拖拽 / 八向手柄缩放 / Ctrl+S 保存热重载",
                viewScale * 100.0f,
                std::clamp(mouseDesignPos.x, 0.0f, kDesignCanvasWidth),
                std::clamp(mouseDesignPos.y, 0.0f, kDesignCanvasHeight));
            drawList->AddText(ImVec2(paneP0.x + 8.0f, paneP0.y + 2.0f), IM_COL32(165, 185, 215, 230), headerBuf);

            // 设计网格点阵
            const float gridStepPx = std::max(12.0f, 32.0f * viewScale);
            for (float y = viewOrigin.y + gridStepPx; y < viewMax.y; y += gridStepPx)
            {
                for (float x = viewOrigin.x + gridStepPx; x < viewMax.x; x += gridStepPx)
                {
                    drawList->AddCircleFilled(ImVec2(x, y), 1.0f, IM_COL32(55, 75, 65, 140));
                }
            }

            // 按前序遍历绘制所有 DOM 元素
            const float baseImGuiFontSize = std::max(10.0f, ImGui::GetFontSize());
            ImFont* activeFont = ImGui::GetFont();

            drawList->PushClipRect(viewOrigin, viewMax, true);
            for (const auto& elem : m_elements)
            {
                const ImVec2 elemMin(viewOrigin.x + elem.x * viewScale, viewOrigin.y + elem.y * viewScale);
                const ImVec2 elemMax(elemMin.x + elem.width * viewScale, elemMin.y + elem.height * viewScale);
                const float radius = elem.borderRadius * viewScale;

                if ((elem.bgColor >> 24) > 0)
                {
                    drawList->AddRectFilled(elemMin, elemMax, elem.bgColor, radius);
                }
                if (elem.boxModel.borderTop > 0.0f || elem.boxModel.borderLeft > 0.0f)
                {
                    const float bw = std::max(1.0f, elem.boxModel.borderTop * viewScale);
                    drawList->AddRect(elemMin, elemMax, elem.borderColor, radius, 0, bw);
                }
                else if (elem.childIds.empty() && (elem.bgColor >> 24) == 0 && elem.innerText.empty())
                {
                    drawList->AddRect(elemMin, elemMax, IM_COL32(80, 100, 130, 70), radius);
                }

                if (!elem.innerText.empty() && activeFont)
                {
                    const float scaledFontPx = std::clamp(elem.fontSize * viewScale, 8.0f, 32.0f);
                    const ImVec2 rawTextSz = ImGui::CalcTextSize(elem.innerText.c_str());
                    const ImVec2 textSz(
                        rawTextSz.x * (scaledFontPx / baseImGuiFontSize),
                        rawTextSz.y * (scaledFontPx / baseImGuiFontSize));

                    float tx = elemMin.x + (elem.boxModel.borderLeft + elem.boxModel.paddingLeft) * viewScale;
                    float ty = elemMin.y + ((elemMax.y - elemMin.y) - textSz.y) * 0.5f;
                    if (elem.textAlign == "center")
                    {
                        tx = elemMin.x + ((elemMax.x - elemMin.x) - textSz.x) * 0.5f;
                    }
                    else if (elem.textAlign == "right")
                    {
                        tx = elemMax.x - (elem.boxModel.borderRight + elem.boxModel.paddingRight) * viewScale - textSz.x;
                    }
                    if (!elem.childIds.empty() && elem.flexGrid.displayMode == "block")
                    {
                        ty = elemMin.y + (elem.boxModel.borderTop + elem.boxModel.paddingTop) * viewScale;
                    }

                    drawList->PushClipRect(elemMin, elemMax, true);
                    drawList->AddText(activeFont, scaledFontPx, ImVec2(tx, ty), elem.textColor, elem.innerText.c_str());
                    drawList->PopClipRect();
                }
            }
            drawList->PopClipRect();

            // 选中高亮边框与 8 向缩放把手（置于最顶层，不被子元素遮挡）
            if (m_selectedElementId != -1)
            {
                if (const HtmlUiElement* selElem = findElementById(static_cast<uint32_t>(m_selectedElementId)))
                {
                    const ImVec2 elemMin(viewOrigin.x + selElem->x * viewScale, viewOrigin.y + selElem->y * viewScale);
                    const ImVec2 elemMax(elemMin.x + selElem->width * viewScale, elemMin.y + selElem->height * viewScale);
                    const float ew = elemMax.x - elemMin.x;
                    const float eh = elemMax.y - elemMin.y;

                    drawList->AddRect(elemMin, elemMax, IM_COL32(255, 215, 0, 255), selElem->borderRadius * viewScale, 0, 2.0f);

                    auto drawHandle = [&](float hx, float hy) {
                        drawList->AddRectFilled(
                            ImVec2(hx - kHandleSize * 0.5f, hy - kHandleSize * 0.5f),
                            ImVec2(hx + kHandleSize * 0.5f, hy + kHandleSize * 0.5f),
                            IM_COL32(255, 215, 0, 255));
                        drawList->AddRect(
                            ImVec2(hx - kHandleSize * 0.5f, hy - kHandleSize * 0.5f),
                            ImVec2(hx + kHandleSize * 0.5f, hy + kHandleSize * 0.5f),
                            IM_COL32(20, 20, 20, 255));
                    };

                    drawHandle(elemMin.x, elemMin.y);
                    drawHandle(elemMin.x + ew * 0.5f, elemMin.y);
                    drawHandle(elemMin.x + ew, elemMin.y);
                    drawHandle(elemMin.x + ew, elemMin.y + eh * 0.5f);
                    drawHandle(elemMin.x + ew, elemMin.y + eh);
                    drawHandle(elemMin.x + ew * 0.5f, elemMin.y + eh);
                    drawHandle(elemMin.x, elemMin.y + eh);
                    drawHandle(elemMin.x, elemMin.y + eh * 0.5f);
                }
            }

            // 鼠标点选、快照累积拖拽与八向缩放
            if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m_activeResizeHandle = ResizeHandle::None;
                m_isDraggingElement = false;
                m_dragStartMousePos = mousePos;

                if (m_selectedElementId != -1)
                {
                    if (const HtmlUiElement* selElem = findElementById(static_cast<uint32_t>(m_selectedElementId)))
                    {
                        const ResizeHandle h = hitTestResizeHandles(*selElem, mousePos, viewOrigin, viewScale);
                        if (h != ResizeHandle::None)
                        {
                            m_activeResizeHandle = h;
                            m_dragStartSnapshot = m_elements;
                        }
                    }
                }

                if (m_activeResizeHandle == ResizeHandle::None)
                {
                    int32_t hitId = -1;
                    for (int i = static_cast<int>(m_elements.size()) - 1; i >= 0; --i)
                    {
                        const auto& elem = m_elements[i];
                        if (mouseDesignPos.x >= elem.x && mouseDesignPos.x <= elem.x + elem.width &&
                            mouseDesignPos.y >= elem.y && mouseDesignPos.y <= elem.y + elem.height)
                        {
                            hitId = static_cast<int32_t>(elem.id);
                            break;
                        }
                    }
                    m_selectedElementId = hitId;
                    if (hitId != -1)
                    {
                        m_isDraggingElement = true;
                        m_dragStartSnapshot = m_elements;
                    }
                }
            }

            if ((canvasActive || m_isDraggingElement || m_activeResizeHandle != ResizeHandle::None) &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f) &&
                !m_dragStartSnapshot.empty())
            {
                const ImVec2 totalDesignDelta(
                    (mousePos.x - m_dragStartMousePos.x) / viewScale,
                    (mousePos.y - m_dragStartMousePos.y) / viewScale);

                const HtmlUiElement* startElemPtr = nullptr;
                for (const auto& se : m_dragStartSnapshot)
                {
                    if (static_cast<int32_t>(se.id) == m_selectedElementId)
                    {
                        startElemPtr = &se;
                        break;
                    }
                }

                HtmlUiElement* curElem = (m_selectedElementId != -1)
                    ? findElementById(static_cast<uint32_t>(m_selectedElementId))
                    : nullptr;

                if (startElemPtr && curElem)
                {
                    if (m_activeResizeHandle != ResizeHandle::None)
                    {
                        m_elements = m_dragStartSnapshot;
                        curElem = findElementById(static_cast<uint32_t>(m_selectedElementId));
                        if (curElem)
                        {
                            applyResizeFromSnapshot(*curElem, *startElemPtr, m_activeResizeHandle, totalDesignDelta);
                            layoutContainerChildren(*curElem);
                            computeAlignmentGuides(*curElem, kDesignCanvasWidth, kDesignCanvasHeight);
                            m_isDirty = true;
                            regenerateRmlAndRcssFromElements();
                        }
                    }
                    else if (m_isDraggingElement)
                    {
                        m_elements = m_dragStartSnapshot;
                        curElem = findElementById(static_cast<uint32_t>(m_selectedElementId));
                        if (curElem)
                        {
                            float targetX = startElemPtr->x + totalDesignDelta.x;
                            float targetY = startElemPtr->y + totalDesignDelta.y;
                            if (m_snapToGrid && m_gridSize > 1.0f && !io.KeyAlt)
                            {
                                targetX = std::round(targetX / m_gridSize) * m_gridSize;
                                targetY = std::round(targetY / m_gridSize) * m_gridSize;
                            }
                            const float appliedDx = targetX - startElemPtr->x;
                            const float appliedDy = targetY - startElemPtr->y;

                            translateElementSubtree(curElem->id, appliedDx, appliedDy);
                            curElem->positionMode = "absolute";
                            curElem->hasExplicitLeft = true;
                            curElem->hasExplicitTop = true;
                            curElem->hasExplicitRight = false;
                            curElem->hasExplicitBottom = false;
                            curElem->hasExplicitWidth = true;
                            curElem->hasExplicitHeight = true;
                            curElem->emitDomIdAttr = true;
                            curElem->layoutModifiedByUser = true;

                            computeAlignmentGuides(*curElem, kDesignCanvasWidth, kDesignCanvasHeight);
                            m_isDirty = true;
                            regenerateRmlAndRcssFromElements();
                        }
                    }
                }
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if ((m_isDraggingElement || m_activeResizeHandle != ResizeHandle::None) && !m_dragStartSnapshot.empty())
                {
                    bool actuallyChanged = false;
                    if (m_elements.size() == m_dragStartSnapshot.size())
                    {
                        for (size_t idx = 0; idx < m_elements.size(); ++idx)
                        {
                            if (std::abs(m_elements[idx].x - m_dragStartSnapshot[idx].x) > 0.25f ||
                                std::abs(m_elements[idx].y - m_dragStartSnapshot[idx].y) > 0.25f ||
                                std::abs(m_elements[idx].width - m_dragStartSnapshot[idx].width) > 0.25f ||
                                std::abs(m_elements[idx].height - m_dragStartSnapshot[idx].height) > 0.25f)
                            {
                                actuallyChanged = true;
                                break;
                            }
                        }
                    }
                    if (actuallyChanged)
                    {
                        pushCommand("画布拖拽/缩放调整", std::move(m_dragStartSnapshot));
                    }
                    m_dragStartSnapshot.clear();
                }
                m_isDraggingElement = false;
                m_activeResizeHandle = ResizeHandle::None;
                m_activeGuides.clear();
            }

            renderAlignmentGuides(drawList, viewOrigin, viewScale);
            drawList->PopClipRect();
        }

    } // namespace GameHtmlUI
} // namespace MQEngine
