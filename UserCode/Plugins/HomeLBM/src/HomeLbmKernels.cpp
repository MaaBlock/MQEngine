#include "HomeLbmKernels.h"
#include "HomeLbmDslMath.h"

namespace MQEngine {

namespace DSL = FCT::Compute::DSL;
using namespace HomeLbmDsl;

Status HomeLbmGpuBuffers::allocate(
    FCT::Compute::ComputeDevice& device,
    uint32_t maxBlocks,
    uint32_t totalLevelGridEntries) {
    if (!device.valid() || maxBlocks == 0u || totalLevelGridEntries == 0u) {
        return InvalidArgumentError("分配 HomeLbmGpuBuffers 失败：设备无效或容量为零");
    }

    release();

    const size_t interiorTotal = static_cast<size_t>(maxBlocks) * HomeLbmConstants::kBlockCells;
    const size_t paddedTotal = static_cast<size_t>(maxBlocks) * HomeLbmConstants::kPaddedCells;

    paramsBuf = device.createBuffer<LbmGpuParams>(1u);
    primitivesBuf = device.createBuffer<LbmGpuPrimitive>(kMaxPrimitives);
    blockMetaBuf = device.createBuffer<LbmBlockMeta>(maxBlocks);
    levelGridBuf = device.createBuffer<int32_t>(totalLevelGridEntries);
    activeIdsBuf = device.createBuffer<int32_t>(maxBlocks);

    m0 = device.createBuffer<FCT::Vec4>(interiorTotal);
    m1 = device.createBuffer<FCT::Vec4>(interiorTotal);
    m2 = device.createBuffer<FCT::Vec4>(interiorTotal);
    aux = device.createBuffer<FCT::Vec4>(interiorTotal);
    flags = device.createBuffer<uint32_t>(interiorTotal);

    prevM0 = device.createBuffer<FCT::Vec4>(interiorTotal);
    prevM1 = device.createBuffer<FCT::Vec4>(interiorTotal);
    prevM2 = device.createBuffer<FCT::Vec4>(interiorTotal);

    padM0 = device.createBuffer<FCT::Vec4>(paddedTotal);
    padM1 = device.createBuffer<FCT::Vec4>(paddedTotal);
    padM2 = device.createBuffer<FCT::Vec4>(paddedTotal);
    padAux = device.createBuffer<FCT::Vec4>(paddedTotal);
    padFlags = device.createBuffer<uint32_t>(paddedTotal);

    excessShareBuf = device.createBuffer<float>(interiorTotal);
    padExcessShareBuf = device.createBuffer<float>(paddedTotal);

    vortField = device.createBuffer<FCT::Vec2>(interiorTotal);
    blockSensors = device.createBuffer<uint32_t>(static_cast<size_t>(maxBlocks) * 2u);

    if (!valid()) {
        release();
        return InternalError("分配 HomeLbmGpuBuffers 失败：底层 GPU 显存缓冲区创建未成功");
    }
    return OkStatus();
}

void HomeLbmGpuBuffers::release() noexcept {
    paramsBuf = {};
    primitivesBuf = {};
    blockMetaBuf = {};
    levelGridBuf = {};
    activeIdsBuf = {};
    m0 = {};
    m1 = {};
    m2 = {};
    aux = {};
    flags = {};
    prevM0 = {};
    prevM1 = {};
    prevM2 = {};
    padM0 = {};
    padM1 = {};
    padM2 = {};
    padAux = {};
    padFlags = {};
    excessShareBuf = {};
    padExcessShareBuf = {};
    vortField = {};
    blockSensors = {};
}

bool HomeLbmGpuBuffers::valid() const noexcept {
    return paramsBuf.valid() &&
           primitivesBuf.valid() &&
           blockMetaBuf.valid() &&
           levelGridBuf.valid() &&
           activeIdsBuf.valid() &&
           m0.valid() && m1.valid() && m2.valid() && aux.valid() && flags.valid() &&
           prevM0.valid() && prevM1.valid() && prevM2.valid() &&
           padM0.valid() && padM1.valid() && padM2.valid() && padAux.valid() && padFlags.valid() &&
           excessShareBuf.valid() && padExcessShareBuf.valid() &&
           vortField.valid() && blockSensors.valid();
}

namespace {

[[nodiscard]] DSL::Bool isPointInsideInitialLiquid(
    DSL::Float3 worldPos,
    DSL::Float3 liqMin,
    DSL::Float3 liqMax) noexcept {
    return (worldPos.x >= liqMin.x) & (worldPos.x <= liqMax.x) &
           (worldPos.y >= liqMin.y) & (worldPos.y <= liqMax.y) &
           (worldPos.z >= liqMin.z) & (worldPos.z <= liqMax.z);
}

[[nodiscard]] DSL::Bool isPointInsideAnyObstacle(
    DSL::Float3 worldPos,
    const DSL::BufferVar<LbmGpuPrimitive>& primitives,
    DSL::Int primitiveCount,
    DSL::Float3& outWallVel) noexcept {
    DSL::Bool hit = false;
    outWallVel = DSL::MakeFloat3(0.0f);
    DSL::For(0, primitiveCount, [&](DSL::Int i) {
        auto prim = primitives.read(i);
        DSL::Bool isObs = (prim.type <= HomeLbmConstants::kPrimObstacleCapsuleY);
        DSL::If(isObs, [&] {
            DSL::Float d = evaluatePrimitiveSdf(worldPos, prim);
            DSL::If(d <= 0.0f, [&] {
                hit = true;
                DSL::Float4 v4 = DSL::ToFloat4(prim.velocityAndPad);
                outWallVel = DSL::MakeFloat3(v4.x, v4.y, v4.z);
            });
        });
    });
    return hit;
}

auto buildInitDomainKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmGpuPrimitive> primitives,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::BufferVar<FCT::Vec4> prevM0,
        DSL::BufferVar<FCT::Vec4> prevM1,
        DSL::BufferVar<FCT::Vec4> prevM2,
        DSL::BufferVar<FCT::Vec2> vortField,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        auto meta = blockMetas.read(blockId);
        auto params = paramsBuf.read(0);

        DSL::Int level = meta.level;
        DSL::Int gx = (meta.blockX << 3) + lx;
        DSL::Int gy = (meta.blockY << 3) + ly;
        DSL::Int gz = (meta.blockZ << 3) + lz;
        DSL::Float3 worldPos = cellCenterWorld(params, level, gx, gy, gz);

        DSL::Float3 wallVel = DSL::MakeFloat3(0.0f);
        DSL::Bool inObstacle = isPointInsideAnyObstacle(worldPos, primitives, params.primitiveCount, wallVel);

        DSL::Float4 outM0 = DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f);
        DSL::Float4 outM1 = DSL::MakeFloat4(0.0f);
        DSL::Float4 outM2 = DSL::MakeFloat4(0.0f, 0.0f, 1.0f, 0.0f);
        DSL::Float4 outAux = DSL::MakeFloat4(0.0f);
        DSL::UInt outFlag = HomeLbmConstants::kCellLiquid;

        DSL::If(inObstacle, [&] {
            outFlag = HomeLbmConstants::kCellSolid;
            outM2 = DSL::MakeFloat4(0.0f);
            outAux = DSL::MakeFloat4(0.0f, wallVel.x, wallVel.y, wallVel.z);
        }).ElseIf(params.simMode == 0, [&] {
            DSL::Bool hasInlet =
                (params.bcNegX == 2) | (params.bcPosX == 2) |
                (params.bcNegY == 2) | (params.bcPosY == 2) |
                (params.bcNegZ == 2) | (params.bcPosZ == 2);
            DSL::Float4 inflow4 = DSL::ToFloat4(params.inflowVelAndSmag);
            DSL::Float3 uInit = selectIf(
                hasInlet,
                DSL::MakeFloat3(inflow4.x, inflow4.y, inflow4.z) + 0.5f * levelBaseAcceleration(params, level, 0.0f),
                0.5f * levelBaseAcceleration(params, level, 0.0f));
            outM0 = DSL::MakeFloat4(1.0f, uInit.x, uInit.y, uInit.z);
            outM1 = DSL::MakeFloat4(uInit.x * uInit.x, uInit.y * uInit.y, uInit.z * uInit.z, uInit.x * uInit.y);
            outM2 = DSL::MakeFloat4(uInit.x * uInit.z, uInit.y * uInit.z, 1.0f, 0.0f);
            outFlag = HomeLbmConstants::kCellLiquid;
        }).Else([&] {
            DSL::Float4 domMin4 = DSL::ToFloat4(params.domainMinAndDx0);
            DSL::Float4 domMax4 = DSL::ToFloat4(params.domainMaxAndVisc0);
            DSL::Float4 rMin4 = DSL::ToFloat4(params.liquidMinRatioAndDecay);
            DSL::Float4 rMax4 = DSL::ToFloat4(params.liquidMaxRatioAndDensityScale);

            DSL::Float3 domMin = DSL::MakeFloat3(domMin4.x, domMin4.y, domMin4.z);
            DSL::Float3 domSpan = DSL::MakeFloat3(domMax4.x, domMax4.y, domMax4.z) - domMin;
            DSL::Float3 liqMin = domMin + domSpan * DSL::MakeFloat3(rMin4.x, rMin4.y, rMin4.z);
            DSL::Float3 liqMax = domMin + domSpan * DSL::MakeFloat3(rMax4.x, rMax4.y, rMax4.z);

            DSL::Int nxCells = (params.baseBlocksX << level) * HomeLbmConstants::kBlockEdge;
            DSL::Int nyCells = (params.baseBlocksY << level) * HomeLbmConstants::kBlockEdge;
            DSL::Int nzCells = (params.baseBlocksZ << level) * HomeLbmConstants::kBlockEdge;

            DSL::Bool selfInLiq = isPointInsideInitialLiquid(worldPos, liqMin, liqMax);
            DSL::If(!selfInLiq, [&] {
                outFlag = HomeLbmConstants::kCellGas;
                outM0 = DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f);
                outM2 = DSL::MakeFloat4(0.0f, 0.0f, 0.0f, 0.0f);
            }).Else([&] {
                DSL::Bool hasOutsideFluidNeighbor = false;
                DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
                    DSL::Int ngx = gx + HomeLbmConstants::kCx[Dir];
                    DSL::Int ngy = gy + HomeLbmConstants::kCy[Dir];
                    DSL::Int ngz = gz + HomeLbmConstants::kCz[Dir];
                    DSL::Bool nbInDomain =
                        (ngx >= 0) & (ngx < nxCells) &
                        (ngy >= 0) & (ngy < nyCells) &
                        (ngz >= 0) & (ngz < nzCells);
                    DSL::If(nbInDomain, [&] {
                        DSL::Float3 nbPos = cellCenterWorld(params, level, ngx, ngy, ngz);
                        DSL::Float3 dummyVel = DSL::MakeFloat3(0.0f);
                        DSL::Bool nbSolid = isPointInsideAnyObstacle(nbPos, primitives, params.primitiveCount, dummyVel);
                        DSL::Bool nbLiq = isPointInsideInitialLiquid(nbPos, liqMin, liqMax);
                        DSL::If((!nbSolid) & (!nbLiq), [&] {
                            hasOutsideFluidNeighbor = true;
                        });
                    });
                });

                DSL::Float3 uStored = 0.5f * levelBaseAcceleration(params, level, 0.0f);
                outM0 = DSL::MakeFloat4(1.0f, uStored.x, uStored.y, uStored.z);
                outM1 = DSL::MakeFloat4(uStored.x * uStored.x, uStored.y * uStored.y, uStored.z * uStored.z, uStored.x * uStored.y);
                outFlag = selectIf(
                    hasOutsideFluidNeighbor,
                    DSL::Def(HomeLbmConstants::kCellInterface),
                    DSL::Def(HomeLbmConstants::kCellLiquid));
                DSL::Float initPhi = selectIf(hasOutsideFluidNeighbor, DSL::Def(0.75f), DSL::Def(1.0f));
                outM2 = DSL::MakeFloat4(uStored.x * uStored.z, uStored.y * uStored.z, initPhi, 0.0f);
            });
        });

        DSL::Int idx = interiorIndex(blockId, lx, ly, lz);
        m0.write(idx, outM0);
        m1.write(idx, outM1);
        m2.write(idx, outM2);
        aux.write(idx, outAux);
        flags.write(idx, outFlag);
        prevM0.write(idx, outM0);
        prevM1.write(idx, outM1);
        prevM2.write(idx, outM2);
        vortField.write(idx, DSL::MakeFloat2(0.0f, 0.0f));
    });
}

auto buildVoxelizeAndInjectKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmGpuPrimitive> primitives,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::Int activeCount,
        DSL::Int currentLevel) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        auto meta = blockMetas.read(blockId);
        auto params = paramsBuf.read(0);

        DSL::Int idx = interiorIndex(blockId, lx, ly, lz);
        DSL::Int gx = (meta.blockX << 3) + lx;
        DSL::Int gy = (meta.blockY << 3) + ly;
        DSL::Int gz = (meta.blockZ << 3) + lz;
        DSL::Float3 worldPos = cellCenterWorld(params, currentLevel, gx, gy, gz);
        DSL::Float dx = levelCellSize(params, currentLevel);

        DSL::Float4 curM0 = m0.readVec(idx);
        DSL::Float4 curM1 = m1.readVec(idx);
        DSL::Float4 curM2 = m2.readVec(idx);
        DSL::Float4 curAux = aux.readVec(idx);
        DSL::UInt curType = flags.read(idx) & HomeLbmConstants::kCellTypeMask;

        DSL::Float3 wallVel = DSL::MakeFloat3(0.0f);
        DSL::Bool inObstacle = isPointInsideAnyObstacle(worldPos, primitives, params.primitiveCount, wallVel);

        DSL::If(inObstacle, [&] {
            // Note: 自由液面模式下仅将非液相单元覆盖为固体，防止动态体素化意外吞噬既有液体质量
            DSL::Bool allowSolidify = (params.simMode == 0) | (curType == HomeLbmConstants::kCellSolid) |
                                      (curType == HomeLbmConstants::kCellGas);
            DSL::If(allowSolidify, [&] {
                curType = HomeLbmConstants::kCellSolid;
                curM0 = DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f);
                curM1 = DSL::MakeFloat4(0.0f);
                curM2 = DSL::MakeFloat4(0.0f);
                curAux = DSL::MakeFloat4(0.0f, wallVel.x, wallVel.y, wallVel.z);
            });
        }).Else([&] {
            DSL::If(curType == HomeLbmConstants::kCellSolid, [&] {
                DSL::Float3 uWake = DSL::MakeFloat3(curAux.y, curAux.z, curAux.w);
                curType = selectIf(
                    params.simMode == 1,
                    DSL::Def(HomeLbmConstants::kCellGas),
                    DSL::Def(HomeLbmConstants::kCellLiquid));
                curM0 = DSL::MakeFloat4(1.0f, uWake.x, uWake.y, uWake.z);
                curM1 = DSL::MakeFloat4(uWake.x * uWake.x, uWake.y * uWake.y, uWake.z * uWake.z, uWake.x * uWake.y);
                DSL::Float wakePhi = selectIf(params.simMode == 1, DSL::Def(0.0f), DSL::Def(1.0f));
                curM2 = DSL::MakeFloat4(uWake.x * uWake.z, uWake.y * uWake.z, wakePhi, 0.0f);
                curAux = DSL::MakeFloat4(0.0f);
            });

            // 处理发射源动量、烟雾浓度与热量注入
            DSL::For(0, params.primitiveCount, [&](DSL::Int i) {
                auto prim = primitives.read(i);
                DSL::Bool isEmitter = (prim.type == HomeLbmConstants::kPrimEmitterSphere) |
                                      (prim.type == HomeLbmConstants::kPrimEmitterBox);
                DSL::If(isEmitter & (curType != HomeLbmConstants::kCellSolid), [&] {
                    DSL::Float d = evaluatePrimitiveSdf(worldPos, prim);
                    DSL::If(d <= 0.0f, [&] {
                        DSL::Float blend = DSL::Clamp(-d / DSL::Max(dx * 1.5f, 1e-4f), 0.30f, 1.0f);
                        curM2.w = DSL::Max(curM2.w, prim.smokeDensity * blend);
                        curAux.x = DSL::Max(curAux.x, prim.temperature * blend);

                        DSL::Float4 ev4 = DSL::ToFloat4(prim.velocityAndPad);
                        DSL::Float3 emitVel = DSL::MakeFloat3(ev4.x, ev4.y, ev4.z);
                        DSL::Float emitSpeedSq = DSL::Dot(emitVel, emitVel);
                        DSL::If((params.simMode == 0) & (emitSpeedSq > 1e-8f), [&] {
                            DSL::Float3 uOld = DSL::MakeFloat3(curM0.y, curM0.z, curM0.w);
                            DSL::Float3 uTarget = emitVel + 0.5f * levelBaseAcceleration(params, currentLevel, curAux.x);
                            DSL::Float3 uNew = DSL::Lerp(uOld, uTarget, DSL::MakeFloat3(0.40f * blend));
                            DSL::Float neqXX = curM1.x - uOld.x * uOld.x;
                            DSL::Float neqYY = curM1.y - uOld.y * uOld.y;
                            DSL::Float neqZZ = curM1.z - uOld.z * uOld.z;
                            DSL::Float neqXY = curM1.w - uOld.x * uOld.y;
                            DSL::Float neqXZ = curM2.x - uOld.x * uOld.z;
                            DSL::Float neqYZ = curM2.y - uOld.y * uOld.z;
                            curM0 = DSL::MakeFloat4(curM0.x, uNew.x, uNew.y, uNew.z);
                            curM1 = DSL::MakeFloat4(
                                uNew.x * uNew.x + neqXX,
                                uNew.y * uNew.y + neqYY,
                                uNew.z * uNew.z + neqZZ,
                                uNew.x * uNew.y + neqXY);
                            curM2.x = uNew.x * uNew.z + neqXZ;
                            curM2.y = uNew.y * uNew.z + neqYZ;
                        });
                    });
                });
            });
        });

        m0.write(idx, curM0);
        m1.write(idx, curM1);
        m2.write(idx, curM2);
        aux.write(idx, curAux);
        flags.write(idx, curType);
    });
}

auto buildSnapshotMomentsKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> prevM0,
        DSL::BufferVar<FCT::Vec4> prevM1,
        DSL::BufferVar<FCT::Vec4> prevM2,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int blockId = activeIds.read(slot);
        DSL::Int idx = blockId * HomeLbmConstants::kBlockCells + localIdx;

        prevM0.write(idx, m0.readVec(idx));
        prevM1.write(idx, m1.readVec(idx));
        prevM2.write(idx, m2.readVec(idx));
    });
}

auto buildProlongateBlocksKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> levelGrid,
        DSL::BufferVar<int32_t> newBlockIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> prevM0,
        DSL::BufferVar<FCT::Vec4> prevM1,
        DSL::BufferVar<FCT::Vec4> prevM2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::Int newBlockCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= newBlockCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = newBlockIds.read(slot);
        auto meta = blockMetas.read(blockId);
        auto params = paramsBuf.read(0);

        DSL::Int fgx = (meta.blockX << 3) + lx;
        DSL::Int fgy = (meta.blockY << 3) + ly;
        DSL::Int fgz = (meta.blockZ << 3) + lz;

        DSL::Float4 outM0, outM1, outM2, outAux;
        DSL::UInt outFlag;
        prolongateCoarseToFineMoments(
            params, levelGrid,
            m0, m1, m2, prevM0, prevM1, prevM2, aux, flags,
            meta.level, fgx, fgy, fgz, 1.0f,
            outM0, outM1, outM2, outAux, outFlag);

        DSL::Int idx = interiorIndex(blockId, lx, ly, lz);
        m0.write(idx, outM0);
        m1.write(idx, outM1);
        m2.write(idx, outM2);
        aux.write(idx, outAux);
        flags.write(idx, outFlag);
        prevM0.write(idx, outM0);
        prevM1.write(idx, outM1);
        prevM2.write(idx, outM2);
    });
}

auto buildFillPaddedHalosKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> levelGrid,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> prevM0,
        DSL::BufferVar<FCT::Vec4> prevM1,
        DSL::BufferVar<FCT::Vec4> prevM2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::BufferVar<FCT::Vec4> padM0,
        DSL::BufferVar<FCT::Vec4> padM1,
        DSL::BufferVar<FCT::Vec4> padM2,
        DSL::BufferVar<FCT::Vec4> padAux,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::Int activeCount,
        DSL::Int currentLevel,
        DSL::Float temporalAlpha) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kPaddedCells, [&] { DSL::Return(); });

        DSL::Int slot = tid / HomeLbmConstants::kPaddedCells;
        DSL::Int rem = tid - slot * HomeLbmConstants::kPaddedCells;
        DSL::Int px = rem % HomeLbmConstants::kPaddedEdge;
        DSL::Int py = (rem / HomeLbmConstants::kPaddedEdge) % HomeLbmConstants::kPaddedEdge;
        DSL::Int pz = rem / (HomeLbmConstants::kPaddedEdge * HomeLbmConstants::kPaddedEdge);

        DSL::Int blockId = activeIds.read(slot);
        auto meta = blockMetas.read(blockId);
        auto params = paramsBuf.read(0);

        DSL::Int dstPadIdx = paddedIndex(blockId, px, py, pz);
        DSL::Bool isInterior =
            (px >= 1) & (px <= 8) &
            (py >= 1) & (py <= 8) &
            (pz >= 1) & (pz <= 8);

        DSL::If(isInterior, [&] {
            DSL::Int srcIdx = interiorIndex(blockId, px - 1, py - 1, pz - 1);
            padM0.write(dstPadIdx, m0.readVec(srcIdx));
            padM1.write(dstPadIdx, m1.readVec(srcIdx));
            padM2.write(dstPadIdx, m2.readVec(srcIdx));
            padAux.write(dstPadIdx, aux.readVec(srcIdx));
            padFlags.write(dstPadIdx, flags.read(srcIdx));
            DSL::Return();
        });

        DSL::Int gx = (meta.blockX << 3) + (px - 1);
        DSL::Int gy = (meta.blockY << 3) + (py - 1);
        DSL::Int gz = (meta.blockZ << 3) + (pz - 1);

        DSL::Int nxCells = (params.baseBlocksX << currentLevel) * HomeLbmConstants::kBlockEdge;
        DSL::Int nyCells = (params.baseBlocksY << currentLevel) * HomeLbmConstants::kBlockEdge;
        DSL::Int nzCells = (params.baseBlocksZ << currentLevel) * HomeLbmConstants::kBlockEdge;

        DSL::Bool inDomain =
            (gx >= 0) & (gx < nxCells) &
            (gy >= 0) & (gy < nyCells) &
            (gz >= 0) & (gz < nzCells);

        DSL::Float4 outM0, outM1, outM2, outAux;
        DSL::UInt outFlag;

        DSL::If(!inDomain, [&] {
            DSL::Int adjIdx = interiorIndex(
                blockId,
                DSL::Clamp(px, 1, 8) - 1,
                DSL::Clamp(py, 1, 8) - 1,
                DSL::Clamp(pz, 1, 8) - 1);
            applyDomainBoundaryHalo(
                params, currentLevel, gx, gy, gz, nxCells, nyCells, nzCells,
                m0.readVec(adjIdx), m1.readVec(adjIdx), m2.readVec(adjIdx), aux.readVec(adjIdx), flags.read(adjIdx),
                outM0, outM1, outM2, outAux, outFlag);
        }).Else([&] {
            DSL::Int nbBlk = lookupAmrBlockId(params, levelGrid, currentLevel, gx >> 3, gy >> 3, gz >> 3);
            DSL::If(nbBlk >= 0, [&] {
                DSL::Int nbIdx = interiorIndex(nbBlk, gx & 7, gy & 7, gz & 7);
                outM0 = m0.readVec(nbIdx);
                outM1 = m1.readVec(nbIdx);
                outM2 = m2.readVec(nbIdx);
                outAux = aux.readVec(nbIdx);
                outFlag = flags.read(nbIdx);
            }).Else([&] {
                prolongateCoarseToFineMoments(
                    params, levelGrid,
                    m0, m1, m2, prevM0, prevM1, prevM2, aux, flags,
                    currentLevel, gx, gy, gz, temporalAlpha,
                    outM0, outM1, outM2, outAux, outFlag);
            });
        });

        padM0.write(dstPadIdx, outM0);
        padM1.write(dstPadIdx, outM1);
        padM2.write(dstPadIdx, outM2);
        padAux.write(dstPadIdx, outAux);
        padFlags.write(dstPadIdx, outFlag);
    });
}

auto buildStreamAndCollideKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> padM0,
        DSL::BufferVar<FCT::Vec4> padM1,
        DSL::BufferVar<FCT::Vec4> padM2,
        DSL::BufferVar<FCT::Vec4> padAux,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::Int activeCount,
        DSL::Int currentLevel) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        auto params = paramsBuf.read(0);
        (void)blockMetas;

        DSL::Int px = lx + 1;
        DSL::Int py = ly + 1;
        DSL::Int pz = lz + 1;
        DSL::Int cPadIdx = paddedIndex(blockId, px, py, pz);
        DSL::Int dstIdx = interiorIndex(blockId, lx, ly, lz);

        DSL::UInt cFlagRaw = padFlags.read(cPadIdx);
        DSL::UInt cType = cFlagRaw & HomeLbmConstants::kCellTypeMask;
        DSL::Bool isInactive = (cType == HomeLbmConstants::kCellSolid) |
                               ((params.simMode == 1) & (cType == HomeLbmConstants::kCellGas));
        DSL::If(isInactive, [&] {
            flags.write(dstIdx, cType);
            DSL::Return();
        });

        DSL::Float4 cm0 = padM0.readVec(cPadIdx);
        DSL::Float4 cm1 = padM1.readVec(cPadIdx);
        DSL::Float4 cm2 = padM2.readVec(cPadIdx);
        DSL::Float4 cAux = padAux.readVec(cPadIdx);

        DSL::Float cRho = cm0.x;
        DSL::Float3 cuStored = DSL::MakeFloat3(cm0.y, cm0.z, cm0.w);
        DSL::Float2 cs2 = DSL::MakeFloat2(cm2.x, cm2.y);
        DSL::Float cPhi = cm2.z;
        DSL::Float3 cAccel = levelBaseAcceleration(params, currentLevel, cAux.x);
        DSL::Float3 cuPhys = cuStored - 0.5f * cAccel;

        DSL::Float rhoStar = reconstructHermiteDir<0>(cRho, cuStored, cm1, cs2);
        DSL::Float3 mom = DSL::MakeFloat3(0.0f);
        DSL::Float rawXX = 0.0f;
        DSL::Float rawYY = 0.0f;
        DSL::Float rawZZ = 0.0f;
        DSL::Float rawXY = 0.0f;
        DSL::Float rawXZ = 0.0f;
        DSL::Float rawYZ = 0.0f;
        DSL::Float deltaMass = 0.0f;

        DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
            constexpr int cx = HomeLbmConstants::kCx[Dir];
            constexpr int cy = HomeLbmConstants::kCy[Dir];
            constexpr int cz = HomeLbmConstants::kCz[Dir];
            constexpr int64_t Opp = HomeLbmConstants::kOpposite[Dir];
            constexpr float fcx = static_cast<float>(cx);
            constexpr float fcy = static_cast<float>(cy);
            constexpr float fcz = static_cast<float>(cz);
            constexpr float w = HomeLbmConstants::kWeight[Dir];

            DSL::Int nbPadIdx = paddedIndex(blockId, px - cx, py - cy, pz - cz);
            DSL::UInt nbType = padFlags.read(nbPadIdx) & HomeLbmConstants::kCellTypeMask;
            DSL::Float4 nbm0 = padM0.readVec(nbPadIdx);
            DSL::Float4 nbm1 = padM1.readVec(nbPadIdx);
            DSL::Float4 nbm2 = padM2.readVec(nbPadIdx);
            DSL::Float2 nbs2 = DSL::MakeFloat2(nbm2.x, nbm2.y);

            DSL::Float fOppCenter = reconstructHermiteDir<Opp>(cRho, cuStored, cm1, cs2);
            DSL::Float fFluid = reconstructHermiteDir<Dir>(
                nbm0.x, DSL::MakeFloat3(nbm0.y, nbm0.z, nbm0.w), nbm1, nbs2);

            // 固体壁面采用三阶 Hermite 半步动量反弹，确保静止壁面质量通量恒等于零
            DSL::Float4 nbAux = padAux.readVec(nbPadIdx);
            DSL::Float cuWall = fcx * nbAux.y + fcy * nbAux.z + fcz * nbAux.w;
            DSL::Float fSolid = fOppCenter + (6.0f * w) * cRho * cuWall;

            // HOME-FREE 自由液面气相大气压边界重构 (Eq. 99)
            DSL::Float fGas =
                reconstructEquilibriumDir<Dir>(1.0f, cuPhys) +
                reconstructEquilibriumDir<Opp>(1.0f, cuPhys) -
                fOppCenter;

            DSL::Float fIn = fFluid;
            fIn = selectIf(nbType == HomeLbmConstants::kCellSolid, fSolid, fIn);
            fIn = selectIf((params.simMode == 1) & (nbType == HomeLbmConstants::kCellGas), fGas, fIn);

            rhoStar = rhoStar + fIn;
            mom.x = mom.x + fcx * fIn;
            mom.y = mom.y + fcy * fIn;
            mom.z = mom.z + fcz * fIn;
            if constexpr (cx != 0) rawXX = rawXX + (fcx * fcx) * fIn;
            if constexpr (cy != 0) rawYY = rawYY + (fcy * fcy) * fIn;
            if constexpr (cz != 0) rawZZ = rawZZ + (fcz * fcz) * fIn;
            if constexpr (cx != 0 && cy != 0) rawXY = rawXY + (fcx * fcy) * fIn;
            if constexpr (cx != 0 && cz != 0) rawXZ = rawXZ + (fcx * fcz) * fIn;
            if constexpr (cy != 0 && cz != 0) rawYZ = rawYZ + (fcy * fcz) * fIn;

            // 成对对称 VOF 质量通量交换 (Eq. 97-98)
            DSL::If((params.simMode == 1) & (cType == HomeLbmConstants::kCellInterface), [&] {
                DSL::Float theta = 0.0f;
                theta = selectIf(nbType == HomeLbmConstants::kCellLiquid, DSL::Def(1.0f), theta);
                theta = selectIf(nbType == HomeLbmConstants::kCellInterface, 0.5f * (cPhi + nbm2.z), theta);
                deltaMass = deltaMass + theta * (fFluid - fOppCenter);
            });
        });

        DSL::Float rhoSafe = DSL::Clamp(rhoStar, 0.35f, 2.50f);
        DSL::Float invRho = 1.0f / rhoSafe;
        DSL::Float3 bodyForce = rhoSafe * cAccel;
        DSL::Float3 uStar = (mom + 0.5f * bodyForce) * invRho;

        DSL::Float4 s1Star = DSL::MakeFloat4(
            rawXX * invRho - (1.0f / 3.0f),
            rawYY * invRho - (1.0f / 3.0f),
            rawZZ * invRho - (1.0f / 3.0f),
            rawXY * invRho);
        DSL::Float2 s2Star = DSL::MakeFloat2(rawXZ * invRho, rawYZ * invRho);

        DSL::Float visc0 = DSL::ToFloat4(params.domainMaxAndVisc0).w;
        DSL::Float nuLevel = visc0 * DSL::Cast<float>(1 << currentLevel);
        DSL::Float smagCs = DSL::ToFloat4(params.inflowVelAndSmag).w;

        DSL::Float4 postM0, postM1;
        DSL::Float2 postS2;
        collideHomeCell(rhoSafe, uStar, s1Star, s2Star, bodyForce, nuLevel, smagCs, postM0, postM1, postS2);

        DSL::Float newPhi = 1.0f;
        DSL::If(params.simMode == 1, [&] {
            DSL::Float oldMass = cRho * cPhi;
            newPhi = selectIf(
                cType == HomeLbmConstants::kCellInterface,
                (oldMass + deltaMass) / postM0.x,
                DSL::Def(1.0f));
        });

        // 在 10^3 Halo 缓冲内执行无条件稳定的半拉格朗日烟雾浓度与温度对流
        DSL::Float3 backPos = DSL::MakeFloat3(
            DSL::Clamp(DSL::Cast<float>(px) - uStar.x, 0.0f, 9.0f),
            DSL::Clamp(DSL::Cast<float>(py) - uStar.y, 0.0f, 9.0f),
            DSL::Clamp(DSL::Cast<float>(pz) - uStar.z, 0.0f, 9.0f));
        DSL::Int bx0 = DSL::Min(8, DSL::Cast<int32_t>(DSL::Floor(backPos.x)));
        DSL::Int by0 = DSL::Min(8, DSL::Cast<int32_t>(DSL::Floor(backPos.y)));
        DSL::Int bz0 = DSL::Min(8, DSL::Cast<int32_t>(DSL::Floor(backPos.z)));
        DSL::Float fx = backPos.x - DSL::Cast<float>(bx0);
        DSL::Float fy = backPos.y - DSL::Cast<float>(by0);
        DSL::Float fz = backPos.z - DSL::Cast<float>(bz0);

        DSL::Float advSmoke = 0.0f;
        DSL::Float advTemp = 0.0f;
        DSL::StaticFor<0, 8>([&]<int64_t Corner>() {
            constexpr int ox = static_cast<int>(Corner & 1);
            constexpr int oy = static_cast<int>((Corner >> 1) & 1);
            constexpr int oz = static_cast<int>((Corner >> 2) & 1);
            DSL::Float w =
                ((ox == 0) ? (1.0f - fx) : fx) *
                ((oy == 0) ? (1.0f - fy) : fy) *
                ((oz == 0) ? (1.0f - fz) : fz);
            DSL::Int sPad = paddedIndex(blockId, bx0 + ox, by0 + oy, bz0 + oz);
            advSmoke = advSmoke + w * padM2.readVec(sPad).w;
            advTemp = advTemp + w * padAux.readVec(sPad).x;
        });

        DSL::Float decay = DSL::ToFloat4(params.liquidMinRatioAndDecay).w;
        DSL::Float invSubstep = 1.0f / DSL::Cast<float>(1 << currentLevel);
        advSmoke = DSL::Max(0.0f, advSmoke * (1.0f - decay * invSubstep));
        advTemp = DSL::Max(0.0f, advTemp * (1.0f - decay * 0.5f * invSubstep));

        m0.write(dstIdx, postM0);
        m1.write(dstIdx, postM1);
        m2.write(dstIdx, DSL::MakeFloat4(postS2.x, postS2.y, newPhi, advSmoke));
        aux.write(dstIdx, DSL::MakeFloat4(advTemp, cAux.y, cAux.z, cAux.w));
        flags.write(dstIdx, cType);
    });
}

inline constexpr uint32_t kBitCandToLiq = 1u << 8;
inline constexpr uint32_t kBitCandToGas = 1u << 9;
inline constexpr uint32_t kBitDidToLiq = 1u << 10;
inline constexpr uint32_t kBitDidToGas = 1u << 11;
inline constexpr uint32_t kBitRecvFromToLiq = 1u << 12;
inline constexpr uint32_t kBitRecvFromToGas = 1u << 13;

auto buildFillTransitionHaloKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> levelGrid,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<uint32_t> flags,
        DSL::BufferVar<float> excessShare,
        DSL::BufferVar<FCT::Vec4> padM0,
        DSL::BufferVar<FCT::Vec4> padM2,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<float> padExcessShare,
        DSL::Int activeCount,
        DSL::Int currentLevel) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kPaddedCells, [&] { DSL::Return(); });

        DSL::Int slot = tid / HomeLbmConstants::kPaddedCells;
        DSL::Int rem = tid - slot * HomeLbmConstants::kPaddedCells;
        DSL::Int px = rem % HomeLbmConstants::kPaddedEdge;
        DSL::Int py = (rem / HomeLbmConstants::kPaddedEdge) % HomeLbmConstants::kPaddedEdge;
        DSL::Int pz = rem / (HomeLbmConstants::kPaddedEdge * HomeLbmConstants::kPaddedEdge);

        DSL::Int blockId = activeIds.read(slot);
        auto meta = blockMetas.read(blockId);
        auto params = paramsBuf.read(0);
        DSL::Int dstPadIdx = paddedIndex(blockId, px, py, pz);

        DSL::Bool isInterior =
            (px >= 1) & (px <= 8) &
            (py >= 1) & (py <= 8) &
            (pz >= 1) & (pz <= 8);

        DSL::If(isInterior, [&] {
            DSL::Int srcIdx = interiorIndex(blockId, px - 1, py - 1, pz - 1);
            padM0.write(dstPadIdx, m0.readVec(srcIdx));
            padM2.write(dstPadIdx, m2.readVec(srcIdx));
            padFlags.write(dstPadIdx, flags.read(srcIdx));
            padExcessShare.write(dstPadIdx, excessShare.read(srcIdx));
            DSL::Return();
        });

        DSL::Int gx = (meta.blockX << 3) + (px - 1);
        DSL::Int gy = (meta.blockY << 3) + (py - 1);
        DSL::Int gz = (meta.blockZ << 3) + (pz - 1);
        DSL::Int nxCells = (params.baseBlocksX << currentLevel) * HomeLbmConstants::kBlockEdge;
        DSL::Int nyCells = (params.baseBlocksY << currentLevel) * HomeLbmConstants::kBlockEdge;
        DSL::Int nzCells = (params.baseBlocksZ << currentLevel) * HomeLbmConstants::kBlockEdge;

        DSL::Bool inDomain =
            (gx >= 0) & (gx < nxCells) &
            (gy >= 0) & (gy < nyCells) &
            (gz >= 0) & (gz < nzCells);

        // 域外护边在状态迁移阶段视为固体壁，确保盈亏质量 100% 留存于域内真实界面单元
        DSL::If(!inDomain, [&] {
            padM0.write(dstPadIdx, DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f));
            padM2.write(dstPadIdx, DSL::MakeFloat4(0.0f));
            padFlags.write(dstPadIdx, HomeLbmConstants::kCellSolid);
            padExcessShare.write(dstPadIdx, 0.0f);
            DSL::Return();
        });

        DSL::Int nbBlk = lookupAmrBlockId(params, levelGrid, currentLevel, gx >> 3, gy >> 3, gz >> 3);
        DSL::If(nbBlk >= 0, [&] {
            DSL::Int nbIdx = interiorIndex(nbBlk, gx & 7, gy & 7, gz & 7);
            padM0.write(dstPadIdx, m0.readVec(nbIdx));
            padM2.write(dstPadIdx, m2.readVec(nbIdx));
            padFlags.write(dstPadIdx, flags.read(nbIdx));
            padExcessShare.write(dstPadIdx, excessShare.read(nbIdx));
        }).Else([&] {
            padM0.write(dstPadIdx, DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f));
            padM2.write(dstPadIdx, DSL::MakeFloat4(0.0f));
            padFlags.write(dstPadIdx, HomeLbmConstants::kCellGas);
            padExcessShare.write(dstPadIdx, 0.0f);
        });
    });
}

auto buildFsMarkCandidatesKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> padM2,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<uint32_t> flags,
        DSL::BufferVar<float> excessShare,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        DSL::Int px = lx + 1;
        DSL::Int py = ly + 1;
        DSL::Int pz = lz + 1;
        DSL::Int cPadIdx = paddedIndex(blockId, px, py, pz);
        DSL::Int dstIdx = interiorIndex(blockId, lx, ly, lz);

        DSL::UInt cType = padFlags.read(cPadIdx) & HomeLbmConstants::kCellTypeMask;
        excessShare.write(dstIdx, 0.0f);

        DSL::If(cType != HomeLbmConstants::kCellInterface, [&] {
            flags.write(dstIdx, cType);
            DSL::Return();
        });

        DSL::Float phi = padM2.readVec(cPadIdx).z;
        DSL::Bool hasGasNb = false;
        DSL::Bool hasLiqNb = false;
        DSL::Bool hasOverflowIntfNb = false;

        DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
            DSL::Int nbPad = paddedIndex(
                blockId,
                px + HomeLbmConstants::kCx[Dir],
                py + HomeLbmConstants::kCy[Dir],
                pz + HomeLbmConstants::kCz[Dir]);
            DSL::UInt nbType = padFlags.read(nbPad) & HomeLbmConstants::kCellTypeMask;
            DSL::Float nbPhi = padM2.readVec(nbPad).z;
            hasGasNb = hasGasNb | (nbType == HomeLbmConstants::kCellGas);
            hasLiqNb = hasLiqNb | (nbType == HomeLbmConstants::kCellLiquid);
            hasOverflowIntfNb = hasOverflowIntfNb |
                                ((nbType == HomeLbmConstants::kCellInterface) & (nbPhi > 1.002f));
        });

        DSL::Bool wantToLiq = (phi > 1.002f) | ((!hasGasNb) & (phi >= 0.55f));
        DSL::Bool wantToGas = (!wantToLiq) & (!hasOverflowIntfNb) &
                              ((phi < -0.002f) | ((!hasLiqNb) & (phi <= 0.45f)));

        DSL::UInt outFlag = cType;
        outFlag = selectIf(wantToLiq, outFlag | kBitCandToLiq, outFlag);
        outFlag = selectIf(wantToGas, outFlag | kBitCandToGas, outFlag);
        flags.write(dstIdx, outFlag);
    });
}

auto buildFsResolveTopologyKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> padM0,
        DSL::BufferVar<FCT::Vec4> padM2,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<uint32_t> flags,
        DSL::BufferVar<float> excessShare,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        DSL::Int px = lx + 1;
        DSL::Int py = ly + 1;
        DSL::Int pz = lz + 1;
        DSL::Int cPadIdx = paddedIndex(blockId, px, py, pz);
        DSL::Int dstIdx = interiorIndex(blockId, lx, ly, lz);

        DSL::UInt cRaw = padFlags.read(cPadIdx);
        DSL::UInt cType = cRaw & HomeLbmConstants::kCellTypeMask;
        DSL::Bool candToLiq = (cRaw & kBitCandToLiq) != 0u;
        DSL::Bool candToGas = (cRaw & kBitCandToGas) != 0u;

        // 1. 正在迁移的 Interface 单元统计单跳确定性接收者数量并计算均摊盈亏质量
        DSL::If(cType == HomeLbmConstants::kCellInterface, [&] {
            DSL::If((!candToLiq) & (!candToGas), [&] {
                flags.write(dstIdx, HomeLbmConstants::kCellInterface | kBitRecvFromToLiq | kBitRecvFromToGas);
                excessShare.write(dstIdx, 0.0f);
                DSL::Return();
            });

            DSL::Int recvCount = 0;
            DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
                DSL::Int nbPad = paddedIndex(
                    blockId,
                    px + HomeLbmConstants::kCx[Dir],
                    py + HomeLbmConstants::kCy[Dir],
                    pz + HomeLbmConstants::kCz[Dir]);
                DSL::UInt nbRaw = padFlags.read(nbPad);
                DSL::UInt nbType = nbRaw & HomeLbmConstants::kCellTypeMask;
                DSL::Bool nbSurvivingIntf = (nbType == HomeLbmConstants::kCellInterface) &
                                            ((nbRaw & (kBitCandToLiq | kBitCandToGas)) == 0u);
                DSL::Bool validForLiq = candToLiq & ((nbType == HomeLbmConstants::kCellGas) | nbSurvivingIntf);
                DSL::Bool validForGas = candToGas & ((nbType == HomeLbmConstants::kCellLiquid) | nbSurvivingIntf);
                recvCount = recvCount + selectIf(validForLiq | validForGas, DSL::Def(1), DSL::Def(0));
            });

            DSL::If(recvCount <= 0, [&] {
                // 无可用接收者时推迟本步迁移，严禁吞没或丢弃任何质量
                flags.write(dstIdx, HomeLbmConstants::kCellInterface);
                excessShare.write(dstIdx, 0.0f);
            }).Else([&] {
                DSL::Float4 cm0 = padM0.readVec(cPadIdx);
                DSL::Float4 cm2 = padM2.readVec(cPadIdx);
                DSL::Float rho = cm0.x;
                DSL::Float phi = cm2.z;
                DSL::Float targetPhi = selectIf(candToLiq, DSL::Def(1.0f), DSL::Def(0.0f));
                DSL::Float share = (rho * (phi - targetPhi)) / DSL::Cast<float>(recvCount);
                excessShare.write(dstIdx, share);

                DSL::If(candToLiq, [&] {
                    m2.write(dstIdx, DSL::MakeFloat4(cm2.x, cm2.y, 1.0f, cm2.w));
                    flags.write(dstIdx, HomeLbmConstants::kCellLiquid | kBitDidToLiq);
                }).Else([&] {
                    m0.write(dstIdx, DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f));
                    m1.write(dstIdx, DSL::MakeFloat4(0.0f));
                    m2.write(dstIdx, DSL::MakeFloat4(0.0f));
                    flags.write(dstIdx, HomeLbmConstants::kCellGas | kBitDidToGas);
                });
            });
            DSL::Return();
        });

        // 2. 气相单元若紧邻任何 ToLiquid 候选单元（且该候选单元至少拥有本气相单元作为接收者，故必成功迁移），唤醒为新界面
        DSL::If(cType == HomeLbmConstants::kCellGas, [&] {
            DSL::Bool touchedToLiq = false;
            DSL::Float3 sumU = DSL::MakeFloat3(0.0f);
            DSL::Float sumRho = 0.0f;
            DSL::Float fluidWeight = 0.0f;

            DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
                DSL::Int nbPad = paddedIndex(
                    blockId,
                    px + HomeLbmConstants::kCx[Dir],
                    py + HomeLbmConstants::kCy[Dir],
                    pz + HomeLbmConstants::kCz[Dir]);
                DSL::UInt nbRaw = padFlags.read(nbPad);
                DSL::UInt nbType = nbRaw & HomeLbmConstants::kCellTypeMask;
                DSL::If((nbRaw & kBitCandToLiq) != 0u, [&] {
                    touchedToLiq = true;
                });
                DSL::Bool nbIsFluid = (nbType == HomeLbmConstants::kCellLiquid) |
                                      (nbType == HomeLbmConstants::kCellInterface);
                DSL::If(nbIsFluid, [&] {
                    DSL::Float4 nbm0 = padM0.readVec(nbPad);
                    sumRho = sumRho + nbm0.x;
                    sumU = sumU + DSL::MakeFloat3(nbm0.y, nbm0.z, nbm0.w);
                    fluidWeight = fluidWeight + 1.0f;
                });
            });

            DSL::If(touchedToLiq, [&] {
                DSL::Float invCnt = 1.0f / DSL::Max(fluidWeight, 1.0f);
                DSL::Float initRho = selectIf(fluidWeight > 0.5f, sumRho * invCnt, DSL::Def(1.0f));
                DSL::Float3 initU = selectIf(fluidWeight > 0.5f, sumU * invCnt, DSL::MakeFloat3(0.0f));
                m0.write(dstIdx, DSL::MakeFloat4(initRho, initU.x, initU.y, initU.z));
                m1.write(dstIdx, DSL::MakeFloat4(initU.x * initU.x, initU.y * initU.y, initU.z * initU.z, initU.x * initU.y));
                m2.write(dstIdx, DSL::MakeFloat4(initU.x * initU.z, initU.y * initU.z, 0.0f, 0.0f));
                flags.write(dstIdx, HomeLbmConstants::kCellInterface | kBitRecvFromToLiq);
            }).Else([&] {
                flags.write(dstIdx, HomeLbmConstants::kCellGas);
            });
            excessShare.write(dstIdx, 0.0f);
            DSL::Return();
        });

        // 3. 液相单元若紧邻任何 ToGas 候选单元（必拥有本液相单元作为接收者而成功迁移），降级为新界面并保留完整质量
        DSL::If(cType == HomeLbmConstants::kCellLiquid, [&] {
            DSL::Bool touchedToGas = false;
            DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
                DSL::Int nbPad = paddedIndex(
                    blockId,
                    px + HomeLbmConstants::kCx[Dir],
                    py + HomeLbmConstants::kCy[Dir],
                    pz + HomeLbmConstants::kCz[Dir]);
                DSL::UInt nbRaw = padFlags.read(nbPad);
                DSL::If((nbRaw & kBitCandToGas) != 0u, [&] {
                    touchedToGas = true;
                });
            });

            DSL::If(touchedToGas, [&] {
                DSL::Float4 cm2 = padM2.readVec(cPadIdx);
                m2.write(dstIdx, DSL::MakeFloat4(cm2.x, cm2.y, 1.0f, cm2.w));
                flags.write(dstIdx, HomeLbmConstants::kCellInterface | kBitRecvFromToGas);
            }).Else([&] {
                flags.write(dstIdx, HomeLbmConstants::kCellLiquid);
            });
            excessShare.write(dstIdx, 0.0f);
            DSL::Return();
        });

        flags.write(dstIdx, cType);
        excessShare.write(dstIdx, 0.0f);
    });
}

auto buildFsGatherExcessMassKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<float> padExcessShare,
        DSL::BufferVar<uint32_t> flags,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        DSL::Int px = lx + 1;
        DSL::Int py = ly + 1;
        DSL::Int pz = lz + 1;
        DSL::Int cPadIdx = paddedIndex(blockId, px, py, pz);
        DSL::Int dstIdx = interiorIndex(blockId, lx, ly, lz);

        DSL::UInt cRaw = padFlags.read(cPadIdx);
        DSL::UInt cType = cRaw & HomeLbmConstants::kCellTypeMask;
        DSL::Bool recvLiq = (cRaw & kBitRecvFromToLiq) != 0u;
        DSL::Bool recvGas = (cRaw & kBitRecvFromToGas) != 0u;

        DSL::If((cType == HomeLbmConstants::kCellInterface) & (recvLiq | recvGas), [&] {
            DSL::Float gatheredMass = 0.0f;
            DSL::StaticFor<1, HomeLbmConstants::kNumDirs>([&]<int64_t Dir>() {
                DSL::Int nbPad = paddedIndex(
                    blockId,
                    px + HomeLbmConstants::kCx[Dir],
                    py + HomeLbmConstants::kCy[Dir],
                    pz + HomeLbmConstants::kCz[Dir]);
                DSL::UInt nbRaw = padFlags.read(nbPad);
                DSL::Float nbShare = padExcessShare.read(nbPad);
                DSL::Bool matchLiq = recvLiq & ((nbRaw & kBitDidToLiq) != 0u);
                DSL::Bool matchGas = recvGas & ((nbRaw & kBitDidToGas) != 0u);
                gatheredMass = gatheredMass + selectIf(matchLiq | matchGas, nbShare, DSL::Def(0.0f));
            });

            DSL::Float4 cm0 = m0.readVec(dstIdx);
            DSL::Float4 cm2 = m2.readVec(dstIdx);
            DSL::Float rho = DSL::Max(cm0.x, 0.35f);
            DSL::Float newMass = rho * cm2.z + gatheredMass;
            m2.write(dstIdx, DSL::MakeFloat4(cm2.x, cm2.y, newMass / rho, cm2.w));
        });

        flags.write(dstIdx, cType);
    });
}

auto buildCoalesceFineToCoarseKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmBlockMeta> blockMetas,
        DSL::BufferVar<int32_t> levelGrid,
        DSL::BufferVar<int32_t> coarseActiveIds,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m1,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec4> aux,
        DSL::BufferVar<uint32_t> flags,
        DSL::Int coarseActiveCount,
        DSL::Int coarseLevel) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= coarseActiveCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int cBlockId = coarseActiveIds.read(slot);
        auto cMeta = blockMetas.read(cBlockId);
        auto params = paramsBuf.read(0);

        DSL::Int fineLevel = coarseLevel + 1;
        DSL::Int cgx = (cMeta.blockX << 3) + lx;
        DSL::Int cgy = (cMeta.blockY << 3) + ly;
        DSL::Int cgz = (cMeta.blockZ << 3) + lz;

        DSL::Int fbx = (cgx << 1) >> 3;
        DSL::Int fby = (cgy << 1) >> 3;
        DSL::Int fbz = (cgz << 1) >> 3;
        DSL::Int fBlockId = lookupAmrBlockId(params, levelGrid, fineLevel, fbx, fby, fbz);
        DSL::If(fBlockId < 0, [&] { DSL::Return(); });

        DSL::Int baseFlx = (cgx << 1) & 7;
        DSL::Int baseFly = (cgy << 1) & 7;
        DSL::Int baseFlz = (cgz << 1) & 7;

        DSL::Float sumRho = 0.0f;
        DSL::Float3 sumUPhys = DSL::MakeFloat3(0.0f);
        DSL::Float4 sumNeq1 = DSL::MakeFloat4(0.0f);
        DSL::Float2 sumNeq2 = DSL::MakeFloat2(0.0f);
        DSL::Float sumLiquidMass = 0.0f;
        DSL::Float sumSmoke = 0.0f;
        DSL::Float4 sumAux = DSL::MakeFloat4(0.0f);
        DSL::Int fluidCnt = 0;
        DSL::Int liqCnt = 0;
        DSL::Int gasCnt = 0;
        DSL::Int intfCnt = 0;

        DSL::StaticFor<0, 8>([&]<int64_t Child>() {
            constexpr int ox = static_cast<int>(Child & 1);
            constexpr int oy = static_cast<int>((Child >> 1) & 1);
            constexpr int oz = static_cast<int>((Child >> 2) & 1);

            DSL::Int fIdx = interiorIndex(fBlockId, baseFlx + ox, baseFly + oy, baseFlz + oz);
            DSL::UInt fType = flags.read(fIdx) & HomeLbmConstants::kCellTypeMask;
            DSL::Float4 fm0 = m0.readVec(fIdx);
            DSL::Float4 fm1 = m1.readVec(fIdx);
            DSL::Float4 fm2 = m2.readVec(fIdx);
            DSL::Float4 fAux = aux.readVec(fIdx);

            sumAux = sumAux + fAux;
            DSL::If(fType != HomeLbmConstants::kCellSolid, [&] {
                fluidCnt = fluidCnt + 1;
                DSL::Float3 fuStored = DSL::MakeFloat3(fm0.y, fm0.z, fm0.w);
                DSL::Float3 fAccel = levelBaseAcceleration(params, fineLevel, fAux.x);
                sumRho = sumRho + fm0.x;
                sumUPhys = sumUPhys + (fuStored - 0.5f * fAccel);
                sumNeq1 = sumNeq1 + DSL::MakeFloat4(
                    fm1.x - fuStored.x * fuStored.x,
                    fm1.y - fuStored.y * fuStored.y,
                    fm1.z - fuStored.z * fuStored.z,
                    fm1.w - fuStored.x * fuStored.y);
                sumNeq2 = sumNeq2 + DSL::MakeFloat2(
                    fm2.x - fuStored.x * fuStored.z,
                    fm2.y - fuStored.y * fuStored.z);
                sumLiquidMass = sumLiquidMass + fm0.x * fm2.z;
                sumSmoke = sumSmoke + fm2.w;

                liqCnt = liqCnt + selectIf(fType == HomeLbmConstants::kCellLiquid, DSL::Def(1), DSL::Def(0));
                gasCnt = gasCnt + selectIf(fType == HomeLbmConstants::kCellGas, DSL::Def(1), DSL::Def(0));
                intfCnt = intfCnt + selectIf(fType == HomeLbmConstants::kCellInterface, DSL::Def(1), DSL::Def(0));
            });
        });

        DSL::Int cIdx = interiorIndex(cBlockId, lx, ly, lz);
        DSL::If(fluidCnt == 0, [&] {
            flags.write(cIdx, HomeLbmConstants::kCellSolid);
            m0.write(cIdx, DSL::MakeFloat4(1.0f, 0.0f, 0.0f, 0.0f));
            m1.write(cIdx, DSL::MakeFloat4(0.0f));
            m2.write(cIdx, DSL::MakeFloat4(0.0f));
            aux.write(cIdx, sumAux * 0.125f);
            DSL::Return();
        });

        DSL::Float invFluid = 1.0f / DSL::Cast<float>(fluidCnt);
        DSL::Float cRho = sumRho * invFluid;
        DSL::Float4 cAux = sumAux * 0.125f;
        DSL::Float3 cAccel = levelBaseAcceleration(params, coarseLevel, cAux.x);
        DSL::Float3 cuStored = (sumUPhys * invFluid) + 0.5f * cAccel;

        // 细网格到粗网格二阶非平衡应力重标度 (Eq. 77, 80)
        DSL::Float visc0 = DSL::ToFloat4(params.domainMaxAndVisc0).w;
        DSL::Float tauC = 3.0f * (visc0 * DSL::Cast<float>(1 << coarseLevel)) + 0.5f;
        DSL::Float tauF = 3.0f * (visc0 * DSL::Cast<float>(1 << fineLevel)) + 0.5f;
        DSL::Float restrictScale = (2.0f * tauC) / DSL::Max(tauF, 1e-4f);

        DSL::Float4 avgNeq1 = sumNeq1 * (invFluid * restrictScale);
        DSL::Float2 avgNeq2 = sumNeq2 * (invFluid * restrictScale);

        DSL::Float cPhi = 1.0f;
        DSL::UInt cType = HomeLbmConstants::kCellLiquid;
        DSL::If(params.simMode == 1, [&] {
            cPhi = DSL::Clamp((sumLiquidMass * invFluid) / DSL::Max(cRho, 0.35f), 0.0f, 1.0f);
            cType = HomeLbmConstants::kCellInterface;
            cType = selectIf((intfCnt == 0) & (gasCnt == 0), DSL::Def(HomeLbmConstants::kCellLiquid), cType);
            cType = selectIf((intfCnt == 0) & (liqCnt == 0), DSL::Def(HomeLbmConstants::kCellGas), cType);
        });

        m0.write(cIdx, DSL::MakeFloat4(cRho, cuStored.x, cuStored.y, cuStored.z));
        m1.write(cIdx, DSL::MakeFloat4(
            cuStored.x * cuStored.x + avgNeq1.x,
            cuStored.y * cuStored.y + avgNeq1.y,
            cuStored.z * cuStored.z + avgNeq1.z,
            cuStored.x * cuStored.y + avgNeq1.w));
        m2.write(cIdx, DSL::MakeFloat4(
            cuStored.x * cuStored.z + avgNeq2.x,
            cuStored.y * cuStored.z + avgNeq2.y,
            cPhi,
            sumSmoke * invFluid));
        aux.write(cIdx, cAux);
        flags.write(cIdx, cType);
    });
}

auto buildClearBlockSensorsKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<uint32_t> blockSensors,
        DSL::Int activeCount) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount, [&] { DSL::Return(); });
        DSL::Int blockId = activeIds.read(tid);
        blockSensors.write(blockId * 2, 0u);
        blockSensors.write(blockId * 2 + 1, 0u);
    });
}

auto buildComputeVorticityAndSensorsKernel() {
    return FCT::Compute::MakeKernel1D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<int32_t> activeIds,
        DSL::BufferVar<FCT::Vec4> padM0,
        DSL::BufferVar<FCT::Vec4> padAux,
        DSL::BufferVar<uint32_t> padFlags,
        DSL::BufferVar<FCT::Vec2> vortField,
        DSL::BufferVar<uint32_t> blockSensors,
        DSL::Int activeCount,
        DSL::Int currentLevel) {
        DSL::Int tid = DSL::Cast<int32_t>(DSL::DispatchId().x);
        DSL::If(tid >= activeCount * HomeLbmConstants::kBlockCells, [&] { DSL::Return(); });

        DSL::Int slot = tid >> 9;
        DSL::Int localIdx = tid & 511;
        DSL::Int lx = localIdx & 7;
        DSL::Int ly = (localIdx >> 3) & 7;
        DSL::Int lz = localIdx >> 6;

        DSL::Int blockId = activeIds.read(slot);
        auto params = paramsBuf.read(0);

        DSL::Int px = lx + 1;
        DSL::Int py = ly + 1;
        DSL::Int pz = lz + 1;
        DSL::Int cPad = paddedIndex(blockId, px, py, pz);
        DSL::Int dstIdx = interiorIndex(blockId, lx, ly, lz);

        DSL::UInt cType = padFlags.read(cPad) & HomeLbmConstants::kCellTypeMask;
        DSL::Bool hasFreeSurfaceFluid =
            (params.simMode == 1) &
            ((cType == HomeLbmConstants::kCellLiquid) | (cType == HomeLbmConstants::kCellInterface));
        DSL::If(hasFreeSurfaceFluid, [&] {
            (void)blockSensors.atomic(blockId * 2 + 1).fetch_or(1u);
        });

        DSL::Bool skipFluidGrad = (cType == HomeLbmConstants::kCellSolid) |
                                  ((params.simMode == 1) & (cType == HomeLbmConstants::kCellGas));
        DSL::If(skipFluidGrad, [&] {
            vortField.write(dstIdx, DSL::MakeFloat2(0.0f, 0.0f));
            DSL::Return();
        });

        auto readPhysVel = [&](DSL::Int qx, DSL::Int qy, DSL::Int qz) {
            DSL::Int pIdx = paddedIndex(blockId, qx, qy, qz);
            DSL::UInt t = padFlags.read(pIdx) & HomeLbmConstants::kCellTypeMask;
            DSL::Float4 v4 = padM0.readVec(pIdx);
            DSL::Float4 a4 = padAux.readVec(pIdx);
            DSL::Float3 uFluid = DSL::MakeFloat3(v4.y, v4.z, v4.w) -
                                 0.5f * levelBaseAcceleration(params, currentLevel, a4.x);
            DSL::Float3 uWall = DSL::MakeFloat3(a4.y, a4.z, a4.w);
            return selectIf(t == HomeLbmConstants::kCellSolid, uWall, uFluid);
        };

        DSL::Float3 uXp = readPhysVel(px + 1, py, pz);
        DSL::Float3 uXm = readPhysVel(px - 1, py, pz);
        DSL::Float3 uYp = readPhysVel(px, py + 1, pz);
        DSL::Float3 uYm = readPhysVel(px, py - 1, pz);
        DSL::Float3 uZp = readPhysVel(px, py, pz + 1);
        DSL::Float3 uZm = readPhysVel(px, py, pz - 1);

        // 归一化至 L0 参考空间尺度的速度梯度张量 J_ij = du_i / dx_j
        DSL::Float gradScale = 0.5f * DSL::Cast<float>(1 << currentLevel);
        DSL::Float3 dUdx = (uXp - uXm) * gradScale;
        DSL::Float3 dUdy = (uYp - uYm) * gradScale;
        DSL::Float3 dUdz = (uZp - uZm) * gradScale;

        DSL::Float3 curl = DSL::MakeFloat3(
            dUdy.z - dUdz.y,
            dUdz.x - dUdx.z,
            dUdx.y - dUdy.x);
        DSL::Float vortMag = DSL::Length(curl);

        // Q 判据 = 0.5 * (||Omega||^2 - ||S||^2)
        DSL::Float s11 = dUdx.x;
        DSL::Float s22 = dUdy.y;
        DSL::Float s33 = dUdz.z;
        DSL::Float s12 = 0.5f * (dUdx.y + dUdy.x);
        DSL::Float s13 = 0.5f * (dUdx.z + dUdz.x);
        DSL::Float s23 = 0.5f * (dUdy.z + dUdz.y);
        DSL::Float strainSq = s11 * s11 + s22 * s22 + s33 * s33 +
                              2.0f * (s12 * s12 + s13 * s13 + s23 * s23);
        DSL::Float omegaSq = 0.5f * DSL::Dot(curl, curl);
        DSL::Float qCrit = 0.5f * (omegaSq - strainSq);

        vortField.write(dstIdx, DSL::MakeFloat2(vortMag, qCrit));

        DSL::Float clampedScaled = DSL::Clamp(vortMag * 1000000.0f, 0.0f, 2000000000.0f);
        DSL::UInt fixedVort = DSL::Cast<uint32_t>(clampedScaled);
        (void)blockSensors.atomic(blockId * 2).fetch_max(fixedVort);
    });
}

/**
 * @brief 查询世界坐标点命中的最细活跃 AMR 叶子单元及其场量
 */
inline void sampleFinestAmrLeaf(
    const DSL::Var<LbmGpuParams>& params,
    const DSL::BufferVar<int32_t>& levelGrid,
    const DSL::BufferVar<FCT::Vec4>& m0,
    const DSL::BufferVar<FCT::Vec4>& m2,
    const DSL::BufferVar<FCT::Vec2>& vortField,
    const DSL::BufferVar<uint32_t>& flags,
    DSL::Float3 worldPos,
    DSL::Int& outLevel,
    DSL::UInt& outCellType,
    DSL::Float4& outM0,
    DSL::Float4& outM2,
    DSL::Float2& outVort) noexcept {
    DSL::Float4 domMin4 = DSL::ToFloat4(params.domainMinAndDx0);
    DSL::Float3 domMin = DSL::MakeFloat3(domMin4.x, domMin4.y, domMin4.z);
    DSL::Float dx0 = domMin4.w;

    outLevel = 0;
    DSL::Int chosenIdx = 0;
    DSL::Bool found = false;

    DSL::StaticFor<0, HomeLbmConstants::kMaxAmrLevels>([&]<int64_t LvRev>() {
        const int lv = static_cast<int>(HomeLbmConstants::kMaxAmrLevels - 1 - LvRev);
        DSL::If((!found) & (lv < params.maxAmrLevels), [&] {
            DSL::Float dx = dx0 / static_cast<float>(1 << lv);
            DSL::Float3 rel = (worldPos - domMin) / dx;
            DSL::Int nxCells = (params.baseBlocksX << lv) * HomeLbmConstants::kBlockEdge;
            DSL::Int nyCells = (params.baseBlocksY << lv) * HomeLbmConstants::kBlockEdge;
            DSL::Int nzCells = (params.baseBlocksZ << lv) * HomeLbmConstants::kBlockEdge;

            DSL::Int gx = DSL::Clamp(DSL::Cast<int32_t>(DSL::Floor(rel.x)), 0, nxCells - 1);
            DSL::Int gy = DSL::Clamp(DSL::Cast<int32_t>(DSL::Floor(rel.y)), 0, nyCells - 1);
            DSL::Int gz = DSL::Clamp(DSL::Cast<int32_t>(DSL::Floor(rel.z)), 0, nzCells - 1);

            DSL::Int blk = lookupAmrBlockId(params, levelGrid, lv, gx >> 3, gy >> 3, gz >> 3);
            DSL::If(blk >= 0, [&] {
                found = true;
                outLevel = lv;
                chosenIdx = interiorIndex(blk, gx & 7, gy & 7, gz & 7);
            });
        });
    });

    outCellType = flags.read(chosenIdx) & HomeLbmConstants::kCellTypeMask;
    outM0 = m0.readVec(chosenIdx);
    outM2 = m2.readVec(chosenIdx);
    outVort = vortField.readVec(chosenIdx);
}

/**
 * @brief 在最细可用 AMR 层级对 VOF 液体填充率 phi 执行三线性平滑采样，消除自由液面体素阶梯感
 */
[[nodiscard]] inline DSL::Float sampleSmoothLiquidPhi(
    const DSL::Var<LbmGpuParams>& params,
    const DSL::BufferVar<int32_t>& levelGrid,
    const DSL::BufferVar<FCT::Vec4>& m2,
    const DSL::BufferVar<uint32_t>& flags,
    DSL::Float3 worldPos) noexcept {
    DSL::Float4 domMin4 = DSL::ToFloat4(params.domainMinAndDx0);
    DSL::Float3 domMin = DSL::MakeFloat3(domMin4.x, domMin4.y, domMin4.z);
    DSL::Int finestLv = DSL::Max(0, params.maxAmrLevels - 1);
    DSL::Float dx = domMin4.w / DSL::Cast<float>(1 << finestLv);

    DSL::Int nxCells = (params.baseBlocksX << finestLv) * HomeLbmConstants::kBlockEdge;
    DSL::Int nyCells = (params.baseBlocksY << finestLv) * HomeLbmConstants::kBlockEdge;
    DSL::Int nzCells = (params.baseBlocksZ << finestLv) * HomeLbmConstants::kBlockEdge;

    DSL::Float3 coord = ((worldPos - domMin) / dx) - DSL::MakeFloat3(0.5f);
    coord.x = DSL::Clamp(coord.x, 0.0f, DSL::Cast<float>(nxCells - 1));
    coord.y = DSL::Clamp(coord.y, 0.0f, DSL::Cast<float>(nyCells - 1));
    coord.z = DSL::Clamp(coord.z, 0.0f, DSL::Cast<float>(nzCells - 1));

    DSL::Int ix0 = DSL::Cast<int32_t>(DSL::Floor(coord.x));
    DSL::Int iy0 = DSL::Cast<int32_t>(DSL::Floor(coord.y));
    DSL::Int iz0 = DSL::Cast<int32_t>(DSL::Floor(coord.z));
    DSL::Int ix1 = DSL::Min(ix0 + 1, nxCells - 1);
    DSL::Int iy1 = DSL::Min(iy0 + 1, nyCells - 1);
    DSL::Int iz1 = DSL::Min(iz0 + 1, nzCells - 1);

    DSL::Float tx = coord.x - DSL::Cast<float>(ix0);
    DSL::Float ty = coord.y - DSL::Cast<float>(iy0);
    DSL::Float tz = coord.z - DSL::Cast<float>(iz0);

    auto readCornerPhi = [&](DSL::Int gx, DSL::Int gy, DSL::Int gz) {
        DSL::Int blk = lookupAmrBlockId(params, levelGrid, finestLv, gx >> 3, gy >> 3, gz >> 3);
        DSL::Float val = 0.0f;
        DSL::If(blk >= 0, [&] {
            DSL::Int idx = interiorIndex(blk, gx & 7, gy & 7, gz & 7);
            DSL::UInt ct = flags.read(idx) & HomeLbmConstants::kCellTypeMask;
            DSL::Float rawPhi = DSL::Clamp(m2.readVec(idx).z, 0.0f, 1.0f);
            val = selectIf(ct == HomeLbmConstants::kCellLiquid, DSL::Def(1.0f), rawPhi);
            val = selectIf((ct == HomeLbmConstants::kCellGas) | (ct == HomeLbmConstants::kCellSolid), DSL::Def(0.0f), val);
        }).Else([&] {
            DSL::Int l0x = gx >> finestLv;
            DSL::Int l0y = gy >> finestLv;
            DSL::Int l0z = gz >> finestLv;
            DSL::Int blk0 = DSL::Max(0, lookupAmrBlockId(params, levelGrid, 0, l0x >> 3, l0y >> 3, l0z >> 3));
            DSL::Int idx0 = interiorIndex(blk0, l0x & 7, l0y & 7, l0z & 7);
            DSL::UInt ct0 = flags.read(idx0) & HomeLbmConstants::kCellTypeMask;
            DSL::Float rawPhi0 = DSL::Clamp(m2.readVec(idx0).z, 0.0f, 1.0f);
            val = selectIf(ct0 == HomeLbmConstants::kCellLiquid, DSL::Def(1.0f), rawPhi0);
            val = selectIf((ct0 == HomeLbmConstants::kCellGas) | (ct0 == HomeLbmConstants::kCellSolid), DSL::Def(0.0f), val);
        });
        return val;
    };

    DSL::Float acc = 0.0f;
    DSL::StaticFor<0, 8>([&]<int64_t Corner>() {
        constexpr int ox = static_cast<int>(Corner & 1);
        constexpr int oy = static_cast<int>((Corner >> 1) & 1);
        constexpr int oz = static_cast<int>((Corner >> 2) & 1);
        DSL::Float w =
            ((ox == 0) ? (1.0f - tx) : tx) *
            ((oy == 0) ? (1.0f - ty) : ty) *
            ((oz == 0) ? (1.0f - tz) : tz);
        acc = acc + w * readCornerPhi(
            (ox == 0) ? ix0 : ix1,
            (oy == 0) ? iy0 : iy1,
            (oz == 0) ? iz0 : iz1);
    });
    return acc;
}

auto buildRaymarchVolumeKernel() {
    return FCT::Compute::MakeKernel2D([](
        DSL::BufferVar<LbmGpuParams> paramsBuf,
        DSL::BufferVar<LbmGpuPrimitive> primitives,
        DSL::BufferVar<int32_t> levelGrid,
        DSL::BufferVar<FCT::Vec4> m0,
        DSL::BufferVar<FCT::Vec4> m2,
        DSL::BufferVar<FCT::Vec2> vortField,
        DSL::BufferVar<uint32_t> flags,
        DSL::FormatImageVar<FCT::Format::R16G16B16A16_SFLOAT> outRadiance,
        DSL::FormatImageVar<FCT::Format::R32_SFLOAT> outHitDist) {
        DSL::UInt2 pix = DSL::MakeUInt2(DSL::DispatchId().x, DSL::DispatchId().y);
        auto params = paramsBuf.read(0);
        DSL::If((DSL::Cast<int32_t>(pix.x) >= params.renderWidth) |
                (DSL::Cast<int32_t>(pix.y) >= params.renderHeight), [&] {
            DSL::Return();
        });

        DSL::Float4 camPos4 = DSL::ToFloat4(params.camPosAndTanHalfFov);
        DSL::Float4 camFwd4 = DSL::ToFloat4(params.camForwardAndAspect);
        DSL::Float4 camRight4 = DSL::ToFloat4(params.camRightAndNear);
        DSL::Float4 camUp4 = DSL::ToFloat4(params.camUpAndFar);
        DSL::Float4 sun4 = DSL::ToFloat4(params.sunDirAndIntensity);

        DSL::Float3 rayOrigin = DSL::MakeFloat3(camPos4.x, camPos4.y, camPos4.z);
        DSL::Float3 camFwd = DSL::MakeFloat3(camFwd4.x, camFwd4.y, camFwd4.z);
        DSL::Float3 camRight = DSL::MakeFloat3(camRight4.x, camRight4.y, camRight4.z);
        DSL::Float3 camUp = DSL::MakeFloat3(camUp4.x, camUp4.y, camUp4.z);
        DSL::Float3 lightDir = DSL::Normalize(-DSL::MakeFloat3(sun4.x, sun4.y, sun4.z));

        DSL::Float u = (DSL::Cast<float>(pix.x) + 0.5f) / DSL::Max(1.0f, DSL::Cast<float>(params.renderWidth));
        DSL::Float v = (DSL::Cast<float>(pix.y) + 0.5f) / DSL::Max(1.0f, DSL::Cast<float>(params.renderHeight));
        DSL::Float ndcX = 2.0f * u - 1.0f;
        DSL::Float ndcY = 1.0f - 2.0f * v;

        DSL::Float3 rayDir = DSL::Normalize(
            camFwd +
            (ndcX * camFwd4.w * camPos4.w) * camRight +
            (ndcY * camPos4.w) * camUp);

        DSL::Float4 domMin4 = DSL::ToFloat4(params.domainMinAndDx0);
        DSL::Float4 domMax4 = DSL::ToFloat4(params.domainMaxAndVisc0);
        DSL::Float3 boxMin = DSL::MakeFloat3(domMin4.x, domMin4.y, domMin4.z);
        DSL::Float3 boxMax = DSL::MakeFloat3(domMax4.x, domMax4.y, domMax4.z);

        DSL::Float3 invDir = 1.0f / selectIf(
            DSL::Abs(rayDir) < DSL::MakeFloat3(1e-6f),
            DSL::CopySign(DSL::MakeFloat3(1e-6f), rayDir),
            rayDir);
        DSL::Float3 t0s = (boxMin - rayOrigin) * invDir;
        DSL::Float3 t1s = (boxMax - rayOrigin) * invDir;
        DSL::Float3 tSmaller = DSL::Min(t0s, t1s);
        DSL::Float3 tBigger = DSL::Max(t0s, t1s);

        DSL::Float tEnter = DSL::Max(camRight4.w, DSL::Max(tSmaller.x, DSL::Max(tSmaller.y, tSmaller.z)));
        DSL::Float tExit = DSL::Min(camUp4.w, DSL::Min(tBigger.x, DSL::Min(tBigger.y, tBigger.z)));

        DSL::If(tExit <= tEnter, [&] {
            outRadiance.write(pix, DSL::MakeFloat4(0.0f));
            outHitDist.write(pix, DSL::MakeFloat4(1e9f, 0.0f, 0.0f, 1.0f));
            DSL::Return();
        });

        DSL::Float finestDx = domMin4.w / DSL::Cast<float>(1 << DSL::Max(0, params.maxAmrLevels - 1));
        DSL::Float baseStep = DSL::Max(finestDx * 0.65f, (tExit - tEnter) / 192.0f);
        DSL::Float densityGain = DSL::ToFloat4(params.liquidMaxRatioAndDensityScale).w;

        DSL::Float3 accumRgb = DSL::MakeFloat3(0.0f);
        DSL::Float transmittance = 1.0f;
        DSL::Float firstHitT = 1e9f;
        DSL::Float t = tEnter + 0.25f * baseStep;

        DSL::Bool renderWaterSurface = (params.simMode == 1) & (params.visMode == 4);

        DSL::If(renderWaterSurface, [&] {
            DSL::Float prevT = t;
            DSL::Float prevPhi = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, rayOrigin + rayDir * prevT);
            DSL::Bool hitSurface = (prevPhi >= 0.5f);
            DSL::Float hitT = prevT;

            DSL::Int stepIter = 0;
            DSL::While([&] { return (!hitSurface) & (t < tExit) & (stepIter < 220); }, [&] {
                stepIter = stepIter + 1;
                t = DSL::Min(t + baseStep, tExit);
                DSL::Float3 samplePos = rayOrigin + rayDir * t;

                // 检测内部固体障碍物命中
                DSL::Int leafLv;
                DSL::UInt leafType;
                DSL::Float4 leafM0, leafM2;
                DSL::Float2 leafVort;
                sampleFinestAmrLeaf(params, levelGrid, m0, m2, vortField, flags, samplePos,
                                    leafLv, leafType, leafM0, leafM2, leafVort);

                DSL::If(leafType == HomeLbmConstants::kCellSolid, [&] {
                    DSL::Float3 obsNormal = DSL::MakeFloat3(0.0f, 1.0f, 0.0f);
                    DSL::Float bestD = 1e6f;
                    DSL::For(0, params.primitiveCount, [&](DSL::Int pi) {
                        auto pr = primitives.read(pi);
                        DSL::If(pr.type <= HomeLbmConstants::kPrimObstacleCapsuleY, [&] {
                            DSL::Float d = DSL::Abs(evaluatePrimitiveSdf(samplePos, pr));
                            DSL::If(d < bestD, [&] {
                                bestD = d;
                                DSL::Float eps = finestDx * 0.5f;
                                DSL::Float gx = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(eps, 0.0f, 0.0f), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(eps, 0.0f, 0.0f), pr);
                                DSL::Float gy = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(0.0f, eps, 0.0f), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(0.0f, eps, 0.0f), pr);
                                DSL::Float gz = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(0.0f, 0.0f, eps), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(0.0f, 0.0f, eps), pr);
                                obsNormal = DSL::Normalize(DSL::MakeFloat3(gx, gy, gz) + DSL::MakeFloat3(1e-6f));
                            });
                        });
                    });
                    DSL::Float ndl = DSL::Max(0.18f, DSL::Dot(obsNormal, lightDir));
                    accumRgb = DSL::MakeFloat3(0.56f, 0.58f, 0.63f) * ndl;
                    transmittance = 0.0f;
                    firstHitT = t;
                    t = tExit + 1.0f;
                }).Else([&] {
                    DSL::Float curPhi = selectIf(
                        (leafType == HomeLbmConstants::kCellGas) & (prevPhi < 0.05f),
                        DSL::Def(0.0f),
                        sampleSmoothLiquidPhi(params, levelGrid, m2, flags, samplePos));
                    DSL::If(curPhi >= 0.5f, [&] {
                        DSL::Float lo = prevT;
                        DSL::Float hi = t;
                        DSL::StaticFor<0, 5>([&]<int64_t>() {
                            DSL::Float mid = 0.5f * (lo + hi);
                            DSL::Float mPhi = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, rayOrigin + rayDir * mid);
                            lo = selectIf(mPhi < 0.5f, mid, lo);
                            hi = selectIf(mPhi >= 0.5f, mid, hi);
                        });
                        hitSurface = true;
                        hitT = 0.5f * (lo + hi);
                    });
                    prevT = t;
                    prevPhi = curPhi;
                });
            });

            DSL::If(hitSurface, [&] {
                DSL::Float3 surfPos = rayOrigin + rayDir * hitT;
                DSL::Float eps = finestDx * 0.85f;
                DSL::Float dPhidx = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos + DSL::MakeFloat3(eps, 0.0f, 0.0f)) -
                                    sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos - DSL::MakeFloat3(eps, 0.0f, 0.0f));
                DSL::Float dPhidy = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos + DSL::MakeFloat3(0.0f, eps, 0.0f)) -
                                    sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos - DSL::MakeFloat3(0.0f, eps, 0.0f));
                DSL::Float dPhidz = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos + DSL::MakeFloat3(0.0f, 0.0f, eps)) -
                                    sampleSmoothLiquidPhi(params, levelGrid, m2, flags, surfPos - DSL::MakeFloat3(0.0f, 0.0f, eps));

                DSL::Float3 gradPhi = DSL::MakeFloat3(dPhidx, dPhidy, dPhidz);
                DSL::Float gradLen = DSL::Length(gradPhi);
                DSL::Float3 normal = selectIf(
                    gradLen > 1e-5f,
                    -gradPhi / gradLen,
                    -rayDir);
                normal = selectIf(DSL::Dot(normal, -rayDir) < 0.0f, -normal, normal);

                DSL::Float3 viewDir = -rayDir;
                DSL::Float ndotv = DSL::Clamp(DSL::Dot(normal, viewDir), 0.0f, 1.0f);
                DSL::Float fresnel = 0.03f + 0.97f * DSL::Pow(1.0f - ndotv, 5.0f);

                DSL::Float3 reflDir = DSL::Reflect(rayDir, normal);
                DSL::Float skyGrad = DSL::Clamp(0.5f * reflDir.y + 0.5f, 0.0f, 1.0f);
                DSL::Float3 skyCol = DSL::Lerp(
                    DSL::MakeFloat3(0.32f, 0.44f, 0.56f),
                    DSL::MakeFloat3(0.68f, 0.84f, 0.98f),
                    DSL::MakeFloat3(skyGrad));

                DSL::Float3 halfVec = DSL::Normalize(lightDir + viewDir);
                DSL::Float spec = DSL::Pow(DSL::Max(0.0f, DSL::Dot(normal, halfVec)), 96.0f) * sun4.w * 1.35f;

                // 水体内部厚度积分与 Beer-Lambert 吸收散射
                DSL::Float waterThickness = baseStep;
                DSL::Float wt = hitT + baseStep;
                DSL::Int wSteps = 0;
                DSL::While([&] { return (wt < tExit) & (wSteps < 48); }, [&] {
                    wSteps = wSteps + 1;
                    DSL::Float pIn = sampleSmoothLiquidPhi(params, levelGrid, m2, flags, rayOrigin + rayDir * wt);
                    waterThickness = waterThickness + selectIf(pIn >= 0.35f, baseStep * 1.5f, DSL::Def(0.0f));
                    wt = wt + baseStep * 1.5f;
                });

                DSL::Float3 sigmaA = DSL::MakeFloat3(0.55f, 0.14f, 0.05f) * densityGain * 0.45f;
                DSL::Float3 absorb = DSL::Exp(-sigmaA * waterThickness);
                DSL::Float3 deepWaterBody = DSL::MakeFloat3(0.04f, 0.34f, 0.48f) *
                                            (0.45f + 0.55f * DSL::Max(0.0f, DSL::Dot(normal, lightDir)));
                DSL::Float3 waterScatter = DSL::MakeFloat3(0.16f, 0.62f, 0.76f) * absorb;
                DSL::Float3 refrCol = DSL::Lerp(deepWaterBody, waterScatter, absorb);

                DSL::Float alpha = DSL::Clamp(
                    0.58f + 0.42f * fresnel + 0.25f * (1.0f - absorb.x),
                    0.58f,
                    0.97f);
                DSL::Float3 finalWaterRgb = DSL::Lerp(refrCol, skyCol, DSL::MakeFloat3(fresnel)) +
                                            DSL::MakeFloat3(spec);

                accumRgb = finalWaterRgb * alpha;
                transmittance = 1.0f - alpha;
                firstHitT = hitT;
            });
        }).Else([&] {
            DSL::Float4 inflow4 = DSL::ToFloat4(params.inflowVelAndSmag);
            DSL::Float refSpeed = DSL::Max(0.04f, DSL::Length(DSL::MakeFloat3(inflow4.x, inflow4.y, inflow4.z)) * 1.6f);
            DSL::Float refVort = DSL::Max(0.008f, DSL::ToFloat4(params.refineParams).x * 2.4f);

            DSL::Int stepIter = 0;
            DSL::While([&] { return (t < tExit) & (transmittance > 0.015f) & (stepIter < 200); }, [&] {
                stepIter = stepIter + 1;
                DSL::Float3 samplePos = rayOrigin + rayDir * t;

                DSL::Int leafLv;
                DSL::UInt leafType;
                DSL::Float4 leafM0, leafM2;
                DSL::Float2 leafVort;
                sampleFinestAmrLeaf(params, levelGrid, m0, m2, vortField, flags, samplePos,
                                    leafLv, leafType, leafM0, leafM2, leafVort);

                DSL::Float localStep = DSL::Max(finestDx * 0.5f, baseStep / DSL::Cast<float>(1 << DSL::Max(0, leafLv - 1)));

                DSL::If(leafType == HomeLbmConstants::kCellSolid, [&] {
                    DSL::Float3 obsNormal = DSL::MakeFloat3(0.0f, 1.0f, 0.0f);
                    DSL::Float bestD = 1e6f;
                    DSL::For(0, params.primitiveCount, [&](DSL::Int pi) {
                        auto pr = primitives.read(pi);
                        DSL::If(pr.type <= HomeLbmConstants::kPrimObstacleCapsuleY, [&] {
                            DSL::Float d = DSL::Abs(evaluatePrimitiveSdf(samplePos, pr));
                            DSL::If(d < bestD, [&] {
                                bestD = d;
                                DSL::Float eps = finestDx * 0.5f;
                                DSL::Float gx = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(eps, 0.0f, 0.0f), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(eps, 0.0f, 0.0f), pr);
                                DSL::Float gy = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(0.0f, eps, 0.0f), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(0.0f, eps, 0.0f), pr);
                                DSL::Float gz = evaluatePrimitiveSdf(samplePos + DSL::MakeFloat3(0.0f, 0.0f, eps), pr) -
                                                evaluatePrimitiveSdf(samplePos - DSL::MakeFloat3(0.0f, 0.0f, eps), pr);
                                obsNormal = DSL::Normalize(DSL::MakeFloat3(gx, gy, gz) + DSL::MakeFloat3(1e-6f));
                            });
                        });
                    });

                    DSL::Float ndl = DSL::Max(0.20f, DSL::Dot(obsNormal, lightDir));
                    DSL::Float3 obsCol = DSL::MakeFloat3(0.55f, 0.58f, 0.64f) * ndl;
                    firstHitT = DSL::Min(firstHitT, t);
                    accumRgb = accumRgb + transmittance * obsCol;
                    transmittance = 0.0f;
                }).Else([&] {
                    DSL::Float densityVal = 0.0f;
                    DSL::Float3 emissionCol = DSL::MakeFloat3(0.85f, 0.88f, 0.92f);

                    DSL::If(params.visMode == 0, [&] {
                        densityVal = DSL::Max(0.0f, leafM2.w) * densityGain;
                        DSL::Float shade = 0.65f + 0.35f * DSL::Exp(-densityVal * 0.5f);
                        emissionCol = DSL::MakeFloat3(0.88f, 0.90f, 0.94f) * shade;
                    }).ElseIf(params.visMode == 1, [&] {
                        DSL::Float normV = DSL::Clamp(leafVort.x / refVort, 0.0f, 1.0f);
                        densityVal = DSL::SmoothStep(0.08f, 0.95f, normV) * densityGain * 1.15f;
                        emissionCol = evalScientificColormap(normV) * 1.15f;
                    }).ElseIf(params.visMode == 2, [&] {
                        DSL::Float qNorm = DSL::Clamp(leafVort.y / DSL::Max(refVort * refVort * 0.35f, 1e-6f), 0.0f, 1.0f);
                        densityVal = DSL::SmoothStep(0.05f, 0.90f, qNorm) * densityGain * 1.35f;
                        emissionCol = evalScientificColormap(0.25f + 0.75f * qNorm) * 1.2f;
                    }).Else([&] {
                        DSL::Float3 uVec = DSL::MakeFloat3(leafM0.y, leafM0.z, leafM0.w);
                        DSL::Float sNorm = DSL::Clamp(DSL::Length(uVec) / refSpeed, 0.0f, 1.0f);
                        DSL::Float activeMask = selectIf(
                            (params.simMode == 1) & (leafType == HomeLbmConstants::kCellGas),
                            DSL::Def(0.0f),
                            DSL::Def(1.0f));
                        densityVal = activeMask * DSL::SmoothStep(0.12f, 0.98f, sNorm) * densityGain * 0.75f;
                        emissionCol = evalScientificColormap(sNorm);
                    });

                    DSL::If(densityVal > 0.004f, [&] {
                        firstHitT = DSL::Min(firstHitT, t);
                        DSL::Float stepAlpha = 1.0f - DSL::Exp(-densityVal * localStep);
                        accumRgb = accumRgb + (transmittance * stepAlpha) * emissionCol;
                        transmittance = transmittance * (1.0f - stepAlpha);
                    });
                });

                t = t + localStep;
            });
        });

        DSL::Float outAlpha = DSL::Clamp(1.0f - transmittance, 0.0f, 1.0f);
        outRadiance.write(pix, DSL::MakeFloat4(accumRgb.x, accumRgb.y, accumRgb.z, outAlpha));
        outHitDist.write(pix, DSL::MakeFloat4(firstHitT, 0.0f, 0.0f, 1.0f));
    });
}

} // namespace

struct HomeLbmKernels::Impl {
    using InitDomainKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildInitDomainKernel()));
    using VoxelizeKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildVoxelizeAndInjectKernel()));
    using SnapshotKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildSnapshotMomentsKernel()));
    using ProlongateKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildProlongateBlocksKernel()));
    using FillHalosKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildFillPaddedHalosKernel()));
    using StreamCollideKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildStreamAndCollideKernel()));
    using FillTransHaloKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildFillTransitionHaloKernel()));
    using FsMarkKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildFsMarkCandidatesKernel()));
    using FsResolveKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildFsResolveTopologyKernel()));
    using FsGatherKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildFsGatherExcessMassKernel()));
    using CoalesceKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildCoalesceFineToCoarseKernel()));
    using ClearSensorsKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildClearBlockSensorsKernel()));
    using VortSensorsKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildComputeVorticityAndSensorsKernel()));
    using RaymarchKernelT = decltype(std::declval<FCT::Compute::ComputeDevice&>().compile(buildRaymarchVolumeKernel()));

    InitDomainKernelT initDomain;
    VoxelizeKernelT voxelizeAndInject;
    SnapshotKernelT snapshotMoments;
    ProlongateKernelT prolongateBlocks;
    FillHalosKernelT fillPaddedHalos;
    StreamCollideKernelT streamAndCollide;
    FillTransHaloKernelT fillTransitionHalo;
    FsMarkKernelT fsMarkCandidates;
    FsResolveKernelT fsResolveTopology;
    FsGatherKernelT fsGatherExcessMass;
    CoalesceKernelT coalesceFineToCoarse;
    ClearSensorsKernelT clearBlockSensors;
    VortSensorsKernelT computeVorticityAndSensors;
    RaymarchKernelT raymarchVolume;
};

HomeLbmKernels::HomeLbmKernels()
    : m_impl(makeUnique<Impl>()) {}

HomeLbmKernels::~HomeLbmKernels() = default;

Status HomeLbmKernels::compileAll(FCT::Compute::ComputeDevice& device) {
    if (!device.valid()) {
        return InvalidArgumentError("编译 HomeLbmKernels 失败：ComputeDevice 无效");
    }

    m_impl->initDomain = device.compile(buildInitDomainKernel());
    m_impl->voxelizeAndInject = device.compile(buildVoxelizeAndInjectKernel());
    m_impl->snapshotMoments = device.compile(buildSnapshotMomentsKernel());
    m_impl->prolongateBlocks = device.compile(buildProlongateBlocksKernel());
    m_impl->fillPaddedHalos = device.compile(buildFillPaddedHalosKernel());
    m_impl->streamAndCollide = device.compile(buildStreamAndCollideKernel());
    m_impl->fillTransitionHalo = device.compile(buildFillTransitionHaloKernel());
    m_impl->fsMarkCandidates = device.compile(buildFsMarkCandidatesKernel());
    m_impl->fsResolveTopology = device.compile(buildFsResolveTopologyKernel());
    m_impl->fsGatherExcessMass = device.compile(buildFsGatherExcessMassKernel());
    m_impl->coalesceFineToCoarse = device.compile(buildCoalesceFineToCoarseKernel());
    m_impl->clearBlockSensors = device.compile(buildClearBlockSensorsKernel());
    m_impl->computeVorticityAndSensors = device.compile(buildComputeVorticityAndSensorsKernel());
    m_impl->raymarchVolume = device.compile(buildRaymarchVolumeKernel());

    if (!isCompiled()) {
        return InternalError("编译 HomeLbmKernels 失败：至少一个 GPU Kernel 未成功生成原生 Shader");
    }
    return OkStatus();
}

void HomeLbmKernels::release() noexcept {
    if (m_impl) {
        *m_impl = Impl{};
    }
}

bool HomeLbmKernels::isCompiled() const noexcept {
    return m_impl &&
           m_impl->initDomain.valid() &&
           m_impl->voxelizeAndInject.valid() &&
           m_impl->snapshotMoments.valid() &&
           m_impl->prolongateBlocks.valid() &&
           m_impl->fillPaddedHalos.valid() &&
           m_impl->streamAndCollide.valid() &&
           m_impl->fillTransitionHalo.valid() &&
           m_impl->fsMarkCandidates.valid() &&
           m_impl->fsResolveTopology.valid() &&
           m_impl->fsGatherExcessMass.valid() &&
           m_impl->coalesceFineToCoarse.valid() &&
           m_impl->clearBlockSensors.valid() &&
           m_impl->computeVorticityAndSensors.valid() &&
           m_impl->raymarchVolume.valid();
}

void HomeLbmKernels::enqueueInitDomain(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t threads = activeBlockCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->initDomain.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.primitivesBuf, bufs.blockMetaBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2, bufs.aux, bufs.flags,
        bufs.prevM0, bufs.prevM1, bufs.prevM2, bufs.vortField,
        static_cast<int32_t>(activeBlockCount)));
}

void HomeLbmKernels::enqueueVoxelizeAndInject(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount,
    int32_t currentLevel) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t threads = activeBlockCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->voxelizeAndInject.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.primitivesBuf, bufs.blockMetaBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2, bufs.aux, bufs.flags,
        static_cast<int32_t>(activeBlockCount), currentLevel));
}

void HomeLbmKernels::enqueueSnapshotMoments(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t threads = activeBlockCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->snapshotMoments.dispatch1D(
        threads,
        bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2,
        bufs.prevM0, bufs.prevM1, bufs.prevM2,
        static_cast<int32_t>(activeBlockCount)));
}

void HomeLbmKernels::enqueueProlongateBlocks(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t newlyRefinedCount) const {
    if (!bufs.valid() || newlyRefinedCount == 0u) return;
    const uint32_t threads = newlyRefinedCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->prolongateBlocks.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2,
        bufs.prevM0, bufs.prevM1, bufs.prevM2,
        bufs.aux, bufs.flags,
        static_cast<int32_t>(newlyRefinedCount)));
}

void HomeLbmKernels::enqueueFillPaddedHalos(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount,
    int32_t currentLevel,
    float temporalAlpha) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t threads = activeBlockCount * HomeLbmConstants::kPaddedCells;
    batch.add(m_impl->fillPaddedHalos.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2,
        bufs.prevM0, bufs.prevM1, bufs.prevM2,
        bufs.aux, bufs.flags,
        bufs.padM0, bufs.padM1, bufs.padM2, bufs.padAux, bufs.padFlags,
        static_cast<int32_t>(activeBlockCount), currentLevel, temporalAlpha));
}

void HomeLbmKernels::enqueueStreamAndCollide(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount,
    int32_t currentLevel) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t threads = activeBlockCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->streamAndCollide.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.activeIdsBuf,
        bufs.padM0, bufs.padM1, bufs.padM2, bufs.padAux, bufs.padFlags,
        bufs.m0, bufs.m1, bufs.m2, bufs.aux, bufs.flags,
        static_cast<int32_t>(activeBlockCount), currentLevel));
}

void HomeLbmKernels::enqueueFreeSurfaceTransitions(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount,
    int32_t currentLevel,
    float temporalAlpha) const {
    (void)temporalAlpha;
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const uint32_t cellThreads = activeBlockCount * HomeLbmConstants::kBlockCells;
    const uint32_t padThreads = activeBlockCount * HomeLbmConstants::kPaddedCells;
    const int32_t countI = static_cast<int32_t>(activeBlockCount);

    // 1. 刷新碰撞后的密度、体积分数与单元类型 Halo，标记本步待转化候选集
    batch.add(m_impl->fillTransitionHalo.dispatch1D(
        padThreads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m2, bufs.flags, bufs.excessShareBuf,
        bufs.padM0, bufs.padM2, bufs.padFlags, bufs.padExcessShareBuf,
        countI, currentLevel));
    batch.add(m_impl->fsMarkCandidates.dispatch1D(
        cellThreads,
        bufs.activeIdsBuf, bufs.padM2, bufs.padFlags, bufs.flags, bufs.excessShareBuf, countI));

    // 2. 刷新候选标志位 Halo，执行拓扑状态跃迁并计算每单元精确均摊盈亏质量份额
    batch.add(m_impl->fillTransitionHalo.dispatch1D(
        padThreads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m2, bufs.flags, bufs.excessShareBuf,
        bufs.padM0, bufs.padM2, bufs.padFlags, bufs.padExcessShareBuf,
        countI, currentLevel));
    batch.add(m_impl->fsResolveTopology.dispatch1D(
        cellThreads,
        bufs.activeIdsBuf, bufs.padM0, bufs.padM2, bufs.padFlags,
        bufs.m0, bufs.m1, bufs.m2, bufs.flags, bufs.excessShareBuf, countI));

    // 3. 刷新均摊份额 Halo，由接收界面单元无锁确定性汇集全部盈亏质量
    batch.add(m_impl->fillTransitionHalo.dispatch1D(
        padThreads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m2, bufs.flags, bufs.excessShareBuf,
        bufs.padM0, bufs.padM2, bufs.padFlags, bufs.padExcessShareBuf,
        countI, currentLevel));
    batch.add(m_impl->fsGatherExcessMass.dispatch1D(
        cellThreads,
        bufs.activeIdsBuf, bufs.m0, bufs.m2, bufs.padFlags, bufs.padExcessShareBuf, bufs.flags, countI));
}

void HomeLbmKernels::enqueueCoalesceFineToCoarse(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t coarseActiveBlockCount,
    int32_t coarseLevel) const {
    if (!bufs.valid() || coarseActiveBlockCount == 0u) return;
    const uint32_t threads = coarseActiveBlockCount * HomeLbmConstants::kBlockCells;
    batch.add(m_impl->coalesceFineToCoarse.dispatch1D(
        threads,
        bufs.paramsBuf, bufs.blockMetaBuf, bufs.levelGridBuf, bufs.activeIdsBuf,
        bufs.m0, bufs.m1, bufs.m2, bufs.aux, bufs.flags,
        static_cast<int32_t>(coarseActiveBlockCount), coarseLevel));
}

void HomeLbmKernels::enqueueComputeVorticityAndSensors(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    uint32_t activeBlockCount,
    int32_t currentLevel) const {
    if (!bufs.valid() || activeBlockCount == 0u) return;
    const int32_t countI = static_cast<int32_t>(activeBlockCount);
    batch.add(m_impl->clearBlockSensors.dispatch1D(
        activeBlockCount,
        bufs.activeIdsBuf, bufs.blockSensors, countI));
    batch.add(m_impl->computeVorticityAndSensors.dispatch1D(
        activeBlockCount * HomeLbmConstants::kBlockCells,
        bufs.paramsBuf, bufs.activeIdsBuf,
        bufs.padM0, bufs.padAux, bufs.padFlags,
        bufs.vortField, bufs.blockSensors,
        countI, currentLevel));
}

void HomeLbmKernels::enqueueRaymarchVolume(
    FCT::Compute::CommandBatch& batch,
    const HomeLbmGpuBuffers& bufs,
    const FCT::Compute::FormatImage<FCT::Format::R16G16B16A16_SFLOAT>& outRadiance,
    const FCT::Compute::FormatImage<FCT::Format::R32_SFLOAT>& outHitDist,
    uint32_t width,
    uint32_t height) const {
    if (width == 0u || height == 0u || !bufs.valid() || !outRadiance.valid() || !outHitDist.valid()) {
        return;
    }
    batch.add(m_impl->raymarchVolume.dispatch2D(
        width, height,
        bufs.paramsBuf, bufs.primitivesBuf, bufs.levelGridBuf,
        bufs.m0, bufs.m2, bufs.vortField, bufs.flags,
        outRadiance, outHitDist));
}

} // namespace MQEngine
