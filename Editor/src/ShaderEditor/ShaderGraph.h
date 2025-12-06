#ifndef EDITOR_SHADERGRAPH_H
#define EDITOR_SHADERGRAPH_H

#include "../thirdparty/thirdparty.h"
#include "../SceneViewer/InspectorObject.h"

namespace MQEngine {
    struct Snippet;

    struct ShaderGraphPin {
        int id;
        std::string name;
        std::string type;
    };

    struct ShaderGraphNode : public InspectorObject {
        int id;
        std::string name;
        std::string snippetUuid;
        std::vector<ShaderGraphPin> inputs;
        std::vector<ShaderGraphPin> outputs;

        void onInspectorGui() override;
    };

    struct ShaderGraphLink {
        int id;
        int startPinId;
        int endPinId;
    };


    class ShaderGraph
    {
    public:
        ShaderGraph();
        ~ShaderGraph();

        void render();
        void onSnippetUpdated(const std::string& uuid);

    private:
        void dispatchContextMenu(bool& isEditorHovered);
        void renderContextMenu();
        void renderNodeContextMenu();
        void renderLinkContextMenu();
        void inspectNode(bool& isEditorHovered);
        void addNode(const Snippet* snippet, const ImVec2& pos);
        void renderNode(ShaderGraphNode* node);
        void renderLinks();
        void addLink(int startPinId, int endPinId);
        bool isTypeCompatible(const std::string& typeA, const std::string& typeB);

        ImNodesEditorContext* m_context = nullptr;

        std::vector<std::unique_ptr<ShaderGraphNode>> m_nodes;
        std::vector<ShaderGraphLink> m_links;

        int m_currentId = 0;
        int getNextId() { return ++m_currentId; }

        char m_nodeSearchFilter[128] = "";
        ImVec2 m_contextMenuPos;
        int m_hoveredNodeId = -1;
        int m_hoveredLinkId = -1;

        FCT::SubscribeId m_snippetUpdateSubId = 0;
    };
} // namespace MQEngine

#endif // EDITOR_SHADERGRAPH_H