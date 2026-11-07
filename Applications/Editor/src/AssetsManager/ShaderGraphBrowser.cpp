#include "ShaderGraphBrowser.h"
#include "../ShaderEditor/ShaderGraph.h"
#include "../core/Global.h"
#include "../SceneViewer/Inspector.h"

namespace MQEngine {
    ShaderGraphBrowser::ShaderGraphBrowser(ShaderGraph* shaderGraph) : m_shaderGraph(shaderGraph) {
    }

    void ShaderGraphBrowser::init() {
        (void)SAI::FileSystem::ensureDirectory(m_rootPath);
    }

    void ShaderGraphBrowser::render() {
        if (ImGui::Button("New Graph")) {
            ImGui::OpenPopup("CreateGraphPopup");
        }

        if (ImGui::BeginPopup("CreateGraphPopup")) {
            ImGui::InputText("Name", m_newGraphName, sizeof(m_newGraphName));
            if (ImGui::Button("Create")) {
                createNewGraph(m_newGraphName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        renderGraphList();
    }

    void ShaderGraphBrowser::renderGraphList() {
        for (const auto& name : SAI::FileSystem::listFileStems(m_rootPath, ".sg")) {
            bool isSelected = (m_selectedGraph == name);
            if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                m_selectedGraph = name;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    loadGraph(name);
                }
            }
        }
    }

    void ShaderGraphBrowser::createNewGraph(const std::string& name) {
        if (m_shaderGraph) {
            m_shaderGraph->clear();
            m_shaderGraph->save(name);
        }
    }

    void ShaderGraphBrowser::loadGraph(const std::string& name) {
        if (m_shaderGraph) {
            m_shaderGraph->load(name);
            if (g_editorGlobal.inspector) {
                g_editorGlobal.inspector->setTarget(m_shaderGraph);
            }
        }
    }
}