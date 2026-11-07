#include "HomeLbmCubeTool.h"

namespace MQEngine {

namespace {

void computeOrthonormalCameraAxes(
    const FCT::Vec3<>& camForward,
    const FCT::Vec3<>& camUp,
    FCT::Vec3<>& outFwd,
    FCT::Vec3<>& outRight,
    FCT::Vec3<>& outUp) noexcept {
    outFwd = FCT::normalize(camForward);
    outRight = FCT::cross(outFwd, camUp);
    if (outRight.length() < 1e-5f) {
        outRight = FCT::cross(outFwd, FCT::Vec3<>(0.0f, 0.0f, 1.0f));
    }
    outRight = FCT::normalize(outRight);
    outUp = FCT::normalize(FCT::cross(outRight, outFwd));
}

} // namespace

void HomeLbmCubeTool::setCreationModeEnabled(bool enabled, float defaultWorkplaneY) noexcept {
    if (enabled) {
        m_state = State::WaitingBaseStart;
        m_workplaneY = defaultWorkplaneY;
        m_activeFace.reset();
    } else if (isCreationActive()) {
        m_state = State::Idle;
    }
}

void HomeLbmCubeTool::cancelInteraction() noexcept {
    m_state = State::Idle;
    m_activeFace.reset();
}

LbmRay3D HomeLbmCubeTool::buildCameraRay(
    const FCT::Vec2& screenPos,
    const FCT::Vec2& viewportMin,
    const FCT::Vec2& viewportSize,
    const FCT::Vec3<>& camPos,
    const FCT::Vec3<>& camForward,
    const FCT::Vec3<>& camUp,
    float fovDegrees) noexcept {
    const float w = std::max(1.0f, viewportSize.x);
    const float h = std::max(1.0f, viewportSize.y);
    const float u = (screenPos.x - viewportMin.x) / w;
    const float v = (screenPos.y - viewportMin.y) / h;
    const float ndcX = 2.0f * u - 1.0f;
    const float ndcY = 1.0f - 2.0f * v;

    constexpr float kDegToRad = 3.14159265359f / 180.0f;
    const float tanHalfFov = std::tan(std::clamp(fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
    const float aspect = w / h;

    FCT::Vec3<> fwd{}, right{}, trueUp{};
    computeOrthonormalCameraAxes(camForward, camUp, fwd, right, trueUp);

    LbmRay3D ray{};
    ray.origin = camPos;
    ray.direction = FCT::normalize(
        fwd +
        right * (ndcX * aspect * tanHalfFov) +
        trueUp * (ndcY * tanHalfFov));
    return ray;
}

bool HomeLbmCubeTool::projectWorldToScreen(
    const FCT::Vec3<>& worldPos,
    const FCT::Vec2& viewportMin,
    const FCT::Vec2& viewportSize,
    const FCT::Vec3<>& camPos,
    const FCT::Vec3<>& camForward,
    const FCT::Vec3<>& camUp,
    float fovDegrees,
    FCT::Vec2& outScreenPos,
    float* outCameraDepth) noexcept {
    FCT::Vec3<> fwd{}, right{}, trueUp{};
    computeOrthonormalCameraAxes(camForward, camUp, fwd, right, trueUp);

    const FCT::Vec3<> rel = worldPos - camPos;
    const float zCam = FCT::dot(rel, fwd);
    if (zCam <= 0.05f) {
        return false;
    }
    if (outCameraDepth) {
        *outCameraDepth = zCam;
    }

    const float xCam = FCT::dot(rel, right);
    const float yCam = FCT::dot(rel, trueUp);

    constexpr float kDegToRad = 3.14159265359f / 180.0f;
    const float tanHalfFov = std::tan(std::clamp(fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
    const float w = std::max(1.0f, viewportSize.x);
    const float h = std::max(1.0f, viewportSize.y);
    const float aspect = w / h;

    const float ndcX = xCam / (zCam * aspect * tanHalfFov);
    const float ndcY = yCam / (zCam * tanHalfFov);

    outScreenPos = FCT::Vec2(
        viewportMin.x + (ndcX * 0.5f + 0.5f) * w,
        viewportMin.y + (0.5f - ndcY * 0.5f) * h);
    return true;
}

bool HomeLbmCubeTool::intersectHorizontalPlane(
    const LbmRay3D& ray,
    float planeY,
    FCT::Vec3<>& outHit) noexcept {
    if (std::abs(ray.direction.y) < 1e-5f) {
        return false;
    }
    const float t = (planeY - ray.origin.y) / ray.direction.y;
    if (t <= 0.0f || t > 2000.0f) {
        return false;
    }
    outHit = ray.origin + ray.direction * t;
    return true;
}

float HomeLbmCubeTool::closestAxisParameterToRay(
    const FCT::Vec3<>& axisOrigin,
    const FCT::Vec3<>& axisUnitDir,
    const LbmRay3D& ray) noexcept {
    const FCT::Vec3<> a = FCT::normalize(axisUnitDir);
    const FCT::Vec3<> d = FCT::normalize(ray.direction);
    const FCT::Vec3<> w0 = axisOrigin - ray.origin;

    const float b = FCT::dot(a, d);
    const float denom = 1.0f - b * b;
    if (denom < 1e-5f) {
        return 0.0f;
    }
    return (b * FCT::dot(d, w0) - FCT::dot(a, w0)) / denom;
}

void HomeLbmCubeTool::rebuildCreationPreview() noexcept {
    float minX = std::min(m_baseAnchorA.x, m_baseCornerB.x);
    float maxX = std::max(m_baseAnchorA.x, m_baseCornerB.x);
    float minZ = std::min(m_baseAnchorA.z, m_baseCornerB.z);
    float maxZ = std::max(m_baseAnchorA.z, m_baseCornerB.z);

    if (maxX - minX < m_minFaceSeparation) {
        maxX = minX + m_minFaceSeparation;
    }
    if (maxZ - minZ < m_minFaceSeparation) {
        maxZ = minZ + m_minFaceSeparation;
    }

    float minY = m_workplaneY;
    float maxY = m_workplaneY + m_minFaceSeparation;
    if (m_state == State::ExtrudingHeight) {
        if (m_extrudeHeight >= 0.0f) {
            minY = m_workplaneY;
            maxY = m_workplaneY + std::max(m_minFaceSeparation, m_extrudeHeight);
        } else {
            minY = m_workplaneY + std::min(-m_minFaceSeparation, m_extrudeHeight);
            maxY = m_workplaneY;
        }
    }

    m_previewMin = FCT::Vec3<>(minX, minY, minZ);
    m_previewMax = FCT::Vec3<>(maxX, maxY, maxZ);
}

bool HomeLbmCubeTool::beginBaseDrag(
    const LbmRay3D& ray,
    const FCT::Vec3<>* optionalSurfaceHit) noexcept {
    if (optionalSurfaceHit) {
        m_workplaneY = optionalSurfaceHit->y;
        m_baseAnchorA = *optionalSurfaceHit;
    } else {
        FCT::Vec3<> hit{};
        if (!intersectHorizontalPlane(ray, m_workplaneY, hit)) {
            return false;
        }
        m_baseAnchorA = hit;
    }

    m_baseCornerB = m_baseAnchorA + FCT::Vec3<>(m_minFaceSeparation, 0.0f, m_minFaceSeparation);
    m_extrudeHeight = m_minFaceSeparation;
    m_state = State::DrawingBaseRect;
    rebuildCreationPreview();
    return true;
}

void HomeLbmCubeTool::updateCreationPointer(const LbmRay3D& ray) noexcept {
    if (m_state == State::DrawingBaseRect) {
        FCT::Vec3<> hit{};
        if (intersectHorizontalPlane(ray, m_workplaneY, hit)) {
            m_baseCornerB = hit;
            rebuildCreationPreview();
        }
    } else if (m_state == State::ExtrudingHeight) {
        m_extrudeHeight = closestAxisParameterToRay(
            FCT::Vec3<>(m_baseCornerB.x, m_workplaneY, m_baseCornerB.z),
            FCT::Vec3<>(0.0f, 1.0f, 0.0f),
            ray);
        rebuildCreationPreview();
    }
}

bool HomeLbmCubeTool::finishBaseDragAndBeginExtrude(const LbmRay3D& ray) noexcept {
    if (m_state != State::DrawingBaseRect) {
        return false;
    }
    updateCreationPointer(ray);
    m_extrudeHeight = std::max(m_minFaceSeparation, 1.0f);
    m_state = State::ExtrudingHeight;
    rebuildCreationPreview();
    return true;
}

bool HomeLbmCubeTool::commitExtrusion(
    const LbmRay3D& ray,
    FCT::Vec3<>& outMinBounds,
    FCT::Vec3<>& outMaxBounds) noexcept {
    if (m_state != State::ExtrudingHeight) {
        return false;
    }
    updateCreationPointer(ray);
    outMinBounds = m_previewMin;
    outMaxBounds = m_previewMax;
    m_state = State::Idle;
    return true;
}

FCT::Vec3<> HomeLbmCubeTool::faceCenter(
    const FCT::Vec3<>& minBounds,
    const FCT::Vec3<>& maxBounds,
    LbmDomainFace face) noexcept {
    const FCT::Vec3<> mid = (minBounds + maxBounds) * 0.5f;
    switch (face) {
        case LbmDomainFace::NegX: return FCT::Vec3<>(minBounds.x, mid.y, mid.z);
        case LbmDomainFace::PosX: return FCT::Vec3<>(maxBounds.x, mid.y, mid.z);
        case LbmDomainFace::NegY: return FCT::Vec3<>(mid.x, minBounds.y, mid.z);
        case LbmDomainFace::PosY: return FCT::Vec3<>(mid.x, maxBounds.y, mid.z);
        case LbmDomainFace::NegZ: return FCT::Vec3<>(mid.x, mid.y, minBounds.z);
        case LbmDomainFace::PosZ: return FCT::Vec3<>(mid.x, mid.y, maxBounds.z);
    }
    return mid;
}

FCT::Vec3<> HomeLbmCubeTool::faceNormal(LbmDomainFace face) noexcept {
    switch (face) {
        case LbmDomainFace::NegX: return FCT::Vec3<>(-1.0f,  0.0f,  0.0f);
        case LbmDomainFace::PosX: return FCT::Vec3<>( 1.0f,  0.0f,  0.0f);
        case LbmDomainFace::NegY: return FCT::Vec3<>( 0.0f, -1.0f,  0.0f);
        case LbmDomainFace::PosY: return FCT::Vec3<>( 0.0f,  1.0f,  0.0f);
        case LbmDomainFace::NegZ: return FCT::Vec3<>( 0.0f,  0.0f, -1.0f);
        case LbmDomainFace::PosZ: return FCT::Vec3<>( 0.0f,  0.0f,  1.0f);
    }
    return FCT::Vec3<>(0.0f, 1.0f, 0.0f);
}

void HomeLbmCubeTool::faceQuadCorners(
    const FCT::Vec3<>& minB,
    const FCT::Vec3<>& maxB,
    LbmDomainFace face,
    std::array<FCT::Vec3<>, 4>& outCorners) noexcept {
    switch (face) {
        case LbmDomainFace::NegX:
            outCorners = { FCT::Vec3<>(minB.x, minB.y, minB.z), FCT::Vec3<>(minB.x, minB.y, maxB.z),
                           FCT::Vec3<>(minB.x, maxB.y, maxB.z), FCT::Vec3<>(minB.x, maxB.y, minB.z) };
            break;
        case LbmDomainFace::PosX:
            outCorners = { FCT::Vec3<>(maxB.x, minB.y, minB.z), FCT::Vec3<>(maxB.x, minB.y, maxB.z),
                           FCT::Vec3<>(maxB.x, maxB.y, maxB.z), FCT::Vec3<>(maxB.x, maxB.y, minB.z) };
            break;
        case LbmDomainFace::NegY:
            outCorners = { FCT::Vec3<>(minB.x, minB.y, minB.z), FCT::Vec3<>(maxB.x, minB.y, minB.z),
                           FCT::Vec3<>(maxB.x, minB.y, maxB.z), FCT::Vec3<>(minB.x, minB.y, maxB.z) };
            break;
        case LbmDomainFace::PosY:
            outCorners = { FCT::Vec3<>(minB.x, maxB.y, minB.z), FCT::Vec3<>(maxB.x, maxB.y, minB.z),
                           FCT::Vec3<>(maxB.x, maxB.y, maxB.z), FCT::Vec3<>(minB.x, maxB.y, maxB.z) };
            break;
        case LbmDomainFace::NegZ:
            outCorners = { FCT::Vec3<>(minB.x, minB.y, minB.z), FCT::Vec3<>(maxB.x, minB.y, minB.z),
                           FCT::Vec3<>(maxB.x, maxB.y, minB.z), FCT::Vec3<>(minB.x, maxB.y, minB.z) };
            break;
        case LbmDomainFace::PosZ:
            outCorners = { FCT::Vec3<>(minB.x, minB.y, maxB.z), FCT::Vec3<>(maxB.x, minB.y, maxB.z),
                           FCT::Vec3<>(maxB.x, maxB.y, maxB.z), FCT::Vec3<>(minB.x, maxB.y, maxB.z) };
            break;
    }
}

std::optional<LbmDomainFace> HomeLbmCubeTool::pickFaceHandle(
    const FCT::Vec2& mouseScreenPos,
    float pickRadiusPixels,
    const FCT::Vec3<>& minBounds,
    const FCT::Vec3<>& maxBounds,
    const FCT::Vec2& viewportMin,
    const FCT::Vec2& viewportSize,
    const FCT::Vec3<>& camPos,
    const FCT::Vec3<>& camForward,
    const FCT::Vec3<>& camUp,
    float fovDegrees) const noexcept {
    constexpr std::array<LbmDomainFace, 6> kAllFaces = {
        LbmDomainFace::NegX, LbmDomainFace::PosX,
        LbmDomainFace::NegY, LbmDomainFace::PosY,
        LbmDomainFace::NegZ, LbmDomainFace::PosZ
    };

    std::optional<LbmDomainFace> bestFace;
    float bestScore = pickRadiusPixels * pickRadiusPixels;
    float bestDepth = 1e9f;

    for (LbmDomainFace face : kAllFaces) {
        const FCT::Vec3<> center = faceCenter(minBounds, maxBounds, face);
        FCT::Vec2 screenPt{};
        float depth = 0.0f;
        if (!projectWorldToScreen(
                center, viewportMin, viewportSize, camPos, camForward, camUp, fovDegrees, screenPt, &depth)) {
            continue;
        }
        const float dx = screenPt.x - mouseScreenPos.x;
        const float dy = screenPt.y - mouseScreenPos.y;
        const float distSq = dx * dx + dy * dy;
        if (distSq <= bestScore) {
            if (!bestFace.has_value() || distSq < bestScore * 0.85f || depth < bestDepth) {
                bestScore = distSq;
                bestDepth = depth;
                bestFace = face;
            }
        }
    }
    return bestFace;
}

void HomeLbmCubeTool::beginFaceDrag(
    LbmDomainFace face,
    const LbmRay3D& initialRay,
    const FCT::Vec3<>& currentMinBounds,
    const FCT::Vec3<>& currentMaxBounds) noexcept {
    m_state = State::DraggingFaceHandle;
    m_activeFace = face;
    m_dragStartMin = currentMinBounds;
    m_dragStartMax = currentMaxBounds;
    m_dragAxisOrigin = faceCenter(currentMinBounds, currentMaxBounds, face);
    m_dragAxisNormal = faceNormal(face);
    m_dragInitialParam = closestAxisParameterToRay(m_dragAxisOrigin, m_dragAxisNormal, initialRay);
}

bool HomeLbmCubeTool::updateFaceDrag(
    const LbmRay3D& currentRay,
    FCT::Vec3<>& outMinBounds,
    FCT::Vec3<>& outMaxBounds) noexcept {
    if (m_state != State::DraggingFaceHandle || !m_activeFace.has_value()) {
        return false;
    }

    const float curParam = closestAxisParameterToRay(m_dragAxisOrigin, m_dragAxisNormal, currentRay);
    const float deltaAlongNormal = curParam - m_dragInitialParam;

    outMinBounds = m_dragStartMin;
    outMaxBounds = m_dragStartMax;

    switch (*m_activeFace) {
        case LbmDomainFace::NegX:
            outMinBounds.x = std::min(m_dragStartMax.x - m_minFaceSeparation, m_dragStartMin.x - deltaAlongNormal);
            break;
        case LbmDomainFace::PosX:
            outMaxBounds.x = std::max(m_dragStartMin.x + m_minFaceSeparation, m_dragStartMax.x + deltaAlongNormal);
            break;
        case LbmDomainFace::NegY:
            outMinBounds.y = std::min(m_dragStartMax.y - m_minFaceSeparation, m_dragStartMin.y - deltaAlongNormal);
            break;
        case LbmDomainFace::PosY:
            outMaxBounds.y = std::max(m_dragStartMin.y + m_minFaceSeparation, m_dragStartMax.y + deltaAlongNormal);
            break;
        case LbmDomainFace::NegZ:
            outMinBounds.z = std::min(m_dragStartMax.z - m_minFaceSeparation, m_dragStartMin.z - deltaAlongNormal);
            break;
        case LbmDomainFace::PosZ:
            outMaxBounds.z = std::max(m_dragStartMin.z + m_minFaceSeparation, m_dragStartMax.z + deltaAlongNormal);
            break;
    }
    return true;
}

void HomeLbmCubeTool::endFaceDrag() noexcept {
    if (m_state == State::DraggingFaceHandle) {
        m_state = State::Idle;
    }
    m_activeFace.reset();
}

} // namespace MQEngine
