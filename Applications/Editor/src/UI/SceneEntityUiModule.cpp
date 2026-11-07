#include "SceneEntityUiModule.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    namespace {
        bool ParsePipeSeparatedPayload(const ImGuiPayload* payload,
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

        bool MatchesFilterCaseInsensitive(const std::string& text, const char* filter) {
            if (!filter || filter[0] == '\0') {
                return true;
            }
            std::string lowerText = text;
            std::string lowerFilter = filter;
            for (char& c : lowerText) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            for (char& c : lowerFilter) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return lowerText.find(lowerFilter) != std::string::npos;
        }

        bool IsBuiltInCoreComponent(entt::id_type typeId) {
            if (typeId == 0) {
                return false;
            }
            bool matched = false;
            [&]<typename... Components>(std::tuple<Components...>) {
                ((matched = matched || (entt::type_hash<Components>::value() == typeId)), ...);
            }(InspectorRenderableComponentsTuple{});
            return matched;
        }
    }

    void SceneEntityUiModule::onRender(UiContext& ctx) {
        ZoneScoped();
        ImGui::Begin(TEXT("场景实体查看器"));

        const auto& state = ctx.state<SceneEntityViewState>();
        if (state.hasActiveScene) {
            renderSceneHeader(ctx, state);
            ImGui::Separator();

            if (ImGui::CollapsingHeader(TEXT("场景全局"), ImGuiTreeNodeFlags_DefaultOpen)) {
                renderGlobalEntitySection(ctx, state);
            }

            for (const auto& trunkGroup : state.trunkGroups) {
                if (ImGui::CollapsingHeader(trunkGroup.trunkName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (trunkGroup.isLoaded) {
                        renderTrunkEntitySection(ctx, trunkGroup);
                    } else {
                        ImGui::TextDisabled(TEXT("(未加载)"));
                    }
                }
            }
            m_inputPopup.render();
        } else {
            ImGui::Text(TEXT("未选择场景"));
        }

        ImGui::End();
        renderScriptBindingModal(ctx);
        renderTextureBindingModal(ctx);
    }

    void SceneEntityUiModule::renderSceneHeader(UiContext& ctx, const SceneEntityViewState& state) {
        ImGui::Text(TEXT("当前场景: %s"), state.sceneName.c_str());
        ImGui::SameLine();
        if (ImGui::Button(TEXT("保存场景"))) {
            ctx.emit(SaveActiveSceneSignal{});
        }

        ImGui::SetNextItemWidth(-60.0f);
        ImGui::InputTextWithHint("##EntitySearchFilter", TEXT("搜索实体名称..."), m_searchFilter, sizeof(m_searchFilter));
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT("清空"))) {
            m_searchFilter[0] = '\0';
        }
    }

    void SceneEntityUiModule::renderPresetCreateMenu(
        UiContext& ctx,
        const std::string& trunkName,
        bool isGlobal,
        entt::entity parentEntity) {
        auto emitPreset = [&ctx, trunkName, isGlobal, parentEntity](const char* defaultName, EntityPresetType preset) {
            ctx.emit(CreateSceneEntitySignal{
                .name = defaultName,
                .trunkName = trunkName,
                .isGlobal = isGlobal,
                .parentEntity = parentEntity,
                .preset = preset
            });
        };

        if (ImGui::MenuItem(TEXT("空实体 (Empty)"))) {
            emitPreset("EmptyEntity", EntityPresetType::Empty);
        }
        if (ImGui::MenuItem(TEXT("摄像机 (Camera)"))) {
            emitPreset("Camera", EntityPresetType::Camera);
        }
        if (ImGui::MenuItem(TEXT("方向光 (Directional Light)"))) {
            emitPreset("DirectionalLight", EntityPresetType::DirectionalLight);
        }
        if (ImGui::MenuItem(TEXT("点光源 (Point Light)"))) {
            emitPreset("PointLight", EntityPresetType::PointLight);
        }

        const ComponentReflection* refl = ctx.state<EntityInspectorViewState>().componentReflection;
        if (refl) {
            bool hasSeparator = false;
            for (const auto& [compName, info] : refl->getComponentInfos()) {
                if (!info.addComponent || IsBuiltInCoreComponent(info.typeId)) {
                    continue;
                }
                if (!hasSeparator) {
                    ImGui::Separator();
                    hasSeparator = true;
                }
                if (ImGui::MenuItem(compName.c_str())) {
                    ctx.emit(CreateSceneEntitySignal{
                        .name = compName,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal,
                        .parentEntity = parentEntity,
                        .preset = EntityPresetType::CustomComponent,
                        .customComponentName = compName
                    });
                }
            }
        }
    }

    void SceneEntityUiModule::renderGlobalEntitySection(UiContext& ctx, const SceneEntityViewState& state) {
        ImGui::BeginChild("GlobalEntitiesChild", ImVec2(0, 130), ImGuiChildFlags_Borders);

        if (ImGui::BeginPopupContextWindow("GlobalEntityContextMenu")) {
            if (ImGui::MenuItem(TEXT("自定义名称创建实体..."))) {
                m_inputPopup.open(
                    TEXT("创建实体"),
                    TEXT("实体名称"),
                    TEXT("新实体"),
                    [&ctx](const std::string& name) {
                        ctx.emit(CreateSceneEntitySignal{
                            .name = name,
                            .trunkName = "",
                            .isGlobal = true
                        });
                    },
                    TEXT("创建位置: 场景全局"));
            }
            if (ImGui::BeginMenu(TEXT("快速创建预设实体"))) {
                renderPresetCreateMenu(ctx, "", true, entt::null);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TEXT("删除选中实体"), nullptr, false, state.hasSelectedGlobalEntity)) {
                ctx.emit(DeleteSceneEntitySignal{
                    .entity = state.selectedGlobalEntity,
                    .trunkName = "",
                    .isGlobal = true
                });
            }
            ImGui::EndPopup();
        }

        renderEntityItemList(ctx, state.globalEntities, "", true);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(20.0f, ImGui::GetContentRegionAvail().y)));
        handleSectionBackgroundDropTarget(ctx, "", true);
        ImGui::EndChild();
    }

    void SceneEntityUiModule::renderTrunkEntitySection(UiContext& ctx, const TrunkEntityGroupView& trunkGroup) {
        std::string childId = "TrunkChild_" + trunkGroup.trunkName;
        ImGui::BeginChild(childId.c_str(), ImVec2(0, 220), ImGuiChildFlags_Borders);

        std::string popupId = "TrunkEntityContextMenu_" + trunkGroup.trunkName;
        if (ImGui::BeginPopupContextWindow(popupId.c_str())) {
            std::string trunkName = trunkGroup.trunkName;
            if (ImGui::MenuItem(TEXT("自定义名称创建实体..."))) {
                m_inputPopup.open(
                    TEXT("创建实体"),
                    TEXT("实体名称"),
                    TEXT("新实体"),
                    [&ctx, trunkName](const std::string& name) {
                        ctx.emit(CreateSceneEntitySignal{
                            .name = name,
                            .trunkName = trunkName,
                            .isGlobal = false
                        });
                    },
                    std::string(TEXT("创建位置: ")) + trunkName);
            }
            if (ImGui::BeginMenu(TEXT("快速创建预设实体"))) {
                renderPresetCreateMenu(ctx, trunkName, false, entt::null);
                ImGui::EndMenu();
            }

            entt::entity selectedInTrunk = entt::null;
            for (const auto& item : trunkGroup.entities) {
                if (item.isSelected) {
                    selectedInTrunk = item.entity;
                    break;
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem(TEXT("删除选中实体"), nullptr, false, trunkGroup.hasSelectedEntityInTrunk)) {
                ctx.emit(DeleteSceneEntitySignal{
                    .entity = selectedInTrunk,
                    .trunkName = trunkName,
                    .isGlobal = false
                });
            }
            ImGui::EndPopup();
        }

        renderEntityItemList(ctx, trunkGroup.entities, trunkGroup.trunkName, false);
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(24.0f, ImGui::GetContentRegionAvail().y)));
        handleSectionBackgroundDropTarget(ctx, trunkGroup.trunkName, false);
        ImGui::EndChild();
    }

    void SceneEntityUiModule::renderEntityItemList(UiContext& ctx,
                                                   const std::vector<EntityItemView>& entities,
                                                   const std::string& trunkName,
                                                   bool isGlobal) {
        for (const auto& item : entities) {
            if (!MatchesFilterCaseInsensitive(item.displayName, m_searchFilter)) {
                continue;
            }

            ImGui::PushID(static_cast<int>(entt::to_integral(item.entity)));
            std::string label;
            for (int d = 0; d < item.depth; ++d) {
                label += "   ";
            }
            if (item.depth > 0) {
                label += "|- ";
            }
            label += item.displayName;
            if (ImGui::Selectable(label.c_str(), item.isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                ctx.emit(SelectSceneEntitySignal{
                    .entity = item.entity,
                    .trunkName = trunkName,
                    .isGlobal = isGlobal
                });
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ctx.emit(FocusEntitySignal{
                        .entity = item.entity,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                uint32_t draggedEntityId = static_cast<uint32_t>(item.entity);
                ImGui::SetDragDropPayload("SCENE_ENTITY_HIERARCHY", &draggedEntityId, sizeof(uint32_t));
                ImGui::Text(TEXT("移动节点: %s"), item.displayName.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("EntityNodeContextMenu")) {
                entt::entity currentEnt = item.entity;
                if (ImGui::MenuItem(TEXT("聚焦到此实体 (F)"))) {
                    ctx.emit(FocusEntitySignal{
                        .entity = currentEnt,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
                if (ImGui::MenuItem(TEXT("重命名实体..."))) {
                    m_inputPopup.open(
                        TEXT("重命名实体"),
                        TEXT("新名称"),
                        item.displayName,
                        [&ctx, trunkName, isGlobal, currentEnt](const std::string& newName) {
                            ctx.emit(RenameSceneEntitySignal{
                                .entity = currentEnt,
                                .trunkName = trunkName,
                                .isGlobal = isGlobal,
                                .newName = newName
                            });
                        },
                        std::string(TEXT("原名称: ")) + item.displayName);
                }
                if (ImGui::MenuItem(TEXT("复制实体 (Duplicate)"))) {
                    ctx.emit(DuplicateSceneEntitySignal{
                        .entity = currentEnt,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TEXT("创建子实体..."))) {
                    m_inputPopup.open(
                        TEXT("创建子实体"),
                        TEXT("子实体名称"),
                        TEXT("新子实体"),
                        [&ctx, trunkName, isGlobal, currentEnt](const std::string& name) {
                            ctx.emit(CreateSceneEntitySignal{
                                .name = name,
                                .trunkName = trunkName,
                                .isGlobal = isGlobal,
                                .parentEntity = currentEnt
                            });
                        },
                        std::string(TEXT("父节点: ")) + item.displayName);
                }
                if (ImGui::BeginMenu(TEXT("创建预设子实体"))) {
                    renderPresetCreateMenu(ctx, trunkName, isGlobal, currentEnt);
                    ImGui::EndMenu();
                }
                if (item.parentEntity != entt::null && ImGui::MenuItem(TEXT("移至根层级 (解除父子关系)"))) {
                    ctx.emit(ReparentSceneEntitySignal{
                        .childEntity = currentEnt,
                        .parentEntity = entt::null,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TEXT("删除实体"))) {
                    ctx.emit(DeleteSceneEntitySignal{
                        .entity = currentEnt,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
                ImGui::EndPopup();
            }
            handleEntityDragDropTarget(ctx, item.entity, trunkName, isGlobal);
            ImGui::PopID();
        }
    }

    void SceneEntityUiModule::handleSectionBackgroundDropTarget(
        UiContext& ctx,
        const std::string& trunkName,
        bool isGlobal) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY_HIERARCHY")) {
            if (payload->Data && payload->DataSize == sizeof(uint32_t)) {
                uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
                entt::entity draggedEntity = static_cast<entt::entity>(draggedId);
                ctx.emit(ReparentSceneEntitySignal{
                    .childEntity = draggedEntity,
                    .parentEntity = entt::null,
                    .trunkName = trunkName,
                    .isGlobal = isGlobal
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
            std::string modelUuid;
            std::string meshName;
            if (ParsePipeSeparatedPayload(payload, modelUuid, meshName)) {
                ctx.emit(SpawnModelInSceneSignal{
                    .modelUuid = std::move(modelUuid),
                    .meshName = std::move(meshName),
                    .modelName = "",
                    .trunkName = trunkName,
                    .isGlobal = isGlobal
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL")) {
            std::string modelUuid;
            std::string modelName;
            if (ParsePipeSeparatedPayload(payload, modelUuid, modelName)) {
                ctx.emit(SpawnModelInSceneSignal{
                    .modelUuid = std::move(modelUuid),
                    .meshName = "",
                    .modelName = std::move(modelName),
                    .trunkName = trunkName,
                    .isGlobal = isGlobal
                });
            }
        }

        ImGui::EndDragDropTarget();
    }

    void SceneEntityUiModule::handleEntityDragDropTarget(UiContext& ctx,
                                                         entt::entity entity,
                                                         const std::string& trunkName,
                                                         bool isGlobal) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY_HIERARCHY")) {
            if (payload->Data && payload->DataSize == sizeof(uint32_t)) {
                uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
                entt::entity draggedEntity = static_cast<entt::entity>(draggedId);
                if (draggedEntity != entity) {
                    ctx.emit(ReparentSceneEntitySignal{
                        .childEntity = draggedEntity,
                        .parentEntity = entity,
                        .trunkName = trunkName,
                        .isGlobal = isGlobal
                    });
                }
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
            std::string functionName = static_cast<const char*>(payload->Data);
            openScriptBindingModal(entity, functionName, isGlobal, trunkName, false);
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_CLASS")) {
            std::string className = static_cast<const char*>(payload->Data);
            openScriptBindingModal(entity, className, isGlobal, trunkName, true);
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
            std::string modelUuid;
            std::string meshName;
            if (ParsePipeSeparatedPayload(payload, modelUuid, meshName)) {
                ctx.emit(BindMeshToEntitySignal{
                    .entity = entity,
                    .trunkName = trunkName,
                    .isGlobal = isGlobal,
                    .modelUuid = std::move(modelUuid),
                    .meshName = std::move(meshName)
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL")) {
            std::string modelUuid;
            std::string modelName;
            if (ParsePipeSeparatedPayload(payload, modelUuid, modelName)) {
                ctx.emit(SpawnModelInSceneSignal{
                    .modelUuid = std::move(modelUuid),
                    .meshName = "",
                    .modelName = std::move(modelName),
                    .trunkName = trunkName,
                    .isGlobal = isGlobal,
                    .parentEntity = entity
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
            std::string modelUuid;
            std::string texturePath;
            if (ParsePipeSeparatedPayload(payload, modelUuid, texturePath)) {
                openTextureBindingModal(entity, modelUuid, texturePath, isGlobal, trunkName);
            }
        }

        ImGui::EndDragDropTarget();
    }

    void SceneEntityUiModule::openScriptBindingModal(entt::entity entity,
                                                     const std::string& scriptName,
                                                     bool isGlobal,
                                                     const std::string& trunkName,
                                                     bool isClass) {
        m_showScriptTypePopup = true;
        m_draggedScriptName = scriptName;
        m_targetEntity = entity;
        m_targetTrunkName = trunkName;
        m_targetIsGlobal = isGlobal;
        m_isDraggedScriptClass = isClass;
    }

    void SceneEntityUiModule::openTextureBindingModal(entt::entity entity,
                                                      const std::string& modelUuid,
                                                      const std::string& texturePath,
                                                      bool isGlobal,
                                                      const std::string& trunkName) {
        m_showTextureTypePopup = true;
        m_targetEntity = entity;
        m_draggedModelUuid = modelUuid;
        m_draggedTexturePath = texturePath;
        m_targetIsGlobal = isGlobal;
        m_targetTrunkName = trunkName;
    }

    void SceneEntityUiModule::emitScriptBindAndClose(UiContext& ctx, ScriptBindMode mode) {
        ctx.emit(BindScriptToEntitySignal{
            .entity = m_targetEntity,
            .trunkName = m_targetTrunkName,
            .isGlobal = m_targetIsGlobal,
            .scriptName = m_draggedScriptName,
            .mode = mode
        });
        m_showScriptTypePopup = false;
        ImGui::CloseCurrentPopup();
    }

    void SceneEntityUiModule::renderScriptBindingModal(UiContext& ctx) {
        if (!m_showScriptTypePopup) {
            return;
        }

        ImGui::OpenPopup(TEXT("选择脚本类型"));
        ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal(TEXT("选择脚本类型"), &m_showScriptTypePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (m_isDraggedScriptClass) {
                ImGui::Text(TEXT("将脚本类 '%s' 绑定到实体"), m_draggedScriptName.c_str());
                ImGui::Separator();
                if (ImGui::Button(TEXT("绑定类"), ImVec2(-1, 0))) {
                    emitScriptBindAndClose(ctx, ScriptBindMode::ClassBinding);
                }
            } else {
                ImGui::Text(TEXT("为实体添加脚本 '%s'"), m_draggedScriptName.c_str());
                ImGui::Separator();

                ImGui::Columns(2, nullptr, true);

                ImGui::Text(TEXT("独立组件模式"));
                ImGui::Separator();
                if (ImGui::Button(TEXT("Ticker (每帧执行)"), ImVec2(-1, 0))) {
                    emitScriptBindAndClose(ctx, ScriptBindMode::StandaloneTicker);
                }
                if (ImGui::Button(TEXT("OnStart (启动时执行)"), ImVec2(-1, 0))) {
                    emitScriptBindAndClose(ctx, ScriptBindMode::StandaloneOnStart);
                }

                ImGui::NextColumn();

                ImGui::Text(TEXT("函数表模式"));
                ImGui::Separator();
                if (ImGui::Button(TEXT("添加到函数表"), ImVec2(-1, 0))) {
                    emitScriptBindAndClose(ctx, ScriptBindMode::FunctionTable);
                }

                ImGui::Columns(1);
            }

            ImGui::Separator();
            if (ImGui::Button(TEXT("取消"), ImVec2(120, 0))) {
                m_showScriptTypePopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            m_showScriptTypePopup = false;
        }
    }

    void SceneEntityUiModule::emitTextureBindAndClose(UiContext& ctx, TextureBindSlot slot) {
        ctx.emit(BindTextureToEntitySignal{
            .entity = m_targetEntity,
            .trunkName = m_targetTrunkName,
            .isGlobal = m_targetIsGlobal,
            .modelUuid = m_draggedModelUuid,
            .texturePath = m_draggedTexturePath,
            .slot = slot
        });
        m_showTextureTypePopup = false;
        ImGui::CloseCurrentPopup();
    }

    void SceneEntityUiModule::renderTextureBindingModal(UiContext& ctx) {
        if (!m_showTextureTypePopup) {
            return;
        }

        ImGui::OpenPopup(TEXT("选择贴图类型"));
        if (ImGui::BeginPopupModal(TEXT("选择贴图类型"), &m_showTextureTypePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(TEXT("为实体添加贴图 '%s'"), m_draggedTexturePath.c_str());
            ImGui::Separator();

            if (ImGui::Button(TEXT("Diffuse (漫反射)"), ImVec2(150, 0))) {
                emitTextureBindAndClose(ctx, TextureBindSlot::Diffuse);
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT("Normal (法线)"), ImVec2(150, 0))) {
                emitTextureBindAndClose(ctx, TextureBindSlot::Normal);
            }
            if (ImGui::Button(TEXT("Albedo (反照率)"), ImVec2(150, 0))) {
                emitTextureBindAndClose(ctx, TextureBindSlot::Albedo);
            }
            if (ImGui::Button(TEXT("Emissive (自发光)"), ImVec2(150, 0))) {
                emitTextureBindAndClose(ctx, TextureBindSlot::Emissive);
            }
            if (ImGui::Button(TEXT("Orm (环境光遮蔽/粗糙度/金属)"), ImVec2(150, 0))) {
                emitTextureBindAndClose(ctx, TextureBindSlot::Orm);
            }

            ImGui::Separator();
            if (ImGui::Button(TEXT("取消"), ImVec2(120, 0))) {
                m_showTextureTypePopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            m_showTextureTypePopup = false;
        }
    }

} // namespace MQEngine
