#ifndef EDITOR_COMPONENT_UI_WIDGET_H
#define EDITOR_COMPONENT_UI_WIDGET_H

#include "UiContext.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    template <typename T>
    void EmitRemoveComponentButton(UiContext& ctx, const char* buttonIdSuffix) {
        ImGui::Spacing();
        std::string label = std::string(TEXT("删除组件##")) + buttonIdSuffix;
        if (ImGui::Button(label.c_str())) {
            ctx.emit(RemoveComponentSignal{
                .componentTypeId = entt::type_hash<T>::value()
            });
        }
    }

    inline bool ParseInspectorPipePayload(const ImGuiPayload* payload,
                                          std::string& outFirst,
                                          std::string& outSecond) {
        if (!payload || !payload->Data) {
            return false;
        }
        std::string dragData = static_cast<const char*>(payload->Data);
        size_t separatorPos = dragData.find('|');
        if (separatorPos == std::string::npos) {
            return false;
        }
        outFirst = dragData.substr(0, separatorPos);
        outSecond = dragData.substr(separatorPos + 1);
        return true;
    }

    template <typename T>
    inline void RenderTextureComponentReadonlyFields(UiContext& ctx, const T& component, TextureBindSlot slot) {
        ImGui::BeginGroup();
        ImGui::Text(TEXT("模型UUID: %s"), component.modelUuid.empty() ? TEXT("未设置") : component.modelUuid.c_str());
        ImGui::Text(TEXT("纹理路径: %s"), component.texturePath.empty() ? TEXT("未设置") : component.texturePath.c_str());
        ImGui::Text(TEXT("纹理状态: %s"), component.texture ? TEXT("已加载") : TEXT("未加载"));
        if constexpr (requires { component.visible; }) {
            ImGui::Text(TEXT("纹理启用: %s"), component.visible ? TEXT("已启用") : TEXT("未启用"));
        }
        ImGui::TextDisabled(TEXT("[可将模型管理器中的纹理直接拖拽到此处替换]"));
        ImGui::EndGroup();

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
                std::string modelUuid;
                std::string texturePath;
                const auto& inspState = ctx.state<EntityInspectorViewState>();
                if (inspState.hasSelectedEntity && ParseInspectorPipePayload(payload, modelUuid, texturePath)) {
                    ctx.emit(BindTextureToEntitySignal{
                        .entity = inspState.entity,
                        .trunkName = inspState.trunkName,
                        .isGlobal = inspState.isGlobal,
                        .modelUuid = std::move(modelUuid),
                        .texturePath = std::move(texturePath),
                        .slot = slot
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    template <typename T>
    struct ComponentUiTraits;

    template <>
    struct ComponentUiTraits<NameTag> {
        static void render(UiContext& ctx, const NameTag& component) {
            if (ImGui::CollapsingHeader("NameTag", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                char nameBuf[128];
                std::strncpy(nameBuf, component.name.c_str(), sizeof(nameBuf));
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText(TEXT("名称##EntityNameInput"), nameBuf, sizeof(nameBuf))) {
                    NameTag updated = component;
                    updated.name = nameBuf;
                    ctx.emit(UpdateComponentSignal<NameTag>{ .value = std::move(updated) });
                }
                if (!component.uuid.empty()) {
                    ImGui::TextDisabled("UUID: %s", component.uuid.c_str());
                }
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<HierarchyComponent> {
        static void render(UiContext& ctx, const HierarchyComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Hierarchy (场景图父子关系)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                entt::entity parentEnt = static_cast<entt::entity>(component.parentEntity);
                if (parentEnt != entt::null) {
                    ImGui::Text(TEXT("父节点 Entity ID: %u"), static_cast<uint32_t>(parentEnt));
                } else {
                    ImGui::Text(TEXT("父节点: <根层级>"));
                }
                if (!component.parentUuid.empty()) {
                    ImGui::TextDisabled("Parent UUID: %s", component.parentUuid.c_str());
                }
                ImGui::Text(TEXT("子节点数量: %zu"), component.childrenUuids.size());

                const auto& inspState = ctx.state<EntityInspectorViewState>();
                if (parentEnt != entt::null && inspState.hasSelectedEntity) {
                    if (ImGui::SmallButton(TEXT("解除父子关系 (移至根层级)"))) {
                        ctx.emit(ReparentSceneEntitySignal{
                            .childEntity = inspState.entity,
                            .parentEntity = entt::null,
                            .trunkName = inspState.trunkName,
                            .isGlobal = inspState.isGlobal
                        });
                    }
                }
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<PositionComponent> {
        static constexpr const char* menuLabel = "Position Component";

        static void render(UiContext& ctx, const PositionComponent& component) {
            if (ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                float pos[3] = { component.position.x, component.position.y, component.position.z };
                if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                    PositionComponent updated = component;
                    updated.position = FCT::Vec3(pos[0], pos[1], pos[2]);
                    ctx.emit(UpdateComponentSignal<PositionComponent>{ .value = updated });
                }
                EmitRemoveComponentButton<PositionComponent>(ctx, "PositionComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<RotationComponent> {
        static constexpr const char* menuLabel = "Rotation Component";

        static void render(UiContext& ctx, const RotationComponent& component) {
            if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                float rot[3] = { component.rotation.x, component.rotation.y, component.rotation.z };
                if (ImGui::DragFloat3("Rotation (degrees)", rot, 1.0f)) {
                    RotationComponent updated = component;
                    updated.rotation = FCT::Vec3(rot[0], rot[1], rot[2]);
                    ctx.emit(UpdateComponentSignal<RotationComponent>{ .value = updated });
                }
                EmitRemoveComponentButton<RotationComponent>(ctx, "RotationComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<ScaleComponent> {
        static constexpr const char* menuLabel = "Scale Component";

        static void render(UiContext& ctx, const ScaleComponent& component) {
            if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                float scale[3] = { component.scale.x, component.scale.y, component.scale.z };
                if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.001f, 100.0f)) {
                    ScaleComponent updated = component;
                    updated.scale = FCT::Vec3(scale[0], scale[1], scale[2]);
                    ctx.emit(UpdateComponentSignal<ScaleComponent>{ .value = updated });
                }
                EmitRemoveComponentButton<ScaleComponent>(ctx, "ScaleComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CameraComponent> {
        static constexpr const char* menuLabel = "Camera Component";

        static void render(UiContext& ctx, const CameraComponent& component) {
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::Text(TEXT("激活: %s"), component.active ? TEXT("是") : TEXT("否"));
                CameraComponent updated = component;
                bool changed = false;
                changed |= ImGui::DragFloat(TEXT("视野角度 (FOV)"), &updated.fov, 0.5f, 10.0f, 160.0f);
                changed |= ImGui::DragFloat(TEXT("近平面 (Near)"), &updated.nearPlane, 0.01f, 0.01f, 1000.0f);
                changed |= ImGui::DragFloat(TEXT("远平面 (Far)"), &updated.farPlane, 1000.0f, 1.0f, 20000000.0f);
                changed |= ImGui::Checkbox(TEXT("Reverse-Z 深度缓冲"), &updated.reverseZ);
                if (changed) {
                    ctx.emit(UpdateComponentSignal<CameraComponent>{ .value = updated });
                }
                EmitRemoveComponentButton<CameraComponent>(ctx, "CameraComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<StaticMeshInstance> {
        static constexpr const char* menuLabel = "Static Mesh Instance";

        static void render(UiContext& ctx, const StaticMeshInstance& component) {
            if (ImGui::CollapsingHeader("StaticMeshInstance", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::BeginGroup();
                ImGui::Text(TEXT("模型UUID: %s"), component.modelUuid.empty() ? TEXT("未设置") : component.modelUuid.c_str());
                ImGui::Text(TEXT("网格体名称: %s"), component.meshName.empty() ? TEXT("未设置") : component.meshName.c_str());
                ImGui::Text(TEXT("网格状态: %s"), component.mesh ? TEXT("已加载") : TEXT("未加载"));
                ImGui::TextDisabled(TEXT("[可将模型管理器中的网格直接拖拽到此处替换]"));
                ImGui::EndGroup();

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                        std::string modelUuid;
                        std::string meshName;
                        const auto& inspState = ctx.state<EntityInspectorViewState>();
                        if (inspState.hasSelectedEntity && ParseInspectorPipePayload(payload, modelUuid, meshName)) {
                            ctx.emit(BindMeshToEntitySignal{
                                .entity = inspState.entity,
                                .trunkName = inspState.trunkName,
                                .isGlobal = inspState.isGlobal,
                                .modelUuid = std::move(modelUuid),
                                .meshName = std::move(meshName)
                            });
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                EmitRemoveComponentButton<StaticMeshInstance>(ctx, "StaticMeshInstance");
                ImGui::Unindent();
            }
        }
    };

    inline bool RenderScriptFunctionSelector(const char* label, std::string& ioFuncName) {
        bool changed = false;
        std::vector<std::string> funcNames;
        if (auto* engine = Runtime::Engine::getInstance(); engine && engine->script->scriptSystem) {
            funcNames = engine->script->scriptSystem->getFunctionNames();
        }

        const char* preview = ioFuncName.empty() ? TEXT("<选择脚本函数>") : ioFuncName.c_str();
        std::string comboLabel = std::string(TEXT("选择函数##")) + label;
        if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
            if (ImGui::Selectable(TEXT("<清空>"), ioFuncName.empty())) {
                ioFuncName.clear();
                changed = true;
            }
            for (const auto& name : funcNames) {
                bool isSelected = (ioFuncName == name);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    ioFuncName = name;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        char buffer[128];
        std::strncpy(buffer, ioFuncName.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';
        std::string inputLabel = std::string(TEXT("函数名##")) + label;
        if (ImGui::InputText(inputLabel.c_str(), buffer, sizeof(buffer))) {
            ioFuncName = buffer;
            changed = true;
        }
        return changed;
    }

    template <>
    struct ComponentUiTraits<TickerScriptComponent> {
        static constexpr const char* menuLabel = "Ticker Script Component";

        static void render(UiContext& ctx, const TickerScriptComponent& component) {
            if (ImGui::CollapsingHeader("Ticker Script", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                TickerScriptComponent updated = component;
                if (RenderScriptFunctionSelector("TickerScript", updated.functionName)) {
                    ctx.emit(UpdateComponentSignal<TickerScriptComponent>{ .value = std::move(updated) });
                }
                EmitRemoveComponentButton<TickerScriptComponent>(ctx, "TickerScriptComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<OnStartScriptComponent> {
        static constexpr const char* menuLabel = "OnStart Script Component";

        static void render(UiContext& ctx, const OnStartScriptComponent& component) {
            if (ImGui::CollapsingHeader("OnStart Script", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                OnStartScriptComponent updated = component;
                if (RenderScriptFunctionSelector("OnStartScript", updated.functionName)) {
                    ctx.emit(UpdateComponentSignal<OnStartScriptComponent>{ .value = std::move(updated) });
                }
                EmitRemoveComponentButton<OnStartScriptComponent>(ctx, "OnStartScriptComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CacheRotationMatrix> {
        static void render(UiContext& ctx, const CacheRotationMatrix& component) {
            (void)ctx;
            if (ImGui::CollapsingHeader("CacheRotationMatrix", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::Text(TEXT("旋转矩阵 (缓存)"));
                ImGui::Text("[%.3f, %.3f, %.3f, %.3f]",
                    component.rotationMatrix.m[0], component.rotationMatrix.m[1],
                    component.rotationMatrix.m[2], component.rotationMatrix.m[3]);
                ImGui::Text("[%.3f, %.3f, %.3f, %.3f]",
                    component.rotationMatrix.m[4], component.rotationMatrix.m[5],
                    component.rotationMatrix.m[6], component.rotationMatrix.m[7]);
                ImGui::Text("[%.3f, %.3f, %.3f, %.3f]",
                    component.rotationMatrix.m[8], component.rotationMatrix.m[9],
                    component.rotationMatrix.m[10], component.rotationMatrix.m[11]);
                ImGui::Text("[%.3f, %.3f, %.3f, %.3f]",
                    component.rotationMatrix.m[12], component.rotationMatrix.m[13],
                    component.rotationMatrix.m[14], component.rotationMatrix.m[15]);
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CacheModelMatrix> {
        static void render(UiContext& ctx, const CacheModelMatrix& component) {
            (void)ctx;
            if (ImGui::CollapsingHeader("CacheModelMatrix", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::Text(TEXT("模型矩阵 (缓存)"));
                ImGui::Text(TEXT("Uniform已缓存并上传到GPU"));
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<DirectionalLightComponent> {
        static constexpr const char* menuLabel = "Directional Light Component";

        static void render(UiContext& ctx, const DirectionalLightComponent& component) {
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                DirectionalLightComponent updated = component;
                bool changed = false;

                float direction[3] = { component.direction.x, component.direction.y, component.direction.z };
                if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f)) {
                    updated.direction = FCT::Vec3(direction[0], direction[1], direction[2]);
                    changed = true;
                }

                float color[3] = { component.color.x, component.color.y, component.color.z };
                if (ImGui::ColorEdit3("Color", color)) {
                    updated.color = FCT::Vec3(color[0], color[1], color[2]);
                    changed = true;
                }

                if (ImGui::SliderFloat("Intensity", &updated.intensity, 0.0f, 5.0f)) {
                    changed = true;
                }

                if (ImGui::Checkbox("Enabled", &updated.enabled)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<DirectionalLightComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<DirectionalLightComponent>(ctx, "DirectionalLightComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<PointLightComponent> {
        static constexpr const char* menuLabel = "Point Light Component";

        static void render(UiContext& ctx, const PointLightComponent& component) {
            if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                PointLightComponent updated = component;
                bool changed = false;

                float color[3] = { component.color.x, component.color.y, component.color.z };
                if (ImGui::ColorEdit3("Color##PointLight", color)) {
                    updated.color = FCT::Vec3(color[0], color[1], color[2]);
                    changed = true;
                }

                if (ImGui::DragFloat("Intensity##PointLight", &updated.intensity, 0.05f, 0.0f, 100.0f)) {
                    changed = true;
                }

                if (ImGui::DragFloat("Range##PointLight", &updated.range, 0.1f, 0.1f, 500.0f)) {
                    changed = true;
                }

                float atten[3] = { component.constant, component.linear, component.quadratic };
                if (ImGui::DragFloat3("Attenuation (C/L/Q)##PointLight", atten, 0.005f, 0.0f, 10.0f)) {
                    updated.constant = atten[0];
                    updated.linear = atten[1];
                    updated.quadratic = atten[2];
                    changed = true;
                }

                if (ImGui::Checkbox("Enabled##PointLight", &updated.enabled)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<PointLightComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<PointLightComponent>(ctx, "PointLightComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<PBRMaterialComponent> {
        static constexpr const char* menuLabel = "PBR Material Component";

        static void render(UiContext& ctx, const PBRMaterialComponent& component) {
            if (ImGui::CollapsingHeader("PBR Material", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                PBRMaterialComponent updated = component;
                bool changed = false;

                float baseColor[3] = { component.baseColor.x, component.baseColor.y, component.baseColor.z };
                if (ImGui::ColorEdit3("Base Color##PBRMat", baseColor)) {
                    updated.baseColor = FCT::Vec3(baseColor[0], baseColor[1], baseColor[2]);
                    changed = true;
                }

                if (ImGui::SliderFloat("Metallic##PBRMat", &updated.metallic, 0.0f, 1.0f)) {
                    changed = true;
                }

                if (ImGui::SliderFloat("Roughness##PBRMat", &updated.roughness, 0.04f, 1.0f)) {
                    changed = true;
                }

                if (ImGui::SliderFloat("Ambient Occlusion##PBRMat", &updated.ao, 0.0f, 1.0f)) {
                    changed = true;
                }

                float emissive[3] = { component.emissive.x, component.emissive.y, component.emissive.z };
                if (ImGui::ColorEdit3("Emissive##PBRMat", emissive)) {
                    updated.emissive = FCT::Vec3(emissive[0], emissive[1], emissive[2]);
                    changed = true;
                }

                char sgBuffer[128];
                std::strncpy(sgBuffer, component.shaderGraphName.c_str(), sizeof(sgBuffer));
                sgBuffer[sizeof(sgBuffer) - 1] = '\0';
                if (ImGui::InputText("ShaderGraph##PBRMat", sgBuffer, sizeof(sgBuffer))) {
                    updated.shaderGraphName = sgBuffer;
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<PBRMaterialComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<PBRMaterialComponent>(ctx, "PBRMaterialComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<DiffuseTextureComponent> {
        static constexpr const char* menuLabel = "Diffuse Texture Component";

        static void render(UiContext& ctx, const DiffuseTextureComponent& component) {
            if (ImGui::CollapsingHeader("Diffuse Texture", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RenderTextureComponentReadonlyFields(ctx, component, TextureBindSlot::Diffuse);
                EmitRemoveComponentButton<DiffuseTextureComponent>(ctx, "DiffuseTextureComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<AlbedoTextureComponent> {
        static void render(UiContext& ctx, const AlbedoTextureComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Albedo 纹理##AlbedoTexture"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RenderTextureComponentReadonlyFields(ctx, component, TextureBindSlot::Albedo);
                EmitRemoveComponentButton<AlbedoTextureComponent>(ctx, "AlbedoTextureComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<ShininessComponent> {
        static constexpr const char* menuLabel = "Shininess Component";

        static void render(UiContext& ctx, const ShininessComponent& component) {
            if (ImGui::CollapsingHeader("Shininess", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ShininessComponent updated = component;
                if (ImGui::DragFloat("Shininess", &updated.shininess, 1.0f, 1.0f, 256.0f)) {
                    ctx.emit(UpdateComponentSignal<ShininessComponent>{ .value = updated });
                }
                EmitRemoveComponentButton<ShininessComponent>(ctx, "ShininessComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<NormalTextureComponent> {
        static constexpr const char* menuLabel = "NormalTextureComponent";

        static void render(UiContext& ctx, const NormalTextureComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Normal 纹理##NormalTexture"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RenderTextureComponentReadonlyFields(ctx, component, TextureBindSlot::Normal);
                EmitRemoveComponentButton<NormalTextureComponent>(ctx, "NormalTextureComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<EmissiveTextureComponent> {
        static constexpr const char* menuLabel = "EmissiveTextureComponent";

        static void render(UiContext& ctx, const EmissiveTextureComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Emissive 纹理##EmissiveTexture"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RenderTextureComponentReadonlyFields(ctx, component, TextureBindSlot::Emissive);
                EmitRemoveComponentButton<EmissiveTextureComponent>(ctx, "EmissiveTextureComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<OrmTextureComponent> {
        static constexpr const char* menuLabel = "OrmTextureComponent";

        static void render(UiContext& ctx, const OrmTextureComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Orm 纹理##OrmTexture"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RenderTextureComponentReadonlyFields(ctx, component, TextureBindSlot::Orm);
                EmitRemoveComponentButton<OrmTextureComponent>(ctx, "OrmTextureComponent");
                ImGui::Unindent();
            }
        }
    };

    inline bool RenderScriptClassSelector(std::string& ioClassName) {
        bool changed = false;
        std::vector<std::string> classNames;
        if (auto* engine = Runtime::Engine::getInstance(); engine && engine->script->scriptSystem) {
            classNames = engine->script->scriptSystem->getClassNames();
        }

        const char* preview = ioClassName.empty() ? TEXT("<无 / 仅使用函数表>") : ioClassName.c_str();
        if (ImGui::BeginCombo(TEXT("选择脚本类##ScriptClassCombo"), preview)) {
            if (ImGui::Selectable(TEXT("<无 / 仅使用函数表>"), ioClassName.empty())) {
                ioClassName.clear();
                changed = true;
            }
            for (const auto& name : classNames) {
                bool isSelected = (ioClassName == name);
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    ioClassName = name;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        char classBuf[128];
        std::strncpy(classBuf, ioClassName.c_str(), sizeof(classBuf));
        classBuf[sizeof(classBuf) - 1] = '\0';
        if (ImGui::InputText(TEXT("脚本类名##ScriptClassInput"), classBuf, sizeof(classBuf))) {
            ioClassName = classBuf;
            changed = true;
        }
        return changed;
    }

    template <>
    struct ComponentUiTraits<ScriptFunctionTableComponent> {
        static constexpr const char* menuLabel = "Script Component (TS/JS)";

        static void render(UiContext& ctx, const ScriptFunctionTableComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("Script (TS/JS 脚本绑定)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                ScriptFunctionTableComponent updated = component;
                bool changed = false;

                ImGui::Text(TEXT("绑定脚本类 (ScriptBehaviour):"));
                if (RenderScriptClassSelector(updated.className)) {
                    changed = true;
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text(TEXT("附加全局 Ticker 函数列表:"));

                std::vector<std::string> funcNames;
                if (auto* engine = Runtime::Engine::getInstance(); engine && engine->script->scriptSystem) {
                    funcNames = engine->script->scriptSystem->getFunctionNames();
                }

                for (int i = 0; i < static_cast<int>(updated.onTicker.size()); ++i) {
                    ImGui::PushID(i);

                    const char* preview = updated.onTicker[i].empty() ? TEXT("<选择函数>") : updated.onTicker[i].c_str();
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo("##funcCombo", preview)) {
                        for (const auto& fn : funcNames) {
                            bool isSelected = (updated.onTicker[i] == fn);
                            if (ImGui::Selectable(fn.c_str(), isSelected)) {
                                updated.onTicker[i] = fn;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();

                    char buffer[128];
                    std::strncpy(buffer, updated.onTicker[i].c_str(), sizeof(buffer));
                    buffer[sizeof(buffer) - 1] = '\0';
                    ImGui::SetNextItemWidth(140.0f);
                    if (ImGui::InputText("##funcInput", buffer, sizeof(buffer))) {
                        updated.onTicker[i] = buffer;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(TEXT("移除"))) {
                        updated.onTicker.erase(updated.onTicker.begin() + i);
                        changed = true;
                        --i;
                    }

                    ImGui::PopID();
                }

                if (ImGui::Button(TEXT("添加 Ticker 函数"))) {
                    updated.onTicker.push_back(funcNames.empty() ? "NewFunction" : funcNames.front());
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(TEXT("刷新脚本列表"))) {
                    if (auto* engine = Runtime::Engine::getInstance(); engine && engine->script->scriptSystem) {
                        engine->script->scriptSystem->reloadScripts();
                    }
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<ScriptFunctionTableComponent>{ .value = std::move(updated) });
                }

                EmitRemoveComponentButton<ScriptFunctionTableComponent>(ctx, "ScriptFunctionTableComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CacheScriptComponent> {
        static void render(UiContext& ctx, const CacheScriptComponent& component) {
            if (ImGui::CollapsingHeader("Cache Script (Runtime)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                if (component.m_jsObject) {
                    ImGui::Text("JS Object: Valid (%p)", component.m_jsObject);
                } else {
                    ImGui::Text("JS Object: Null");
                }
                ImGui::Text(TEXT("Note: 运行时缓存对象，不可直接编辑。"));
                EmitRemoveComponentButton<CacheScriptComponent>(ctx, "CacheScriptComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<SkyboxComponent> {
        static void render(UiContext& ctx, const SkyboxComponent& component) {
            if (ImGui::CollapsingHeader("Skybox Config", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                char buffer[256];
                std::strncpy(buffer, component.texturePath.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                ImGui::Text("Texture Path (Directory):");
                if (ImGui::InputText("##skyboxPath", buffer, sizeof(buffer))) {
                    SkyboxComponent updated = component;
                    updated.texturePath = buffer;
                    ctx.emit(UpdateComponentSignal<SkyboxComponent>{ .value = std::move(updated) });
                }

                EmitRemoveComponentButton<SkyboxComponent>(ctx, "SkyboxComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CacheSkyboxComponent> {
        static void render(UiContext& ctx, const CacheSkyboxComponent& component) {
            (void)ctx;
            if (ImGui::CollapsingHeader("Skybox Cache (Runtime)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                ImGui::Text("Texture Loaded: %s", component.texture ? "Yes" : "No");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<RigidBodyComponent> {
        static constexpr const char* menuLabel = "RigidBody Component";

        static void render(UiContext& ctx, const RigidBodyComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("刚体 (RigidBody)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                RigidBodyComponent updated = component;
                bool changed = false;

                const char* bodyTypes[] = { TEXT("静态 (Static)"), TEXT("动态 (Dynamic)"), TEXT("运动学 (Kinematic)") };
                int currentType = static_cast<int>(updated.bodyType);
                if (ImGui::Combo(TEXT("刚体类型"), &currentType, bodyTypes, IM_ARRAYSIZE(bodyTypes))) {
                    updated.bodyType = static_cast<RigidBodyType>(currentType);
                    changed = true;
                }

                if (ImGui::DragFloat(TEXT("质量 (kg)"), &updated.mass, 0.1f, 0.001f, 10000.0f)) {
                    changed = true;
                }
                if (ImGui::Checkbox(TEXT("启用重力"), &updated.useGravity)) {
                    changed = true;
                }
                if (ImGui::DragFloat(TEXT("线性阻尼"), &updated.linearDamping, 0.01f, 0.0f, 10.0f)) {
                    changed = true;
                }
                if (ImGui::DragFloat(TEXT("角阻尼"), &updated.angularDamping, 0.01f, 0.0f, 10.0f)) {
                    changed = true;
                }

                ImGui::Text(TEXT("位置锁定 (Freeze Position):"));
                if (ImGui::Checkbox("X##FrzPosX", &updated.freezePositionX)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Y##FrzPosY", &updated.freezePositionY)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Z##FrzPosZ", &updated.freezePositionZ)) changed = true;

                ImGui::Text(TEXT("旋转锁定 (Freeze Rotation):"));
                if (ImGui::Checkbox("X##FrzRotX", &updated.freezeRotationX)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Y##FrzRotY", &updated.freezeRotationY)) changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Z##FrzRotZ", &updated.freezeRotationZ)) changed = true;

                if (changed) {
                    ctx.emit(UpdateComponentSignal<RigidBodyComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<RigidBodyComponent>(ctx, "RigidBodyComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<BoxColliderComponent> {
        static constexpr const char* menuLabel = "BoxCollider Component";

        static void render(UiContext& ctx, const BoxColliderComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("盒体碰撞体 (Box Collider)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                BoxColliderComponent updated = component;
                bool changed = false;

                float ext[3] = { updated.halfExtents.x, updated.halfExtents.y, updated.halfExtents.z };
                if (ImGui::DragFloat3(TEXT("半尺寸 (Half Extents)"), ext, 0.05f, 0.001f, 1000.0f)) {
                    updated.halfExtents = FCT::Vec3<>(ext[0], ext[1], ext[2]);
                    changed = true;
                }
                float center[3] = { updated.center.x, updated.center.y, updated.center.z };
                if (ImGui::DragFloat3(TEXT("偏移中心 (Center)"), center, 0.05f)) {
                    updated.center = FCT::Vec3<>(center[0], center[1], center[2]);
                    changed = true;
                }
                if (ImGui::Checkbox(TEXT("触发器 (Is Trigger)"), &updated.isTrigger)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<BoxColliderComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<BoxColliderComponent>(ctx, "BoxColliderComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<SphereColliderComponent> {
        static constexpr const char* menuLabel = "SphereCollider Component";

        static void render(UiContext& ctx, const SphereColliderComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("球体碰撞体 (Sphere Collider)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                SphereColliderComponent updated = component;
                bool changed = false;

                if (ImGui::DragFloat(TEXT("半径 (Radius)"), &updated.radius, 0.05f, 0.001f, 1000.0f)) {
                    changed = true;
                }
                float center[3] = { updated.center.x, updated.center.y, updated.center.z };
                if (ImGui::DragFloat3(TEXT("偏移中心 (Center)"), center, 0.05f)) {
                    updated.center = FCT::Vec3<>(center[0], center[1], center[2]);
                    changed = true;
                }
                if (ImGui::Checkbox(TEXT("触发器 (Is Trigger)"), &updated.isTrigger)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<SphereColliderComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<SphereColliderComponent>(ctx, "SphereColliderComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<CapsuleColliderComponent> {
        static constexpr const char* menuLabel = "CapsuleCollider Component";

        static void render(UiContext& ctx, const CapsuleColliderComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("胶囊体碰撞体 (Capsule Collider)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                CapsuleColliderComponent updated = component;
                bool changed = false;

                if (ImGui::DragFloat(TEXT("半径 (Radius)"), &updated.radius, 0.05f, 0.001f, 1000.0f)) {
                    changed = true;
                }
                if (ImGui::DragFloat(TEXT("半高 (Half Height)"), &updated.halfHeight, 0.05f, 0.001f, 1000.0f)) {
                    changed = true;
                }
                float center[3] = { updated.center.x, updated.center.y, updated.center.z };
                if (ImGui::DragFloat3(TEXT("偏移中心 (Center)"), center, 0.05f)) {
                    updated.center = FCT::Vec3<>(center[0], center[1], center[2]);
                    changed = true;
                }
                if (ImGui::Checkbox(TEXT("触发器 (Is Trigger)"), &updated.isTrigger)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<CapsuleColliderComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<CapsuleColliderComponent>(ctx, "CapsuleColliderComponent");
                ImGui::Unindent();
            }
        }
    };

    template <>
    struct ComponentUiTraits<PhysicsMaterialComponent> {
        static constexpr const char* menuLabel = "PhysicsMaterial Component";

        static void render(UiContext& ctx, const PhysicsMaterialComponent& component) {
            if (ImGui::CollapsingHeader(TEXT("物理材质 (Physics Material)"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                PhysicsMaterialComponent updated = component;
                bool changed = false;

                if (ImGui::DragFloat(TEXT("静摩擦系数"), &updated.staticFriction, 0.01f, 0.0f, 10.0f)) {
                    changed = true;
                }
                if (ImGui::DragFloat(TEXT("动摩擦系数"), &updated.dynamicFriction, 0.01f, 0.0f, 10.0f)) {
                    changed = true;
                }
                if (ImGui::DragFloat(TEXT("弹性恢复系数"), &updated.restitution, 0.01f, 0.0f, 1.0f)) {
                    changed = true;
                }

                if (changed) {
                    ctx.emit(UpdateComponentSignal<PhysicsMaterialComponent>{ .value = updated });
                }

                EmitRemoveComponentButton<PhysicsMaterialComponent>(ctx, "PhysicsMaterialComponent");
                ImGui::Unindent();
            }
        }
    };

    inline bool RenderReflectedScalarOrStringField(const ComponentFieldInfo& field, ComponentValue& ioValue) {
        if (field.type == "bool" && std::holds_alternative<bool>(ioValue)) {
            bool val = std::get<bool>(ioValue);
            if (ImGui::Checkbox(field.name.c_str(), &val)) {
                ioValue = val;
                return true;
            }
        } else if (field.type == "int" && std::holds_alternative<int>(ioValue)) {
            int val = std::get<int>(ioValue);
            if (ImGui::DragInt(field.name.c_str(), &val)) {
                ioValue = val;
                return true;
            }
        } else if (field.type == "float" && std::holds_alternative<float>(ioValue)) {
            float val = std::get<float>(ioValue);
            if (ImGui::DragFloat(field.name.c_str(), &val, 0.1f)) {
                ioValue = val;
                return true;
            }
        } else if (field.type == "double" && std::holds_alternative<double>(ioValue)) {
            double val = std::get<double>(ioValue);
            if (ImGui::InputDouble(field.name.c_str(), &val, 0.1, 1.0, "%.4f")) {
                ioValue = val;
                return true;
            }
        } else if ((field.type == "string" || field.type == "std::string") &&
                   std::holds_alternative<std::string>(ioValue)) {
            std::string cur = std::get<std::string>(ioValue);
            char buf[256];
            std::strncpy(buf, cur.c_str(), sizeof(buf));
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(field.name.c_str(), buf, sizeof(buf))) {
                ioValue = std::string(buf);
                return true;
            }
        }
        return false;
    }

    inline bool RenderReflectedVectorField(const ComponentFieldInfo& field, ComponentValue& ioValue) {
        if (field.type == "Vec2" && std::holds_alternative<FCT::Vec2>(ioValue)) {
            FCT::Vec2 v = std::get<FCT::Vec2>(ioValue);
            float arr[2] = { v.x, v.y };
            if (ImGui::DragFloat2(field.name.c_str(), arr, 0.1f)) {
                ioValue = FCT::Vec2(arr[0], arr[1]);
                return true;
            }
        } else if (field.type == "Vec3" && std::holds_alternative<FCT::Vec3<>>(ioValue)) {
            FCT::Vec3<> v = std::get<FCT::Vec3<>>(ioValue);
            float arr[3] = { v.x, v.y, v.z };
            if (ImGui::DragFloat3(field.name.c_str(), arr, 0.1f)) {
                ioValue = FCT::Vec3<>(arr[0], arr[1], arr[2]);
                return true;
            }
        } else if (field.type == "Vec4" && std::holds_alternative<FCT::Vec4>(ioValue)) {
            FCT::Vec4 v = std::get<FCT::Vec4>(ioValue);
            float arr[4] = { v.x, v.y, v.z, v.w };
            if (ImGui::DragFloat4(field.name.c_str(), arr, 0.1f)) {
                ioValue = FCT::Vec4(arr[0], arr[1], arr[2], arr[3]);
                return true;
            }
        }
        return false;
    }

    inline void RenderReflectedComponentWidget(UiContext& ctx,
                                               const ComponentInfo& info,
                                               entt::registry& registry,
                                               entt::entity entity) {
        if (!info.hasComponent || !info.getComponent || !info.hasComponent(registry, entity)) {
            return;
        }
        void* compPtr = info.getComponent(registry, entity);
        if (!compPtr) {
            return;
        }

        if (ImGui::CollapsingHeader(info.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            ImGui::PushID(info.name.c_str());

            for (const auto& field : info.fields) {
                if (!field.getter) {
                    continue;
                }
                ComponentValue val = field.getter(compPtr);
                bool modified = RenderReflectedScalarOrStringField(field, val) ||
                                RenderReflectedVectorField(field, val);
                if (modified && field.setter) {
                    UpdateReflectedComponentFieldSignal sig;
                    sig.componentName = info.name;
                    sig.fieldName = field.name;
                    sig.value = std::move(val);
                    ctx.emit(std::move(sig));
                }
            }

            if (info.removeComponent) {
                ImGui::Spacing();
                std::string btnLabel = std::string(TEXT("删除组件##Reflected_")) + info.name;
                if (ImGui::Button(btnLabel.c_str())) {
                    RemoveReflectedComponentSignal sig;
                    sig.componentName = info.name;
                    ctx.emit(std::move(sig));
                }
            }

            ImGui::PopID();
            ImGui::Unindent();
        }
    }

} // namespace MQEngine

#endif // EDITOR_COMPONENT_UI_WIDGET_H

