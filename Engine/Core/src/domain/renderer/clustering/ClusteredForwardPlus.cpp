/**
 * @file ClusteredForwardPlus.cpp
 * @brief 3D Clustered 前向光照剔除与分桶管理器实现 (CPU SIMD128 加速)
 */
#include <MQEngine/domain/renderer/clustering/ClusteredForwardPlus.h>
#include <algorithm>
#include <cmath>

namespace MQEngine::Domain::Renderer
{
    namespace
    {
        bool isValidLightRecord(const ClusterLightRecord& lt) noexcept
        {
            if (lt.radius <= 0.0f || std::isnan(lt.radius))
            {
                return false;
            }
            if (std::isnan(lt.position[0]) || std::isnan(lt.position[1]) || std::isnan(lt.position[2]))
            {
                return false;
            }
            return true;
        }

        /**
         * @brief 4 路 SIMD128 批量计算 4 盏视空间球形光源与单个 Froxel AABB 的相交位掩码 (bit 0..3)
         */
        uint32_t testFourLightsSimd128(
            const FroxelAabb& box,
            const float* lx,
            const float* ly,
            const float* lz,
            const float* lrSq) noexcept
        {
#if defined(MQENGINE_SIMD_SSE)
            const __m128 minX = _mm_set1_ps(box.minPt[0]);
            const __m128 maxX = _mm_set1_ps(box.maxPt[0]);
            const __m128 minY = _mm_set1_ps(box.minPt[1]);
            const __m128 maxY = _mm_set1_ps(box.maxPt[1]);
            const __m128 minZ = _mm_set1_ps(box.minPt[2]);
            const __m128 maxZ = _mm_set1_ps(box.maxPt[2]);
            const __m128 zero = _mm_setzero_ps();

            const __m128 px = _mm_loadu_ps(lx);
            const __m128 py = _mm_loadu_ps(ly);
            const __m128 pz = _mm_loadu_ps(lz);
            const __m128 r2 = _mm_loadu_ps(lrSq);

            const __m128 dx = _mm_max_ps(_mm_sub_ps(minX, px), _mm_max_ps(zero, _mm_sub_ps(px, maxX)));
            const __m128 dy = _mm_max_ps(_mm_sub_ps(minY, py), _mm_max_ps(zero, _mm_sub_ps(py, maxY)));
            const __m128 dz = _mm_max_ps(_mm_sub_ps(minZ, pz), _mm_max_ps(zero, _mm_sub_ps(pz, maxZ)));

            const __m128 distSq = _mm_add_ps(
                _mm_mul_ps(dx, dx),
                _mm_add_ps(_mm_mul_ps(dy, dy), _mm_mul_ps(dz, dz)));
            const __m128 cmp = _mm_cmple_ps(distSq, r2);
            return static_cast<uint32_t>(_mm_movemask_ps(cmp)) & 0xFu;
#elif defined(MQENGINE_SIMD_WASM)
            const v128_t minX = wasm_f32x4_splat(box.minPt[0]);
            const v128_t maxX = wasm_f32x4_splat(box.maxPt[0]);
            const v128_t minY = wasm_f32x4_splat(box.minPt[1]);
            const v128_t maxY = wasm_f32x4_splat(box.maxPt[1]);
            const v128_t minZ = wasm_f32x4_splat(box.minPt[2]);
            const v128_t maxZ = wasm_f32x4_splat(box.maxPt[2]);
            const v128_t zero = wasm_f32x4_const_splat(0.0f);

            const v128_t px = wasm_v128_load(lx);
            const v128_t py = wasm_v128_load(ly);
            const v128_t pz = wasm_v128_load(lz);
            const v128_t r2 = wasm_v128_load(lrSq);

            const v128_t dx = wasm_f32x4_pmax(wasm_f32x4_sub(minX, px), wasm_f32x4_pmax(zero, wasm_f32x4_sub(px, maxX)));
            const v128_t dy = wasm_f32x4_pmax(wasm_f32x4_sub(minY, py), wasm_f32x4_pmax(zero, wasm_f32x4_sub(py, maxY)));
            const v128_t dz = wasm_f32x4_pmax(wasm_f32x4_sub(minZ, pz), wasm_f32x4_pmax(zero, wasm_f32x4_sub(pz, maxZ)));

            const v128_t distSq = wasm_f32x4_add(
                wasm_f32x4_mul(dx, dx),
                wasm_f32x4_add(wasm_f32x4_mul(dy, dy), wasm_f32x4_mul(dz, dz)));
            const v128_t cmp = wasm_f32x4_le(distSq, r2);
            return static_cast<uint32_t>(wasm_i32x4_bitmask(cmp)) & 0xFu;
#elif defined(MQENGINE_SIMD_NEON)
            const float32x4_t minX = vdupq_n_f32(box.minPt[0]);
            const float32x4_t maxX = vdupq_n_f32(box.maxPt[0]);
            const float32x4_t minY = vdupq_n_f32(box.minPt[1]);
            const float32x4_t maxY = vdupq_n_f32(box.maxPt[1]);
            const float32x4_t minZ = vdupq_n_f32(box.minPt[2]);
            const float32x4_t maxZ = vdupq_n_f32(box.maxPt[2]);
            const float32x4_t zero = vdupq_n_f32(0.0f);

            const float32x4_t px = vld1q_f32(lx);
            const float32x4_t py = vld1q_f32(ly);
            const float32x4_t pz = vld1q_f32(lz);

            const float32x4_t dx = vmaxq_f32(vsubq_f32(minX, px), vmaxq_f32(zero, vsubq_f32(px, maxX)));
            const float32x4_t dy = vmaxq_f32(vsubq_f32(minY, py), vmaxq_f32(zero, vsubq_f32(py, maxY)));
            const float32x4_t dz = vmaxq_f32(vsubq_f32(minZ, pz), vmaxq_f32(zero, vsubq_f32(pz, maxZ)));

            const float32x4_t distSq = vaddq_f32(
                vmulq_f32(dx, dx),
                vaddq_f32(vmulq_f32(dy, dy), vmulq_f32(dz, dz)));
            alignas(16) float distArr[4];
            vst1q_f32(distArr, distSq);

            uint32_t mask = 0;
            for (uint32_t lane = 0; lane < 4; ++lane)
            {
                if (distArr[lane] <= lrSq[lane])
                {
                    mask |= (1u << lane);
                }
            }
            return mask;
#else
            uint32_t mask = 0;
            for (uint32_t lane = 0; lane < 4; ++lane)
            {
                const float dx = std::max(box.minPt[0] - lx[lane], std::max(0.0f, lx[lane] - box.maxPt[0]));
                const float dy = std::max(box.minPt[1] - ly[lane], std::max(0.0f, ly[lane] - box.maxPt[1]));
                const float dz = std::max(box.minPt[2] - lz[lane], std::max(0.0f, lz[lane] - box.maxPt[2]));
                if (dx * dx + dy * dy + dz * dz <= lrSq[lane])
                {
                    mask |= (1u << lane);
                }
            }
            return mask;
#endif
        }
    } // namespace

    ClusteredForwardPlus::ClusteredForwardPlus()
    {
        FCT::Mat4 defaultProj{};
        defaultProj.m[0] = 1.0f;
        defaultProj.m[5] = 1.0f;
        updateFrustumGrid(defaultProj, 0.1f, 100.0f, 1920, 1080);
        m_froxelGridBuffer.resize(m_grid.totalFroxels() * 2, 0);
    }

    void ClusteredForwardPlus::updateFrustumGrid(
        const FCT::Mat4& projMatrix,
        float nearPlane,
        float farPlane,
        uint32_t viewportWidth,
        uint32_t viewportHeight)
    {
        if (nearPlane <= 0.0f) nearPlane = 0.1f;
        if (farPlane <= nearPlane) farPlane = nearPlane + 100.0f;

        const bool isOrthographic = (std::abs(projMatrix.m[15]) > 0.5f);

        float halfExtentY = 1.0f;
        if (std::abs(projMatrix.m[5]) > 1e-4f)
        {
            halfExtentY = 1.0f / std::abs(projMatrix.m[5]);
        }

        float halfExtentX = 1.0f;
        if (std::abs(projMatrix.m[0]) > 1e-4f)
        {
            halfExtentX = 1.0f / std::abs(projMatrix.m[0]);
        }
        else if (viewportHeight > 0)
        {
            halfExtentX = halfExtentY * (static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight));
        }

        const FroxelGridConfig fullGrid{16, 8, 16};
        m_froxelAabbs.resize(fullGrid.totalFroxels());

        const float logFarNear = std::log(farPlane / nearPlane);

        for (uint32_t z = 0; z < fullGrid.dimZ; ++z)
        {
            const float zNearSlice = nearPlane * std::exp(logFarNear * (static_cast<float>(z) / static_cast<float>(fullGrid.dimZ)));
            const float zFarSlice = nearPlane * std::exp(logFarNear * (static_cast<float>(z + 1) / static_cast<float>(fullGrid.dimZ)));

            for (uint32_t y = 0; y < fullGrid.dimY; ++y)
            {
                const float ndcMinY = -1.0f + 2.0f * (static_cast<float>(y) / static_cast<float>(fullGrid.dimY));
                const float ndcMaxY = -1.0f + 2.0f * (static_cast<float>(y + 1) / static_cast<float>(fullGrid.dimY));

                for (uint32_t x = 0; x < fullGrid.dimX; ++x)
                {
                    const float ndcMinX = -1.0f + 2.0f * (static_cast<float>(x) / static_cast<float>(fullGrid.dimX));
                    const float ndcMaxX = -1.0f + 2.0f * (static_cast<float>(x + 1) / static_cast<float>(fullGrid.dimX));

                    const uint32_t fIdx = x + fullGrid.dimX * (y + fullGrid.dimY * z);

                    // Note: 正交投影下 Froxel 视空间 XY 边界直接为 ndc * halfExtent，不随深度切片缩放
                    const float xNearScale = isOrthographic ? halfExtentX : (halfExtentX * zNearSlice);
                    const float xFarScale  = isOrthographic ? halfExtentX : (halfExtentX * zFarSlice);
                    const float yNearScale = isOrthographic ? halfExtentY : (halfExtentY * zNearSlice);
                    const float yFarScale  = isOrthographic ? halfExtentY : (halfExtentY * zFarSlice);

                    const float x0Near = ndcMinX * xNearScale;
                    const float x1Near = ndcMaxX * xNearScale;
                    const float x0Far  = ndcMinX * xFarScale;
                    const float x1Far  = ndcMaxX * xFarScale;

                    const float y0Near = ndcMinY * yNearScale;
                    const float y1Near = ndcMaxY * yNearScale;
                    const float y0Far  = ndcMinY * yFarScale;
                    const float y1Far  = ndcMaxY * yFarScale;

                    FroxelAabb& aabb = m_froxelAabbs[fIdx];
                    aabb.minPt[0] = std::min({x0Near, x1Near, x0Far, x1Far});
                    aabb.maxPt[0] = std::max({x0Near, x1Near, x0Far, x1Far});

                    aabb.minPt[1] = std::min({y0Near, y1Near, y0Far, y1Far});
                    aabb.maxPt[1] = std::max({y0Near, y1Near, y0Far, y1Far});

                    aabb.minPt[2] = -zFarSlice;
                    aabb.maxPt[2] = -zNearSlice;
                }
            }
        }
    }

    uint32_t ClusteredForwardPlus::getFroxelLightCount(uint32_t froxelIndex) const noexcept
    {
        const size_t offsetIdx = static_cast<size_t>(froxelIndex) * 2u + 1u;
        if (offsetIdx >= m_froxelGridBuffer.size())
        {
            return 0u;
        }
        return m_froxelGridBuffer[offsetIdx];
    }

    const FroxelAabb* ClusteredForwardPlus::getFroxelAabb(uint32_t froxelIndex) const noexcept
    {
        if (froxelIndex >= m_froxelAabbs.size())
        {
            return nullptr;
        }
        return &m_froxelAabbs[froxelIndex];
    }

    uint32_t ClusteredForwardPlus::computeSliceIndex(
        float viewZ,
        float nearPlane,
        float farPlane,
        uint32_t sliceCount) noexcept
    {
        if (sliceCount == 0)
        {
            return 0u;
        }
        const float safeNear = (nearPlane > 1e-4f) ? nearPlane : 0.1f;
        const float safeFar = (farPlane > safeNear) ? farPlane : (safeNear + 100.0f);
        const float depth = -viewZ;
        if (std::isnan(depth) || depth <= safeNear)
        {
            return 0u;
        }
        if (depth >= safeFar)
        {
            return sliceCount - 1u;
        }

        const float logRatio = std::log(depth / safeNear) / std::log(safeFar / safeNear);
        const uint32_t rawSlice = static_cast<uint32_t>(logRatio * static_cast<float>(sliceCount));
        return std::min(rawSlice, sliceCount - 1u);
    }

    bool ClusteredForwardPlus::TestFroxelSphereIntersection(
        const FroxelAabb& box,
        float cx,
        float cy,
        float cz,
        float radius) noexcept
    {
        if (radius <= 0.0f || std::isnan(cx) || std::isnan(cy) || std::isnan(cz) || std::isnan(radius))
        {
            return false;
        }

        float distSq = 0.0f;
        const float center[3] = {cx, cy, cz};

        for (int i = 0; i < 3; ++i)
        {
            if (center[i] < box.minPt[i])
            {
                const float d = box.minPt[i] - center[i];
                distSq += d * d;
            }
            else if (center[i] > box.maxPt[i])
            {
                const float d = center[i] - box.maxPt[i];
                distSq += d * d;
            }
        }

        return distSq <= (radius * radius);
    }

    void ClusteredForwardPlus::cullLights(
        const std::vector<ClusterLightRecord>& sceneLights,
        const FCT::Mat4& viewMatrix)
    {
        m_activeLightTable.clear();
        m_activeLightTable.reserve(sceneLights.size());
        for (const auto& lt : sceneLights)
        {
            if (isValidLightRecord(lt))
            {
                m_activeLightTable.push_back(lt);
            }
        }

        m_activeLightCount = static_cast<uint32_t>(m_activeLightTable.size());

        // 自适应单簇退化：当有效局部光源 <= 4 盏时直接退化为 1x1x1 单簇
        if (m_activeLightCount <= SingleClusterDegradeThreshold)
        {
            m_isDegraded = true;
            m_grid = FroxelGridConfig{1, 1, 1};
            m_froxelGridBuffer.assign({0u, m_activeLightCount});
            m_lightIndexList.resize(m_activeLightCount);
            for (uint32_t i = 0; i < m_activeLightCount; ++i)
            {
                m_lightIndexList[i] = i;
            }
            return;
        }

        m_isDegraded = false;
        m_grid = FroxelGridConfig{16, 8, 16};
        const uint32_t totalFroxels = m_grid.totalFroxels();
        if (m_froxelAabbs.size() < totalFroxels)
        {
            FCT::Mat4 defaultProj{};
            defaultProj.m[0] = 1.0f;
            defaultProj.m[5] = 1.0f;
            updateFrustumGrid(defaultProj, 0.1f, 100.0f, 1920, 1080);
        }

        m_froxelGridBuffer.assign(totalFroxels * 2, 0);
        m_lightIndexList.clear();
        m_lightIndexList.reserve(static_cast<size_t>(totalFroxels) * 4u);

        // SoA 视空间光源数组（按 4 对齐填充以支持无分支 SIMD128 批处理）
        const size_t paddedCount = (static_cast<size_t>(m_activeLightCount) + 3u) & ~size_t(3u);
        std::vector<float> viewX(paddedCount, 1e9f);
        std::vector<float> viewY(paddedCount, 1e9f);
        std::vector<float> viewZ(paddedCount, 1e9f);
        std::vector<float> radiusSq(paddedCount, -1.0f);

        for (uint32_t i = 0; i < m_activeLightCount; ++i)
        {
            const auto& lt = m_activeLightTable[i];
            viewX[i] = viewMatrix.m[0] * lt.position[0] + viewMatrix.m[1] * lt.position[1] + viewMatrix.m[2] * lt.position[2] + viewMatrix.m[3];
            viewY[i] = viewMatrix.m[4] * lt.position[0] + viewMatrix.m[5] * lt.position[1] + viewMatrix.m[6] * lt.position[2] + viewMatrix.m[7];
            viewZ[i] = viewMatrix.m[8] * lt.position[0] + viewMatrix.m[9] * lt.position[1] + viewMatrix.m[10] * lt.position[2] + viewMatrix.m[11];
            radiusSq[i] = lt.radius * lt.radius;
        }

        for (uint32_t fIdx = 0; fIdx < totalFroxels; ++fIdx)
        {
            const auto& aabb = m_froxelAabbs[fIdx];
            const uint32_t offset = static_cast<uint32_t>(m_lightIndexList.size());
            uint32_t count = 0;

            for (size_t base = 0; base < paddedCount && count < MaxLightsPerFroxel; base += 4)
            {
                const uint32_t hitMask = testFourLightsSimd128(
                    aabb,
                    &viewX[base],
                    &viewY[base],
                    &viewZ[base],
                    &radiusSq[base]);

                if (hitMask == 0)
                {
                    continue;
                }

                for (uint32_t lane = 0; lane < 4; ++lane)
                {
                    if ((hitMask & (1u << lane)) != 0)
                    {
                        const uint32_t lightIdx = static_cast<uint32_t>(base + lane);
                        if (lightIdx < m_activeLightCount)
                        {
                            m_lightIndexList.push_back(lightIdx);
                            if (++count >= MaxLightsPerFroxel)
                            {
                                break;
                            }
                        }
                    }
                }
            }

            m_froxelGridBuffer[fIdx * 2 + 0] = offset;
            m_froxelGridBuffer[fIdx * 2 + 1] = count;
        }
    }
}
