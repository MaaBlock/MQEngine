#include "HomeLbmSystem.h"

namespace MQEngine {

namespace {

bool boxesOverlap(
    const FCT::Vec3<>& minA,
    const FCT::Vec3<>& maxA,
    const FCT::Vec3<>& minB,
    const FCT::Vec3<>& maxB) noexcept {
    return (minA.x <= maxB.x && maxA.x >= minB.x) &&
           (minA.y <= maxB.y && maxA.y >= minB.y) &&
           (minA.z <= maxB.z && maxA.z >= minB.z);
}

bool intersectRayAabb(
    const LbmRay3D& ray,
    const FCT::Vec3<>& boxMin,
    const FCT::Vec3<>& boxMax,
    float& outT) noexcept {
    float tMin = 1e-3f;
    float tMax = 1e20f;
    const float orig[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float dir[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float bMin[3] = {boxMin.x, boxMin.y, boxMin.z};
    const float bMax[3] = {boxMax.x, boxMax.y, boxMax.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-7f) {
            if (orig[axis] < bMin[axis] || orig[axis] > bMax[axis]) {
                return false;
            }
        } else {
            float invD = 1.0f / dir[axis];
            float t0 = (bMin[axis] - orig[axis]) * invD;
            float t1 = (bMax[axis] - orig[axis]) * invD;
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin) {
                return false;
            }
        }
    }
    outT = tMin;
    return true;
}

bool intersectRaySphere(
    const LbmRay3D& ray,
    const FCT::Vec3<>& center,
    float radius,
    float& outT) noexcept {
    const FCT::Vec3<> oc = ray.origin - center;
    const float b = oc.x * ray.direction.x + oc.y * ray.direction.y + oc.z * ray.direction.z;
    const float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }
    const float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc;
    if (t < 1e-3f) {
        t = -b + sqrtDisc;
    }
    if (t < 1e-3f) {
        return false;
    }
    outT = t;
    return true;
}

std::vector<entt::registry*> collectCandidateRegistries(DataManager* dataManager) {
    std::vector<entt::registry*> result;
    auto appendUnique = [&](entt::registry* reg) {
        if (reg && std::find(result.begin(), result.end(), reg) == result.end()) {
            result.push_back(reg);
        }
    };

    // Note: 优先收集当前活跃场景及其已加载 Trunk 的注册表，确保新建实体落入主场景而非编辑器内部相机注册表
    if (dataManager) {
        if (Scene::Scene* curScene = dataManager->getCurrentScene()) {
            appendUnique(curScene->getRegistry());
            for (const auto& trunkName : curScene->getTrunkList()) {
                if (curScene->isLoad(trunkName)) {
                    if (SceneTrunk* trunk = curScene->getLoadedTrunk(trunkName)) {
                        appendUnique(trunk->getRegistry());
                    }
                }
            }
        }
        for (entt::registry* reg : dataManager->currentRegistries()) {
            appendUnique(reg);
        }
    }

    if (g_engineGlobal.registriesManager) {
        for (entt::registry* reg : g_engineGlobal.registriesManager->currentRegistries()) {
            appendUnique(reg);
        }
    }
    return result;
}

FCT::Vec3<> resolveEntityPosition(const entt::registry& reg, entt::entity e) {
    if (const auto* cacheMat = reg.try_get<CacheModelMatrix>(e); cacheMat && cacheMat->init) {
        const FCT::Vec4 wp = cacheMat->worldMatrix * FCT::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return FCT::Vec3<>(wp.x, wp.y, wp.z);
    }
    if (const auto* pos = reg.try_get<PositionComponent>(e)) {
        return pos->position;
    }
    return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
}

FCT::Vec3<> resolveEntityAbsScale(const entt::registry& reg, entt::entity e) {
    if (const auto* sc = reg.try_get<ScaleComponent>(e)) {
        return FCT::Vec3<>(
            std::max(1e-3f, std::abs(sc->scale.x)),
            std::max(1e-3f, std::abs(sc->scale.y)),
            std::max(1e-3f, std::abs(sc->scale.z)));
    }
    return FCT::Vec3<>(1.0f, 1.0f, 1.0f);
}

int32_t mapObstacleShape(LbmPrimitiveShape shape) noexcept {
    switch (shape) {
        case LbmPrimitiveShape::Box: return HomeLbmConstants::kPrimObstacleBox;
        case LbmPrimitiveShape::CapsuleY: return HomeLbmConstants::kPrimObstacleCapsuleY;
        default: return HomeLbmConstants::kPrimObstacleSphere;
    }
}

int32_t mapEmitterShape(LbmPrimitiveShape shape) noexcept {
    return (shape == LbmPrimitiveShape::Box)
        ? HomeLbmConstants::kPrimEmitterBox
        : HomeLbmConstants::kPrimEmitterSphere;
}

void autoProportionBaseBlocks(LbmDomainComponent& domain) noexcept {
    const FCT::Vec3<> span = domain.maxBounds - domain.minBounds;
    const float minSpan = std::max(0.25f, std::min({span.x, span.y, span.z}));
    constexpr int32_t kMinAxisBlocks = 4;
    domain.baseBlocksX = std::clamp(
        static_cast<int32_t>(std::lround((span.x / minSpan) * kMinAxisBlocks)), 2, 14);
    domain.baseBlocksY = std::clamp(
        static_cast<int32_t>(std::lround((span.y / minSpan) * kMinAxisBlocks)), 2, 14);
    domain.baseBlocksZ = std::clamp(
        static_cast<int32_t>(std::lround((span.z / minSpan) * kMinAxisBlocks)), 2, 14);
}

} // namespace

HomeLbmSystem::HomeLbmSystem(FCT::Context* rhiContext, DataManager* dataManager)
    : m_rhiContext(rhiContext), m_dataManager(dataManager) {}

HomeLbmSystem::~HomeLbmSystem() {
    shutdown();
}

Status HomeLbmSystem::initialize() {
    if (m_rhiContext) {
        CHECK_STATUS(m_renderer.initialize(m_rhiContext));
    }
    m_lastStatusMessage = "HOME-LBM 系统已初始化";
    publishUiSnapshot(nullptr, entt::null);
    return OkStatus();
}

void HomeLbmSystem::shutdown() {
    m_renderer.shutdown();
    m_solver.shutdown();
    m_hasTrackedBounds = false;
}

void HomeLbmSystem::submitUiCommand(HomeLbmUiPendingCommand cmd) {
    if (!cmd.hasAnyAction()) {
        return;
    }
    const uint32_t writeIdx = m_uiCmdWriteIdx.load(std::memory_order_relaxed) & 1u;
    m_pendingUiCommands[writeIdx] = std::move(cmd);
    m_hasPendingUiCmd.store(true, std::memory_order_release);
}

void HomeLbmSystem::consumePendingUiCommands() {
    if (!m_hasPendingUiCmd.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const uint32_t readIdx = m_uiCmdWriteIdx.fetch_xor(1u, std::memory_order_acq_rel) & 1u;
    HomeLbmUiPendingCommand cmd = std::move(m_pendingUiCommands[readIdx]);
    m_pendingUiCommands[readIdx] = HomeLbmUiPendingCommand{};

    if (cmd.hasStatusMessage) {
        m_lastStatusMessage = std::move(cmd.statusMessage);
    }

    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;

    if (cmd.createDefaultDomain) {
        if (findPrimaryDomainEntity(reg, entity, true) && reg) {
            applyPresetToDomain(
                reg->get<LbmDomainComponent>(entity),
                LbmPresetScenario::WindTunnelKarmanVortex);
        }
        return;
    }

    if (cmd.hasBoxCommit) {
        if (findPrimaryDomainEntity(reg, entity, true) && reg) {
            auto& domain = reg->get<LbmDomainComponent>(entity);
            domain.minBounds = cmd.committedMinBounds;
            domain.maxBounds = cmd.committedMaxBounds;
            autoProportionBaseBlocks(domain);
            domain.enabled = true;
            domain.paused = false;
            domain.resetRequested = true;
            m_lastStatusMessage = "已通过 UE5 空间建盒创建并初始化三维自适应流体域";
        }
        return;
    }

    if (!findPrimaryDomainEntity(reg, entity, false) || !reg) {
        return;
    }

    auto& domain = reg->get<LbmDomainComponent>(entity);
    if (cmd.hasDomainPatch) {
        domain = cmd.patchedDomain;
    }
    if (cmd.hasPresetRequest) {
        applyPresetToDomain(domain, cmd.requestedPreset);
    }
    if (cmd.hasBackendSwitchRequest) {
        (void)requestBackendSwitch(cmd.requestedBackendMode);
    }
    if (cmd.requestDiagnostics) {
        (void)evaluateFieldDiagnostics(domain);
    }
}

void HomeLbmSystem::publishUiSnapshot(entt::registry* domainReg, entt::entity domainEntity) {
    const uint32_t curIdx = m_publishedUiSnapshotIdx.load(std::memory_order_relaxed) & 1u;
    const uint32_t nextIdx = 1u - curIdx;
    HomeLbmUiSnapshot& snap = m_uiSnapshots[nextIdx];

    snap.sequence = ++m_snapshotSequence;
    snap.hasDomain = (domainReg != nullptr && domainEntity != entt::null &&
                      domainReg->valid(domainEntity) &&
                      domainReg->all_of<LbmDomainComponent>(domainEntity));
    if (snap.hasDomain) {
        snap.domain = domainReg->get<LbmDomainComponent>(domainEntity);
    } else {
        snap.domain = LbmDomainComponent{};
    }

    snap.telemetry = m_solver.telemetry();
    snap.solverInitialized = m_solver.isInitialized();

    const auto& backend = m_solver.backend();
    snap.dxAvailable = backend.isBackendAvailable(LbmComputeBackendMode::MatchRHI_DX12);
    snap.cudaAvailable = backend.isBackendAvailable(LbmComputeBackendMode::CUDA);
    snap.vkAvailable = backend.isBackendAvailable(LbmComputeBackendMode::Vulkan);
    snap.sharingRhiDx12 = backend.isSharingRhiDx12Device();
    snap.activeBackendName = snap.solverInitialized ? backend.getActiveBackendDisplayName() : std::string{};
    snap.statusMessage = m_lastStatusMessage;

    float aspect = 16.0f / 9.0f;
    if (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->platform && Runtime::Engine::getInstance()->platform->getWindow()) {
        const float ww = static_cast<float>(std::max(1, Runtime::Engine::getInstance()->platform->getWindow()->getWidth()));
        const float wh = static_cast<float>(std::max(1, Runtime::Engine::getInstance()->platform->getWindow()->getHeight()));
        aspect = ww / wh;
    }
    snap.cameraView = resolveActiveCameraView(snap.domain, aspect);

    snap.amrWireframeBoxes.clear();
    if (snap.hasDomain && snap.domain.showAmrBlocks && snap.solverInitialized) {
        m_solver.amrGrid().collectWireframeBoxes(snap.amrWireframeBoxes);
    }

    snap.sceneColliderPrimitives.clear();
    const FCT::Vec3<> hugeMin(-1e5f, -1e5f, -1e5f);
    const FCT::Vec3<> hugeMax(1e5f, 1e5f, 1e5f);
    for (entt::registry* reg : collectCandidateRegistries(m_dataManager)) {
        collectSceneCollidersInRegistry(
            reg, domainEntity, hugeMin, hugeMax, snap.sceneColliderPrimitives);
    }

    m_publishedUiSnapshotIdx.store(nextIdx, std::memory_order_release);
}

bool HomeLbmSystem::findPrimaryDomainEntity(
    entt::registry*& outRegistry,
    entt::entity& outEntity,
    bool createIfMissing) {
    outRegistry = nullptr;
    outEntity = entt::null;

    const auto registries = collectCandidateRegistries(m_dataManager);
    for (entt::registry* reg : registries) {
        auto view = std::as_const(*reg).view<LbmDomainComponent>();
        for (entt::entity entity : view) {
            if (reg->valid(entity)) {
                outRegistry = reg;
                outEntity = entity;
                return true;
            }
        }
    }

    if (!createIfMissing || registries.empty()) {
        return false;
    }

    entt::registry* targetReg = registries.front();
    entt::entity created = targetReg->create();
    NameTag tag;
    tag.name = "HOME_LBM_Domain";
    targetReg->emplace<NameTag>(created, std::move(tag));
    targetReg->emplace<PositionComponent>(created, PositionComponent{FCT::Vec3<>(0.0f, 0.0f, 0.0f)});
    targetReg->emplace<LbmDomainComponent>(created);
    outRegistry = targetReg;
    outEntity = created;
    m_lastStatusMessage = "已创建 HOME-LBM 空间模拟域实体";
    return true;
}

void HomeLbmSystem::applyPresetToDomain(LbmDomainComponent& domain, LbmPresetScenario preset) {
    switch (preset) {
        case LbmPresetScenario::WindTunnelKarmanVortex: {
            domain.simMode = LbmSimulationMode::AirSmokeWindTunnel;
            domain.visMode = LbmVisualizationMode::VorticityMagnitude;
            domain.minBounds = FCT::Vec3<>(-4.0f, 0.0f, -2.0f);
            domain.maxBounds = FCT::Vec3<>(4.0f, 4.0f, 2.0f);
            domain.baseBlocksX = 8;
            domain.baseBlocksY = 4;
            domain.baseBlocksZ = 4;
            domain.maxAmrLevels = 2;
            domain.kinematicViscosity = 0.0022f;
            domain.smagorinskyCs = 0.08f;
            domain.inflowVelocity = FCT::Vec3<>(0.09f, 0.0f, 0.0f);
            domain.gravity = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
            domain.buoyancyAlpha = 0.0008f;
            domain.negXBoundary = LbmBoundaryType::VelocityInlet;
            domain.posXBoundary = LbmBoundaryType::PressureOutflow;
            domain.negYBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posYBoundary = LbmBoundaryType::FreeSlipWall;
            domain.negZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.enableBuiltinObstacle = true;
            domain.builtinObstacleShape = LbmPrimitiveShape::Sphere;
            domain.builtinObstacleNormalizedPos = FCT::Vec3<>(0.22f, 0.50f, 0.50f);
            domain.builtinObstacleRadiusRatio = 0.11f;
            m_lastStatusMessage = "已加载预设: 风洞球体绕流卡门涡街 (HOME-LBM)";
            break;
        }
        case LbmPresetScenario::FreeSurfaceDamBreak3D: {
            domain.simMode = LbmSimulationMode::FreeSurfaceLiquid;
            domain.visMode = LbmVisualizationMode::FreeSurfaceWater;
            domain.minBounds = FCT::Vec3<>(-3.0f, 0.0f, -2.0f);
            domain.maxBounds = FCT::Vec3<>(3.0f, 4.0f, 2.0f);
            domain.baseBlocksX = 6;
            domain.baseBlocksY = 4;
            domain.baseBlocksZ = 4;
            domain.maxAmrLevels = 2;
            domain.kinematicViscosity = 0.0040f;
            domain.smagorinskyCs = 0.10f;
            domain.inflowVelocity = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
            domain.gravity = FCT::Vec3<>(0.0f, -0.0018f, 0.0f);
            domain.buoyancyAlpha = 0.0f;
            domain.negXBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posXBoundary = LbmBoundaryType::FreeSlipWall;
            domain.negYBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posYBoundary = LbmBoundaryType::FreeSlipWall;
            domain.negZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.initialLiquidMinRatio = FCT::Vec3<>(0.03f, 0.02f, 0.04f);
            domain.initialLiquidMaxRatio = FCT::Vec3<>(0.36f, 0.72f, 0.96f);
            domain.enableBuiltinObstacle = true;
            domain.builtinObstacleShape = LbmPrimitiveShape::Box;
            domain.builtinObstacleNormalizedPos = FCT::Vec3<>(0.68f, 0.22f, 0.50f);
            domain.builtinObstacleRadiusRatio = 0.11f;
            m_lastStatusMessage = "已加载预设: 三维溃坝撞柱自由液面 (HOME-FREE)";
            break;
        }
        case LbmPresetScenario::ThermalBuoyantPlume: {
            domain.simMode = LbmSimulationMode::AirSmokeWindTunnel;
            domain.visMode = LbmVisualizationMode::SmokeDensity;
            domain.minBounds = FCT::Vec3<>(-2.5f, 0.0f, -2.5f);
            domain.maxBounds = FCT::Vec3<>(2.5f, 5.0f, 2.5f);
            domain.baseBlocksX = 5;
            domain.baseBlocksY = 5;
            domain.baseBlocksZ = 5;
            domain.maxAmrLevels = 2;
            domain.kinematicViscosity = 0.0020f;
            domain.smagorinskyCs = 0.09f;
            domain.inflowVelocity = FCT::Vec3<>(0.0f, 0.055f, 0.0f);
            domain.gravity = FCT::Vec3<>(0.0f, -0.0005f, 0.0f);
            domain.buoyancyAlpha = 0.0038f;
            domain.negXBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posXBoundary = LbmBoundaryType::FreeSlipWall;
            domain.negYBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posYBoundary = LbmBoundaryType::PressureOutflow;
            domain.negZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.posZBoundary = LbmBoundaryType::FreeSlipWall;
            domain.enableBuiltinObstacle = false;
            m_lastStatusMessage = "已加载预设: 热浮力湍流烟羽 (HOME-LBM)";
            break;
        }
    }
    domain.paused = false;
    domain.resetRequested = true;
}

Status HomeLbmSystem::requestBackendSwitch(LbmComputeBackendMode targetMode) {
    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (findPrimaryDomainEntity(reg, entity, false) && reg) {
        reg->get<LbmDomainComponent>(entity).backendMode = targetMode;
    }

    if (m_solver.isInitialized()) {
        Status st = m_solver.switchBackend(targetMode, m_rhiContext);
        if (!st.ok()) {
            m_lastStatusMessage = StrCat("切换计算后端失败: ", st.message());
            return st;
        }
        m_lastStatusMessage = StrCat(
            "已热迁移至后端: ",
            m_solver.backend().getActiveBackendDisplayName());
    }
    return OkStatus();
}

Status HomeLbmSystem::evaluateFieldDiagnostics(const LbmDomainComponent& domain) {
    if (!m_solver.isInitialized()) {
        return FailedPreconditionError("求解器未就绪，无法读取流场诊断指标");
    }
    return m_solver.evaluateFieldDiagnostics(domain);
}

Status HomeLbmSystem::stepManual(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    int32_t steps) {
    if (!m_solver.isInitialized()) {
        return FailedPreconditionError("求解器未就绪");
    }
    const int32_t clampedSteps = std::clamp(steps, 1, 64);
    for (int32_t i = 0; i < clampedSteps; ++i) {
        CHECK_STATUS(m_solver.step(domain, primitives));
    }
    return OkStatus();
}

bool HomeLbmSystem::raycastSceneColliders(const LbmRay3D& ray, FCT::Vec3<>& outHitPoint) const {
    float bestT = std::numeric_limits<float>::max();
    bool hitAny = false;

    for (entt::registry* reg : collectCandidateRegistries(m_dataManager)) {
        auto boxView = std::as_const(*reg).view<BoxColliderComponent>();
        for (entt::entity e : boxView) {
            const auto& col = boxView.get<BoxColliderComponent>(e);
            if (col.isTrigger) {
                continue;
            }
            const FCT::Vec3<> pos = resolveEntityPosition(*reg, e) + col.center;
            const FCT::Vec3<> sc = resolveEntityAbsScale(*reg, e);
            const FCT::Vec3<> ext(col.halfExtents.x * sc.x, col.halfExtents.y * sc.y, col.halfExtents.z * sc.z);
            float t = 0.0f;
            if (intersectRayAabb(ray, pos - ext, pos + ext, t) && t < bestT) {
                bestT = t;
                hitAny = true;
            }
        }

        auto sphereView = std::as_const(*reg).view<SphereColliderComponent>();
        for (entt::entity e : sphereView) {
            const auto& col = sphereView.get<SphereColliderComponent>(e);
            if (col.isTrigger) {
                continue;
            }
            const FCT::Vec3<> pos = resolveEntityPosition(*reg, e) + col.center;
            const FCT::Vec3<> sc = resolveEntityAbsScale(*reg, e);
            const float rad = col.radius * std::max({sc.x, sc.y, sc.z});
            float t = 0.0f;
            if (intersectRaySphere(ray, pos, rad, t) && t < bestT) {
                bestT = t;
                hitAny = true;
            }
        }
    }

    if (hitAny) {
        outHitPoint = ray.origin + ray.direction * bestT;
        return true;
    }
    return HomeLbmCubeTool::intersectHorizontalPlane(ray, 0.0f, outHitPoint);
}

bool HomeLbmSystem::raycastSnapshotColliders(const LbmRay3D& ray, FCT::Vec3<>& outHitPoint) const {
    const HomeLbmUiSnapshot& snap = currentUiSnapshot();
    float bestT = std::numeric_limits<float>::max();
    bool hitAny = false;

    for (const auto& prim : snap.sceneColliderPrimitives) {
        const FCT::Vec3<> pos(prim.posAndRadius.x, prim.posAndRadius.y, prim.posAndRadius.z);
        const float r = prim.posAndRadius.w;
        float t = 0.0f;
        if (prim.type == HomeLbmConstants::kPrimObstacleBox) {
            const FCT::Vec3<> ext(prim.extentsAndHeight.x, prim.extentsAndHeight.y, prim.extentsAndHeight.z);
            if (intersectRayAabb(ray, pos - ext, pos + ext, t) && t < bestT) {
                bestT = t;
                hitAny = true;
            }
        } else if (prim.type == HomeLbmConstants::kPrimObstacleSphere ||
                   prim.type == HomeLbmConstants::kPrimObstacleCapsuleY) {
            if (intersectRaySphere(ray, pos, r, t) && t < bestT) {
                bestT = t;
                hitAny = true;
            }
        }
    }

    if (hitAny) {
        outHitPoint = ray.origin + ray.direction * bestT;
        return true;
    }
    return HomeLbmCubeTool::intersectHorizontalPlane(ray, 0.0f, outHitPoint);
}

HomeLbmCameraView HomeLbmSystem::resolveActiveCameraView(
    const LbmDomainComponent& domain,
    float fallbackAspect) const {
    HomeLbmCameraView view{};
    view.aspectRatio = std::max(0.1f, fallbackAspect);

    bool foundCamera = false;
    if (g_engineGlobal.cameraSystem) {
        CameraComponent camComp{};
        if (g_engineGlobal.cameraSystem->getActiveCameraWorldTransform(
                view.position, view.forward, view.up, camComp)) {
            view.fovDegrees = std::clamp(camComp.fov, 15.0f, 120.0f);
            view.nearPlane = std::max(0.02f, camComp.nearPlane);
            view.farPlane = std::max(view.nearPlane + 10.0f, std::min(camComp.farPlane, 5000.0f));
            foundCamera = true;
        }
    }

    if (!foundCamera) {
        const FCT::Vec3<> center = (domain.minBounds + domain.maxBounds) * 0.5f;
        const FCT::Vec3<> ext = domain.maxBounds - domain.minBounds;
        const float diag = std::max(2.0f, ext.length());
        view.position = center + FCT::Vec3<>(0.0f, diag * 0.45f, -diag * 1.15f);
        FCT::Vec3<> fwd = center - view.position;
        if (fwd.length() > 1e-4f) {
            fwd.normalize();
        } else {
            fwd = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
        }
        view.forward = fwd;
        view.up = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
        view.fovDegrees = 50.0f;
    }

    for (entt::registry* reg : collectCandidateRegistries(m_dataManager)) {
        auto lightView = std::as_const(*reg).view<DirectionalLightComponent>();
        for (entt::entity e : lightView) {
            const auto& dl = lightView.get<DirectionalLightComponent>(e);
            if (dl.enabled && dl.direction.length() > 1e-4f) {
                FCT::Vec3<> dir = dl.direction;
                dir.normalize();
                view.sunDirection = dir;
                view.sunIntensity = std::clamp(dl.intensity, 0.2f, 4.0f);
                return view;
            }
        }
    }
    return view;
}

void HomeLbmSystem::collectSceneCollidersInRegistry(
    entt::registry* registry,
    entt::entity domainEntity,
    const FCT::Vec3<>& domMin,
    const FCT::Vec3<>& domMax,
    std::vector<LbmGpuPrimitive>& outPrimitives) const {
    if (!registry) {
        return;
    }

    const entt::registry& constReg = *registry;
    auto readBodyVel = [&](entt::entity e) -> FCT::Vec3<> {
        if (const auto* rb = constReg.try_get<RigidBodyComponent>(e)) {
            return rb->linearVelocity * 0.02f;
        }
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    };

    auto obsView = constReg.view<LbmObstacleComponent>();
    for (entt::entity e : obsView) {
        const auto& obs = obsView.get<LbmObstacleComponent>(e);
        if (!obs.enabled) {
            continue;
        }
        const FCT::Vec3<> pos = resolveEntityPosition(constReg, e) + obs.localOffset;
        const FCT::Vec3<> sc = resolveEntityAbsScale(constReg, e);
        const float r = obs.radius * std::max({sc.x, sc.y, sc.z});
        const FCT::Vec3<> ext(obs.halfExtents.x * sc.x, obs.halfExtents.y * sc.y, obs.halfExtents.z * sc.z);
        const FCT::Vec3<> boundExt = (obs.shape == LbmPrimitiveShape::Box) ? ext : FCT::Vec3<>(r, r + ext.y, r);
        if (!boxesOverlap(domMin, domMax, pos - boundExt, pos + boundExt)) {
            continue;
        }
        LbmGpuPrimitive prim{};
        prim.type = mapObstacleShape(obs.shape);
        prim.posAndRadius = FCT::Vec4(pos.x, pos.y, pos.z, r);
        prim.extentsAndHeight = FCT::Vec4(ext.x, ext.y, ext.z, ext.y);
        prim.velocityAndPad = FCT::Vec4(obs.linearVelocity.x, obs.linearVelocity.y, obs.linearVelocity.z, 0.0f);
        outPrimitives.push_back(prim);
    }

    auto emitView = constReg.view<LbmEmitterComponent>();
    for (entt::entity e : emitView) {
        const auto& em = emitView.get<LbmEmitterComponent>(e);
        if (!em.enabled) {
            continue;
        }
        const FCT::Vec3<> pos = resolveEntityPosition(constReg, e) + em.localOffset;
        const FCT::Vec3<> sc = resolveEntityAbsScale(constReg, e);
        const float r = em.radius * std::max({sc.x, sc.y, sc.z});
        const FCT::Vec3<> ext(em.halfExtents.x * sc.x, em.halfExtents.y * sc.y, em.halfExtents.z * sc.z);
        const FCT::Vec3<> boundExt = (em.shape == LbmPrimitiveShape::Box) ? ext : FCT::Vec3<>(r, r, r);
        if (!boxesOverlap(domMin, domMax, pos - boundExt, pos + boundExt)) {
            continue;
        }
        LbmGpuPrimitive prim{};
        prim.type = mapEmitterShape(em.shape);
        prim.injectLiquid = em.injectLiquid ? 1 : 0;
        prim.smokeDensity = em.smokeDensity;
        prim.temperature = em.temperature;
        prim.posAndRadius = FCT::Vec4(pos.x, pos.y, pos.z, r);
        prim.extentsAndHeight = FCT::Vec4(ext.x, ext.y, ext.z, 0.0f);
        prim.velocityAndPad = FCT::Vec4(em.injectVelocity.x, em.injectVelocity.y, em.injectVelocity.z, 0.0f);
        outPrimitives.push_back(prim);
    }

    auto boxView = constReg.view<BoxColliderComponent>();
    for (entt::entity e : boxView) {
        if (e == domainEntity || constReg.all_of<LbmObstacleComponent>(e)) {
            continue;
        }
        const auto& col = boxView.get<BoxColliderComponent>(e);
        if (col.isTrigger) {
            continue;
        }
        const FCT::Vec3<> pos = resolveEntityPosition(constReg, e) + col.center;
        const FCT::Vec3<> sc = resolveEntityAbsScale(constReg, e);
        const FCT::Vec3<> ext(col.halfExtents.x * sc.x, col.halfExtents.y * sc.y, col.halfExtents.z * sc.z);
        if (!boxesOverlap(domMin, domMax, pos - ext, pos + ext)) {
            continue;
        }
        const FCT::Vec3<> vel = readBodyVel(e);
        LbmGpuPrimitive prim{};
        prim.type = HomeLbmConstants::kPrimObstacleBox;
        prim.posAndRadius = FCT::Vec4(pos.x, pos.y, pos.z, ext.length());
        prim.extentsAndHeight = FCT::Vec4(ext.x, ext.y, ext.z, 0.0f);
        prim.velocityAndPad = FCT::Vec4(vel.x, vel.y, vel.z, 0.0f);
        outPrimitives.push_back(prim);
    }

    auto sphereView = constReg.view<SphereColliderComponent>();
    for (entt::entity e : sphereView) {
        if (e == domainEntity || constReg.all_of<LbmObstacleComponent>(e)) {
            continue;
        }
        const auto& col = sphereView.get<SphereColliderComponent>(e);
        if (col.isTrigger) {
            continue;
        }
        const FCT::Vec3<> pos = resolveEntityPosition(constReg, e) + col.center;
        const FCT::Vec3<> sc = resolveEntityAbsScale(constReg, e);
        const float r = col.radius * std::max({sc.x, sc.y, sc.z});
        const FCT::Vec3<> rVec(r, r, r);
        if (!boxesOverlap(domMin, domMax, pos - rVec, pos + rVec)) {
            continue;
        }
        const FCT::Vec3<> vel = readBodyVel(e);
        LbmGpuPrimitive prim{};
        prim.type = HomeLbmConstants::kPrimObstacleSphere;
        prim.posAndRadius = FCT::Vec4(pos.x, pos.y, pos.z, r);
        prim.velocityAndPad = FCT::Vec4(vel.x, vel.y, vel.z, 0.0f);
        outPrimitives.push_back(prim);
    }

    auto capView = constReg.view<CapsuleColliderComponent>();
    for (entt::entity e : capView) {
        if (e == domainEntity || constReg.all_of<LbmObstacleComponent>(e)) {
            continue;
        }
        const auto& col = capView.get<CapsuleColliderComponent>(e);
        if (col.isTrigger) {
            continue;
        }
        const FCT::Vec3<> pos = resolveEntityPosition(constReg, e) + col.center;
        const FCT::Vec3<> sc = resolveEntityAbsScale(constReg, e);
        const float r = col.radius * std::max(sc.x, sc.z);
        const float hh = col.halfHeight * sc.y;
        const FCT::Vec3<> bExt(r, hh + r, r);
        if (!boxesOverlap(domMin, domMax, pos - bExt, pos + bExt)) {
            continue;
        }
        const FCT::Vec3<> vel = readBodyVel(e);
        LbmGpuPrimitive prim{};
        prim.type = HomeLbmConstants::kPrimObstacleCapsuleY;
        prim.posAndRadius = FCT::Vec4(pos.x, pos.y, pos.z, r);
        prim.extentsAndHeight = FCT::Vec4(r, hh, r, hh);
        prim.velocityAndPad = FCT::Vec4(vel.x, vel.y, vel.z, 0.0f);
        outPrimitives.push_back(prim);
    }
}

void HomeLbmSystem::gatherDomainPrimitives(
    const LbmDomainComponent& domain,
    entt::entity domainEntity,
    std::vector<LbmGpuPrimitive>& outPrimitives) const {
    outPrimitives.clear();
    HomeLbmSolver::buildBuiltinPrimitives(domain, outPrimitives);

    if (domain.simMode == LbmSimulationMode::AirSmokeWindTunnel &&
        !domain.enableBuiltinObstacle &&
        domain.buoyancyAlpha > 0.0025f) {
        const FCT::Vec3<> span = domain.maxBounds - domain.minBounds;
        const float minSpan = std::max(0.25f, std::min({span.x, span.y, span.z}));
        const FCT::Vec3<> plumePos(
            domain.minBounds.x + 0.5f * span.x,
            domain.minBounds.y + 0.12f * span.y,
            domain.minBounds.z + 0.5f * span.z);
        LbmGpuPrimitive plume{};
        plume.type = HomeLbmConstants::kPrimEmitterSphere;
        plume.smokeDensity = 1.0f;
        plume.temperature = 1.0f;
        plume.posAndRadius = FCT::Vec4(plumePos.x, plumePos.y, plumePos.z, minSpan * 0.11f);
        plume.velocityAndPad = FCT::Vec4(
            domain.inflowVelocity.x,
            std::max(0.035f, domain.inflowVelocity.y),
            domain.inflowVelocity.z,
            0.0f);
        outPrimitives.push_back(plume);
    }

    if (!domain.autoVoxelizeScene) {
        return;
    }
    for (entt::registry* reg : collectCandidateRegistries(m_dataManager)) {
        collectSceneCollidersInRegistry(
            reg, domainEntity, domain.minBounds, domain.maxBounds, outPrimitives);
    }
}

void HomeLbmSystem::updateLogic() {
    consumePendingUiCommands();

    entt::registry* reg = nullptr;
    entt::entity entity = entt::null;
    if (!findPrimaryDomainEntity(reg, entity, false) || !reg) {
        m_renderer.setActiveForFrame(false);
        publishUiSnapshot(nullptr, entt::null);
        return;
    }

    auto& domain = reg->get<LbmDomainComponent>(entity);
    if (!domain.enabled) {
        m_renderer.setActiveForFrame(false);
        publishUiSnapshot(reg, entity);
        return;
    }

    const auto boundsChanged = [&]() -> bool {
        if (!m_hasTrackedBounds) {
            return false;
        }
        const FCT::Vec3<> dMin = domain.minBounds - m_lastMinBounds;
        const FCT::Vec3<> dMax = domain.maxBounds - m_lastMaxBounds;
        return (dMin.length() > 1e-3f) || (dMax.length() > 1e-3f);
    };

    if (boundsChanged() && !m_cubeTool.isDraggingFace()) {
        domain.resetRequested = true;
    }
    m_lastMinBounds = domain.minBounds;
    m_lastMaxBounds = domain.maxBounds;
    m_hasTrackedBounds = true;

    gatherDomainPrimitives(domain, entity, m_framePrimitives);
    m_renderPrimitives = m_framePrimitives;

    if (!m_solver.isInitialized()) {
        Status st = m_solver.initialize(domain, m_framePrimitives, m_rhiContext);
        if (!st.ok()) {
            m_lastStatusMessage = StrCat("求解器初始化失败: ", st.message());
            m_renderer.setActiveForFrame(false);
            publishUiSnapshot(reg, entity);
            return;
        }
        domain.resetRequested = false;
        m_lastStatusMessage = StrCat(
            "运行中 — ",
            m_solver.backend().getActiveBackendDisplayName());
    } else if (m_solver.backend().activeMode() != domain.backendMode &&
               m_solver.backend().isBackendAvailable(domain.backendMode)) {
        Status swSt = m_solver.switchBackend(domain.backendMode, m_rhiContext);
        if (!swSt.ok()) {
            m_lastStatusMessage = StrCat("后端切换回退: ", swSt.message());
            domain.backendMode = m_solver.backend().activeMode();
        }
    }

    if (domain.resetRequested) {
        Status rst = m_solver.resetSimulation(domain, m_framePrimitives);
        domain.resetRequested = false;
        if (!rst.ok()) {
            m_lastStatusMessage = StrCat("重置模拟域失败: ", rst.message());
            publishUiSnapshot(reg, entity);
            return;
        }
    }

    if (!domain.paused || domain.stepOnce) {
        const int32_t substeps = domain.stepOnce ? 1 : std::clamp(domain.substepsPerFrame, 1, 8);
        domain.stepOnce = false;
        for (int32_t s = 0; s < substeps; ++s) {
            Status stepSt = m_solver.step(domain, m_framePrimitives);
            if (!stepSt.ok()) {
                m_lastStatusMessage = StrCat("时间步推进失败: ", stepSt.message());
                break;
            }
        }
    }

    if (m_rhiContext && m_solver.isInitialized()) {
        uint32_t baseW = 1280u;
        uint32_t baseH = 720u;
        if (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->platform && Runtime::Engine::getInstance()->platform->getWindow()) {
            baseW = static_cast<uint32_t>(std::max(64, Runtime::Engine::getInstance()->platform->getWindow()->getWidth()));
            baseH = static_cast<uint32_t>(std::max(64, Runtime::Engine::getInstance()->platform->getWindow()->getHeight()));
        }

        const float scale = std::clamp(domain.renderResolutionScale, 0.25f, 1.0f);
        const uint32_t targetW = std::max(64u, static_cast<uint32_t>(static_cast<float>(baseW) * scale));
        const uint32_t targetH = std::max(64u, static_cast<uint32_t>(static_cast<float>(baseH) * scale));
        const float aspect = static_cast<float>(baseW) / static_cast<float>(std::max(1u, baseH));

        const HomeLbmCameraView camView = resolveActiveCameraView(domain, aspect);
        Status renderSt = m_renderer.dispatchVolumeRender(
            m_solver, domain, m_renderPrimitives, camView, targetW, targetH);
        if (!renderSt.ok()) {
            m_lastStatusMessage = StrCat("体积渲染合成失败: ", renderSt.message());
        }
    }

    publishUiSnapshot(reg, entity);
}

void HomeLbmSystem::updateRender() {
    // Note: 流体步进与体积光线步进均在 updateLogic 逻辑线程完成，RHI 纹理同步在 Context::flush() 的 syncTickers 屏障执行，
    // 彻底避免跨线程争用求解器与 Vulkan 命令池，无需互斥锁。
}

} // namespace MQEngine
