//
// Created by Administrator on 2025/8/24.
//

#include "SceneManager.h"

#include "../core/Global.h"
#include "../imgui/EditorCameraManager.h"
#define TEXT(str) (const char*)u8##str
using namespace FCT;
namespace MQEngine {
    SceneManager::SceneManager()
    {
    }

    void SceneManager::init()
    {
        m_dataManager = g_editorGlobal.dataManager;
        refreshSceneList();
    }

    std::string SceneManager::getSceneUuid(const std::string& sceneName)
    {
        std::string uuidFile = SAI::Path::join("./Assets/Scenes", sceneName, "scene.uuid");
        auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
        auto uuidOr = ModelAssetSerializer::loadSceneUuid(loader, uuidFile);
        if (uuidOr.ok()) {
            return uuidOr->uuid;
        }
        fout << "读取场景UUID失败: " << uuidOr.status().message() << std::endl;
        return "";
    }


    void SceneManager::newScene(const std::string& sceneName)
    {
        std::string sceneFolder = SAI::Path::join("./Assets/Scenes", sceneName);

        try {
            (void)SAI::FileSystem::createDirectories(sceneFolder);

            static boost::uuids::random_generator uuidGen;
            boost::uuids::uuid sceneUuid = uuidGen();
            std::string uuidString = boost::uuids::to_string(sceneUuid);

            SceneUuidFile sceneUuidData;
            sceneUuidData.uuid = uuidString;

            std::string uuidFile = SAI::Path::join(sceneFolder, "scene.uuid");
            auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
            Status st = ModelAssetSerializer::saveSceneUuid(loader, uuidFile, sceneUuidData);
            if (st.ok()) {
                fout << "创建场景成功: " << sceneFolder << std::endl;
                fout << "场景 UUID: " << uuidString << std::endl;
            } else {
                fout << "无法创建 UUID 文件: " << st.message() << std::endl;
            }

        } catch (const std::exception& e) {
            fout << "创建场景失败: " << e.what() << std::endl;
        }
    }
    void SceneManager::openScene(const std::string& sceneName)
    {
        try {
            std::string uuid = getSceneUuid(sceneName);
            m_dataManager->loadScenePathList();
            m_dataManager->openScene(uuid);
            g_editorGlobal.cameraManager->hookCamera();
        } catch (const std::exception& e) {
            fout << "打开场景时发生错误: " << e.what() << std::endl;
        }
    }


void SceneManager::render()
{
    if (ImGui::Button("创建新场景")) {
        m_showCreateDialog = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        refreshSceneList();
    }

    ImGui::Separator();

    // 显示场景列表
    // ImGui::BeginChild("场景列表", ImVec2(0, 0), true); // ContentBrowser already wraps in a child window
    {
        if (m_sceneList.empty()) {
            ImGui::Text("暂无场景");
            ImGui::Text("点击 '创建新场景' 创建第一个场景");
        } else {
            const float iconSize = 64.0f;
            const float iconSpacing = 10.0f;
            const float itemWidth = iconSize + iconSpacing;

            // 计算每行可以放多少个图标
            float windowWidth = ImGui::GetContentRegionAvail().x;
            int itemsPerRow = std::max(1, (int)(windowWidth / itemWidth));

            for (size_t i = 0; i < m_sceneList.size(); i++) {
                const auto& sceneName = m_sceneList[i];

                // 计算当前项在行中的位置
                if (i > 0 && (i % itemsPerRow) != 0) {
                    ImGui::SameLine();
                }

                ImGui::BeginGroup();
                {
                    ImGui::PushID((int)i);

                    // 图标占位（暂时用按钮替代）
                    bool isSelected = (m_selectedSceneIndex == i);
                    if (isSelected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                    }

                    if (ImGui::Button("场景", ImVec2(iconSize, iconSize))) {
                        m_selectedSceneIndex = i;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastSceneOpenTime).count();
                        if (elapsed > 2)
                        {
                            openScene(sceneName);
                            m_lastSceneOpenTime = now;
                        }
                    }
                    if (isSelected) {
                        ImGui::PopStyleColor();
                    }

                    if (ImGui::BeginPopupContextItem(("SceneContextMenu" + std::to_string(i)).c_str())) {
                        if (ImGui::MenuItem("设置为初始场景")) {
                            std::string uuid = getSceneUuid(sceneName);
                            if (!uuid.empty()) {
                                m_dataManager->setInitialSceneUuid(uuid);
                                fout << "已设置初始场景: " << sceneName << " (UUID: " << uuid << ")" << std::endl;
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("在文件夹中显示")) {
#ifdef _WIN32
                            std::string sceneFolder = SAI::Path::join("./Assets/Scenes", sceneName);
                            std::string command = "explorer \"" + sceneFolder + "\"";
                            system(command.c_str());
#endif
                        }
                        if (ImGui::MenuItem("删除", nullptr, false)) {
                            m_deleteSceneIndex = i;
                            m_showDeleteDialog = true;
                        }
                        ImGui::EndPopup();
                    }

                    // 显示名称（截断过长的名称）
                    std::string displayName = sceneName;
                    if (displayName.length() > 10) {
                        displayName = displayName.substr(0, 10) + "...";
                    }

                    // 居中显示文字
                    float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
                    float centerOffset = (iconSize - textWidth) * 0.5f;
                    if (centerOffset > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
                    ImGui::Text("%s", displayName.c_str());

                    ImGui::PopID();
                }
                ImGui::EndGroup();

                // 悬停提示
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("场景: %s", sceneName.c_str());
                    std::string uuid = getSceneUuid(sceneName);
                    if (!uuid.empty()) {
                        ImGui::Text("UUID: %s", uuid.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
        }
    }
    // ImGui::EndChild();

    // 创建场景对话框
    renderCreateSceneDialog();

    // 删除确认对话框
    renderDeleteSceneDialog();
}

void SceneManager::refreshSceneList()
{
    m_sceneList.clear();
    m_selectedSceneIndex = -1;

    std::string scenesDir = "./Assets/Scenes";
    if (!SAI::FileSystem::exists(scenesDir)) {
        (void)SAI::FileSystem::createDirectories(scenesDir);
        return;
    }

    for (const auto& sceneName : SAI::FileSystem::listSubDirectoryNames(scenesDir)) {
        if (SAI::FileSystem::exists(SAI::Path::join(scenesDir, sceneName, "scene.uuid"))) {
            m_sceneList.push_back(sceneName);
        }
    }
}

void SceneManager::renderCreateSceneDialog()
{
    if (m_showCreateDialog) {
        ImGui::OpenPopup("创建新场景");
    }

    if (ImGui::BeginPopupModal("创建新场景", &m_showCreateDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char sceneName[256] = "";

        ImGui::Text("场景名称:");
        ImGui::InputText("##SceneName", sceneName, sizeof(sceneName));

        if (!m_errorMessage.empty()) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", m_errorMessage.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button("创建")) {
            if (strlen(sceneName) > 0) {
                std::string sceneFolder = SAI::Path::join("./Assets/Scenes", sceneName);
                if (SAI::FileSystem::exists(sceneFolder)) {
                    m_errorMessage = "场景已存在: " + std::string(sceneName);
                } else {
                    newScene(sceneName);
                    refreshSceneList();

                    // 选中新创建的场景
                    for (size_t i = 0; i < m_sceneList.size(); i++) {
                        if (m_sceneList[i] == sceneName) {
                            m_selectedSceneIndex = i;
                            break;
                        }
                    }

                    memset(sceneName, 0, sizeof(sceneName));
                    m_errorMessage.clear();
                    m_showCreateDialog = false;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) {
            memset(sceneName, 0, sizeof(sceneName));
            m_errorMessage.clear();
            m_showCreateDialog = false;
        }

        ImGui::EndPopup();
    }
}

void SceneManager::renderDeleteSceneDialog()
{
    if (m_showDeleteDialog) {
        ImGui::OpenPopup("删除场景");
    }

    if (ImGui::BeginPopupModal("删除场景", &m_showDeleteDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_deleteSceneIndex >= 0 && m_deleteSceneIndex < m_sceneList.size()) {
            ImGui::Text("确定要删除场景 '%s' 吗？", m_sceneList[m_deleteSceneIndex].c_str());
            ImGui::Text("此操作不可撤销！");

            ImGui::Separator();

            if (ImGui::Button("删除")) {
                std::string sceneFolder = SAI::Path::join("./Assets/Scenes", m_sceneList[m_deleteSceneIndex]);
                if (SAI::FileSystem::removeAll(sceneFolder).ok()) {
                    fout << "删除场景成功: " << sceneFolder << std::endl;
                    refreshSceneList();
                } else {
                    fout << "删除场景失败: " << sceneFolder << std::endl;
                }

                m_deleteSceneIndex = -1;
                m_showDeleteDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("取消")) {
                m_deleteSceneIndex = -1;
                m_showDeleteDialog = false;
            }
        }

        ImGui::EndPopup();
    }
}

} // MQEngine