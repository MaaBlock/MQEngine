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
        
        // 显示实体基本信息
        ImGui::Text("实体ID: %u", static_cast<uint32_t>(selectedEntity.entity));
        
        if (selectedEntity.isGlobal) {
            ImGui::Text("所属: 全局");
        } else {
            ImGui::Text("所属Trunk: %s", selectedEntity.trunkName.c_str());
        }
        
        ImGui::Separator();
        
        // 获取对应的注册表
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
        
        // 检查实体是否仍然有效
        if (!registry->valid(selectedEntity.entity)) {
            ImGui::Text("错误: 实体已被删除");
            return;
        }
        
        // 显示组件列表
        ImGui::Text("组件列表:");
        ImGui::BeginChild("ComponentList", ImVec2(0, 0), true);
        
        renderComponents(registry);
        
        ImGui::EndChild();
    }
    
    void EntityInspector::renderComponents(entt::registry* registry)
    {
        auto& selectedEntity = g_editorGlobal.selectedEntity;
        bool hasComponents = false;
        
        // 检查并显示NameTag组件
        if (auto nameTag = registry->try_get<NameTag>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<NameTag>(nameTag);
        }
        
        // 检查并显示Position组件
        if (auto position = registry->try_get<PositionComponent>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<PositionComponent>(position);
        }
        
        // 检查并显示Rotation组件
        if (auto rotation = registry->try_get<RotationComponent>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<RotationComponent>(rotation);
        }
        
        // 检查并显示Scale组件
        if (auto scale = registry->try_get<ScaleComponent>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<ScaleComponent>(scale);
        }
        
        // 检查并显示Camera组件
        if (auto camera = registry->try_get<CameraComponent>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<CameraComponent>(camera);
        }
        
        // 检查并显示StaticMeshInstance组件
        if (auto meshInstance = registry->try_get<StaticMeshInstance>(selectedEntity.entity)) {
            hasComponents = true;
            renderComponent<StaticMeshInstance>(meshInstance);
        }
        
        if (!hasComponents) {
            ImGui::Text("该实体没有组件");
        }
    }
} // MQEngine