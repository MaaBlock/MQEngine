#ifndef EDITOR_UI_BEHAVIOR_CONTROLLER_H
#define EDITOR_UI_BEHAVIOR_CONTROLLER_H

#include "DeclarativeUiBinder.h"
#include "../UI/UiLayer.h"

namespace MQEngine {

    /**
     * @brief Editor UI 行为控制器，承载全部与 UI 解耦的编辑器业务逻辑，并通过声明式元编程表完成静态绑定
     */
    class EditorUiBehaviorController {
    public:
        EditorUiBehaviorController() = default;
        ~EditorUiBehaviorController() = default;

        void bindToLayer(UiLayer& uiLayer);

        void syncMainMenuState(MainMenuViewState& state);
        void syncViewportState(ViewportViewState& state);
        void syncCameraControlState(CameraControlViewState& state);
        void syncSceneEntityState(SceneEntityViewState& state);
        void syncEntityInspectorState(EntityInspectorViewState& state);

        void onSaveProjectSetting(const SaveProjectSettingSignal& signal);
        void onRequestResetLayout(const RequestResetLayoutSignal& signal);
        void onSetEngineRunning(const SetEngineRunningSignal& signal);
        Status onSaveActiveScene(const SaveActiveSceneSignal& signal);
        void onWindowCloseSave(const WindowCloseSaveSignal& signal);
        void onDumpDebugObjects(const DumpDebugObjectsSignal& signal);
        void onRecordDebugObjectDiff(const RecordDebugObjectDiffSignal& signal);
        void onImportGodotScene(const ImportGodotSceneSignal& signal);

        void onHotReloadAll(const HotReloadAllSignal& signal);
        void onReloadUserCode(const ReloadUserCodeSignal& signal);
        void onLoadPlugin(const LoadPluginSignal& signal);
        void onUnloadPlugin(const UnloadPluginSignal& signal);
        void onReloadPlugin(const ReloadPluginSignal& signal);
        void onSetAutoHotReload(const SetAutoHotReloadSignal& signal);

        void onUndoCommand(const UndoCommandSignal& signal);
        void onRedoCommand(const RedoCommandSignal& signal);

        void onActivateEditorCamera(const ActivateEditorCameraSignal& signal);
        void onActivateSceneCamera(const ActivateSceneCameraSignal& signal);
        void onUpdateEditorCameraTransform(const UpdateEditorCameraTransformSignal& signal);
        void onUpdateEditorCameraSettings(const UpdateEditorCameraSettingsSignal& signal);
        void onSyncEditorCameraFromScene(const SyncEditorCameraFromSceneSignal& signal);
        void onSyncSceneCameraFromEditor(const SyncSceneCameraFromEditorSignal& signal);
        void onResetEditorCamera(const ResetEditorCameraSignal& signal);
        void onFocusEntity(const FocusEntitySignal& signal);

        void onSelectSceneEntity(const SelectSceneEntitySignal& signal);
        void onCreateSceneEntity(const CreateSceneEntitySignal& signal);
        void onDuplicateSceneEntity(const DuplicateSceneEntitySignal& signal);
        void onRenameSceneEntity(const RenameSceneEntitySignal& signal);
        void onSpawnModelInScene(const SpawnModelInSceneSignal& signal);
        void onDeleteSceneEntity(const DeleteSceneEntitySignal& signal);
        void onReparentSceneEntity(const ReparentSceneEntitySignal& signal);
        void onBindScriptToEntity(const BindScriptToEntitySignal& signal);
        void onBindMeshToEntity(const BindMeshToEntitySignal& signal);
        void onBindTextureToEntity(const BindTextureToEntitySignal& signal);

        void onRemoveComponent(const RemoveComponentSignal& signal);
        void onAddSkyboxComponent(const AddSkyboxComponentSignal& signal);
        void onAddReflectedComponent(const AddReflectedComponentSignal& signal);
        void onRemoveReflectedComponent(const RemoveReflectedComponentSignal& signal);
        void onUpdateReflectedComponentField(const UpdateReflectedComponentFieldSignal& signal);

        template <typename ComponentType>
        void onAddTypedComponent(const AddTypedComponentSignal<ComponentType>& signal) {
            (void)signal;
            entt::registry* registry = resolveSelectedEntityRegistry();
            entt::entity entity = g_editorGlobal.selectedEntity.entity;
            if (!registry || !registry->valid(entity) || registry->all_of<ComponentType>(entity)) {
                return;
            }
            emplaceDefaultComponent<ComponentType>(*registry, entity);
        }

        template <typename ComponentType>
        void onUpdateTypedComponent(const UpdateComponentSignal<ComponentType>& signal) {
            entt::registry* registry = resolveSelectedEntityRegistry();
            entt::entity entity = g_editorGlobal.selectedEntity.entity;
            if (!registry || !registry->valid(entity)) {
                return;
            }
            if (auto* mutableComp = registry->try_get<ComponentType>(entity)) {
                *mutableComp = signal.value;
            }
        }

    private:
        entt::registry* resolveEntityRegistry(Scene::Scene* scene, bool isGlobal, const std::string& trunkName) const;
        entt::registry* resolveSelectedEntityRegistry() const;
        const ComponentReflection* resolveActiveReflection() const;
        static std::string resolveEntityDisplayName(const entt::registry& registry, entt::entity entity);
        void collectEntityItems(entt::registry& registry,
                                bool isGlobal,
                                const std::string& trunkName,
                                std::vector<EntityItemView>& outItems,
                                bool& outHasSelected) const;

        template <typename ComponentType>
        void emplaceDefaultComponent(entt::registry& registry, entt::entity entity) {
            if constexpr (std::is_same_v<ComponentType, PositionComponent> ||
                          std::is_same_v<ComponentType, RotationComponent>) {
                registry.emplace<ComponentType>(entity, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
            } else if constexpr (std::is_same_v<ComponentType, ScaleComponent>) {
                registry.emplace<ComponentType>(entity, FCT::Vec3{ 1.0f, 1.0f, 1.0f });
            } else if constexpr (std::is_same_v<ComponentType, CameraComponent>) {
                auto& camera = registry.emplace<CameraComponent>(entity);
                camera.active = false;
                camera.fov = 60.0f;
                camera.nearPlane = 0.1f;
                camera.farPlane = 50000.0f;
                if (g_editorGlobal.cameraManager) {
                    g_editorGlobal.cameraManager->hookCamera();
                }
            } else if constexpr (std::is_same_v<ComponentType, DiffuseTextureComponent> ||
                                 std::is_same_v<ComponentType, NormalTextureComponent> ||
                                 std::is_same_v<ComponentType, EmissiveTextureComponent> ||
                                 std::is_same_v<ComponentType, OrmTextureComponent>) {
                registry.emplace<ComponentType>(entity, "", "");
            } else {
                registry.emplace<ComponentType>(entity);
            }
        }

    private:
        UiLayer* m_uiLayer = nullptr;
    };

    /**
     * @brief Editor 全局声明式 UI 行为绑定配置表
     */
    using EditorUiBehaviorProfile = UiBehaviorBindingTable<
        EditorUiBehaviorController,
        SyncState<MainMenuViewState, &EditorUiBehaviorController::syncMainMenuState>,
        SyncState<ViewportViewState, &EditorUiBehaviorController::syncViewportState>,
        SyncState<CameraControlViewState, &EditorUiBehaviorController::syncCameraControlState>,
        SyncState<SceneEntityViewState, &EditorUiBehaviorController::syncSceneEntityState>,
        SyncState<EntityInspectorViewState, &EditorUiBehaviorController::syncEntityInspectorState>,

        OnSignal<SaveProjectSettingSignal, &EditorUiBehaviorController::onSaveProjectSetting>,
        OnSignal<RequestResetLayoutSignal, &EditorUiBehaviorController::onRequestResetLayout>,
        OnSignal<SetEngineRunningSignal, &EditorUiBehaviorController::onSetEngineRunning>,
        OnSignal<SaveActiveSceneSignal, &EditorUiBehaviorController::onSaveActiveScene>,
        OnSignal<WindowCloseSaveSignal, &EditorUiBehaviorController::onWindowCloseSave>,
        OnSignal<DumpDebugObjectsSignal, &EditorUiBehaviorController::onDumpDebugObjects>,
        OnSignal<RecordDebugObjectDiffSignal, &EditorUiBehaviorController::onRecordDebugObjectDiff>,
        OnSignal<ImportGodotSceneSignal, &EditorUiBehaviorController::onImportGodotScene>,

        OnSignal<HotReloadAllSignal, &EditorUiBehaviorController::onHotReloadAll>,
        OnSignal<ReloadUserCodeSignal, &EditorUiBehaviorController::onReloadUserCode>,
        OnSignal<LoadPluginSignal, &EditorUiBehaviorController::onLoadPlugin>,
        OnSignal<UnloadPluginSignal, &EditorUiBehaviorController::onUnloadPlugin>,
        OnSignal<ReloadPluginSignal, &EditorUiBehaviorController::onReloadPlugin>,
        OnSignal<SetAutoHotReloadSignal, &EditorUiBehaviorController::onSetAutoHotReload>,

        OnSignal<UndoCommandSignal, &EditorUiBehaviorController::onUndoCommand>,
        OnSignal<RedoCommandSignal, &EditorUiBehaviorController::onRedoCommand>,

        OnSignal<ActivateEditorCameraSignal, &EditorUiBehaviorController::onActivateEditorCamera>,
        OnSignal<ActivateSceneCameraSignal, &EditorUiBehaviorController::onActivateSceneCamera>,
        OnSignal<UpdateEditorCameraTransformSignal, &EditorUiBehaviorController::onUpdateEditorCameraTransform>,
        OnSignal<UpdateEditorCameraSettingsSignal, &EditorUiBehaviorController::onUpdateEditorCameraSettings>,
        OnSignal<SyncEditorCameraFromSceneSignal, &EditorUiBehaviorController::onSyncEditorCameraFromScene>,
        OnSignal<SyncSceneCameraFromEditorSignal, &EditorUiBehaviorController::onSyncSceneCameraFromEditor>,
        OnSignal<ResetEditorCameraSignal, &EditorUiBehaviorController::onResetEditorCamera>,
        OnSignal<FocusEntitySignal, &EditorUiBehaviorController::onFocusEntity>,

        OnSignal<SelectSceneEntitySignal, &EditorUiBehaviorController::onSelectSceneEntity>,
        OnSignal<CreateSceneEntitySignal, &EditorUiBehaviorController::onCreateSceneEntity>,
        OnSignal<DuplicateSceneEntitySignal, &EditorUiBehaviorController::onDuplicateSceneEntity>,
        OnSignal<RenameSceneEntitySignal, &EditorUiBehaviorController::onRenameSceneEntity>,
        OnSignal<SpawnModelInSceneSignal, &EditorUiBehaviorController::onSpawnModelInScene>,
        OnSignal<DeleteSceneEntitySignal, &EditorUiBehaviorController::onDeleteSceneEntity>,
        OnSignal<ReparentSceneEntitySignal, &EditorUiBehaviorController::onReparentSceneEntity>,
        OnSignal<BindScriptToEntitySignal, &EditorUiBehaviorController::onBindScriptToEntity>,
        OnSignal<BindMeshToEntitySignal, &EditorUiBehaviorController::onBindMeshToEntity>,
        OnSignal<BindTextureToEntitySignal, &EditorUiBehaviorController::onBindTextureToEntity>,

        OnSignal<RemoveComponentSignal, &EditorUiBehaviorController::onRemoveComponent>,
        OnSignal<AddSkyboxComponentSignal, &EditorUiBehaviorController::onAddSkyboxComponent>,
        OnSignal<AddReflectedComponentSignal, &EditorUiBehaviorController::onAddReflectedComponent>,
        OnSignal<RemoveReflectedComponentSignal, &EditorUiBehaviorController::onRemoveReflectedComponent>,
        OnSignal<UpdateReflectedComponentFieldSignal, &EditorUiBehaviorController::onUpdateReflectedComponentField>,

        BindComponentAddTuple<InspectorAddableComponentsTuple>,
        BindComponentUpdateTuple<InspectorEditableComponentsTuple>
    >;

} // namespace MQEngine

#endif // EDITOR_UI_BEHAVIOR_CONTROLLER_H
