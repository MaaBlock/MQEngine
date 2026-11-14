/**
 * @file GPUScene.cpp
 * @brief GPUScene 全局纹理数组图集跨材质合批与 2D 数据纹理管理器实现
 */
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace MQEngine::Domain::Renderer
{
    namespace
    {
        struct FrustumPlane
        {
            float a{0.0f};
            float b{0.0f};
            float c{0.0f};
            float d{0.0f};

            void normalize() noexcept
            {
                const float len = std::sqrt(a * a + b * b + c * c);
                if (len > 1e-6f)
                {
                    const float invLen = 1.0f / len;
                    a *= invLen;
                    b *= invLen;
                    c *= invLen;
                    d *= invLen;
                }
            }
        };

        /**
         * @brief 4 路 SIMD128 批量测试 4 个实例包围球是否通过全部 6 个视锥平面
         */
        uint32_t testFourSpheresAgainstFrustumSimd128(
            const FrustumPlane planes[6],
            const float* cx,
            const float* cy,
            const float* cz,
            const float* negRadius) noexcept
        {
#if defined(MQENGINE_SIMD_SSE)
            const __m128 px = _mm_loadu_ps(cx);
            const __m128 py = _mm_loadu_ps(cy);
            const __m128 pz = _mm_loadu_ps(cz);
            const __m128 nr = _mm_loadu_ps(negRadius);
            uint32_t aliveMask = 0xFu;

            for (int p = 0; p < 6 && aliveMask != 0; ++p)
            {
                const __m128 pa = _mm_set1_ps(planes[p].a);
                const __m128 pb = _mm_set1_ps(planes[p].b);
                const __m128 pc = _mm_set1_ps(planes[p].c);
                const __m128 pd = _mm_set1_ps(planes[p].d);

                const __m128 dist = _mm_add_ps(
                    _mm_add_ps(_mm_mul_ps(pa, px), _mm_mul_ps(pb, py)),
                    _mm_add_ps(_mm_mul_ps(pc, pz), pd));
                const __m128 cmp = _mm_cmpge_ps(dist, nr);
                aliveMask &= (static_cast<uint32_t>(_mm_movemask_ps(cmp)) & 0xFu);
            }
            return aliveMask;
#elif defined(MQENGINE_SIMD_WASM)
            const v128_t px = wasm_v128_load(cx);
            const v128_t py = wasm_v128_load(cy);
            const v128_t pz = wasm_v128_load(cz);
            const v128_t nr = wasm_v128_load(negRadius);
            uint32_t aliveMask = 0xFu;

            for (int p = 0; p < 6 && aliveMask != 0; ++p)
            {
                const v128_t pa = wasm_f32x4_splat(planes[p].a);
                const v128_t pb = wasm_f32x4_splat(planes[p].b);
                const v128_t pc = wasm_f32x4_splat(planes[p].c);
                const v128_t pd = wasm_f32x4_splat(planes[p].d);

                const v128_t dist = wasm_f32x4_add(
                    wasm_f32x4_add(wasm_f32x4_mul(pa, px), wasm_f32x4_mul(pb, py)),
                    wasm_f32x4_add(wasm_f32x4_mul(pc, pz), pd));
                const v128_t cmp = wasm_f32x4_ge(dist, nr);
                aliveMask &= (static_cast<uint32_t>(wasm_i32x4_bitmask(cmp)) & 0xFu);
            }
            return aliveMask;
#else
            uint32_t aliveMask = 0xFu;
            for (int p = 0; p < 6 && aliveMask != 0; ++p)
            {
                uint32_t planePass = 0;
                for (uint32_t lane = 0; lane < 4; ++lane)
                {
                    const float dist = planes[p].a * cx[lane] + planes[p].b * cy[lane] + planes[p].c * cz[lane] + planes[p].d;
                    if (dist >= negRadius[lane])
                    {
                        planePass |= (1u << lane);
                    }
                }
                aliveMask &= planePass;
            }
            return aliveMask;
#endif
        }
    } // namespace

    GPUScene::GPUScene()
    {
        // 预分配本地 2D 数据纹理镜像缓冲 (1024 宽 * 256 高 * 16 字节 = 4MB)
        m_localDataTextureBytes.resize(DataTextureWidth * 256 * 16, 0);
        initBuiltinTextureRecords();
    }

    void GPUScene::initBuiltinTextureRecords()
    {
        // Slot 0: 默认 1x1 纯白贴图；Slot 1: 默认 1x1 平坦法线；Slot 2: 1x1 占位贴图 (FEAT-17)
        GpuTextureRecord slot0{};
        slot0.uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
        slot0.bindlessOrPageIndex = 0;
        m_textureRecords[0] = slot0;

        GpuTextureRecord slot1 = slot0;
        slot1.bindlessOrPageIndex = 1u;
        m_textureRecords[1] = slot1;

        GpuTextureRecord slot2 = slot0;
        slot2.bindlessOrPageIndex = 2u;
        slot2.flags = 1u; // 标记为占位槽位
        m_textureRecords[2] = slot2;
    }

    Status GPUScene::init(FCT::Context* ctx)
    {
        if (ctx)
        {
            m_dataTextureStorage = ctx->createStorageBuffer(m_localDataTextureBytes.size());
            if (m_dataTextureStorage)
            {
                (void)m_dataTextureStorage->create(m_localDataTextureBytes.size());
            }
        }
        return OkStatus();
    }

    void GPUScene::shutdown() noexcept
    {
        m_shelfPacker.reset();
        m_dataTextureStorage = nullptr;
        m_texture2DArray = nullptr;
        m_textureRecords.clear();
        initBuiltinTextureRecords();
    }

    AtlasAllocation GPUScene::registerTextureToAtlas(uint32_t width, uint32_t height)
    {
        return m_shelfPacker.allocate(width, height);
    }

    FCT::Vec4 GPUScene::getTextureUvScaleOffset(const AtlasAllocation& alloc) const noexcept
    {
        return m_shelfPacker.calculateUvScaleOffset(alloc);
    }

    Status GPUScene::allocateTextureLayer(uint32_t layerIndex, const FCT::Vec4& uvScaleOffset)
    {
        if (layerIndex >= MaxTextureArrayLayers)
        {
            return ResourceExhaustedError("GL_TEXTURE_2D_ARRAY 图层数量超出 256 上限");
        }

        GpuTextureRecord rec{};
        rec.uvScaleOffset = uvScaleOffset;
        rec.bindlessOrPageIndex = layerIndex;
        m_textureRecords[layerIndex] = rec;
        return OkStatus();
    }

    GpuTextureRecord GPUScene::getTextureRecord(uint32_t slot) const noexcept
    {
        auto it = m_textureRecords.find(slot);
        if (it != m_textureRecords.end())
        {
            return it->second;
        }

        auto fallbackIt = m_textureRecords.find(0u);
        if (fallbackIt != m_textureRecords.end())
        {
            return fallbackIt->second;
        }

        GpuTextureRecord identity{};
        identity.uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
        identity.bindlessOrPageIndex = 0;
        return identity;
    }

    void GPUScene::updateTextureRecord(uint32_t slot, const GpuTextureRecord& record)
    {
        m_textureRecords[slot] = record;
    }

    void GPUScene::TexelCoordFromSlot(uint32_t slot, uint32_t recordTexelSize, uint32_t& outX, uint32_t& outY) noexcept
    {
        const uint32_t texelIndex = slot * recordTexelSize;
        outX = texelIndex & 1023u;
        outY = texelIndex >> 10u;
    }

    StatusOr<DataTextureDirtyRowSpan> GPUScene::computeDirtyTextureRows(
        uint32_t startSlot,
        uint32_t endSlot,
        uint32_t recordTexelSize) noexcept
    {
        if (startSlot > endSlot)
        {
            return InvalidArgumentError("GPUScene 数据纹理更新区间非法: startSlot > endSlot");
        }
        const uint32_t safeStride = (recordTexelSize > 0) ? recordTexelSize : 1u;
        const uint64_t firstTexel = static_cast<uint64_t>(startSlot) * safeStride;
        const uint64_t lastTexel = static_cast<uint64_t>(endSlot) * safeStride + (safeStride - 1u);

        const uint32_t startRow = static_cast<uint32_t>(firstTexel / DataTextureWidth);
        const uint32_t endRow = static_cast<uint32_t>(lastTexel / DataTextureWidth);

        DataTextureDirtyRowSpan span{};
        span.startRow = startRow;
        span.rowCount = (endRow - startRow) + 1u;
        return span;
    }

    Status GPUScene::uploadInstanceData(std::span<const GpuInstanceRow> instances, uint32_t startSlot)
    {
        if (instances.empty())
        {
            return OkStatus();
        }

        const size_t byteOffset = startSlot * sizeof(GpuInstanceRow);
        const size_t byteSize = instances.size_bytes();

        if (byteOffset + byteSize > m_localDataTextureBytes.size())
        {
            const size_t newSize = std::max(m_localDataTextureBytes.size() * 2, byteOffset + byteSize);
            m_localDataTextureBytes.resize(newSize, 0);
            if (m_dataTextureStorage)
            {
                (void)m_dataTextureStorage->create(newSize);
            }
        }

        std::memcpy(m_localDataTextureBytes.data() + byteOffset, instances.data(), byteSize);

        if (m_dataTextureStorage)
        {
            m_dataTextureStorage->updateData(instances.data(), byteSize, byteOffset);
        }

        return OkStatus();
    }

    Status GPUScene::uploadMaterialData(std::span<const GpuMaterialRecord> materials, uint32_t startSlot)
    {
        if (materials.empty())
        {
            return OkStatus();
        }

        const size_t byteOffset = startSlot * sizeof(GpuMaterialRecord);
        const size_t byteSize = materials.size_bytes();

        if (byteOffset + byteSize > m_localDataTextureBytes.size())
        {
            const size_t newSize = std::max(m_localDataTextureBytes.size() * 2, byteOffset + byteSize);
            m_localDataTextureBytes.resize(newSize, 0);
            if (m_dataTextureStorage)
            {
                (void)m_dataTextureStorage->create(newSize);
            }
        }

        std::memcpy(m_localDataTextureBytes.data() + byteOffset, materials.data(), byteSize);

        if (m_dataTextureStorage)
        {
            m_dataTextureStorage->updateData(materials.data(), byteSize, byteOffset);
        }

        return OkStatus();
    }

    std::vector<uint32_t> GPUScene::extractVisibleInstanceSlotStream(
        std::span<const GpuInstanceRow> instances,
        const FCT::Mat4& viewProjMatrix)
    {
        std::vector<uint32_t> visibleSlots;
        if (instances.empty())
        {
            return visibleSlots;
        }
        visibleSlots.reserve(instances.size());

        FrustumPlane planes[6];
        const auto& m = viewProjMatrix.m;

        // Gribb-Hartmann 提取视锥 6 裁剪面
        planes[0] = {m[12] + m[0], m[13] + m[1], m[14] + m[2], m[15] + m[3]}; // Left
        planes[1] = {m[12] - m[0], m[13] - m[1], m[14] - m[2], m[15] - m[3]}; // Right
        planes[2] = {m[12] + m[4], m[13] + m[5], m[14] + m[6], m[15] + m[7]}; // Bottom
        planes[3] = {m[12] - m[4], m[13] - m[5], m[14] - m[6], m[15] - m[7]}; // Top
        planes[4] = {m[12] + m[8], m[13] + m[9], m[14] + m[10], m[15] + m[11]}; // Near
        planes[5] = {m[12] - m[8], m[13] - m[9], m[14] - m[10], m[15] - m[11]}; // Far

        for (auto& p : planes)
        {
            p.normalize();
        }

        // 第一级过滤：剔除不可见标记、非正半径及 NaN 非法包围球，收集候选实例进入 4 路 SIMD 视锥剔除
        std::vector<uint32_t> candidateIndices;
        candidateIndices.reserve(instances.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(instances.size()); ++i)
        {
            const auto& row = instances[i];
            if ((row.flags & 1u) == 0u)
            {
                continue;
            }

            const float cx = row.worldSphereBounds.x;
            const float cy = row.worldSphereBounds.y;
            const float cz = row.worldSphereBounds.z;
            const float radius = row.worldSphereBounds.w;
            if (radius <= 0.0f || std::isnan(cx) || std::isnan(cy) || std::isnan(cz) || std::isnan(radius))
            {
                continue;
            }
            candidateIndices.push_back(i);
        }

        alignas(16) float batchX[4];
        alignas(16) float batchY[4];
        alignas(16) float batchZ[4];
        alignas(16) float batchNegR[4];

        const size_t candCount = candidateIndices.size();
        size_t base = 0;
        for (; base + 4 <= candCount; base += 4)
        {
            for (size_t lane = 0; lane < 4; ++lane)
            {
                const auto& b = instances[candidateIndices[base + lane]].worldSphereBounds;
                batchX[lane] = b.x;
                batchY[lane] = b.y;
                batchZ[lane] = b.z;
                batchNegR[lane] = -b.w;
            }

            const uint32_t passMask = testFourSpheresAgainstFrustumSimd128(planes, batchX, batchY, batchZ, batchNegR);
            for (uint32_t lane = 0; lane < 4; ++lane)
            {
                if ((passMask & (1u << lane)) != 0)
                {
                    visibleSlots.push_back(candidateIndices[base + lane]);
                }
            }
        }

        for (; base < candCount; ++base)
        {
            const uint32_t slotIdx = candidateIndices[base];
            const auto& b = instances[slotIdx].worldSphereBounds;
            bool inside = true;
            for (int p = 0; p < 6; ++p)
            {
                const float dist = planes[p].a * b.x + planes[p].b * b.y + planes[p].c * b.z + planes[p].d;
                if (dist < -b.w)
                {
                    inside = false;
                    break;
                }
            }
            if (inside)
            {
                visibleSlots.push_back(slotIdx);
            }
        }

        return visibleSlots;
    }
}
