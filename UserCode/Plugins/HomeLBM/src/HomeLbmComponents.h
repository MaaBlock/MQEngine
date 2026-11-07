#ifndef MQENGINE_HOMELBM_COMPONENTS_H
#define MQENGINE_HOMELBM_COMPONENTS_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief 自适应 HOME-LBM 物理介质模拟模式
 */
enum class LbmSimulationMode : int32_t {
    AirSmokeWindTunnel = 0,
    FreeSurfaceLiquid = 1
};

/**
 * @brief LuisaCompute (FCT_Compute) 运行时硬件计算后端模式
 */
enum class LbmComputeBackendMode : int32_t {
    MatchRHI_DX12 = 0,
    CUDA = 1,
    Vulkan = 2
};

/**
 * @brief 三维流体场实时可视化通道
 */
enum class LbmVisualizationMode : int32_t {
    SmokeDensity = 0,
    VorticityMagnitude = 1,
    QCriterion = 2,
    VelocityMagnitude = 3,
    FreeSurfaceWater = 4
};

/**
 * @brief 模拟域六面边界条件类型
 */
enum class LbmBoundaryType : int32_t {
    NoSlipWall = 0,
    FreeSlipWall = 1,
    VelocityInlet = 2,
    PressureOutflow = 3
};

/**
 * @brief 流体源或障碍物几何基元形状
 */
enum class LbmPrimitiveShape : int32_t {
    Sphere = 0,
    Box = 1,
    CapsuleY = 2
};

/**
 * @brief 模拟域包围盒六个主轴面标识
 */
enum class LbmDomainFace : int32_t {
    NegX = 0,
    PosX = 1,
    NegY = 2,
    PosY = 3,
    NegZ = 4,
    PosZ = 5
};

/**
 * @brief 内置流体实验场景预设类型
 */
enum class LbmPresetScenario : int32_t {
    WindTunnelKarmanVortex = 0,
    FreeSurfaceDamBreak3D = 1,
    ThermalBuoyantPlume = 2
};

/**
 * @brief 自适应多级块结构 HOME-LBM 空间模拟域组件
 */
struct LbmDomainComponent {
    bool enabled = true;
    bool paused = false;
    bool stepOnce = false;
    bool resetRequested = true;
    bool autoVoxelizeScene = true;
    bool showDomainWireframe = true;
    bool showAmrBlocks = true;

    LbmSimulationMode simMode = LbmSimulationMode::AirSmokeWindTunnel;
    LbmVisualizationMode visMode = LbmVisualizationMode::VorticityMagnitude;
    LbmComputeBackendMode backendMode = LbmComputeBackendMode::MatchRHI_DX12;

    FCT::Vec3<> minBounds = FCT::Vec3<>(-4.0f, 0.0f, -2.0f);
    FCT::Vec3<> maxBounds = FCT::Vec3<>(4.0f, 4.0f, 2.0f);

    int32_t baseBlocksX = 8;
    int32_t baseBlocksY = 4;
    int32_t baseBlocksZ = 4;
    int32_t maxAmrLevels = 2;
    int32_t amrRemeshInterval = 8;
    int32_t substepsPerFrame = 2;

    float kinematicViscosity = 0.0025f;
    float smagorinskyCs = 0.08f;
    FCT::Vec3<> inflowVelocity = FCT::Vec3<>(0.085f, 0.0f, 0.0f);
    FCT::Vec3<> gravity = FCT::Vec3<>(0.0f, -0.0012f, 0.0f);
    float buoyancyAlpha = 0.0020f;
    float ambientSmokeDecay = 0.0005f;

    float vorticityRefineThreshold = 0.018f;
    float vorticityCoarsenThreshold = 0.006f;
    float obstacleRefineDistance = 1.8f;

    LbmBoundaryType negXBoundary = LbmBoundaryType::VelocityInlet;
    LbmBoundaryType posXBoundary = LbmBoundaryType::PressureOutflow;
    LbmBoundaryType negYBoundary = LbmBoundaryType::FreeSlipWall;
    LbmBoundaryType posYBoundary = LbmBoundaryType::FreeSlipWall;
    LbmBoundaryType negZBoundary = LbmBoundaryType::FreeSlipWall;
    LbmBoundaryType posZBoundary = LbmBoundaryType::FreeSlipWall;

    FCT::Vec3<> initialLiquidMinRatio = FCT::Vec3<>(0.04f, 0.02f, 0.04f);
    FCT::Vec3<> initialLiquidMaxRatio = FCT::Vec3<>(0.36f, 0.72f, 0.96f);

    bool enableBuiltinObstacle = true;
    LbmPrimitiveShape builtinObstacleShape = LbmPrimitiveShape::Sphere;
    FCT::Vec3<> builtinObstacleNormalizedPos = FCT::Vec3<>(0.24f, 0.50f, 0.50f);
    float builtinObstacleRadiusRatio = 0.11f;

    float densityScale = 2.8f;
    float renderResolutionScale = 0.5f;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /*version*/) {
        ar & enabled;
        ar & paused;
        ar & autoVoxelizeScene;
        ar & showDomainWireframe;
        ar & showAmrBlocks;
        ar & simMode;
        ar & visMode;
        ar & backendMode;
        ar & minBounds;
        ar & maxBounds;
        ar & baseBlocksX;
        ar & baseBlocksY;
        ar & baseBlocksZ;
        ar & maxAmrLevels;
        ar & amrRemeshInterval;
        ar & substepsPerFrame;
        ar & kinematicViscosity;
        ar & smagorinskyCs;
        ar & inflowVelocity;
        ar & gravity;
        ar & buoyancyAlpha;
        ar & ambientSmokeDecay;
        ar & vorticityRefineThreshold;
        ar & vorticityCoarsenThreshold;
        ar & obstacleRefineDistance;
        ar & negXBoundary;
        ar & posXBoundary;
        ar & negYBoundary;
        ar & posYBoundary;
        ar & negZBoundary;
        ar & posZBoundary;
        ar & initialLiquidMinRatio;
        ar & initialLiquidMaxRatio;
        ar & enableBuiltinObstacle;
        ar & builtinObstacleShape;
        ar & builtinObstacleNormalizedPos;
        ar & builtinObstacleRadiusRatio;
        ar & densityScale;
        ar & renderResolutionScale;
    }
};

/**
 * @brief 空间流体注入源组件（支持烟雾、热量、动量与液体注入）
 */
struct LbmEmitterComponent {
    bool enabled = true;
    LbmPrimitiveShape shape = LbmPrimitiveShape::Sphere;
    FCT::Vec3<> localOffset = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    float radius = 0.45f;
    FCT::Vec3<> halfExtents = FCT::Vec3<>(0.40f, 0.40f, 0.40f);
    FCT::Vec3<> injectVelocity = FCT::Vec3<>(0.085f, 0.0f, 0.0f);
    float smokeDensity = 1.0f;
    float temperature = 0.6f;
    bool injectLiquid = false;

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /*version*/) {
        ar & enabled;
        ar & shape;
        ar & localOffset;
        ar & radius;
        ar & halfExtents;
        ar & injectVelocity;
        ar & smokeDensity;
        ar & temperature;
        ar & injectLiquid;
    }
};

/**
 * @brief 显式流体固体障碍物组件（支持运动壁面单节点矩边界闭合）
 */
struct LbmObstacleComponent {
    bool enabled = true;
    LbmPrimitiveShape shape = LbmPrimitiveShape::Sphere;
    FCT::Vec3<> localOffset = FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    float radius = 0.55f;
    FCT::Vec3<> halfExtents = FCT::Vec3<>(0.50f, 0.50f, 0.50f);
    FCT::Vec3<> linearVelocity = FCT::Vec3<>(0.0f, 0.0f, 0.0f);

    template<class Archive>
    void serialize(Archive& ar, const unsigned int /*version*/) {
        ar & enabled;
        ar & shape;
        ar & localOffset;
        ar & radius;
        ar & halfExtents;
        ar & linearVelocity;
    }
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_COMPONENTS_H
