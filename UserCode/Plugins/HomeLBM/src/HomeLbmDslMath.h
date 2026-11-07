#ifndef MQENGINE_HOMELBM_DSL_MATH_H
#define MQENGINE_HOMELBM_DSL_MATH_H

#include "HomeLbmKernels.h"

namespace MQEngine::HomeLbmDsl {

namespace DSL = FCT::Compute::DSL;

template<typename Pred, typename TVal, typename FVal>
[[nodiscard]] inline auto selectIf(Pred&& cond, TVal&& trueVal, FVal&& falseVal) noexcept {
    return DSL::Select(
        std::forward<FVal>(falseVal),
        std::forward<TVal>(trueVal),
        std::forward<Pred>(cond));
}

[[nodiscard]] inline DSL::Int interiorIndex(DSL::Int blockId, DSL::Int lx, DSL::Int ly, DSL::Int lz) noexcept {
    return blockId * HomeLbmConstants::kBlockCells + ((lz << 3) + ly) * HomeLbmConstants::kBlockEdge + lx;
}

[[nodiscard]] inline DSL::Int paddedIndex(DSL::Int blockId, DSL::Int px, DSL::Int py, DSL::Int pz) noexcept {
    return blockId * HomeLbmConstants::kPaddedCells +
           (pz * HomeLbmConstants::kPaddedEdge + py) * HomeLbmConstants::kPaddedEdge + px;
}

[[nodiscard]] inline DSL::Int levelGridOffsetFor(const DSL::Var<LbmGpuParams>& params, DSL::Int level) noexcept {
    DSL::Int off = params.levelGridOffset0;
    off = selectIf(level == 1, params.levelGridOffset1, off);
    off = selectIf(level == 2, params.levelGridOffset2, off);
    return off;
}

[[nodiscard]] inline DSL::Int lookupAmrBlockId(
    const DSL::Var<LbmGpuParams>& params,
    const DSL::BufferVar<int32_t>& levelGrid,
    DSL::Int level,
    DSL::Int bx,
    DSL::Int by,
    DSL::Int bz) noexcept {
    DSL::Int nx = params.baseBlocksX << level;
    DSL::Int ny = params.baseBlocksY << level;
    DSL::Int nz = params.baseBlocksZ << level;

    DSL::Bool inBounds =
        (level >= 0) & (level < params.maxAmrLevels) &
        (bx >= 0) & (bx < nx) &
        (by >= 0) & (by < ny) &
        (bz >= 0) & (bz < nz);

    DSL::Int clampedX = DSL::Clamp(bx, 0, DSL::Max(0, nx - 1));
    DSL::Int clampedY = DSL::Clamp(by, 0, DSL::Max(0, ny - 1));
    DSL::Int clampedZ = DSL::Clamp(bz, 0, DSL::Max(0, nz - 1));
    DSL::Int flatIdx = levelGridOffsetFor(params, level) + (clampedZ * ny + clampedY) * nx + clampedX;
    DSL::Int rawId = levelGrid.read(flatIdx);
    return selectIf(inBounds, rawId, DSL::Def(-1));
}

[[nodiscard]] inline DSL::Float levelCellSize(const DSL::Var<LbmGpuParams>& params, DSL::Int level) noexcept {
    DSL::Float4 minAndDx0 = DSL::ToFloat4(params.domainMinAndDx0);
    DSL::Float scale = DSL::Cast<float>(1 << level);
    return minAndDx0.w / scale;
}

[[nodiscard]] inline DSL::Float3 cellCenterWorld(
    const DSL::Var<LbmGpuParams>& params,
    DSL::Int level,
    DSL::Int gx,
    DSL::Int gy,
    DSL::Int gz) noexcept {
    DSL::Float4 minAndDx0 = DSL::ToFloat4(params.domainMinAndDx0);
    DSL::Float dx = levelCellSize(params, level);
    return DSL::MakeFloat3(
        minAndDx0.x + (DSL::Cast<float>(gx) + 0.5f) * dx,
        minAndDx0.y + (DSL::Cast<float>(gy) + 0.5f) * dx,
        minAndDx0.z + (DSL::Cast<float>(gz) + 0.5f) * dx);
}

[[nodiscard]] inline DSL::Float3 levelBaseAcceleration(
    const DSL::Var<LbmGpuParams>& params,
    DSL::Int level,
    DSL::Float temperature) noexcept {
    DSL::Float4 gravBuoy = DSL::ToFloat4(params.gravityAndBuoyancy);
    DSL::Float invScale = 1.0f / DSL::Cast<float>(1 << level);
    DSL::Float ay = gravBuoy.y + selectIf(params.simMode == 0, gravBuoy.w * temperature, DSL::Def(0.0f));
    return DSL::MakeFloat3(gravBuoy.x, ay, gravBuoy.z) * invScale;
}

[[nodiscard]] inline DSL::Float sdfSphere(DSL::Float3 p, DSL::Float3 center, DSL::Float radius) noexcept {
    return DSL::Length(p - center) - radius;
}

[[nodiscard]] inline DSL::Float sdfBox(DSL::Float3 p, DSL::Float3 center, DSL::Float3 halfExt) noexcept {
    DSL::Float3 q = DSL::Abs(p - center) - halfExt;
    DSL::Float3 qPos = DSL::Max(q, DSL::MakeFloat3(0.0f));
    DSL::Float outsideDist = DSL::Length(qPos);
    DSL::Float insideDist = DSL::Min(DSL::Max(q.x, DSL::Max(q.y, q.z)), 0.0f);
    return outsideDist + insideDist;
}

[[nodiscard]] inline DSL::Float sdfCapsuleY(
    DSL::Float3 p,
    DSL::Float3 center,
    DSL::Float radius,
    DSL::Float halfHeight) noexcept {
    DSL::Float3 local = p - center;
    local.y = local.y - DSL::Clamp(local.y, -halfHeight, halfHeight);
    return DSL::Length(local) - radius;
}

[[nodiscard]] inline DSL::Float evaluatePrimitiveSdf(
    DSL::Float3 worldPos,
    const DSL::Var<LbmGpuPrimitive>& prim) noexcept {
    DSL::Float4 posRad = DSL::ToFloat4(prim.posAndRadius);
    DSL::Float4 extH = DSL::ToFloat4(prim.extentsAndHeight);
    DSL::Float3 center = DSL::MakeFloat3(posRad.x, posRad.y, posRad.z);
    DSL::Float3 halfExt = DSL::MakeFloat3(extH.x, extH.y, extH.z);

    DSL::Float dSphere = sdfSphere(worldPos, center, posRad.w);
    DSL::Float dBox = sdfBox(worldPos, center, halfExt);
    DSL::Float dCaps = sdfCapsuleY(worldPos, center, posRad.w, extH.w);

    DSL::Bool isBox = (prim.type == HomeLbmConstants::kPrimObstacleBox) |
                      (prim.type == HomeLbmConstants::kPrimEmitterBox);
    DSL::Bool isCaps = (prim.type == HomeLbmConstants::kPrimObstacleCapsuleY);
    DSL::Float dist = selectIf(isBox, dBox, dSphere);
    dist = selectIf(isCaps, dCaps, dist);
    return dist;
}

/**
 * @brief 三阶无分布函数 Hermite 分布重构 (SIGGRAPH 2026 Course Eq. 86)
 * @param m1 (Sxx, Syy, Szz, Sxy) 去压归一化二阶矩
 * @param s2 (Sxz, Syz) 去压归一化二阶剪切矩
 */
template<int64_t Dir>
[[nodiscard]] inline DSL::Float reconstructHermiteDir(
    DSL::Float rho,
    DSL::Float3 u,
    DSL::Float4 m1,
    DSL::Float2 s2) noexcept {
    constexpr float w = HomeLbmConstants::kWeight[Dir];
    constexpr float cx = static_cast<float>(HomeLbmConstants::kCx[Dir]);
    constexpr float cy = static_cast<float>(HomeLbmConstants::kCy[Dir]);
    constexpr float cz = static_cast<float>(HomeLbmConstants::kCz[Dir]);
    constexpr float hxx = cx * cx - (1.0f / 3.0f);
    constexpr float hyy = cy * cy - (1.0f / 3.0f);
    constexpr float hzz = cz * cz - (1.0f / 3.0f);

    DSL::Float cu = cx * u.x + cy * u.y + cz * u.z;

    DSL::Float second =
        hxx * m1.x + hyy * m1.y + hzz * m1.z +
        2.0f * (cx * cy * m1.w + cx * cz * s2.x + cy * cz * s2.y);

    DSL::Float ux2 = u.x * u.x;
    DSL::Float uy2 = u.y * u.y;
    DSL::Float uz2 = u.z * u.z;

    DSL::Float axxy = m1.x * u.y + 2.0f * m1.w * u.x - 2.0f * ux2 * u.y;
    DSL::Float axyy = m1.y * u.x + 2.0f * m1.w * u.y - 2.0f * uy2 * u.x;
    DSL::Float axxz = m1.x * u.z + 2.0f * s2.x * u.x - 2.0f * ux2 * u.z;
    DSL::Float axzz = m1.z * u.x + 2.0f * s2.x * u.z - 2.0f * uz2 * u.x;
    DSL::Float ayzz = m1.z * u.y + 2.0f * s2.y * u.z - 2.0f * uz2 * u.y;
    DSL::Float ayyz = m1.y * u.z + 2.0f * s2.y * u.y - 2.0f * uy2 * u.z;
    DSL::Float axyz = s2.x * u.y + s2.y * u.x + m1.w * u.z - 2.0f * u.x * u.y * u.z;

    DSL::Float third =
        hxx * cy * axxy + hyy * cx * axyy +
        hxx * cz * axxz + hzz * cx * axzz +
        hzz * cy * ayzz + hyy * cz * ayyz +
        2.0f * cx * cy * cz * axyz;

    return (w * rho) * (1.0f + 3.0f * cu + 4.5f * second + 13.5f * third);
}

template<int64_t Dir>
[[nodiscard]] inline DSL::Float reconstructEquilibriumDir(DSL::Float rho, DSL::Float3 u) noexcept {
    DSL::Float4 sEq1 = DSL::MakeFloat4(u.x * u.x, u.y * u.y, u.z * u.z, u.x * u.y);
    DSL::Float2 sEq2 = DSL::MakeFloat2(u.x * u.z, u.y * u.z);
    return reconstructHermiteDir<Dir>(rho, u, sEq1, sEq2);
}

/**
 * @brief HOME-LBM 闭式二阶矩碰撞与 Smagorinsky 亚格子湍流松弛 (Eq. 89-91)
 */
inline void collideHomeCell(
    DSL::Float rhoStar,
    DSL::Float3 uStar,
    DSL::Float4 s1Star,
    DSL::Float2 s2Star,
    DSL::Float3 bodyForce,
    DSL::Float nuBaseLevel,
    DSL::Float smagCs,
    DSL::Float4& outM0,
    DSL::Float4& outM1,
    DSL::Float2& outS2) noexcept {
    DSL::Float rhoSafe = DSL::Clamp(rhoStar, 0.35f, 2.50f);
    DSL::Float speedSq = DSL::Dot(uStar, uStar);
    DSL::Float maxMachSpeed = 0.32f;
    DSL::Float speedScale = selectIf(
        speedSq > (maxMachSpeed * maxMachSpeed),
        maxMachSpeed * DSL::Rsqrt(DSL::Max(speedSq, 1e-12f)),
        DSL::Def(1.0f));
    DSL::Float3 u = uStar * speedScale;

    // 非平衡应力张量 Frobenius 范数驱动局部涡粘性
    DSL::Float neqXX = s1Star.x - u.x * u.x;
    DSL::Float neqYY = s1Star.y - u.y * u.y;
    DSL::Float neqZZ = s1Star.z - u.z * u.z;
    DSL::Float neqXY = s1Star.w - u.x * u.y;
    DSL::Float neqXZ = s2Star.x - u.x * u.z;
    DSL::Float neqYZ = s2Star.y - u.y * u.z;

    DSL::Float neqNorm = DSL::Sqrt(DSL::Max(
        0.0f,
        neqXX * neqXX + neqYY * neqYY + neqZZ * neqZZ +
        2.0f * (neqXY * neqXY + neqXZ * neqXZ + neqYZ * neqYZ)));

    DSL::Float nuEff = nuBaseLevel + (smagCs * smagCs) * 3.0f * neqNorm;
    DSL::Float tau = DSL::Clamp(3.0f * nuEff + 0.5f, 0.506f, 1.95f);
    DSL::Float invTau = 1.0f / tau;
    DSL::Float c1 = (tau - 1.0f) / (3.0f * tau);
    DSL::Float invRho = 1.0f / rhoSafe;

    DSL::Float ux2 = u.x * u.x;
    DSL::Float uy2 = u.y * u.y;
    DSL::Float uz2 = u.z * u.z;
    DSL::Float uSqDiv3 = (ux2 + uy2 + uz2) * (1.0f / 3.0f);

    DSL::Float fxUx = bodyForce.x * u.x;
    DSL::Float fyUy = bodyForce.y * u.y;
    DSL::Float fzUz = bodyForce.z * u.z;

    DSL::Float sxxNew =
        c1 * (2.0f * s1Star.x - s1Star.y - s1Star.z) +
        uSqDiv3 + (invTau * (1.0f / 3.0f)) * (2.0f * ux2 - uy2 - uz2) +
        invRho * fxUx + (c1 * invRho) * (2.0f * fxUx - fyUy - fzUz);

    DSL::Float syyNew =
        c1 * (2.0f * s1Star.y - s1Star.z - s1Star.x) +
        uSqDiv3 + (invTau * (1.0f / 3.0f)) * (2.0f * uy2 - uz2 - ux2) +
        invRho * fyUy + (c1 * invRho) * (2.0f * fyUy - fzUz - fxUx);

    DSL::Float szzNew =
        c1 * (2.0f * s1Star.z - s1Star.x - s1Star.y) +
        uSqDiv3 + (invTau * (1.0f / 3.0f)) * (2.0f * uz2 - ux2 - uy2) +
        invRho * fzUz + (c1 * invRho) * (2.0f * fzUz - fxUx - fyUy);

    DSL::Float forceShearWeight = (2.0f * tau - 1.0f) * (0.5f * invTau * invRho);
    DSL::Float oneMinusInvTau = 1.0f - invTau;

    DSL::Float sxyNew = oneMinusInvTau * s1Star.w + invTau * (u.x * u.y) +
                        forceShearWeight * (bodyForce.x * u.y + bodyForce.y * u.x);
    DSL::Float sxzNew = oneMinusInvTau * s2Star.x + invTau * (u.x * u.z) +
                        forceShearWeight * (bodyForce.x * u.z + bodyForce.z * u.x);
    DSL::Float syzNew = oneMinusInvTau * s2Star.y + invTau * (u.y * u.z) +
                        forceShearWeight * (bodyForce.y * u.z + bodyForce.z * u.y);

    DSL::Float3 uStored = u + (0.5f * invRho) * bodyForce;
    outM0 = DSL::MakeFloat4(rhoSafe, uStored.x, uStored.y, uStored.z);
    outM1 = DSL::MakeFloat4(sxxNew, syyNew, szzNew, sxyNew);
    outS2 = DSL::MakeFloat2(sxzNew, syzNew);
}

/**
 * @brief 粗网格到细网格时空三线性插值与二阶非平衡矩重标度 (Eq. 76)
 */
inline void prolongateCoarseToFineMoments(
    const DSL::Var<LbmGpuParams>& params,
    const DSL::BufferVar<int32_t>& levelGrid,
    const DSL::BufferVar<FCT::Vec4>& m0,
    const DSL::BufferVar<FCT::Vec4>& m1,
    const DSL::BufferVar<FCT::Vec4>& m2,
    const DSL::BufferVar<FCT::Vec4>& prevM0,
    const DSL::BufferVar<FCT::Vec4>& prevM1,
    const DSL::BufferVar<FCT::Vec4>& prevM2,
    const DSL::BufferVar<FCT::Vec4>& aux,
    const DSL::BufferVar<uint32_t>& flags,
    DSL::Int fineLevel,
    DSL::Int fgx,
    DSL::Int fgy,
    DSL::Int fgz,
    DSL::Float alphaT,
    DSL::Float4& outM0,
    DSL::Float4& outM1,
    DSL::Float4& outM2,
    DSL::Float4& outAux,
    DSL::UInt& outFlag) noexcept {
    DSL::Int coarseLevel = DSL::Max(0, fineLevel - 1);
    DSL::Int cNx = (params.baseBlocksX << coarseLevel) * HomeLbmConstants::kBlockEdge;
    DSL::Int cNy = (params.baseBlocksY << coarseLevel) * HomeLbmConstants::kBlockEdge;
    DSL::Int cNz = (params.baseBlocksZ << coarseLevel) * HomeLbmConstants::kBlockEdge;

    DSL::Float cxCoord = DSL::Clamp(0.5f * DSL::Cast<float>(fgx) - 0.25f, 0.0f, DSL::Cast<float>(cNx - 1));
    DSL::Float cyCoord = DSL::Clamp(0.5f * DSL::Cast<float>(fgy) - 0.25f, 0.0f, DSL::Cast<float>(cNy - 1));
    DSL::Float czCoord = DSL::Clamp(0.5f * DSL::Cast<float>(fgz) - 0.25f, 0.0f, DSL::Cast<float>(cNz - 1));

    DSL::Int ix0 = DSL::Cast<int32_t>(DSL::Floor(cxCoord));
    DSL::Int iy0 = DSL::Cast<int32_t>(DSL::Floor(cyCoord));
    DSL::Int iz0 = DSL::Cast<int32_t>(DSL::Floor(czCoord));
    DSL::Int ix1 = DSL::Min(ix0 + 1, cNx - 1);
    DSL::Int iy1 = DSL::Min(iy0 + 1, cNy - 1);
    DSL::Int iz1 = DSL::Min(iz0 + 1, cNz - 1);

    DSL::Float tx = cxCoord - DSL::Cast<float>(ix0);
    DSL::Float ty = cyCoord - DSL::Cast<float>(iy0);
    DSL::Float tz = czCoord - DSL::Cast<float>(iz0);

    DSL::Float4 accM0 = DSL::MakeFloat4(0.0f);
    DSL::Float4 accM1 = DSL::MakeFloat4(0.0f);
    DSL::Float4 accM2 = DSL::MakeFloat4(0.0f);
    DSL::Float4 accAux = DSL::MakeFloat4(0.0f);
    DSL::Float weightSum = 0.0f;

    // 最近邻粗单元类型决定拓扑标识（防止自由液面/固体离散标志被插值模糊）
    DSL::Int nearX = DSL::Clamp(fgx >> 1, 0, cNx - 1);
    DSL::Int nearY = DSL::Clamp(fgy >> 1, 0, cNy - 1);
    DSL::Int nearZ = DSL::Clamp(fgz >> 1, 0, cNz - 1);
    DSL::Int nearBlk = DSL::Max(0, lookupAmrBlockId(params, levelGrid, coarseLevel, nearX >> 3, nearY >> 3, nearZ >> 3));
    DSL::Int nearIdx = interiorIndex(nearBlk, nearX & 7, nearY & 7, nearZ & 7);
    DSL::UInt nearFlag = flags.read(nearIdx) & HomeLbmConstants::kCellTypeMask;

    DSL::StaticFor<0, 8>([&]<int64_t Corner>() {
        constexpr int ox = static_cast<int>(Corner & 1);
        constexpr int oy = static_cast<int>((Corner >> 1) & 1);
        constexpr int oz = static_cast<int>((Corner >> 2) & 1);

        DSL::Int gx = (ox == 0) ? ix0 : ix1;
        DSL::Int gy = (oy == 0) ? iy0 : iy1;
        DSL::Int gz = (oz == 0) ? iz0 : iz1;
        DSL::Float wx = (ox == 0) ? (1.0f - tx) : tx;
        DSL::Float wy = (oy == 0) ? (1.0f - ty) : ty;
        DSL::Float wz = (oz == 0) ? (1.0f - tz) : tz;
        DSL::Float w = wx * wy * wz;

        DSL::Int cBlk = lookupAmrBlockId(params, levelGrid, coarseLevel, gx >> 3, gy >> 3, gz >> 3);
        DSL::Bool validBlk = (cBlk >= 0);
        DSL::Int safeBlk = DSL::Max(0, cBlk);
        DSL::Int cIdx = interiorIndex(safeBlk, gx & 7, gy & 7, gz & 7);

        DSL::UInt cType = flags.read(cIdx) & HomeLbmConstants::kCellTypeMask;
        DSL::Bool usable = validBlk & ((nearFlag == HomeLbmConstants::kCellSolid) |
                                       (cType != HomeLbmConstants::kCellSolid));
        DSL::Float effW = selectIf(usable, w, DSL::Def(0.0f));

        DSL::Float4 cm0 = DSL::Lerp(prevM0.readVec(cIdx), m0.readVec(cIdx), DSL::MakeFloat4(alphaT));
        DSL::Float4 cm1 = DSL::Lerp(prevM1.readVec(cIdx), m1.readVec(cIdx), DSL::MakeFloat4(alphaT));
        DSL::Float4 cm2 = DSL::Lerp(prevM2.readVec(cIdx), m2.readVec(cIdx), DSL::MakeFloat4(alphaT));
        DSL::Float4 cAux = aux.readVec(cIdx);

        accM0 = accM0 + effW * cm0;
        accM1 = accM1 + effW * cm1;
        accM2 = accM2 + effW * cm2;
        accAux = accAux + effW * cAux;
        weightSum = weightSum + effW;
    });

    DSL::Bool fallbackNear = (weightSum < 1e-5f);
    DSL::Float invW = selectIf(fallbackNear, DSL::Def(1.0f), 1.0f / DSL::Max(weightSum, 1e-6f));
    DSL::Float4 baseM0 = selectIf(fallbackNear, m0.readVec(nearIdx), accM0 * invW);
    DSL::Float4 baseM1 = selectIf(fallbackNear, m1.readVec(nearIdx), accM1 * invW);
    DSL::Float4 baseM2 = selectIf(fallbackNear, m2.readVec(nearIdx), accM2 * invW);
    DSL::Float4 baseAux = selectIf(fallbackNear, aux.readVec(nearIdx), accAux * invW);

    DSL::Float visc0 = DSL::ToFloat4(params.domainMaxAndVisc0).w;
    DSL::Float tauC = 3.0f * (visc0 * DSL::Cast<float>(1 << coarseLevel)) + 0.5f;
    DSL::Float tauF = 3.0f * (visc0 * DSL::Cast<float>(1 << fineLevel)) + 0.5f;
    DSL::Float stressScale = tauF / DSL::Max(2.0f * tauC, 1e-4f);

    DSL::Float3 ucStored = DSL::MakeFloat3(baseM0.y, baseM0.z, baseM0.w);
    DSL::Float3 accelC = levelBaseAcceleration(params, coarseLevel, baseAux.x);
    DSL::Float3 accelF = levelBaseAcceleration(params, fineLevel, baseAux.x);
    DSL::Float3 uPhys = ucStored - 0.5f * accelC;
    DSL::Float3 ufStored = selectIf(
        nearFlag == HomeLbmConstants::kCellSolid,
        DSL::MakeFloat3(0.0f),
        uPhys + 0.5f * accelF);

    DSL::Float neqXX = baseM1.x - ucStored.x * ucStored.x;
    DSL::Float neqYY = baseM1.y - ucStored.y * ucStored.y;
    DSL::Float neqZZ = baseM1.z - ucStored.z * ucStored.z;
    DSL::Float neqXY = baseM1.w - ucStored.x * ucStored.y;
    DSL::Float neqXZ = baseM2.x - ucStored.x * ucStored.z;
    DSL::Float neqYZ = baseM2.y - ucStored.y * ucStored.z;

    outM0 = DSL::MakeFloat4(DSL::Clamp(baseM0.x, 0.35f, 2.50f), ufStored.x, ufStored.y, ufStored.z);
    outM1 = DSL::MakeFloat4(
        ufStored.x * ufStored.x + stressScale * neqXX,
        ufStored.y * ufStored.y + stressScale * neqYY,
        ufStored.z * ufStored.z + stressScale * neqZZ,
        ufStored.x * ufStored.y + stressScale * neqXY);
    outM2 = DSL::MakeFloat4(
        ufStored.x * ufStored.z + stressScale * neqXZ,
        ufStored.y * ufStored.z + stressScale * neqYZ,
        DSL::Clamp(baseM2.z, 0.0f, 1.0f),
        DSL::Max(0.0f, baseM2.w));
    outAux = baseAux;
    outFlag = nearFlag;
}

/**
 * @brief 计算域六面外护边边界条件映射（支持面/棱/角正交反射张量变换）
 */
inline void applyDomainBoundaryHalo(
    const DSL::Var<LbmGpuParams>& params,
    DSL::Int level,
    DSL::Int gx,
    DSL::Int gy,
    DSL::Int gz,
    DSL::Int nxCells,
    DSL::Int nyCells,
    DSL::Int nzCells,
    DSL::Float4 adjM0,
    DSL::Float4 adjM1,
    DSL::Float4 adjM2,
    DSL::Float4 adjAux,
    DSL::UInt adjFlag,
    DSL::Float4& outM0,
    DSL::Float4& outM1,
    DSL::Float4& outM2,
    DSL::Float4& outAux,
    DSL::UInt& outFlag) noexcept {
    DSL::Bool outNegX = (gx < 0);
    DSL::Bool outPosX = (gx >= nxCells);
    DSL::Bool outNegY = (gy < 0);
    DSL::Bool outPosY = (gy >= nyCells);
    DSL::Bool outNegZ = (gz < 0);
    DSL::Bool outPosZ = (gz >= nzCells);

    // 确定主导边界面类型（优先级: Inlet > NoSlip > Outflow > FreeSlip）
    DSL::Int bc = DSL::Def(1); // FreeSlipWall
    auto mergeFaceBc = [&](DSL::Bool crossed, DSL::Int faceBc) {
        DSL::Bool upgrade = crossed & (
            (faceBc == 2) |
            ((faceBc == 0) & (bc != 2)) |
            ((faceBc == 3) & (bc == 1)));
        bc = selectIf(upgrade, faceBc, bc);
    };
    mergeFaceBc(outNegX, params.bcNegX);
    mergeFaceBc(outPosX, params.bcPosX);
    mergeFaceBc(outNegY, params.bcNegY);
    mergeFaceBc(outPosY, params.bcPosY);
    mergeFaceBc(outNegZ, params.bcNegZ);
    mergeFaceBc(outPosZ, params.bcPosZ);

    outM0 = adjM0;
    outM1 = adjM1;
    outM2 = adjM2;
    outAux = adjAux;
    outFlag = adjFlag & HomeLbmConstants::kCellTypeMask;

    DSL::If(bc == 0, [&] {
        // NoSlipWall: 标记为静止固体，由 Eq. (92) 单节点三阶边界闭合处理
        outM0 = DSL::MakeFloat4(adjM0.x, 0.0f, 0.0f, 0.0f);
        outM1 = DSL::MakeFloat4(0.0f);
        outM2 = DSL::MakeFloat4(0.0f);
        outAux = DSL::MakeFloat4(adjAux.x, 0.0f, 0.0f, 0.0f);
        outFlag = HomeLbmConstants::kCellSolid;
    }).ElseIf(bc == 2, [&] {
        // VelocityInlet: 施加恒定远场速度与平衡二阶矩
        DSL::Float4 inflow4 = DSL::ToFloat4(params.inflowVelAndSmag);
        DSL::Float3 uIn = DSL::MakeFloat3(inflow4.x, inflow4.y, inflow4.z) +
                          0.5f * levelBaseAcceleration(params, level, 0.0f);
        outM0 = DSL::MakeFloat4(1.0f, uIn.x, uIn.y, uIn.z);
        outM1 = DSL::MakeFloat4(uIn.x * uIn.x, uIn.y * uIn.y, uIn.z * uIn.z, uIn.x * uIn.y);
        DSL::Float inletPhi = selectIf(params.simMode == 1, DSL::Def(0.0f), DSL::Def(0.0f));
        outM2 = DSL::MakeFloat4(uIn.x * uIn.z, uIn.y * uIn.z, inletPhi, 0.0f);
        outAux = DSL::MakeFloat4(0.0f);
        outFlag = selectIf(
            params.simMode == 1,
            DSL::Def(HomeLbmConstants::kCellGas),
            DSL::Def(HomeLbmConstants::kCellLiquid));
    }).ElseIf(bc == 3, [&] {
        // PressureOutflow: 大气参考密度 + 零梯度速度外推（抑制回流发散）
        DSL::Float3 uAdj = DSL::MakeFloat3(adjM0.y, adjM0.z, adjM0.w);
        DSL::Float3 uOut = uAdj;
        uOut.x = selectIf(outPosX, DSL::Max(0.0f, uOut.x), selectIf(outNegX, DSL::Min(0.0f, uOut.x), uOut.x));
        uOut.y = selectIf(outPosY, DSL::Max(0.0f, uOut.y), selectIf(outNegY, DSL::Min(0.0f, uOut.y), uOut.y));
        uOut.z = selectIf(outPosZ, DSL::Max(0.0f, uOut.z), selectIf(outNegZ, DSL::Min(0.0f, uOut.z), uOut.z));

        DSL::Float neqXX = adjM1.x - uAdj.x * uAdj.x;
        DSL::Float neqYY = adjM1.y - uAdj.y * uAdj.y;
        DSL::Float neqZZ = adjM1.z - uAdj.z * uAdj.z;
        DSL::Float neqXY = adjM1.w - uAdj.x * uAdj.y;
        DSL::Float neqXZ = adjM2.x - uAdj.x * uAdj.z;
        DSL::Float neqYZ = adjM2.y - uAdj.y * uAdj.z;

        outM0 = DSL::MakeFloat4(1.0f, uOut.x, uOut.y, uOut.z);
        outM1 = DSL::MakeFloat4(
            uOut.x * uOut.x + neqXX,
            uOut.y * uOut.y + neqYY,
            uOut.z * uOut.z + neqZZ,
            uOut.x * uOut.y + neqXY);
        outM2 = DSL::MakeFloat4(uOut.x * uOut.z + neqXZ, uOut.y * uOut.z + neqYZ, adjM2.z, adjM2.w);
    }).Else([&] {
        // FreeSlipWall: R = diag(rx, ry, rz) 二阶张量正交镜像变换，严格保持自由液面对称质量通量零泄漏
        DSL::Float rx = selectIf(outNegX | outPosX, DSL::Def(-1.0f), DSL::Def(1.0f));
        DSL::Float ry = selectIf(outNegY | outPosY, DSL::Def(-1.0f), DSL::Def(1.0f));
        DSL::Float rz = selectIf(outNegZ | outPosZ, DSL::Def(-1.0f), DSL::Def(1.0f));

        outM0 = DSL::MakeFloat4(adjM0.x, rx * adjM0.y, ry * adjM0.z, rz * adjM0.w);
        outM1 = DSL::MakeFloat4(adjM1.x, adjM1.y, adjM1.z, (rx * ry) * adjM1.w);
        outM2 = DSL::MakeFloat4((rx * rz) * adjM2.x, (ry * rz) * adjM2.y, adjM2.z, adjM2.w);
    });
}

/**
 * @brief 科学流体可视化伪彩色映射（蓝 -> 青 -> 黄 -> 炽白）
 */
[[nodiscard]] inline DSL::Float3 evalScientificColormap(DSL::Float tNorm) noexcept {
    DSL::Float t = DSL::Clamp(tNorm, 0.0f, 1.0f);
    DSL::Float3 c0 = DSL::MakeFloat3(0.06f, 0.14f, 0.48f);
    DSL::Float3 c1 = DSL::MakeFloat3(0.05f, 0.72f, 0.88f);
    DSL::Float3 c2 = DSL::MakeFloat3(0.98f, 0.78f, 0.14f);
    DSL::Float3 c3 = DSL::MakeFloat3(1.00f, 0.96f, 0.88f);

    DSL::Float3 seg0 = DSL::Lerp(c0, c1, DSL::MakeFloat3(DSL::Clamp(t * 3.0f, 0.0f, 1.0f)));
    DSL::Float3 seg1 = DSL::Lerp(c1, c2, DSL::MakeFloat3(DSL::Clamp(t * 3.0f - 1.0f, 0.0f, 1.0f)));
    DSL::Float3 seg2 = DSL::Lerp(c2, c3, DSL::MakeFloat3(DSL::Clamp(t * 3.0f - 2.0f, 0.0f, 1.0f)));

    DSL::Float3 col = selectIf(t < (1.0f / 3.0f), seg0, seg1);
    col = selectIf(t >= (2.0f / 3.0f), seg2, col);
    return col;
}

} // namespace MQEngine::HomeLbmDsl

#endif // MQENGINE_HOMELBM_DSL_MATH_H
