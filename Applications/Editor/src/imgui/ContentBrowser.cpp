#include "ContentBrowser.h"
#include <imgui.h>

namespace MQEngine {

    void ContentBrowser::render() {
        ImGui::Begin("内容浏览器"); 

        if (m_providers.empty()) {
            ImGui::TextDisabled("没有可用的内容提供者");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("ContentBrowserTabs")) {
            for (const auto& provider : m_providers) {
                if (ImGui::BeginTabItem(provider->getName().c_str())) {
                    provider->render();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
