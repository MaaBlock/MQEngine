//
// Created by Administrator on 2025/8/25.
//

#include "SceneEntityViewer.h"

#include "../core/Global.h"

#define TEXT(str) (const char*)u8##str
namespace MQEngine {
    SceneEntityViewer::SceneEntityViewer()
    {
        m_dataManager = g_global.dataManager;
    }

    void SceneEntityViewer::render()
    {
        ImGui::Begin(TEXT("场景实体查看器"));

        std::string uuid = m_dataManager->getCurrentSceneUuid();
        if (uuid.size())
        {
            try {
                auto scene = m_dataManager->getCurrentScene();
                if (scene) {
                    std::string sceneName = scene->getName();
                    ImGui::Text(TEXT("当前场景: %s"), sceneName.c_str());
                    ImGui::Text(TEXT("UUID: %s"), uuid.c_str());
                } else {
                    ImGui::Text(TEXT("场景加载失败"));
                }
            } catch (const std::exception& e) {
                ImGui::Text(TEXT("获取场景信息失败: %s"), e.what());
            }
        } else {
            ImGui::Text(TEXT("未选择场景"));
        }

        ImGui::End();
    }
} // MQEngine