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
        ZoneScoped();
        ImGui::Begin("实体检查器");
        
        auto& selectedEntity = g_editorGlobal.selectedEntity;
        if (selectedEntity.scene == nullptr || selectedEntity.entity == entt::null) {
            ImGui::Text("未选择实体");
            ImGui::Text("请在场景实体查看器中选择一个实体来查看详情");
        } else {
            renderEntityDetails();
        }
        
        ImGui::End();

        m_inputPopup.render();
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
            registry = selectedEntity.scene->getRegistry();
        } else {
            SceneTrunk* trunk = selectedEntity.scene->getLoadedTrunk(selectedEntity.trunkName);
            if (trunk) {
                registry = trunk->getRegistry();
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

            if (ImGui::MenuItem("Skeletal Animator Component")) {
                if (!registry->all_of<SkeletalAnimatorComponent>(selectedEntity.entity)) {
                    std::string modelUuid;
                    if (const auto* meshInst = registry->try_get<StaticMeshInstance>(selectedEntity.entity)) {
                        modelUuid = meshInst->modelUuid;
                    }
                    registry->emplace<SkeletalAnimatorComponent>(selectedEntity.entity, SkeletalAnimatorComponent{modelUuid});
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Skeletal Bone Socket")) {
                if (!registry->all_of<SkeletalBoneSocketComponent>(selectedEntity.entity)) {
                    registry->emplace<SkeletalBoneSocketComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Network Identity Component")) {
                if (!registry->all_of<NetworkIdentityComponent>(selectedEntity.entity)) {
                    auto& netId = registry->emplace<NetworkIdentityComponent>(selectedEntity.entity);
                    if (const auto* tag = registry->try_get<NameTag>(selectedEntity.entity)) {
                        netId.networkGuid = tag->uuid;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Network Transform Component")) {
                if (!registry->all_of<NetworkTransformComponent>(selectedEntity.entity)) {
                    registry->emplace<NetworkTransformComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Network Replicated Component")) {
                if (!registry->all_of<NetworkReplicatedComponent>(selectedEntity.entity)) {
                    registry->emplace<NetworkReplicatedComponent>(selectedEntity.entity);
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

            if (ImGui::MenuItem("Point Light Component")) {
                if (!registry->all_of<PointLightComponent>(selectedEntity.entity)) {
                    registry->emplace<PointLightComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("PBR Material Component")) {
                if (!registry->all_of<PBRMaterialComponent>(selectedEntity.entity)) {
                    registry->emplace<PBRMaterialComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Diffuse Texture Component")) {
                if (!registry->all_of<DiffuseTextureComponent>(selectedEntity.entity)) {
                    registry->emplace<DiffuseTextureComponent>(selectedEntity.entity, "", "");
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Shininess Component")) {
                if (!registry->all_of<ShininessComponent>(selectedEntity.entity)) {
                    registry->emplace<ShininessComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("NormalTextureComponent")) {
                if (!registry->all_of<NormalTextureComponent>(selectedEntity.entity)) {
                    registry->emplace<NormalTextureComponent>(selectedEntity.entity, "", "");
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("EmissiveTextureComponent")) {
                if (!registry->all_of<EmissiveTextureComponent>(selectedEntity.entity)) {
                    registry->emplace<EmissiveTextureComponent>(selectedEntity.entity, "", "");
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("OrmTextureComponent")) {
                if (!registry->all_of<OrmTextureComponent>(selectedEntity.entity)) {
                    registry->emplace<OrmTextureComponent>(selectedEntity.entity, "", "");
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Script Function Table")) {
                if (!registry->all_of<ScriptFunctionTableComponent>(selectedEntity.entity)) {
                    registry->emplace<ScriptFunctionTableComponent>(selectedEntity.entity);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Skybox Component")) {
                if (!registry->all_of<SkyboxComponent>(selectedEntity.entity)) {
                    m_inputPopup.open("设置 Skybox 路径", "Skybox 路径", "Assets/Textures/skybox/",
                    [registry, selectedEntity](const std::string& path) {
                        if (registry->valid(selectedEntity.entity) && !registry->all_of<SkyboxComponent>(selectedEntity.entity)) {
                            registry->emplace<SkyboxComponent>(selectedEntity.entity, path);
                        }
                    }, "路径应包含6个面: right.jpg, left.jpg, ...");
                }
                ImGui::CloseCurrentPopup();
            }

            renderDynamicComponentMenuItems(registry, selectedEntity.entity);
            
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

    namespace {
        bool IsBuiltinInspectorComponent(const std::string& compName) {
            static const std::unordered_set<std::string> kBuiltins = {
                "NameTag",
                "PositionComponent",
                "RotationComponent",
                "ScaleComponent",
                "CameraComponent",
                "CacheRotationMatrix",
                "CacheModelMatrix",
                "StaticMeshInstance",
                "SkeletalAnimatorComponent",
                "SkeletalBoneSocketComponent",
                "CacheSkeletalAnimation",
                "NetworkIdentityComponent",
                "NetworkTransformComponent",
                "NetworkReplicatedComponent",
                "CacheNetworkSyncComponent",
                "HierarchyComponent",
                "TickerScriptComponent",
                "OnStartScriptComponent",
                "DirectionalLightComponent",
                "PointLightComponent",
                "PBRMaterialComponent",
                "DiffuseTextureComponent",
                "ShininessComponent",
                "AlbedoTextureComponent",
                "NormalTextureComponent",
                "EmissiveTextureComponent",
                "OrmTextureComponent",
                "ScriptFunctionTableComponent",
                "CacheScriptComponent",
                "SkyboxComponent",
                "CacheSkyboxComponent"
            };
            return kBuiltins.count(compName) > 0;
        }
    } // namespace

    void EntityInspector::renderDynamicComponentMenuItems(entt::registry* registry, entt::entity entity)
    {
        ComponentReflection* refl = (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->foundation) ? Runtime::Engine::getInstance()->foundation->getComponentReflection() : nullptr;
        if (!refl || !registry || !registry->valid(entity)) {
            return;
        }

        bool hasSeparator = false;
        for (const auto& compName : refl->getRegisteredComponentNames()) {
            if (IsBuiltinInspectorComponent(compName)) {
                continue;
            }
            if (!hasSeparator) {
                ImGui::Separator();
                ImGui::TextDisabled("UserCode / Plugin 组件:");
                hasSeparator = true;
            }
            if (ImGui::MenuItem(compName.c_str())) {
                if (!refl->hasComponent(*registry, entity, compName)) {
                    refl->addComponent(*registry, entity, compName);
                }
                ImGui::CloseCurrentPopup();
            }
        }
    }

    bool EntityInspector::renderDynamicComponents(entt::registry* registry, entt::entity entity)
    {
        ComponentReflection* refl = (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->foundation) ? Runtime::Engine::getInstance()->foundation->getComponentReflection() : nullptr;
        if (!refl || !registry || !registry->valid(entity)) {
            return false;
        }

        bool renderedAny = false;
        std::string compToRemove;

        for (const auto& compName : refl->getRegisteredComponentNames()) {
            if (IsBuiltinInspectorComponent(compName) || !refl->hasComponent(*registry, entity, compName)) {
                continue;
            }
            renderedAny = true;

            if (ImGui::CollapsingHeader(compName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::PushID(compName.c_str());

                for (const auto& fieldName : refl->getComponentFieldNames(compName)) {
                    std::string fieldType = refl->getComponentFieldType(compName, fieldName);
                    try {
                        ComponentValue val = refl->getComponentField(*registry, entity, compName, fieldName);
                        if (fieldType == "bool" && std::holds_alternative<bool>(val)) {
                            bool bVal = std::get<bool>(val);
                            if (ImGui::Checkbox(fieldName.c_str(), &bVal)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, bVal);
                            }
                        } else if (fieldType == "int" && std::holds_alternative<int>(val)) {
                            int iVal = std::get<int>(val);
                            if (ImGui::DragInt(fieldName.c_str(), &iVal)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, iVal);
                            }
                        } else if (fieldType == "float" && std::holds_alternative<float>(val)) {
                            float fVal = std::get<float>(val);
                            if (ImGui::DragFloat(fieldName.c_str(), &fVal, 0.1f)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, fVal);
                            }
                        } else if (fieldType == "double" && std::holds_alternative<double>(val)) {
                            double dVal = std::get<double>(val);
                            if (ImGui::InputDouble(fieldName.c_str(), &dVal, 0.0001, 0.01, "%.7f")) {
                                refl->setComponentField(*registry, entity, compName, fieldName, dVal);
                            }
                        } else if ((fieldType == "string" || fieldType == "std::string") && std::holds_alternative<std::string>(val)) {
                            std::string sVal = std::get<std::string>(val);
                            char buf[256] = {};
                            std::snprintf(buf, sizeof(buf), "%s", sVal.c_str());
                            if (ImGui::InputText(fieldName.c_str(), buf, sizeof(buf))) {
                                refl->setComponentField(*registry, entity, compName, fieldName, std::string(buf));
                            }
                        } else if (fieldType == "Vec2" && std::holds_alternative<FCT::Vec2>(val)) {
                            FCT::Vec2 v = std::get<FCT::Vec2>(val);
                            float arr[2] = { v.x, v.y };
                            if (ImGui::DragFloat2(fieldName.c_str(), arr, 0.1f)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, FCT::Vec2(arr[0], arr[1]));
                            }
                        } else if (fieldType == "Vec3" && std::holds_alternative<FCT::Vec3<>>(val)) {
                            FCT::Vec3<> v = std::get<FCT::Vec3<>>(val);
                            float arr[3] = { v.x, v.y, v.z };
                            if (ImGui::DragFloat3(fieldName.c_str(), arr, 0.1f)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, FCT::Vec3(arr[0], arr[1], arr[2]));
                            }
                        } else if (fieldType == "Vec4" && std::holds_alternative<FCT::Vec4>(val)) {
                            FCT::Vec4 v = std::get<FCT::Vec4>(val);
                            float arr[4] = { v.x, v.y, v.z, v.w };
                            if (ImGui::DragFloat4(fieldName.c_str(), arr, 0.1f)) {
                                refl->setComponentField(*registry, entity, compName, fieldName, FCT::Vec4(arr[0], arr[1], arr[2], arr[3]));
                            }
                        }
                    } catch (...) {
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button("删除组件")) {
                    compToRemove = compName;
                }

                ImGui::PopID();
                ImGui::Unindent();
            }
        }

        if (!compToRemove.empty()) {
            refl->removeComponent(*registry, entity, compToRemove);
        }

        return renderedAny;
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
        hasComponents |= tryRenderComponent<SkeletalAnimatorComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<SkeletalBoneSocketComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<NetworkIdentityComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<NetworkTransformComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<NetworkReplicatedComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<TickerScriptComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<OnStartScriptComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<DirectionalLightComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<PointLightComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<PBRMaterialComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<DiffuseTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<ShininessComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<AlbedoTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<NormalTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<EmissiveTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<OrmTextureComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<ScriptFunctionTableComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<CacheScriptComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<SkyboxComponent>(registry, selectedEntity.entity);
        hasComponents |= tryRenderComponent<CacheSkyboxComponent>(registry, selectedEntity.entity);
        hasComponents |= renderDynamicComponents(registry, selectedEntity.entity);

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
