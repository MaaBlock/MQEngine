/**
 * @file MemoryWarningCoordinator.cpp
 * @brief FEAT-08 抖音宿主内存告警联动 AsyncLoader、FontSystem 与 TransientResourcePool 实现
 */
#include "MemoryWarningCoordinator.h"

#include <Font/Composites/FontRuntimeComposite/FontRuntimeComposite.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <MQEngine/resource/core/resource.h>

namespace MQEngine::DouyinMiniGame
{
    MemoryWarningCoordinator::MemoryWarningCoordinator(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    Status MemoryWarningCoordinator::handleMemoryWarning(int32_t level)
    {
        if (level < 0)
        {
            return InvalidArgumentError("tt.onMemoryWarning 告警等级不能为负数");
        }

        const int32_t normalizedLevel = (level == 0) ? kLevelLow : level;

        uint64_t reclaimed = 0;
        reclaimed += purgeInactiveAssets(normalizedLevel);
        reclaimed += trimColdFontGlyphPages(normalizedLevel);
        reclaimed += purgeTransientRenderPool(normalizedLevel);

        ++m_stats.totalWarningsHandled;
        m_stats.lastWarningLevel = normalizedLevel;
        m_stats.totalBytesReclaimed += reclaimed;
        return OkStatus();
    }

    uint64_t MemoryWarningCoordinator::purgeInactiveAssets(int32_t level)
    {
        uint64_t freedBytes = 0;
        if (auto* asyncLoader = Asset::AsyncLoader::InstancePtr())
        {
            const uint64_t beforeBytes = asyncLoader->getTotalCacheUsageBytes();
            const uint64_t highMark = (level >= kLevelCritical)
                ? (32ull * 1024ull * 1024ull)
                : (96ull * 1024ull * 1024ull);
            const uint64_t lowMark = (level >= kLevelCritical)
                ? (16ull * 1024ull * 1024ull)
                : (64ull * 1024ull * 1024ull);
            asyncLoader->enforceCacheQuota(highMark, lowMark);
            const uint64_t afterBytes = asyncLoader->getTotalCacheUsageBytes();
            if (beforeBytes > afterBytes)
            {
                freedBytes += (beforeBytes - afterBytes);
            }
        }

        if (m_customAssetTrimCb)
        {
            freedBytes += m_customAssetTrimCb(level);
        }
        ++m_stats.assetCachePurgeCount;
        return freedBytes;
    }

    uint64_t MemoryWarningCoordinator::trimColdFontGlyphPages(int32_t level)
    {
        uint64_t freedBytes = 0;
        Resource::Font::FontSystem* fontSys = m_boundFontSystem;
        if (!fontSys)
        {
            fontSys = Font::FontRuntimeComposite::getActiveFontSystem();
        }

        if (fontSys)
        {
            const size_t pagesBefore = fontSys->getAtlasManager().getPageCount();
            fontSys->onMemoryWarning();
            const size_t pagesAfter = fontSys->getAtlasManager().getPageCount();
            if (pagesBefore > pagesAfter)
            {
                freedBytes += static_cast<uint64_t>(pagesBefore - pagesAfter) * 1024ull * 1024ull;
            }
        }

        if (m_customFontTrimCb)
        {
            freedBytes += m_customFontTrimCb(level);
        }
        ++m_stats.fontAtlasTrimCount;
        return freedBytes;
    }

    uint64_t MemoryWarningCoordinator::purgeTransientRenderPool(int32_t level)
    {
        uint64_t freedBytes = 0;
        if (m_boundTransientPool)
        {
            const size_t sizeBefore = m_boundTransientPool->getPoolSize();
            if (level >= kLevelCritical)
            {
                m_boundTransientPool->clear();
            }
            else
            {
                m_boundTransientPool->evictUnused();
            }
            const size_t sizeAfter = m_boundTransientPool->getPoolSize();
            if (sizeBefore > sizeAfter)
            {
                freedBytes += static_cast<uint64_t>(sizeBefore - sizeAfter) * 512ull * 1024ull;
            }
        }

        if (m_customRenderTrimCb)
        {
            freedBytes += m_customRenderTrimCb(level);
        }
        ++m_stats.renderPoolTrimCount;
        return freedBytes;
    }
} // namespace MQEngine::DouyinMiniGame
