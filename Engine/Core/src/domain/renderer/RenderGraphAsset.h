/**
 * @file RenderGraphAsset.h
 * @brief 纯数据 .pgraph 资产解析器与四大预设工厂 (PRD 10, FEAT-10, FEAT-68, FEAT-70, FEAT-72)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_RENDERGRAPHASSET_H
#define MQENGINE_DOMAIN_RENDERER_RENDERGRAPHASSET_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Domain::Renderer
{
    /**
     * @brief 引脚语义槽位连线声明 (Pin Slot)
     */
    struct PassTextureInput
    {
        std::string slot;
        std::string source;
        std::string fallback;
    };

    /**
     * @brief 颜色附件输出绑定描述
     */
    struct PassTargetBinding
    {
        std::string imageName;
        bool isWindow{false};
    };

    /**
     * @brief 纯数据 Pass 节点描述 (严格 0 Shader 字段)
     */
    struct RenderGraphPassNodeDesc
    {
        std::string name;
        std::vector<std::string> tags;
        uint64_t tagMask{0};

        bool hasClearColor{false};
        FCT::Vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
        bool hasClearDepth{false};
        float clearDepth{1.0f};
        bool discardDepthAfterPass{false};

        std::vector<PassTargetBinding> targets;
        std::string depthStencilImage;

        std::vector<PassTextureInput> textures;
        std::vector<std::string> readGroups;
        std::vector<std::string> writeGroups;

        [[nodiscard]] bool hasTag(std::string_view tag) const noexcept
        {
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }

        [[nodiscard]] PureDataPassNode toPureDataNode(uint32_t passId = 0) const noexcept
        {
            PureDataPassNode node{};
            node.id = passId;
            node.tagMask = tagMask;
            node.clearFlags = (hasClearColor ? 1u : 0u) | (hasClearDepth ? 2u : 0u);
            node.targetCount = static_cast<uint32_t>(targets.size());
            node.hasDepthStencil = depthStencilImage.empty() ? 0u : 1u;
            node.discardDepthOnExit = discardDepthAfterPass ? 1u : 0u;
            return node;
        }
    };

    /**
     * @brief 瞬态或持久化图像资源描述
     */
    struct RenderGraphImageDesc
    {
        std::string name;
        std::string format;
        float scaleX{1.0f};
        float scaleY{1.0f};
        uint32_t width{0};
        uint32_t height{0};
    };

    /**
     * @brief 纯数据 .pgraph 资产结构
     */
    struct ENGINE_API RenderGraphAssetDesc
    {
        std::string name;
        std::string version{"3.0"};
        std::vector<std::string> resourceGroups;
        std::vector<RenderGraphImageDesc> images;
        std::vector<RenderGraphPassNodeDesc> passes;

        [[nodiscard]] const RenderGraphPassNodeDesc* findPass(std::string_view passName) const noexcept;
        [[nodiscard]] const RenderGraphImageDesc* findImage(std::string_view imageName) const noexcept;
        [[nodiscard]] bool isAcyclic() const;

        [[nodiscard]] bool isDirectToBackbuffer() const noexcept;
        [[nodiscard]] size_t countOffscreenColorImages() const noexcept;
        [[nodiscard]] uint32_t getOffscreenColorAttachmentCount() const noexcept
        {
            return static_cast<uint32_t>(countOffscreenColorImages());
        }

        /**
         * @brief 按 Pass 名称与 Pin Slot 解析绑定的物理纹理 URI（未连线时返回 fallback）
         */
        [[nodiscard]] StatusOr<std::string> resolvePinSlot(std::string_view passName, std::string_view slotName) const;

        /**
         * @brief 校验深度丢弃 (discard_depth / glInvalidateFramebuffer) 是否与下游深度采样冲突
         */
        [[nodiscard]] Status validateDepthSamplingHazard() const;

        /**
         * @brief 汇总整张渲染图所有 Pass 节点的标签位掩码并集（供离线变体裁剪使用）
         */
        [[nodiscard]] uint64_t collectUnionPassTagMask() const noexcept;
    };

    /**
     * @brief 纯数据帧图资产解析器与四大预设工厂
     */
    class ENGINE_API RenderGraphAsset
    {
    public:
        static StatusOr<RenderGraphAssetDesc> LoadFromToml(std::string_view tomlContent);
        static StatusOr<RenderGraphAssetDesc> LoadFromFile(std::string_view assetPath);

        static RenderGraphAssetDesc CreateDesktopDeferredHdrPreset();
        static RenderGraphAssetDesc CreateDesktopHybridRayTracingPreset();
        static RenderGraphAssetDesc CreateMiniGameForwardPlusPreset();
        static RenderGraphAssetDesc CreateMiniGameForwardLitePreset();

        static Status ApplyToFctRenderGraph(
            const RenderGraphAssetDesc& desc,
            FCT::RenderGraph* rg,
            FCT::Window* window,
            FCT::Image* externalColorTarget = nullptr,
            FCT::Image* externalDepthTarget = nullptr);
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_RENDERGRAPHASSET_H
