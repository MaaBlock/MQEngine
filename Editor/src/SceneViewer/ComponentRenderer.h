#ifndef COMPONENTRENDERER_H
#define COMPONENTRENDERER_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine {

    template<typename T>
    void renderComponent(const T* component);

    template<>
    inline void renderComponent<NameTag>(const NameTag* component) {
        if (ImGui::CollapsingHeader("NameTag", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("名称: %s", component->name.c_str());
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<PositionComponent>(const PositionComponent* component) {
        if (ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("X: %.3f", component->position.x);
            ImGui::Text("Y: %.3f", component->position.y);
            ImGui::Text("Z: %.3f", component->position.z);
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<RotationComponent>(const RotationComponent* component) {
        if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("X: %.3f°", component->rotation.x);
            ImGui::Text("Y: %.3f°", component->rotation.y);
            ImGui::Text("Z: %.3f°", component->rotation.z);
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<ScaleComponent>(const ScaleComponent* component) {
        if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("X: %.3f", component->scale.x);
            ImGui::Text("Y: %.3f", component->scale.y);
            ImGui::Text("Z: %.3f", component->scale.z);
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<CameraComponent>(const CameraComponent* component) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("激活: %s", component->active ? "是" : "否");
            ImGui::Text("视野角度: %.1f°", component->fov);
            ImGui::Text("近平面: %.3f", component->nearPlane);
            ImGui::Text("远平面: %.1f", component->farPlane);
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<StaticMeshInstance>(const StaticMeshInstance* component) {
        if (ImGui::CollapsingHeader("StaticMeshInstance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            if (component->staticMesh) {
                ImGui::Text("网格: 已加载");
            } else {
                ImGui::Text("网格: 未加载");
            }
            ImGui::Unindent();
        }
    }
    
} // MQEngine

#endif //COMPONENTRENDERER_H