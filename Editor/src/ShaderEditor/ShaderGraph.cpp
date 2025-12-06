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
            const Snippet* foundSnippet = g_engineGlobal.shaderSnippetManager->getSnippetByUuid(snippetUuid);

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
        if (g_engineGlobal.shaderSnippetManager) {
            m_snippetUpdateSubId = g_engineGlobal.shaderSnippetManager->subscribe<SnippetEvent::Update>(
                [this](const SnippetEvent::Update& e) {
                    onSnippetUpdated(e.uuid);
                });
        }
    }

    ShaderGraph::~ShaderGraph() {
        if (g_engineGlobal.shaderSnippetManager) {
            g_engineGlobal.shaderSnippetManager->unsubscribe<SnippetEvent::Update>(m_snippetUpdateSubId);
        }
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
                
                for (const auto& snippet : snippets) {
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

    void ShaderGraph::onSnippetUpdated(const std::string& uuid) {
        if (!g_engineGlobal.shaderSnippetManager) return;
        
        spdlog::info("ShaderGraph::onSnippetUpdated called for UUID: {}", uuid);

        const Snippet* newSnippet = g_engineGlobal.shaderSnippetManager->getSnippetByUuid(uuid);
        if (!newSnippet) {
            spdlog::warn("ShaderGraph: Snippet not found for UUID: {}", uuid);
            return;
        }
        spdlog::info("  Updated Snippet Name: '{}'", newSnippet->name);

        spdlog::info("  Current Nodes in Graph: {}", m_nodes.size());
        for (const auto& node : m_nodes) {
            spdlog::info("    - Node ID: {}, Name: '{}', UUID: '{}'", node->id, node->name, node->snippetUuid);
        }

        bool nodeFound = false;
        for (auto& node : m_nodes) {
            // Match by UUID OR Name (to handle cases where UUID might have regenerated but file/name is same)
            bool uuidMatch = (node->snippetUuid == uuid);
            bool nameMatch = (node->name == newSnippet->name);

            if (uuidMatch || nameMatch) {
                nodeFound = true;
                spdlog::info("  MATCHED Node ID: {} (UUID Match: {}, Name Match: {})", node->id, uuidMatch, nameMatch);
                spdlog::info("  Updating node '{}' (ID: {}). Old Inputs: {}, Old Outputs: {}", 
                    node->name, node->id, node->inputs.size(), node->outputs.size());

                // Sync UUID in case it changed (e.g. meta file regeneration)
                if (node->snippetUuid != uuid) {
                    spdlog::info("  ShaderGraph: Updating node UUID from {} to {}", node->snippetUuid, uuid);
                    node->snippetUuid = uuid;
                }

                // Keep track of old pins to try and preserve links
                std::vector<ShaderGraphPin> oldInputs = node->inputs;
                std::vector<ShaderGraphPin> oldOutputs = node->outputs;

                node->inputs.clear();
                node->outputs.clear();
                node->name = newSnippet->name;

                // Re-create inputs
                for (const auto& param : newSnippet->inputs) {
                    ShaderGraphPin pin;
                    pin.id = getNextId(); 
                    pin.name = param.name;
                    pin.type = param.type;
                    
                    auto it = std::find_if(oldInputs.begin(), oldInputs.end(), [&](const ShaderGraphPin& p) {
                        return p.name == pin.name && p.type == pin.type;
                    });
                    if (it != oldInputs.end()) {
                        pin.id = it->id;
                    }
                    
                    node->inputs.push_back(pin);
                }
                
                // Re-create inouts (as inputs)
                for (const auto& param : newSnippet->inouts) {
                    ShaderGraphPin pin;
                    pin.id = getNextId();
                    pin.name = param.name + " (in)";
                    pin.type = param.type;

                    auto it = std::find_if(oldInputs.begin(), oldInputs.end(), [&](const ShaderGraphPin& p) {
                        // Match old input name (e.g. "A" for in, or "A (in)" for inout)
                        // Note: If param was previously "inout", name in oldInputs was "A (in)".
                        // If param was "in", name was "A".
                        // New logic: name is "A (in)".
                        return p.name == pin.name && p.type == pin.type;
                    });
                    if (it != oldInputs.end()) {
                        pin.id = it->id;
                    }

                    node->inputs.push_back(pin);
                }

                // Re-create outputs
                for (const auto& param : newSnippet->outputs) {
                    ShaderGraphPin pin;
                    pin.id = getNextId();
                    pin.name = param.name;
                    pin.type = param.type;

                    auto it = std::find_if(oldOutputs.begin(), oldOutputs.end(), [&](const ShaderGraphPin& p) {
                        return p.name == pin.name && p.type == pin.type;
                    });
                    if (it != oldOutputs.end()) {
                        pin.id = it->id;
                    }

                    node->outputs.push_back(pin);
                }

                // Re-create inouts (as outputs)
                for (const auto& param : newSnippet->inouts) {
                    ShaderGraphPin pin;
                    pin.id = getNextId();
                    pin.name = param.name + " (out)";
                    pin.type = param.type;

                    auto it = std::find_if(oldOutputs.begin(), oldOutputs.end(), [&](const ShaderGraphPin& p) {
                        return p.name == pin.name && p.type == pin.type;
                    });
                    if (it != oldOutputs.end()) {
                        pin.id = it->id;
                    }

                    node->outputs.push_back(pin);
                }
                
                spdlog::info("  Node updated. New Inputs: {}, New Outputs: {}", 
                    node->inputs.size(), node->outputs.size());
            }
        }
        
        if (!nodeFound) {
            spdlog::info("ShaderGraph: No nodes found for snippet '{}' (UUID: {})", newSnippet->name, uuid);
        } else {
            // Cleanup invalid links
            // Collect all valid pin IDs
            std::vector<int> validPinIds;
            for (const auto& node : m_nodes) {
                for (const auto& pin : node->inputs) validPinIds.push_back(pin.id);
                for (const auto& pin : node->outputs) validPinIds.push_back(pin.id);
            }
            std::sort(validPinIds.begin(), validPinIds.end());

            size_t oldLinkCount = m_links.size();
            m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [&](const ShaderGraphLink& link) {
                bool startValid = std::binary_search(validPinIds.begin(), validPinIds.end(), link.startPinId);
                bool endValid = std::binary_search(validPinIds.begin(), validPinIds.end(), link.endPinId);
                if (!startValid || !endValid) {
                    spdlog::info("ShaderGraph: Removing invalid link ID {} (Pins: {} -> {})", link.id, link.startPinId, link.endPinId);
                    return true;
                }
                return false;
            }), m_links.end());
            
            if (m_links.size() != oldLinkCount) {
                spdlog::info("ShaderGraph: Removed {} invalid links.", oldLinkCount - m_links.size());
            }
        }
    }
}

