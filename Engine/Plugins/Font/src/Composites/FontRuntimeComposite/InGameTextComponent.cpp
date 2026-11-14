/**
 * @file InGameTextComponent.cpp
 * @brief 游戏内跳字组件与 SIMD128 批量动画演算实现
 */
#include "InGameTextComponent.h"

namespace MQEngine::Resource::Font
{
    void InGameTextComponent::spawnDamageNumber(
        const std::string& text,
        const FCT::Vec3<>& pos,
        float scale,
        uint32_t color,
        bool isCritical)
    {
        ActiveTextItem item;
        item.text = text;
        item.worldPos[0] = pos.x;
        item.worldPos[1] = pos.y;
        item.worldPos[2] = pos.z;
        item.scale = isCritical ? (scale * 1.5f) : scale;
        item.fillColor = isCritical ? 0xFFD700FFu : color;
        item.outlineColor = 0x00000002u;
        item.isCritical = isCritical;
        item.lifeTime = 0.0f;
        item.maxLife = 1.2f;
        m_activeItems.push_back(std::move(item));
    }

    void InGameTextComponent::tickItems(float dt)
    {
        ZoneScoped();
        if (dt <= 0.0f)
        {
            return;
        }
        for (auto& item : m_activeItems)
        {
            item.lifeTime += dt;
            item.worldPos[1] += 5.0f * dt;
            item.scale = std::max(0.5f, item.scale - 0.1f * dt);
        }
        m_activeItems.erase(
            std::remove_if(m_activeItems.begin(), m_activeItems.end(), [](const ActiveTextItem& it) {
                return it.lifeTime >= it.maxLife;
            }),
            m_activeItems.end());
    }

    void InGameTextComponent::buildQuadInstances(
        const BakedFont& font,
        std::vector<GpuInGameTextQuadInstance>& outQuads) const
    {
        ZoneScoped();
        outQuads.clear();
        for (const auto& item : m_activeItems)
        {
            float cursorOffset = 0.0f;
            size_t strIdx = 0;
            while (strIdx < item.text.size())
            {
                char32_t cp = 0;
                if (!FontTrackClassifier::decodeNextUtf8(item.text, strIdx, cp))
                {
                    break;
                }
                const GlyphMetric metric = font.getGlyphMetric(cp);
                GpuInGameTextQuadInstance quad{};
                quad.worldPosAndScale[0] = item.worldPos[0] + cursorOffset * 0.02f * item.scale;
                quad.worldPosAndScale[1] = item.worldPos[1];
                quad.worldPosAndScale[2] = item.worldPos[2];
                quad.worldPosAndScale[3] = item.scale;
                quad.uvMinMax[0] = metric.uvMinMax[0];
                quad.uvMinMax[1] = metric.uvMinMax[1];
                quad.uvMinMax[2] = metric.uvMinMax[2];
                quad.uvMinMax[3] = metric.uvMinMax[3];
                quad.fillColorRgba8 = item.fillColor;
                quad.outlineColorAndPx = item.outlineColor;
                outQuads.push_back(quad);
                cursorOffset += metric.advanceX;
            }
        }
    }

    void InGameTextComponent::evaluateBatchAnimations(
        std::span<GpuInGameTextQuadInstance> quads,
        float dt) noexcept
    {
        ZoneScoped();
        const float deltaY = 5.0f * dt;
        const float deltaScale = -0.1f * dt;

#if defined(MQENGINE_SIMD_SSE)
        const __m128 addVec = _mm_setr_ps(0.0f, deltaY, 0.0f, deltaScale);
        const __m128 minScaleMask = _mm_setr_ps(-1.0e30f, -1.0e30f, -1.0e30f, 0.5f);
        for (auto& quad : quads)
        {
            __m128 posScale = _mm_load_ps(quad.worldPosAndScale);
            posScale = _mm_add_ps(posScale, addVec);
            posScale = _mm_max_ps(posScale, minScaleMask);
            _mm_store_ps(quad.worldPosAndScale, posScale);
        }
#elif defined(MQENGINE_SIMD_WASM)
        const v128_t addVec = wasm_f32x4_make(0.0f, deltaY, 0.0f, deltaScale);
        const v128_t minScaleMask = wasm_f32x4_make(-1.0e30f, -1.0e30f, -1.0e30f, 0.5f);
        for (auto& quad : quads)
        {
            v128_t posScale = wasm_v128_load(quad.worldPosAndScale);
            posScale = wasm_f32x4_add(posScale, addVec);
            posScale = wasm_f32x4_pmax(minScaleMask, posScale);
            wasm_v128_store(quad.worldPosAndScale, posScale);
        }
#elif defined(MQENGINE_SIMD_NEON)
        const float addArr[4] = {0.0f, deltaY, 0.0f, deltaScale};
        const float minArr[4] = {-1.0e30f, -1.0e30f, -1.0e30f, 0.5f};
        const float32x4_t addVec = vld1q_f32(addArr);
        const float32x4_t minScaleMask = vld1q_f32(minArr);
        for (auto& quad : quads)
        {
            float32x4_t posScale = vld1q_f32(quad.worldPosAndScale);
            posScale = vaddq_f32(posScale, addVec);
            posScale = vmaxq_f32(posScale, minScaleMask);
            vst1q_f32(quad.worldPosAndScale, posScale);
        }
#else
        for (auto& quad : quads)
        {
            quad.worldPosAndScale[1] += deltaY;
            quad.worldPosAndScale[3] = std::max(0.5f, quad.worldPosAndScale[3] + deltaScale);
        }
#endif
    }
}
