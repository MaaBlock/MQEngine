#include "ShaderGraph.h"
#include <imgui.h>
#include "../core/Global.h"
#include "../SceneViewer/Inspector.h"
#include <algorithm>

namespace MQEngine {
    void ShaderGraphNode::onInspectorGui() {
        ImGui::Text("节点 ID: %d", id);
        ImGui::Text("Shader片段名: %s", name.c_str());
        ImGui::Text("片段UUID: %s", snippetUuid.c_str());
        ImGui::Separator();
        if (g_engineGlobal.shaderSnippetManager) {
            const auto& snippets = g_engineGlobal.shaderSnippetManager->getSnippets();
            const Snippet* foundSnippet = nullptr;
            for (const auto& [key, val] : snippets) {
                if (val.uuid == snippetUuid) {
                    foundSnippet = &val;
                    break;
                }
            }

            if (foundSnippet) {
                ImGui::Text("Shader片段源码:");
                ImGui::InputTextMultiline("##source",
                    const_cast<char*>(foundSnippet->source.c_str()),
                    foundSnippet->source.size() + 1,
                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16),
                    ImGuiInputTextFlags_ReadOnly);
            }
        }
    }

    ShaderGraph::ShaderGraph() {
        m_context = ImNodes::EditorContextCreate();
    }

    ShaderGraph::~ShaderGraph() {
        ImNodes::EditorContextFree(m_context);
    }

    void ShaderGraph::render()
    {
        ImGui::Begin("Shader Graph");
        ImNodes::EditorContextSet(m_context);

        ImNodes::BeginNodeEditor();
        bool isEditorHovered = ImGui::IsWindowHovered();
        for (const auto& node : m_nodes)
        {
            renderNode(node.get());
        }
        renderLinks();
        ImNodes::EndNodeEditor();

        inspectNode(isEditorHovered);

        dispatchContextMenu(isEditorHovered);

        renderContextMenu();
        renderNodeContextMenu();
        renderLinkContextMenu();

        int startPinId, endPinId;
        if (ImNodes::IsLinkCreated(&startPinId, &endPinId)) {
            addLink(startPinId, endPinId);
        }

        ImGui::End();
    }
    void ShaderGraph::dispatchContextMenu(bool& isEditorHovered)
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && isEditorHovered)
        {
            if (ImNodes::IsNodeHovered(&m_hoveredNodeId))
            {
                ImGui::OpenPopup("NodeContextMenu");
            }
            else if (ImNodes::IsLinkHovered(&m_hoveredLinkId))
            {
                ImGui::OpenPopup("LinkContextMenu");
            }
            else
            {
                ImGui::OpenPopup("ShaderGraphContextMenu");
                m_contextMenuPos = ImGui::GetMousePosOnOpeningCurrentPopup();
            }
        }
    }

    bool ShaderGraph::isTypeCompatible(const std::string& typeA, const std::string& typeB) {
        if (typeA == typeB) return true;

        auto strip = [](const std::string& s) {
            auto pos = s.find('<');
            return (pos != std::string::npos) ? s.substr(0, pos) : s;
        };

        return strip(typeA) == strip(typeB);
    }

    void ShaderGraph::addLink(int idA, int idB) {
        const ShaderGraphPin* outPin = nullptr;
        const ShaderGraphPin* inPin = nullptr;
        int outPinId = -1, inPinId = -1;

        for (const auto& node : m_nodes) {
            for (const auto& pin : node->outputs) {
                if (pin.id == idA) { outPin = &pin; outPinId = idA; }
                if (pin.id == idB) { outPin = &pin; outPinId = idB; }
            }
            for (const auto& pin : node->inputs) {
                if (pin.id == idA) { inPin = &pin; inPinId = idA; }
                if (pin.id == idB) { inPin = &pin; inPinId = idB; }
            }
        }

        if (outPin && inPin) {
            if (isTypeCompatible(outPin->type, inPin->type)) {
                for (const auto& link : m_links) {
                    if (link.startPinId == outPinId && link.endPinId == inPinId) return;

                    if (link.endPinId == inPinId) {
                        spdlog::warn("Input pin already connected");
                        return; // Or replace existing?
                    }
                }
                
                ShaderGraphLink link;
                link.id = getNextId();
                link.startPinId = outPinId;
                link.endPinId = inPinId;
                m_links.push_back(link);
            } else {
                spdlog::warn("Type mismatch: Cannot connect {} to {}", outPin->type, inPin->type);
            }
        }
    }

    void ShaderGraph::renderLinks()
    {
        for (const auto& link : m_links)
        {
            ImNodes::Link(link.id, link.startPinId, link.endPinId);
        }
    }
    void ShaderGraph::renderNode(ShaderGraphNode* node)
    {
        ImNodes::BeginNode(node->id);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node->name.c_str());
        ImNodes::EndNodeTitleBar();

        for (const auto& pin : node->inputs)
        {
            ImNodes::BeginInputAttribute(pin.id);
            ImGui::Text("%s (%s)", pin.name.c_str(), pin.type.c_str());
            ImNodes::EndInputAttribute();
        }

        for (const auto& pin : node->outputs)
        {
            ImNodes::BeginOutputAttribute(pin.id);
            ImGui::Text("%s (%s)", pin.name.c_str(), pin.type.c_str());
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }

    void ShaderGraph::renderNodeContextMenu() {
        if (ImGui::BeginPopup("NodeContextMenu")) {
            if (ImGui::MenuItem("删除节点")) {
                if (m_hoveredNodeId != -1)
                {
                    auto itLink = m_links.begin();
                    while (itLink != m_links.end()) {
                        bool connected = false;
                        for (const auto& node : m_nodes) {
                            if (node->id == m_hoveredNodeId) {
                                for (const auto& pin : node->inputs)
                                    if (pin.id == itLink->endPinId) connected = true;
                                for (const auto& pin : node->outputs)
                                    if (pin.id == itLink->startPinId) connected = true;
                            }
                        }
                        
                        if (connected) {
                            itLink = m_links.erase(itLink);
                        } else {
                            ++itLink;
                        }
                    }

                    for (const auto& node : m_nodes)
                    {
                        if (node->id == m_hoveredNodeId) {
                            if (g_editorGlobal.inspector->getCurrentObject() == node.get()) {
                                g_editorGlobal.inspector->setTarget(nullptr);
                            }
                            break;
                        }
                    }


                    auto itNode = std::remove_if(m_nodes.begin(), m_nodes.end(),
                        [this](const auto& n) { return n->id == m_hoveredNodeId; });
                    m_nodes.erase(itNode, m_nodes.end());

                    m_hoveredNodeId = -1;
                }
            }
            ImGui::EndPopup();
        }
    }

    void ShaderGraph::renderLinkContextMenu()
    {
        if (ImGui::BeginPopup("LinkContextMenu"))
        {
            if (ImGui::MenuItem("删除链接"))
            {
                if (m_hoveredLinkId != -1)
                {
                    auto it = std::remove_if(m_links.begin(), m_links.end(),
                                             [this](const ShaderGraphLink& l) { return l.id == m_hoveredLinkId; });
                    m_links.erase(it, m_links.end());
                    m_hoveredLinkId = -1;
                }
            }
            ImGui::EndPopup();
        }
    }
    void ShaderGraph::inspectNode(bool& isEditorHovered)
    {
        int numSelected = ImNodes::NumSelectedNodes();
        if (numSelected == 1) {
            int selectedId = 0;
            ImNodes::GetSelectedNodes(&selectedId);

            auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [selectedId](const auto& n){ return n->id == selectedId; });
            if (it != m_nodes.end()) {
                g_editorGlobal.inspector->setTarget(it->get());
            }
        } else if (isEditorHovered && (
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            g_editorGlobal.inspector->setTarget(nullptr);
        }
    }

    void ShaderGraph::renderContextMenu() {
        if (ImGui::BeginPopup("ShaderGraphContextMenu")) {
            ImGui::Text("添加节点");
            ImGui::Separator();

            ImGui::SetNextItemWidth(200);
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##Search", "搜索", m_nodeSearchFilter, sizeof(m_nodeSearchFilter));

            ImGui::Separator();

            if (g_engineGlobal.shaderSnippetManager) {
                const auto& snippets = g_engineGlobal.shaderSnippetManager->getSnippets();
                
                ImGui::BeginChild("SnippetList", ImVec2(300, 200));
                
                for (const auto& [uuid, snippet] : snippets) {
                    if (m_nodeSearchFilter[0] != '\0') {
                        std::string lowerName = snippet.name;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                        std::string lowerFilter = m_nodeSearchFilter;
                        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                        
                        if (lowerName.find(lowerFilter) == std::string::npos)
                            continue;
                    }

                    if (ImGui::Selectable(snippet.name.c_str())) {
                        addNode(&snippet, m_contextMenuPos);
                        ImGui::CloseCurrentPopup();
                    }
                }
                
                ImGui::EndChild();
            }
            ImGui::EndPopup();
        }
    }

    void ShaderGraph::addNode(const Snippet* snippet, const ImVec2& pos) {
        auto node = std::make_unique<ShaderGraphNode>();
        node->id = getNextId();
        node->name = snippet->name;
        node->snippetUuid = snippet->uuid;

        for (const auto& param : snippet->inputs) {
            ShaderGraphPin pin;
            pin.id = getNextId();
            pin.name = param.name;
            pin.type = param.type;
            node->inputs.push_back(pin);
        }
        for (const auto& param : snippet->inouts) {
            ShaderGraphPin pin;
            pin.id = getNextId();
            pin.name = param.name + " (in)";
            pin.type = param.type;
            node->inputs.push_back(pin);
        }

        // Create output pins
        for (const auto& param : snippet->outputs) {
            ShaderGraphPin pin;
            pin.id = getNextId();
            pin.name = param.name;
            pin.type = param.type;
            node->outputs.push_back(pin);
        }
        for (const auto& param : snippet->inouts) {
            ShaderGraphPin pin;
            pin.id = getNextId();
            pin.name = param.name + " (out)";
            pin.type = param.type;
            node->outputs.push_back(pin);
        }

        ImNodes::SetNodeScreenSpacePos(node->id, pos);
        m_nodes.push_back(std::move(node));
    }
}

