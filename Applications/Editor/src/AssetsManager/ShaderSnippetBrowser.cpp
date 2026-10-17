#include "ShaderSnippetBrowser.h"
#include <imgui.h>
#include <cstdlib>
#include <fstream>
#include <algorithm> // For std::sort, etc.

namespace MQEngine {

    void ShaderSnippetBrowser::init() {
        // ShaderSnippetManager handles directory creation/management
        if (g_engineGlobal.shaderSnippetManager) {
            g_engineGlobal.shaderSnippetManager->loadSnippetFromResource(); // Initial load
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

        if (g_engineGlobal.shaderSnippetManager) {
            const auto& snippets = g_engineGlobal.shaderSnippetManager->getSnippets();
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
        std::filesystem::path snippetsDir = "./res/snippets";
        std::filesystem::path newPath = snippetsDir / (name + ".hlsl");
        if (std::filesystem::exists(newPath)) {
            // TODO: Show error in ImGui
            spdlog::warn("Shader snippet '{}' already exists.", name);
            return;
        }

        std::ofstream outfile(newPath);
        if (outfile.is_open()) {
            outfile << "void main(in float3 pos, out float4 color) {\n"
                    << "    color = float4(1.0, 1.0, 1.0, 1.0);\n"
                    << "}\n";
            outfile.close();
            spdlog::info("Created new shader snippet file: '{}'", newPath.string());
            
            if (g_engineGlobal.shaderSnippetManager) {
                Status status = g_engineGlobal.shaderSnippetManager->loadSnippetFromResource(); // Force refresh engine's manager
                if (!status.ok()) {
                    spdlog::error("Failed to reload snippets after creation: {}", status.message());
                } else {
                    spdlog::info("Successfully reloaded snippets.");
                }
            }
        } else {
            spdlog::error("Failed to create shader snippet file: '{}'", newPath.string());
        }
    }

    void ShaderSnippetBrowser::openInVsCode(const std::filesystem::path& path) {
        // Try to open with code command
        // Note: This relies on 'code' being in PATH
        std::string cmd = "code \"" + path.string() + "\"";
        // Use standard system call. On Windows this works if VSCode is installed and in PATH.
        // The user explicitly asked for "open in vscode".
        int ret = system(cmd.c_str());
        if (ret != 0) {
            // Fallback or error logging could go here
            // spdlog::warn("Failed to open VSCode with command: {}", cmd);
        }
    }
}
