//
// Created by Administrator on 2025/8/25.
//

#include "EntityInspector.h"
#include "ComponentRenderer.h"
#include "../imgui/EditorCameraManager.h"

namespace MQEngine {
    EntityInspector::EntityInspector()
    {

    }

    void EntityInspector::render()
    {
        ImGui::Begin("实体检查器");
        
        auto& selectedEntity = g_editorGlobal.selectedEntity;
        if (selectedEntity.scene == nullptr || selectedEntity.entity == entt::null) {
            ImGui::Text("未选择实体");
            ImGui::Text("请在场景实体查看器中选择一个实体来查看详情");
        } else {
            renderEntityDetails();
        }
        
        ImGui::End();
    }
    
    void EntityInspector::renderEntityDetails()
    {
        auto& selectedEntity = g_editorGlobal.selectedEntity;

        ImGui::Text("实体ID: %u", static_cast<uint32_t>(selectedEntity.entity));
        
        if (selectedEntity.isGlobal) {
            ImGui::Text("所属: 全局");
        } else {
            ImGui::Text("所属Trunk: %s", selectedEntity.trunkName.c_str());
        }
        
        ImGui::Separator();

        entt::registry* registry = nullptr;
        if (selectedEntity.isGlobal) {
            registry = &selectedEntity.scene->getRegistry();
        } else {
            SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
            if (trunk) {
                registry = &trunk->getRegistry();
            }
        }
        
        if (registry == nullptr) {
            ImGui::Text("错误: 无法获取实体注册表");
            return;
        }

        if (!registry->valid(selectedEntity.entity)) {
            ImGui::Text("错误: 实体已被删除");
            return;
        }

        ImGui::Text("组件列表:");

        if (ImGui::Button("添加组件")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            ImGui::Text("选择要添加的组件:");
            ImGui::Separator();
            
            if (ImGui::MenuItem("Position Component")) {
                if (!registry->all_of<PositionComponent>(selectedEntity.entity)) {
                    registry->emplace<PositionComponent>(selectedEntity.entity, FCT::Vec3{0.0f, 0.0f, 0.0f});
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Rotation Component")) {
                if (!registry->all_of<RotationComponent>(selectedEntity.entity)) {
                    registry->emplace<RotationComponent>(selectedEntity.entity, FCT::Vec3{0.0f, 0.0f, 0.0f});
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Scale Component")) {
                if (!registry->all_of<ScaleComponent>(selectedEntity.entity)) {
                    registry->emplace<ScaleComponent>(selectedEntity.entity, FCT::Vec3{1.0f, 1.0f, 1.0f});
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Camera Component")) {
                if (!registry->all_of<CameraComponent>(selectedEntity.entity)) {
                    auto& camera = registry->emplace<CameraComponent>(selectedEntity.entity);
                    camera.active = false;
                    camera.fov = 60;
                    camera.nearPlane = 0.1f;
                    camera.farPlane = 50.0f;
                    g_editorGlobal.cameraManager->hookCamera();
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Static Mesh Instance")) {
                if (!registry->all_of<StaticMeshInstance>(selectedEntity.entity)) {
                    registry->emplace<StaticMeshInstance>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Ticker Script Component")) {
                if (!registry->all_of<TickerScriptComponent>(selectedEntity.entity)) {
                    registry->emplace<TickerScriptComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("OnStart Script Component")) {
                if (!registry->all_of<OnStartScriptComponent>(selectedEntity.entity)) {
                    registry->emplace<OnStartScriptComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }
            
            if (ImGui::MenuItem("Directional Light Component")) {
                if (!registry->all_of<DirectionalLightComponent>(selectedEntity.entity)) {
                    registry->emplace<DirectionalLightComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Normal Map Component")) {
                if (!registry->all_of<NormalMapComponent>(selectedEntity.entity)) {
                    registry->emplace<NormalMapComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Shininess Component")) {
                if (!registry->all_of<ShininessComponent>(selectedEntity.entity)) {
                    registry->emplace<ShininessComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }
        
        ImGui::BeginChild("ComponentList", ImVec2(0, 0), true);
        
        renderComponents(registry);
        
        ImGui::EndChild();
    }

    template<typename ComponentType>
    bool EntityInspector::tryRenderComponent(entt::registry* registry, entt::entity entity)
    {
        if (auto component = registry->try_get<ComponentType>(entity)) {
            renderComponent<ComponentType>(component);
            return true;
        }
        return false;
    }

    void EntityInspector::renderComponents(entt::registry* registry)
    {
        auto& selectedEntity = g_editorGlobal.selectedEntity;
        bool hasComponents = false;

        hasComponents |= tryRenderComponent<NameTag>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<PositionComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<RotationComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<ScaleComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<CameraComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<CacheRotationMatrix>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<CacheModelMatrix>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<StaticMeshInstance>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<TickerScriptComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<OnStartScriptComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<DirectionalLightComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<DiffuseTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<NormalMapComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<ShininessComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<AlbedoTextureComponent>(registry, selectedEntity.entity);

        if (g_editorGlobal.componentToDelete != 0) {
            auto storage = registry->storage(g_editorGlobal.componentToDelete);
            if (storage && storage->contains(selectedEntity.entity)) {
                storage->remove(selectedEntity.entity);
            }
            g_editorGlobal.componentToDelete = 0;
        }
        
        if (!hasComponents) {
            ImGui::Text("该实体没有组件");
        }
    }

} // MQEngine