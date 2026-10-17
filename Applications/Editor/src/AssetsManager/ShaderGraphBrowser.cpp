#include "ShaderGraphBrowser.h"
#include "../ShaderEditor/ShaderGraph.h"
#include <imgui.h>
#include <filesystem>
#include "../core/Global.h"
#include "../SceneViewer/Inspector.h"

namespace MQEngine {
    ShaderGraphBrowser::ShaderGraphBrowser(ShaderGraph* shaderGraph) : m_shaderGraph(shaderGraph) {
    }

    void ShaderGraphBrowser::init() {
        if (!std::filesystem::exists(m_rootPath)) {
            std::filesystem::create_directories(m_rootPath);
        }
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
        if (!std::filesystem::exists(m_rootPath)) return;

        for (const auto& entry : std::filesystem::directory_iterator(m_rootPath)) {
            if (entry.path().extension() == ".sg") {
                std::string name = entry.path().stem().string();
                bool isSelected = (m_selectedGraph == name);
                if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_selectedGraph = name;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        loadGraph(name);
                    }
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