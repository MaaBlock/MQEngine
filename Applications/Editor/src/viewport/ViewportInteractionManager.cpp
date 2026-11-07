#include "ViewportInteractionManager.h"
#include "../core/Global.h"
#include "../imgui/EditorCameraManager.h"
#include "../command/TranslateEntityCommand.h"
#include "../command/UndoManager.h"
#include <Engine/headers.h>
#include <Engine/render/system/CameraSystem.h>
#include <Engine/physics/system/PhysicsSystem.h>
#include <Engine/render/system/MatrixCacheSystem.h>
#include <Engine/runtime/manager/SystemManager.h>
#include <Engine/scene/manager/DataManager.h>
#include <Engine/scene/data/Component.h>
#include <Engine/render/data/Camera.h>
#include <Engine/scene/core/Scene.h>
#include <Engine/physics/manager/PhysicsScene.h>
#include <Engine/physics/data/PhysicsCommon.h>
#include <HomeLBM/HomeLbmSystem.h>
#include <HomeLBM/HomeLbmCubeTool.h>

#include <cmath>
#include <algorithm>
#include <limits>

namespace MQEngine {

    namespace {
        constexpr float kDegToRad = 3.14159265359f / 180.0f;

        void computeOrthonormalAxes(
            const FCT::Vec3<>& fwdIn,
            const FCT::Vec3<>& upIn,
            FCT::Vec3<>& outFwd,
            FCT::Vec3<>& outRight,
            FCT::Vec3<>& outTrueUp) noexcept {
            outFwd = FCT::normalize(fwdIn);
            outRight = FCT::cross(outFwd, upIn);
            if (outRight.length() < 1e-5f) {
                outRight = FCT::cross(outFwd, FCT::Vec3<>(0.0f, 0.0f, 1.0f));
            }
            outRight = FCT::normalize(outRight);
            outTrueUp = FCT::normalize(FCT::cross(outRight, outFwd));
        }

        FCT::Vec3<> queryEntityWorldPosition(entt::registry* reg, entt::entity entity) {
            if (!reg || entity == entt::null || !reg->valid(entity)) {
                return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
            }
            if (Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
                return Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldPosition(reg, entity);
            }
            FCT::Vec3<> worldPos(0.0f, 0.0f, 0.0f);
            entt::entity cur = entity;
            int depth = 0;
            while (cur != entt::null && reg->valid(cur) && depth < 32) {
                if (const auto* pos = reg->try_get<PositionComponent>(cur)) {
                    worldPos = worldPos + pos->position;
                }
                if (const auto* h = reg->try_get<HierarchyComponent>(cur)) {
                    entt::entity next = static_cast<entt::entity>(h->parentEntity);
                    if (next == cur) {
                        break;
                    }
                    cur = next;
                } else {
                    break;
                }
                ++depth;
            }
            return worldPos;
        }

        FCT::Vec3<> worldToEntityLocalPosition(entt::registry* reg, entt::entity entity, const FCT::Vec3<>& worldPos) {
            if (!reg || entity == entt::null || !reg->valid(entity)) {
                return worldPos;
            }
            const auto* h = reg->try_get<HierarchyComponent>(entity);
            if (!h) {
                return worldPos;
            }
            entt::entity parent = static_cast<entt::entity>(h->parentEntity);
            if (parent == entt::null || parent == entity || !reg->valid(parent)) {
                return worldPos;
            }
            if (Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
                FCT::Mat4 parentWorld = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldMatrix(reg, parent);
                FCT::Vec4 local4 = parentWorld.inverse() * FCT::Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
                return FCT::Vec3<>(local4.x, local4.y, local4.z);
            }
            return worldPos - queryEntityWorldPosition(reg, parent);
        }

        void refreshEntityMatrixCache(entt::registry* reg, entt::entity entity) {
            if (!reg || entity == entt::null || !reg->valid(entity)) {
                return;
            }
            if (Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
                if (auto* cacheModel = reg->try_get<CacheModelMatrix>(entity)) {
                    const FCT::Mat4 worldMat = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldMatrix(reg, entity);
                    const FCT::Mat4 localMat = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityLocalMatrix(reg, entity);
                    cacheModel->localMatrix = localMat;
                    cacheModel->worldMatrix = worldMat;
                    if (cacheModel->uniform) {
                        cacheModel->uniform->setValue(FCT::UniformType::ModelMatrix, worldMat);
                        cacheModel->uniform->setValue("modelInverseTransposeMatrix", worldMat.inverse().transpose());
                    }
                    cacheModel->init = true;
                }
            }
            if (auto* physCache = reg->try_get<CachePhysicsActor>(entity)) {
                if (physCache->actor) {
                    const FCT::Vec3<> worldPos = queryEntityWorldPosition(reg, entity);
                    physx::PxTransform pose = physCache->actor->getGlobalPose();
                    pose.p = physx::PxVec3(worldPos.x, worldPos.y, worldPos.z);
                    physCache->actor->setGlobalPose(pose);
                }
            }
        }

        entt::registry* resolveSelectedEntityRegistry(const SelectedEntityState& selState) {
            if (selState.entity == entt::null) {
                return nullptr;
            }
            if (selState.scene) {
                if (selState.isGlobal) {
                    return selState.scene->getRegistry();
                }
                if (selState.scene->isLoad(selState.trunkName)) {
                    SceneTrunk* trunk = selState.scene->getLoadedTrunk(selState.trunkName);
                    return trunk ? trunk->getRegistry() : nullptr;
                }
            }
            if (g_editorGlobal.dataManager) {
                Scene::Scene* scene = g_editorGlobal.dataManager->getCurrentScene();
                if (scene) {
                    if (selState.isGlobal) {
                        return scene->getRegistry();
                    }
                    if (scene->isLoad(selState.trunkName)) {
                        SceneTrunk* trunk = scene->getLoadedTrunk(selState.trunkName);
                        return trunk ? trunk->getRegistry() : nullptr;
                    }
                }
            }
            return nullptr;
        }
    } // namespace

    ViewportRay3D ViewportInteractionManager::buildCameraRay(
        const FCT::Vec2& screenPos,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        const FCT::Vec3<>& camPos,
        const FCT::Vec3<>& camForward,
        const FCT::Vec3<>& camUp,
        float fovDegrees) noexcept {
        const float w = std::max(1.0f, vpSize.x);
        const float h = std::max(1.0f, vpSize.y);
        const float u = (screenPos.x - vpMin.x) / w;
        const float v = (screenPos.y - vpMin.y) / h;
        const float ndcX = 2.0f * u - 1.0f;
        const float ndcY = 1.0f - 2.0f * v;

        const float tanHalfFov = std::tan(std::clamp(fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
        const float aspect = w / h;

        FCT::Vec3<> fwd{}, right{}, trueUp{};
        computeOrthonormalAxes(camForward, camUp, fwd, right, trueUp);

        ViewportRay3D ray{};
        ray.origin = camPos;
        ray.direction = FCT::normalize(
            fwd +
            right * (ndcX * aspect * tanHalfFov) +
            trueUp * (ndcY * tanHalfFov));
        return ray;
    }

    bool ViewportInteractionManager::projectWorldToScreen(
        const FCT::Vec3<>& worldPos,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        const FCT::Vec3<>& camPos,
        const FCT::Vec3<>& camForward,
        const FCT::Vec3<>& camUp,
        float fovDegrees,
        FCT::Vec2& outScreenPos,
        float* outCameraDepth) noexcept {
        FCT::Vec3<> fwd{}, right{}, trueUp{};
        computeOrthonormalAxes(camForward, camUp, fwd, right, trueUp);

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

        const float tanHalfFov = std::tan(std::clamp(fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
        const float w = std::max(1.0f, vpSize.x);
        const float h = std::max(1.0f, vpSize.y);
        const float aspect = w / h;

        const float ndcX = xCam / (zCam * aspect * tanHalfFov);
        const float ndcY = yCam / (zCam * tanHalfFov);

        outScreenPos = FCT::Vec2(
            vpMin.x + (ndcX * 0.5f + 0.5f) * w,
            vpMin.y + (0.5f - ndcY * 0.5f) * h);
        return true;
    }

    float ViewportInteractionManager::closestAxisParameterToRay(
        const FCT::Vec3<>& axisOrigin,
        const FCT::Vec3<>& axisUnitDir,
        const ViewportRay3D& ray) noexcept {
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

    bool ViewportInteractionManager::intersectRayPlane(
        const ViewportRay3D& ray,
        const FCT::Vec3<>& planePoint,
        const FCT::Vec3<>& planeNormal,
        FCT::Vec3<>& outHitPoint) noexcept {
        const FCT::Vec3<> n = FCT::normalize(planeNormal);
        const float denom = FCT::dot(n, ray.direction);
        if (std::abs(denom) < 1e-5f) {
            return false;
        }
        const float t = FCT::dot(planePoint - ray.origin, n) / denom;
        if (t < 0.0f) {
            return false;
        }
        outHitPoint = ray.origin + ray.direction * t;
        return true;
    }

    bool ViewportInteractionManager::intersectRayAabb(
        const FCT::Vec3<>& rayOrigin,
        const FCT::Vec3<>& rayDir,
        const FCT::Vec3<>& boxMin,
        const FCT::Vec3<>& boxMax,
        float& outTNear,
        float maxDistance) noexcept {
        float tMin = 0.0f;
        float tMax = maxDistance;

        const float orig[3] = { rayOrigin.x, rayOrigin.y, rayOrigin.z };
        const float dir[3] = { rayDir.x, rayDir.y, rayDir.z };
        const float bMin[3] = { boxMin.x, boxMin.y, boxMin.z };
        const float bMax[3] = { boxMax.x, boxMax.y, boxMax.z };

        for (int i = 0; i < 3; ++i) {
            if (std::abs(dir[i]) < 1e-6f) {
                if (orig[i] < bMin[i] || orig[i] > bMax[i]) {
                    return false;
                }
            } else {
                float invD = 1.0f / dir[i];
                float t1 = (bMin[i] - orig[i]) * invD;
                float t2 = (bMax[i] - orig[i]) * invD;
                if (t1 > t2) {
                    std::swap(t1, t2);
                }
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax) {
                    return false;
                }
            }
        }
        outTNear = tMin;
        return true;
    }

    float ViewportInteractionManager::pointToSegmentDistance2D(
        const FCT::Vec2& p,
        const FCT::Vec2& a,
        const FCT::Vec2& b) noexcept {
        const FCT::Vec2 v = b - a;
        const float l2 = v.x * v.x + v.y * v.y;
        if (l2 < 1e-6f) {
            const float dx = p.x - a.x;
            const float dy = p.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        const float u = std::clamp(((p.x - a.x) * v.x + (p.y - a.y) * v.y) / l2, 0.0f, 1.0f);
        const FCT::Vec2 proj = a + v * u;
        const float dx = p.x - proj.x;
        const float dy = p.y - proj.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    ViewportInteractionTier ViewportInteractionManager::resolveInteractionTier(
        bool isCameraNavigating,
        bool isHomeLbmActive,
        bool isGizmoInteracting,
        bool isLmbClickedInViewport) noexcept {
        if (isCameraNavigating) {
            return ViewportInteractionTier::CameraNavigation;
        }
        if (isHomeLbmActive) {
            return ViewportInteractionTier::HomeLbmTool;
        }
        if (isGizmoInteracting) {
            return ViewportInteractionTier::GizmoTransform;
        }
        if (isLmbClickedInViewport) {
            return ViewportInteractionTier::EntityPicking;
        }
        return ViewportInteractionTier::None;
    }

    bool ViewportInteractionManager::isHomeLbmToolActive() {
        if (!Runtime::Engine::getInstance()->host->systemManager) {
            return false;
        }
        ISystem* sys = Runtime::Engine::getInstance()->host->systemManager->findSystem("HomeLbmSystem");
        if (!sys) {
            return false;
        }
        auto* lbmSys = static_cast<HomeLbmSystem*>(sys);
        return lbmSys->cubeTool().isCreationActive() || lbmSys->cubeTool().isDraggingFace();
    }

    bool ViewportInteractionManager::resolveActiveCameraParameters(
        EditorCameraManager* cameraMgr,
        ViewportCameraData& outParams) {
        if (cameraMgr) {
            entt::registry* reg = nullptr;
            entt::entity camEnt = entt::null;
            if (cameraMgr->resolveControlledCamera(reg, camEnt) && reg && reg->valid(camEnt)) {
                outParams.position = EditorCameraManager::queryEntityWorldPosition(reg, camEnt);
                if (const auto* rot = reg->try_get<RotationComponent>(camEnt)) {
                    float pitchRad = rot->rotation.x * kDegToRad;
                    float yawRad = rot->rotation.y * kDegToRad;
                    FCT::Vec3<> fwd(
                        std::cos(yawRad) * std::cos(pitchRad),
                        std::sin(pitchRad),
                        std::sin(yawRad) * std::cos(pitchRad));
                    outParams.forward = fwd.length() > 1e-5f ? FCT::normalize(fwd) : FCT::Vec3<>(0.0f, 0.0f, -1.0f);
                    FCT::Vec3<> r = FCT::cross(outParams.forward, FCT::Vec3<>(0.0f, 1.0f, 0.0f));
                    FCT::Vec3<> right = r.length() > 1e-5f ? FCT::normalize(r) : FCT::Vec3<>(1.0f, 0.0f, 0.0f);
                    outParams.up = FCT::normalize(FCT::cross(right, outParams.forward));
                }
                if (const auto* cam = reg->try_get<CameraComponent>(camEnt)) {
                    outParams.fovDegrees = cam->fov;
                }
                return true;
            }
        }

        if (Runtime::Engine::getInstance()->renderer->getCameraSystem()) {
            CameraComponent camComp;
            if (Runtime::Engine::getInstance()->renderer->getCameraSystem()->getActiveCameraWorldTransform(
                outParams.position, outParams.forward, outParams.up, camComp)) {
                outParams.fovDegrees = camComp.fov;
                return true;
            }
        }
        return false;
    }

    entt::entity ViewportInteractionManager::pickEntityInRegistry(
        const ViewportRay3D& ray,
        entt::registry* registry,
        PhysicsSystem* physicsSystem,
        float* outDist) {
        if (!registry) return entt::null;

        // 1. 如果有 PhysicsSystem，优先尝试 PhysX 快速射线检测
        if (physicsSystem) {
            RaycastHit hit;
            if (physicsSystem->raycast(ray.origin, ray.direction, 10000.0f, hit)) {
                if (hit.hasHit && hit.entity != entt::null && registry->valid(hit.entity)) {
                    if (outDist) *outDist = hit.distance;
                    return hit.entity;
                }
            }
        }

        // 2. AABB 遍历扫描保底
        float bestDist = std::numeric_limits<float>::max();
        entt::entity bestEntity = entt::null;

        auto view = registry->view<PositionComponent>();
        for (auto entity : view) {
            FCT::Vec3<> worldPos = queryEntityWorldPosition(registry, entity);
            FCT::Vec3<> scale(1.0f, 1.0f, 1.0f);
            if (const auto* sc = registry->try_get<ScaleComponent>(entity)) {
                scale = sc->scale;
            }

            FCT::Vec3<> center = worldPos;
            FCT::Vec3<> halfExtents(
                std::max(0.5f, std::abs(scale.x) * 0.5f),
                std::max(0.5f, std::abs(scale.y) * 0.5f),
                std::max(0.5f, std::abs(scale.z) * 0.5f));

            if (const auto* box = registry->try_get<BoxColliderComponent>(entity)) {
                halfExtents = FCT::Vec3<>(
                    std::abs(box->halfExtents.x * scale.x),
                    std::abs(box->halfExtents.y * scale.y),
                    std::abs(box->halfExtents.z * scale.z));
                center = worldPos + box->center;
            } else if (const auto* sphere = registry->try_get<SphereColliderComponent>(entity)) {
                float maxS = std::max({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
                float r = std::abs(sphere->radius) * (maxS > 1e-4f ? maxS : 1.0f);
                halfExtents = FCT::Vec3<>(r, r, r);
                center = worldPos + sphere->center;
            } else if (const auto* capsule = registry->try_get<CapsuleColliderComponent>(entity)) {
                float rad = std::abs(capsule->radius) * std::max(std::abs(scale.x), std::abs(scale.z));
                float halfH = (std::abs(capsule->halfHeight) + std::abs(capsule->radius)) * std::abs(scale.y);
                halfExtents = FCT::Vec3<>(rad, halfH, rad);
                center = worldPos + capsule->center;
            }

            FCT::Vec3<> boxMin = center - halfExtents;
            FCT::Vec3<> boxMax = center + halfExtents;

            float tNear = 0.0f;
            if (intersectRayAabb(ray.origin, ray.direction, boxMin, boxMax, tNear)) {
                if (tNear > 0.0f && tNear < bestDist) {
                    bestDist = tNear;
                    bestEntity = entity;
                }
            }
        }

        if (bestEntity != entt::null && outDist) {
            *outDist = bestDist;
        }
        return bestEntity;
    }

    bool ViewportInteractionManager::pickEntityAtRay(
        const ViewportRay3D& ray,
        ViewportPickResult& outResult,
        Scene::Scene* scene,
        PhysicsSystem* physicsSystem) {
        outResult = ViewportPickResult{};

        if (!scene && g_editorGlobal.dataManager) {
            scene = g_editorGlobal.dataManager->getCurrentScene();
        }
        if (!physicsSystem) {
            physicsSystem = Runtime::Engine::getInstance()->physics->getPhysicsSystem();
        }

        if (!scene) {
            return false;
        }

        // Tier 1: PhysX 射线检测快速通道
        if (physicsSystem) {
            RaycastHit hit;
            if (physicsSystem->raycast(ray.origin, ray.direction, 10000.0f, hit)) {
                if (hit.hasHit && hit.entity != entt::null) {
                    float bestActorDist = std::numeric_limits<float>::max();
                    auto evaluateCandidateReg = [&](entt::registry* candReg, bool isGlobal, const std::string& trunkName) {
                        if (!candReg || !candReg->valid(hit.entity) || !candReg->all_of<CachePhysicsActor>(hit.entity)) {
                            return;
                        }
                        const auto& physActor = candReg->get<CachePhysicsActor>(hit.entity);
                        if (!physActor.actor) {
                            return;
                        }
                        const physx::PxTransform pose = physActor.actor->getGlobalPose();
                        const FCT::Vec3<> actorPos(pose.p.x, pose.p.y, pose.p.z);
                        const float d = (actorPos - hit.point).length();
                        if (d < bestActorDist) {
                            bestActorDist = d;
                            outResult.entity = hit.entity;
                            outResult.isGlobal = isGlobal;
                            outResult.trunkName = trunkName;
                            outResult.distance = hit.distance;
                            outResult.hitPoint = hit.point;
                            outResult.hasHit = true;
                        }
                    };

                    if (auto* globalReg = scene->getRegistry()) {
                        evaluateCandidateReg(globalReg, true, "");
                    }
                    for (const auto& trunkName : scene->getTrunkList()) {
                        if (scene->isLoad(trunkName)) {
                            if (SceneTrunk* trunk = scene->getLoadedTrunk(trunkName)) {
                                evaluateCandidateReg(trunk->getRegistry(), false, trunkName);
                            }
                        }
                    }
                    if (outResult.hasHit) {
                        return true;
                    }
                }
            }
        }

        // Tier 2: 场景注册表 AABB 遍历扫描保底
        float bestDist = std::numeric_limits<float>::max();

        if (auto* globalReg = scene->getRegistry()) {
            float dist = 0.0f;
            entt::entity ent = pickEntityInRegistry(ray, globalReg, nullptr, &dist);
            if (ent != entt::null && dist < bestDist) {
                bestDist = dist;
                outResult.entity = ent;
                outResult.isGlobal = true;
                outResult.trunkName = "";
                outResult.distance = dist;
                outResult.hitPoint = ray.origin + ray.direction * dist;
                outResult.hasHit = true;
            }
        }

        for (const auto& trunkName : scene->getTrunkList()) {
            if (scene->isLoad(trunkName)) {
                if (SceneTrunk* trunk = scene->getLoadedTrunk(trunkName)) {
                    if (auto* trunkReg = trunk->getRegistry()) {
                        float dist = 0.0f;
                        entt::entity ent = pickEntityInRegistry(ray, trunkReg, nullptr, &dist);
                        if (ent != entt::null && dist < bestDist) {
                            bestDist = dist;
                            outResult.entity = ent;
                            outResult.isGlobal = false;
                            outResult.trunkName = trunkName;
                            outResult.distance = dist;
                            outResult.hitPoint = ray.origin + ray.direction * dist;
                            outResult.hasHit = true;
                        }
                    }
                }
            }
        }

        return outResult.hasHit;
    }

    GizmoAxis ViewportInteractionManager::pickGizmoHandle(
        const FCT::Vec2& mousePos,
        const FCT::Vec2& s0,
        const FCT::Vec2& sx,
        const FCT::Vec2& sy,
        const FCT::Vec2& sz) const noexcept {
        const float dCenter = std::sqrt((mousePos.x - s0.x) * (mousePos.x - s0.x) + (mousePos.y - s0.y) * (mousePos.y - s0.y));
        if (dCenter <= 10.0f) {
            return GizmoAxis::Center;
        }

        const float dX = pointToSegmentDistance2D(mousePos, s0, sx);
        const float dY = pointToSegmentDistance2D(mousePos, s0, sy);
        const float dZ = pointToSegmentDistance2D(mousePos, s0, sz);

        float minD = 7.5f;
        GizmoAxis best = GizmoAxis::None;

        if (dX < minD) {
            minD = dX;
            best = GizmoAxis::X;
        }
        if (dY < minD) {
            minD = dY;
            best = GizmoAxis::Y;
        }
        if (dZ < minD) {
            minD = dZ;
            best = GizmoAxis::Z;
        }
        return best;
    }

    void ViewportInteractionManager::drawArrowhead(
        ImDrawList* drawList,
        const FCT::Vec2& startScreen,
        const FCT::Vec2& endScreen,
        ImU32 color,
        float length,
        float halfWidth) const noexcept {
        if (!drawList) return;
        const float dx = endScreen.x - startScreen.x;
        const float dy = endScreen.y - startScreen.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 10.0f) {
            return;
        }
        const float ux = dx / len;
        const float uy = dy / len;
        const float vx = -uy;
        const float vy = ux;

        const ImVec2 tip(endScreen.x, endScreen.y);
        const ImVec2 base(endScreen.x - ux * length, endScreen.y - uy * length);
        const ImVec2 left(base.x + vx * halfWidth, base.y + vy * halfWidth);
        const ImVec2 right(base.x - vx * halfWidth, base.y - vy * halfWidth);

        drawList->AddTriangleFilled(tip, left, right, color);
    }

    void ViewportInteractionManager::renderGizmoVisuals(
        ImDrawList* drawList,
        const FCT::Vec2& s0,
        const FCT::Vec2& sx,
        const FCT::Vec2& sy,
        const FCT::Vec2& sz,
        bool visX,
        bool visY,
        bool visZ,
        GizmoAxis highlightAxis) const {
        if (!drawList) return;

        // X 轴 (红色)
        if (visX) {
            const bool hx = (highlightAxis == GizmoAxis::X);
            const ImU32 colX = hx ? IM_COL32(255, 95, 85, 255) : IM_COL32(235, 64, 52, 255);
            drawList->AddLine(ImVec2(s0.x, s0.y), ImVec2(sx.x, sx.y), colX, hx ? 4.0f : 2.5f);
            drawArrowhead(drawList, s0, sx, colX);
            drawList->AddText(ImVec2(sx.x + 4.0f, sx.y - 6.0f), colX, "X");
        }

        // Y 轴 (绿色)
        if (visY) {
            const bool hy = (highlightAxis == GizmoAxis::Y);
            const ImU32 colY = hy ? IM_COL32(95, 230, 105, 255) : IM_COL32(67, 196, 75, 255);
            drawList->AddLine(ImVec2(s0.x, s0.y), ImVec2(sy.x, sy.y), colY, hy ? 4.0f : 2.5f);
            drawArrowhead(drawList, s0, sy, colY);
            drawList->AddText(ImVec2(sy.x + 4.0f, sy.y - 6.0f), colY, "Y");
        }

        // Z 轴 (蓝色)
        if (visZ) {
            const bool hz = (highlightAxis == GizmoAxis::Z);
            const ImU32 colZ = hz ? IM_COL32(85, 165, 255, 255) : IM_COL32(52, 131, 235, 255);
            drawList->AddLine(ImVec2(s0.x, s0.y), ImVec2(sz.x, sz.y), colZ, hz ? 4.0f : 2.5f);
            drawArrowhead(drawList, s0, sz, colZ);
            drawList->AddText(ImVec2(sz.x + 4.0f, sz.y - 6.0f), colZ, "Z");
        }

        // 中心屏幕平面手柄 (黄色)
        {
            const bool hc = (highlightAxis == GizmoAxis::Center);
            const ImU32 colCenter = hc ? IM_COL32(255, 235, 80, 255) : IM_COL32(235, 204, 52, 200);
            const float radius = hc ? 10.5f : 8.0f;
            drawList->AddCircleFilled(ImVec2(s0.x, s0.y), radius, colCenter);
            drawList->AddCircle(ImVec2(s0.x, s0.y), radius, IM_COL32(255, 255, 255, 230), 16, 1.5f);
        }
    }

    void ViewportInteractionManager::cancelDrag() noexcept {
        m_isDragging = false;
        m_activeAxis = GizmoAxis::None;
    }

    void ViewportInteractionManager::updateAndRender(
        UiContext& ctx,
        const ViewportViewState& viewState,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        bool isViewportHovered,
        bool isWindowFocused) {
        ViewportCameraData camParams{};
        if (!resolveActiveCameraParameters(g_editorGlobal.cameraManager, camParams)) {
            camParams.position = viewState.cameraPos;
            camParams.fovDegrees = 60.0f;
            camParams.forward = FCT::Vec3<>(0.0f, 0.0f, -1.0f);
            camParams.up = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
        }

        const ImGuiIO& io = ImGui::GetIO();
        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool altLmbDown = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool isCameraNavigating = rmbDown || mmbDown || altLmbDown;

        const bool isHomeLbmActive = isHomeLbmToolActive();

        entt::registry* selReg = resolveSelectedEntityRegistry(g_editorGlobal.selectedEntity);
        entt::entity selEnt = g_editorGlobal.selectedEntity.entity;
        const bool hasSelected = (selReg != nullptr && selEnt != entt::null && selReg->valid(selEnt) && selReg->all_of<PositionComponent>(selEnt));

        // 优先级 1 与 优先级 2 独占抑制
        if (isCameraNavigating || isHomeLbmActive) {
            if (m_isDragging) {
                cancelDrag();
            }
            m_hoveredAxis = GizmoAxis::None;
            if (hasSelected) {
                // 仅静止渲染 Gizmo（无交互高亮）
                const FCT::Vec3<> originWorld = queryEntityWorldPosition(selReg, selEnt);
                FCT::Vec2 s0{};
                float zCam = 0.0f;
                if (projectWorldToScreen(originWorld, vpMin, vpSize, camParams, s0, &zCam)) {
                    const float tanHalfFov = std::tan(std::clamp(camParams.fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
                    const float axisLength = std::clamp(75.0f * (2.0f * zCam * tanHalfFov / std::max(1.0f, vpSize.y)), 0.05f, 500.0f);

                    FCT::Vec2 sx{}, sy{}, sz{};
                    const bool visX = projectWorldToScreen(originWorld + FCT::Vec3<>(axisLength, 0.0f, 0.0f), vpMin, vpSize, camParams, sx);
                    const bool visY = projectWorldToScreen(originWorld + FCT::Vec3<>(0.0f, axisLength, 0.0f), vpMin, vpSize, camParams, sy);
                    const bool visZ = projectWorldToScreen(originWorld + FCT::Vec3<>(0.0f, 0.0f, axisLength), vpMin, vpSize, camParams, sz);

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->PushClipRect(ImVec2(vpMin.x, vpMin.y), ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);
                    renderGizmoVisuals(drawList, s0, sx, sy, sz, visX, visY, visZ, GizmoAxis::None);
                    drawList->PopClipRect();
                }
            }
            return;
        }

        const FCT::Vec2 mousePos(io.MousePos.x, io.MousePos.y);
        const ViewportRay3D curRay = buildCameraRay(mousePos, vpMin, vpSize, camParams);

        bool gizmoInteractedThisFrame = false;
        FCT::Vec2 s0{}, sx{}, sy{}, sz{};
        bool visX = false, visY = false, visZ = false;
        bool gizmoVisible = false;

        if (hasSelected) {
            const FCT::Vec3<> originWorld = queryEntityWorldPosition(selReg, selEnt);
            float zCam = 0.0f;
            if (projectWorldToScreen(originWorld, vpMin, vpSize, camParams, s0, &zCam)) {
                gizmoVisible = true;
                const float tanHalfFov = std::tan(std::clamp(camParams.fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
                const float axisLength = std::clamp(75.0f * (2.0f * zCam * tanHalfFov / std::max(1.0f, vpSize.y)), 0.05f, 500.0f);

                visX = projectWorldToScreen(originWorld + FCT::Vec3<>(axisLength, 0.0f, 0.0f), vpMin, vpSize, camParams, sx);
                visY = projectWorldToScreen(originWorld + FCT::Vec3<>(0.0f, axisLength, 0.0f), vpMin, vpSize, camParams, sy);
                visZ = projectWorldToScreen(originWorld + FCT::Vec3<>(0.0f, 0.0f, axisLength), vpMin, vpSize, camParams, sz);

                if (!m_isDragging) {
                    m_hoveredAxis = isViewportHovered ? pickGizmoHandle(mousePos, s0, sx, sy, sz) : GizmoAxis::None;
                    if (m_hoveredAxis != GizmoAxis::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        m_isDragging = true;
                        m_activeAxis = m_hoveredAxis;
                        m_dragStartLocalPos = selReg->get<PositionComponent>(selEnt).position;
                        m_dragStartWorldPos = originWorld;

                        if (m_activeAxis == GizmoAxis::X) {
                            m_initialAxisParam = closestAxisParameterToRay(originWorld, FCT::Vec3<>(1.0f, 0.0f, 0.0f), curRay);
                        } else if (m_activeAxis == GizmoAxis::Y) {
                            m_initialAxisParam = closestAxisParameterToRay(originWorld, FCT::Vec3<>(0.0f, 1.0f, 0.0f), curRay);
                        } else if (m_activeAxis == GizmoAxis::Z) {
                            m_initialAxisParam = closestAxisParameterToRay(originWorld, FCT::Vec3<>(0.0f, 0.0f, 1.0f), curRay);
                        } else if (m_activeAxis == GizmoAxis::Center) {
                            intersectRayPlane(curRay, originWorld, camParams.forward, m_initialPlaneHit);
                        }
                        gizmoInteractedThisFrame = true;
                    }
                }

                if (m_isDragging) {
                    gizmoInteractedThisFrame = true;
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        FCT::Vec3<> targetWorldPos = m_dragStartWorldPos;
                        if (m_activeAxis == GizmoAxis::X) {
                            const float curParam = closestAxisParameterToRay(m_dragStartWorldPos, FCT::Vec3<>(1.0f, 0.0f, 0.0f), curRay);
                            targetWorldPos.x += (curParam - m_initialAxisParam);
                        } else if (m_activeAxis == GizmoAxis::Y) {
                            const float curParam = closestAxisParameterToRay(m_dragStartWorldPos, FCT::Vec3<>(0.0f, 1.0f, 0.0f), curRay);
                            targetWorldPos.y += (curParam - m_initialAxisParam);
                        } else if (m_activeAxis == GizmoAxis::Z) {
                            const float curParam = closestAxisParameterToRay(m_dragStartWorldPos, FCT::Vec3<>(0.0f, 0.0f, 1.0f), curRay);
                            targetWorldPos.z += (curParam - m_initialAxisParam);
                        } else if (m_activeAxis == GizmoAxis::Center) {
                            FCT::Vec3<> curHit{};
                            if (intersectRayPlane(curRay, m_dragStartWorldPos, camParams.forward, curHit)) {
                                targetWorldPos = m_dragStartWorldPos + (curHit - m_initialPlaneHit);
                            }
                        }

                        // 60fps 实时同步：更新局部坐标并刷新矩阵缓存与 Inspector
                        selReg->get<PositionComponent>(selEnt).position = worldToEntityLocalPosition(selReg, selEnt, targetWorldPos);
                        refreshEntityMatrixCache(selReg, selEnt);
                    } else {
                        // 鼠标释放：位移判定与撤销/重做命令压入
                        const FCT::Vec3<> finalLocalPos = selReg->get<PositionComponent>(selEnt).position;
                        const float displacement = (finalLocalPos - m_dragStartLocalPos).length();

                        if (displacement > 1e-6f && g_editorGlobal.undoManager) {
                            auto cmd = makeUnique<TranslateEntityCommand>(
                                selReg,
                                selEnt,
                                m_dragStartLocalPos,
                                finalLocalPos,
                                Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem());
                            g_editorGlobal.undoManager->pushExecutedCommand(std::move(cmd));
                        }
                        m_isDragging = false;
                        m_activeAxis = GizmoAxis::None;
                    }
                }
            } else {
                cancelDrag();
                m_hoveredAxis = GizmoAxis::None;
            }
        } else {
            cancelDrag();
            m_hoveredAxis = GizmoAxis::None;
        }

        // 优先级 4: 视口物体射线拾取（未被前序优先级消费的左键单击）
        const bool isGizmoClaiming = gizmoInteractedThisFrame || (m_hoveredAxis != GizmoAxis::None) || m_isDragging;
        const bool isLmbClicked = isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const ViewportInteractionTier tier = resolveInteractionTier(false, false, isGizmoClaiming, isLmbClicked);

        if (tier == ViewportInteractionTier::EntityPicking) {
            ViewportPickResult pickResult;
            if (pickEntityAtRay(curRay, pickResult)) {
                ctx.emit(SelectSceneEntitySignal{
                    .entity = pickResult.entity,
                    .trunkName = pickResult.trunkName,
                    .isGlobal = pickResult.isGlobal
                });
                g_editorGlobal.selectedEntity.entity = pickResult.entity;
                g_editorGlobal.selectedEntity.trunkName = pickResult.trunkName;
                g_editorGlobal.selectedEntity.isGlobal = pickResult.isGlobal;
                if (g_editorGlobal.dataManager && g_editorGlobal.dataManager->getCurrentScene()) {
                    g_editorGlobal.selectedEntity.scene = g_editorGlobal.dataManager->getCurrentScene();
                }

                // 支持点击物体后直接按住左键在视平面拖拽移动（若仅单击原地释放则位移为 0，不会污染撤销栈）
                entt::registry* pickedReg = resolveSelectedEntityRegistry(g_editorGlobal.selectedEntity);
                if (pickedReg && pickedReg->valid(pickResult.entity) && pickedReg->all_of<PositionComponent>(pickResult.entity)) {
                    const FCT::Vec3<> pickedWorldPos = queryEntityWorldPosition(pickedReg, pickResult.entity);
                    m_isDragging = true;
                    m_activeAxis = GizmoAxis::Center;
                    m_dragStartLocalPos = pickedReg->get<PositionComponent>(pickResult.entity).position;
                    m_dragStartWorldPos = pickedWorldPos;
                    intersectRayPlane(curRay, pickedWorldPos, camParams.forward, m_initialPlaneHit);
                }
            } else {
                ctx.emit(SelectSceneEntitySignal{
                    .entity = entt::null,
                    .trunkName = "",
                    .isGlobal = false
                });
            }
        }

        // 渲染 3D 平移 Gizmo
        if (hasSelected && gizmoVisible) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(ImVec2(vpMin.x, vpMin.y), ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y), true);
            const GizmoAxis highlight = m_isDragging ? m_activeAxis : m_hoveredAxis;
            renderGizmoVisuals(drawList, s0, sx, sy, sz, visX, visY, visZ, highlight);
            drawList->PopClipRect();
        }
    }

} // namespace MQEngine
