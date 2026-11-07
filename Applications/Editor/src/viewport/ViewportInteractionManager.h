#ifndef MQENGINE_EDITOR_VIEWPORT_INTERACTION_MANAGER_H
#define MQENGINE_EDITOR_VIEWPORT_INTERACTION_MANAGER_H

#include "../thirdparty/thirdparty.h"
#include "../UI/UiContext.h"
#include "../UI/UiSignals.h"

namespace MQEngine {

    class EditorCameraManager;
    class PhysicsSystem;
    class UndoManager;

    /**
     * @brief 3D 平移 Gizmo 坐标轴手柄枚举
     */
    enum class GizmoAxis : uint8_t {
        None = 0,
        X,
        Y,
        Z,
        Center
    };

    /**
     * @brief 视口四级输入优先级仲裁层级
     */
    enum class ViewportInteractionTier : uint8_t {
        None = 0,
        CameraNavigation,   // 优先级 1: 相机导航 (RMB/MMB/Alt+LMB)
        HomeLbmTool,        // 优先级 2: HomeLBM 视口建盒与面拖拽工具
        GizmoTransform,     // 优先级 3: 3D 平移 Gizmo 坐标轴拖拽
        EntityPicking       // 优先级 4: 视口物体射线拾取
    };

    /**
     * @brief 三维空间射线
     */
    struct ViewportRay3D {
        FCT::Vec3<> origin{ 0.0f, 0.0f, 0.0f };
        FCT::Vec3<> direction{ 0.0f, 0.0f, -1.0f };
    };

    /**
     * @brief 视口相机几何计算参数包
     */
    struct ViewportCameraData {
        FCT::Vec3<> position{ 0.0f, 0.0f, 0.0f };
        FCT::Vec3<> forward{ 0.0f, 0.0f, -1.0f };
        FCT::Vec3<> up{ 0.0f, 1.0f, 0.0f };
        float fovDegrees = 60.0f;
    };

    /**
     * @brief 实体拾取结果数据结构
     */
    struct ViewportPickResult {
        entt::entity entity = entt::null;
        std::string trunkName;
        bool isGlobal = false;
        float distance = 0.0f;
        FCT::Vec3<> hitPoint{ 0.0f, 0.0f, 0.0f };
        bool hasHit = false;
    };

    using Ray3D = ViewportRay3D;
    using PickResult = ViewportPickResult;
    using CameraRayParameters = ViewportCameraData;

    /**
     * @brief 编辑器视口交互与 3D 平移 Gizmo 管理器
     * 
     * 核心职责：
     * 1. 视口屏幕坐标到世界空间射线计算 (Screen-to-World) 与世界坐标到屏幕像素坐标投影 (World-to-Screen)；
     * 2. 两级实体拾取（Tier 1 PhysX 快速通道 + Tier 2 ECS AABB 包围盒扫描保底）；
     * 3. 3D 平移 Gizmo 原生 ImDrawList 绘制（X/Y/Z 正交轴箭头与中心视平面手柄）；
     * 4. 空间射线与坐标轴/视平面交互约束解算、60fps 实时 Transform 同步与 Inspector 即时刷新；
     * 5. 鼠标释放时位移阈值判定与单个 TranslateEntityCommand 生成并压入 UndoManager 撤销栈；
     * 6. 四级输入冲突仲裁状态机（Camera > HomeLBM > Gizmo > Picking）。
     */
    class ViewportInteractionManager {
    public:
        ViewportInteractionManager() = default;
        ~ViewportInteractionManager() = default;

        // --- 静态纯几何与数学工具 ---
        static ViewportRay3D buildCameraRay(
            const FCT::Vec2& screenPos,
            const FCT::Vec2& vpMin,
            const FCT::Vec2& vpSize,
            const FCT::Vec3<>& camPos,
            const FCT::Vec3<>& camForward,
            const FCT::Vec3<>& camUp,
            float fovDegrees) noexcept;

        static ViewportRay3D buildCameraRay(
            const FCT::Vec2& screenPos,
            const FCT::Vec2& vpMin,
            const FCT::Vec2& vpSize,
            const ViewportCameraData& camParams) noexcept {
            return buildCameraRay(screenPos, vpMin, vpSize, camParams.position, camParams.forward, camParams.up, camParams.fovDegrees);
        }

        static bool projectWorldToScreen(
            const FCT::Vec3<>& worldPos,
            const FCT::Vec2& vpMin,
            const FCT::Vec2& vpSize,
            const FCT::Vec3<>& camPos,
            const FCT::Vec3<>& camForward,
            const FCT::Vec3<>& camUp,
            float fovDegrees,
            FCT::Vec2& outScreenPos,
            float* outCameraDepth = nullptr) noexcept;

        static bool projectWorldToScreen(
            const FCT::Vec3<>& worldPos,
            const FCT::Vec2& vpMin,
            const FCT::Vec2& vpSize,
            const ViewportCameraData& camParams,
            FCT::Vec2& outScreenPos,
            float* outCameraDepth = nullptr) noexcept {
            return projectWorldToScreen(worldPos, vpMin, vpSize, camParams.position, camParams.forward, camParams.up, camParams.fovDegrees, outScreenPos, outCameraDepth);
        }

        static float closestAxisParameterToRay(
            const FCT::Vec3<>& axisOrigin,
            const FCT::Vec3<>& axisUnitDir,
            const ViewportRay3D& ray) noexcept;

        static bool intersectRayPlane(
            const ViewportRay3D& ray,
            const FCT::Vec3<>& planePoint,
            const FCT::Vec3<>& planeNormal,
            FCT::Vec3<>& outHitPoint) noexcept;

        static bool intersectPlane(
            const ViewportRay3D& ray,
            const FCT::Vec3<>& planePoint,
            const FCT::Vec3<>& planeNormal,
            FCT::Vec3<>& outHitPoint) noexcept {
            return intersectRayPlane(ray, planePoint, planeNormal, outHitPoint);
        }

        static bool intersectRayAabb(
            const FCT::Vec3<>& rayOrigin,
            const FCT::Vec3<>& rayDir,
            const FCT::Vec3<>& boxMin,
            const FCT::Vec3<>& boxMax,
            float& outTNear,
            float maxDistance = 10000.0f) noexcept;

        static bool intersectRayAabb(
            const ViewportRay3D& ray,
            const FCT::Vec3<>& boxMin,
            const FCT::Vec3<>& boxMax,
            float& outTNear) noexcept {
            return intersectRayAabb(ray.origin, ray.direction, boxMin, boxMax, outTNear);
        }

        static float pointToSegmentDistance2D(
            const FCT::Vec2& p,
            const FCT::Vec2& a,
            const FCT::Vec2& b) noexcept;

        // --- 四级输入优先级仲裁 ---
        static ViewportInteractionTier resolveInteractionTier(
            bool isCameraNavigating,
            bool isHomeLbmActive,
            bool isGizmoInteracting,
            bool isLmbClickedInViewport) noexcept;

        // --- 相机参数与两级拾取 ---
        static bool resolveActiveCameraParameters(
            EditorCameraManager* cameraMgr,
            ViewportCameraData& outParams);

        static bool pickEntityAtRay(
            const ViewportRay3D& ray,
            ViewportPickResult& outResult,
            Scene::Scene* scene = nullptr,
            PhysicsSystem* physicsSystem = nullptr);

        static entt::entity pickEntityInRegistry(
            const ViewportRay3D& ray,
            entt::registry* registry,
            PhysicsSystem* physicsSystem = nullptr,
            float* outDist = nullptr);

        static bool isHomeLbmToolActive();

        // --- 视口交互与渲染入口 ---
        void updateAndRender(
            UiContext& ctx,
            const ViewportViewState& viewState,
            const FCT::Vec2& vpMin,
            const FCT::Vec2& vpSize,
            bool isViewportHovered,
            bool isWindowFocused);

        void cancelDrag() noexcept;

        bool isDragging() const noexcept { return m_isDragging; }
        GizmoAxis activeAxis() const noexcept { return m_activeAxis; }
        GizmoAxis hoveredAxis() const noexcept { return m_hoveredAxis; }

    private:
        GizmoAxis pickGizmoHandle(
            const FCT::Vec2& mousePos,
            const FCT::Vec2& s0,
            const FCT::Vec2& sx,
            const FCT::Vec2& sy,
            const FCT::Vec2& sz) const noexcept;

        void drawArrowhead(
            ImDrawList* drawList,
            const FCT::Vec2& startScreen,
            const FCT::Vec2& endScreen,
            ImU32 color,
            float length = 12.0f,
            float halfWidth = 5.0f) const noexcept;

        void renderGizmoVisuals(
            ImDrawList* drawList,
            const FCT::Vec2& s0,
            const FCT::Vec2& sx,
            const FCT::Vec2& sy,
            const FCT::Vec2& sz,
            bool visX,
            bool visY,
            bool visZ,
            GizmoAxis highlightAxis) const;

    private:
        bool m_isDragging = false;
        GizmoAxis m_activeAxis = GizmoAxis::None;
        GizmoAxis m_hoveredAxis = GizmoAxis::None;

        FCT::Vec3<> m_dragStartLocalPos{ 0.0f, 0.0f, 0.0f };
        FCT::Vec3<> m_dragStartWorldPos{ 0.0f, 0.0f, 0.0f };
        float m_initialAxisParam = 0.0f;
        FCT::Vec3<> m_initialPlaneHit{ 0.0f, 0.0f, 0.0f };
    };

} // namespace MQEngine

#endif // MQENGINE_EDITOR_VIEWPORT_INTERACTION_MANAGER_H
