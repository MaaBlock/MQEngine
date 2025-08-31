//
// Created by Administrator on 2025/8/25.
//

#include "EntityInspector.h"
#include "ComponentRenderer.h"

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
        hasComponents |= tryRenderComponent<StaticMeshInstance>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<ScriptComponent>(registry, selectedEntity.entity);
        
        if (!hasComponents) {
            ImGui::Text("该实体没有组件");
        }
    }
} // MQEngine