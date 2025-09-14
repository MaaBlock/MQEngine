#include "SceneEntityViewer.h"
#include "../thirdparty/thirdparty.h"
#include "imgui.h"
#include "../core/Global.h"
#include "../data/NameTag.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    SceneEntityViewer::SceneEntityViewer() {
        m_dataManager = g_editorGlobal.dataManager;
    }

    void SceneEntityViewer::render() {
        ImGui::Begin(TEXT("场景实体查看器"));

        if (m_dataManager && m_dataManager->getCurrentScene()) {
            Scene* scene = m_dataManager->getCurrentScene();
            renderSceneEntityList(scene);
            renderCreateEntityDialog(scene);
        } else {
            ImGui::Text(TEXT("未选择场景"));
        }

        ImGui::End();
        renderScriptTypeSelectionPopup();
    }

    void SceneEntityViewer::renderSceneEntityList(Scene* scene) {
        ImGui::Text(TEXT("当前场景: %s"), scene->getName().c_str());
        ImGui::SameLine();
        if (ImGui::Button(TEXT("保存场景"))) {
            scene->save();
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader(TEXT("场景全局"), ImGuiTreeNodeFlags_DefaultOpen)) {
            renderGlobalEntityList(scene);
        }

        for (auto& trunkName : scene->getTrunkList()) {
            if (ImGui::CollapsingHeader(trunkName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                 if (scene->isLoad(trunkName)) {
                    renderTrunkEntityList(scene, trunkName);
                 } else {
                    ImGui::TextDisabled(TEXT("(未加载)"));
                 }
            }
        }
    }

    void SceneEntityViewer::renderGlobalEntityList(Scene* scene) {
        ImGui::BeginGroup();
        
        auto& registry = scene->getRegistry();
        registry.view<entt::entity>().each([&](auto entity) {
            std::string displayName;
            if (auto nameTag = registry.try_get<NameTag>(entity)) {
                displayName = nameTag->name;
            } else {
                displayName = "entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            bool isSelected = (g_editorGlobal.selectedEntity.entity == entity && g_editorGlobal.selectedEntity.isGlobal);
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                selectEntity(entity, "", true);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
                    std::string functionName = static_cast<const char*>(payload->Data);
                    addScriptComponent(entity, functionName, true, "");
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string meshName = dragData.substr(separatorPos + 1);
                        addStaticMeshComponent(entity, modelUuid, meshName, true, "");
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string texturePath = dragData.substr(separatorPos + 1);
                        addDiffuseTextureComponent(entity, modelUuid, texturePath, true, "");
                    }
                }
                ImGui::EndDragDropTarget();
            }
        });
        ImGui::EndGroup();

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("GlobalEntityContextMenu");
        }

        if (ImGui::BeginPopup("GlobalEntityContextMenu")) {
            if (ImGui::MenuItem(TEXT("创建实体"))) {
                showCreateEntityDialog("", true);
            }
            if (ImGui::MenuItem(TEXT("删除实体"), nullptr, false, g_editorGlobal.selectedEntity.entity != entt::null)) {
                deleteEntity(g_editorGlobal.selectedEntity.entity, "", true);
            }
            ImGui::EndPopup();
        }
    }

    void SceneEntityViewer::renderTrunkEntityList(Scene* scene, std::string trunkName) {
        ImGui::BeginGroup();
        SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
        if (!trunk) {
            ImGui::EndGroup();
            return;
        }
        auto& registry = trunk->getRegistry();
        
        registry.view<entt::entity>().each([&](auto entity) {
            std::string displayName;
            if (auto nameTag = registry.try_get<NameTag>(entity)) {
                displayName = nameTag->name;
            } else {
                displayName = "entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            bool isSelected = (g_editorGlobal.selectedEntity.entity == entity && !g_editorGlobal.selectedEntity.isGlobal && g_editorGlobal.selectedEntity.trunkName == trunkName);
            if (ImGui::Selectable(displayName.c_str(), isSelected)) {
                selectEntity(entity, trunkName, false);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
                    std::string functionName = static_cast<const char*>(payload->Data);
                    addScriptComponent(entity, functionName, false, trunkName);
                }
                 if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                    std::string dragData = static_cast<const char*>(payload->Data);
                    size_t separatorPos = dragData.find('|');
                    if (separatorPos != std::string::npos) {
                        std::string modelUuid = dragData.substr(0, separatorPos);
                        std::string meshName = dragData.substr(separatorPos + 1);
                        addStaticMeshComponent(entity, modelUuid, meshName, false, trunkName);
                    }
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
        });
        ImGui::EndGroup();

        std::string popupId = "TrunkEntityContextMenu_" + trunkName;
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (ImGui::BeginPopup(popupId.c_str())) {
            if (ImGui::MenuItem(TEXT("在Trunk中创建实体"))) {
                showCreateEntityDialog(trunkName, false);
            }
            if (ImGui::MenuItem(TEXT("删除实体"), nullptr, false, g_editorGlobal.selectedEntity.entity != entt::null)) {
                deleteEntity(g_editorGlobal.selectedEntity.entity, trunkName, false);
            }
            ImGui::EndPopup();
        }
    }

    void SceneEntityViewer::selectEntity(entt::entity entity, const std::string& trunkName, bool isGlobal) {
        g_editorGlobal.selectedEntity.entity = entity;
        g_editorGlobal.selectedEntity.trunkName = trunkName;
        g_editorGlobal.selectedEntity.isGlobal = isGlobal;
        g_editorGlobal.selectedEntity.scene = m_dataManager->getCurrentScene();
    }

    void SceneEntityViewer::addScriptComponent(entt::entity entity, const std::string& functionName, bool isGlobal, const std::string& trunkName) {
        m_showScriptTypePopup = true;
        m_draggedFunctionName = functionName;
        m_targetEntity = entity;
        m_targetTrunkName = trunkName;
        m_targetIsGlobal = isGlobal;
    }

    void SceneEntityViewer::renderScriptTypeSelectionPopup() {
        if (!m_showScriptTypePopup) return;

        ImGui::OpenPopup(TEXT("选择脚本类型"));
        if (ImGui::BeginPopupModal(TEXT("选择脚本类型"), &m_showScriptTypePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(TEXT("为实体添加脚本 '%s'"), m_draggedFunctionName.c_str());
            ImGui::Separator();

            if (ImGui::Button(TEXT("Ticker (每帧执行)"), ImVec2(150, 0))) {
                Scene* scene = m_dataManager->getCurrentScene();
                if (scene) {
                    entt::registry* registry = m_targetIsGlobal ? &scene->getRegistry() : &scene->getLoadedTrunk(m_targetTrunkName)->getRegistry();
                    if (registry) {
                        registry->emplace_or_replace<TickerScriptComponent>(m_targetEntity, m_draggedFunctionName);
                    }
                }
                m_showScriptTypePopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT("OnStart (启动时执行)"), ImVec2(150, 0))) {
                Scene* scene = m_dataManager->getCurrentScene();
                if (scene) {
                    entt::registry* registry = m_targetIsGlobal ? &scene->getRegistry() : &scene->getLoadedTrunk(m_targetTrunkName)->getRegistry();
                    if (registry) {
                        registry->emplace_or_replace<OnStartScriptComponent>(m_targetEntity, m_draggedFunctionName);
                    }
                }
                m_showScriptTypePopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (ImGui::Button(TEXT("取消"), ImVec2(120, 0))) {
                m_showScriptTypePopup = false;
            }
            ImGui::EndPopup();
        } else {
            m_showScriptTypePopup = false;
        }
    }

    void SceneEntityViewer::showCreateEntityDialog(const std::string& targetTrunk, bool isGlobal) {
        m_showCreateEntityDialog = true;
        m_targetTrunkName = targetTrunk;
        m_createInGlobal = isGlobal;
        strcpy_s(m_newEntityName, sizeof(m_newEntityName), "新实体");
    }

    void SceneEntityViewer::renderCreateEntityDialog(Scene* scene) {
        if (!m_showCreateEntityDialog) return;

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(TEXT("创建实体"), &m_showCreateEntityDialog, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoResize)) {
            ImGui::Text(TEXT("创建位置: %s"), m_createInGlobal ? TEXT("场景全局") : m_targetTrunkName.c_str());
            ImGui::Separator();
            ImGui::InputText(TEXT("实体名称"), m_newEntityName, sizeof(m_newEntityName));
            ImGui::Separator();

            if (ImGui::Button(TEXT("创建"))) {
                if (strlen(m_newEntityName) > 0) {
                    createEntity(scene, m_newEntityName, m_targetTrunkName, m_createInGlobal);
                    m_showCreateEntityDialog = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT("取消"))) {
                m_showCreateEntityDialog = false;
            }
        }
        ImGui::End();
    }

    void SceneEntityViewer::createEntity(Scene* scene, const std::string& name, const std::string& trunkName, bool isGlobal) {
        entt::registry* registry = nullptr;
        if (isGlobal) {
            registry = &scene->getRegistry();
        } else {
            SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
            if (trunk) {
                registry = &trunk->getRegistry();
            }
        }

        if (registry) {
            auto newEntity = registry->create();
            registry->emplace<NameTag>(newEntity, name);
        }
    }

    void SceneEntityViewer::deleteEntity(entt::entity entity, const std::string& trunkName, bool isGlobal) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene || entity == entt::null) return;

        entt::registry* registry = nullptr;
        if (isGlobal) {
            registry = &scene->getRegistry();
        } else {
            SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
            if (trunk) {
                registry = &trunk->getRegistry();
            }
        }

        if (registry && registry->valid(entity)) {
            registry->destroy(entity);
            if (g_editorGlobal.selectedEntity.entity == entity) {
                g_editorGlobal.selectedEntity.entity = entt::null;
                g_editorGlobal.selectedEntity.trunkName = "";
                g_editorGlobal.selectedEntity.isGlobal = false;
                g_editorGlobal.selectedEntity.scene = nullptr;
            }
        }
    }

    void SceneEntityViewer::addStaticMeshComponent(entt::entity entity, const std::string& modelUuid, const std::string& meshName, bool isGlobal, const std::string& trunkName) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene) return;
        
        entt::registry* registry = isGlobal ? &scene->getRegistry() : &scene->getLoadedTrunk(trunkName)->getRegistry();
        if (!registry) return;

        registry->emplace_or_replace<StaticMeshInstance>(entity, modelUuid, meshName);
    }
    
    void SceneEntityViewer::addDiffuseTextureComponent(entt::entity entity, const std::string& modelUuid, const std::string& texturePath, bool isGlobal, const std::string& trunkName) {
        Scene* scene = m_dataManager->getCurrentScene();
        if (!scene) return;
        
        entt::registry* registry = isGlobal ? &scene->getRegistry() : &scene->getLoadedTrunk(trunkName)->getRegistry();
        if (!registry) return;

        registry->emplace_or_replace<DiffuseTextureComponent>(entity, modelUuid, texturePath);
    }

} // namespace MQEngine