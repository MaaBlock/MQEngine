#ifndef MQENGINE_HOMELBM_CUBE_TOOL_H
#define MQENGINE_HOMELBM_CUBE_TOOL_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"

namespace MQEngine {

/**
 * @brief 三维空间拾取射线
 */
struct LbmRay3D {
    FCT::Vec3<> origin = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> direction = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
};

/**
 * @brief 类 UE5 两段式拉框建盒 + 六面独立推拉手柄几何状态机
 */
class HOME_LBM_API HomeLbmCubeTool {
public:
    enum class State : int32_t {
        Idle = 0,
        WaitingBaseStart = 1,
        DrawingBaseRect = 2,
        ExtrudingHeight = 3,
        DraggingFaceHandle = 4
    };

    HomeLbmCubeTool() = default;
    ~HomeLbmCubeTool() = default;

    void setCreationModeEnabled(bool enabled, float defaultWorkplaneY = 0.0f) noexcept;
    void cancelInteraction() noexcept;

    State state() const noexcept { return m_state; }
    bool isCreationActive() const noexcept {
        return m_state == State::WaitingBaseStart ||
               m_state == State::DrawingBaseRect ||
               m_state == State::ExtrudingHeight;
    }
    bool isDraggingFace() const noexcept { return m_state == State::DraggingFaceHandle; }
    std::optional<LbmDomainFace> activeFace() const noexcept { return m_activeFace; }

    float minFaceSeparation() const noexcept { return m_minFaceSeparation; }
    void setMinFaceSeparation(float minSep) noexcept { m_minFaceSeparation = std::max(0.1f, minSep); }

    // --- 阶段 1 & 2: UE5 两段式拉框建盒 (底面拖拽 -> 垂直拉伸定高) ---
    bool beginBaseDrag(const LbmRay3D& ray, const FCT::Vec3<>* optionalSurfaceHit = nullptr) noexcept;
    void updateCreationPointer(const LbmRay3D& ray) noexcept;
    bool finishBaseDragAndBeginExtrude(const LbmRay3D& ray) noexcept;
    bool commitExtrusion(
        const LbmRay3D& ray,
        FCT::Vec3<>& outMinBounds,
        FCT::Vec3<>& outMaxBounds) noexcept;

    void currentPreviewBounds(FCT::Vec3<>& outMin, FCT::Vec3<>& outMax) const noexcept {
        outMin = m_previewMin;
        outMax = m_previewMax;
    }

    // --- 阶段 3: 六面中心推拉手柄交互 ---
    static FCT::Vec3<> faceCenter(
        const FCT::Vec3<>& minBounds,
        const FCT::Vec3<>& maxBounds,
        LbmDomainFace face) noexcept;

    static FCT::Vec3<> faceNormal(LbmDomainFace face) noexcept;

    static void faceQuadCorners(
        const FCT::Vec3<>& minBounds,
        const FCT::Vec3<>& maxBounds,
        LbmDomainFace face,
        std::array<FCT::Vec3<>, 4>& outCorners) noexcept;

    std::optional<LbmDomainFace> pickFaceHandle(
        const FCT::Vec2& mouseScreenPos,
        float pickRadiusPixels,
        const FCT::Vec3<>& minBounds,
        const FCT::Vec3<>& maxBounds,
        const FCT::Vec2& viewportMin,
        const FCT::Vec2& viewportSize,
        const FCT::Vec3<>& camPos,
        const FCT::Vec3<>& camForward,
        const FCT::Vec3<>& camUp,
        float fovDegrees) const noexcept;

    void beginFaceDrag(
        LbmDomainFace face,
        const LbmRay3D& initialRay,
        const FCT::Vec3<>& currentMinBounds,
        const FCT::Vec3<>& currentMaxBounds) noexcept;

    bool updateFaceDrag(
        const LbmRay3D& currentRay,
        FCT::Vec3<>& outMinBounds,
        FCT::Vec3<>& outMaxBounds) noexcept;

    void endFaceDrag() noexcept;

    // --- 通用三维相机投影与射线几何求解工具 ---
    static LbmRay3D buildCameraRay(
        const FCT::Vec2& screenPos,
        const FCT::Vec2& viewportMin,
        const FCT::Vec2& viewportSize,
        const FCT::Vec3<>& camPos,
        const FCT::Vec3<>& camForward,
        const FCT::Vec3<>& camUp,
        float fovDegrees) noexcept;

    static bool projectWorldToScreen(
        const FCT::Vec3<>& worldPos,
        const FCT::Vec2& viewportMin,
        const FCT::Vec2& viewportSize,
        const FCT::Vec3<>& camPos,
        const FCT::Vec3<>& camForward,
        const FCT::Vec3<>& camUp,
        float fovDegrees,
        FCT::Vec2& outScreenPos,
        float* outCameraDepth = nullptr) noexcept;

    static bool intersectHorizontalPlane(
        const LbmRay3D& ray,
        float planeY,
        FCT::Vec3<>& outHit) noexcept;

    static float closestAxisParameterToRay(
        const FCT::Vec3<>& axisOrigin,
        const FCT::Vec3<>& axisUnitDir,
        const LbmRay3D& ray) noexcept;

private:
    void rebuildCreationPreview() noexcept;

private:
    State m_state = State::Idle;
    float m_workplaneY = 0.0f;
    float m_minFaceSeparation = 0.25f;

    FCT::Vec3<> m_baseAnchorA = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> m_baseCornerB = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    float m_extrudeHeight = 1.0f;
    FCT::Vec3<> m_previewMin = FCT::Vec3<>(-1.0f, 0.0f, -1.0f);
    FCT::Vec3<> m_previewMax = FCT::Vec3<>(1.0f, 1.0f, 1.0f);

    std::optional<LbmDomainFace> m_activeFace;
    FCT::Vec3<> m_dragStartMin = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> m_dragStartMax = FCT::Vec3<>(1.0f, 1.0f, 1.0f);
    FCT::Vec3<> m_dragAxisOrigin = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    FCT::Vec3<> m_dragAxisNormal = FCT::Vec3<>(1.0f, 0.0f, 0.0f);
    float m_dragInitialParam = 0.0f;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_CUBE_TOOL_H
