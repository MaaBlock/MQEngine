/**
 * @file BlueprintEditorCanvas.cpp
 * @brief 蓝图可视化编辑器 ImNodes 节点画布渲染、引脚连线交互、小地图与右键上下文菜单实现
 */
#include "BlueprintEditorComposite.h"

#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cstring>
#include <cctype>

namespace MQEngine
{
    namespace Blueprint
    {
        namespace
        {
            std::string toLowerFilter(std::string_view sv)
            {
                std::string res;
                res.reserve(sv.size());
                for (char c : sv)
                {
                    res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                return res;
            }
        } // namespace

        void BlueprintEditorComposite::ensureImNodesInitialized()
        {
            if (!m_imnodesCtx)
            {
                m_imnodesCtx = ImNodes::CreateContext();
                ImNodes::SetCurrentContext(m_imnodesCtx);
                ImNodes::StyleColorsDark();
                m_editorCtx = ImNodes::EditorContextCreate();
            }
            else
            {
                ImNodes::SetCurrentContext(m_imnodesCtx);
            }

            if (m_editorCtx)
            {
                ImNodes::EditorContextSet(m_editorCtx);
            }
        }

        bool BlueprintEditorComposite::isPinLinked(uint32_t pinId) const noexcept
        {
            for (const auto& link : m_links)
            {
                if (link.startPinId == pinId || link.endPinId == pinId)
                {
                    return true;
                }
            }
            return false;
        }

        const RuntimePin* BlueprintEditorComposite::findPin(uint32_t pinId, const EditorNode** outOwnerNode) const
        {
            for (const auto& n : m_nodes)
            {
                for (const auto& inPin : n.inputs)
                {
                    if (inPin.id == pinId)
                    {
                        if (outOwnerNode) *outOwnerNode = &n;
                        return &inPin;
                    }
                }
                for (const auto& outPin : n.outputs)
                {
                    if (outPin.id == pinId)
                    {
                        if (outOwnerNode) *outOwnerNode = &n;
                        return &outPin;
                    }
                }
            }
            return nullptr;
        }

        RuntimePin* BlueprintEditorComposite::findPinMutable(uint32_t pinId, EditorNode** outOwnerNode)
        {
            for (auto& n : m_nodes)
            {
                for (auto& inPin : n.inputs)
                {
                    if (inPin.id == pinId)
                    {
                        if (outOwnerNode) *outOwnerNode = &n;
                        return &inPin;
                    }
                }
                for (auto& outPin : n.outputs)
                {
                    if (outPin.id == pinId)
                    {
                        if (outOwnerNode) *outOwnerNode = &n;
                        return &outPin;
                    }
                }
            }
            return nullptr;
        }

        void BlueprintEditorComposite::handleLinkCreated(int startAttr, int endAttr)
        {
            const EditorNode* startOwner = nullptr;
            const EditorNode* endOwner = nullptr;
            const RuntimePin* pStart = findPin(static_cast<uint32_t>(startAttr), &startOwner);
            const RuntimePin* pEnd = findPin(static_cast<uint32_t>(endAttr), &endOwner);

            if (!pStart || !pEnd || startOwner == endOwner)
            {
                return;
            }

            uint32_t outPinId = static_cast<uint32_t>(startAttr);
            uint32_t inPinId = static_cast<uint32_t>(endAttr);
            if (!pStart->isOutput && pEnd->isOutput)
            {
                std::swap(outPinId, inPinId);
                std::swap(pStart, pEnd);
            }
            if (!pStart->isOutput || pEnd->isOutput)
            {
                return;
            }

            for (const auto& l : m_links)
            {
                if (l.startPinId == outPinId && l.endPinId == inPinId)
                {
                    return;
                }
            }

            GraphSnapshot beforeState = captureGraphSnapshot();
            m_links.push_back(EditorLink{
                .id = allocateLinkId(),
                .startPinId = outPinId,
                .endPinId = inPinId
            });
            pushCommand("创建连线", std::move(beforeState));
        }

        void BlueprintEditorComposite::handleLinkDestroyed(int linkId)
        {
            auto it = std::find_if(m_links.begin(), m_links.end(), [linkId](const EditorLink& l) {
                return l.id == static_cast<uint32_t>(linkId);
            });
            if (it != m_links.end())
            {
                GraphSnapshot beforeState = captureGraphSnapshot();
                m_links.erase(it);
                pushCommand("删除连线", std::move(beforeState));
            }
        }

        void BlueprintEditorComposite::handleDeleteSelection()
        {
            const int selectedLinkCount = ImNodes::NumSelectedLinks();
            const int selectedNodeCount = ImNodes::NumSelectedNodes();
            if (selectedLinkCount == 0 && selectedNodeCount == 0)
            {
                return;
            }

            GraphSnapshot beforeState = captureGraphSnapshot();

            if (selectedLinkCount > 0)
            {
                std::vector<int> linkIds(selectedLinkCount);
                ImNodes::GetSelectedLinks(linkIds.data());
                for (int lid : linkIds)
                {
                    m_links.erase(
                        std::remove_if(m_links.begin(), m_links.end(), [lid](const EditorLink& l) {
                            return l.id == static_cast<uint32_t>(lid);
                        }),
                        m_links.end());
                }
            }

            if (selectedNodeCount > 0)
            {
                std::vector<int> nodeIds(selectedNodeCount);
                ImNodes::GetSelectedNodes(nodeIds.data());
                for (int nid : nodeIds)
                {
                    auto nodeIt = std::find_if(m_nodes.begin(), m_nodes.end(), [nid](const EditorNode& n) {
                        return n.id == static_cast<uint32_t>(nid);
                    });
                    if (nodeIt != m_nodes.end())
                    {
                        std::vector<uint32_t> pinIds;
                        for (const auto& pin : nodeIt->inputs) pinIds.push_back(pin.id);
                        for (const auto& pin : nodeIt->outputs) pinIds.push_back(pin.id);

                        m_links.erase(
                            std::remove_if(m_links.begin(), m_links.end(), [&pinIds](const EditorLink& l) {
                                return std::find(pinIds.begin(), pinIds.end(), l.startPinId) != pinIds.end() ||
                                       std::find(pinIds.begin(), pinIds.end(), l.endPinId) != pinIds.end();
                            }),
                            m_links.end());

                        m_nodes.erase(nodeIt);
                    }
                }
            }

            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
            pushCommand("删除所选元素", std::move(beforeState));
        }

        void BlueprintEditorComposite::renderVisualCanvas()
        {
            ensureImNodesInitialized();

            ImNodes::BeginNodeEditor();

            // 1. 设置节点在编辑器网格空间的坐标（仅在坐标改变或首次加载时）
            if (!m_nodesPositioned)
            {
                for (const auto& node : m_nodes)
                {
                    ImNodes::SetNodeEditorSpacePos(static_cast<int>(node.id), ImVec2(node.pos.x, node.pos.y));
                }
                m_nodesPositioned = true;
            }

            // 2. 渲染各个 AST 节点
            for (auto& node : m_nodes)
            {
                const int nodeId = static_cast<int>(node.id);

                ImU32 titleBarColor = IM_COL32(40, 110, 180, 255);
                ImU32 titleBarSelected = IM_COL32(60, 140, 220, 255);
                if (node.opCode.find("On") == 0)
                {
                    titleBarColor = IM_COL32(180, 50, 60, 255);
                    titleBarSelected = IM_COL32(220, 70, 80, 255);
                }
                else if (node.opCode.find("UI") != std::string::npos || node.opCode.find("Style") != std::string::npos || node.opCode.find("Class") != std::string::npos)
                {
                    titleBarColor = IM_COL32(45, 140, 80, 255);
                    titleBarSelected = IM_COL32(65, 180, 110, 255);
                }
                else if (node.opCode.find("Component") != std::string::npos)
                {
                    titleBarColor = IM_COL32(130, 65, 175, 255);
                    titleBarSelected = IM_COL32(165, 85, 220, 255);
                }
                else if (node.opCode == "Branch" || node.opCode.find("Math") != std::string::npos || node.opCode.find("Logic") != std::string::npos || node.opCode == "Compare")
                {
                    titleBarColor = IM_COL32(190, 135, 35, 255);
                    titleBarSelected = IM_COL32(225, 165, 50, 255);
                }

                ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleBarColor);
                ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, titleBarSelected);

                ImNodes::BeginNode(nodeId);

                ImNodes::BeginNodeTitleBar();
                ImGui::TextUnformatted(node.title.c_str());
                ImNodes::EndNodeTitleBar();

                ImNodes::PopColorStyle();
                ImNodes::PopColorStyle();

                // 渲染输入引脚与未连线默认值内联编辑器
                for (auto& pin : node.inputs)
                {
                    const int pinId = static_cast<int>(pin.id);
                    ImNodes::BeginInputAttribute(pinId);

                    ImGui::TextColored(
                        (pin.dataType == PinDataType::Execution) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.95f, 1.0f),
                        "%s", pin.name.c_str());

                    if (!isPinLinked(pin.id) && pin.dataType != PinDataType::Execution)
                    {
                        ImGui::SameLine();
                        ImGui::PushItemWidth(85.0f);
                        std::string widgetId = "##pin_val_" + std::to_string(pin.id);

                        if (pin.dataType == PinDataType::String)
                        {
                            std::string strVal;
                            if (std::holds_alternative<std::string>(pin.defaultValue.data))
                            {
                                strVal = std::get<std::string>(pin.defaultValue.data);
                            }
                            char buf[128];
                            std::strncpy(buf, strVal.c_str(), sizeof(buf) - 1);
                            buf[sizeof(buf) - 1] = '\0';
                            if (ImGui::InputText(widgetId.c_str(), buf, sizeof(buf)))
                            {
                                pin.defaultValue.data = std::string(buf);
                                m_typeScriptSource = translateGraphToTypeScript();
                            }
                        }
                        else if (pin.dataType == PinDataType::Float)
                        {
                            float fVal = 0.0f;
                            if (std::holds_alternative<float>(pin.defaultValue.data))
                            {
                                fVal = std::get<float>(pin.defaultValue.data);
                            }
                            if (ImGui::DragFloat(widgetId.c_str(), &fVal, 0.1f))
                            {
                                pin.defaultValue.data = fVal;
                                m_typeScriptSource = translateGraphToTypeScript();
                            }
                        }
                        else if (pin.dataType == PinDataType::Bool)
                        {
                            bool bVal = false;
                            if (std::holds_alternative<bool>(pin.defaultValue.data))
                            {
                                bVal = std::get<bool>(pin.defaultValue.data);
                            }
                            if (ImGui::Checkbox(widgetId.c_str(), &bVal))
                            {
                                pin.defaultValue.data = bVal;
                                m_typeScriptSource = translateGraphToTypeScript();
                            }
                        }
                        else if (pin.dataType == PinDataType::Int)
                        {
                            int iVal = 0;
                            if (std::holds_alternative<int32_t>(pin.defaultValue.data))
                            {
                                iVal = std::get<int32_t>(pin.defaultValue.data);
                            }
                            if (ImGui::DragInt(widgetId.c_str(), &iVal))
                            {
                                pin.defaultValue.data = static_cast<int32_t>(iVal);
                                m_typeScriptSource = translateGraphToTypeScript();
                            }
                        }

                        ImGui::PopItemWidth();
                    }

                    ImNodes::EndInputAttribute();
                }

                // 渲染输出引脚
                for (const auto& pin : node.outputs)
                {
                    const int pinId = static_cast<int>(pin.id);
                    ImNodes::BeginOutputAttribute(pinId);
                    ImGui::Indent(40.0f);
                    ImGui::TextColored(
                        (pin.dataType == PinDataType::Execution) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.95f, 1.0f),
                        "%s", pin.name.c_str());
                    ImGui::Unindent(40.0f);
                    ImNodes::EndOutputAttribute();
                }

                ImNodes::EndNode();
            }

            // 3. 渲染所有连线
            for (const auto& link : m_links)
            {
                ImNodes::Link(static_cast<int>(link.id), static_cast<int>(link.startPinId), static_cast<int>(link.endPinId));
            }

            // 4. 渲染小地图
            ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);

            ImNodes::EndNodeEditor();

            // 5. 交互连线创建与销毁
            int startAttr = 0;
            int endAttr = 0;
            if (ImNodes::IsLinkCreated(&startAttr, &endAttr))
            {
                handleLinkCreated(startAttr, endAttr);
            }

            int destroyedLinkId = 0;
            if (ImNodes::IsLinkDestroyed(&destroyedLinkId))
            {
                handleLinkDestroyed(destroyedLinkId);
            }

            // 6. Delete / Backspace 键删除选中节点与连线
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
            {
                handleDeleteSelection();
            }

            // 7. 右键菜单交互
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                m_openContextMenu = true;
                ImVec2 mousePos = ImGui::GetMousePos();
                m_contextMenuPos = FCT::Vec2(mousePos.x, mousePos.y);
            }

            renderNodeContextMenu();

            // 8. 外部拖拽投递接收（HTML DOM 元素、资产管理器项）
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HTML_UI_DOM_ELEMENT"))
                {
                    const char* domId = static_cast<const char*>(payload->Data);
                    if (domId && domId[0] != '\0')
                    {
                        handleDomElementDrop(domId);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && path[0] != '\0')
                    {
                        handleAssetDrop(path);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM"))
                {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && path[0] != '\0')
                    {
                        handleAssetDrop(path);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        void BlueprintEditorComposite::renderNodeContextMenu()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            if (m_openContextMenu)
            {
                ImGui::OpenPopup("BlueprintContextMenuPopup");
                m_openContextMenu = false;
            }

            if (ImGui::BeginPopup("BlueprintContextMenuPopup"))
            {
                ImGui::TextDisabled("%s", L("添加蓝图节点 (Add Blueprint Node)", "Add Blueprint Node"));
                ImGui::Separator();

                ImGui::InputTextWithHint("##Filter", L("搜索节点...", "Search nodes..."), m_nodeSearchFilter, sizeof(m_nodeSearchFilter));
                std::string filter = toLowerFilter(m_nodeSearchFilter);

                auto spawnItem = [this, &filter](const std::string& opCode, const char* labelZh, const char* labelEn) {
                    std::string fullLabel = std::string(labelZh) + " (" + opCode + ")";
                    std::string lowerFull = toLowerFilter(fullLabel);
                    if (!filter.empty() && lowerFull.find(filter) == std::string::npos)
                    {
                        return;
                    }

                    auto L = [this](const char* zh, const char* en) -> const char* {
                        return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
                    };

                    if (ImGui::MenuItem(L(labelZh, labelEn)))
                    {
                        GraphSnapshot beforeState = captureGraphSnapshot();
                        EditorNode n = createNodeByOpCode(opCode, m_contextMenuPos);
                        m_nodes.push_back(std::move(n));
                        m_nodesPositioned = false;
                        pushCommand(L("添加节点: ", "Add Node: ") + std::string(labelZh), std::move(beforeState));
                        ImGui::CloseCurrentPopup();
                    }
                };

                if (ImGui::TreeNodeEx(L("事件节点 (Events)###MenuCatEvents", "Event Nodes###MenuCatEvents"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    spawnItem("OnStart", "OnStart 事件入口", "OnStart Event");
                    spawnItem("OnUpdate", "OnUpdate 每帧回调", "OnUpdate Event");
                    spawnItem("OnUIClick", "OnUIClick 元素点击", "OnUIClick Event");
                    spawnItem("OnUIInput", "OnUIInput 元素输入", "OnUIInput Event");
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(L("引擎与应用 (App / Engine)###MenuCatApp", "Engine & App###MenuCatApp"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    spawnItem("GetStartupFilePath", "获取启动文件路径", "Get Startup File Path");
                    spawnItem("OpenModel", "打开并聚焦模型", "Open & Focus Model");
                    spawnItem("PrintLog", "打印控制台日志", "Print Console Log");
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(L("UI 交互控制 (HTML UI)###MenuCatUI", "HTML UI Controls###MenuCatUI"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    spawnItem("SetUIInnerText", "设置 UI 内部文本", "Set UI Inner Text");
                    spawnItem("SetStyleProperty", "设置 UI 样式属性", "Set Style Property");
                    spawnItem("ToggleClass", "切换 UI 类名样式", "Toggle Class");
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(L("ECS 实体组件 (ECS Components)###MenuCatECS", "ECS Components###MenuCatECS"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    spawnItem("GetComponent", "获取实体组件", "Get Component");
                    spawnItem("SetComponent", "设置实体组件", "Set Component");
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(L("流程与数学逻辑 (Flow & Math)###MenuCatFlow", "Flow & Math Logic###MenuCatFlow"), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    spawnItem("Branch", "条件分支 (Branch)", "Branch");
                    spawnItem("Add", "数值加法 (Add)", "Math Add");
                    spawnItem("Sub", "数值减法 (Sub)", "Math Sub");
                    spawnItem("Mul", "数值乘法 (Mul)", "Math Mul");
                    spawnItem("Div", "数值除法 (Div)", "Math Div");
                    spawnItem("Compare", "数值比较 (Compare)", "Compare");
                    spawnItem("LogicAnd", "逻辑与 (And)", "Logic And");
                    spawnItem("LogicOr", "逻辑或 (Or)", "Logic Or");
                    spawnItem("LogicNot", "逻辑非 (Not)", "Logic Not");
                    ImGui::TreePop();
                }

                ImGui::EndPopup();
            }
        }

    } // namespace Blueprint
} // namespace MQEngine
