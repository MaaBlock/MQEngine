#include "EditorUiBehaviorController.h"
#include "../core/Global.h"
#include "../command/UndoManager.h"

using namespace FCT;

namespace MQEngine {

    void EditorUiBehaviorController::bindToLayer(UiLayer& uiLayer) {
        m_uiLayer = &uiLayer;
        EditorUiBehaviorProfile::apply(uiLayer.getUiContext(), *this);
    }

    void EditorUiBehaviorController::syncMainMenuState(MainMenuViewState& state) {
        state.isEngineRunning = Runtime::Runtime::instance().isPlaying();
        if (g_editorGlobal.undoManager) {
            state.canUndo = g_editorGlobal.undoManager->canUndo();
            state.canRedo = g_editorGlobal.undoManager->canRedo();
            state.undoCommandName = std::string(g_editorGlobal.undoManager->getUndoCommandName());
            state.redoCommandName = std::string(g_editorGlobal.undoManager->getRedoCommandName());
        } else {
            state.canUndo = false;
            state.canRedo = false;
            state.undoCommandName.clear();
            state.redoCommandName.clear();
        }

        state.plugins.clear();
        state.userCodes.clear();
        if (auto* engine = Runtime::Engine::getInstance()) {
            state.autoHotReloadEnabled = Runtime::Runtime::instance().isAutoHotReloadEnabled();
            if (auto* pm = engine->host->getPluginManager()) {
                for (const auto& name : pm->getAllPluginNames()) {
                    const Runtime::PluginEntry* entry = pm->getPluginEntry(name);
                    PluginMenuEntryView view;
                    view.name = name;
                    if (entry) {
                        view.isLoaded = (entry->state == Runtime::PluginState::Initialized || entry->state == Runtime::PluginState::Loaded);
                        view.isDynamic = entry->isDynamic;
                        view.isScript = entry->isScript;
                        view.reloadCount = entry->reloadCount;
                    }
                    state.plugins.push_back(std::move(view));
                }
            }
            if (auto* ucm = engine->host->getUserCodeManager()) {
                for (const auto& name : ucm->getAllUserCodeNames()) {
                    const Runtime::UserCodeEntry* entry = ucm->getUserCodeEntry(name);
                    UserCodeMenuEntryView view;
                    view.name = name;
                    if (entry) {
                        view.isLoaded = (entry->state == Runtime::UserCodeState::Initialized || entry->state == Runtime::UserCodeState::Loaded);
                        view.reloadCount = entry->reloadCount;
                    }
                    state.userCodes.push_back(std::move(view));
                }
            }
        }
    }

    void EditorUiBehaviorController::syncViewportState(ViewportViewState& state) {
        FCT::ImguiContext* imguiCtx = m_uiLayer ? m_uiLayer->getImguiContext() : g_editorGlobal.imguiContext;
        state.sceneTextureId = imguiCtx ? imguiCtx->getTexture("SceneColorTarget") : 0;
        state.aspectRatio = 800.0f / 600.0f;

        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (cameraMgr) {
            state.moveSpeed = cameraMgr->getMoveSpeed();
            entt::registry* ctrlReg = nullptr;
            entt::entity ctrlEnt = entt::null;
            if (cameraMgr->resolveControlledCamera(ctrlReg, ctrlEnt) && ctrlReg && ctrlReg->valid(ctrlEnt)) {
                state.usingEditorCamera = (ctrlReg == cameraMgr->getEditorRegistry());
                if (const auto* pos = ctrlReg->try_get<PositionComponent>(ctrlEnt)) {
                    state.cameraPos = pos->position;
                }
                if (const auto* rot = ctrlReg->try_get<RotationComponent>(ctrlEnt)) {
                    state.cameraRot = rot->rotation;
                }
            }
        }

        const auto& selected = g_editorGlobal.selectedEntity;
        state.hasSelectedEntity = (selected.scene != nullptr && selected.entity != entt::null);
        state.selectedEntity = selected.entity;
        state.selectedTrunkName = selected.trunkName;
        state.selectedIsGlobal = selected.isGlobal;
        state.selectedEntityName.clear();
        if (state.hasSelectedEntity) {
            if (entt::registry* selReg = resolveSelectedEntityRegistry(); selReg && selReg->valid(selected.entity)) {
                state.selectedEntityName = resolveEntityDisplayName(*selReg, selected.entity);
            }
        }

        state.defaultTrunkName.clear();
        if (g_editorGlobal.dataManager && g_editorGlobal.dataManager->getCurrentScene()) {
            Scene::Scene* scene = g_editorGlobal.dataManager->getCurrentScene();
            for (const auto& trunkName : scene->getTrunkList()) {
                if (scene->isLoad(trunkName)) {
                    state.defaultTrunkName = trunkName;
                    break;
                }
            }
        }
    }

    void EditorUiBehaviorController::syncCameraControlState(CameraControlViewState& state) {
        state.usingEditorCamera = false;
        state.hasEditorCamera = false;
        state.sceneCameras.clear();
        state.activeCameraLabel = "未知场景相机";

        const auto& selected = g_editorGlobal.selectedEntity;
        state.hasSelectedEntity = (selected.scene != nullptr && selected.entity != entt::null);
        state.selectedEntity = selected.entity;
        state.selectedTrunkName = selected.trunkName;
        state.selectedIsGlobal = selected.isGlobal;

        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (cameraMgr) {
            state.moveSpeed = cameraMgr->getMoveSpeed();
            state.mouseSensitivity = cameraMgr->getMouseSensitivity();
            state.fastMultiplier = cameraMgr->getFastMultiplier();

            auto editorEntity = cameraMgr->getEditorCameraEntity();
            auto* editorRegistry = cameraMgr->getEditorRegistry();
            if (editorEntity != entt::null && editorRegistry &&
                editorRegistry->valid(editorEntity) &&
                editorRegistry->all_of<CameraComponent>(editorEntity)) {
                state.hasEditorCamera = true;
                const auto& editorCamera = editorRegistry->get<CameraComponent>(editorEntity);
                state.usingEditorCamera = editorCamera.active;
                state.editorFov = editorCamera.fov;
                state.editorNearPlane = editorCamera.nearPlane;
                state.editorFarPlane = editorCamera.farPlane;
                state.editorReverseZ = editorCamera.reverseZ;
                if (const auto* pos = editorRegistry->try_get<PositionComponent>(editorEntity)) {
                    state.editorCameraPos = pos->position;
                }
                if (const auto* rot = editorRegistry->try_get<RotationComponent>(editorEntity)) {
                    state.editorCameraRot = rot->rotation;
                }
            }
        }

        if (!g_editorGlobal.dataManager) {
            return;
        }

        auto registries = g_editorGlobal.dataManager->currentRegistries();
        int cameraIndex = 0;
        for (auto* registry : registries) {
            if (!registry || (cameraMgr && registry == cameraMgr->getEditorRegistry())) {
                continue;
            }
            auto view = registry->view<CameraComponent>();
            for (auto entity : view) {
                const auto& camera = view.get<CameraComponent>(entity);
                CameraItemView info;
                info.entity = entity;
                info.registry = registry;
                info.active = camera.active;

                if (registry->all_of<NameTag>(entity)) {
                    const auto& nameTag = registry->get<NameTag>(entity);
                    info.name = nameTag.name + " (相机 " + std::to_string(cameraIndex) + ")";
                } else {
                    info.name = "相机 " + std::to_string(cameraIndex);
                }

                if (info.active && !state.usingEditorCamera) {
                    state.activeCameraLabel = info.name;
                }

                state.sceneCameras.push_back(std::move(info));
                ++cameraIndex;
            }
        }
    }

    void EditorUiBehaviorController::syncSceneEntityState(SceneEntityViewState& state) {
        state.hasActiveScene = false;
        state.sceneName.clear();
        state.hasSelectedGlobalEntity = false;
        state.selectedGlobalEntity = entt::null;
        state.globalEntities.clear();
        state.trunkGroups.clear();

        if (!g_editorGlobal.dataManager || !g_editorGlobal.dataManager->getCurrentScene()) {
            return;
        }

        Scene::Scene* scene = g_editorGlobal.dataManager->getCurrentScene();
        state.hasActiveScene = true;
        state.sceneName = scene->getName();

        if (auto* globalReg = scene->getRegistry()) {
            bool hasSelectedGlobal = false;
            collectEntityItems(*globalReg, true, "", state.globalEntities, hasSelectedGlobal);
            state.hasSelectedGlobalEntity = hasSelectedGlobal;
            if (hasSelectedGlobal) {
                state.selectedGlobalEntity = g_editorGlobal.selectedEntity.entity;
            }
        }

        for (const auto& trunkName : scene->getTrunkList()) {
            TrunkEntityGroupView group;
            group.trunkName = trunkName;
            group.isLoaded = scene->isLoad(trunkName);
            if (group.isLoaded) {
                if (SceneTrunk* trunk = scene->getLoadedTrunk(trunkName)) {
                    if (auto* trunkReg = trunk->getRegistry()) {
                        collectEntityItems(*trunkReg, false, trunkName, group.entities, group.hasSelectedEntityInTrunk);
                    }
                }
            }
            state.trunkGroups.push_back(std::move(group));
        }
    }

    void EditorUiBehaviorController::syncEntityInspectorState(EntityInspectorViewState& state) {
        const auto& selected = g_editorGlobal.selectedEntity;
        state.hasSelectedEntity = (selected.scene != nullptr && selected.entity != entt::null);
        state.entity = selected.entity;
        state.isGlobal = selected.isGlobal;
        state.trunkName = selected.trunkName;
        state.activeRegistry = nullptr;
        state.isRegistryAvailable = false;
        state.isEntityValid = false;
        state.componentReflection = resolveActiveReflection();

        if (!state.hasSelectedEntity) {
            return;
        }

        entt::registry* registry = resolveSelectedEntityRegistry();
        state.activeRegistry = registry;
        state.isRegistryAvailable = (registry != nullptr);
        if (registry) {
            state.isEntityValid = registry->valid(selected.entity);
        }
    }

    void EditorUiBehaviorController::onSaveProjectSetting(const SaveProjectSettingSignal&) {
        if (m_uiLayer && m_uiLayer->getEditorUiService()) {
            (void)m_uiLayer->getEditorUiService()->saveProjectSettings();
            return;
        }
        if (g_editorGlobal.dataManager) {
            g_editorGlobal.dataManager->saveProjectSetting(g_editorGlobal.dataManager->getProjectSetting());
        }
    }

    void EditorUiBehaviorController::onRequestResetLayout(const RequestResetLayoutSignal&) {
        if (m_uiLayer) {
            m_uiLayer->requestResetLayout();
        }
    }

    void EditorUiBehaviorController::onSetEngineRunning(const SetEngineRunningSignal& signal) {
        Runtime::Runtime::instance().setPlayMode(signal.running);
        if (!signal.running) {
            onActivateEditorCamera(ActivateEditorCameraSignal{});
        }
    }

    void EditorUiBehaviorController::onUndoCommand(const UndoCommandSignal&) {
        if (!g_editorGlobal.undoManager) {
            spdlog::warn("UndoManager 未就绪，无法执行撤销");
            return;
        }
        Status st = g_editorGlobal.undoManager->undo();
        if (!st.ok()) {
            spdlog::debug("撤销未执行或撤销栈为空: {}", st.message());
        }
    }

    void EditorUiBehaviorController::onRedoCommand(const RedoCommandSignal&) {
        if (!g_editorGlobal.undoManager) {
            spdlog::warn("UndoManager 未就绪，无法执行重做");
            return;
        }
        Status st = g_editorGlobal.undoManager->redo();
        if (!st.ok()) {
            spdlog::debug("重做未执行或重做栈为空: {}", st.message());
        }
    }

    void EditorUiBehaviorController::onHotReloadAll(const HotReloadAllSignal&) {
        Runtime::Runtime::instance().hotReloadAll();
    }

    void EditorUiBehaviorController::onReloadUserCode(const ReloadUserCodeSignal& signal) {
        (void)Runtime::Runtime::instance().reloadUserCode(signal.moduleName);
    }

    void EditorUiBehaviorController::onLoadPlugin(const LoadPluginSignal& signal) {
        if (!signal.pluginIdent.empty()) {
            (void)Runtime::Runtime::instance().loadPlugin(signal.pluginIdent);
        }
    }

    void EditorUiBehaviorController::onUnloadPlugin(const UnloadPluginSignal& signal) {
        if (!signal.pluginName.empty()) {
            (void)Runtime::Runtime::instance().unloadPlugin(signal.pluginName);
        }
    }

    void EditorUiBehaviorController::onReloadPlugin(const ReloadPluginSignal& signal) {
        if (!signal.pluginName.empty()) {
            (void)Runtime::Runtime::instance().reloadPlugin(signal.pluginName);
        }
    }

    void EditorUiBehaviorController::onSetAutoHotReload(const SetAutoHotReloadSignal& signal) {
        Runtime::Runtime::instance().setAutoHotReloadEnabled(signal.enabled);
    }

    Status EditorUiBehaviorController::onSaveActiveScene(const SaveActiveSceneSignal&) {
        if (m_uiLayer && m_uiLayer->getEditorUiService()) {
            return m_uiLayer->getEditorUiService()->saveActiveScene();
        }
        if (!g_editorGlobal.dataManager) {
            return FailedPreconditionError("DataManager 未初始化");
        }
        Scene::Scene* scene = g_editorGlobal.dataManager->getCurrentScene();
        if (!scene) {
            return NotFoundError("当前没有打开的场景");
        }
        CHECK_STATUS(scene->save());
        return OkStatus();
    }

    void EditorUiBehaviorController::onWindowCloseSave(const WindowCloseSaveSignal&) {
        if (m_uiLayer && m_uiLayer->getEditorUiService()) {
            (void)m_uiLayer->getEditorUiService()->saveProjectSettings();
            Status st = m_uiLayer->getEditorUiService()->saveActiveScene();
            if (!st.ok() && !IsNotFound(st)) {
                spdlog::error("保存场景失败: {}", st.message());
            }
            return;
        }
        if (!g_editorGlobal.dataManager) {
            return;
        }
        g_editorGlobal.dataManager->saveProjectSetting(g_editorGlobal.dataManager->getProjectSetting());
        if (auto* scene = g_editorGlobal.dataManager->getCurrentScene()) {
            Status st = scene->save();
            if (!st.ok()) {
                spdlog::error("保存场景失败: {}", st.message());
            }
        }
    }

    void EditorUiBehaviorController::onDumpDebugObjects(const DumpDebugObjectsSignal&) {
#ifdef FCT_DEBUG
        _output_object(fout);
        OutputDebugObject();
#endif
    }

    void EditorUiBehaviorController::onRecordDebugObjectDiff(const RecordDebugObjectDiffSignal&) {
#ifdef FCT_DEBUG
        fout << "输出距离上次记录新增:" << std::endl;
        static std::map<std::string, int> objectCount;
        std::map<std::string, int> currentCount;
        auto list = GetDebugObject();
        for (auto& obj : list) {
            currentCount[obj->describe]++;
        }
        for (auto& pair : currentCount) {
            if (pair.second > objectCount[pair.first]) {
                fout << std::oct << "新增:" << pair.first
                     << ", 新增数量:" << pair.second - objectCount[pair.first] << std::endl;
            }
        }
        objectCount = currentCount;
#endif
    }

    void EditorUiBehaviorController::onImportGodotScene(const ImportGodotSceneSignal& signal) {
        if (signal.filePath.empty()) {
            return;
        }
        if (Runtime::Engine::getInstance()) {
            Status st = Runtime::Runtime::instance().loadPlugin("GodotImporter");
            if (!st.ok()) {
                spdlog::error("加载 GodotImporter 插件失败: {}", st.message());
                return;
            }
        }

        auto* engine = Runtime::Engine::getInstance();
        auto* scriptSys = engine ? engine->script->scriptSystem.get() : nullptr;
        if (!scriptSys || !scriptSys->getPyEnvironment()) {
            spdlog::error("FCT_PyBri Python 环境未初始化，无法执行 Godot 导入");
            return;
        }

        auto* pyEnv = scriptSys->getPyEnvironment();
        std::string normalizedPath = SAI::Path::normalize(signal.filePath);

        if (SAI::Path::hasExtension(normalizedPath, ".gd")) {
            try {
                std::string clsName = pyEnv->callFunction<std::string>("importGdScriptFile", normalizedPath, std::string(""));
                if (!clsName.empty() && g_editorGlobal.selectedEntity.entity != entt::null) {
                    onBindScriptToEntity(BindScriptToEntitySignal{
                        .entity = g_editorGlobal.selectedEntity.entity,
                        .trunkName = g_editorGlobal.selectedEntity.trunkName,
                        .isGlobal = g_editorGlobal.selectedEntity.isGlobal,
                        .scriptName = clsName,
                        .mode = ScriptBindMode::ClassBinding
                    });
                }
            } catch (const std::exception& e) {
                spdlog::error("导入 GDScript 失败: {}", e.what());
            }
            return;
        }

        std::string trunkName = g_editorGlobal.selectedEntity.trunkName;
        if (trunkName.empty() && g_editorGlobal.dataManager && g_editorGlobal.dataManager->getCurrentScene()) {
            Scene::Scene* scene = g_editorGlobal.dataManager->getCurrentScene();
            for (const auto& t : scene->getTrunkList()) {
                if (scene->isLoad(t)) {
                    trunkName = t;
                    break;
                }
            }
        }

        try {
            int rootEntityId = pyEnv->callFunction<int>("importGodotScene", normalizedPath, trunkName);
            if (rootEntityId > 0) {
                onSelectSceneEntity(SelectSceneEntitySignal{
                    .entity = static_cast<entt::entity>(rootEntityId),
                    .trunkName = trunkName,
                    .isGlobal = trunkName.empty()
                });
            }
        } catch (const std::exception& e) {
            spdlog::error("导入 Godot 场景失败: {}", e.what());
        }
    }

    void EditorUiBehaviorController::onActivateEditorCamera(const ActivateEditorCameraSignal&) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        auto* engine = Runtime::Engine::getInstance();
        if (!cameraMgr || !engine || !engine->renderer->getCameraSystem()) {
            return;
        }
        auto editorEntity = cameraMgr->getEditorCameraEntity();
        auto* editorRegistry = cameraMgr->getEditorRegistry();
        if (editorEntity != entt::null && editorRegistry) {
            engine->renderer->getCameraSystem()->setActiveCamera(editorRegistry, editorEntity);
        }
    }

    void EditorUiBehaviorController::onActivateSceneCamera(const ActivateSceneCameraSignal& signal) {
        auto* engine = Runtime::Engine::getInstance();
        if (engine && engine->renderer->getCameraSystem() && signal.registry && signal.entity != entt::null) {
            engine->renderer->getCameraSystem()->setActiveCamera(signal.registry, signal.entity);
        }
    }

    void EditorUiBehaviorController::onUpdateEditorCameraTransform(const UpdateEditorCameraTransformSignal& signal) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (!cameraMgr) {
            return;
        }
        auto editorEntity = cameraMgr->getEditorCameraEntity();
        auto* editorRegistry = cameraMgr->getEditorRegistry();
        if (editorEntity != entt::null && editorRegistry && editorRegistry->valid(editorEntity)) {
            editorRegistry->get_or_emplace<PositionComponent>(editorEntity).position = signal.position;
            editorRegistry->get_or_emplace<RotationComponent>(editorEntity).rotation = signal.rotation;
        }
    }

    void EditorUiBehaviorController::onUpdateEditorCameraSettings(const UpdateEditorCameraSettingsSignal& signal) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (!cameraMgr) {
            return;
        }
        cameraMgr->setMoveSpeed(signal.moveSpeed);
        cameraMgr->setMouseSensitivity(signal.mouseSensitivity);
        cameraMgr->setFastMultiplier(signal.fastMultiplier);

        auto editorEntity = cameraMgr->getEditorCameraEntity();
        auto* editorRegistry = cameraMgr->getEditorRegistry();
        if (editorEntity != entt::null && editorRegistry && editorRegistry->valid(editorEntity)) {
            auto& cam = editorRegistry->get_or_emplace<CameraComponent>(editorEntity);
            cam.fov = signal.fov;
            cam.nearPlane = signal.nearPlane;
            cam.farPlane = signal.farPlane;
            cam.reverseZ = signal.reverseZ;
        }
    }

    void EditorUiBehaviorController::onSyncEditorCameraFromScene(const SyncEditorCameraFromSceneSignal&) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (cameraMgr) {
            cameraMgr->syncFromSceneCamera();
            cameraMgr->hookCamera();
        }
    }

    void EditorUiBehaviorController::onSyncSceneCameraFromEditor(const SyncSceneCameraFromEditorSignal& signal) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (cameraMgr) {
            cameraMgr->syncToSceneCamera(signal.targetRegistry, signal.targetEntity);
        }
    }

    void EditorUiBehaviorController::onResetEditorCamera(const ResetEditorCameraSignal&) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        if (cameraMgr) {
            cameraMgr->resetCamera();
            cameraMgr->hookCamera();
        }
    }

    void EditorUiBehaviorController::onFocusEntity(const FocusEntitySignal& signal) {
        EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager;
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        if (!cameraMgr || !scene) {
            return;
        }
        entt::entity targetEnt = signal.entity != entt::null ? signal.entity : g_editorGlobal.selectedEntity.entity;
        bool isGlobal = signal.entity != entt::null ? signal.isGlobal : g_editorGlobal.selectedEntity.isGlobal;
        std::string trunkName = signal.entity != entt::null ? signal.trunkName : g_editorGlobal.selectedEntity.trunkName;

        entt::registry* reg = resolveEntityRegistry(scene, isGlobal, trunkName);
        if (reg && targetEnt != entt::null && reg->valid(targetEnt)) {
            Vec3<> worldPos = EditorCameraManager::queryEntityWorldPosition(reg, targetEnt);
            cameraMgr->focusOnPosition(worldPos, std::clamp(cameraMgr->getMoveSpeed() * 1.2f, 8.0f, 250.0f));
        }
    }

    void EditorUiBehaviorController::onSelectSceneEntity(const SelectSceneEntitySignal& signal) {
        g_editorGlobal.selectedEntity.entity = signal.entity;
        g_editorGlobal.selectedEntity.trunkName = signal.trunkName;
        g_editorGlobal.selectedEntity.isGlobal = signal.isGlobal;
        g_editorGlobal.selectedEntity.scene = g_editorGlobal.dataManager
            ? g_editorGlobal.dataManager->getCurrentScene()
            : nullptr;
    }

    void EditorUiBehaviorController::onCreateSceneEntity(const CreateSceneEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry) {
            return;
        }
        auto newEntity = registry->create();
        registry->emplace<NameTag>(newEntity, signal.name);
        registry->emplace<PositionComponent>(newEntity, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
        registry->emplace<RotationComponent>(newEntity, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
        registry->emplace<ScaleComponent>(newEntity, FCT::Vec3{ 1.0f, 1.0f, 1.0f });

        switch (signal.preset) {
        case EntityPresetType::Camera: {
            auto& cam = registry->emplace<CameraComponent>(newEntity);
            cam.active = false;
            cam.fov = 60.0f;
            cam.nearPlane = 0.5f;
            cam.farPlane = 100000.0f;
            break;
        }
        case EntityPresetType::DirectionalLight: {
            auto& light = registry->emplace<DirectionalLightComponent>(newEntity);
            light.direction = FCT::Vec3(-0.4f, -0.85f, -0.35f);
            light.color = FCT::Vec3(1.0f, 0.98f, 0.92f);
            light.intensity = 2.0f;
            light.enabled = true;
            break;
        }
        case EntityPresetType::PointLight: {
            auto& light = registry->emplace<PointLightComponent>(newEntity);
            light.color = FCT::Vec3(1.0f, 0.95f, 0.85f);
            light.intensity = 3.0f;
            light.range = 25.0f;
            light.enabled = true;
            break;
        }
        case EntityPresetType::CustomComponent: {
            if (!signal.customComponentName.empty()) {
                if (const ComponentReflection* refl = resolveActiveReflection()) {
                    if (const auto* info = refl->findComponent(signal.customComponentName)) {
                        if (info->addComponent) {
                            info->addComponent(*registry, newEntity);
                        }
                    }
                }
            }
            break;
        }
        case EntityPresetType::Empty:
        default:
            break;
        }

        if (signal.parentEntity != entt::null && registry->valid(signal.parentEntity)) {
            DataManager::setRegistryEntityParent(registry, newEntity, signal.parentEntity);
        }

        onSelectSceneEntity(SelectSceneEntitySignal{
            .entity = newEntity,
            .trunkName = signal.trunkName,
            .isGlobal = signal.isGlobal
        });
    }

    void EditorUiBehaviorController::onDuplicateSceneEntity(const DuplicateSceneEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry || signal.entity == entt::null || !registry->valid(signal.entity)) {
            return;
        }

        entt::entity src = signal.entity;
        entt::entity dst = registry->create();

        std::string copyName = resolveEntityDisplayName(*registry, src) + "_Copy";
        registry->emplace<NameTag>(dst, copyName);

        if (const auto* pos = registry->try_get<PositionComponent>(src)) {
            registry->emplace<PositionComponent>(dst, pos->position + FCT::Vec3(1.0f, 0.0f, 1.0f));
        }
        if (const auto* rot = registry->try_get<RotationComponent>(src)) {
            registry->emplace<RotationComponent>(dst, *rot);
        }
        if (const auto* scale = registry->try_get<ScaleComponent>(src)) {
            registry->emplace<ScaleComponent>(dst, *scale);
        }
        if (const auto* cam = registry->try_get<CameraComponent>(src)) {
            CameraComponent copiedCam = *cam;
            copiedCam.active = false;
            registry->emplace<CameraComponent>(dst, copiedCam);
        }
        if (const auto* mesh = registry->try_get<StaticMeshInstance>(src)) {
            registry->emplace<StaticMeshInstance>(dst, mesh->modelUuid, mesh->meshName);
        }
        if (const auto* dLight = registry->try_get<DirectionalLightComponent>(src)) {
            registry->emplace<DirectionalLightComponent>(dst, *dLight);
        }
        if (const auto* pLight = registry->try_get<PointLightComponent>(src)) {
            registry->emplace<PointLightComponent>(dst, *pLight);
        }
        if (const auto* pbr = registry->try_get<PBRMaterialComponent>(src)) {
            registry->emplace<PBRMaterialComponent>(dst, *pbr);
        }
        if (const auto* diffTex = registry->try_get<DiffuseTextureComponent>(src)) {
            registry->emplace<DiffuseTextureComponent>(dst, diffTex->modelUuid, diffTex->texturePath);
        }
        if (const auto* normTex = registry->try_get<NormalTextureComponent>(src)) {
            registry->emplace<NormalTextureComponent>(dst, normTex->modelUuid, normTex->texturePath);
        }
        if (const auto* scriptTbl = registry->try_get<ScriptFunctionTableComponent>(src)) {
            registry->emplace<ScriptFunctionTableComponent>(dst, *scriptTbl);
        }
        if (const auto* h = registry->try_get<HierarchyComponent>(src)) {
            entt::entity parent = static_cast<entt::entity>(h->parentEntity);
            if (parent != entt::null && registry->valid(parent)) {
                DataManager::setRegistryEntityParent(registry, dst, parent);
            }
        }

        onSelectSceneEntity(SelectSceneEntitySignal{
            .entity = dst,
            .trunkName = signal.trunkName,
            .isGlobal = signal.isGlobal
        });
    }

    void EditorUiBehaviorController::onRenameSceneEntity(const RenameSceneEntitySignal& signal) {
        if (signal.newName.empty()) {
            return;
        }
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry || signal.entity == entt::null || !registry->valid(signal.entity)) {
            return;
        }
        if (auto* nameTag = registry->try_get<NameTag>(signal.entity)) {
            nameTag->name = signal.newName;
        } else {
            registry->emplace<NameTag>(signal.entity, signal.newName);
        }
    }

    void EditorUiBehaviorController::onSpawnModelInScene(const SpawnModelInSceneSignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        if (!scene) {
            return;
        }

        bool isGlobal = signal.isGlobal;
        std::string trunkName = signal.trunkName;
        if (!isGlobal && trunkName.empty()) {
            for (const auto& t : scene->getTrunkList()) {
                if (scene->isLoad(t)) {
                    trunkName = t;
                    break;
                }
            }
            if (trunkName.empty()) {
                isGlobal = true;
            }
        }

        entt::registry* registry = resolveEntityRegistry(scene, isGlobal, trunkName);
        if (!registry) {
            return;
        }

        FCT::Vec3<> spawnPos(0.0f, 0.0f, 0.0f);
        if (EditorCameraManager* cameraMgr = m_uiLayer ? m_uiLayer->getCameraManager() : g_editorGlobal.cameraManager) {
            spawnPos = cameraMgr->getOrbitPivot();
        }

        entt::entity createdEntity = registry->create();
        std::string entityName = !signal.meshName.empty()
            ? signal.meshName
            : (!signal.modelName.empty() ? signal.modelName : "ModelEntity");

        registry->emplace<NameTag>(createdEntity, entityName);
        registry->emplace<PositionComponent>(createdEntity, spawnPos);
        registry->emplace<RotationComponent>(createdEntity, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
        registry->emplace<ScaleComponent>(createdEntity, FCT::Vec3{ 1.0f, 1.0f, 1.0f });

        if (!signal.meshName.empty()) {
            registry->emplace<StaticMeshInstance>(createdEntity, signal.modelUuid, signal.meshName);
        } else if (!signal.modelUuid.empty() && g_editorGlobal.dataManager) {
            std::string modelDir = g_editorGlobal.dataManager->getModelPathByUuid(signal.modelUuid);
            std::string indexFile = SAI::Path::join(modelDir, "index.edit.dat");
            auto* loader = Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
            auto sceneInfoOr = ModelAssetSerializer::loadModelIndex(loader, indexFile);
            if (sceneInfoOr.ok()) {
                const auto& sceneInfo = sceneInfoOr.value();
                if (sceneInfo.meshInfos.size() == 1) {
                    registry->emplace<StaticMeshInstance>(createdEntity, signal.modelUuid, sceneInfo.meshInfos[0].name);
                    if (!sceneInfo.textureInfos.empty()) {
                        registry->emplace<DiffuseTextureComponent>(createdEntity, signal.modelUuid, sceneInfo.textureInfos[0].path);
                    }
                } else {
                    for (const auto& meshInfo : sceneInfo.meshInfos) {
                        entt::entity childEnt = registry->create();
                        registry->emplace<NameTag>(childEnt, meshInfo.name);
                        registry->emplace<PositionComponent>(childEnt, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
                        registry->emplace<RotationComponent>(childEnt, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
                        registry->emplace<ScaleComponent>(childEnt, FCT::Vec3{ 1.0f, 1.0f, 1.0f });
                        registry->emplace<StaticMeshInstance>(childEnt, signal.modelUuid, meshInfo.name);
                        if (!sceneInfo.textureInfos.empty()) {
                            registry->emplace<DiffuseTextureComponent>(childEnt, signal.modelUuid, sceneInfo.textureInfos[0].path);
                        }
                        DataManager::setRegistryEntityParent(registry, childEnt, createdEntity);
                    }
                }
            }
        }

        if (signal.parentEntity != entt::null && registry->valid(signal.parentEntity)) {
            DataManager::setRegistryEntityParent(registry, createdEntity, signal.parentEntity);
        }

        onSelectSceneEntity(SelectSceneEntitySignal{
            .entity = createdEntity,
            .trunkName = trunkName,
            .isGlobal = isGlobal
        });
    }

    void EditorUiBehaviorController::onDeleteSceneEntity(const DeleteSceneEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        if (!scene || signal.entity == entt::null) {
            return;
        }
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (registry && registry->valid(signal.entity)) {
            DataManager::destroyRegistryEntityHierarchy(registry, signal.entity);
            if (!registry->valid(g_editorGlobal.selectedEntity.entity)) {
                g_editorGlobal.selectedEntity = SelectedEntityState{};
            }
        }
    }

    void EditorUiBehaviorController::onReparentSceneEntity(const ReparentSceneEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        if (!scene || signal.childEntity == entt::null) {
            return;
        }
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (registry && registry->valid(signal.childEntity)) {
            DataManager::setRegistryEntityParent(registry, signal.childEntity, signal.parentEntity);
        }
    }

    void EditorUiBehaviorController::onBindScriptToEntity(const BindScriptToEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry || !registry->valid(signal.entity)) {
            return;
        }

        switch (signal.mode) {
        case ScriptBindMode::ClassBinding: {
            auto& component = registry->get_or_emplace<ScriptFunctionTableComponent>(signal.entity);
            component.className = signal.scriptName;
            break;
        }
        case ScriptBindMode::StandaloneTicker:
            registry->emplace_or_replace<TickerScriptComponent>(signal.entity, signal.scriptName);
            break;
        case ScriptBindMode::StandaloneOnStart:
            registry->emplace_or_replace<OnStartScriptComponent>(signal.entity, signal.scriptName);
            break;
        case ScriptBindMode::FunctionTable: {
            auto& component = registry->get_or_emplace<ScriptFunctionTableComponent>(signal.entity);
            auto it = std::find(component.onTicker.begin(), component.onTicker.end(), signal.scriptName);
            if (it == component.onTicker.end()) {
                component.onTicker.push_back(signal.scriptName);
            }
            break;
        }
        }
    }

    void EditorUiBehaviorController::onBindMeshToEntity(const BindMeshToEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry || !registry->valid(signal.entity)) {
            return;
        }
        registry->emplace_or_replace<StaticMeshInstance>(signal.entity, signal.modelUuid, signal.meshName);
    }

    void EditorUiBehaviorController::onBindTextureToEntity(const BindTextureToEntitySignal& signal) {
        Scene::Scene* scene = g_editorGlobal.dataManager ? g_editorGlobal.dataManager->getCurrentScene() : nullptr;
        entt::registry* registry = resolveEntityRegistry(scene, signal.isGlobal, signal.trunkName);
        if (!registry || !registry->valid(signal.entity)) {
            return;
        }

        switch (signal.slot) {
        case TextureBindSlot::Diffuse:
            registry->emplace_or_replace<DiffuseTextureComponent>(signal.entity, signal.modelUuid, signal.texturePath);
            break;
        case TextureBindSlot::Normal:
            registry->emplace_or_replace<NormalTextureComponent>(signal.entity, signal.modelUuid, signal.texturePath);
            break;
        case TextureBindSlot::Albedo:
            registry->emplace_or_replace<AlbedoTextureComponent>(signal.entity, signal.modelUuid, signal.texturePath);
            break;
        case TextureBindSlot::Emissive:
            registry->emplace_or_replace<EmissiveTextureComponent>(signal.entity, signal.modelUuid, signal.texturePath);
            break;
        case TextureBindSlot::Orm:
            registry->emplace_or_replace<OrmTextureComponent>(signal.entity, signal.modelUuid, signal.texturePath);
            break;
        }
    }

    void EditorUiBehaviorController::onRemoveComponent(const RemoveComponentSignal& signal) {
        if (signal.componentTypeId == 0) {
            return;
        }
        entt::registry* registry = resolveSelectedEntityRegistry();
        entt::entity entity = g_editorGlobal.selectedEntity.entity;
        if (!registry || !registry->valid(entity)) {
            return;
        }
        if (auto* storage = registry->storage(signal.componentTypeId)) {
            if (storage->contains(entity)) {
                storage->remove(entity);
            }
        }
    }

    void EditorUiBehaviorController::onAddSkyboxComponent(const AddSkyboxComponentSignal& signal) {
        entt::registry* registry = resolveSelectedEntityRegistry();
        entt::entity entity = g_editorGlobal.selectedEntity.entity;
        if (!registry || !registry->valid(entity) || registry->all_of<SkyboxComponent>(entity)) {
            return;
        }
        registry->emplace<SkyboxComponent>(entity, signal.texturePath);
    }

    void EditorUiBehaviorController::onAddReflectedComponent(const AddReflectedComponentSignal& signal) {
        if (signal.componentName.empty()) {
            return;
        }
        entt::registry* registry = resolveSelectedEntityRegistry();
        entt::entity entity = g_editorGlobal.selectedEntity.entity;
        const ComponentReflection* reflection = resolveActiveReflection();
        if (!registry || !registry->valid(entity) || !reflection) {
            return;
        }
        const auto* info = reflection->findComponent(signal.componentName);
        if (info && info->addComponent && (!info->hasComponent || !info->hasComponent(*registry, entity))) {
            info->addComponent(*registry, entity);
        }
    }

    void EditorUiBehaviorController::onRemoveReflectedComponent(const RemoveReflectedComponentSignal& signal) {
        if (signal.componentName.empty()) {
            return;
        }
        entt::registry* registry = resolveSelectedEntityRegistry();
        entt::entity entity = g_editorGlobal.selectedEntity.entity;
        const ComponentReflection* reflection = resolveActiveReflection();
        if (!registry || !registry->valid(entity) || !reflection) {
            return;
        }
        const auto* info = reflection->findComponent(signal.componentName);
        if (info && info->removeComponent && info->hasComponent && info->hasComponent(*registry, entity)) {
            info->removeComponent(*registry, entity);
        }
    }

    void EditorUiBehaviorController::onUpdateReflectedComponentField(const UpdateReflectedComponentFieldSignal& signal) {
        if (signal.componentName.empty() || signal.fieldName.empty()) {
            return;
        }
        entt::registry* registry = resolveSelectedEntityRegistry();
        if (!registry || !registry->valid(signal.entity)) {
            return;
        }
        const ComponentReflection* reflection = resolveActiveReflection();
        if (!reflection) {
            return;
        }
        const auto* info = reflection->findComponent(signal.componentName);
        if (!info || !info->getComponent || !info->hasComponent || !info->hasComponent(*registry, signal.entity)) {
            return;
        }
        void* compPtr = info->getComponent(*registry, signal.entity);
        if (!compPtr) {
            return;
        }
        for (const auto& field : info->fields) {
            if (field.name == signal.fieldName && field.setter) {
                field.setter(compPtr, signal.value);
                break;
            }
        }
    }

    const ComponentReflection* EditorUiBehaviorController::resolveActiveReflection() const {
        if (auto* engine = Runtime::Engine::getInstance()) {
            return engine->foundation->getComponentReflection();
        }
        return nullptr;
    }

    entt::registry* EditorUiBehaviorController::resolveEntityRegistry(
        Scene::Scene* scene, bool isGlobal, const std::string& trunkName) const {
        if (!scene) {
            return nullptr;
        }
        if (isGlobal) {
            return scene->getRegistry();
        }
        SceneTrunk* trunk = scene->getLoadedTrunk(trunkName);
        return trunk ? trunk->getRegistry() : nullptr;
    }

    entt::registry* EditorUiBehaviorController::resolveSelectedEntityRegistry() const {
        const auto& selected = g_editorGlobal.selectedEntity;
        return resolveEntityRegistry(selected.scene, selected.isGlobal, selected.trunkName);
    }

    std::string EditorUiBehaviorController::resolveEntityDisplayName(
        const entt::registry& registry, entt::entity entity) {
        if (const auto* nameTag = registry.try_get<NameTag>(entity)) {
            return nameTag->name;
        }
        return "entity " + std::to_string(static_cast<uint32_t>(entity));
    }

    void EditorUiBehaviorController::collectEntityItems(
        entt::registry& registry,
        bool isGlobal,
        const std::string& trunkName,
        std::vector<EntityItemView>& outItems,
        bool& outHasSelected) const {
        outHasSelected = false;
        const auto& selected = g_editorGlobal.selectedEntity;

        std::vector<entt::entity> visibleEntities;
        std::unordered_map<entt::entity, std::vector<entt::entity>> childrenMap;
        std::unordered_map<entt::entity, entt::entity> parentMap;

        registry.view<entt::entity>().each([&](auto entity) {
            if (const auto* subTag = registry.try_get<RuntimeSubEntityTag>(entity)) {
                if (subTag->hideInHierarchy) {
                    return;
                }
            }
            visibleEntities.push_back(entity);
        });

        for (auto entity : visibleEntities) {
            entt::entity parent = entt::null;
            if (const auto* h = registry.try_get<HierarchyComponent>(entity)) {
                entt::entity candidate = static_cast<entt::entity>(h->parentEntity);
                if (candidate != entt::null && candidate != entity && registry.valid(candidate)) {
                    if (const auto* parentSub = registry.try_get<RuntimeSubEntityTag>(candidate);
                        !parentSub || !parentSub->hideInHierarchy) {
                        parent = candidate;
                    }
                }
            }
            parentMap[entity] = parent;
            if (parent != entt::null) {
                childrenMap[parent].push_back(entity);
            }
        }

        std::set<entt::entity> visited;
        auto appendSubtree = [&](auto& self, entt::entity entity, int depth) -> void {
            if (!visited.insert(entity).second) {
                return;
            }
            bool isSelected = (selected.entity == entity &&
                               selected.isGlobal == isGlobal &&
                               (isGlobal || selected.trunkName == trunkName));
            if (isSelected) {
                outHasSelected = true;
            }
            const auto childrenIt = childrenMap.find(entity);
            bool hasChildren = (childrenIt != childrenMap.end() && !childrenIt->second.empty());
            outItems.push_back(EntityItemView{
                .entity = entity,
                .parentEntity = parentMap[entity],
                .depth = depth,
                .hasChildren = hasChildren,
                .displayName = resolveEntityDisplayName(registry, entity),
                .isSelected = isSelected
            });
            if (hasChildren) {
                for (auto child : childrenIt->second) {
                    self(self, child, depth + 1);
                }
            }
        };

        for (auto entity : visibleEntities) {
            if (parentMap[entity] == entt::null) {
                appendSubtree(appendSubtree, entity, 0);
            }
        }
        for (auto entity : visibleEntities) {
            if (!visited.contains(entity)) {
                appendSubtree(appendSubtree, entity, 0);
            }
        }
    }

} // namespace MQEngine
