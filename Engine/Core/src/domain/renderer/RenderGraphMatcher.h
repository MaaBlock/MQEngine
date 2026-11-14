/**
 * @file RenderGraphMatcher.h
 * @brief 双向位掩码匹配器、同 Pass 确定性稳定排序与空载/离线裁剪引擎 (PRD 10, FEAT-67~73)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_RENDERGRAPHMATCHER_H
#define MQENGINE_DOMAIN_RENDERER_RENDERGRAPHMATCHER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MQEngine::Domain::Renderer
{
    /**
     * @brief 引脚语义槽位绑定配置
     */
    struct PinSlotSamplerBinding
    {
        std::string samplerName;
        std::string pinSlot;
    };

    /**
     * @brief 管线绑定描述结构 (同构 PipelineDesc)
     */
    struct PipelineBindingDesc
    {
        uint32_t pipelineId{0};
        uint32_t ownerTechId{0};
        std::string name;
        bool globalMatch{false};

        uint64_t requiredInputTags{0};
        uint64_t excludedInputTags{0};
        uint64_t requiredPassTags{0};
        uint64_t excludedPassTags{0};

        int32_t order{0};
        std::string boundShaderId;
        std::string vertexShaderPath;
        std::string pixelShaderPath;

        std::vector<std::string> expectedVertexAttributes;
        uint32_t expectedRenderTargetCount{1};
        bool requiresDepthWrite{true};

        std::vector<PinSlotSamplerBinding> pinSamplers;

        [[nodiscard]] bool matchesPass(uint64_t passTagMask) const noexcept
        {
            if ((passTagMask & requiredPassTags) != requiredPassTags) return false;
            if ((passTagMask & excludedPassTags) != 0) return false;
            return true;
        }

        [[nodiscard]] bool matchesBucket(const DrawBucket& bucket) const noexcept
        {
            if (!globalMatch && ownerTechId != 0 && bucket.boundTechId != ownerTechId)
            {
                if (bucket.boundTechId != 0 || (bucket.inputTagsMask & InputTags::Viewport) == 0)
                {
                    return false;
                }
            }
            if ((bucket.inputTagsMask & requiredInputTags) != requiredInputTags) return false;
            if ((bucket.inputTagsMask & excludedInputTags) != 0) return false;
            return true;
        }
    };

    /**
     * @brief 单 Pass 内部执行项
     */
    struct PassExecutionItem
    {
        int32_t order{0};
        uint32_t pipelineId{0};
        const PipelineBindingDesc* pipeline{nullptr};
        const DrawBucket* bucket{nullptr};
    };

    /**
     * @brief 双向位掩码匹配器、布局签名校验与变体裁剪引擎
     */
    class ENGINE_API RenderGraphMatcher
    {
    public:
        RenderGraphMatcher() = default;

        void registerPipeline(const PipelineBindingDesc& desc);
        void unregisterPipelinesForTech(uint32_t techId);
        void clearPipelines();

        /**
         * @brief 从 .mqtech TOML 文本解析一组管线变体描述
         */
        static StatusOr<std::vector<PipelineBindingDesc>> LoadTechFromToml(
            std::string_view tomlContent,
            uint32_t ownerTechId = 1);

        /**
         * @brief 通过 ResourceLoader 从文件加载 .mqtech 资产
         */
        static StatusOr<std::vector<PipelineBindingDesc>> LoadTechFromFile(
            std::string_view assetPath,
            uint32_t ownerTechId = 1);

        /**
         * @brief 离线打包变体裁剪：仅保留能与目标 .pgraph 中任一 Pass 节点匹配的管线变体 (FEAT-73)
         */
        static std::vector<PipelineBindingDesc> StripUnusedPipelinesForGraph(
            const RenderGraphAssetDesc& graphDesc,
            std::span<const PipelineBindingDesc> allPipelines);

        /**
         * @brief 原地裁剪当前注册表中无法匹配目标渲染图的管线变体，返回被剥离的变体数
         */
        size_t stripUnusedPipelinesForGraph(const RenderGraphAssetDesc& graphDesc);

        /**
         * @brief 上下游双重布局签名防呆校验 (FEAT-69)
         */
        static Status validateLayoutSignatures(
            const PipelineBindingDesc& pipe,
            const DrawBucket& bucket,
            uint32_t passRenderTargetCount,
            bool passHasDepthStencil);

        std::vector<const PipelineBindingDesc*> matchPipelinesForPass(uint64_t passTagMask) const;

        std::vector<PassExecutionItem> evaluatePassExecutionItems(
            uint64_t passTagMask,
            const std::vector<DrawBucket>& frameBuckets) const;

        bool hasActiveBucketsForPass(
            uint64_t passTagMask,
            const std::vector<DrawBucket>& frameBuckets) const;

        /**
         * @brief 动态空载 Pass 旁路判定（含上游生产者无消费者时的级联裁剪）
         */
        std::vector<std::string> evaluateActivePassNames(
            const RenderGraphAssetDesc& graphDesc,
            const std::vector<DrawBucket>& frameBuckets) const;

        const std::vector<PipelineBindingDesc>& getPipelines() const noexcept { return m_pipelines; }

    private:
        std::vector<PipelineBindingDesc> m_pipelines;
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_RENDERGRAPHMATCHER_H
