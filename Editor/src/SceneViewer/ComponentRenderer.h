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
            
            static float pos[3] = {component->position.x, component->position.y, component->position.z};
            pos[0] = component->position.x;
            pos[1] = component->position.y;
            pos[2] = component->position.z;
            
            if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<PositionComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->position.x = pos[0];
                            mutableComponent->position.y = pos[1];
                            mutableComponent->position.z = pos[2];
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##PositionComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<PositionComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<RotationComponent>(const RotationComponent* component) {
        if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            static float rot[3] = {component->rotation.x, component->rotation.y, component->rotation.z};
            rot[0] = component->rotation.x;
            rot[1] = component->rotation.y;
            rot[2] = component->rotation.z;
            
            if (ImGui::DragFloat3("Rotation (degrees)", rot, 1.0f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<RotationComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->rotation.x = rot[0];
                            mutableComponent->rotation.y = rot[1];
                            mutableComponent->rotation.z = rot[2];
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##RotationComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<RotationComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<ScaleComponent>(const ScaleComponent* component) {
        if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            static float scale[3] = {component->scale.x, component->scale.y, component->scale.z};
            scale[0] = component->scale.x;
            scale[1] = component->scale.y;
            scale[2] = component->scale.z;
            
            if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.001f, 100.0f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<ScaleComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->scale.x = scale[0];
                            mutableComponent->scale.y = scale[1];
                            mutableComponent->scale.z = scale[2];
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##ScaleComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<ScaleComponent>::value();
            }
            
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
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("网格体名称: %s", component->meshName.empty() ? "未设置" : component->meshName.c_str());
            if (component->mesh) {
                ImGui::Text("网格状态: 已加载");
            } else {
                ImGui::Text("网格状态: 未加载");
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##StaticMeshInstance")) {
                g_editorGlobal.componentToDelete = entt::type_hash<StaticMeshInstance>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<TickerScriptComponent>(const TickerScriptComponent* component) {
        if (ImGui::CollapsingHeader("Ticker Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("函数名: %s", component->functionName.empty() ? "未设置" : component->functionName.c_str());
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##TickerScriptComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<TickerScriptComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<OnStartScriptComponent>(const OnStartScriptComponent* component) {
        if (ImGui::CollapsingHeader("OnStart Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("函数名: %s", component->functionName.empty() ? "未设置" : component->functionName.c_str());
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##OnStartScriptComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<OnStartScriptComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<CacheRotationMatrix>(const CacheRotationMatrix* component) {
        if (ImGui::CollapsingHeader("CacheRotationMatrix", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("旋转矩阵 (缓存)");
            ImGui::Text("[%.3f, %.3f, %.3f, %.3f]", 
                component->rotationMatrix.m[0], component->rotationMatrix.m[1], 
                component->rotationMatrix.m[2], component->rotationMatrix.m[3]);
            ImGui::Text("[%.3f, %.3f, %.3f, %.3f]", 
                component->rotationMatrix.m[4], component->rotationMatrix.m[5], 
                component->rotationMatrix.m[6], component->rotationMatrix.m[7]);
            ImGui::Text("[%.3f, %.3f, %.3f, %.3f]", 
                component->rotationMatrix.m[8], component->rotationMatrix.m[9], 
                component->rotationMatrix.m[10], component->rotationMatrix.m[11]);
            ImGui::Text("[%.3f, %.3f, %.3f, %.3f]", 
                component->rotationMatrix.m[12], component->rotationMatrix.m[13], 
                component->rotationMatrix.m[14], component->rotationMatrix.m[15]);
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<CacheModelMatrix>(const CacheModelMatrix* component) {
        if (ImGui::CollapsingHeader("CacheModelMatrix", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型矩阵 (缓存)");
            ImGui::Text("Uniform已缓存并上传到GPU");
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<DirectionalLightComponent>(const DirectionalLightComponent* component) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            static float direction[3] = {component->direction.x, component->direction.y, component->direction.z};
            direction[0] = component->direction.x;
            direction[1] = component->direction.y;
            direction[2] = component->direction.z;
            
            if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<DirectionalLightComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->direction.x = direction[0];
                            mutableComponent->direction.y = direction[1];
                            mutableComponent->direction.z = direction[2];
                        }
                    }
                }
            }

            static float color[3] = {component->color.x, component->color.y, component->color.z};
            color[0] = component->color.x;
            color[1] = component->color.y;
            color[2] = component->color.z;
            
            if (ImGui::ColorEdit3("Color", color)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<DirectionalLightComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->color.x = color[0];
                            mutableComponent->color.y = color[1];
                            mutableComponent->color.z = color[2];
                        }
                    }
                }
            }

            static float intensity = component->intensity;
            intensity = component->intensity;
            
            if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 5.0f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<DirectionalLightComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->intensity = intensity;
                        }
                    }
                }
            }

            static bool enabled = component->enabled;
            enabled = component->enabled;
            
            if (ImGui::Checkbox("Enabled", &enabled)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<DirectionalLightComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->enabled = enabled;
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##DirectionalLightComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<DirectionalLightComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<DiffuseTextureComponent>(const DiffuseTextureComponent* component) {
        if (ImGui::CollapsingHeader("Diffuse Texture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##DiffuseTextureComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<DiffuseTextureComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<AlbedoTextureComponent>(const AlbedoTextureComponent* component) {
        if (ImGui::CollapsingHeader("Albedo 纹理##AlbedoTexture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");
            ImGui::Text("纹理启用: %s", component->visible ? "已启用" : "未启用");

            ImGui::Spacing();

            if (ImGui::Button("删除组件##AlbedoTextureComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<AlbedoTextureComponent>::value();
            }

            ImGui::Unindent(); // 取消缩进
        }
    }

    template<>
    inline void renderComponent<NormalMapComponent>(const NormalMapComponent* component) {
        if (ImGui::CollapsingHeader("Normal Map", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");

            ImGui::Spacing();
            if (ImGui::Button("删除组件##NormalMapComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<NormalMapComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<ShininessComponent>(const ShininessComponent* component) {
        if (ImGui::CollapsingHeader("Shininess", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            static float shininess = component->shininess;
            shininess = component->shininess;
            
            if (ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 256.0f)) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry;
                    if (selectedEntity.isGlobal) {
                        registry = &selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = &trunk->getRegistry();
                    }
                    
                    if (registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<ShininessComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->shininess = shininess;
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##ShininessComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<ShininessComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }
    
} // MQEngine

#endif //COMPONENTRENDERER_H