/**
 * @file CapsuleAndOpenDataService.cpp
 * @brief FEAT-07 & FEAT-54 胶囊避让 CSS 注入、开放数据域按需同步及首屏 Splash 接力实现
 */
#include "CapsuleAndOpenDataService.h"

#include <Composites/RmlUiComposite/RmlUiComposite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        float clampNonNegativeFinite(float val, float maxBound) noexcept
        {
            if (!std::isfinite(val) || val < 0.0f)
            {
                return 0.0f;
            }
            return std::min(val, maxBound);
        }

        std::string formatCssPx(float px)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fpx", static_cast<double>(px));
            return std::string(buf);
        }
    } // namespace

    CapsuleAvoidanceBridge::CapsuleAvoidanceBridge(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    CapsuleSafeAreaCssMetrics CapsuleAvoidanceBridge::computeSanitizedMetrics(
        const DouyinSystemSafeArea& sysArea,
        const DouyinCapsuleRect& rawCapsule)
    {
        const float winW = (std::isfinite(sysArea.windowWidth) && sysArea.windowWidth > 0.0f)
            ? sysArea.windowWidth
            : 390.0f;
        const float winH = (std::isfinite(sysArea.windowHeight) && sysArea.windowHeight > 0.0f)
            ? sysArea.windowHeight
            : 844.0f;

        CapsuleSafeAreaCssMetrics m{};
        m.capsuleAvoidTopPx = clampNonNegativeFinite(rawCapsule.top, winH);
        m.capsuleAvoidWidthPx = clampNonNegativeFinite(rawCapsule.width, winW);
        m.capsuleAvoidHeightPx = clampNonNegativeFinite(rawCapsule.height, winH);

        // 右上角避让安全边距 = 窗口总宽 - 胶囊左边缘（若胶囊异常全零或越界则安全钳制至 >=0）
        if (std::isfinite(rawCapsule.left) && rawCapsule.left > 0.0f && rawCapsule.left <= winW && m.capsuleAvoidWidthPx > 0.0f)
        {
            m.capsuleAvoidRightPx = clampNonNegativeFinite(winW - rawCapsule.left + 8.0f, winW);
        }
        else
        {
            m.capsuleAvoidRightPx = m.capsuleAvoidWidthPx;
        }

        m.safeTopPx = clampNonNegativeFinite(sysArea.safeTop, winH);
        m.safeLeftPx = clampNonNegativeFinite(sysArea.safeLeft, winW);
        m.safeBottomPx = (std::isfinite(sysArea.safeBottom) && sysArea.safeBottom > 0.0f && sysArea.safeBottom <= winH)
            ? (winH - sysArea.safeBottom)
            : 0.0f;
        m.safeRightPx = (std::isfinite(sysArea.safeRight) && sysArea.safeRight > 0.0f && sysArea.safeRight <= winW)
            ? (winW - sysArea.safeRight)
            : 0.0f;

        return m;
    }

    CapsuleSafeAreaCssMetrics CapsuleAvoidanceBridge::refreshAndInjectCssVariables()
    {
        DouyinSystemSafeArea sysArea{};
        DouyinCapsuleRect capsuleRect{};
        if (m_hostApi)
        {
            sysArea = m_hostApi->querySystemSafeArea();
            capsuleRect = m_hostApi->queryMenuButtonCapsuleRect();
        }
        return applyExplicitRects(sysArea, capsuleRect);
    }

    CapsuleSafeAreaCssMetrics CapsuleAvoidanceBridge::applyExplicitRects(
        const DouyinSystemSafeArea& sysArea,
        const DouyinCapsuleRect& rawCapsule)
    {
        m_lastMetrics = computeSanitizedMetrics(sysArea, rawCapsule);
        pushMetricsToRmlUi(m_lastMetrics);
        return m_lastMetrics;
    }

    void CapsuleAvoidanceBridge::pushMetricsToRmlUi(const CapsuleSafeAreaCssMetrics& metrics)
    {
        m_injectedCssVars["--mq-capsule-avoid-top"] = formatCssPx(metrics.capsuleAvoidTopPx);
        m_injectedCssVars["--mq-capsule-avoid-right"] = formatCssPx(metrics.capsuleAvoidRightPx);
        m_injectedCssVars["--mq-capsule-avoid-width"] = formatCssPx(metrics.capsuleAvoidWidthPx);
        m_injectedCssVars["--mq-capsule-avoid-height"] = formatCssPx(metrics.capsuleAvoidHeightPx);
        m_injectedCssVars["--mq-safe-top"] = formatCssPx(metrics.safeTopPx);
        m_injectedCssVars["--mq-safe-bottom"] = formatCssPx(metrics.safeBottomPx);
        m_injectedCssVars["--mq-safe-left"] = formatCssPx(metrics.safeLeftPx);
        m_injectedCssVars["--mq-safe-right"] = formatCssPx(metrics.safeRightPx);

        if (m_rmlUi)
        {
            for (const auto& [propName, propVal] : m_injectedCssVars)
            {
                (void)m_rmlUi->setElementStyleProperty(m_rootElementId, propName, propVal);
            }
        }
    }

    OpenDataDomainBridge::OpenDataDomainBridge(IDouyinHostApi* hostApi)
        : m_hostApi(hostApi)
    {
    }

    bool OpenDataDomainBridge::isBalancedJsonObject(std::string_view jsonText) noexcept
    {
        size_t first = 0;
        while (first < jsonText.size() && (jsonText[first] == ' ' || jsonText[first] == '\t' || jsonText[first] == '\n' || jsonText[first] == '\r'))
        {
            ++first;
        }
        size_t last = jsonText.size();
        while (last > first && (jsonText[last - 1] == ' ' || jsonText[last - 1] == '\t' || jsonText[last - 1] == '\n' || jsonText[last - 1] == '\r'))
        {
            --last;
        }
        if (last <= first || jsonText[first] != '{' || jsonText[last - 1] != '}')
        {
            return false;
        }

        int braceDepth = 0;
        bool inQuotes = false;
        bool escaped = false;
        for (size_t i = first; i < last; ++i)
        {
            const char c = jsonText[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (c == '\\' && inQuotes)
            {
                escaped = true;
                continue;
            }
            if (c == '"')
            {
                inQuotes = !inQuotes;
                continue;
            }
            if (!inQuotes)
            {
                if (c == '{') ++braceDepth;
                else if (c == '}')
                {
                    --braceDepth;
                    if (braceDepth < 0) return false;
                }
            }
        }
        return !inQuotes && braceDepth == 0;
    }

    Status OpenDataDomainBridge::postMessage(const std::string& jsonPayload)
    {
        if (!isBalancedJsonObject(jsonPayload))
        {
            return InvalidArgumentError("OpenDataContext.postMessage 仅允许格式合法的非空 JSON 对象载荷");
        }
        if (m_hostApi)
        {
            CHECK_STATUS(m_hostApi->postOpenDataMessage(jsonPayload));
        }
        m_lastPostedJson = jsonPayload;
        ++m_postedMessageCount;
        return OkStatus();
    }

    void OpenDataDomainBridge::setLeaderboardVisible(bool visible) noexcept
    {
        m_leaderboardVisible = visible;
        if (!visible)
        {
            // 关闭排行榜面板后立即清零累加器并停止纹理同步，达成后台 0 开销休眠
            m_accumulatedSec = 0.0f;
        }
    }

    void OpenDataDomainBridge::setSyncIntervalSeconds(float intervalSec) noexcept
    {
        if (std::isfinite(intervalSec) && intervalSec >= 0.016f)
        {
            m_syncIntervalSec = intervalSec;
        }
    }

    void OpenDataDomainBridge::tick(float deltaTime)
    {
        if (!m_leaderboardVisible || deltaTime <= 0.0f)
        {
            return;
        }

        m_accumulatedSec += deltaTime;
        if (m_accumulatedSec >= m_syncIntervalSec)
        {
            m_accumulatedSec = 0.0f;
            performSharedCanvasUpload();
        }
    }

    void OpenDataDomainBridge::performSharedCanvasUpload()
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;
        if (m_hostApi && !m_hostApi->readSharedRankCanvasRgba(width, height, rgba))
        {
            return;
        }
        if (width == 0 || height == 0)
        {
            width = 256u;
            height = 256u;
            rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
        }

        if (m_targetRankImage && !rgba.empty())
        {
            m_targetRankImage->updateSubRegion(
                0u, 0u, width, height, 0u, 0u, rgba.data(), static_cast<size_t>(width) * 4u);
        }
        ++m_sharedCanvasSyncCount;
    }

    void SplashRelayCoordinator::notifyFirstFrameRendered()
    {
        if (m_splashDismissed)
        {
            return;
        }
        m_splashDismissed = true;
        if (m_hostApi)
        {
            m_hostApi->dismissBootSplash();
        }
    }
} // namespace MQEngine::DouyinMiniGame
