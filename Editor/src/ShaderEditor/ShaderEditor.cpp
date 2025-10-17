#include "ShaderEditor.h"
#include "imgui.h"
#include "tinyfiledialogs.h"

ShaderEditor::ShaderEditor()
{
    m_textEditor.SetLanguage(TextEditor::Language::Hlsl());
}

void ShaderEditor::render()
{
    ImGui::Begin("着色器编辑器", nullptr, ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("文件"))
        {
            if (ImGui::MenuItem("打开"))
            {
                char const * lFilterPatterns[2] = { "*.vert", "*.frag" };
                char const * lTheOpenFileName = tinyfd_openFileDialog(
                    "打开一个着色器文件",
                    "",
                    2,
                    lFilterPatterns,
                    nullptr,
                    0);

                if (lTheOpenFileName)
                {

                }
            }
            if (ImGui::MenuItem("保存"))
            {

            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    m_textEditor.Render("文本编辑器");

    ImGui::End();
}
