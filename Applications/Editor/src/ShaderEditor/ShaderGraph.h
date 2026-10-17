#ifndef EDITOR_SHADERGRAPH_H
#define EDITOR_SHADERGRAPH_H

#include "../thirdparty/thirdparty.h"
#include "../SceneViewer/InspectorObject.h"
#include "../../Engine/src/manager/ShaderGraphManager.h"
#include <map>
#include <string>

// Boost Serialization
#include <boost/serialization/map.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/unordered_map.hpp> // Assuming for ShaderGraphPin, ShaderGraphNode
#include <imgui.h> // For ImVec2

namespace MQEngine {
    struct Snippet;

    struct EditorGraphNode : public InspectorObject {
        int engineNodeId;
        int visualId = 0;
        ImVec2 pos = ImVec2(0,0); // Add position field

        // These maps store ImNodes pin IDs for connections
        std::map<std::string, int> inputPinIds;
        std::map<std::string, int> outputPinIds;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & engineNodeId;
            ar & visualId;
            ar & pos.x;
            ar & pos.y;
            ar & inputPinIds;
            ar & outputPinIds;
        }

        void onInspectorGui() override;
    };

    class ShaderGraph : public InspectorObject
    {
    public:
        ShaderGraph();
        ~ShaderGraph();

        void render();
        void redirectionNode(const std::string& uuid);

        void save(const std::string& name);
        void load(const std::string& name);
        void clear();

        void onInspectorGui() override;

    private:
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version) {
            ar & m_editorNodes; // unique_ptr serialization requires specific boost headers
            ar & m_pinIdMap; // Need serialization for PinRef
            ar & m_currentId;
            ar & m_imguiEditorState; // ImNodes state as a string
        }

        void dispatchContextMenu(bool& isEditorHovered);
        void renderContextMenu();
        void renderNodeContextMenu();
        void renderLinkContextMenu();
        void inspectNode(bool& isEditorHovered);
        void addNode(const Snippet* snippet, const ImVec2& pos);
        void renderNode(ShaderGraphNode* engineNode, EditorGraphNode* editorNode);
        void renderLinks();
        void addLink(int startPinId, int endPinId);
        bool isTypeCompatible(const std::string& typeA, const std::string& typeB);
        
        void redirectionSlot(ShaderGraphNode* node, const Snippet* newSnippet);
        void redirectionLink();

        int getPinId(EditorGraphNode* editorNode, const std::string& pinName, bool isInput, const std::string& type);
        struct PinRef {
            int nodeId;
            std::string pinName;
            std::string type; // cache type for compatibility check
            bool isInput;

            template<class Archive>
            void serialize(Archive& ar, const unsigned int version) {
                ar & nodeId;
                ar & pinName;
                ar & type;
                ar & isInput;
            }
        };
        const PinRef* getPinRef(int pinId) const;

        ImNodesEditorContext* m_context = nullptr;

        std::map<int, std::unique_ptr<EditorGraphNode>> m_editorNodes; // Map EngineNodeID -> EditorNode
        std::map<int, PinRef> m_pinIdMap; // Map ImNodes PinID -> PinRef

        int m_currentId = 10000;
        int getNextId() { return ++m_currentId; }

        char m_nodeSearchFilter[128] = "";
        ImVec2 m_contextMenuPos;
        int m_hoveredNodeId = -1;
        int m_hoveredLinkId = -1;

        FCT::SubscribeId m_snippetUpdateSubId = 0;
        std::string m_imguiEditorState; // To store ImNodes state
    };
} // namespace MQEngine

#endif // EDITOR_SHADERGRAPH_H