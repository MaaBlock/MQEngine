#include "ShaderGraph.h"
#include <imgui.h>
#include "../core/Global.h"
#include "../SceneViewer/Inspector.h"
#include "../../Engine/src/manager/SystemManager.h"
#include "../../Engine/src/system/BindedSystem.h"
#include <algorithm>

namespace MQEngine {
    void EditorGraphNode::onInspectorGui() {
        if (!g_engineGlobal.shaderGraphManager) return;
        ShaderGraphNode* engineNode = g_engineGlobal.shaderGraphManager->getNode(engineNodeId);
        if (!engineNode) return;

        ImGui::Text("Node ID: %d", engineNode->id);
        ImGui::Text("Shader Snippet: %s", engineNode->name.c_str());
        ImGui::Text("UUID: %s", engineNode->uuid.c_str());
        ImGui::Separator();
        if (g_engineGlobal.shaderSnippetManager) {
            const Snippet* foundSnippet = g_engineGlobal.shaderSnippetManager->getSnippetByUuid(engineNode->uuid);

            if (foundSnippet) {
                ImGui::Text("Source:");
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
                    redirectionNode(e.uuid);
                });
        }
    }

    ShaderGraph::~ShaderGraph() {
        if (g_engineGlobal.shaderSnippetManager) {
            g_engineGlobal.shaderSnippetManager->unsubscribe<SnippetEvent::Update>(m_snippetUpdateSubId);
        }
        ImNodes::EditorContextFree(m_context);
    }

    int ShaderGraph::getPinId(EditorGraphNode* editorNode, const std::string& pinName, bool isInput, const std::string& type) {
        std::map<std::string, int>& map = isInput ? editorNode->inputPinIds : editorNode->outputPinIds;
        if (map.find(pinName) == map.end()) {
            int newId = getNextId();
            map[pinName] = newId;
            m_pinIdMap[newId] = { editorNode->engineNodeId, pinName, type, isInput };
        }
        return map[pinName];
    }

    const ShaderGraph::PinRef* ShaderGraph::getPinRef(int pinId) const {
        auto it = m_pinIdMap.find(pinId);
        if (it != m_pinIdMap.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void ShaderGraph::onInspectorGui() {
        if (!g_engineGlobal.shaderGraphManager)
            return;

        ShaderGraphConfig config = g_engineGlobal.shaderGraphManager->getConfig();
        bool changed = false;

        // Shader类型
        const char* shaderTypes[] = { "Vertex", "Pixel" };
        int currentType = static_cast<int>(config.type);
        if (ImGui::Combo("Shader Type", &currentType, shaderTypes, IM_ARRAYSIZE(shaderTypes))) {
            config.type = static_cast<ShaderType>(currentType);
            changed = true;
        }

        // 布局
        if (ImGui::CollapsingHeader("Layouts", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& vertexLayouts = g_engineGlobal.shaderGraphManager->getRegisteredVertexLayouts();
            const auto& pixelLayouts = g_engineGlobal.shaderGraphManager->getRegisteredPixelLayouts();

            if (config.type == ShaderType::Vertex) {
                if (ImGui::BeginCombo("Vertex Layout (Input)", config.vertexLayoutName.c_str())) {
                    for (const auto& [name, layout] : vertexLayouts) {
                        bool isSelected = (config.vertexLayoutName == name);
                        if (ImGui::Selectable(name.c_str(), isSelected)) {
                            config.vertexLayoutName = name;
                            changed = true;
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            if (ImGui::BeginCombo("Pixel Layout (Output/Input)", config.pixelLayoutName.c_str())) {
                for (const auto& [name, layout] : pixelLayouts) {
                    bool isSelected = (config.pixelLayoutName == name);
                    if (ImGui::Selectable(name.c_str(), isSelected)) {
                        config.pixelLayoutName = name;
                        changed = true;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // 绑定系统
        if (ImGui::CollapsingHeader("Binded Systems")) {
            if (g_engineGlobal.systemManager) {
                const auto& systemConfigs = g_engineGlobal.systemManager->getConfigs();
                
                if (ImGui::BeginCombo("Add System", "Select to add...")) {
                    for (const auto& [name, sysConfig] : systemConfigs) {
                        // 检查是否为绑定系统
                        if (dynamic_cast<BindedSystem*>(sysConfig.system)) {
                            bool alreadyBound = std::find(config.bindedSystemNames.begin(), config.bindedSystemNames.end(), name) != config.bindedSystemNames.end();
                            if (!alreadyBound) {
                                if (ImGui::Selectable(name.c_str())) {
                                    config.bindedSystemNames.push_back(name);
                                    changed = true;
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // 列出绑定系统，带移除选项和插槽预览
                for (auto it = config.bindedSystemNames.begin(); it != config.bindedSystemNames.end(); ) {
                    std::string sysName = *it;
                    bool removed = false;
                    
                    if (ImGui::TreeNode(sysName.c_str())) {
                        if (ImGui::Button("Remove")) {
                            it = config.bindedSystemNames.erase(it);
                            changed = true;
                            removed = true;
                        } else {
                            // 显示插槽
                            auto sysIt = systemConfigs.find(sysName);
                            if (sysIt != systemConfigs.end()) {
                                BindedSystem* bs = dynamic_cast<BindedSystem*>(sysIt->second.system);
                                if (bs) {
                                    auto uniformSlots = bs->getUniformSlots();
                                    if (!uniformSlots.empty()) {
                                        ImGui::TextDisabled("Uniforms:");
                                        for (const auto& slot : uniformSlots) {
                                            if(ImGui::Selectable(("Uniform: " + std::string(slot.getName())).c_str())) {
                                                 g_engineGlobal.shaderGraphManager->createParameterNode(slot.getName(), "uniform", sysName);
                                            }
                                        }
                                    }
                                    auto samplerSlots = bs->getSamplerSlots();
                                    if (!samplerSlots.empty()) {
                                        ImGui::TextDisabled("Samplers:");
                                        for (const auto& slot : samplerSlots) {
                                            if(ImGui::Selectable(("Sampler: " + std::string(slot.getName())).c_str())) {
                                                 g_engineGlobal.shaderGraphManager->createParameterNode(slot.getName(), "sampler", sysName);
                                            }
                                        }
                                    }
                                    auto textureSlots = bs->getTextureSlots();
                                    if (!textureSlots.empty()) {
                                        ImGui::TextDisabled("Textures:");
                                        for (const auto& slot : textureSlots) {
                                            if(ImGui::Selectable(("Texture: " + slot.name).c_str())) {
                                                 g_engineGlobal.shaderGraphManager->createParameterNode(slot.name, "texture", sysName);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        ImGui::TreePop();
                    } else {
                        // 简单行（如果不展开）
                    }

                    if (!removed) ++it;
                }
            }
        }

        if (changed) {
            g_engineGlobal.shaderGraphManager->setConfig(config);
            
            if (m_editorNodes.count(ShaderGraphManager::INPUT_NODE_ID)) {
                m_editorNodes[ShaderGraphManager::INPUT_NODE_ID]->inputPinIds.clear();
                m_editorNodes[ShaderGraphManager::INPUT_NODE_ID]->outputPinIds.clear();
            }
            if (m_editorNodes.count(ShaderGraphManager::OUTPUT_NODE_ID)) {
                m_editorNodes[ShaderGraphManager::OUTPUT_NODE_ID]->inputPinIds.clear();
                m_editorNodes[ShaderGraphManager::OUTPUT_NODE_ID]->outputPinIds.clear();
            }
        }
    }

    void ShaderGraph::render()
    {
        ImGui::Begin("Shader Graph");
        
        // Settings moved to Inspector
        // if (renderSettings()) ... code moved to onInspectorGui
        ImGui::Separator();

        ImNodes::EditorContextSet(m_context);

        ImNodes::BeginNodeEditor();
        bool isEditorHovered = ImGui::IsWindowHovered();
        
        if (g_engineGlobal.shaderGraphManager) {
            const auto& engineNodes = g_engineGlobal.shaderGraphManager->getNodes();
            for (auto engineNode : engineNodes) {
                // Find or create editor node
                if (m_editorNodes.find(engineNode->id) == m_editorNodes.end()) {
                    auto editorNode = std::make_unique<EditorGraphNode>();
                    editorNode->engineNodeId = engineNode->id;
                    editorNode->visualId = getNextId();
                    m_editorNodes[engineNode->id] = std::move(editorNode);
                    
                    // Set default position for new nodes to avoid ImNodes issues
                    int nodeIndex = m_editorNodes.size();
                    float x = 100.0f + (nodeIndex % 5) * 250.0f;
                    float y = 100.0f + (nodeIndex / 5) * 150.0f;
                    ImNodes::SetNodeScreenSpacePos(m_editorNodes[engineNode->id]->visualId, ImVec2(x, y));
                }
                renderNode(engineNode, m_editorNodes[engineNode->id].get());
            }

            renderLinks();
        }

        ImNodes::MiniMap();
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
            int hoveredNodeId = -1;
            int hoveredLinkId = -1;
            if (ImNodes::IsNodeHovered(&hoveredNodeId))
            {
                m_hoveredNodeId = hoveredNodeId; // This is now Visual ID
                ImGui::OpenPopup("NodeContextMenu");
            }
            else if (ImNodes::IsLinkHovered(&hoveredLinkId))
            {
                m_hoveredLinkId = hoveredLinkId;
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

        void ShaderGraph::addLink(int startPinId, int endPinId) {
            const PinRef* startRef = getPinRef(startPinId);
            const PinRef* endRef = getPinRef(endPinId);
    
            if (startRef && endRef) {
                // 确保一个是输出，一个是输入
                // ImNodes通常处理方向，但我们要验证
                // 实际上startPinId通常是输出，endPinId是输入
                
                if (isTypeCompatible(startRef->type, endRef->type)) {
                    // 检查链接是否已存在于引擎中
                    if (g_engineGlobal.shaderGraphManager) {
                        const auto& links = g_engineGlobal.shaderGraphManager->getLinks();
                        for (const auto& link : links) {
                            if (link.endNodeId == endRef->nodeId && link.endPinName == endRef->pinName) {
                                 spdlog::warn("Input pin already connected");
                                 return;
                            }
                        }
                        
                        int newLinkId = getNextId();
                        g_engineGlobal.shaderGraphManager->createLink(
                            newLinkId,
                            startRef->nodeId, startRef->pinName,
                            endRef->nodeId, endRef->pinName
                        );
                    }
                } else {
                    spdlog::warn("Type mismatch: Cannot connect {} to {}", startRef->type, endRef->type);
                }
            }
        }
    
        void ShaderGraph::renderLinks()
        {
            if (!g_engineGlobal.shaderGraphManager) return;
    
            const auto& links = g_engineGlobal.shaderGraphManager->getLinks();
            for (const auto& link : links)
            {
                // 我们需要将引擎节点/引脚解析为ImNodes引脚ID
                auto startNodeIt = m_editorNodes.find(link.beginNodeId);
                auto endNodeIt = m_editorNodes.find(link.endNodeId);
                
                if (startNodeIt != m_editorNodes.end() && endNodeIt != m_editorNodes.end()) {
                     int startPinId = getPinId(startNodeIt->second.get(), link.beginPinName, false, ""); // Type not needed for lookup       
                     int endPinId = getPinId(endNodeIt->second.get(), link.endPinName, true, "");
    
                     ImNodes::Link(link.id, startPinId, endPinId);
                }
            }
        }
    
        void ShaderGraph::renderNode(ShaderGraphNode* node, EditorGraphNode* editorNode)
        {
            ImNodes::BeginNode(editorNode->visualId);
    
            ImNodes::BeginNodeTitleBar();
            ImGui::TextUnformatted(node->name.c_str());
            ImNodes::EndNodeTitleBar();
    
            for (const auto& pin : node->inputs)
            {
                int pinId = getPinId(editorNode, pin.name, true, pin.type);
                ImNodes::BeginInputAttribute(pinId);
                ImGui::Text("%s (%s)", pin.name.c_str(), pin.type.c_str());
                ImNodes::EndInputAttribute();
            }
            
            for (const auto& pin : node->inouts)
            {
                std::string inName = pin.name + " (in)";
                int inPinId = getPinId(editorNode, inName, true, pin.type);
                ImNodes::BeginInputAttribute(inPinId);
                ImGui::Text("%s", inName.c_str());
                ImNodes::EndInputAttribute();
            }
    
            for (const auto& pin : node->outputs)
            {
                int pinId = getPinId(editorNode, pin.name, false, pin.type);
                ImNodes::BeginOutputAttribute(pinId);
                ImGui::Text("%s (%s)", pin.name.c_str(), pin.type.c_str());
                ImNodes::EndOutputAttribute();
            }
            
            for (const auto& pin : node->inouts)
            {
                std::string outName = pin.name + " (out)";
                int outPinId = getPinId(editorNode, outName, false, pin.type);
                ImNodes::BeginOutputAttribute(outPinId);
                ImGui::Text("%s", outName.c_str());
                ImNodes::EndOutputAttribute();
            }
    
            // 确保节点内容不为空（避免ImNodes/ImGui断言）
            if (node->inputs.empty() && node->outputs.empty() && node->inouts.empty()) {
                ImGui::Dummy(ImVec2(80, 20));
            }
    
            ImNodes::EndNode();
        }
    void ShaderGraph::renderNodeContextMenu() {
        if (ImGui::BeginPopup("NodeContextMenu")) {
            if (ImGui::MenuItem("删除节点")) {
                if (m_hoveredNodeId != -1)
                {
                    // 将VisualID映射到EngineID
                    int engineId = -1;
                    for (const auto& [id, editorNode] : m_editorNodes) {
                        if (editorNode->visualId == m_hoveredNodeId) {
                            engineId = id;
                            break;
                        }
                    }

                    if (engineId != -1) {
                        if (g_engineGlobal.shaderGraphManager) {
                            g_engineGlobal.shaderGraphManager->deleteNode(engineId);
                        }
                        
                        // 检查是否正在删除检视器目标
                        if (g_editorGlobal.inspector->getCurrentObject() == m_editorNodes[engineId].get()) {
                             g_editorGlobal.inspector->setTarget(nullptr);
                        }

                        m_editorNodes.erase(engineId);
                    }
                    
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
                    if (g_engineGlobal.shaderGraphManager) {
                        g_engineGlobal.shaderGraphManager->deleteLink(m_hoveredLinkId);
                    }
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
            int selectedVisualId = 0;
            ImNodes::GetSelectedNodes(&selectedVisualId);
            
            // 查找具有此VisualID的编辑器节点
            for (const auto& [engineId, editorNode] : m_editorNodes) {
                if (editorNode->visualId == selectedVisualId) {
                    g_editorGlobal.inspector->setTarget(editorNode.get());
                    break;
                }
            }

        } else if (isEditorHovered && (
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            g_editorGlobal.inspector->setTarget(this);
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

            // 添加绑定系统的插槽
            if (ImGui::BeginMenu("Binded System Slots")) {
                if (g_engineGlobal.shaderGraphManager && g_engineGlobal.systemManager) {
                    const auto& config = g_engineGlobal.shaderGraphManager->getConfig();
                    const auto& systemConfigs = g_engineGlobal.systemManager->getConfigs();

                    for (const auto& sysName : config.bindedSystemNames) {
                        auto sysIt = systemConfigs.find(sysName);
                        if (sysIt != systemConfigs.end()) {
                            if (ImGui::BeginMenu(sysName.c_str())) {
                                BindedSystem* bs = dynamic_cast<BindedSystem*>(sysIt->second.system);
                                if (bs) {
                                    auto uniformSlots = bs->getUniformSlots();
                                    for(const auto& slot : uniformSlots) {
                                        if(ImGui::Selectable(("Uniform: " + std::string(slot.getName())).c_str())) {
                                            g_engineGlobal.shaderGraphManager->createParameterNode(slot.getName(), "uniform", sysName);
                                        }
                                    }
                                    auto samplerSlots = bs->getSamplerSlots();
                                    for(const auto& slot : samplerSlots) {
                                        if(ImGui::Selectable(("Sampler: " + std::string(slot.getName())).c_str())) {
                                            g_engineGlobal.shaderGraphManager->createParameterNode(slot.getName(), "sampler", sysName);
                                        }
                                    }
                                    auto textureSlots = bs->getTextureSlots();
                                    for(const auto& slot : textureSlots) {
                                        if(ImGui::Selectable(("Texture: " + std::string(slot.name)).c_str())) {
                                            g_engineGlobal.shaderGraphManager->createParameterNode(slot.name, "texture", sysName);
                                        }
                                    }
                                }
                                ImGui::EndMenu();
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
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
        if (!g_engineGlobal.shaderGraphManager) return;
        
        ShaderGraphNode* node = g_engineGlobal.shaderGraphManager->createSnippetNode(snippet->uuid);
        if (!node) return;
        
        int nodeId = node->id;
        
        auto editorNode = std::make_unique<EditorGraphNode>();
        editorNode->engineNodeId = nodeId;
        editorNode->visualId = getNextId();
        
        m_editorNodes[nodeId] = std::move(editorNode);

        // 为新节点设置默认位置以避免ImNodes问题
        ImNodes::SetNodeScreenSpacePos(m_editorNodes[nodeId]->visualId, pos);
    }

    void ShaderGraph::redirectionSlot(ShaderGraphNode* node, const Snippet* newSnippet) {
        // 需要清除编辑器引脚映射，因为名称可能更改或引脚可能已移除
        if (m_editorNodes.find(node->id) != m_editorNodes.end()) {
             // ...
        }

        node->inputs.clear();
        node->outputs.clear();
        node->inouts.clear();
        node->name = newSnippet->name;

        // 输入
        for (const auto& param : newSnippet->inputs) {
            ShaderGraphPin pin;
            pin.name = param.name;
            pin.type = param.type;
            node->inputs.push_back(pin);
        }
        // 输入输出
        for (const auto& param : newSnippet->inouts) {
             ShaderGraphPin pin;
            pin.name = param.name;
            pin.type = param.type;
            node->inouts.push_back(pin);
        }
        // 输出
        for (const auto& param : newSnippet->outputs) {
            ShaderGraphPin pin;
            pin.name = param.name;
            pin.type = param.type;
            node->outputs.push_back(pin);
        }
    }

    void ShaderGraph::redirectionLink() {
        if (!g_engineGlobal.shaderGraphManager) return;
        const auto& nodes = g_engineGlobal.shaderGraphManager->getNodes();
        const auto& links = g_engineGlobal.shaderGraphManager->getLinks();
        
        std::vector<int> linksToRemove;
        
        for (const auto& link : links) {
             // 验证起始节点/引脚和结束节点/引脚
             bool beginFound = false;
             bool endFound = false;
             
             for (const auto& node : nodes) {
                 if (node->id == link.beginNodeId) {
                     // 检查输出和输入输出(out)
                     for(const auto& p : node->outputs) if(p.name == link.beginPinName) beginFound = true;
                     for(const auto& p : node->inouts) if((p.name + " (out)") == link.beginPinName) beginFound = true;
                 }
                 if (node->id == link.endNodeId) {
                     // 检查输入和输入输出(in)
                     for(const auto& p : node->inputs) if(p.name == link.endPinName) endFound = true;
                     for(const auto& p : node->inouts) if((p.name + " (in)") == link.endPinName) endFound = true;
                 }
             }
             
             if (!beginFound || !endFound) {
                 linksToRemove.push_back(link.id);
             }
        }
        
        for(int id : linksToRemove) {
            g_engineGlobal.shaderGraphManager->deleteLink(id);
        }
    }

    void ShaderGraph::redirectionNode(const std::string& uuid) {
        if (!g_engineGlobal.shaderSnippetManager || !g_engineGlobal.shaderGraphManager) return;

        spdlog::info("ShaderGraph::redirectionNode called for UUID: {}", uuid);

        const Snippet* newSnippet = g_engineGlobal.shaderSnippetManager->getSnippetByUuid(uuid);
        if (!newSnippet) return;

        const auto& nodes = g_engineGlobal.shaderGraphManager->getNodes();
        bool nodeFound = false;

        // 我们仔细迭代。
        for (auto node : nodes) {
            bool uuidMatch = (node->uuid == uuid);
            bool nameMatch = (node->name == newSnippet->name);

            if (uuidMatch || nameMatch) {
                nodeFound = true;
                 if (node->uuid != uuid) {
                    node->uuid = uuid;
                }
                redirectionSlot(node, newSnippet);
            }
        }

        if (!nodeFound) {
             // log
        } else {
            redirectionLink();
        }
    }

    void ShaderGraph::save(const std::string& name) {
        // 1. 从ImNodes上下文更新位置
        for (auto& [id, node] : m_editorNodes) {
            node->pos = ImNodes::GetNodeGridSpacePos(node->visualId);
        }
        
        // 2. 保存ImNodes状态
        size_t size = 0;
        const char* state = ImNodes::SaveCurrentEditorStateToIniString(&size);
        if (state && size > 0) {
            m_imguiEditorState = std::string(state, size);
        }

        // 3. 序列化编辑器数据
        std::string path = "res/shadergraph/" + name + ".sgedt";
        if (g_engineGlobal.resourceLoader) {
            g_engineGlobal.resourceLoader->ensureDir("res/shadergraph/");
            std::stringstream ss;
            boost::archive::text_oarchive oa(ss);
            oa << *this;
            Status st = g_engineGlobal.resourceLoader->saveFile(path, ss.str());
            if (!st.ok()) spdlog::error("Failed to save editor data: {}", st.message());
        }

        // 4. 保存引擎数据
        if (g_engineGlobal.shaderGraphManager) {
            g_engineGlobal.shaderGraphManager->setName(name);
            Status st = g_engineGlobal.shaderGraphManager->save(name);
            if (!st.ok()) spdlog::error("Failed to save engine data: {}", st.message());
        }
    }

    void ShaderGraph::load(const std::string& name) {
        // 1. 加载引擎数据
        if (g_engineGlobal.shaderGraphManager) {
            Status st = g_engineGlobal.shaderGraphManager->load(name);
            if (!st.ok()) spdlog::warn("Failed to load engine data: {}", st.message());
        }

        // 2. 加载编辑器数据
        std::string path = "res/shadergraph/" + name + ".sgedt";
        if (g_engineGlobal.resourceLoader) {
            auto contentStatus = g_engineGlobal.resourceLoader->loadFile(path);
            if (contentStatus.ok()) {
                std::stringstream ss(contentStatus.value());
                boost::archive::text_iarchive ia(ss);
                ia >> *this;

                // 3. 恢复ImNodes状态
                if (!m_imguiEditorState.empty()) {
                    ImNodes::LoadCurrentEditorStateFromIniString(m_imguiEditorState.c_str(), m_imguiEditorState.size());
                }
                
                // 4. 恢复位置
                // 注意：ImNodes状态恢复可能会处理位置，但显式恢复更安全（防止状态部分丢失）
                for (auto& [id, node] : m_editorNodes) {
                    ImNodes::SetNodeGridSpacePos(node->visualId, node->pos);
                }
            } else {
                spdlog::warn("Failed to load editor data: {}", contentStatus.status().message());
            }
        }
    }

    void ShaderGraph::clear() {
        m_editorNodes.clear();
        m_pinIdMap.clear();
        m_imguiEditorState.clear();
        m_hoveredNodeId = -1;
        m_hoveredLinkId = -1;
        m_currentId = 10000;
        
        if (g_engineGlobal.shaderGraphManager) {
            g_engineGlobal.shaderGraphManager->clear();
            
            // 重置为默认配置
            ShaderGraphConfig config;
            // 确保默认绑定系统仍然存在（为了方便）
            config.bindedSystemNames = {"CameraSystem", "LightingSystem", "TextureSamplerSystem"};
            
            g_engineGlobal.shaderGraphManager->setConfig(config);
            g_engineGlobal.shaderGraphManager->setName("NewShaderGraph");
        }
    }
}
