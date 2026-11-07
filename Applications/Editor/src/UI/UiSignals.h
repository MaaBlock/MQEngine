#ifndef EDITOR_UI_SIGNALS_H
#define EDITOR_UI_SIGNALS_H

#include "../thirdparty/thirdparty.h"
#include "../data/NameTag.h"

namespace MQEngine {

    struct UiSignalTag {};
    struct UiViewStateTag {};

    template <typename T>
    concept UiSignalConcept = std::derived_from<std::decay_t<T>, UiSignalTag>;

    template <typename T>
    concept UiViewStateConcept = std::derived_from<std::decay_t<T>, UiViewStateTag>;

    struct SaveProjectSettingSignal : UiSignalTag {};

    struct RequestResetLayoutSignal : UiSignalTag {};

    struct SetEngineRunningSignal : UiSignalTag {
        bool running = false;
    };

    struct SaveActiveSceneSignal : UiSignalTag {};

    struct WindowCloseSaveSignal : UiSignalTag {};

    struct UndoCommandSignal : UiSignalTag {};

    struct RedoCommandSignal : UiSignalTag {};

    struct DumpDebugObjectsSignal : UiSignalTag {};

    struct RecordDebugObjectDiffSignal : UiSignalTag {};

    struct ImportGodotSceneSignal : UiSignalTag {
        std::string filePath;
    };

    struct HotReloadAllSignal : UiSignalTag {};

    struct ReloadUserCodeSignal : UiSignalTag {
        std::string moduleName = "UserCode";
    };

    struct LoadPluginSignal : UiSignalTag {
        std::string pluginIdent;
    };

    struct UnloadPluginSignal : UiSignalTag {
        std::string pluginName;
    };

    struct ReloadPluginSignal : UiSignalTag {
        std::string pluginName;
    };

    struct SetAutoHotReloadSignal : UiSignalTag {
        bool enabled = true;
    };

    struct ActivateEditorCameraSignal : UiSignalTag {};

    struct ActivateSceneCameraSignal : UiSignalTag {
        entt::registry* registry = nullptr;
        entt::entity entity = entt::null;
    };

    struct UpdateEditorCameraTransformSignal : UiSignalTag {
        FCT::Vec3<> position{ 0.0f, 0.0f, 0.0f };
        FCT::Vec3<> rotation{ 0.0f, 0.0f, 0.0f };
    };

    struct UpdateEditorCameraSettingsSignal : UiSignalTag {
        float fov = 60.0f;
        float nearPlane = 0.5f;
        float farPlane = 20000000.0f;
        bool reverseZ = true;
        float moveSpeed = 25.0f;
        float mouseSensitivity = 0.18f;
        float fastMultiplier = 4.0f;
    };

    struct SyncEditorCameraFromSceneSignal : UiSignalTag {};

    struct SyncSceneCameraFromEditorSignal : UiSignalTag {
        entt::registry* targetRegistry = nullptr;
        entt::entity targetEntity = entt::null;
    };

    struct ResetEditorCameraSignal : UiSignalTag {};

    struct FocusEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
    };

    struct SelectSceneEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
    };

    enum class EntityPresetType : uint8_t {
        Empty = 0,
        Camera,
        DirectionalLight,
        PointLight,
        CustomComponent
    };

    struct CreateSceneEntitySignal : UiSignalTag {
        std::string name;
        std::string trunkName;
        bool isGlobal = true;
        entt::entity parentEntity = entt::null;
        EntityPresetType preset = EntityPresetType::Empty;
        std::string customComponentName;
    };

    struct DuplicateSceneEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
    };

    struct RenameSceneEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
        std::string newName;
    };

    struct SpawnModelInSceneSignal : UiSignalTag {
        std::string modelUuid;
        std::string meshName;
        std::string modelName;
        std::string trunkName;
        bool isGlobal = false;
        entt::entity parentEntity = entt::null;
    };

    struct ReparentSceneEntitySignal : UiSignalTag {
        entt::entity childEntity = entt::null;
        entt::entity parentEntity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
    };

    struct DeleteSceneEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
    };

    enum class ScriptBindMode : uint8_t {
        ClassBinding = 0,
        StandaloneTicker,
        StandaloneOnStart,
        FunctionTable
    };

    struct BindScriptToEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
        std::string scriptName;
        ScriptBindMode mode = ScriptBindMode::FunctionTable;
    };

    struct BindMeshToEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
        std::string modelUuid;
        std::string meshName;
    };

    enum class TextureBindSlot : uint8_t {
        Diffuse = 0,
        Normal,
        Albedo,
        Emissive,
        Orm
    };

    struct BindTextureToEntitySignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
        std::string modelUuid;
        std::string texturePath;
        TextureBindSlot slot = TextureBindSlot::Diffuse;
    };

    template <typename ComponentType>
    struct AddTypedComponentSignal : UiSignalTag {};

    struct AddSkyboxComponentSignal : UiSignalTag {
        std::string texturePath;
    };

    struct RemoveComponentSignal : UiSignalTag {
        entt::id_type componentTypeId = 0;
    };

    template <typename ComponentType>
    struct UpdateComponentSignal : UiSignalTag {
        ComponentType value;
    };

    struct AddReflectedComponentSignal : UiSignalTag {
        std::string componentName;
    };

    struct RemoveReflectedComponentSignal : UiSignalTag {
        std::string componentName;
    };

    struct UpdateReflectedComponentFieldSignal : UiSignalTag {
        entt::entity entity = entt::null;
        std::string componentName;
        std::string fieldName;
        ComponentValue value;
    };

    using InspectorAddableComponentsTuple = std::tuple<
        PositionComponent,
        RotationComponent,
        ScaleComponent,
        CameraComponent,
        StaticMeshInstance,
        TickerScriptComponent,
        OnStartScriptComponent,
        DirectionalLightComponent,
        PointLightComponent,
        PBRMaterialComponent,
        DiffuseTextureComponent,
        ShininessComponent,
        NormalTextureComponent,
        EmissiveTextureComponent,
        OrmTextureComponent,
        ScriptFunctionTableComponent,
        RigidBodyComponent,
        BoxColliderComponent,
        SphereColliderComponent,
        CapsuleColliderComponent,
        PhysicsMaterialComponent
    >;

    using InspectorEditableComponentsTuple = std::tuple<
        NameTag,
        PositionComponent,
        RotationComponent,
        ScaleComponent,
        CameraComponent,
        DirectionalLightComponent,
        PointLightComponent,
        PBRMaterialComponent,
        ShininessComponent,
        ScriptFunctionTableComponent,
        SkyboxComponent,
        RigidBodyComponent,
        BoxColliderComponent,
        SphereColliderComponent,
        CapsuleColliderComponent,
        PhysicsMaterialComponent
    >;

    using InspectorRenderableComponentsTuple = std::tuple<
        NameTag,
        HierarchyComponent,
        PositionComponent,
        RotationComponent,
        ScaleComponent,
        CameraComponent,
        CacheRotationMatrix,
        CacheModelMatrix,
        StaticMeshInstance,
        TickerScriptComponent,
        OnStartScriptComponent,
        DirectionalLightComponent,
        PointLightComponent,
        PBRMaterialComponent,
        DiffuseTextureComponent,
        ShininessComponent,
        AlbedoTextureComponent,
        NormalTextureComponent,
        EmissiveTextureComponent,
        OrmTextureComponent,
        ScriptFunctionTableComponent,
        CacheScriptComponent,
        SkyboxComponent,
        CacheSkyboxComponent,
        RigidBodyComponent,
        BoxColliderComponent,
        SphereColliderComponent,
        CapsuleColliderComponent,
        PhysicsMaterialComponent
    >;

    struct PluginMenuEntryView {
        std::string name;
        bool isLoaded = false;
        bool isDynamic = false;
        bool isScript = false;
        uint32_t reloadCount = 0;
    };

    struct UserCodeMenuEntryView {
        std::string name;
        bool isLoaded = false;
        uint32_t reloadCount = 0;
    };

    struct MainMenuViewState : UiViewStateTag {
        bool isEngineRunning = false;
        bool canUndo = false;
        bool canRedo = false;
        std::string undoCommandName;
        std::string redoCommandName;
        bool autoHotReloadEnabled = true;
        std::vector<PluginMenuEntryView> plugins;
        std::vector<UserCodeMenuEntryView> userCodes;
    };

    struct ViewportViewState : UiViewStateTag {
        ImTextureID sceneTextureId = 0;
        float aspectRatio = 800.0f / 600.0f;
        bool usingEditorCamera = true;
        FCT::Vec3<> cameraPos{ 0.0f, 0.0f, 0.0f };
        FCT::Vec3<> cameraRot{ 0.0f, 0.0f, 0.0f };
        float moveSpeed = 25.0f;
        bool hasSelectedEntity = false;
        entt::entity selectedEntity = entt::null;
        std::string selectedEntityName;
        std::string selectedTrunkName;
        bool selectedIsGlobal = false;
        std::string defaultTrunkName;
    };

    struct CameraItemView {
        entt::entity entity = entt::null;
        entt::registry* registry = nullptr;
        std::string name;
        bool active = false;
    };

    struct CameraControlViewState : UiViewStateTag {
        bool usingEditorCamera = false;
        bool hasEditorCamera = false;
        FCT::Vec3<> editorCameraPos{ 0.0f, 35.0f, 75.0f };
        FCT::Vec3<> editorCameraRot{ -25.0f, -90.0f, 0.0f };
        float editorFov = 60.0f;
        float editorNearPlane = 0.5f;
        float editorFarPlane = 20000000.0f;
        bool editorReverseZ = true;
        float moveSpeed = 25.0f;
        float mouseSensitivity = 0.18f;
        float fastMultiplier = 4.0f;
        bool hasSelectedEntity = false;
        entt::entity selectedEntity = entt::null;
        std::string selectedTrunkName;
        bool selectedIsGlobal = false;
        std::vector<CameraItemView> sceneCameras;
        std::string activeCameraLabel = "未知场景相机";
    };

    struct EntityItemView {
        entt::entity entity = entt::null;
        entt::entity parentEntity = entt::null;
        int depth = 0;
        bool hasChildren = false;
        std::string displayName;
        bool isSelected = false;
    };

    struct TrunkEntityGroupView {
        std::string trunkName;
        bool isLoaded = false;
        bool hasSelectedEntityInTrunk = false;
        std::vector<EntityItemView> entities;
    };

    struct SceneEntityViewState : UiViewStateTag {
        bool hasActiveScene = false;
        std::string sceneName;
        bool hasSelectedGlobalEntity = false;
        entt::entity selectedGlobalEntity = entt::null;
        std::vector<EntityItemView> globalEntities;
        std::vector<TrunkEntityGroupView> trunkGroups;
    };

    struct EntityInspectorViewState : UiViewStateTag {
        bool hasSelectedEntity = false;
        bool isRegistryAvailable = false;
        bool isEntityValid = false;
        entt::entity entity = entt::null;
        bool isGlobal = false;
        std::string trunkName;
        const entt::registry* activeRegistry = nullptr;
        const ComponentReflection* componentReflection = nullptr;
    };

} // namespace MQEngine

#endif // EDITOR_UI_SIGNALS_H
