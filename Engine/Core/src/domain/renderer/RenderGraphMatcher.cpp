/**
 * @file RenderGraphMatcher.cpp
 * @brief 双向位掩码匹配器、同 Pass 确定性稳定排序与空载/离线裁剪引擎实现
 */
#include <MQEngine/domain/renderer/RenderGraphMatcher.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/thirdparty/TomlBackend.h>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace MQEngine::Domain::Renderer
{
    namespace
    {
        bool isBlankToml(std::string_view text) noexcept
        {
            for (char ch : text)
            {
                if (!std::isspace(static_cast<unsigned char>(ch)))
                {
                    return false;
                }
            }
            return true;
        }

        std::vector<std::string> readTomlStringList(const toml::array* arr)
        {
            std::vector<std::string> out;
            if (!arr)
            {
                return out;
            }
            for (const auto& item : *arr)
            {
                if (auto s = item.value<std::string>())
                {
                    out.push_back(*s);
                }
            }
            return out;
        }
    } // namespace

    void RenderGraphMatcher::registerPipeline(const PipelineBindingDesc& desc)
    {
        for (auto& existing : m_pipelines)
        {
            if (existing.pipelineId == desc.pipelineId)
            {
                existing = desc;
                return;
            }
        }
        m_pipelines.push_back(desc);
    }

    void RenderGraphMatcher::unregisterPipelinesForTech(uint32_t techId)
    {
        m_pipelines.erase(
            std::remove_if(m_pipelines.begin(), m_pipelines.end(),
                [techId](const PipelineBindingDesc& p) { return p.ownerTechId == techId; }),
            m_pipelines.end());
    }

    void RenderGraphMatcher::clearPipelines()
    {
        m_pipelines.clear();
    }

    StatusOr<std::vector<PipelineBindingDesc>> RenderGraphMatcher::LoadTechFromToml(
        std::string_view tomlContent,
        uint32_t ownerTechId)
    {
        if (isBlankToml(tomlContent))
        {
            return InvalidArgumentError(".mqtech TOML 内容不能为空");
        }

        toml::parse_result parsed = toml::parse(tomlContent);
        if (parsed.failed())
        {
            return InvalidArgumentError(StrCat(".mqtech TOML 解析失败: ", parsed.error().description()));
        }

        const auto& root = parsed.table();
        uint32_t resolvedTechId = static_cast<uint32_t>(root["tech_id"].value_or(static_cast<int64_t>(ownerTechId)));
        if (const auto* techTbl = root["technique"].as_table())
        {
            resolvedTechId = static_cast<uint32_t>((*techTbl)["id"].value_or(static_cast<int64_t>(resolvedTechId)));
        }

        std::vector<PipelineBindingDesc> pipelines;
        const auto* pipeArr = root["pipelines"].as_array();
        if (!pipeArr)
        {
            return InvalidArgumentError(".mqtech 缺少 [[pipelines]] 数组定义");
        }

        uint32_t nextAutoId = resolvedTechId * 100u + 1u;
        for (const auto& elem : *pipeArr)
        {
            const auto* pTbl = elem.as_table();
            if (!pTbl)
            {
                continue;
            }

            PipelineBindingDesc desc;
            desc.ownerTechId = resolvedTechId;
            desc.pipelineId = static_cast<uint32_t>((*pTbl)["id"].value_or(static_cast<int64_t>(nextAutoId++)));
            desc.name = (*pTbl)["name"].value_or(std::string{});
            desc.globalMatch = (*pTbl)["global_match"].value_or(false);
            desc.order = static_cast<int32_t>((*pTbl)["order"].value_or(0));

            desc.vertexShaderPath = (*pTbl)["vertex_shader"].value_or(std::string{});
            desc.pixelShaderPath = (*pTbl)["pixel_shader"].value_or(
                (*pTbl)["fragment_shader"].value_or(std::string{}));
            desc.boundShaderId = (*pTbl)["shader"].value_or(
                !desc.pixelShaderPath.empty() ? desc.pixelShaderPath : desc.vertexShaderPath);

            const auto reqInputs = readTomlStringList((*pTbl)["required_input_tags"].as_array());
            const auto exclInputs = readTomlStringList((*pTbl)["excluded_input_tags"].as_array());
            const auto reqPasses = readTomlStringList((*pTbl)["required_pass_tags"].as_array());
            const auto exclPasses = readTomlStringList((*pTbl)["excluded_pass_tags"].as_array());

            desc.requiredInputTags = TagRegistry::StringsToInputMask(reqInputs);
            desc.excludedInputTags = TagRegistry::StringsToInputMask(exclInputs);
            desc.requiredPassTags = TagRegistry::StringsToPassMask(reqPasses);
            desc.excludedPassTags = TagRegistry::StringsToPassMask(exclPasses);

            desc.expectedVertexAttributes = readTomlStringList((*pTbl)["vertex_attributes"].as_array());
            desc.expectedRenderTargetCount = static_cast<uint32_t>((*pTbl)["render_target_count"].value_or(1));
            desc.requiresDepthWrite = (*pTbl)["depth_write"].value_or(true);

            if (const auto* samplersArr = (*pTbl)["pin_samplers"].as_array())
            {
                for (const auto& sElem : *samplersArr)
                {
                    if (const auto* sTbl = sElem.as_table())
                    {
                        PinSlotSamplerBinding binding;
                        binding.samplerName = (*sTbl)["sampler"].value_or(std::string{});
                        binding.pinSlot = (*sTbl)["slot"].value_or(std::string{});
                        if (!binding.samplerName.empty() && !binding.pinSlot.empty())
                        {
                            desc.pinSamplers.push_back(std::move(binding));
                        }
                    }
                }
            }

            pipelines.push_back(std::move(desc));
        }

        return pipelines;
    }

    StatusOr<std::vector<PipelineBindingDesc>> RenderGraphMatcher::LoadTechFromFile(
        std::string_view assetPath,
        uint32_t ownerTechId)
    {
        if (assetPath.empty())
        {
            return InvalidArgumentError(".mqtech 路径不能为空");
        }

        const std::string pathStr(assetPath);
        if (auto* res = Resource::Resource::InstancePtr())
        {
            if (auto* loader = res->getResourceLoader())
            {
                auto contentOr = loader->loadFile(pathStr);
                if (contentOr.ok())
                {
                    return LoadTechFromToml(*contentOr, ownerTechId);
                }
            }
        }

        if (FCT::SAI::FileSystem::exists(pathStr))
        {
            auto bytesOr = FCT::SAI::FileSystem::readBinaryFile(pathStr);
            if (!bytesOr.ok())
            {
                return bytesOr.status();
            }
            std::string text(reinterpret_cast<const char*>(bytesOr->data()), bytesOr->size());
            return LoadTechFromToml(text, ownerTechId);
        }

        return NotFoundError(StrCat("未找到 .mqtech 技术资产: ", pathStr));
    }

    std::vector<PipelineBindingDesc> RenderGraphMatcher::StripUnusedPipelinesForGraph(
        const RenderGraphAssetDesc& graphDesc,
        std::span<const PipelineBindingDesc> allPipelines)
    {
        std::vector<PipelineBindingDesc> kept;
        kept.reserve(allPipelines.size());

        for (const auto& pipe : allPipelines)
        {
            bool matchedAnyPass = false;
            for (const auto& pass : graphDesc.passes)
            {
                if (pipe.matchesPass(pass.tagMask))
                {
                    matchedAnyPass = true;
                    break;
                }
            }
            if (matchedAnyPass)
            {
                kept.push_back(pipe);
            }
        }

        return kept;
    }

    size_t RenderGraphMatcher::stripUnusedPipelinesForGraph(const RenderGraphAssetDesc& graphDesc)
    {
        const size_t beforeCount = m_pipelines.size();
        m_pipelines = StripUnusedPipelinesForGraph(graphDesc, m_pipelines);
        return beforeCount - m_pipelines.size();
    }

    Status RenderGraphMatcher::validateLayoutSignatures(
        const PipelineBindingDesc& pipe,
        const DrawBucket& bucket,
        uint32_t passRenderTargetCount,
        bool passHasDepthStencil)
    {
        if (pipe.requiresDepthWrite && !passHasDepthStencil)
        {
            return InvalidArgumentError("管线需要深度写入，但目标 Pass 缺失深度附件");
        }

        if (pipe.expectedRenderTargetCount != passRenderTargetCount)
        {
            return InvalidArgumentError("Pass 渲染目标附件数量与管线像素着色器输出数量不匹配");
        }

        if (!pipe.expectedVertexAttributes.empty())
        {
            std::unordered_set<std::string> available(bucket.attributeNames.begin(), bucket.attributeNames.end());
            for (const auto& expected : pipe.expectedVertexAttributes)
            {
                if (available.find(expected) == available.end())
                {
                    return InvalidArgumentError(StrCat("绘制桶缺失管线所需顶点属性: ", expected));
                }
            }
        }

        return OkStatus();
    }

    std::vector<const PipelineBindingDesc*> RenderGraphMatcher::matchPipelinesForPass(uint64_t passTagMask) const
    {
        std::vector<const PipelineBindingDesc*> matched;
        for (const auto& pipe : m_pipelines)
        {
            if (pipe.matchesPass(passTagMask))
            {
                matched.push_back(&pipe);
            }
        }
        return matched;
    }

    std::vector<PassExecutionItem> RenderGraphMatcher::evaluatePassExecutionItems(
        uint64_t passTagMask,
        const std::vector<DrawBucket>& frameBuckets) const
    {
        std::vector<PassExecutionItem> items;
        for (const auto& pipe : m_pipelines)
        {
            if (!pipe.matchesPass(passTagMask))
            {
                continue;
            }

            for (const auto& bucket : frameBuckets)
            {
                if (bucket.instanceCount == 0)
                {
                    continue;
                }

                if (pipe.matchesBucket(bucket))
                {
                    PassExecutionItem item;
                    item.order = pipe.order;
                    item.pipelineId = pipe.pipelineId;
                    item.pipeline = &pipe;
                    item.bucket = &bucket;
                    items.push_back(item);
                }
            }
        }

        // 确定性稳定排序：按 (order, pipelineId) 双升序排布 (FEAT-71)
        std::stable_sort(items.begin(), items.end(),
            [](const PassExecutionItem& a, const PassExecutionItem& b) {
                if (a.order != b.order)
                {
                    return a.order < b.order;
                }
                return a.pipelineId < b.pipelineId;
            });

        return items;
    }

    bool RenderGraphMatcher::hasActiveBucketsForPass(
        uint64_t passTagMask,
        const std::vector<DrawBucket>& frameBuckets) const
    {
        for (const auto& pipe : m_pipelines)
        {
            if (!pipe.matchesPass(passTagMask))
            {
                continue;
            }

            for (const auto& bucket : frameBuckets)
            {
                if (bucket.instanceCount > 0 && pipe.matchesBucket(bucket))
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::vector<std::string> RenderGraphMatcher::evaluateActivePassNames(
        const RenderGraphAssetDesc& graphDesc,
        const std::vector<DrawBucket>& frameBuckets) const
    {
        const size_t n = graphDesc.passes.size();
        std::vector<bool> hasWork(n, false);
        std::vector<bool> writesWindow(n, false);
        std::unordered_map<std::string, size_t> resourceWriter;

        for (size_t i = 0; i < n; ++i)
        {
            const auto& p = graphDesc.passes[i];
            hasWork[i] = hasActiveBucketsForPass(p.tagMask, frameBuckets);
            for (const auto& t : p.targets)
            {
                if (t.isWindow)
                {
                    writesWindow[i] = true;
                }
                else if (!t.imageName.empty())
                {
                    resourceWriter[t.imageName] = i;
                }
            }
            if (!p.depthStencilImage.empty())
            {
                resourceWriter[p.depthStencilImage] = i;
            }
        }

        // 反向引用计数：仅保留有活跃绘制桶且其产物最终服务于有效下游（或直写窗口）的 Pass
        std::vector<bool> alive(n, false);
        for (size_t idx = n; idx > 0; --idx)
        {
            const size_t i = idx - 1;
            if (!hasWork[i])
            {
                continue;
            }
            if (writesWindow[i] || alive[i])
            {
                alive[i] = true;
                for (const auto& tex : graphDesc.passes[i].textures)
                {
                    auto it = resourceWriter.find(tex.source);
                    if (it != resourceWriter.end() && hasWork[it->second])
                    {
                        alive[it->second] = true;
                    }
                }
            }
        }

        std::vector<std::string> result;
        for (size_t i = 0; i < n; ++i)
        {
            if (alive[i])
            {
                result.push_back(graphDesc.passes[i].name);
            }
        }
        return result;
    }
}
