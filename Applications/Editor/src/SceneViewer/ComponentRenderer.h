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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (!trunk) return;
                        registry = trunk->getRegistry();
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

    template<>
    inline void renderComponent<NormalTextureComponent>(const NormalTextureComponent* component) {
        if (ImGui::CollapsingHeader("Normal 纹理##NormalTexture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");
            ImGui::Text("纹理启用: %s", component->visible ? "已启用" : "未启用");

            ImGui::Spacing();

            if (ImGui::Button("删除组件##NormalTextureComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<NormalTextureComponent>::value();
            }

            ImGui::Unindent(); 
        }
    }

    template<>
    inline void renderComponent<EmissiveTextureComponent>(const EmissiveTextureComponent* component) {
        if (ImGui::CollapsingHeader("Emissive 纹理##EmissiveTexture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");
            ImGui::Text("纹理启用: %s", component->visible ? "已启用" : "未启用");

            ImGui::Spacing();

            if (ImGui::Button("删除组件##EmissiveTextureComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<EmissiveTextureComponent>::value();
            }

            ImGui::Unindent(); 
        }
    }

    template<>
    inline void renderComponent<OrmTextureComponent>(const OrmTextureComponent* component) {
        if (ImGui::CollapsingHeader("Orm 纹理##OrmTexture", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "未设置" : component->modelUuid.c_str());
            ImGui::Text("纹理路径: %s", component->texturePath.empty() ? "未设置" : component->texturePath.c_str());
            ImGui::Text("纹理状态: %s", component->texture ? "已加载" : "未加载");
            ImGui::Text("纹理启用: %s", component->visible ? "已启用" : "未启用");

            ImGui::Spacing();

            if (ImGui::Button("删除组件##OrmTextureComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<OrmTextureComponent>::value();
            }

            ImGui::Unindent(); 
        }
    }

    template<>
    inline void renderComponent<ScriptFunctionTableComponent>(const ScriptFunctionTableComponent* component) {
        if (ImGui::CollapsingHeader("Script Function Table", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            auto& selectedEntity = g_editorGlobal.selectedEntity;
            ScriptFunctionTableComponent* mutableComponent = nullptr;
            if (selectedEntity.scene) {
                entt::registry* registry = nullptr;
                if (selectedEntity.isGlobal) {
                    registry = selectedEntity.scene->getRegistry();
                } else {
                    SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                    if (trunk) registry = trunk->getRegistry();
                }
                if (registry && registry->valid(selectedEntity.entity)) {
                    mutableComponent = registry->try_get<ScriptFunctionTableComponent>(selectedEntity.entity);
                }
            }

            ImGui::Text("OnTicker Functions:");
            if (mutableComponent) {
                for (int i = 0; i < mutableComponent->onTicker.size(); ++i) {
                    ImGui::PushID(i);
                    
                    char buffer[128];
                    strncpy(buffer, mutableComponent->onTicker[i].c_str(), sizeof(buffer));
                    buffer[sizeof(buffer) - 1] = '\0'; // Ensure null termination

                    if (ImGui::InputText("##func", buffer, sizeof(buffer))) {
                         mutableComponent->onTicker[i] = buffer;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove")) {
                         mutableComponent->onTicker.erase(mutableComponent->onTicker.begin() + i);
                         i--; 
                    }
                    
                    ImGui::PopID();
                }

                if (ImGui::Button("Add Function")) {
                    mutableComponent->onTicker.push_back("NewFunction");
                }
            } else {
                 for (const auto& func : component->onTicker) {
                     ImGui::Text("- %s", func.c_str());
                 }
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##ScriptFunctionTableComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<ScriptFunctionTableComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<CacheScriptComponent>(const CacheScriptComponent* component) {
        if (ImGui::CollapsingHeader("Cache Script (Runtime)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            if (component->m_jsObject) {
                ImGui::Text("JS Object: Valid (%p)", component->m_jsObject);
            } else {
                ImGui::Text("JS Object: Null");
            }
            
            ImGui::Text("Note: Runtime cache, not editable.");
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##CacheScriptComponent")) {
                 g_editorGlobal.componentToDelete = entt::type_hash<CacheScriptComponent>::value();
            }

            ImGui::Unindent();
        }
    }
    
    template<>
    inline void renderComponent<SkyboxComponent>(const SkyboxComponent* component) {
        if (ImGui::CollapsingHeader("Skybox Config", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            
            char buffer[256];
            strncpy(buffer, component->texturePath.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';
            
            ImGui::Text("Texture Path (Directory):");
            if (ImGui::InputText("##skyboxPath", buffer, sizeof(buffer))) {
                auto& selectedEntity = g_editorGlobal.selectedEntity;
                if (selectedEntity.scene) {
                    entt::registry* registry = nullptr;
                    if (selectedEntity.isGlobal) {
                        registry = selectedEntity.scene->getRegistry();
                    } else {
                        SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                        if (trunk) registry = trunk->getRegistry();
                    }
                    
                    if (registry && registry->valid(selectedEntity.entity)) {
                        auto* mutableComponent = registry->try_get<SkyboxComponent>(selectedEntity.entity);
                        if (mutableComponent) {
                            mutableComponent->texturePath = buffer;
                        }
                    }
                }
            }
            
            ImGui::Spacing();
            if (ImGui::Button("删除组件##SkyboxComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<SkyboxComponent>::value();
            }
            
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<CacheSkyboxComponent>(const CacheSkyboxComponent* component) {
        if (ImGui::CollapsingHeader("Skybox Cache (Runtime)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::Text("Texture Loaded: %s", component->texture ? "Yes" : "No");
            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<PointLightComponent>(const PointLightComponent* component) {
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            PointLightComponent* mutableComponent = nullptr;
            if (selectedEntity.scene) {
                entt::registry* registry = nullptr;
                if (selectedEntity.isGlobal) {
                    registry = selectedEntity.scene->getRegistry();
                } else {
                    SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                    if (trunk) registry = trunk->getRegistry();
                }
                if (registry && registry->valid(selectedEntity.entity)) {
                    mutableComponent = registry->try_get<PointLightComponent>(selectedEntity.entity);
                }
            }

            float color[3] = {component->color.x, component->color.y, component->color.z};
            if (ImGui::ColorEdit3("Color##PointLight", color) && mutableComponent) {
                mutableComponent->color = FCT::Vec3(color[0], color[1], color[2]);
            }

            float intensity = component->intensity;
            if (ImGui::DragFloat("Intensity##PointLight", &intensity, 0.05f, 0.0f, 100.0f) && mutableComponent) {
                mutableComponent->intensity = intensity;
            }

            float range = component->range;
            if (ImGui::DragFloat("Range##PointLight", &range, 0.1f, 0.1f, 500.0f) && mutableComponent) {
                mutableComponent->range = range;
            }

            float atten[3] = {
                component->constant,
                component->linear,
                component->quadratic
            };
            if (ImGui::DragFloat3("Attenuation (C/L/Q)##PointLight", atten, 0.005f, 0.0f, 10.0f) && mutableComponent) {
                mutableComponent->constant = atten[0];
                mutableComponent->linear = atten[1];
                mutableComponent->quadratic = atten[2];
            }

            bool enabled = component->enabled;
            if (ImGui::Checkbox("Enabled##PointLight", &enabled) && mutableComponent) {
                mutableComponent->enabled = enabled;
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##PointLightComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<PointLightComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<PBRMaterialComponent>(const PBRMaterialComponent* component) {
        if (ImGui::CollapsingHeader("PBR Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            PBRMaterialComponent* mutableComponent = nullptr;
            if (selectedEntity.scene) {
                entt::registry* registry = nullptr;
                if (selectedEntity.isGlobal) {
                    registry = selectedEntity.scene->getRegistry();
                } else {
                    SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                    if (trunk) registry = trunk->getRegistry();
                }
                if (registry && registry->valid(selectedEntity.entity)) {
                    mutableComponent = registry->try_get<PBRMaterialComponent>(selectedEntity.entity);
                }
            }

            float baseColor[3] = {component->baseColor.x, component->baseColor.y, component->baseColor.z};
            if (ImGui::ColorEdit3("Base Color##PBRMat", baseColor) && mutableComponent) {
                mutableComponent->baseColor = FCT::Vec3(baseColor[0], baseColor[1], baseColor[2]);
            }

            float metallic = component->metallic;
            if (ImGui::SliderFloat("Metallic##PBRMat", &metallic, 0.0f, 1.0f) && mutableComponent) {
                mutableComponent->metallic = metallic;
            }

            float roughness = component->roughness;
            if (ImGui::SliderFloat("Roughness##PBRMat", &roughness, 0.04f, 1.0f) && mutableComponent) {
                mutableComponent->roughness = roughness;
            }

            float ao = component->ao;
            if (ImGui::SliderFloat("Ambient Occlusion##PBRMat", &ao, 0.0f, 1.0f) && mutableComponent) {
                mutableComponent->ao = ao;
            }

            float emissive[3] = {component->emissive.x, component->emissive.y, component->emissive.z};
            if (ImGui::ColorEdit3("Emissive##PBRMat", emissive) && mutableComponent) {
                mutableComponent->emissive = FCT::Vec3(emissive[0], emissive[1], emissive[2]);
            }

            char sgBuffer[128];
            strncpy(sgBuffer, component->shaderGraphName.c_str(), sizeof(sgBuffer));
            sgBuffer[sizeof(sgBuffer) - 1] = '\0';
            if (ImGui::InputText("ShaderGraph##PBRMat", sgBuffer, sizeof(sgBuffer)) && mutableComponent) {
                mutableComponent->shaderGraphName = sgBuffer;
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##PBRMaterialComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<PBRMaterialComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<SkeletalAnimatorComponent>(const SkeletalAnimatorComponent* component) {
        if (ImGui::CollapsingHeader("骨骼动画 (Skeletal Animator)##SkeletalAnimator", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            entt::registry* registry = nullptr;
            SkeletalAnimatorComponent* mutableComponent = nullptr;
            const CacheSkeletalAnimation* cacheComponent = nullptr;
            if (selectedEntity.scene) {
                if (selectedEntity.isGlobal) {
                    registry = selectedEntity.scene->getRegistry();
                } else {
                    SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
                    if (trunk) registry = trunk->getRegistry();
                }
                if (registry && registry->valid(selectedEntity.entity)) {
                    mutableComponent = registry->try_get<SkeletalAnimatorComponent>(selectedEntity.entity);
                    cacheComponent = registry->try_get<CacheSkeletalAnimation>(selectedEntity.entity);
                }
            }

            std::shared_ptr<FCT::ModelSkeletonData> skeleton = cacheComponent ? cacheComponent->skeleton : nullptr;
            if (auto* engine = Runtime::Engine::getInstance(); !skeleton && engine && engine->animation->skeletalAnimationSystem && !component->modelUuid.empty()) {
                skeleton = engine->animation->skeletalAnimationSystem->getOrLoadSkeleton(component->modelUuid);
            }

            ImGui::Text("模型UUID: %s", component->modelUuid.empty() ? "自动关联" : component->modelUuid.c_str());
            if (skeleton) {
                ImGui::Text("骨骼数量: %u", static_cast<uint32_t>(skeleton->bones.size()));
            }

            if (skeleton && !skeleton->animations.empty()) {
                const char* previewClip = component->clipName.empty() ? skeleton->animations[0].name.c_str() : component->clipName.c_str();
                if (ImGui::BeginCombo("动画片段##SkeletalClip", previewClip)) {
                    for (const auto& clip : skeleton->animations) {
                        bool isSelected = (component->clipName == clip.name);
                        if (ImGui::Selectable(clip.name.c_str(), isSelected) && mutableComponent) {
                            mutableComponent->clipName = clip.name;
                            mutableComponent->currentTime = 0.0f;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                char clipBuffer[128];
                strncpy(clipBuffer, component->clipName.c_str(), sizeof(clipBuffer));
                clipBuffer[sizeof(clipBuffer) - 1] = '\0';
                if (ImGui::InputText("动画片段##SkeletalClipInput", clipBuffer, sizeof(clipBuffer)) && mutableComponent) {
                    mutableComponent->clipName = clipBuffer;
                }
            }

            bool playing = component->playing;
            if (ImGui::Checkbox("播放 (Playing)##SkeletalPlay", &playing) && mutableComponent) {
                mutableComponent->playing = playing;
            }
            ImGui::SameLine();
            bool loop = component->loop;
            if (ImGui::Checkbox("循环 (Loop)##SkeletalLoop", &loop) && mutableComponent) {
                mutableComponent->loop = loop;
            }
            ImGui::SameLine();
            if (ImGui::Button("重置##SkeletalReset") && mutableComponent) {
                mutableComponent->currentTime = 0.0f;
            }

            float speed = component->speed;
            if (ImGui::DragFloat("播放倍速 (Speed)##SkeletalSpeed", &speed, 0.05f, -5.0f, 5.0f, "%.2fx") && mutableComponent) {
                mutableComponent->speed = speed;
            }

            float maxDuration = 10.0f;
            if (skeleton) {
                if (const auto* activeClip = skeleton->findAnimation(component->clipName)) {
                    float clipSec = activeClip->getDurationInSeconds();
                    if (clipSec > 0.0f) {
                        maxDuration = clipSec;
                    }
                }
            }
            float currentTime = component->currentTime;
            if (ImGui::SliderFloat("当前时间 (s)##SkeletalTime", &currentTime, 0.0f, maxDuration, "%.3f s") && mutableComponent) {
                mutableComponent->currentTime = currentTime;
            }

            bool lockRoot = component->lockRootPosition;
            if (ImGui::Checkbox("锁定水平根位移 (Lock Root)##SkeletalLockRoot", &lockRoot) && mutableComponent) {
                mutableComponent->lockRootPosition = lockRoot;
            }

            ImGui::Separator();
            ImGui::TextDisabled("平滑过渡 (Crossfade)");
            if (skeleton && !skeleton->animations.empty()) {
                const char* targetPreview = component->targetClipName.empty() ? "(无过渡)" : component->targetClipName.c_str();
                if (ImGui::BeginCombo("过渡目标片段##SkeletalTargetClip", targetPreview)) {
                    if (ImGui::Selectable("(无过渡)", component->targetClipName.empty()) && mutableComponent) {
                        mutableComponent->targetClipName.clear();
                        mutableComponent->blendWeight = 0.0f;
                        mutableComponent->blendElapsed = 0.0f;
                    }
                    for (const auto& clip : skeleton->animations) {
                        bool isSel = (component->targetClipName == clip.name);
                        if (ImGui::Selectable(clip.name.c_str(), isSel) && mutableComponent) {
                            mutableComponent->targetClipName = clip.name;
                            mutableComponent->targetTime = 0.0f;
                            mutableComponent->blendElapsed = 0.0f;
                            mutableComponent->blendWeight = 0.0f;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            float blendDuration = component->blendDuration;
            if (ImGui::DragFloat("过渡时长 (s)##SkeletalBlendDur", &blendDuration, 0.02f, 0.01f, 5.0f, "%.2f s") && mutableComponent) {
                mutableComponent->blendDuration = blendDuration;
            }
            if (!component->targetClipName.empty()) {
                ImGui::ProgressBar(component->blendWeight, ImVec2(-1.0f, 0.0f), "Crossfade 权重");
            }

            ImGui::Separator();
            ImGui::TextDisabled("分层遮罩混合 (Layer Mask Blending)");
            if (skeleton && !skeleton->animations.empty()) {
                const char* layerPreview = component->layerClipName.empty() ? "(关闭分层)" : component->layerClipName.c_str();
                if (ImGui::BeginCombo("分层片段##SkeletalLayerClip", layerPreview)) {
                    if (ImGui::Selectable("(关闭分层)", component->layerClipName.empty()) && mutableComponent) {
                        mutableComponent->layerClipName.clear();
                        mutableComponent->layerWeight = 0.0f;
                    }
                    for (const auto& clip : skeleton->animations) {
                        bool isSel = (component->layerClipName == clip.name);
                        if (ImGui::Selectable(clip.name.c_str(), isSel) && mutableComponent) {
                            mutableComponent->layerClipName = clip.name;
                            mutableComponent->layerTime = 0.0f;
                            if (mutableComponent->layerWeight <= 0.0f) {
                                mutableComponent->layerWeight = 1.0f;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (skeleton && !skeleton->flatNodes.empty()) {
                const char* maskPreview = component->layerMaskRootBone.empty() ? "(全身叠加)" : component->layerMaskRootBone.c_str();
                if (ImGui::BeginCombo("遮罩根骨骼##SkeletalMaskBone", maskPreview)) {
                    if (ImGui::Selectable("(全身叠加)", component->layerMaskRootBone.empty()) && mutableComponent) {
                        mutableComponent->layerMaskRootBone.clear();
                    }
                    for (const auto& node : skeleton->flatNodes) {
                        if (node.name.empty()) continue;
                        bool isSel = (component->layerMaskRootBone == node.name);
                        if (ImGui::Selectable(node.name.c_str(), isSel) && mutableComponent) {
                            mutableComponent->layerMaskRootBone = node.name;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                char maskBuf[128] = {};
                std::snprintf(maskBuf, sizeof(maskBuf), "%s", component->layerMaskRootBone.c_str());
                if (ImGui::InputText("遮罩根骨骼##SkeletalMaskInput", maskBuf, sizeof(maskBuf)) && mutableComponent) {
                    mutableComponent->layerMaskRootBone = maskBuf;
                }
            }

            float layerWeight = component->layerWeight;
            if (ImGui::SliderFloat("分层权重##SkeletalLayerWeight", &layerWeight, 0.0f, 1.0f, "%.2f") && mutableComponent) {
                mutableComponent->layerWeight = layerWeight;
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##SkeletalAnimatorComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<SkeletalAnimatorComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    namespace detail {
        inline entt::registry* resolveSelectedEntityRegistry() {
            auto& selectedEntity = g_editorGlobal.selectedEntity;
            if (!selectedEntity.scene) {
                return nullptr;
            }
            if (selectedEntity.isGlobal) {
                return selectedEntity.scene->getRegistry();
            }
            SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
            return trunk ? trunk->getRegistry() : nullptr;
        }
    }

    template<>
    inline void renderComponent<SkeletalBoneSocketComponent>(const SkeletalBoneSocketComponent* component) {
        if (ImGui::CollapsingHeader("骨骼挂点 (Skeletal Bone Socket)##BoneSocket", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            entt::registry* registry = detail::resolveSelectedEntityRegistry();
            SkeletalBoneSocketComponent* mutableComp = (registry && registry->valid(selectedEntity.entity))
                ? registry->try_get<SkeletalBoneSocketComponent>(selectedEntity.entity)
                : nullptr;

            std::string targetPreview = component->targetEntityUuid.empty() ? "(自动查找父级骨骼实体)" : component->targetEntityUuid;
            entt::entity resolvedTarget = entt::null;
            if (registry) {
                if (!component->targetEntityUuid.empty()) {
                    auto tagView = registry->view<NameTag>();
                    for (auto e : tagView) {
                        const auto& tag = tagView.get<NameTag>(e);
                        if (tag.uuid == component->targetEntityUuid) {
                            targetPreview = tag.name + " (" + tag.uuid.substr(0, 8) + ")";
                            resolvedTarget = e;
                            break;
                        }
                    }
                } else if (const auto* hier = registry->try_get<HierarchyComponent>(selectedEntity.entity)) {
                    entt::entity p = static_cast<entt::entity>(hier->parentEntity);
                    if (p != entt::null && registry->valid(p)) {
                        resolvedTarget = p;
                    }
                }
            }

            if (registry && ImGui::BeginCombo("目标骨骼实体##SocketTarget", targetPreview.c_str())) {
                if (ImGui::Selectable("(自动查找父级骨骼实体)", component->targetEntityUuid.empty()) && mutableComp) {
                    mutableComp->targetEntityUuid.clear();
                }
                auto animView = registry->view<SkeletalAnimatorComponent, NameTag>();
                for (auto e : animView) {
                    const auto& tag = animView.get<NameTag>(e);
                    std::string label = tag.name + "##" + tag.uuid;
                    bool isSel = (component->targetEntityUuid == tag.uuid);
                    if (ImGui::Selectable(label.c_str(), isSel) && mutableComp) {
                        mutableComp->targetEntityUuid = tag.uuid;
                    }
                }
                ImGui::EndCombo();
            }

            std::shared_ptr<FCT::ModelSkeletonData> targetSkeleton;
            if (registry && resolvedTarget != entt::null && registry->valid(resolvedTarget)) {
                if (const auto* cache = registry->try_get<CacheSkeletalAnimation>(resolvedTarget)) {
                    targetSkeleton = cache->skeleton;
                }
                if (auto* engine = Runtime::Engine::getInstance(); !targetSkeleton && engine && engine->animation->skeletalAnimationSystem) {
                    if (const auto* anim = registry->try_get<SkeletalAnimatorComponent>(resolvedTarget)) {
                        targetSkeleton = engine->animation->skeletalAnimationSystem->getOrLoadSkeleton(anim->modelUuid);
                    }
                }
            }

            if (targetSkeleton && !targetSkeleton->flatNodes.empty()) {
                const char* bonePreview = component->boneName.empty() ? "(选择骨骼节点)" : component->boneName.c_str();
                if (ImGui::BeginCombo("挂载骨骼##SocketBoneCombo", bonePreview)) {
                    for (const auto& node : targetSkeleton->flatNodes) {
                        if (node.name.empty()) continue;
                        bool isSel = (component->boneName == node.name);
                        if (ImGui::Selectable(node.name.c_str(), isSel) && mutableComp) {
                            mutableComp->boneName = node.name;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                char boneBuf[128] = {};
                std::snprintf(boneBuf, sizeof(boneBuf), "%s", component->boneName.c_str());
                if (ImGui::InputText("挂载骨骼##SocketBoneInput", boneBuf, sizeof(boneBuf)) && mutableComp) {
                    mutableComp->boneName = boneBuf;
                }
            }

            float pos[3] = {component->localPosition.x, component->localPosition.y, component->localPosition.z};
            if (ImGui::DragFloat3("局部偏移##SocketPos", pos, 0.02f) && mutableComp) {
                mutableComp->localPosition = FCT::Vec3(pos[0], pos[1], pos[2]);
            }

            float rot[3] = {component->localRotation.x, component->localRotation.y, component->localRotation.z};
            if (ImGui::DragFloat3("局部旋转##SocketRot", rot, 0.5f) && mutableComp) {
                mutableComp->localRotation = FCT::Vec3(rot[0], rot[1], rot[2]);
            }

            float scl[3] = {component->localScale.x, component->localScale.y, component->localScale.z};
            if (ImGui::DragFloat3("局部缩放##SocketScl", scl, 0.02f) && mutableComp) {
                mutableComp->localScale = FCT::Vec3(scl[0], scl[1], scl[2]);
            }

            bool followRot = component->followRotation;
            if (ImGui::Checkbox("跟随骨骼旋转##SocketFollowRot", &followRot) && mutableComp) {
                mutableComp->followRotation = followRot;
            }
            ImGui::SameLine();
            bool followScl = component->followScale;
            if (ImGui::Checkbox("跟随骨骼缩放##SocketFollowScl", &followScl) && mutableComp) {
                mutableComp->followScale = followScl;
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##SkeletalBoneSocketComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<SkeletalBoneSocketComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<NetworkIdentityComponent>(const NetworkIdentityComponent* component) {
        if (ImGui::CollapsingHeader("网络标识 (Network Identity)##NetIdentity", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            entt::registry* registry = detail::resolveSelectedEntityRegistry();
            NetworkIdentityComponent* mutableComp = (registry && registry->valid(selectedEntity.entity))
                ? registry->try_get<NetworkIdentityComponent>(selectedEntity.entity)
                : nullptr;

            ImGui::Text("Network ID: %d", component->networkId);
            char guidBuf[128] = {};
            std::snprintf(guidBuf, sizeof(guidBuf), "%s", component->networkGuid.c_str());
            if (ImGui::InputText("Network GUID##NetGuid", guidBuf, sizeof(guidBuf)) && mutableComp) {
                mutableComp->networkGuid = guidBuf;
            }

            int ownerId = component->ownerClientId;
            if (ImGui::InputInt("所属客户端 ID##NetOwner", &ownerId) && mutableComp) {
                mutableComp->ownerClientId = ownerId;
            }

            bool serverAuth = component->serverAuthority;
            if (ImGui::Checkbox("服务端权威 (Server Authority)##NetAuth", &serverAuth) && mutableComp) {
                mutableComp->serverAuthority = serverAuth;
            }
            ImGui::SameLine();
            bool autoRep = component->autoReplicate;
            if (ImGui::Checkbox("自动复制##NetAutoRep", &autoRep) && mutableComp) {
                mutableComp->autoReplicate = autoRep;
            }

            char prefabBuf[128] = {};
            std::snprintf(prefabBuf, sizeof(prefabBuf), "%s", component->prefabTag.c_str());
            if (ImGui::InputText("预制体标签##NetPrefab", prefabBuf, sizeof(prefabBuf)) && mutableComp) {
                mutableComp->prefabTag = prefabBuf;
            }

            if (NetworkManager* netMgr = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->network->getNetworkManager() : nullptr) {
                ImGui::Separator();
                ImGui::Text("会话状态: %s | LocalID: %d | 连接数: %d | RTT: %.1f ms",
                    netMgr->getRoleName().c_str(),
                    netMgr->getLocalClientId(),
                    netMgr->getConnectedClientCount(),
                    netMgr->getRttMs());

                static char s_connectHost[64] = "127.0.0.1";
                static int s_connectPort = 7777;
                ImGui::InputText("地址##NetHostQuick", s_connectHost, sizeof(s_connectHost));
                ImGui::InputInt("端口##NetPortQuick", &s_connectPort);
                if (netMgr->getRole() == NetworkRole::Offline) {
                    if (ImGui::Button("启动 Host##NetStartHost")) {
                        netMgr->startHost(s_connectPort);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("启动 Server##NetStartSrv")) {
                        netMgr->startServer(s_connectPort);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("连接 Client##NetConnCli")) {
                        netMgr->connectClient(s_connectHost, s_connectPort);
                    }
                } else {
                    if (ImGui::Button("断开网络会话##NetDisc")) {
                        netMgr->disconnect();
                    }
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##NetworkIdentityComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<NetworkIdentityComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<NetworkTransformComponent>(const NetworkTransformComponent* component) {
        if (ImGui::CollapsingHeader("网络变换同步 (Network Transform)##NetTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            entt::registry* registry = detail::resolveSelectedEntityRegistry();
            NetworkTransformComponent* mutableComp = (registry && registry->valid(selectedEntity.entity))
                ? registry->try_get<NetworkTransformComponent>(selectedEntity.entity)
                : nullptr;

            bool syncPos = component->syncPosition;
            if (ImGui::Checkbox("同步位置##NetSyncPos", &syncPos) && mutableComp) {
                mutableComp->syncPosition = syncPos;
            }
            ImGui::SameLine();
            bool syncRot = component->syncRotation;
            if (ImGui::Checkbox("同步旋转##NetSyncRot", &syncRot) && mutableComp) {
                mutableComp->syncRotation = syncRot;
            }
            ImGui::SameLine();
            bool syncScl = component->syncScale;
            if (ImGui::Checkbox("同步缩放##NetSyncScl", &syncScl) && mutableComp) {
                mutableComp->syncScale = syncScl;
            }

            bool syncAnim = component->syncAnimation;
            if (ImGui::Checkbox("同步骨骼动画状态##NetSyncAnim", &syncAnim) && mutableComp) {
                mutableComp->syncAnimation = syncAnim;
            }

            float sendRate = component->sendRateHz;
            if (ImGui::SliderFloat("发送频率 (Hz)##NetSendRate", &sendRate, 1.0f, 60.0f, "%.1f Hz") && mutableComp) {
                mutableComp->sendRateHz = sendRate;
            }

            float posThresh = component->positionThreshold;
            if (ImGui::DragFloat("位移死区阈值##NetPosThresh", &posThresh, 0.001f, 0.0f, 1.0f, "%.4f") && mutableComp) {
                mutableComp->positionThreshold = posThresh;
            }

            float rotThresh = component->rotationThreshold;
            if (ImGui::DragFloat("旋转死区阈值 (deg)##NetRotThresh", &rotThresh, 0.05f, 0.0f, 15.0f, "%.2f") && mutableComp) {
                mutableComp->rotationThreshold = rotThresh;
            }

            float backTime = component->interpolationBackTime;
            if (ImGui::SliderFloat("插值缓冲延迟 (s)##NetInterpBack", &backTime, 0.0f, 0.5f, "%.3f s") && mutableComp) {
                mutableComp->interpolationBackTime = backTime;
            }

            float interpSpeed = component->interpolationSpeed;
            if (ImGui::SliderFloat("平滑收敛速度##NetInterpSpd", &interpSpeed, 1.0f, 40.0f, "%.1f") && mutableComp) {
                mutableComp->interpolationSpeed = interpSpeed;
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##NetworkTransformComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<NetworkTransformComponent>::value();
            }

            ImGui::Unindent();
        }
    }

    template<>
    inline void renderComponent<NetworkReplicatedComponent>(const NetworkReplicatedComponent* component) {
        if (ImGui::CollapsingHeader("网络反射组件同步 (Network Replicated)##NetReplicated", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();

            auto& selectedEntity = g_editorGlobal.selectedEntity;
            entt::registry* registry = detail::resolveSelectedEntityRegistry();
            NetworkReplicatedComponent* mutableComp = (registry && registry->valid(selectedEntity.entity))
                ? registry->try_get<NetworkReplicatedComponent>(selectedEntity.entity)
                : nullptr;

            float interval = component->syncIntervalSec;
            if (ImGui::DragFloat("同步间隔 (s)##NetRepInterval", &interval, 0.01f, 0.01f, 5.0f, "%.2f s") && mutableComp) {
                mutableComp->syncIntervalSec = interval;
            }

            bool reliable = component->reliableSync;
            if (ImGui::Checkbox("可靠通道同步 (TCP)##NetRepReliable", &reliable) && mutableComp) {
                mutableComp->reliableSync = reliable;
            }

            if (ComponentReflection* refl = (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->foundation) ? Runtime::Engine::getInstance()->foundation->getComponentReflection() : nullptr) {
                if (ImGui::BeginCombo("添加同步组件##NetRepAddCombo", "(选择已注册组件...)")) {
                    for (const auto& name : refl->getRegisteredComponentNames()) {
                        if (name == "NameTag" || name == "NetworkIdentityComponent" || name == "NetworkReplicatedComponent") {
                            continue;
                        }
                        const bool alreadyAdded = std::find(
                            component->componentNames.begin(),
                            component->componentNames.end(),
                            name) != component->componentNames.end();
                        if (!alreadyAdded && ImGui::Selectable(name.c_str(), false) && mutableComp) {
                            mutableComp->componentNames.push_back(name);
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            int removeIdx = -1;
            for (size_t i = 0; i < component->componentNames.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::BulletText("%s", component->componentNames[i].c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("移除")) {
                    removeIdx = static_cast<int>(i);
                }
                ImGui::PopID();
            }
            if (removeIdx >= 0 && mutableComp && static_cast<size_t>(removeIdx) < mutableComp->componentNames.size()) {
                mutableComp->componentNames.erase(mutableComp->componentNames.begin() + removeIdx);
            }

            ImGui::Spacing();
            if (ImGui::Button("删除组件##NetworkReplicatedComponent")) {
                g_editorGlobal.componentToDelete = entt::type_hash<NetworkReplicatedComponent>::value();
            }

            ImGui::Unindent();
        }
    }

} // MQEngine

#endif //COMPONENTRENDERER_H