#include "ContentBrowser.h"
#include <imgui.h>

namespace MQEngine {

    void ContentBrowser::render() {
        ImGui::Begin("内容浏览器"); 

        // Left panel: List
        ImGui::BeginChild("ContentList", ImVec2(150, 0), true);
        for (const auto& provider : m_providers) {
            std::string name = provider->getName();
            if (ImGui::Selectable(name.c_str(), m_selectedContentName == name)) {
                m_selectedContentName = name;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel: View
        ImGui::BeginGroup();
        
        IContentProvider* currentProvider = nullptr;
        for (const auto& provider : m_providers) {
            if (provider->getName() == m_selectedContentName) {
                currentProvider = provider.get();
                break;
            }
        }

        if (currentProvider) {
            // Render the content view inside a child window to handle scrolling/clipping separately if needed
            ImGui::BeginChild("ContentView", ImVec2(0, 0), false); 
            currentProvider->render();
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("请在左侧选择内容进行查看"); 
        }
        ImGui::EndGroup();

        ImGui::End();
    }
}
