#include "GenericInputPopup.h"
#include "imgui.h"
#include <cstring>

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    void GenericInputPopup::open(const std::string& title, const std::string& label, const std::string& defaultValue,
                                 std::function<void(const std::string&)> onConfirm, const std::string& hintText) {
        m_title = title;
        m_label = label;
        m_hintText = hintText;
        m_onConfirm = onConfirm;
        strncpy(m_buffer, defaultValue.c_str(), sizeof(m_buffer));
        m_buffer[sizeof(m_buffer) - 1] = '\0';
        
        m_shouldOpen = true;
        m_isOpen = true;
    }

    void GenericInputPopup::render() {
        if (m_shouldOpen) {
            ImGui::OpenPopup(m_title.c_str());
            m_shouldOpen = false;
        }

        if (ImGui::BeginPopupModal(m_title.c_str(), &m_isOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            
            if (!m_hintText.empty()) {
                ImGui::TextWrapped("%s", m_hintText.c_str());
                ImGui::Spacing();
            }

            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }

            bool enterPressed = ImGui::InputText(m_label.c_str(), m_buffer, sizeof(m_buffer), ImGuiInputTextFlags_EnterReturnsTrue);
            
            ImGui::Separator();

            if (ImGui::Button(TEXT("确认"), ImVec2(120, 0)) || enterPressed) {
                if (m_onConfirm) {
                    m_onConfirm(std::string(m_buffer));
                }
                m_isOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT("取消"), ImVec2(120, 0))) {
                m_isOpen = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

} // MQEngine
