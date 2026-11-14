/**
 * @file SceneViewerComposite.h
 * @brief 场景视口、层级大纲、相机控制与实体属性检视器 Composite
 */
#ifndef MQENGINE_MQ_EDITOR_SCENE_VIEWER_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_SCENE_VIEWER_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "SceneViewerTypes.h"
#include "ScenePrimitiveMeshes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>
#include <Composites/EditorUiComposite/CommandStack.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        enum class PieStartMode : uint8_t
        {
            FromMainScene = 0,
            FromCurrentScene = 1
        };

        /**
         * @brief 实体子树剪贴板/快照节点，保存界面描述与类型擦除完整 ECS 组件快照
         */
        struct EntityClipboardNode
        {
            SceneEntityDesc desc;
            int32_t parentIndex = -1;
            std::vector<EditorBase::DynamicComponentBlob> dynamicComponents;
        };

        class SubtreeInstantiateCommand;

        /**
         * @brief 场景视口与实体检视 Composite：
         *        独占负责 3D Viewport 渲染与相机操纵、3D Gizmo 与射线拾取、场景层级大纲树、相机控制面板与 Inspector 属性检视面板
         *        Note: ECS 时间轴与回放由 ReplayTimelineComposite 独占负责，不在本组件中重复注册。
         */
        class SCENE_VIEWER_COMPOSITE_API SceneViewerComposite : public Asset::Composite
        {
        public:
            SceneViewerComposite();
            ~SceneViewerComposite() override;

            SceneViewerComposite(const SceneViewerComposite&) = delete;
            SceneViewerComposite& operator=(const SceneViewerComposite&) = delete;

            // ==================== Composite 生命周期接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "SceneViewerComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor"}; }

        private:
            // ==================== 3D 视口面板 (SceneViewportPanel.cpp) ====================

            void renderViewport();
            void renderViewportToolbar();
            void handleViewportCameraInput(bool isHovered, float deltaTime);
            void render3dGridAndAxes(ImDrawList* drawList, const FCT::Mat4& viewProj, const ImVec2& canvasP0, const ImVec2& avail);
            void render3dEntities(ImDrawList* drawList, const FCT::Mat4& viewProj, const ImVec2& canvasP0, const ImVec2& avail);
            void renderLightGizmos(ImDrawList* drawList, const FCT::Mat4& viewProj, const ImVec2& canvasP0, const ImVec2& avail, bool isHovered);
            void renderAndHandle3dGizmo(ImDrawList* drawList, const FCT::Mat4& viewProj, const ImVec2& canvasP0, const ImVec2& avail, bool isHovered);
            void handleViewportPicking(const ImVec2& canvasP0, const ImVec2& avail, const FCT::Mat4& viewProj, bool isHovered);

            // ==================== 层级大纲面板 (SceneHierarchyPanel.cpp) ====================

            void renderHierarchy();
            void renderHierarchyNode(SceneEntityDesc& entity);
            void renderTrunkStreamingSection();
            void renderCategorizedEntityCreateMenus(uint32_t parentId = 0);
            void handleHierarchyAndViewportShortcuts(bool allowShortcut);

            // ==================== 属性检视与相机控制面板 (SceneInspectorPanel.cpp) ====================

            void renderInspector();
            void renderCameraControl();

            // ==================== 空间投影、射线构建与 Domain 同步 (SceneViewerComposite.cpp) ====================

            bool projectWorldToScreen(
                const FCT::Vec3<>& worldPos,
                const FCT::Mat4& viewProj,
                const ImVec2& canvasP0,
                const ImVec2& avail,
                ImVec2& outScreen) const;

            void buildCameraRay(
                const ImVec2& mouseScreen,
                const ImVec2& canvasP0,
                const ImVec2& avail,
                FCT::Vec3<>& outOrigin,
                FCT::Vec3<>& outDir) const;

            FCT::Mat4 computeViewMatrix() const;
            FCT::Mat4 computeProjMatrix(float aspect) const;

            void syncEntityToDomain(SceneEntityDesc& entity);
            void syncEntitiesFromDomain();
            void syncCameraToRenderer();
            void updatePieSimulation(float deltaTime);

            SceneEntityDesc* getSelectedEntity();
            SceneEntityDesc* findEntityById(uint32_t id);

            // 视口双相机解耦与透视/正交矩阵辅助
            void registerSceneSignalHandlers();
            void syncProjectStartupStateIfReady();
            void loadInitialStartupScene();
            void updateEditorUiSceneState() const;
            std::string resolveProjectPath(const std::string& path) const;
            std::string toProjectRelativePath(const std::string& path) const;
            std::string getEditorSessionFilePath() const;
            void loadEditorSession();
            void saveEditorSession() const;
            std::string getActiveViewportTargetName() const noexcept;
            void ensureEditorCameraEntity();
            void savePrePieState();
            void restorePrePieState();
            std::string getStartupSceneDisplayHint() const;
            FCT::Mat4 computeEditorViewMatrix() const;
            FCT::Mat4 computeEditorProjMatrix(float aspect) const;
            FCT::ECS::registry* resolveEntityRegistry(const SceneEntityDesc& entity) const;
            bool isEntityPassingFilter(const SceneEntityDesc& entity) const;
            void syncFilterVisibilityToScene() const;
            void createNewEntity(const std::string& name, uint32_t parentId = 0);
            void deleteEntity(uint32_t id);
            void setEntityParent(uint32_t childId, uint32_t newParentId);

            // 唯一命名、UUID 生成与预设创建 / 子树复制粘贴克隆辅助
            std::string generateEntityUuid() const;
            std::string makeUniqueEntityName(const std::string& baseName, const std::string& registryName) const;
            SceneEntityDesc* createEntityFromPreset(EntityPresetKind preset, uint32_t parentId = 0);
            void collectSubtreeSnapshot(uint32_t rootEntityId, std::vector<EntityClipboardNode>& outNodes) const;
            std::vector<uint32_t> instantiateSubtreeSnapshot(
                const std::vector<EntityClipboardNode>& nodes,
                uint32_t targetParentId,
                const std::string& targetRegistry,
                bool pushUndoCommand = true,
                const std::string& commandLabel = "Paste Entity Subtree");
            std::vector<uint32_t> commitSubtreeInstantiationDirect(
                const std::vector<EntityClipboardNode>& resolvedNodes,
                const std::string& targetRegistry);
            void removeSubtreeEntitiesDirect(const std::vector<uint32_t>& entityIds);
            void copyEntityToClipboard(uint32_t rootEntityId);
            void pasteEntityFromClipboard(uint32_t overrideParentId = 0, bool keepSourceParent = true);
            void duplicateEntitySubtree(uint32_t rootEntityId);
            bool hasClipboardEntities() const noexcept { return !m_entityClipboard.empty(); }

            friend class SubtreeInstantiateCommand;

        public:
            // 只读视口相机模式与活跃矩阵查询接口（支持编辑态与 PIE 运行态随时切换自由相机/游戏主相机/指定相机实体）
            ViewportCameraSource getViewportCameraSource() const noexcept { return m_cameraSource; }
            void setViewportCameraSource(ViewportCameraSource source) noexcept;
            void setSpecificCameraEntity(const std::string& registryName, FCT::ECS::entity entity) noexcept;
            const std::string& getSpecificCameraRegistry() const noexcept { return m_specificCameraRegistry; }
            FCT::ECS::entity getSpecificCameraEntity() const noexcept { return m_specificCameraEcsEntity; }
            void toggleEditorAndGameCamera() noexcept;
            void alignEditorCameraToCurrentView() noexcept;
            bool isEditorFreeCameraActive() const noexcept;
            bool isGameCameraPreviewActive() const noexcept;
            void setGameCameraPreviewActive(bool active) noexcept;
            FCT::Mat4 getActiveViewMatrix() const;
            FCT::Mat4 getActiveProjectionMatrix() const;

            // 场景生命周期与主场景持久化操作
            Status newEmptyScene();
            Status openSceneFromFile(const std::string& scenePath);
            Status saveActiveScene();
            Status saveSceneAs(const std::string& targetPath);
            Status setCurrentSceneAsMain();
            const std::string& getActiveScenePath() const noexcept { return m_activeScenePath; }
            const std::string& getMainScenePath() const noexcept { return m_mainScenePath; }

            // PIE 运行会话控制（区分从主场景开始 / 从当前场景开始）
            void startPieSession(PieStartMode mode);
            void stopPieSession();
            bool isPiePlaying() const noexcept;
            PieStartMode getPieStartMode() const noexcept { return m_pieStartMode; }

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;

            std::vector<SceneEntityDesc> m_entities;
            uint32_t m_selectedEntityId = 0;
            uint32_t m_nextEntityId = 1;

            // 编辑器独立自由相机与多源视口相机切换状态（不污染场景内任何 CameraComponent 实体）
            ViewportCameraSource m_cameraSource = ViewportCameraSource::EditorFree;
            std::string m_specificCameraRegistry = "Main";
            FCT::ECS::entity m_specificCameraEcsEntity = FCT::ECS::null;
            bool m_useGameCameraPreview = false;
            bool m_editorCameraOrthographic = false;
            float m_editorCameraOrthoSize = 12.0f;
            float m_cameraPos[3] = {0.0f, 4.5f, -9.5f};
            float m_cameraTarget[3] = {0.0f, 1.0f, 0.0f};
            float m_cameraRot[3] = {18.0f, 0.0f, 0.0f};
            float m_cameraFov = 60.0f;

            std::string m_activeScenePath;
            std::string m_mainScenePath;
            bool m_sceneSignalsRegistered = false;
            bool m_initialStartupResolved = false;
            std::string m_boundProjectDir;
            FCT::ECS::entity m_editorCameraEntity = FCT::ECS::null;
            uint32_t m_lastViewportWidth = 1280;
            uint32_t m_lastViewportHeight = 720;
            int32_t m_lastViewportOffsetX = 0;
            int32_t m_lastViewportOffsetY = 0;
            float m_cameraNear = 0.1f;
            float m_cameraFar = 1000.0f;
            float m_cameraSpeed = 8.0f;
            float m_lastViewportAspect = 16.0f / 9.0f;
            float m_lastDeltaTime = 0.016f;
            int m_gizmoMode = 0; // 0: Translate, 1: Rotate, 2: Scale
            int m_activeGizmoAxis = -1; // -1: None, 0: X, 1: Y, 2: Z
            float m_gizmoDragStartVal[3] = {0.0f, 0.0f, 0.0f};
            ImVec2 m_gizmoDragStartMouse = ImVec2(0.0f, 0.0f);
            bool m_wireframe = false;
            bool m_show3dGrid = true;
            bool m_showBoundingBoxes = true;
            bool m_showLightGizmos = true;
            bool m_enableAntiAliasing = true;
            bool m_draggingLightDir = false;
            uint32_t m_draggingLightEntityId = 0;
            float m_lightDragPlaneDepth = 2.2f;
            float m_lightDragStartDir[3] = {0.0f, -1.0f, 0.0f};
            bool m_wasPiePlaying = false;
            float m_pieTimeAccum = 0.0f;
            PieStartMode m_pieStartMode = PieStartMode::FromMainScene;
            std::string m_prePieScenePath;
            std::string m_prePieBackupFilePath;
            bool m_prePieSwitchedScene = false;
            ViewportCameraSource m_prePieCameraSource = ViewportCameraSource::EditorFree;
            std::string m_prePieSpecificCameraRegistry = "Main";
            FCT::ECS::entity m_prePieSpecificCameraEcsEntity = FCT::ECS::null;
            bool m_prePieUseGameCameraPreview = false;
            uint32_t m_prePieSelectedEntityId = 0;
            bool m_prePieEditorCameraOrthographic = false;
            float m_prePieEditorCameraOrthoSize = 12.0f;
            float m_prePieCameraPos[3] = {0.0f, 4.5f, -9.5f};
            float m_prePieCameraTarget[3] = {0.0f, 1.0f, 0.0f};
            float m_prePieCameraRot[3] = {18.0f, 0.0f, 0.0f};
            std::vector<SceneEntityDesc> m_prePieEntities;
            char m_hierarchyFilter[128] = {0};
            std::string m_selectedRegistryFilter = "All";
            std::string m_activeTargetRegistry = "Main";
            bool m_syncFilterToViewport = true;
            char m_newRegistryNameBuf[64] = {0};

            // 实体子树剪贴板状态与单帧快捷键去重索引
            std::vector<EntityClipboardNode> m_entityClipboard;
            uint32_t m_clipboardSourceParentId = 0;
            std::string m_clipboardSourceRegistry = "Main";
            uint64_t m_lastShortcutFrame = 0;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_SCENE_VIEWER_COMPOSITE_H
