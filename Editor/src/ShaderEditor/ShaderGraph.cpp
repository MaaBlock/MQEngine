#include "ShaderGraph.h"
#include <imgui.h>

namespace MQEngine {
    ShaderGraph::ShaderGraph() {
        m_context = ImNodes::EditorContextCreate();
    }

    ShaderGraph::~ShaderGraph() {
        ImNodes::EditorContextFree(m_context);
    }

    void ShaderGraph::render() {
        ImGui::Begin("Shader Graph");
        ImNodes::EditorContextSet(m_context);
        
        ImNodes::BeginNodeEditor();


        ImNodes::EndNodeEditor();
        ImGui::End();
    }
}
