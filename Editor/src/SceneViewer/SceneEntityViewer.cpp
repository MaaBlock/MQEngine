//
// Created by Administrator on 2025/8/25.
//

#include "SceneEntityViewer.h"

#include "../core/Global.h"
#include "../data/NameTag.h"
#include "../Thirdparty/thirdparty.h"

#define TEXT(str) (const char*)u8##str
namespace MQEngine {
    SceneEntityViewer::SceneEntityViewer()
    {
        m_dataManager = g_editorGlobal.dataManager;
    }
    void SceneEntityViewer::showCreateEntityDialog(const std::string& targetTrunk, bool isGlobal)
    {
        m_showCreateEntityDialog = true;
        m_targetTrunkName = targetTrunk;
        m_createInGlobal = isGlobal;

        strcpy_s(m_newEntityName, sizeof(m_newEntityName), "新实体");
    }
     void SceneEntityViewer::renderCreateEntityDialog(Scene* scene)
    {
        if (!m_showCreateEntityDialog) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("创建实体", &m_showCreateEntityDialog, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoResize)) {

            // 显示创建位置信息
            ImGui::Text("创建位置: %s",
                m_createInGlobal ? "场景全局" : m_targetTrunkName.c_str());
            ImGui::Separator();

            // 基本属性设置
            ImGui::Text("基本属性");
            ImGui::InputText("实体名称", m_newEntityName, sizeof(m_newEntityName));

            ImGui::Separator();

            // 预留扩展区域
            ImGui::Text("组件设置");
            ImGui::BeginChild("ComponentSettings", ImVec2(0, 150), true);
            {
                // 这里以后可以添加各种组件的设置选项
                ImGui::Text("暂无可配置组件");
                ImGui::Text("(未来可在此处添加Transform、Renderer等组件配置)");
            }
            ImGui::EndChild();

            ImGui::Separator();

            // 按钮区域
            float buttonWidth = 80.0f;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float totalWidth = buttonWidth * 2 + spacing;
            float startX = (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

            if (ImGui::Button("创建", ImVec2(buttonWidth, 0))) {
                // 验证名称不为空
                std::string entityName = std::string(m_newEntityName);
                if (!entityName.empty()) {
                    createEntity(scene, entityName, m_targetTrunkName, m_createInGlobal);
                    m_showCreateEntityDialog = false;
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("取消", ImVec2(buttonWidth, 0))) {
                m_showCreateEntityDialog = false;
            }
        }
        ImGui::End();
    }
    void SceneEntityViewer::createEntity(Scene* scene, const std::string& name, const std::string& trunkName, bool isGlobal)
    {
        try {
            entt::entity newEntity;

            if (isGlobal) {
                // 在场景全局注册表中创建实体
                auto& registry = scene->getRegistry();
                newEntity = registry.create();

                // 添加名称组件
                registry.emplace<NameTag>(newEntity, name);
            } else {
                // 在指定的trunk中创建实体
                SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
                if (trunk) {
                    auto& registry = trunk->getRegistry();
                    newEntity = registry.create();

                    // 添加名称组件
                    registry.emplace<NameTag>(newEntity, name);
                } else {
                    // trunk未加载，可能需要先加载或给出错误提示
                    return;
                }
            }

            // 这里以后可以添加其他默认组件
            // 比如 Transform 组件等

        } catch (const std::exception& e) {
            // 错误处理，可以显示错误消息
            // 或者记录日志
        }
    }


void SceneEntityViewer::renderGloabaEntityList(Scene* scene)
    {
        ImGui::BeginGroup();

        auto& registry = scene->getRegistry();
        auto view = registry.view<entt::entity>();

        bool hasEntities = false;
        for (auto entity : view) {
            hasEntities = true;
            std::string displayName;

            if (auto nameTag = registry.try_get<NameTag>(entity)) {
                displayName = nameTag->name;
            } else {
                displayName = "entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            // 检查是否为选中的实体
            auto& selectedEntity = g_editorGlobal.selectedEntity;
            bool isSelected = (selectedEntity.entity == entity && selectedEntity.isGlobal);
            
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                selectEntity(entity, "", true);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string meshName = dragData.substr(separatorPos + 1);
                        addStaticMeshComponent(entity, modelUuid, meshName, true);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
                    std::string functionName = static_cast<const char*>(payload->Data);
                    addScriptComponent(entity, functionName, true);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string texturePath = dragData.substr(separatorPos + 1);
                        addDiffuseTextureComponent(entity, modelUuid, texturePath, true);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (!hasEntities) {
            ImGui::TextDisabled("(空)");
        }

        ImGui::Dummy(ImVec2(0, 20));

        ImGui::EndGroup();

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("GlobalEntityContextMenu");
        }

        if (ImGui::BeginPopup("GlobalEntityContextMenu")) {
            if (ImGui::MenuItem("创建实体")) {
                showCreateEntityDialog("", true);
            }
            ImGui::EndPopup();
        }
    }

void SceneEntityViewer::renderTrunkEntityList(Scene* scene, std::string trunkName)
    {
        ImGui::BeginGroup();

        SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
        auto& registry = trunk->getRegistry();
        auto view = registry.view<entt::entity>();

        bool hasEntities = false;
        for (auto entity : view) {
            hasEntities = true;
            std::string displayName;

            if (auto nameTag = registry.try_get<NameTag>(entity)) {
                displayName = nameTag->name;
            } else {
                displayName = "entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            bool isSelected = (selectedEntity.entity == entity && !selectedEntity.isGlobal && selectedEntity.trunkName == trunkName);
            
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                selectEntity(entity, trunkName, false);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string meshName = dragData.substr(separatorPos + 1);

                        addStaticMeshComponent(entity, modelUuid, meshName, false, trunkName);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
                    std::string functionName = static_cast<const char*>(payload->Data);
                    addScriptComponent(entity, functionName, false, trunkName);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string texturePath = dragData.substr(separatorPos + 1);
                        addDiffuseTextureComponent(entity, modelUuid, texturePath, false, trunkName);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (!hasEntities) {
            ImGui::TextDisabled("(空)");
        }

        ImGui::Dummy(ImVec2(0, 20));

        ImGui::EndGroup();

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            std::string popupId = "TrunkEntityContextMenu_" + trunkName;
            ImGui::OpenPopup(popupId.c_str());
        }

        std::string contextMenuId = "TrunkEntityContextMenu_" + trunkName;
        if (ImGui::BeginPopup(contextMenuId.c_str())) {
            if (ImGui::MenuItem("创建实体")) {
                showCreateEntityDialog(trunkName, false);
            }
            ImGui::EndPopup();
        }
    }
    void SceneEntityViewer::renderSceneEntityList(Scene* scene)
    {
        std::string sceneName = scene->getName();
        ImGui::Text("当前场景: %s", sceneName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("保存场景")) {
            scene->save();
        }
        ImGui::Separator();
        ImGui::Text("实体列表:");
        if (ImGui::TreeNode("场景全局")) {
            renderGloabaEntityList(scene);
            ImGui::TreePop();
        }
        for (auto& trunk : scene->getTrunkList())
        {
            bool isLoaded = scene->isLoad(trunk);
            if (!isLoaded) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色
            }

            if (ImGui::TreeNode("%s", trunk.c_str())) {
                if (scene->isLoad(trunk))
                {
                    renderTrunkEntityList(scene, trunk);
                    ImGui::TreePop();
                } else
                {
                    ImGui::TreePop();
                }
            }
            if (!isLoaded) {
                ImGui::PopStyleColor();
            }
        }
    }


    void SceneEntityViewer::render()
    {
        ImGui::Begin("场景实体查看器");

        std::string uuid = m_dataManager->getCurrentSceneUuid();
        if (uuid.size())
        {
            try {
                auto scene = m_dataManager->getCurrentScene();
                if (scene) {
                    renderSceneEntityList(scene);
                    renderCreateEntityDialog(scene);
                } else {
                    ImGui::Text("场景加载失败");
                }
            } catch (const std::exception& e) {
                ImGui::Text("获取场景信息失败: %s", e.what());
            }
        } else {
            ImGui::Text("未选择场景");
        }

        ImGui::End();
    }
    
    void SceneEntityViewer::selectEntity(entt::entity entity, const std::string& trunkName, bool isGlobal) {
        auto& selectedEntity = g_editorGlobal.selectedEntity;
        selectedEntity.entity = entity;
        selectedEntity.trunkName = trunkName;
        selectedEntity.isGlobal = isGlobal;
        selectedEntity.scene = m_dataManager->getCurrentScene();
    }
    
    void SceneEntityViewer::addStaticMeshComponent(entt::entity entity, const std::string& modelUuid, const std::string& meshName, bool isGlobal, const std::string& trunkName) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene) return;
        
        entt::registry* registry;
        if (isGlobal) {
            registry = &scene->getRegistry();
        } else {
            SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
            if (!trunk) return;
            registry = &trunk->getRegistry();
        }

        if (registry->all_of<StaticMeshInstance>(entity)) {
            auto& meshComponent = registry->get<StaticMeshInstance>(entity);
            meshComponent.modelUuid = modelUuid;
            meshComponent.meshName = meshName;
            meshComponent.mesh = nullptr;
        } else {
            registry->emplace<StaticMeshInstance>(entity, modelUuid, meshName);
        }
        
        FCT::fout << "为实体添加了StaticMeshInstance组件: modelUuid=" << modelUuid << ", meshName=" << meshName << std::endl;
    }
    
    void SceneEntityViewer::addScriptComponent(entt::entity entity, const std::string& functionName, bool isGlobal, const std::string& trunkName) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene) return;
        
        entt::registry* registry;
        if (isGlobal) {
            registry = &scene->getRegistry();
        } else {
            SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
            if (!trunk) return;
            registry = &trunk->getRegistry();
        }

        if (registry->all_of<ScriptComponent>(entity)) {
            auto& scriptComponent = registry->get<ScriptComponent>(entity);
            scriptComponent.functionName = functionName;
        } else {
            registry->emplace<ScriptComponent>(entity, functionName);
        }
        
        FCT::fout << "为实体添加了ScriptComponent组件: functionName=" << functionName << std::endl;
    }
    
    void SceneEntityViewer::addDiffuseTextureComponent(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene) return;
        
        entt::registry* registry;
        if (isGlobal) {
            registry = &scene->getRegistry();
        } else {
            SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
            if (!trunk) return;
            registry = &trunk->getRegistry();
        }

        if (registry->all_of<DiffuseTextureComponent>(entity)) {
            auto& diffuseComponent = registry->get<DiffuseTextureComponent>(entity);
            diffuseComponent.modelUuid = modelUuid;
            diffuseComponent.texturePath = texturePath;
            diffuseComponent.texture = nullptr;
        } else {
            registry->emplace<DiffuseTextureComponent>(entity, modelUuid, texturePath);
        }
        
        FCT::fout << "为实体添加了DiffuseTextureComponent组件: modelUuid=" << modelUuid << ", texturePath=" << texturePath << std::endl;
    }

} // MQEngine