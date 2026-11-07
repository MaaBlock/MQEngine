#ifndef MQENGINE_HOMELBM_SOLVER_H
#define MQENGINE_HOMELBM_SOLVER_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"
#include "HomeLbmComputeBackend.h"
#include "AmrBlockGrid.h"
#include "HomeLbmKernels.h"

namespace MQEngine {

/**
 * @brief 求解器实时监控指标与物理守恒性诊断结果
 */
struct HomeLbmTelemetry {
    int32_t stepCount = 0;
    int32_t activeBlocksL0 = 0;
    int32_t activeBlocksL1 = 0;
    int32_t activeBlocksL2 = 0;
    int32_t totalActiveBlocks = 0;
    int32_t totalActiveCells = 0;
    int32_t finestResolutionX = 0;
    int32_t finestResolutionY = 0;
    int32_t finestResolutionZ = 0;

    bool allFinite = true;
    float totalFluidMass = 0.0f;
    float initialLiquidMass = 0.0f;
    float totalLiquidMass = 0.0f;
    float liquidMassDriftPercent = 0.0f;
    FCT::Vec3<> liquidCenterOfMass = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    float maxSpeed = 0.0f;
    float maxVorticity = 0.0f;
};

/**
 * @brief 三维光线步进相机参数包
 */
struct HomeLbmCameraView {
    FCT::Vec3<> position = FCT::Vec3<>(0.0f, 3.0f, -8.0f);
    FCT::Vec3<> forward = FCT::Vec3<>(0.0f, -0.2f, 1.0f);
    FCT::Vec3<> up = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
    float fovDegrees = 60.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    FCT::Vec3<> sunDirection = FCT::Vec3<>(-0.45f, -0.80f, 0.40f);
    float sunIntensity = 1.4f;
};

/**
 * @brief 自适应块结构多级 HOME-LBM & HOME-FREE 核心求解器（支持跨硬件计算后端状态热迁移）
 */
class HOME_LBM_API HomeLbmSolver {
public:
    HomeLbmSolver() = default;
    ~HomeLbmSolver();

    HomeLbmSolver(const HomeLbmSolver&) = delete;
    HomeLbmSolver& operator=(const HomeLbmSolver&) = delete;

    Status initialize(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        FCT::Context* rhiContext);

    void shutdown();

    Status resetSimulation(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives);

    /**
     * @brief 在不丢失当前三维流场矩与自由液面状态的前提下热切换底层计算后端 (DX12 / CUDA / Vulkan)
     */
    Status switchBackend(
        LbmComputeBackendMode targetMode,
        FCT::Context* rhiContext);

    /**
     * @brief 推进一个完整 L0 宏时间步（内含 L1/L2 递归 2:1 时间子步与动态 AMR 重网格化）
     */
    Status step(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives);

    /**
     * @brief 调度 GPU 多级 AMR 体积/自由液面光线步进，直接写入目标图像
     */
    Status renderVolume(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        const HomeLbmCameraView& camera,
        const FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT>& outRadiance,
        const FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT>& outHitDist);

    /**
     * @brief 从 GPU 回读叶子层流场，精确计算总液体质量、质心、最大涡量与数值有限性
     */
    Status evaluateFieldDiagnostics(const LbmDomainComponent& domain);

    bool isInitialized() const noexcept {
        return m_initialized.load(std::memory_order_acquire) && m_backend.isValid() && m_buffers.valid();
    }
    HomeLbmComputeBackend& backend() noexcept { return m_backend; }
    const HomeLbmComputeBackend& backend() const noexcept { return m_backend; }
    const AmrBlockGrid& amrGrid() const noexcept { return m_amrGrid; }
    const HomeLbmTelemetry& telemetry() const noexcept { return m_telemetry; }

    static void buildBuiltinPrimitives(
        const LbmDomainComponent& domain,
        std::vector<LbmGpuPrimitive>& inoutPrimitives);

private:
    struct HostStateCheckpoint {
        std::vector<FCT::Vec4> m0;
        std::vector<FCT::Vec4> m1;
        std::vector<FCT::Vec4> m2;
        std::vector<FCT::Vec4> aux;
        std::vector<uint32_t> flags;
        std::vector<FCT::Vec4> prevM0;
        std::vector<FCT::Vec4> prevM1;
        std::vector<FCT::Vec4> prevM2;
        std::vector<FCT::Vec2> vortField;
    };

    bool requiresTopologyRebuild(const LbmDomainComponent& domain) const noexcept;
    void fillGpuParams(
        const LbmDomainComponent& domain,
        uint32_t primitiveCount,
        const HomeLbmCameraView* optCamera,
        uint32_t renderWidth,
        uint32_t renderHeight);
    void uploadParamsAndPrimitives(std::span<const LbmGpuPrimitive> primitives);
    void uploadAmrMetadata();
    void advanceAmrLevel(int32_t level, float coarseAlphaT, const LbmDomainComponent& domain);
    void performDynamicAmrRemesh(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives);
    void refreshTopologyTelemetry();
    void captureCheckpoint(HostStateCheckpoint& outCheckpoint);
    void restoreCheckpoint(const HostStateCheckpoint& checkpoint);

private:
    FCT::Context* m_rhiContext = nullptr;
    HomeLbmComputeBackend m_backend;
    AmrBlockGrid m_amrGrid;
    HomeLbmKernels m_kernels;
    HomeLbmGpuBuffers m_buffers;

    LbmGpuParams m_cachedParams{};
    HomeLbmTelemetry m_telemetry{};
    LbmSimulationMode m_lastSimMode = LbmSimulationMode::AirSmokeWindTunnel;
    std::atomic<bool> m_initialized{false};
    bool m_initialMassCaptured = false;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_SOLVER_H
