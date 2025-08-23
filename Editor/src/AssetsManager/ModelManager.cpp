//
// Created by Administrator on 2025/8/23.
//

#include "ModelManager.h"

#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine {
    ModelManager::ModelManager(DataManager* dataManager)
    {
        m_dataManager = dataManager;
    }

    void ModelManager::render()
    {
        ImGui::Begin(TEXT("模型资产管理"));
        auto model = m_dataManager->getModelList();
        ImGui::BeginChild(TEXT("文件列表"), ImVec2(350, 0), true);
        {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILES")) {

                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
