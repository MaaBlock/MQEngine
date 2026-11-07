#include "HomeLbmSolver.h"

namespace MQEngine {

HomeLbmSolver::~HomeLbmSolver() {
    shutdown();
}

void HomeLbmSolver::shutdown() {
    m_initialized.store(false, std::memory_order_release);
    if (m_backend.isValid()) {
        m_backend.stream().synchronize();
    }
    m_buffers.release();
    m_kernels.release();
    m_backend.shutdown();
}

void HomeLbmSolver::buildBuiltinPrimitives(
    const LbmDomainComponent& domain,
    std::vector<LbmGpuPrimitive>& inoutPrimitives) {
    if (!domain.enableBuiltinObstacle) {
        return;
    }

    const FCT::Vec3<> span = domain.maxBounds - domain.minBounds;
    const float minSpan = std::max(0.1f, std::min({ span.x, span.y, span.z }));

    LbmGpuPrimitive obs{};
    if (domain.builtinObstacleShape == LbmPrimitiveShape::Box) {
        obs.type = HomeLbmConstants::kPrimObstacleBox;
    } else if (domain.builtinObstacleShape == LbmPrimitiveShape::CapsuleY) {
        obs.type = HomeLbmConstants::kPrimObstacleCapsuleY;
    } else {
        obs.type = HomeLbmConstants::kPrimObstacleSphere;
    }

    const FCT::Vec3<> center(
        domain.minBounds.x + span.x * domain.builtinObstacleNormalizedPos.x,
        domain.minBounds.y + span.y * domain.builtinObstacleNormalizedPos.y,
        domain.minBounds.z + span.z * domain.builtinObstacleNormalizedPos.z);
    const float radius = std::max(0.05f, minSpan * domain.builtinObstacleRadiusRatio);

    obs.posAndRadius = FCT::Vec4(center.x, center.y, center.z, radius);
    obs.extentsAndHeight = FCT::Vec4(radius, radius * 1.15f, radius, radius * 0.85f);
    obs.velocityAndPad = FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    inoutPrimitives.push_back(obs);

    // 气烟风洞模式下若场景未挂载额外发射源且处于烟雾/热羽流可视化，自动在入流上游生成示踪烟雾注入核
    if (domain.simMode == LbmSimulationMode::AirSmokeWindTunnel && inoutPrimitives.size() == 1u) {
        LbmGpuPrimitive tracer{};
        tracer.type = HomeLbmConstants::kPrimEmitterSphere;
        const FCT::Vec3<> emitPos(
            domain.minBounds.x + span.x * std::max(0.06f, domain.builtinObstacleNormalizedPos.x * 0.42f),
            center.y,
            center.z);
        tracer.posAndRadius = FCT::Vec4(emitPos.x, emitPos.y, emitPos.z, radius * 0.90f);
        tracer.smokeDensity = 1.0f;
        tracer.temperature = 0.25f;
        tracer.velocityAndPad = FCT::Vec4(
            domain.inflowVelocity.x,
            domain.inflowVelocity.y,
            domain.inflowVelocity.z,
            0.0f);
        inoutPrimitives.push_back(tracer);
    }
}

Status HomeLbmSolver::initialize(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    FCT::Context* rhiContext) {
    m_rhiContext = rhiContext;

    LbmComputeBackendMode chosenMode = domain.backendMode;
    if (!m_backend.isBackendAvailable(chosenMode)) {
        chosenMode = LbmComputeBackendMode::MatchRHI_DX12;
    }

    CHECK_STATUS(m_backend.initialize(chosenMode, m_rhiContext));
    CHECK_STATUS(m_kernels.compileAll(m_backend.device()));

    CHECK_STATUS(resetSimulation(domain, primitives));
    m_initialized.store(true, std::memory_order_release);
    return OkStatus();
}

bool HomeLbmSolver::requiresTopologyRebuild(const LbmDomainComponent& domain) const noexcept {
    const int32_t clampedBx = std::clamp(domain.baseBlocksX, 1, 32);
    const int32_t clampedBy = std::clamp(domain.baseBlocksY, 1, 32);
    const int32_t clampedBz = std::clamp(domain.baseBlocksZ, 1, 32);
    const int32_t clampedLv = std::clamp(domain.maxAmrLevels, 1, HomeLbmConstants::kMaxAmrLevels);

    return !m_buffers.valid() ||
           m_amrGrid.baseBlocksX() != clampedBx ||
           m_amrGrid.baseBlocksY() != clampedBy ||
           m_amrGrid.baseBlocksZ() != clampedBz ||
           m_amrGrid.maxLevels() != clampedLv;
}

Status HomeLbmSolver::resetSimulation(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives) {
    if (!m_backend.isValid() || !m_kernels.isCompiled()) {
        return FailedPreconditionError("HomeLbmSolver 尚未完成后端与 Kernel 初始化");
    }

    CHECK_STATUS(m_amrGrid.initialize(domain, primitives));
    CHECK_STATUS(m_buffers.allocate(
        m_backend.device(),
        static_cast<uint32_t>(m_amrGrid.maxBlocks()),
        static_cast<uint32_t>(m_amrGrid.levelBlockGrid().size())));

    m_telemetry = HomeLbmTelemetry{};
    m_lastSimMode = domain.simMode;
    m_initialMassCaptured = false;

    fillGpuParams(domain, static_cast<uint32_t>(primitives.size()), nullptr, 640u, 360u);
    uploadParamsAndPrimitives(primitives);
    uploadAmrMetadata();

    // 自底向上在所有活跃层级初始化流场，随后自顶向下执行限制算子保证粗细覆盖区严格一致
    for (int32_t lv = 0; lv < m_amrGrid.maxLevels(); ++lv) {
        auto active = m_amrGrid.activeBlocksAtLevel(lv);
        if (active.empty()) {
            continue;
        }
        FCT::Compute::CommandBatch batch;
        batch.add(m_buffers.activeIdsBuf.copyFromHost(active));
        m_kernels.enqueueInitDomain(batch, m_buffers, static_cast<uint32_t>(active.size()));
        m_backend.stream().submitBatch(std::move(batch));
    }

    for (int32_t lv = m_amrGrid.maxLevels() - 2; lv >= 0; --lv) {
        auto coarseActive = m_amrGrid.activeBlocksAtLevel(lv);
        if (coarseActive.empty()) {
            continue;
        }
        FCT::Compute::CommandBatch batch;
        batch.add(m_buffers.activeIdsBuf.copyFromHost(coarseActive));
        m_kernels.enqueueCoalesceFineToCoarse(batch, m_buffers, static_cast<uint32_t>(coarseActive.size()), lv);
        m_backend.stream().submitBatch(std::move(batch));
    }

    m_backend.stream().synchronize();
    refreshTopologyTelemetry();

    if (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
        CHECK_STATUS(evaluateFieldDiagnostics(domain));
    }
    return OkStatus();
}

void HomeLbmSolver::captureCheckpoint(HostStateCheckpoint& outCheckpoint) {
    const size_t cellTotal = static_cast<size_t>(m_amrGrid.maxBlocks()) * HomeLbmConstants::kBlockCells;
    outCheckpoint.m0.resize(cellTotal);
    outCheckpoint.m1.resize(cellTotal);
    outCheckpoint.m2.resize(cellTotal);
    outCheckpoint.aux.resize(cellTotal);
    outCheckpoint.flags.resize(cellTotal);
    outCheckpoint.prevM0.resize(cellTotal);
    outCheckpoint.prevM1.resize(cellTotal);
    outCheckpoint.prevM2.resize(cellTotal);
    outCheckpoint.vortField.resize(cellTotal);

    m_backend.stream().submitAndSync(
        m_buffers.m0.copyToHost(outCheckpoint.m0),
        m_buffers.m1.copyToHost(outCheckpoint.m1),
        m_buffers.m2.copyToHost(outCheckpoint.m2),
        m_buffers.aux.copyToHost(outCheckpoint.aux),
        m_buffers.flags.copyToHost(outCheckpoint.flags),
        m_buffers.prevM0.copyToHost(outCheckpoint.prevM0),
        m_buffers.prevM1.copyToHost(outCheckpoint.prevM1),
        m_buffers.prevM2.copyToHost(outCheckpoint.prevM2),
        m_buffers.vortField.copyToHost(outCheckpoint.vortField));
}

void HomeLbmSolver::restoreCheckpoint(const HostStateCheckpoint& checkpoint) {
    m_backend.stream().submitAndSync(
        m_buffers.m0.copyFromHost(checkpoint.m0),
        m_buffers.m1.copyFromHost(checkpoint.m1),
        m_buffers.m2.copyFromHost(checkpoint.m2),
        m_buffers.aux.copyFromHost(checkpoint.aux),
        m_buffers.flags.copyFromHost(checkpoint.flags),
        m_buffers.prevM0.copyFromHost(checkpoint.prevM0),
        m_buffers.prevM1.copyFromHost(checkpoint.prevM1),
        m_buffers.prevM2.copyFromHost(checkpoint.prevM2),
        m_buffers.vortField.copyFromHost(checkpoint.vortField));
}

Status HomeLbmSolver::switchBackend(
    LbmComputeBackendMode targetMode,
    FCT::Context* rhiContext) {
    m_rhiContext = rhiContext;
    if (m_initialized.load(std::memory_order_acquire) && m_backend.isValid() && m_backend.activeMode() == targetMode) {
        return OkStatus();
    }
    if (!m_backend.isBackendAvailable(targetMode)) {
        return UnavailableError(StrCat("目标计算后端不可用: ", HomeLbmComputeBackend::toBackendIdent(targetMode)));
    }

    const bool hadValidState = m_initialized.load(std::memory_order_acquire) && m_backend.isValid() && m_buffers.valid();
    m_initialized.store(false, std::memory_order_release);
    HostStateCheckpoint checkpoint{};
    if (hadValidState) {
        captureCheckpoint(checkpoint);
    }

    m_buffers.release();
    m_kernels.release();
    CHECK_STATUS(m_backend.initialize(targetMode, m_rhiContext));
    CHECK_STATUS(m_kernels.compileAll(m_backend.device()));

    if (hadValidState) {
        CHECK_STATUS(m_buffers.allocate(
            m_backend.device(),
            static_cast<uint32_t>(m_amrGrid.maxBlocks()),
            static_cast<uint32_t>(m_amrGrid.levelBlockGrid().size())));
        uploadAmrMetadata();
        m_backend.stream().submitAndSync(m_buffers.paramsBuf.copyFromHost(&m_cachedParams));
        restoreCheckpoint(checkpoint);
    }

    m_initialized.store(true, std::memory_order_release);
    return OkStatus();
}

void HomeLbmSolver::fillGpuParams(
    const LbmDomainComponent& domain,
    uint32_t primitiveCount,
    const HomeLbmCameraView* optCamera,
    uint32_t renderWidth,
    uint32_t renderHeight) {
    LbmGpuParams& p = m_cachedParams;
    p.domainMinAndDx0 = FCT::Vec4(
        domain.minBounds.x,
        domain.minBounds.y,
        domain.minBounds.z,
        m_amrGrid.levelCellSize(0));
    p.domainMaxAndVisc0 = FCT::Vec4(
        domain.maxBounds.x,
        domain.maxBounds.y,
        domain.maxBounds.z,
        std::clamp(domain.kinematicViscosity, 0.0001f, 0.25f));
    p.inflowVelAndSmag = FCT::Vec4(
        domain.inflowVelocity.x,
        domain.inflowVelocity.y,
        domain.inflowVelocity.z,
        std::clamp(domain.smagorinskyCs, 0.0f, 0.30f));
    p.gravityAndBuoyancy = FCT::Vec4(
        domain.gravity.x,
        domain.gravity.y,
        domain.gravity.z,
        domain.buoyancyAlpha);
    p.liquidMinRatioAndDecay = FCT::Vec4(
        domain.initialLiquidMinRatio.x,
        domain.initialLiquidMinRatio.y,
        domain.initialLiquidMinRatio.z,
        std::clamp(domain.ambientSmokeDecay, 0.0f, 0.1f));
    p.liquidMaxRatioAndDensityScale = FCT::Vec4(
        domain.initialLiquidMaxRatio.x,
        domain.initialLiquidMaxRatio.y,
        domain.initialLiquidMaxRatio.z,
        std::max(0.1f, domain.densityScale));
    p.refineParams = FCT::Vec4(
        domain.vorticityRefineThreshold,
        domain.vorticityCoarsenThreshold,
        domain.obstacleRefineDistance,
        0.0f);

    if (optCamera) {
        constexpr float kDegToRad = 3.14159265359f / 180.0f;
        const float tanHalfFov = std::tan(std::clamp(optCamera->fovDegrees, 15.0f, 120.0f) * 0.5f * kDegToRad);
        const FCT::Vec3<> fwd = FCT::normalize(optCamera->forward);
        FCT::Vec3<> right = FCT::cross(fwd, optCamera->up);
        if (right.length() < 1e-5f) {
            right = FCT::cross(fwd, FCT::Vec3<>(0.0f, 0.0f, 1.0f));
        }
        right = FCT::normalize(right);
        const FCT::Vec3<> trueUp = FCT::normalize(FCT::cross(right, fwd));

        p.camPosAndTanHalfFov = FCT::Vec4(optCamera->position.x, optCamera->position.y, optCamera->position.z, tanHalfFov);
        p.camForwardAndAspect = FCT::Vec4(fwd.x, fwd.y, fwd.z, optCamera->aspectRatio);
        p.camRightAndNear = FCT::Vec4(right.x, right.y, right.z, std::max(0.01f, optCamera->nearPlane));
        p.camUpAndFar = FCT::Vec4(trueUp.x, trueUp.y, trueUp.z, std::max(1.0f, optCamera->farPlane));
        p.sunDirAndIntensity = FCT::Vec4(
            optCamera->sunDirection.x,
            optCamera->sunDirection.y,
            optCamera->sunDirection.z,
            optCamera->sunIntensity);
    }

    p.baseBlocksX = m_amrGrid.baseBlocksX();
    p.baseBlocksY = m_amrGrid.baseBlocksY();
    p.baseBlocksZ = m_amrGrid.baseBlocksZ();
    p.maxAmrLevels = m_amrGrid.maxLevels();

    p.simMode = static_cast<int32_t>(domain.simMode);
    p.visMode = static_cast<int32_t>(domain.visMode);
    p.primitiveCount = static_cast<int32_t>(std::min<uint32_t>(primitiveCount, HomeLbmGpuBuffers::kMaxPrimitives));
    p.maxBlocks = m_amrGrid.maxBlocks();

    p.bcNegX = static_cast<int32_t>(domain.negXBoundary);
    p.bcPosX = static_cast<int32_t>(domain.posXBoundary);
    p.bcNegY = static_cast<int32_t>(domain.negYBoundary);
    p.bcPosY = static_cast<int32_t>(domain.posYBoundary);
    p.bcNegZ = static_cast<int32_t>(domain.negZBoundary);
    p.bcPosZ = static_cast<int32_t>(domain.posZBoundary);

    p.levelGridOffset0 = m_amrGrid.levelGridOffset(0);
    p.levelGridOffset1 = m_amrGrid.levelGridOffset(1);
    p.levelGridOffset2 = m_amrGrid.levelGridOffset(2);
    p.renderWidth = static_cast<int32_t>(renderWidth);
    p.renderHeight = static_cast<int32_t>(renderHeight);
    p.stepIndex = m_telemetry.stepCount;
}

void HomeLbmSolver::uploadParamsAndPrimitives(std::span<const LbmGpuPrimitive> primitives) {
    FCT::Compute::CommandBatch batch;
    batch.add(m_buffers.paramsBuf.copyFromHost(&m_cachedParams));
    if (!primitives.empty()) {
        const size_t clampedCount = std::min<size_t>(primitives.size(), HomeLbmGpuBuffers::kMaxPrimitives);
        batch.add(m_buffers.primitivesBuf.copyFromHost(primitives.subspan(0, clampedCount)));
    }
    m_backend.stream().submitBatch(std::move(batch));
}

void HomeLbmSolver::uploadAmrMetadata() {
    m_backend.stream().submit(
        m_buffers.blockMetaBuf.copyFromHost(m_amrGrid.blockMetas()),
        m_buffers.levelGridBuf.copyFromHost(m_amrGrid.levelBlockGrid()));
}

void HomeLbmSolver::advanceAmrLevel(
    int32_t level,
    float coarseAlphaT,
    const LbmDomainComponent& domain) {
    auto activeIds = m_amrGrid.activeBlocksAtLevel(level);
    if (activeIds.empty()) {
        return;
    }

    const uint32_t activeCount = static_cast<uint32_t>(activeIds.size());
    const bool hasFinerLevel = (level + 1 < m_amrGrid.maxLevels()) &&
                               (!m_amrGrid.activeBlocksAtLevel(level + 1).empty());

    {
        FCT::Compute::CommandBatch batch;
        batch.add(m_buffers.activeIdsBuf.copyFromHost(activeIds));
        m_kernels.enqueueVoxelizeAndInject(batch, m_buffers, activeCount, level);
        if (hasFinerLevel) {
            m_kernels.enqueueSnapshotMoments(batch, m_buffers, activeCount);
        }
        m_kernels.enqueueFillPaddedHalos(batch, m_buffers, activeCount, level, coarseAlphaT);
        m_kernels.enqueueStreamAndCollide(batch, m_buffers, activeCount, level);
        if (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
            m_kernels.enqueueFreeSurfaceTransitions(batch, m_buffers, activeCount, level, coarseAlphaT);
        }
        m_kernels.enqueueFillPaddedHalos(batch, m_buffers, activeCount, level, coarseAlphaT);
        m_kernels.enqueueComputeVorticityAndSensors(batch, m_buffers, activeCount, level);
        m_backend.stream().submitBatch(std::move(batch));
    }

    // 声学/对流时间缩放 2:1 递归子步演化
    if (hasFinerLevel) {
        advanceAmrLevel(level + 1, 0.0f, domain);
        advanceAmrLevel(level + 1, 0.5f, domain);

        FCT::Compute::CommandBatch restrictBatch;
        restrictBatch.add(m_buffers.activeIdsBuf.copyFromHost(activeIds));
        m_kernels.enqueueCoalesceFineToCoarse(restrictBatch, m_buffers, activeCount, level);
        m_backend.stream().submitBatch(std::move(restrictBatch));
    }
}

void HomeLbmSolver::performDynamicAmrRemesh(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives) {
    if (m_amrGrid.maxLevels() <= 1) {
        return;
    }

    std::vector<uint32_t> rawSensors(static_cast<size_t>(m_amrGrid.maxBlocks()) * 2u, 0u);
    m_backend.stream().submitAndSync(m_buffers.blockSensors.copyToHost(rawSensors));

    std::vector<AmrBlockSensorSample> samples(static_cast<size_t>(m_amrGrid.maxBlocks()));
    float peakVort = 0.0f;
    for (int32_t i = 0; i < m_amrGrid.maxBlocks(); ++i) {
        samples[i].maxVorticity = static_cast<float>(rawSensors[i * 2]) * 1e-6f;
        samples[i].hasFreeSurfaceInterface = rawSensors[i * 2 + 1];
        if (m_amrGrid.blockMetas()[i].active != 0) {
            peakVort = std::max(peakVort, samples[i].maxVorticity);
        }
    }
    m_telemetry.maxVorticity = peakVort;

    if (m_amrGrid.updateDynamicRefinement(domain, primitives, samples)) {
        uploadAmrMetadata();
        auto newBlocks = m_amrGrid.newlyRefinedBlocks();
        if (!newBlocks.empty()) {
            FCT::Compute::CommandBatch prolongBatch;
            prolongBatch.add(m_buffers.activeIdsBuf.copyFromHost(newBlocks));
            m_kernels.enqueueProlongateBlocks(
                prolongBatch,
                m_buffers,
                static_cast<uint32_t>(newBlocks.size()));
            m_backend.stream().submitBatch(std::move(prolongBatch));
            m_backend.stream().synchronize();
        }
        m_amrGrid.clearNewlyRefinedFlags();
        refreshTopologyTelemetry();
    }
}

Status HomeLbmSolver::step(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives) {
    if (!isInitialized()) {
        return FailedPreconditionError("HomeLbmSolver 未初始化");
    }

    if (domain.backendMode != m_backend.activeMode() &&
        m_backend.isBackendAvailable(domain.backendMode)) {
        CHECK_STATUS(switchBackend(domain.backendMode, m_rhiContext));
    }

    if (requiresTopologyRebuild(domain) || domain.simMode != m_lastSimMode) {
        CHECK_STATUS(resetSimulation(domain, primitives));
    }

    fillGpuParams(
        domain,
        static_cast<uint32_t>(primitives.size()),
        nullptr,
        static_cast<uint32_t>(m_cachedParams.renderWidth),
        static_cast<uint32_t>(m_cachedParams.renderHeight));
    uploadParamsAndPrimitives(primitives);

    advanceAmrLevel(0, 0.0f, domain);
    ++m_telemetry.stepCount;

    const int32_t remeshInterval = std::max(1, domain.amrRemeshInterval);
    if ((m_telemetry.stepCount % remeshInterval) == 0) {
        performDynamicAmrRemesh(domain, primitives);
    } else {
        m_backend.stream().synchronize();
    }

    refreshTopologyTelemetry();
    return OkStatus();
}

Status HomeLbmSolver::renderVolume(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    const HomeLbmCameraView& camera,
    const FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT>& outRadiance,
    const FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT>& outHitDist) {
    if (!isInitialized() || !m_buffers.valid() || !outRadiance.valid() || !outHitDist.valid()) {
        return FailedPreconditionError("HomeLbmSolver::renderVolume 目标纹理或计算设备未就绪");
    }

    const uint32_t w = outRadiance.width();
    const uint32_t h = outRadiance.height();
    fillGpuParams(domain, static_cast<uint32_t>(primitives.size()), &camera, w, h);

    FCT::Compute::CommandBatch batch;
    batch.add(m_buffers.paramsBuf.copyFromHost(&m_cachedParams));
    if (!primitives.empty()) {
        const size_t clampedCount = std::min<size_t>(primitives.size(), HomeLbmGpuBuffers::kMaxPrimitives);
        batch.add(m_buffers.primitivesBuf.copyFromHost(primitives.subspan(0, clampedCount)));
    }
    m_kernels.enqueueRaymarchVolume(batch, m_buffers, outRadiance, outHitDist, w, h);
    m_backend.stream().submitBatch(std::move(batch));
    m_backend.stream().synchronize();
    return OkStatus();
}

void HomeLbmSolver::refreshTopologyTelemetry() {
    m_telemetry.activeBlocksL0 = m_amrGrid.activeBlockCount(0);
    m_telemetry.activeBlocksL1 = m_amrGrid.activeBlockCount(1);
    m_telemetry.activeBlocksL2 = m_amrGrid.activeBlockCount(2);
    m_telemetry.totalActiveBlocks = m_amrGrid.totalActiveBlockCount();
    m_telemetry.totalActiveCells = m_telemetry.totalActiveBlocks * HomeLbmConstants::kBlockCells;

    const int32_t finestLv = std::max(0, m_amrGrid.maxLevels() - 1);
    m_telemetry.finestResolutionX = m_amrGrid.levelBlocksX(finestLv) * HomeLbmConstants::kBlockEdge;
    m_telemetry.finestResolutionY = m_amrGrid.levelBlocksY(finestLv) * HomeLbmConstants::kBlockEdge;
    m_telemetry.finestResolutionZ = m_amrGrid.levelBlocksZ(finestLv) * HomeLbmConstants::kBlockEdge;
}

Status HomeLbmSolver::evaluateFieldDiagnostics(const LbmDomainComponent& domain) {
    if (!isInitialized() || !m_buffers.valid()) {
        return FailedPreconditionError("HomeLbmSolver 未就绪，无法读取流场诊断指标");
    }

    const size_t cellTotal = static_cast<size_t>(m_amrGrid.maxBlocks()) * HomeLbmConstants::kBlockCells;
    std::vector<FCT::Vec4> hostM0(cellTotal);
    std::vector<FCT::Vec4> hostM2(cellTotal);
    std::vector<FCT::Vec2> hostVort(cellTotal);
    std::vector<uint32_t> hostFlags(cellTotal);

    m_backend.stream().submitAndSync(
        m_buffers.m0.copyToHost(hostM0),
        m_buffers.m2.copyToHost(hostM2),
        m_buffers.vortField.copyToHost(hostVort),
        m_buffers.flags.copyToHost(hostFlags));

    bool finiteOk = true;
    double fluidMassSum = 0.0;
    double liquidMassSum = 0.0;
    double comX = 0.0;
    double comY = 0.0;
    double comZ = 0.0;
    float maxSpeed = 0.0f;
    float maxVort = 0.0f;

    const auto metas = m_amrGrid.blockMetas();
    for (int32_t lv = 0; lv < m_amrGrid.maxLevels(); ++lv) {
        const float dx = m_amrGrid.levelCellSize(lv);
        const double cellVol = static_cast<double>(dx) * static_cast<double>(dx) * static_cast<double>(dx);
        const float invScale = 1.0f / static_cast<float>(1 << lv);
        const FCT::Vec3<> halfAccel = domain.gravity * (0.5f * invScale);

        for (int32_t blkId : m_amrGrid.activeBlocksAtLevel(lv)) {
            const LbmBlockMeta& meta = metas[blkId];
            for (int32_t lz = 0; lz < HomeLbmConstants::kBlockEdge; ++lz) {
                for (int32_t ly = 0; ly < HomeLbmConstants::kBlockEdge; ++ly) {
                    for (int32_t lx = 0; lx < HomeLbmConstants::kBlockEdge; ++lx) {
                        const int32_t gx = (meta.blockX << 3) + lx;
                        const int32_t gy = (meta.blockY << 3) + ly;
                        const int32_t gz = (meta.blockZ << 3) + lz;

                        if (lv + 1 < m_amrGrid.maxLevels()) {
                            const int32_t childBlk = m_amrGrid.lookupBlockId(
                                lv + 1, (gx << 1) >> 3, (gy << 1) >> 3, (gz << 1) >> 3);
                            if (childBlk >= 0) {
                                continue;
                            }
                        }

                        const size_t idx = static_cast<size_t>(blkId * HomeLbmConstants::kBlockCells + ((lz << 3) + ly) * 8 + lx);
                        const FCT::Vec4& v0 = hostM0[idx];
                        const FCT::Vec4& v2 = hostM2[idx];
                        const FCT::Vec2& vt = hostVort[idx];
                        const uint32_t ct = hostFlags[idx] & HomeLbmConstants::kCellTypeMask;

                        if (!std::isfinite(v0.x) || !std::isfinite(v0.y) || !std::isfinite(v0.z) || !std::isfinite(v0.w) ||
                            !std::isfinite(v2.x) || !std::isfinite(v2.y) || !std::isfinite(v2.z) || !std::isfinite(v2.w) ||
                            !std::isfinite(vt.x) || !std::isfinite(vt.y)) {
                            finiteOk = false;
                            continue;
                        }

                        if (ct == HomeLbmConstants::kCellSolid) {
                            continue;
                        }

                        fluidMassSum += static_cast<double>(v0.x) * cellVol;

                        const FCT::Vec3<> uPhys(v0.y - halfAccel.x, v0.z - halfAccel.y, v0.w - halfAccel.z);
                        const float speed = uPhys.length();
                        if (ct != HomeLbmConstants::kCellGas || domain.simMode == LbmSimulationMode::AirSmokeWindTunnel) {
                            maxSpeed = std::max(maxSpeed, speed);
                            maxVort = std::max(maxVort, vt.x);
                        }

                        double cellLiqMass = 0.0;
                        if (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
                            if (ct == HomeLbmConstants::kCellLiquid) {
                                cellLiqMass = static_cast<double>(v0.x) * cellVol;
                            } else if (ct == HomeLbmConstants::kCellInterface) {
                                cellLiqMass = static_cast<double>(v0.x) * static_cast<double>(v2.z) * cellVol;
                            }
                        } else {
                            cellLiqMass = static_cast<double>(v0.x) * cellVol;
                        }

                        if (cellLiqMass > 0.0) {
                            const double wx = static_cast<double>(domain.minBounds.x) + (static_cast<double>(gx) + 0.5) * dx;
                            const double wy = static_cast<double>(domain.minBounds.y) + (static_cast<double>(gy) + 0.5) * dx;
                            const double wz = static_cast<double>(domain.minBounds.z) + (static_cast<double>(gz) + 0.5) * dx;
                            liquidMassSum += cellLiqMass;
                            comX += cellLiqMass * wx;
                            comY += cellLiqMass * wy;
                            comZ += cellLiqMass * wz;
                        }
                    }
                }
            }
        }
    }

    m_telemetry.allFinite = finiteOk;
    m_telemetry.totalFluidMass = static_cast<float>(fluidMassSum);
    m_telemetry.totalLiquidMass = static_cast<float>(liquidMassSum);
    m_telemetry.maxSpeed = maxSpeed;
    m_telemetry.maxVorticity = maxVort;

    if (liquidMassSum > 1e-9) {
        m_telemetry.liquidCenterOfMass = FCT::Vec3<>(
            static_cast<float>(comX / liquidMassSum),
            static_cast<float>(comY / liquidMassSum),
            static_cast<float>(comZ / liquidMassSum));
    }

    if (!m_initialMassCaptured && liquidMassSum > 1e-9) {
        m_telemetry.initialLiquidMass = m_telemetry.totalLiquidMass;
        m_initialMassCaptured = true;
    }
    if (m_telemetry.initialLiquidMass > 1e-9f) {
        m_telemetry.liquidMassDriftPercent =
            std::abs(m_telemetry.totalLiquidMass - m_telemetry.initialLiquidMass) /
            m_telemetry.initialLiquidMass * 100.0f;
    } else {
        m_telemetry.liquidMassDriftPercent = 0.0f;
    }

    return OkStatus();
}

} // namespace MQEngine
