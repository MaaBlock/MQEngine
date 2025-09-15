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
        m_dataManager = g_editorGlobal.dataManager;
        refreshSceneList();
    }
    std::string SceneManager::getSceneUuid(const std::string& sceneName)
    {
        std::filesystem::path sceneFolder = std::filesystem::path("./res/scenes") / sceneName;
        std::filesystem::path uuidFile = sceneFolder / "scene.uuid";

        if (std::filesystem::exists(uuidFile)) {
            try {
                std::ifstream uuidIn(uuidFile, std::ios::binary);
                if (uuidIn.is_open()) {
                    SceneUuidFile sceneUuidData;
                    boost::archive::binary_iarchive archive(uuidIn);
                    archive >> sceneUuidData;
                    return sceneUuidData.uuid;
                }
            } catch (const std::exception& e) {
                fout << "读取场景UUID失败: " << e.what() << std::endl;
            }
        }
        return "";
    }


    void SceneManager::newScene(const std::string& sceneName)
    {
        //创建一个文件夹
        std::filesystem::path sceneFolder = std::filesystem::path("./res/scenes") / sceneName;

        try {
            std::filesystem::create_directories(sceneFolder);

            //使用boost生成uuid
            static boost::uuids::random_generator uuidGen;
            boost::uuids::uuid sceneUuid = uuidGen();
            std::string uuidString = boost::uuids::to_string(sceneUuid);

            SceneUuidFile sceneUuidData;
            sceneUuidData.uuid = uuidString;

            //然后在/res/scenes/场景名/底下生成一个uuid文件
            std::filesystem::path uuidFile = sceneFolder / "scene.uuid";
            std::ofstream uuidOut(uuidFile, std::ios::binary);

            if (uuidOut.is_open()) {
                boost::archive::binary_oarchive archive(uuidOut);
                archive << sceneUuidData;
                uuidOut.close();

                fout << "创建场景成功: " << sceneFolder << std::endl;
                fout << "场景 UUID: " << uuidString << std::endl;
            } else {
                fout << "无法创建 UUID 文件: " << uuidFile << std::endl;
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
    ImGui::Begin("场景管理器");

    if (ImGui::Button("创建新场景")) {
        m_showCreateDialog = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        refreshSceneList();
    }

    ImGui::Separator();

    // 显示场景列表
    ImGui::BeginChild("场景列表", ImVec2(0, 0), true);
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
                        if (elapsed > 1)
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
                            std::filesystem::path sceneFolder = std::filesystem::path("./res/scenes") / sceneName;
                            std::string command = "explorer "" + sceneFolder.string() + """;
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
    ImGui::EndChild();

    // 创建场景对话框
    renderCreateSceneDialog();

    // 删除确认对话框
    renderDeleteSceneDialog();

    ImGui::End();
}

void SceneManager::refreshSceneList()
{
    m_sceneList.clear();
    m_selectedSceneIndex = -1;

    std::filesystem::path scenesDir = "./res/scenes";
    if (!std::filesystem::exists(scenesDir)) {
        std::filesystem::create_directories(scenesDir);
        return;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(scenesDir)) {
            if (entry.is_directory()) {
                // 检查是否包含场景UUID文件
                std::filesystem::path uuidFile = entry.path() / "scene.uuid";
                if (std::filesystem::exists(uuidFile)) {
                    std::string sceneName = entry.path().filename().string();
                    m_sceneList.push_back(sceneName);
                }
            }
        }

        // 按名字排序
        std::sort(m_sceneList.begin(), m_sceneList.end());

    } catch (const std::filesystem::filesystem_error& e) {
        fout << "扫描场景目录失败: " << e.what() << std::endl;
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
                // 检查场景是否已存在
                std::filesystem::path sceneFolder = std::filesystem::path("./res/scenes") / sceneName;
                if (std::filesystem::exists(sceneFolder)) {
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
                // 删除场景文件夹
                std::filesystem::path sceneFolder = std::filesystem::path("./res/scenes") / m_sceneList[m_deleteSceneIndex];
                try {
                    std::filesystem::remove_all(sceneFolder);
                    fout << "删除场景成功: " << sceneFolder << std::endl;
                    refreshSceneList();
                } catch (const std::exception& e) {
                    fout << "删除场景失败: " << e.what() << std::endl;
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