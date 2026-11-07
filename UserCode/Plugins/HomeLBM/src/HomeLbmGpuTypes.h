#ifndef MQENGINE_HOMELBM_GPU_TYPES_H
#define MQENGINE_HOMELBM_GPU_TYPES_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief D3Q27 格子玻尔兹曼离散速度集与块结构 AMR 编译期常量
 */
namespace HomeLbmConstants {

inline constexpr int32_t kBlockEdge = 8;
inline constexpr int32_t kBlockCells = kBlockEdge * kBlockEdge * kBlockEdge; // 512
inline constexpr int32_t kPaddedEdge = kBlockEdge + 2;                       // 10
inline constexpr int32_t kPaddedCells = kPaddedEdge * kPaddedEdge * kPaddedEdge; // 1000
inline constexpr int32_t kMaxAmrLevels = 3;
inline constexpr int32_t kNumDirs = 27;

inline constexpr uint32_t kCellLiquid = 0u;
inline constexpr uint32_t kCellInterface = 1u;
inline constexpr uint32_t kCellGas = 2u;
inline constexpr uint32_t kCellSolid = 3u;

inline constexpr uint32_t kCellFlagSpawnInterface = 1u << 8;
inline constexpr uint32_t kCellFlagToLiquid = 1u << 9;
inline constexpr uint32_t kCellFlagToGas = 1u << 10;
inline constexpr uint32_t kCellTypeMask = 0xFFu;

inline constexpr int32_t kPrimObstacleSphere = 0;
inline constexpr int32_t kPrimObstacleBox = 1;
inline constexpr int32_t kPrimObstacleCapsuleY = 2;
inline constexpr int32_t kPrimEmitterSphere = 3;
inline constexpr int32_t kPrimEmitterBox = 4;

// D3Q27 离散速度分量:
// 0: (0,0,0)
// 1..6: 轴向 (+-1,0,0), (0,+-1,0), (0,0,+-1)
// 7..18: 面对角 (+-1,+-1,0), (+-1,0,+-1), (0,+-1,+-1)
// 19..26: 体对角 (+-1,+-1,+-1)
inline constexpr std::array<int32_t, kNumDirs> kCx = {
    0,
    1, -1,  0,  0,  0,  0,
    1, -1,  1, -1,  1, -1,  1, -1,  0,  0,  0,  0,
    1, -1,  1, -1,  1, -1,  1, -1
};

inline constexpr std::array<int32_t, kNumDirs> kCy = {
    0,
    0,  0,  1, -1,  0,  0,
    1, -1, -1,  1,  0,  0,  0,  0,  1, -1,  1, -1,
    1, -1,  1, -1, -1,  1, -1,  1
};

inline constexpr std::array<int32_t, kNumDirs> kCz = {
    0,
    0,  0,  0,  0,  1, -1,
    0,  0,  0,  0,  1, -1, -1,  1,  1, -1, -1,  1,
    1, -1, -1,  1,  1, -1, -1,  1
};

inline constexpr std::array<int32_t, kNumDirs> kOpposite = {
    0,
    2,  1,  4,  3,  6,  5,
    8,  7, 10,  9, 12, 11, 14, 13, 16, 15, 18, 17,
    20, 19, 22, 21, 24, 23, 26, 25
};

inline constexpr std::array<float, kNumDirs> kWeight = {
    8.0f / 27.0f,
    2.0f / 27.0f, 2.0f / 27.0f, 2.0f / 27.0f, 2.0f / 27.0f, 2.0f / 27.0f, 2.0f / 27.0f,
    1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f,
    1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f,
    1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f, 1.0f / 54.0f,
    1.0f / 216.0f, 1.0f / 216.0f, 1.0f / 216.0f, 1.0f / 216.0f,
    1.0f / 216.0f, 1.0f / 216.0f, 1.0f / 216.0f, 1.0f / 216.0f
};

consteval bool verifyD3Q27Quadrature() {
    double wSum = 0.0;
    double cx2Sum = 0.0;
    double hxxy2Sum = 0.0;
    double hxyz2Sum = 0.0;
    for (int i = 0; i < kNumDirs; ++i) {
        int opp = kOpposite[i];
        if (kCx[opp] != -kCx[i] || kCy[opp] != -kCy[i] || kCz[opp] != -kCz[i]) {
            return false;
        }
        double w = static_cast<double>(kWeight[i]);
        double cx = static_cast<double>(kCx[i]);
        double cy = static_cast<double>(kCy[i]);
        double cz = static_cast<double>(kCz[i]);
        wSum += w;
        cx2Sum += w * cx * cx;
        double hxxy = (cx * cx - 1.0 / 3.0) * cy;
        double hxyz = cx * cy * cz;
        hxxy2Sum += w * hxxy * hxxy;
        hxyz2Sum += w * hxyz * hxyz;
    }
    auto approx = [](double a, double b) {
        double d = a - b;
        if (d < 0.0) d = -d;
        return d < 1e-6;
    };
    return approx(wSum, 1.0) &&
           approx(cx2Sum, 1.0 / 3.0) &&
           approx(hxxy2Sum, 2.0 / 27.0) &&
           approx(hxyz2Sum, 1.0 / 27.0);
}

static_assert(verifyD3Q27Quadrature(), "D3Q27 Hermite 离散求积权重或反向索引表不满足三阶正交关系");

} // namespace HomeLbmConstants

/**
 * @brief GPU 端每个 8^3 宏块的拓扑元数据（对齐为平凡可拷贝 POD）
 */
struct LbmBlockMeta {
    int32_t level = 0;
    int32_t blockX = 0;
    int32_t blockY = 0;
    int32_t blockZ = 0;
    int32_t parentBlock = -1;
    int32_t childOctant = 0;
    int32_t active = 0;
    int32_t newlyRefined = 0;
};

/**
 * @brief GPU 端解析几何体（障碍物或流体注入源）描述符
 */
struct LbmGpuPrimitive {
    int32_t type = 0;
    int32_t injectLiquid = 0;
    float smokeDensity = 0.0f;
    float temperature = 0.0f;
    FCT::Vec4 posAndRadius = FCT::Vec4(0.0f, 0.0f, 0.0f, 0.5f);
    FCT::Vec4 extentsAndHeight = FCT::Vec4(0.5f, 0.5f, 0.5f, 0.0f);
    FCT::Vec4 velocityAndPad = FCT::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief GPU 端统一模拟与光线步进参数缓冲元素（单元素 Buffer，避免滑条调节触发 Shader 重编译）
 */
struct LbmGpuParams {
    FCT::Vec4 domainMinAndDx0 = FCT::Vec4(-4.0f, 0.0f, -2.0f, 0.125f);
    FCT::Vec4 domainMaxAndVisc0 = FCT::Vec4(4.0f, 4.0f, 2.0f, 0.0025f);
    FCT::Vec4 inflowVelAndSmag = FCT::Vec4(0.085f, 0.0f, 0.0f, 0.08f);
    FCT::Vec4 gravityAndBuoyancy = FCT::Vec4(0.0f, -0.0012f, 0.0f, 0.0020f);
    FCT::Vec4 liquidMinRatioAndDecay = FCT::Vec4(0.04f, 0.02f, 0.04f, 0.0005f);
    FCT::Vec4 liquidMaxRatioAndDensityScale = FCT::Vec4(0.36f, 0.72f, 0.96f, 2.8f);
    FCT::Vec4 refineParams = FCT::Vec4(0.018f, 0.006f, 1.8f, 0.0f);

    FCT::Vec4 camPosAndTanHalfFov = FCT::Vec4(0.0f, 3.0f, -8.0f, 0.57735f);
    FCT::Vec4 camForwardAndAspect = FCT::Vec4(0.0f, -0.2f, 1.0f, 1.3333f);
    FCT::Vec4 camRightAndNear = FCT::Vec4(1.0f, 0.0f, 0.0f, 0.1f);
    FCT::Vec4 camUpAndFar = FCT::Vec4(0.0f, 1.0f, 0.0f, 500.0f);
    FCT::Vec4 sunDirAndIntensity = FCT::Vec4(-0.45f, -0.80f, 0.40f, 1.4f);

    int32_t baseBlocksX = 8;
    int32_t baseBlocksY = 4;
    int32_t baseBlocksZ = 4;
    int32_t maxAmrLevels = 2;

    int32_t simMode = 0;
    int32_t visMode = 1;
    int32_t primitiveCount = 0;
    int32_t maxBlocks = 512;

    int32_t bcNegX = 2;
    int32_t bcPosX = 3;
    int32_t bcNegY = 1;
    int32_t bcPosY = 1;
    int32_t bcNegZ = 1;
    int32_t bcPosZ = 1;

    int32_t levelGridOffset0 = 0;
    int32_t levelGridOffset1 = 0;
    int32_t levelGridOffset2 = 0;
    int32_t renderWidth = 640;
    int32_t renderHeight = 360;
    int32_t stepIndex = 0;
};

static_assert(std::is_trivially_copyable_v<LbmBlockMeta>);
static_assert(std::is_trivially_copyable_v<LbmGpuPrimitive>);
static_assert(std::is_trivially_copyable_v<LbmGpuParams>);

} // namespace MQEngine

FCT_COMPUTE_BACKEND_REFLECT_STRUCT(
    MQEngine::LbmBlockMeta,
    level,
    blockX,
    blockY,
    blockZ,
    parentBlock,
    childOctant,
    active,
    newlyRefined);

FCT_COMPUTE_BACKEND_REFLECT_STRUCT(
    MQEngine::LbmGpuPrimitive,
    type,
    injectLiquid,
    smokeDensity,
    temperature,
    posAndRadius,
    extentsAndHeight,
    velocityAndPad);

FCT_COMPUTE_BACKEND_REFLECT_STRUCT(
    MQEngine::LbmGpuParams,
    domainMinAndDx0,
    domainMaxAndVisc0,
    inflowVelAndSmag,
    gravityAndBuoyancy,
    liquidMinRatioAndDecay,
    liquidMaxRatioAndDensityScale,
    refineParams,
    camPosAndTanHalfFov,
    camForwardAndAspect,
    camRightAndNear,
    camUpAndFar,
    sunDirAndIntensity,
    baseBlocksX,
    baseBlocksY,
    baseBlocksZ,
    maxAmrLevels,
    simMode,
    visMode,
    primitiveCount,
    maxBlocks,
    bcNegX,
    bcPosX,
    bcNegY,
    bcPosY,
    bcNegZ,
    bcPosZ,
    levelGridOffset0,
    levelGridOffset1,
    levelGridOffset2,
    renderWidth,
    renderHeight,
    stepIndex);

#endif // MQENGINE_HOMELBM_GPU_TYPES_H
