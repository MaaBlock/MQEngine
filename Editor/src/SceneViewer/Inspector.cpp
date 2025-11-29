#include "Inspector.h"
#include <imgui.h>

namespace MQEngine {
    Inspector::Inspector() {
    }

    void Inspector::render() {
        ImGui::Begin("细节"); 
        
        if (m_currentObject) {
            m_currentObject->onInspectorGui();
        } else {
            ImGui::TextDisabled("选择一个对象来查看细节。");
        }
        
        ImGui::End();
    }
}
