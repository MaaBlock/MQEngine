/**
 * @file MemoryWarningCoordinator.h
 * @brief FEAT-08：抖音 tt.onMemoryWarning 分级联动 Asset 非活跃缓存、FontSystem 冷门字形页与 TransientResourcePool 自救协调器
 */
#ifndef MQENGINE_DOUYIN_MEMORY_WARNING_COORDINATOR_H
#define MQENGINE_DOUYIN_MEMORY_WARNING_COORDINATOR_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinWebJsAdapter.h"

#include <Font/Composites/FontRuntimeComposite/FontSystem.h>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 抖音宿主内存告警分级清理统计账本
     */
    struct MemoryReclaimStats
    {
        uint32_t totalWarningsHandled{0};
        int32_t lastWarningLevel{0};
        uint32_t assetCachePurgeCount{0};
        uint32_t fontAtlasTrimCount{0};
        uint32_t renderPoolTrimCount{0};
        uint64_t totalBytesReclaimed{0};
    };

    /**
     * @brief 宿主内存告警三级联动自救中枢（FEAT-08）
     */
    class DOUYIN_PLATFORM_RUNTIME_API MemoryWarningCoordinator
    {
    public:
        static constexpr int32_t kLevelModerate = 5;
        static constexpr int32_t kLevelLow = 10;
        static constexpr int32_t kLevelCritical = 15;

        using CustomSubSystemTrimCallback = std::function<uint64_t(int32_t level)>;

        explicit MemoryWarningCoordinator(IDouyinHostApi* hostApi = nullptr);
        ~MemoryWarningCoordinator() = default;

        void setHostApi(IDouyinHostApi* hostApi) noexcept { m_hostApi = hostApi; }
        void bindFontSystem(Resource::Font::FontSystem* fontSystem) noexcept { m_boundFontSystem = fontSystem; }
        void bindTransientResourcePool(FCT::TransientResourcePool* pool) noexcept { m_boundTransientPool = pool; }

        void setAssetCacheTrimCallback(CustomSubSystemTrimCallback cb) { m_customAssetTrimCb = std::move(cb); }
        void setFontAtlasTrimCallback(CustomSubSystemTrimCallback cb) { m_customFontTrimCb = std::move(cb); }
        void setRenderPoolTrimCallback(CustomSubSystemTrimCallback cb) { m_customRenderTrimCb = std::move(cb); }

        Status handleMemoryWarning(int32_t level);

        const MemoryReclaimStats& getStats() const noexcept { return m_stats; }
        void resetStats() noexcept { m_stats = MemoryReclaimStats{}; }

    private:
        uint64_t purgeInactiveAssets(int32_t level);
        uint64_t trimColdFontGlyphPages(int32_t level);
        uint64_t purgeTransientRenderPool(int32_t level);

    private:
        IDouyinHostApi* m_hostApi{nullptr};
        Resource::Font::FontSystem* m_boundFontSystem{nullptr};
        FCT::TransientResourcePool* m_boundTransientPool{nullptr};
        CustomSubSystemTrimCallback m_customAssetTrimCb;
        CustomSubSystemTrimCallback m_customFontTrimCb;
        CustomSubSystemTrimCallback m_customRenderTrimCb;
        MemoryReclaimStats m_stats;
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_MEMORY_WARNING_COORDINATOR_H
