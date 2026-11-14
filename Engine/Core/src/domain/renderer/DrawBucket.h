/**
 * @file DrawBucket.h
 * @brief 渲染图谱纯数据绘制桶与标签位掩码定义 (PRD 10, FEAT-67, FEAT-68)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_DRAWBUCKET_H
#define MQENGINE_DOMAIN_RENDERER_DRAWBUCKET_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace FCT::RHI
{
    class VertexBuffer;
    class IndexBuffer;
}

namespace MQEngine::Domain::Renderer
{
    namespace InputTags
    {
        inline constexpr uint64_t None        = 0ULL;
        inline constexpr uint64_t Mesh        = (1ULL << 0);
        inline constexpr uint64_t Static      = (1ULL << 1);
        inline constexpr uint64_t Skinned     = (1ULL << 2);
        inline constexpr uint64_t CastShadow  = (1ULL << 3);
        inline constexpr uint64_t Viewport    = (1ULL << 4);
        inline constexpr uint64_t HDR         = (1ULL << 5);
        inline constexpr uint64_t Bloom       = (1ULL << 6);
        inline constexpr uint64_t ToneMap     = (1ULL << 7);
        inline constexpr uint64_t LDR         = (1ULL << 8);
        inline constexpr uint64_t UI          = (1ULL << 9);
        inline constexpr uint64_t ScreenSpace = (1ULL << 10);
        inline constexpr uint64_t RmlUi       = (1ULL << 11);
        inline constexpr uint64_t Selected    = (1ULL << 12);
        inline constexpr uint64_t Opaque      = (1ULL << 13);
        inline constexpr uint64_t Translucent = (1ULL << 14);
        inline constexpr uint64_t InGameBaked = (1ULL << 15);
        inline constexpr uint64_t Text        = (1ULL << 16);
        inline constexpr uint64_t Toon        = (1ULL << 17);
        inline constexpr uint64_t Outline     = (1ULL << 18);
    }

    namespace PassTags
    {
        inline constexpr uint64_t None            = 0ULL;
        inline constexpr uint64_t Geometry        = (1ULL << 0);
        inline constexpr uint64_t Shadow          = (1ULL << 1);
        inline constexpr uint64_t Opaque          = (1ULL << 2);
        inline constexpr uint64_t Translucent     = (1ULL << 3);
        inline constexpr uint64_t ForwardPlus     = (1ULL << 4);
        inline constexpr uint64_t ForwardLite     = (1ULL << 5);
        inline constexpr uint64_t DeferredGBuffer = (1ULL << 6);
        inline constexpr uint64_t Lighting        = (1ULL << 7);
        inline constexpr uint64_t PostProcess     = (1ULL << 8);
        inline constexpr uint64_t Bloom           = (1ULL << 9);
        inline constexpr uint64_t ToneMap         = (1ULL << 10);
        inline constexpr uint64_t Overlay         = (1ULL << 11);
        inline constexpr uint64_t UI              = (1ULL << 12);
        inline constexpr uint64_t Background      = (1ULL << 13);
        inline constexpr uint64_t Skybox          = (1ULL << 14);
        inline constexpr uint64_t RayTracing      = (1ULL << 15);
        inline constexpr uint64_t Editor          = (1ULL << 16);
    }

    /**
     * @brief 标准布局纯数据 Pass 容器头 (FEAT-68: 严格零虚函数、零 Shader 引用)
     */
    struct PureDataPassNode
    {
        uint32_t id{0};
        uint64_t tagMask{0};
        uint32_t clearFlags{0};
        uint32_t targetCount{1};
        uint32_t hasDepthStencil{0};
        uint32_t discardDepthOnExit{0};
        uint32_t boundPipelines[8]{0};
        uint32_t boundCount{0};

        Status bindPipeline(uint32_t pipelineId) noexcept
        {
            if (pipelineId == 0)
            {
                return InvalidArgumentError("禁止绑定 ID 为 0 的无效管线");
            }
            if (boundCount >= 8)
            {
                return ResourceExhaustedError("单 Pass 静态管线槽位已满");
            }
            boundPipelines[boundCount++] = pipelineId;
            return OkStatus();
        }

        [[nodiscard]] std::span<const uint32_t> getBoundPipelines() const noexcept
        {
            return std::span<const uint32_t>(boundPipelines, boundCount);
        }
    };
    static_assert(std::is_standard_layout_v<PureDataPassNode>, "PureDataPassNode 必须保持标准布局纯数据属性");

    /**
     * @brief 纯数据绘制桶 (DrawBucket)
     */
    struct DrawBucket
    {
        uint32_t boundTechId{0};
        uint64_t inputTagsMask{0};
        uint32_t instanceCount{0};
        uint32_t vertexLayoutId{0};
        std::vector<std::string> attributeNames;
        std::vector<uint32_t> instanceSlots;
        FCT::RHI::VertexBuffer* vertexBuffer{nullptr};
        FCT::RHI::IndexBuffer* indexBuffer{nullptr};
        uint32_t indexCount{0};
        uint32_t startIndex{0};
        int32_t  baseVertex{0};

        DrawBucket() = default;
        DrawBucket(uint32_t techId, uint64_t mask, uint32_t instances = 0)
            : boundTechId(techId), inputTagsMask(mask), instanceCount(instances) {}
    };

    /**
     * @brief 字符串标签到位掩码映射注册器
     */
    class TagRegistry
    {
    public:
        static uint64_t StringToInputTag(std::string_view tag) noexcept
        {
            if (tag == "Mesh")        return InputTags::Mesh;
            if (tag == "Static")      return InputTags::Static;
            if (tag == "Skinned")     return InputTags::Skinned;
            if (tag == "CastShadow")  return InputTags::CastShadow;
            if (tag == "Viewport")    return InputTags::Viewport;
            if (tag == "HDR")         return InputTags::HDR;
            if (tag == "Bloom")       return InputTags::Bloom;
            if (tag == "ToneMap")     return InputTags::ToneMap;
            if (tag == "LDR")         return InputTags::LDR;
            if (tag == "UI")          return InputTags::UI;
            if (tag == "ScreenSpace") return InputTags::ScreenSpace;
            if (tag == "RmlUi")       return InputTags::RmlUi;
            if (tag == "Selected")    return InputTags::Selected;
            if (tag == "Opaque")      return InputTags::Opaque;
            if (tag == "Translucent") return InputTags::Translucent;
            if (tag == "InGameBaked") return InputTags::InGameBaked;
            if (tag == "Text")        return InputTags::Text;
            if (tag == "Toon")        return InputTags::Toon;
            if (tag == "Outline")     return InputTags::Outline;
            return InputTags::None;
        }

        static uint64_t StringToPassTag(std::string_view tag) noexcept
        {
            if (tag == "Geometry")        return PassTags::Geometry;
            if (tag == "Shadow" || tag == "ShadowAtlas") return PassTags::Shadow;
            if (tag == "Opaque")          return PassTags::Opaque;
            if (tag == "Translucent")     return PassTags::Translucent;
            if (tag == "ForwardPlus")     return PassTags::ForwardPlus;
            if (tag == "ForwardLite")     return PassTags::ForwardLite;
            if (tag == "DeferredGBuffer") return PassTags::DeferredGBuffer;
            if (tag == "Lighting")        return PassTags::Lighting;
            if (tag == "PostProcess")     return PassTags::PostProcess;
            if (tag == "Bloom")           return PassTags::Bloom;
            if (tag == "ToneMap")         return PassTags::ToneMap;
            if (tag == "Overlay")         return PassTags::Overlay;
            if (tag == "UI")              return PassTags::UI;
            if (tag == "Background")      return PassTags::Background;
            if (tag == "Skybox")          return PassTags::Skybox;
            if (tag == "RayTracing" || tag == "RTShadow") return PassTags::RayTracing;
            if (tag == "Editor")          return PassTags::Editor;
            return PassTags::None;
        }

        static uint64_t StringsToInputMask(const std::vector<std::string>& tags) noexcept
        {
            uint64_t mask = 0;
            for (const auto& t : tags)
            {
                mask |= StringToInputTag(t);
            }
            return mask;
        }

        static uint64_t StringsToPassMask(const std::vector<std::string>& tags) noexcept
        {
            uint64_t mask = 0;
            for (const auto& t : tags)
            {
                mask |= StringToPassTag(t);
            }
            return mask;
        }
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_DRAWBUCKET_H
