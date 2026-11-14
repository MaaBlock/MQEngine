/**
 * @file FontTypes.h
 * @brief 双轨字体系统基础类型、紧凑跳字实例、MSDF 距离场位图与分轨分类器
 */
#ifndef MQENGINE_FONT_FONTTYPES_H
#define MQENGINE_FONT_FONTTYPES_H

#include "../../FontApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine::Resource::Font
{
    /**
     * @brief 字体分轨类型 (FEAT-55)
     */
    enum class FontTrack : uint8_t
    {
        InGameBaked = 0, // 轨道 A: 封闭游戏内文字 (MSDF / 艺术位图，离线烘焙，运行时零光栅化)
        UiRuntime   = 1  // 轨道 B: 开放 UI 动态文字 (动态图集，支持系统字库兜底与彩色 Emoji)
    };

    /**
     * @brief 字体后端模式 (FEAT-58)
     */
    enum class FontBackendMode : uint8_t
    {
        Desktop_FreeType = 0, // 桌面端: FreeType 光栅化 + Hinting 启用，DirectWrite 系统兜底
        MiniGame_Hybrid  = 1  // 小游戏端: 优先命中预烘焙常用字子集，未命中回退离屏 2D Canvas
    };

    /**
     * @brief 图集像素格式 (FEAT-59)
     */
    enum class TextureFormat : uint8_t
    {
        R8_UNORM       = 0, // 单通道灰阶抗锯齿 (省 75% 显存)
        R8G8B8A8_UNORM = 1  // 彩色 Emoji / 彩色艺术字
    };

    /**
     * @brief 单个字形度量数据
     */
    struct GlyphMetric
    {
        float advanceX = 0.0f;
        float advanceY = 0.0f;
        float bearingX = 0.0f;
        float bearingY = 0.0f;
        float width    = 0.0f;
        float height   = 0.0f;
        uint16_t uvMinMax[4] = {0, 0, 0, 0}; // UNORM16 [uMin, vMin, uMax, vMax]
    };

    /**
     * @brief 轨道 B 动态 UI 缓存字形查询结果 (供 HtmlUI / 渲染前端消费)
     */
    struct CachedUiGlyph
    {
        uint32_t pageId{0};
        TextureFormat format{TextureFormat::R8_UNORM};
        GlyphMetric metric{};
        uint32_t generation{0};
    };

    /**
     * @brief 离线烘焙 MSDF 多通道有向距离场位图切片 (FEAT-56)
     */
    struct MsdfGlyphBitmap
    {
        char32_t codepoint{0};
        uint32_t width{0};
        uint32_t height{0};
        uint32_t channels{3};
        std::vector<float> distanceField;

        static MsdfGlyphBitmap generate(char32_t cp, uint32_t w, uint32_t h, float pxRange = 4.0f)
        {
            MsdfGlyphBitmap bmp;
            bmp.codepoint = cp;
            bmp.width = w;
            bmp.height = h;
            bmp.channels = 3;
            if (w == 0 || h == 0)
            {
                return bmp;
            }

            bmp.distanceField.resize(static_cast<size_t>(w) * h * 3u, 0.0f);
            const float centerX = static_cast<float>(w) * 0.5f;
            const float centerY = static_cast<float>(h) * 0.5f;
            const float maxR = std::max(1.0f, std::min(centerX, centerY));
            const float rangeScale = std::max(1.0f, pxRange) / maxR;

            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    const float dx = static_cast<float>(x) - centerX;
                    const float dy = static_cast<float>(y) - centerY;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float signedDist = (maxR * 0.6f) - dist;
                    const float normR = std::clamp(0.5f + signedDist / (maxR * 0.8f), 0.0f, 1.0f);
                    const float edgeBias = 0.02f * rangeScale * ((dx * dy >= 0.0f) ? 1.0f : -1.0f);
                    const size_t idx = (static_cast<size_t>(y) * w + x) * 3u;
                    bmp.distanceField[idx + 0] = std::clamp(normR + edgeBias, 0.0f, 1.0f);
                    bmp.distanceField[idx + 1] = normR;
                    bmp.distanceField[idx + 2] = std::clamp(normR - edgeBias, 0.0f, 1.0f);
                }
            }
            return bmp;
        }

        [[nodiscard]] float sampleDistance(uint32_t x, uint32_t y, uint32_t ch) const noexcept
        {
            if (x >= width || y >= height || ch >= channels)
            {
                return 0.0f;
            }
            return distanceField[(static_cast<size_t>(y) * width + x) * channels + ch];
        }
    };

    /**
     * @brief 严格 32 字节紧凑跳字四边形顶点实例结构 (FEAT-57)
     */
    struct alignas(16) GpuInGameTextQuadInstance
    {
        float worldPosAndScale[4];    // 16B: xyz=世界坐标锚点, w=缩放倍率
        uint16_t uvMinMax[4];         // 8B : 4x UNORM16 [uMin, vMin, uMax, vMax] (0~65535)
        uint32_t fillColorRgba8;      // 4B : 正文填充色 RGBA8 (支持暴击渐变调制)
        uint32_t outlineColorAndPx;   // 4B : RGB 描边色 + A 低8位为描边像素宽度参数
    };
    static_assert(sizeof(GpuInGameTextQuadInstance) == 32, "GpuInGameTextQuadInstance 必须为严格 32 字节");
    static_assert(alignof(GpuInGameTextQuadInstance) == 16, "GpuInGameTextQuadInstance 必须对齐 16 字节");

    /**
     * @brief 双轨文本分类器 (FEAT-55)
     */
    class FONT_RUNTIME_COMPOSITE_API FontTrackClassifier
    {
    public:
        static bool isClosedCombatCjk(char32_t cp) noexcept
        {
            switch (cp)
            {
            case U'暴':
            case U'击':
            case U'闪':
            case U'避':
            case U'格':
            case U'挡':
            case U'免':
            case U'疫':
                return true;
            default:
                return false;
            }
        }

        static bool decodeNextUtf8(std::string_view str, size_t& ioIndex, char32_t& outCp) noexcept
        {
            if (ioIndex >= str.size())
            {
                return false;
            }
            const unsigned char c0 = static_cast<unsigned char>(str[ioIndex]);
            if (c0 < 0x80u)
            {
                outCp = static_cast<char32_t>(c0);
                ioIndex += 1;
                return true;
            }
            if ((c0 & 0xE0u) == 0xC0u && ioIndex + 1 < str.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(str[ioIndex + 1]);
                if ((c1 & 0xC0u) != 0x80u) return false;
                outCp = (static_cast<char32_t>(c0 & 0x1Fu) << 6) | static_cast<char32_t>(c1 & 0x3Fu);
                ioIndex += 2;
                return true;
            }
            if ((c0 & 0xF0u) == 0xE0u && ioIndex + 2 < str.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(str[ioIndex + 1]);
                const unsigned char c2 = static_cast<unsigned char>(str[ioIndex + 2]);
                if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) return false;
                outCp = (static_cast<char32_t>(c0 & 0x0Fu) << 12) |
                        (static_cast<char32_t>(c1 & 0x3Fu) << 6) |
                        static_cast<char32_t>(c2 & 0x3Fu);
                ioIndex += 3;
                return true;
            }
            if ((c0 & 0xF8u) == 0xF0u && ioIndex + 3 < str.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(str[ioIndex + 1]);
                const unsigned char c2 = static_cast<unsigned char>(str[ioIndex + 2]);
                const unsigned char c3 = static_cast<unsigned char>(str[ioIndex + 3]);
                if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) return false;
                outCp = (static_cast<char32_t>(c0 & 0x07u) << 18) |
                        (static_cast<char32_t>(c1 & 0x3Fu) << 12) |
                        (static_cast<char32_t>(c2 & 0x3Fu) << 6) |
                        static_cast<char32_t>(c3 & 0x3Fu);
                ioIndex += 4;
                return true;
            }
            return false;
        }

        static FontTrack classifyTrack(std::string_view str) noexcept
        {
            if (str.empty())
            {
                return FontTrack::UiRuntime;
            }

            size_t idx = 0;
            while (idx < str.size())
            {
                char32_t cp = 0;
                if (!decodeNextUtf8(str, idx, cp))
                {
                    return FontTrack::UiRuntime;
                }

                // 私有保留区 (U+E000 ~ U+F8FF) 与彩色 Emoji 统一走轨道 B 动态图集
                if ((cp >= 0xE000u && cp <= 0xF8FFu) || cp >= 0x10000u || (cp >= 0x2600u && cp <= 0x27BFu))
                {
                    return FontTrack::UiRuntime;
                }

                // 非 ASCII 字符若不在封闭战斗白名单内，路由至开放 UI 轨道 B
                if (cp >= 0x80u && !isClosedCombatCjk(cp))
                {
                    return FontTrack::UiRuntime;
                }
            }

            return FontTrack::InGameBaked;
        }
    };
}

#endif // MQENGINE_FONT_FONTTYPES_H
