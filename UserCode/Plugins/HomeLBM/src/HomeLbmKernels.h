#ifndef MQENGINE_HOMELBM_KERNELS_H
#define MQENGINE_HOMELBM_KERNELS_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmGpuTypes.h"

namespace MQEngine {

/**
 * @brief 自适应多级块结构 HOME-LBM / HOME-FREE 的全部 GPU 缓冲区集合
 */
struct HomeLbmGpuBuffers {
    static constexpr uint32_t kMaxPrimitives = 128u;

    FCT::Compute::Buffer<LbmGpuParams> paramsBuf;
    FCT::Compute::Buffer<LbmGpuPrimitive> primitivesBuf;
    FCT::Compute::Buffer<LbmBlockMeta> blockMetaBuf;
    FCT::Compute::Buffer<int32_t> levelGridBuf;
    FCT::Compute::Buffer<int32_t> activeIdsBuf;

    // 8^3 宏块内部单元十阶矩、VOF 体积分数与辅助标量场 (size = maxBlocks * 512)
    FCT::Compute::Buffer<FCT::Vec4> m0;    // (rho, ux_stored, uy_stored, uz_stored)
    FCT::Compute::Buffer<FCT::Vec4> m1;    // (sxx, syy, szz, sxy)
    FCT::Compute::Buffer<FCT::Vec4> m2;    // (sxz, syz, phi, smoke)
    FCT::Compute::Buffer<FCT::Vec4> aux;   // (temperature, wallUx, wallUy, wallUz)
    FCT::Compute::Buffer<uint32_t> flags;  // 低 8 位: 单元类型，高 24 位: 状态迁移标记

    // 粗网格时间步前向快照，供细网格子步时间插值 (size = maxBlocks * 512)
    FCT::Compute::Buffer<FCT::Vec4> prevM0;
    FCT::Compute::Buffer<FCT::Vec4> prevM1;
    FCT::Compute::Buffer<FCT::Vec4> prevM2;

    // 10^3 带 1 层 Halo 护边的无分支拉取流缓冲 (size = maxBlocks * 1000)
    FCT::Compute::Buffer<FCT::Vec4> padM0;
    FCT::Compute::Buffer<FCT::Vec4> padM1;
    FCT::Compute::Buffer<FCT::Vec4> padM2;
    FCT::Compute::Buffer<FCT::Vec4> padAux;
    FCT::Compute::Buffer<uint32_t> padFlags;

    // HOME-FREE 自由液面状态迁移确定性盈亏质量分摊缓冲
    FCT::Compute::Buffer<float> excessShareBuf;    // size = maxBlocks * 512
    FCT::Compute::Buffer<float> padExcessShareBuf; // size = maxBlocks * 1000

    // 涡量 / Q 判据可视化场与每宏块 AMR 原子传感器统计值
    FCT::Compute::Buffer<FCT::Vec2> vortField;   // size = maxBlocks * 512: (|curl u|, Q)
    FCT::Compute::Buffer<uint32_t> blockSensors; // size = maxBlocks * 2: [2*b]=maxVort*1e6, [2*b+1]=hasLiqOrIntf

    Status allocate(
        FCT::Compute::ComputeDevice& device,
        uint32_t maxBlocks,
        uint32_t totalLevelGridEntries);

    void release() noexcept;
    bool valid() const noexcept;
};

/**
 * @brief 基于 FCT_Compute (LuisaCompute DSL) 编译与调度的三维自适应 D3Q27 HOME-LBM & HOME-FREE 核心算子集
 */
class HOME_LBM_API HomeLbmKernels {
public:
    HomeLbmKernels();
    ~HomeLbmKernels();

    HomeLbmKernels(const HomeLbmKernels&) = delete;
    HomeLbmKernels& operator=(const HomeLbmKernels&) = delete;

    /**
     * @brief 在给定计算设备上编译全部 LBM 演化、AMR 耦合、自由液面迁移与体积光线步进 Kernel
     */
    Status compileAll(FCT::Compute::ComputeDevice& device);

    /**
     * @brief 释放当前已编译的设备端 Shader 句柄
     */
    void release() noexcept;

    bool isCompiled() const noexcept;

    void enqueueInitDomain(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount) const;

    void enqueueVoxelizeAndInject(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount,
        int32_t currentLevel) const;

    void enqueueSnapshotMoments(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount) const;

    void enqueueProlongateBlocks(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t newlyRefinedCount) const;

    void enqueueFillPaddedHalos(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount,
        int32_t currentLevel,
        float temporalAlpha) const;

    void enqueueStreamAndCollide(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount,
        int32_t currentLevel) const;

    void enqueueFreeSurfaceTransitions(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount,
        int32_t currentLevel,
        float temporalAlpha) const;

    void enqueueCoalesceFineToCoarse(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t coarseActiveBlockCount,
        int32_t coarseLevel) const;

    void enqueueComputeVorticityAndSensors(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        uint32_t activeBlockCount,
        int32_t currentLevel) const;

    void enqueueRaymarchVolume(
        FCT::Compute::CommandBatch& batch,
        const HomeLbmGpuBuffers& bufs,
        const FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT>& outRadiance,
        const FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT>& outHitDist,
        uint32_t width,
        uint32_t height) const;

private:
    struct Impl;
    UniquePtr<Impl> m_impl;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_KERNELS_H
