#include "AmrBlockGrid.h"

namespace MQEngine {

namespace {

float distancePointToAabb(
    const FCT::Vec3<>& p,
    const FCT::Vec3<>& boxMin,
    const FCT::Vec3<>& boxMax) noexcept {
    const float dx = std::max({ boxMin.x - p.x, 0.0f, p.x - boxMax.x });
    const float dy = std::max({ boxMin.y - p.y, 0.0f, p.y - boxMax.y });
    const float dz = std::max({ boxMin.z - p.z, 0.0f, p.z - boxMax.z });
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float distanceAabbToAabb(
    const FCT::Vec3<>& aMin,
    const FCT::Vec3<>& aMax,
    const FCT::Vec3<>& bMin,
    const FCT::Vec3<>& bMax) noexcept {
    const float dx = std::max({ aMin.x - bMax.x, bMin.x - aMax.x, 0.0f });
    const float dy = std::max({ aMin.y - bMax.y, bMin.y - aMax.y, 0.0f });
    const float dz = std::max({ aMin.z - bMax.z, bMin.z - aMax.z, 0.0f });
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

Status AmrBlockGrid::initialize(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives) {
    m_baseBlocksX = std::clamp(domain.baseBlocksX, 1, 32);
    m_baseBlocksY = std::clamp(domain.baseBlocksY, 1, 32);
    m_baseBlocksZ = std::clamp(domain.baseBlocksZ, 1, 32);
    m_maxLevels = std::clamp(domain.maxAmrLevels, 1, HomeLbmConstants::kMaxAmrLevels);

    m_domainMin = domain.minBounds;
    m_domainMax = domain.maxBounds;

    const float extentX = std::max(0.05f, m_domainMax.x - m_domainMin.x);
    m_baseCellSize = extentX / static_cast<float>(m_baseBlocksX * HomeLbmConstants::kBlockEdge);

    int32_t totalIndirectionEntries = 0;
    for (int32_t level = 0; level < HomeLbmConstants::kMaxAmrLevels; ++level) {
        m_levelGridOffsets[level] = totalIndirectionEntries;
        const int32_t count = levelBlocksX(level) * levelBlocksY(level) * levelBlocksZ(level);
        m_levelGridCounts[level] = count;
        totalIndirectionEntries += count;
    }

    const int32_t l0Count = m_levelGridCounts[0];
    int32_t poolBudget = l0Count;
    if (m_maxLevels >= 2) {
        poolBudget += std::max(128, l0Count * 5);
    }
    if (m_maxLevels >= 3) {
        poolBudget += std::max(256, l0Count * 8);
    }
    m_maxBlocks = std::min(poolBudget, 4096);

    m_levelBlockGrid.assign(static_cast<size_t>(totalIndirectionEntries), -1);
    m_blockMetas.assign(static_cast<size_t>(m_maxBlocks), LbmBlockMeta{});
    m_blockNeighbors.assign(static_cast<size_t>(m_maxBlocks * HomeLbmConstants::kNumDirs), -1);

    m_freeBlockIds.clear();
    m_freeBlockIds.reserve(static_cast<size_t>(m_maxBlocks));
    for (int32_t id = m_maxBlocks - 1; id >= l0Count; --id) {
        m_freeBlockIds.push_back(id);
    }

    int32_t nextL0Id = 0;
    for (int32_t bz = 0; bz < m_baseBlocksZ; ++bz) {
        for (int32_t by = 0; by < m_baseBlocksY; ++by) {
            for (int32_t bx = 0; bx < m_baseBlocksX; ++bx) {
                const int32_t id = nextL0Id++;
                LbmBlockMeta& meta = m_blockMetas[id];
                meta.level = 0;
                meta.blockX = bx;
                meta.blockY = by;
                meta.blockZ = bz;
                meta.parentBlock = -1;
                meta.childOctant = 0;
                meta.active = 1;
                meta.newlyRefined = 0;
                m_levelBlockGrid[computeGridFlatIndex(0, bx, by, bz)] = id;
            }
        }
    }

    rebuildNeighborAndLevelLists();
    updateDynamicRefinement(domain, primitives, {});
    clearNewlyRefinedFlags();
    return OkStatus();
}

int32_t AmrBlockGrid::computeGridFlatIndex(int32_t level, int32_t bx, int32_t by, int32_t bz) const noexcept {
    const int32_t nx = levelBlocksX(level);
    const int32_t ny = levelBlocksY(level);
    return m_levelGridOffsets[level] + (bz * ny + by) * nx + bx;
}

int32_t AmrBlockGrid::lookupBlockId(int32_t level, int32_t bx, int32_t by, int32_t bz) const noexcept {
    if (level < 0 || level >= m_maxLevels) {
        return -1;
    }
    const int32_t nx = levelBlocksX(level);
    const int32_t ny = levelBlocksY(level);
    const int32_t nz = levelBlocksZ(level);
    if (bx < 0 || bx >= nx || by < 0 || by >= ny || bz < 0 || bz >= nz) {
        return -1;
    }
    return m_levelBlockGrid[computeGridFlatIndex(level, bx, by, bz)];
}

void AmrBlockGrid::computeBlockWorldBounds(
    int32_t level,
    int32_t bx,
    int32_t by,
    int32_t bz,
    FCT::Vec3<>& outMin,
    FCT::Vec3<>& outMax) const noexcept {
    const float dx = levelCellSize(level);
    const float blockWorldEdge = dx * static_cast<float>(HomeLbmConstants::kBlockEdge);
    outMin = FCT::Vec3<>(
        m_domainMin.x + static_cast<float>(bx) * blockWorldEdge,
        m_domainMin.y + static_cast<float>(by) * blockWorldEdge,
        m_domainMin.z + static_cast<float>(bz) * blockWorldEdge);
    outMax = FCT::Vec3<>(
        outMin.x + blockWorldEdge,
        outMin.y + blockWorldEdge,
        outMin.z + blockWorldEdge);
}

bool AmrBlockGrid::shouldRefineGeometryAtBlock(
    int32_t level,
    int32_t bx,
    int32_t by,
    int32_t bz,
    bool includeInitialLiquidSeed,
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives) const noexcept {
    FCT::Vec3<> bMin{};
    FCT::Vec3<> bMax{};
    computeBlockWorldBounds(level, bx, by, bz, bMin, bMax);

    const float dx = levelCellSize(level);
    const float refineMargin = std::max(1.5f * dx, domain.obstacleRefineDistance * dx * 2.5f);

    for (const auto& prim : primitives) {
        const FCT::Vec3<> center(prim.posAndRadius.x, prim.posAndRadius.y, prim.posAndRadius.z);
        if (prim.type == HomeLbmConstants::kPrimObstacleSphere ||
            prim.type == HomeLbmConstants::kPrimEmitterSphere) {
            const float radius = prim.posAndRadius.w;
            const float dist = distancePointToAabb(center, bMin, bMax) - radius;
            if (dist <= refineMargin && dist >= -radius * 0.85f) {
                return true;
            }
        } else if (prim.type == HomeLbmConstants::kPrimObstacleBox ||
                   prim.type == HomeLbmConstants::kPrimEmitterBox) {
            const FCT::Vec3<> halfExt(prim.extentsAndHeight.x, prim.extentsAndHeight.y, prim.extentsAndHeight.z);
            const FCT::Vec3<> pMin = center - halfExt;
            const FCT::Vec3<> pMax = center + halfExt;
            if (distanceAabbToAabb(bMin, bMax, pMin, pMax) <= refineMargin) {
                return true;
            }
        } else if (prim.type == HomeLbmConstants::kPrimObstacleCapsuleY) {
            const float radius = prim.posAndRadius.w;
            const float halfH = prim.extentsAndHeight.w;
            const FCT::Vec3<> pMin(center.x - radius, center.y - halfH - radius, center.z - radius);
            const FCT::Vec3<> pMax(center.x + radius, center.y + halfH + radius, center.z + radius);
            if (distanceAabbToAabb(bMin, bMax, pMin, pMax) <= refineMargin) {
                return true;
            }
        }
    }

    if (includeInitialLiquidSeed && domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
        const FCT::Vec3<> domSpan = m_domainMax - m_domainMin;
        const FCT::Vec3<> liqMin = m_domainMin + FCT::Vec3<>(
            domSpan.x * domain.initialLiquidMinRatio.x,
            domSpan.y * domain.initialLiquidMinRatio.y,
            domSpan.z * domain.initialLiquidMinRatio.z);
        const FCT::Vec3<> liqMax = m_domainMin + FCT::Vec3<>(
            domSpan.x * domain.initialLiquidMaxRatio.x,
            domSpan.y * domain.initialLiquidMaxRatio.y,
            domSpan.z * domain.initialLiquidMaxRatio.z);
        const float waveSafetyMargin = std::max(refineMargin, dx * static_cast<float>(HomeLbmConstants::kBlockEdge));
        if (distanceAabbToAabb(bMin, bMax, liqMin, liqMax) <= waveSafetyMargin) {
            return true;
        }
    }

    return false;
}

void AmrBlockGrid::buildDesiredMasks(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    std::span<const AmrBlockSensorSample> gpuSensors,
    std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& outDesired) const {
    for (int32_t level = 0; level < HomeLbmConstants::kMaxAmrLevels; ++level) {
        outDesired[level].assign(static_cast<size_t>(m_levelGridCounts[level]), level == 0 ? 1u : 0u);
    }
    if (m_maxLevels <= 1) {
        return;
    }

    const bool hasGpuSensors = (gpuSensors.size() >= static_cast<size_t>(m_maxBlocks));
    const float refineThresh = std::max(1e-5f, domain.vorticityRefineThreshold);
    const float coarsenThresh = std::clamp(domain.vorticityCoarsenThreshold, 0.0f, refineThresh * 0.85f);

    for (int32_t targetLevel = 1; targetLevel < m_maxLevels; ++targetLevel) {
        const int32_t parentLevel = targetLevel - 1;
        const int32_t nx = levelBlocksX(targetLevel);
        const int32_t ny = levelBlocksY(targetLevel);
        const int32_t nz = levelBlocksZ(targetLevel);

        for (int32_t bz = 0; bz < nz; ++bz) {
            for (int32_t by = 0; by < ny; ++by) {
                for (int32_t bx = 0; bx < nx; ++bx) {
                    const int32_t localIdx = (bz * ny + by) * nx + bx;
                    bool wantActive = shouldRefineGeometryAtBlock(
                        targetLevel, bx, by, bz, !hasGpuSensors, domain, primitives);

                    if (!wantActive && hasGpuSensors) {
                        const int32_t existingId = lookupBlockId(targetLevel, bx, by, bz);
                        const int32_t parentId = lookupBlockId(parentLevel, bx >> 1, by >> 1, bz >> 1);
                        const float levelScale = static_cast<float>(1 << parentLevel);

                        if (existingId >= 0) {
                            const auto& s = gpuSensors[existingId];
                            if (s.maxVorticity >= coarsenThresh * levelScale ||
                                (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid && s.hasFreeSurfaceInterface != 0u)) {
                                wantActive = true;
                            }
                        } else if (parentId >= 0) {
                            const auto& ps = gpuSensors[parentId];
                            if (ps.maxVorticity >= refineThresh * levelScale ||
                                (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid && ps.hasFreeSurfaceInterface != 0u)) {
                                wantActive = true;
                            }
                        }
                    }

                    if (wantActive) {
                        outDesired[targetLevel][localIdx] = 1u;
                    }
                }
            }
        }

        // 自由液面波前单宏块安全护边膨胀，确保重网格周期内推进波前始终处于同层级精细网格内
        if (hasGpuSensors && domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
            const std::vector<uint8_t> snapshot = outDesired[targetLevel];
            for (int32_t bz = 0; bz < nz; ++bz) {
                for (int32_t by = 0; by < ny; ++by) {
                    for (int32_t bx = 0; bx < nx; ++bx) {
                        if (snapshot[(bz * ny + by) * nx + bx] == 0u) {
                            continue;
                        }
                        for (int32_t dz = -1; dz <= 1; ++dz) {
                            for (int32_t dy = -1; dy <= 1; ++dy) {
                                for (int32_t dx = -1; dx <= 1; ++dx) {
                                    const int32_t nbx = bx + dx;
                                    const int32_t nby = by + dy;
                                    const int32_t nbz = bz + dz;
                                    if (nbx >= 0 && nbx < nx && nby >= 0 && nby < ny && nbz >= 0 && nbz < nz) {
                                        outDesired[targetLevel][(nbz * ny + nby) * nx + nbx] = 1u;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void AmrBlockGrid::enforceProperNesting(
    std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& desired) const {
    // 自顶向下施加 2:1 嵌套约束: L2 宏块周围的父级 L1 26-邻域宏块必须全部存在，防止跨两级跳跃插值
    for (int32_t fineLevel = m_maxLevels - 1; fineLevel >= 2; --fineLevel) {
        const int32_t parentLevel = fineLevel - 1;
        const int32_t fnx = levelBlocksX(fineLevel);
        const int32_t fny = levelBlocksY(fineLevel);
        const int32_t fnz = levelBlocksZ(fineLevel);
        const int32_t pnx = levelBlocksX(parentLevel);
        const int32_t pny = levelBlocksY(parentLevel);
        const int32_t pnz = levelBlocksZ(parentLevel);

        for (int32_t bz = 0; bz < fnz; ++bz) {
            for (int32_t by = 0; by < fny; ++by) {
                for (int32_t bx = 0; bx < fnx; ++bx) {
                    const int32_t fIdx = (bz * fny + by) * fnx + bx;
                    if (desired[fineLevel][fIdx] == 0u) {
                        continue;
                    }
                    const int32_t pbx = bx >> 1;
                    const int32_t pby = by >> 1;
                    const int32_t pbz = bz >> 1;
                    for (int32_t dz = -1; dz <= 1; ++dz) {
                        for (int32_t dy = -1; dy <= 1; ++dy) {
                            for (int32_t dx = -1; dx <= 1; ++dx) {
                                const int32_t nbx = pbx + dx;
                                const int32_t nby = pby + dy;
                                const int32_t nbz = pbz + dz;
                                if (nbx >= 0 && nbx < pnx &&
                                    nby >= 0 && nby < pny &&
                                    nbz >= 0 && nbz < pnz) {
                                    desired[parentLevel][(nbz * pny + nby) * pnx + nbx] = 1u;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

bool AmrBlockGrid::applyDesiredTopology(
    const std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels>& desired) {
    bool changed = false;
    m_newlyRefinedBlocks.clear();

    // 1. 自顶向下释放不再需要的高层级宏块
    for (int32_t level = m_maxLevels - 1; level >= 1; --level) {
        const int32_t nx = levelBlocksX(level);
        const int32_t ny = levelBlocksY(level);
        const int32_t nz = levelBlocksZ(level);
        for (int32_t bz = 0; bz < nz; ++bz) {
            for (int32_t by = 0; by < ny; ++by) {
                for (int32_t bx = 0; bx < nx; ++bx) {
                    const int32_t localIdx = (bz * ny + by) * nx + bx;
                    const int32_t gridFlat = m_levelGridOffsets[level] + localIdx;
                    const int32_t existingId = m_levelBlockGrid[gridFlat];
                    if (existingId >= 0 && desired[level][localIdx] == 0u) {
                        m_levelBlockGrid[gridFlat] = -1;
                        m_blockMetas[existingId] = LbmBlockMeta{};
                        m_freeBlockIds.push_back(existingId);
                        changed = true;
                    }
                }
            }
        }
    }

    // 2. 自底向上分配新增细层级宏块，确保父块先于子块就绪
    for (int32_t level = 1; level < m_maxLevels; ++level) {
        const int32_t nx = levelBlocksX(level);
        const int32_t ny = levelBlocksY(level);
        const int32_t nz = levelBlocksZ(level);
        for (int32_t bz = 0; bz < nz; ++bz) {
            for (int32_t by = 0; by < ny; ++by) {
                for (int32_t bx = 0; bx < nx; ++bx) {
                    const int32_t localIdx = (bz * ny + by) * nx + bx;
                    if (desired[level][localIdx] == 0u) {
                        continue;
                    }
                    const int32_t gridFlat = m_levelGridOffsets[level] + localIdx;
                    if (m_levelBlockGrid[gridFlat] >= 0) {
                        continue;
                    }
                    const int32_t parentId = lookupBlockId(level - 1, bx >> 1, by >> 1, bz >> 1);
                    if (parentId < 0 || m_freeBlockIds.empty()) {
                        continue;
                    }

                    const int32_t newId = m_freeBlockIds.back();
                    m_freeBlockIds.pop_back();

                    LbmBlockMeta& meta = m_blockMetas[newId];
                    meta.level = level;
                    meta.blockX = bx;
                    meta.blockY = by;
                    meta.blockZ = bz;
                    meta.parentBlock = parentId;
                    meta.childOctant = ((bz & 1) << 2) | ((by & 1) << 1) | (bx & 1);
                    meta.active = 1;
                    meta.newlyRefined = 1;

                    m_levelBlockGrid[gridFlat] = newId;
                    m_newlyRefinedBlocks.push_back(newId);
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        rebuildNeighborAndLevelLists();
    }
    return changed;
}

void AmrBlockGrid::rebuildNeighborAndLevelLists() {
    for (auto& list : m_activeBlocksByLevel) {
        list.clear();
    }
    std::fill(m_blockNeighbors.begin(), m_blockNeighbors.end(), -1);

    for (int32_t id = 0; id < m_maxBlocks; ++id) {
        const LbmBlockMeta& meta = m_blockMetas[id];
        if (meta.active == 0) {
            continue;
        }
        m_activeBlocksByLevel[meta.level].push_back(id);
        const int32_t baseNeighborOffset = id * HomeLbmConstants::kNumDirs;
        for (int32_t dir = 0; dir < HomeLbmConstants::kNumDirs; ++dir) {
            const int32_t nbx = meta.blockX + HomeLbmConstants::kCx[dir];
            const int32_t nby = meta.blockY + HomeLbmConstants::kCy[dir];
            const int32_t nbz = meta.blockZ + HomeLbmConstants::kCz[dir];
            m_blockNeighbors[baseNeighborOffset + dir] = lookupBlockId(meta.level, nbx, nby, nbz);
        }
    }
}

bool AmrBlockGrid::updateDynamicRefinement(
    const LbmDomainComponent& domain,
    std::span<const LbmGpuPrimitive> primitives,
    std::span<const AmrBlockSensorSample> gpuSensors) {
    m_domainMin = domain.minBounds;
    m_domainMax = domain.maxBounds;
    const float extentX = std::max(0.05f, m_domainMax.x - m_domainMin.x);
    m_baseCellSize = extentX / static_cast<float>(m_baseBlocksX * HomeLbmConstants::kBlockEdge);

    std::array<std::vector<uint8_t>, HomeLbmConstants::kMaxAmrLevels> desired;
    buildDesiredMasks(domain, primitives, gpuSensors, desired);
    enforceProperNesting(desired);
    return applyDesiredTopology(desired);
}

void AmrBlockGrid::clearNewlyRefinedFlags() noexcept {
    for (int32_t id : m_newlyRefinedBlocks) {
        if (id >= 0 && id < static_cast<int32_t>(m_blockMetas.size())) {
            m_blockMetas[id].newlyRefined = 0;
        }
    }
    m_newlyRefinedBlocks.clear();
}

void AmrBlockGrid::collectWireframeBoxes(std::vector<AmrBlockWireframeBox>& outBoxes) const {
    outBoxes.clear();
    outBoxes.reserve(static_cast<size_t>(totalActiveBlockCount()));
    for (int32_t level = 0; level < m_maxLevels; ++level) {
        for (int32_t id : m_activeBlocksByLevel[level]) {
            const LbmBlockMeta& meta = m_blockMetas[id];
            AmrBlockWireframeBox box{};
            computeBlockWorldBounds(meta.level, meta.blockX, meta.blockY, meta.blockZ, box.minWorld, box.maxWorld);
            box.level = meta.level;
            box.blockId = id;
            outBoxes.push_back(box);
        }
    }
}

} // namespace MQEngine
