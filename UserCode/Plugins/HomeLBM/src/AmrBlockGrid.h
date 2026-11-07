#ifndef MQENGINE_HOMELBM_AMR_BLOCK_GRID_H
#define MQENGINE_HOMELBM_AMR_BLOCK_GRID_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmComponents.h"
#include "HomeLbmGpuTypes.h"

namespace MQEngine {

/**
 * @brief 用于编辑器 3D 视口叠加绘制的 AMR 宏块包围盒信息
 */
struct AmrBlockWireframeBox {
    FCT::Vec3<> minWorld;
    FCT::Vec3<> maxWorld;
    int32_t level = 0;
    int32_t blockId = -1;
};

/**
 * @brief GPU 端回传的宏块动态加密传感器统计值
 */
struct AmrBlockSensorSample {
    float maxVorticity = 0.0f;
    uint32_t hasFreeSurfaceInterface = 0u;
};

/**
 * @brief 块结构多级动态自适应网格 (8^3 宏块池 + 2:1 嵌套约束 + O(1) 稠密索引表)
 */
class HOME_LBM_API AmrBlockGrid {
public:
    AmrBlockGrid() = default;
    ~AmrBlockGrid() = default;

    /**
     * @brief 初始化基础 L0 网格与多级宏块池容量
     */
    Status initialize(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives);

    /**
     * @brief 基于几何障碍物距离、发射源、涡量传感器与自由液面标记执行动态加密/粗化重构
     * @return 若拓扑发生变更需同步至 GPU 则返回 true
     */
    bool updateDynamicRefinement(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        std::span<const AmrBlockSensorSample> gpuSensors);

    int32_t maxBlocks() const noexcept { return m_maxBlocks; }
    int32_t maxLevels() const noexcept { return m_maxLevels; }

    int32_t baseBlocksX() const noexcept { return m_baseBlocksX; }
    int32_t baseBlocksY() const noexcept { return m_baseBlocksY; }
    int32_t baseBlocksZ() const noexcept { return m_baseBlocksZ; }

    int32_t levelBlocksX(int32_t level) const noexcept { return m_baseBlocksX << level; }
    int32_t levelBlocksY(int32_t level) const noexcept { return m_baseBlocksY << level; }
    int32_t levelBlocksZ(int32_t level) const noexcept { return m_baseBlocksZ << level; }
    int32_t levelGridOffset(int32_t level) const noexcept { return m_levelGridOffsets[level]; }

    float levelCellSize(int32_t level) const noexcept {
        return m_baseCellSize / static_cast<float>(1 << level);
    }

    int32_t activeBlockCount(int32_t level) const noexcept {
        return (level >= 0 && level < HomeLbmConstants::kMaxAmrLevels)
            ? static_cast<int32_t>(m_activeBlocksByLevel[level].size())
            : 0;
    }

    int32_t totalActiveBlockCount() const noexcept {
        return activeBlockCount(0) + activeBlockCount(1) + activeBlockCount(2);
    }

    std::span<const int32_t> activeBlocksAtLevel(int32_t level) const noexcept {
        return m_activeBlocksByLevel[level];
    }

    std::span<const int32_t> newlyRefinedBlocks() const noexcept {
        return m_newlyRefinedBlocks;
    }

    void clearNewlyRefinedFlags() noexcept;

    std::span<const LbmBlockMeta> blockMetas() const noexcept { return m_blockMetas; }
    std::span<const int32_t> blockNeighbors() const noexcept { return m_blockNeighbors; }
    std::span<const int32_t> levelBlockGrid() const noexcept { return m_levelBlockGrid; }

    int32_t lookupBlockId(int32_t level, int32_t bx, int32_t by, int32_t bz) const noexcept;

    void collectWireframeBoxes(std::vector<AmrBlockWireframeBox>& outBoxes) const;

private:
    int32_t computeGridFlatIndex(int32_t level, int32_t bx, int32_t by, int32_t bz) const noexcept;
    void computeBlockWorldBounds(
        int32_t level,
        int32_t bx,
        int32_t by,
        int32_t bz,
        FCT::Vec3<>& outMin,
        FCT::Vec3<>& outMax) const noexcept;

    bool shouldRefineGeometryAtBlock(
        int32_t level,
        int32_t bx,
        int32_t by,
        int32_t bz,
        bool includeInitialLiquidSeed,
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives) const noexcept;

    void buildDesiredMasks(
        const LbmDomainComponent& domain,
        std::span<const LbmGpuPrimitive> primitives,
        std::span<const AmrBlockSensorSample> gpuSensors,
        std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& outDesired) const;

    void enforceProperNesting(
        std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& desired) const;

    bool applyDesiredTopology(
        const std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& desired);

    void rebuildNeighborAndLevelLists();

private:
    FCT::Vec3<> m_domainMin = FCT::Vec3<>(-4.0f, 0.0f, -2.0f);
    FCT::Vec3<> m_domainMax = FCT::Vec3<>(4.0f, 4.0f, 2.0f);
    float m_baseCellSize = 0.125f;

    int32_t m_baseBlocksX = 8;
    int32_t m_baseBlocksY = 4;
    int32_t m_baseBlocksZ = 4;
    int32_t m_maxLevels = 2;
    int32_t m_maxBlocks = 512;

    std::array<int32_t, HomeLbmConstants::kMaxAmrLevels> m_levelGridOffsets = { 0, 0, 0 };
    std::array<int32_t, HomeLbmConstants::kMaxAmrLevels> m_levelGridCounts = { 0, 0, 0 };

    std::vector<int32_t> m_levelBlockGrid;
    std::vector<LbmBlockMeta> m_blockMetas;
    std::vector<int32_t> m_blockNeighbors;
    std::vector<int32_t> m_freeBlockIds;

    std::array<std::vector<int32_t>, HomeLbmConstants::kMaxAmrLevels> m_activeBlocksByLevel;
    std::vector<int32_t> m_newlyRefinedBlocks;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_AMR_BLOCK_GRID_H
