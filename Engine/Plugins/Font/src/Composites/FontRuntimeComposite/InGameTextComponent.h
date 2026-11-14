/**
 * @file InGameTextComponent.h
 * @brief 游戏内跳字组件、紧凑实例 SIMD 动画计算与合批渲染器
 */
#ifndef MQENGINE_FONT_INGAMETEXTCOMPONENT_H
#define MQENGINE_FONT_INGAMETEXTCOMPONENT_H

#include "../../FontApi.h"
#include "FontTypes.h"
#include "BakedFont.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine::Resource::Font
{
    struct ActiveTextItem
    {
        std::string text;
        float worldPos[3]{0.0f, 0.0f, 0.0f};
        float scale{1.0f};
        float lifeTime{0.0f};
        float maxLife{1.5f};
        uint32_t fillColor{0xFFFFFFFFu};
        uint32_t outlineColor{0x00000002u};
        bool isCritical{false};
    };

    /**
     * @brief 游戏内高频跳字合批组件 (FEAT-57)
     */
    class FONT_RUNTIME_COMPOSITE_API InGameTextComponent
    {
    public:
        static constexpr uint32_t DefaultMaxBatchSize = 1024u;

        InGameTextComponent() = default;
        ~InGameTextComponent() = default;

        void spawnDamageNumber(
            const std::string& text,
            const FCT::Vec3<>& pos,
            float scale = 1.0f,
            uint32_t color = 0xFFFFFFFFu,
            bool isCritical = false);

        void tickItems(float dt);

        void buildQuadInstances(
            const BakedFont& font,
            std::vector<GpuInGameTextQuadInstance>& outQuads) const;

        static void evaluateBatchAnimations(std::span<GpuInGameTextQuadInstance> quads, float dt) noexcept;
        void evaluateAnimations(std::span<GpuInGameTextQuadInstance> quads, float dt) noexcept
        {
            evaluateBatchAnimations(quads, dt);
        }

        static uint32_t calculateDrawCallCount(size_t quadCount, uint32_t maxBatchSize = DefaultMaxBatchSize) noexcept
        {
            if (quadCount == 0 || maxBatchSize == 0)
            {
                return 0u;
            }
            return static_cast<uint32_t>((quadCount + maxBatchSize - 1u) / maxBatchSize);
        }

        [[nodiscard]] const std::vector<ActiveTextItem>& getActiveItems() const noexcept { return m_activeItems; }
        void clear() noexcept { m_activeItems.clear(); }

    private:
        std::vector<ActiveTextItem> m_activeItems;
    };
}

#endif // MQENGINE_FONT_INGAMETEXTCOMPONENT_H
