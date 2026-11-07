#include "ShaderSnippetBrowser.h"

namespace MQEngine {

    void ShaderSnippetBrowser::init() {
        // ShaderSnippetManager handles directory creation/management
        if (auto* engine = Runtime::Engine::getInstance()) {
            engine->effect->shaderSnippetManager->loadSnippetFromResource(); // Initial load
        }
    }

    void ShaderSnippetBrowser::render() {
        if (ImGui::Button("新建片段")) {
            ImGui::OpenPopup("CreateSnippetPopup");
            memset(m_newSnippetName, 0, sizeof(m_newSnippetName));
        }

        if (ImGui::BeginPopupModal("CreateSnippetPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("请输入片段名称:");
            ImGui::InputText("##SnippetName", m_newSnippetName, sizeof(m_newSnippetName));

            if (ImGui::Button("创建", ImVec2(120, 0))) {
                if (strlen(m_newSnippetName) > 0) {
                    createSnippet(m_newSnippetName);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // Simple list view
        ImGui::Columns(2, "SnippetBrowserColumns");
        
        // Headers
        ImGui::Text("名称"); ImGui::NextColumn();
        ImGui::Text("类型"); ImGui::NextColumn();
        ImGui::Separator();

        if (auto* engine = Runtime::Engine::getInstance()) {
            const auto& snippets = engine->effect->shaderSnippetManager->getSnippets();
            // Iterate boost::multi_index_container directly
            for (const auto& snippet : snippets) {
                // Only display non-embedded snippets (file-backed ones)
                if (!snippet.isEmbed) {
                    if (ImGui::Selectable(snippet.name.c_str(), m_selectedSnippet == snippet.name, ImGuiSelectableFlags_SpanAllColumns)) {
                        m_selectedSnippet = snippet.name;
                    }

                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("用 VSCode 打开")) {
                            openInVsCode(snippet.filePath);
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::NextColumn();
                    ImGui::Text("着色器片段");
                    ImGui::NextColumn();
                }
            }
        }
        ImGui::Columns(1);
    }

    void ShaderSnippetBrowser::createSnippet(const std::string& name) {
        std::string newPath = SAI::Path::join("./Assets/Snippets", name + ".hlsl");
        if (SAI::FileSystem::exists(newPath)) {
            spdlog::warn("Shader snippet '{}' already exists.", name);
            return;
        }

        static constexpr const char* kDefaultSnippetTemplate =
            "void main(in float3 pos, out float4 color) {\n"
            "    color = float4(1.0, 1.0, 1.0, 1.0);\n"
            "}\n";

        Status writeStatus = SAI::FileSystem::writeTextFile(newPath, kDefaultSnippetTemplate);
        if (writeStatus.ok()) {
            spdlog::info("Created new shader snippet file: '{}'", newPath);
            
            if (auto* engine = Runtime::Engine::getInstance()) {
                Status status = engine->effect->shaderSnippetManager->loadSnippetFromResource();
                if (!status.ok()) {
                    spdlog::error("Failed to reload snippets after creation: {}", status.message());
                } else {
                    spdlog::info("Successfully reloaded snippets.");
                }
            }
        } else {
            spdlog::error("Failed to create shader snippet file '{}': {}", newPath, writeStatus.message());
        }
    }

    void ShaderSnippetBrowser::openInVsCode(const std::string& path) {
        std::string cmd = "code \"" + path + "\"";
        (void)system(cmd.c_str());
    }
}
