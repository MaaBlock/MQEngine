#include "EntityInspectorUiModule.h"
#include "ComponentUiWidget.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    void EntityInspectorUiModule::onRender(UiContext& ctx) {
        ZoneScoped();
        ImGui::Begin(TEXT("实体检查器"));

        const auto& state = ctx.state<EntityInspectorViewState>();
        if (!state.hasSelectedEntity) {
            ImGui::Text(TEXT("未选择实体"));
            ImGui::Text(TEXT("请在场景实体查看器中选择一个实体来查看详情"));
        } else {
            renderEntityDetails(ctx, state);
        }

        ImGui::End();
        m_inputPopup.render();
    }

    void EntityInspectorUiModule::renderEntityDetails(UiContext& ctx, const EntityInspectorViewState& state) {
        ImGui::Text(TEXT("实体ID: %u"), static_cast<uint32_t>(state.entity));
        if (state.isGlobal) {
            ImGui::Text(TEXT("所属: 全局"));
        } else {
            ImGui::Text(TEXT("所属Trunk: %s"), state.trunkName.c_str());
        }

        ImGui::Separator();

        if (!state.isRegistryAvailable || !state.activeRegistry) {
            ImGui::Text(TEXT("错误: 无法获取实体注册表"));
            return;
        }

        if (!state.isEntityValid) {
            ImGui::Text(TEXT("错误: 实体已被删除"));
            return;
        }

        ImGui::Text(TEXT("组件列表:"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT("聚焦实体 (F)"))) {
            ctx.emit(FocusEntitySignal{
                .entity = state.entity,
                .trunkName = state.trunkName,
                .isGlobal = state.isGlobal
            });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT("复制实体"))) {
            ctx.emit(DuplicateSceneEntitySignal{
                .entity = state.entity,
                .trunkName = state.trunkName,
                .isGlobal = state.isGlobal
            });
        }

        if (ImGui::Button(TEXT("添加组件"))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        renderAddComponentPopup(ctx, state);

        ImGui::BeginChild("ComponentList", ImVec2(0, 0), true);
        renderComponentList(ctx, state);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(28.0f, ImGui::GetContentRegionAvail().y)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
                std::string modelUuid;
                std::string meshName;
                if (ParseInspectorPipePayload(payload, modelUuid, meshName)) {
                    ctx.emit(BindMeshToEntitySignal{
                        .entity = state.entity,
                        .trunkName = state.trunkName,
                        .isGlobal = state.isGlobal,
                        .modelUuid = std::move(modelUuid),
                        .meshName = std::move(meshName)
                    });
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
                std::string modelUuid;
                std::string texturePath;
                if (ParseInspectorPipePayload(payload, modelUuid, texturePath)) {
                    ctx.emit(BindTextureToEntitySignal{
                        .entity = state.entity,
                        .trunkName = state.trunkName,
                        .isGlobal = state.isGlobal,
                        .modelUuid = std::move(modelUuid),
                        .texturePath = std::move(texturePath),
                        .slot = TextureBindSlot::Diffuse
                    });
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_CLASS")) {
                if (payload->Data) {
                    ctx.emit(BindScriptToEntitySignal{
                        .entity = state.entity,
                        .trunkName = state.trunkName,
                        .isGlobal = state.isGlobal,
                        .scriptName = static_cast<const char*>(payload->Data),
                        .mode = ScriptBindMode::ClassBinding
                    });
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
                if (payload->Data) {
                    ctx.emit(BindScriptToEntitySignal{
                        .entity = state.entity,
                        .trunkName = state.trunkName,
                        .isGlobal = state.isGlobal,
                        .scriptName = static_cast<const char*>(payload->Data),
                        .mode = ScriptBindMode::FunctionTable
                    });
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
    }

    template <typename ComponentType>
    void EntityInspectorUiModule::renderAddComponentMenuItem(UiContext& ctx) {
        if (ImGui::MenuItem(ComponentUiTraits<ComponentType>::menuLabel)) {
            ctx.emit(AddTypedComponentSignal<ComponentType>{});
            ImGui::CloseCurrentPopup();
        }
    }

    void EntityInspectorUiModule::renderAddComponentPopup(UiContext& ctx, const EntityInspectorViewState& state) {
        if (!ImGui::BeginPopup("AddComponentPopup")) {
            return;
        }

        ImGui::Text(TEXT("选择要添加的组件:"));
        ImGui::Separator();

        [&]<typename... Components>(std::tuple<Components...>) {
            (renderAddComponentMenuItem<Components>(ctx), ...);
        }(InspectorAddableComponentsTuple{});

        if (ImGui::MenuItem("Skybox Component")) {
            m_inputPopup.open(
                TEXT("设置 Skybox 路径"),
                TEXT("Skybox 路径"),
                "Assets/Textures/skybox/",
                [&ctx](const std::string& path) {
                    ctx.emit(AddSkyboxComponentSignal{ .texturePath = path });
                },
                TEXT("路径应包含6个面: right.jpg, left.jpg, ..."));
            ImGui::CloseCurrentPopup();
        }

        renderReflectedAddMenuItems(ctx, state);

        ImGui::EndPopup();
    }

    void EntityInspectorUiModule::renderReflectedAddMenuItems(UiContext& ctx, const EntityInspectorViewState& state) {
        if (!state.componentReflection) {
            return;
        }

        bool headerPrinted = false;
        for (const auto& [name, info] : state.componentReflection->getComponentInfos()) {
            if (!info.addComponent || isBuiltInInspectorComponent(info.typeId)) {
                continue;
            }
            if (!headerPrinted) {
                ImGui::Separator();
                ImGui::Text(TEXT("自定义反射组件:"));
                headerPrinted = true;
            }
            if (ImGui::MenuItem(name.c_str())) {
                ctx.emit(AddReflectedComponentSignal{ .componentName = name });
                ImGui::CloseCurrentPopup();
            }
        }
    }

    template <typename ComponentType>
    bool EntityInspectorUiModule::tryRenderComponentWidget(UiContext& ctx,
                                                           const entt::registry& registry,
                                                           entt::entity entity) {
        if (const auto* component = registry.try_get<ComponentType>(entity)) {
            ComponentUiTraits<ComponentType>::render(ctx, *component);
            return true;
        }
        return false;
    }

    void EntityInspectorUiModule::renderComponentList(UiContext& ctx, const EntityInspectorViewState& state) {
        bool hasComponents = false;
        const entt::registry& registry = *state.activeRegistry;

        [&]<typename... Components>(std::tuple<Components...>) {
            ((hasComponents |= tryRenderComponentWidget<Components>(ctx, registry, state.entity)), ...);
        }(InspectorRenderableComponentsTuple{});

        hasComponents |= renderReflectedComponents(ctx, state);

        if (!hasComponents) {
            ImGui::Text(TEXT("该实体没有组件"));
        }
    }

    bool EntityInspectorUiModule::renderReflectedComponents(UiContext& ctx, const EntityInspectorViewState& state) {
        if (!state.componentReflection || !state.activeRegistry) {
            return false;
        }

        bool renderedAny = false;
        auto& mutableReg = const_cast<entt::registry&>(*state.activeRegistry);
        for (const auto& [name, info] : state.componentReflection->getComponentInfos()) {
            if (isBuiltInInspectorComponent(info.typeId)) {
                continue;
            }
            if (info.hasComponent && info.hasComponent(mutableReg, state.entity)) {
                RenderReflectedComponentWidget(ctx, info, mutableReg, state.entity);
                renderedAny = true;
            }
        }
        return renderedAny;
    }

    bool EntityInspectorUiModule::isBuiltInInspectorComponent(entt::id_type typeId) {
        if (typeId == 0) {
            return false;
        }
        bool matched = false;
        [&]<typename... Components>(std::tuple<Components...>) {
            ((matched = matched || (entt::type_hash<Components>::value() == typeId)), ...);
        }(InspectorRenderableComponentsTuple{});
        return matched;
    }

} // namespace MQEngine
