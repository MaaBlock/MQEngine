/**
 * @file DouyinShaderCooker.cpp
 * @brief Step 1 着色器变体裁剪（.pgraph ⋈ .mqtech）与 SPIRV-Cross #version 300 es 离线转译实现
 */
#include "DouyinShaderCooker.h"
#include <MQEngine/resource/core/DiskResourceLoader.h>

#include <algorithm>
#include <cstring>

namespace MQEngine::DouyinMiniGame
{
    namespace
    {
        class BinaryWriter
        {
        public:
            void writeU32(uint32_t v)
            {
                const uint8_t bytes[4] = {
                    static_cast<uint8_t>(v & 0xFFu),
                    static_cast<uint8_t>((v >> 8) & 0xFFu),
                    static_cast<uint8_t>((v >> 16) & 0xFFu),
                    static_cast<uint8_t>((v >> 24) & 0xFFu)
                };
                m_buf.insert(m_buf.end(), bytes, bytes + 4);
            }

            void writeString(std::string_view s)
            {
                writeU32(static_cast<uint32_t>(s.size()));
                if (!s.empty())
                {
                    const auto* ptr = reinterpret_cast<const uint8_t*>(s.data());
                    m_buf.insert(m_buf.end(), ptr, ptr + s.size());
                }
            }

            std::vector<uint8_t> take() { return std::move(m_buf); }

        private:
            std::vector<uint8_t> m_buf;
        };

        class BinaryReader
        {
        public:
            explicit BinaryReader(std::span<const uint8_t> data)
                : m_data(data) {}

            bool readU32(uint32_t& out)
            {
                if (m_offset + 4 > m_data.size())
                {
                    return false;
                }
                out = static_cast<uint32_t>(m_data[m_offset]) |
                      (static_cast<uint32_t>(m_data[m_offset + 1]) << 8) |
                      (static_cast<uint32_t>(m_data[m_offset + 2]) << 16) |
                      (static_cast<uint32_t>(m_data[m_offset + 3]) << 24);
                m_offset += 4;
                return true;
            }

            bool readString(std::string& out)
            {
                uint32_t len = 0;
                if (!readU32(len) || m_offset + len > m_data.size())
                {
                    return false;
                }
                out.assign(reinterpret_cast<const char*>(m_data.data() + m_offset), len);
                m_offset += len;
                return true;
            }

        private:
            std::span<const uint8_t> m_data;
            size_t m_offset{0};
        };

        void addUniqueTag(std::vector<std::string>& tags, std::string_view tag)
        {
            if (tag.empty())
            {
                return;
            }
            for (const auto& existing : tags)
            {
                if (existing == tag)
                {
                    return;
                }
            }
            tags.emplace_back(tag);
        }

        std::vector<ShaderVariantInput> buildBuiltinTechniqueCandidates()
        {
            std::vector<ShaderVariantInput> list;

            // 1. MobileForward 主通道顶点着色器（含 StructuredBuffer SSBO 实例矩阵，触发 2D 数据纹理重写）
            ShaderVariantInput fwdVs;
            fwdVs.shaderId = "MobileForward_Lit_VS";
            fwdVs.techniqueName = "MobileForwardLit";
            fwdVs.passTag = "ForwardOpaque";
            fwdVs.entryPoint = "VSMain";
            fwdVs.stage = FCT::ShaderKind::VertexShader;
            fwdVs.hlslSource = R"(
cbuffer PerFrameUBO : register(b0)
{
    float4x4 uViewProj;
};
struct InstanceRecord
{
    float4 worldRow0;
    float4 worldRow1;
    float4 worldRow2;
};
StructuredBuffer<InstanceRecord> g_Instances : register(t0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};
struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};
VSOutput VSMain(VSInput input)
{
    VSOutput o;
    InstanceRecord inst = g_Instances[input.instanceId];
    float3 worldPos;
    worldPos.x = dot(inst.worldRow0, float4(input.position, 1.0));
    worldPos.y = dot(inst.worldRow1, float4(input.position, 1.0));
    worldPos.z = dot(inst.worldRow2, float4(input.position, 1.0));
    o.position = mul(uViewProj, float4(worldPos, 1.0));
    o.normal = input.normal;
    o.uv = input.uv;
    return o;
}
)";
            list.push_back(std::move(fwdVs));

            // 2. MobileForward 主通道像素着色器（含分离式 Texture2D + SamplerState，触发组合采样器合成）
            ShaderVariantInput fwdPs;
            fwdPs.shaderId = "MobileForward_Lit_PS";
            fwdPs.techniqueName = "MobileForwardLit";
            fwdPs.passTag = "ForwardOpaque";
            fwdPs.entryPoint = "PSMain";
            fwdPs.stage = FCT::ShaderKind::FragmentShader;
            fwdPs.hlslSource = R"(
cbuffer MaterialUBO : register(b1)
{
    float4 uBaseColorFactor;
    float4 uLightDirAndIntensity;
};
Texture2D g_AlbedoTex : register(t1);
SamplerState g_LinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};
float4 PSMain(PSInput input) : SV_Target0
{
    float4 albedo = g_AlbedoTex.Sample(g_LinearSampler, input.uv) * uBaseColorFactor;
    float ndl = saturate(dot(normalize(input.normal), normalize(uLightDirAndIntensity.xyz)));
    float3 lit = albedo.rgb * (0.25 + 0.75 * ndl * uLightDirAndIntensity.w);
    return float4(lit, albedo.a);
}
)";
            list.push_back(std::move(fwdPs));

            // 3. ShadowCaster 阴影深度顶点着色器
            ShaderVariantInput shadowVs;
            shadowVs.shaderId = "MobileShadowCaster_VS";
            shadowVs.techniqueName = "MobileForwardLit";
            shadowVs.passTag = "ShadowCaster";
            shadowVs.entryPoint = "VSMain";
            shadowVs.stage = FCT::ShaderKind::VertexShader;
            shadowVs.hlslSource = R"(
cbuffer ShadowUBO : register(b0)
{
    float4x4 uLightViewProj;
};
struct VSInput
{
    float3 position : POSITION;
};
float4 VSMain(VSInput input) : SV_Position
{
    return mul(uLightViewProj, float4(input.position, 1.0));
}
)";
            list.push_back(std::move(shadowVs));

            // 4. UI 图层着色器
            ShaderVariantInput uiPs;
            uiPs.shaderId = "MobileHudUI_PS";
            uiPs.techniqueName = "MobileUI";
            uiPs.passTag = "UI";
            uiPs.entryPoint = "PSMain";
            uiPs.stage = FCT::ShaderKind::FragmentShader;
            uiPs.hlslSource = R"(
Texture2D g_UiAtlas : register(t0);
SamplerState g_UiSampler : register(s0);
struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};
float4 PSMain(PSInput input) : SV_Target0
{
    return g_UiAtlas.Sample(g_UiSampler, input.uv);
}
)";
            list.push_back(std::move(uiPs));

            // 5. 桌面端专用变体（用于离线关系连接时被精确静态剔除，严禁进入移动端首包）
            ShaderVariantInput desktopEditorVariant;
            desktopEditorVariant.shaderId = "Desktop_EditorGizmo_PS";
            desktopEditorVariant.techniqueName = "EditorSelection";
            desktopEditorVariant.passTag = "Editor";
            desktopEditorVariant.entryPoint = "PSMain";
            desktopEditorVariant.stage = FCT::ShaderKind::FragmentShader;
            desktopEditorVariant.hlslSource = "float4 PSMain() : SV_Target0 { return float4(1, 0.5, 0, 1); }";
            list.push_back(std::move(desktopEditorVariant));

            ShaderVariantInput deferredVariant;
            deferredVariant.shaderId = "Desktop_DeferredGBuffer_PS";
            deferredVariant.techniqueName = "DesktopDeferred";
            deferredVariant.passTag = "DeferredGBuffer";
            deferredVariant.entryPoint = "PSMain";
            deferredVariant.stage = FCT::ShaderKind::FragmentShader;
            deferredVariant.hlslSource = "float4 PSMain() : SV_Target0 { return float4(0, 0, 0, 1); }";
            list.push_back(std::move(deferredVariant));

            ShaderVariantInput rayTracingVariant;
            rayTracingVariant.shaderId = "Desktop_RayTracingShadow_PS";
            rayTracingVariant.techniqueName = "DesktopRayTracing";
            rayTracingVariant.passTag = "RayTracing";
            rayTracingVariant.entryPoint = "PSMain";
            rayTracingVariant.stage = FCT::ShaderKind::FragmentShader;
            rayTracingVariant.hlslSource = "float4 PSMain() : SV_Target0 { return float4(1, 1, 1, 1); }";
            list.push_back(std::move(rayTracingVariant));

            return list;
        }

        void appendTechniquesFromProjectFiles(
            ResourceLoader* loader,
            const std::string& projectDir,
            std::vector<ShaderVariantInput>& inoutCandidates)
        {
            const std::string techDir = SAI::Path::join(projectDir, "Assets/Techniques");
            if (!loader->exists(techDir))
            {
                return;
            }

            auto filesOr = loader->readDir(techDir);
            if (!filesOr.ok())
            {
                return;
            }

            for (const auto& filePtr : *filesOr)
            {
                if (!filePtr || filePtr->extension() != ".mqtech")
                {
                    continue;
                }
                auto docOr = ConfigDocument::loadFromFile(loader, filePtr->path(), ConfigFormat::Toml);
                if (!docOr.ok())
                {
                    continue;
                }

                const ConfigNode& root = docOr->root();
                const std::string techName = root.getString("name", filePtr->stem());
                const ConfigNode* pipelinesNode = root.findNode("pipelines");
                if (!pipelinesNode || !pipelinesNode->isArray())
                {
                    continue;
                }

                uint32_t idx = 0;
                for (const auto& item : pipelinesNode->asArray())
                {
                    if (!item.isTable())
                    {
                        continue;
                    }
                    const std::string hlsl = item.getString("hlsl_source", "");
                    if (hlsl.empty())
                    {
                        continue;
                    }
                    ShaderVariantInput v;
                    v.shaderId = item.getString("id", techName + "_Var" + std::to_string(idx++));
                    v.techniqueName = techName;
                    v.passTag = item.getString("pass_tag", "ForwardOpaque");
                    v.entryPoint = item.getString("entry_point", "main");
                    const std::string stageStr = item.getString("stage", "vertex");
                    v.stage = (stageStr == "fragment" || stageStr == "pixel")
                        ? FCT::ShaderKind::FragmentShader
                        : FCT::ShaderKind::VertexShader;
                    v.hlslSource = hlsl;
                    inoutCandidates.push_back(std::move(v));
                }
            }
        }
    } // namespace

    bool DouyinShaderCooker::isDesktopOnlyPassTag(std::string_view passTag) noexcept
    {
        return passTag == "Editor" ||
               passTag == "EditorGizmo" ||
               passTag == "DeferredGBuffer" ||
               passTag == "DeferredLighting" ||
               passTag == "RayTracing" ||
               passTag == "PathTracing";
    }

    std::vector<std::string> DouyinShaderCooker::resolveActivePassTags(
        ResourceLoader* loader,
        const std::string& projectDir,
        const DouyinPackagerConfig& config)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        std::vector<std::string> tags;
        const std::string candidatePaths[2] = {
            SAI::Path::join(projectDir, SAI::Path::join("Assets/RenderGraphs", config.renderGraphPreset)),
            SAI::Path::join(projectDir, config.renderGraphPreset)
        };

        for (const auto& graphPath : candidatePaths)
        {
            if (!graphPath.empty() && activeLoader->exists(graphPath))
            {
                auto docOr = ConfigDocument::loadFromFile(activeLoader, graphPath, ConfigFormat::Toml);
                if (docOr.ok())
                {
                    for (const auto& t : docOr->root().getStringArray("pass_tags"))
                    {
                        if (!isDesktopOnlyPassTag(t))
                        {
                            addUniqueTag(tags, t);
                        }
                    }
                    if (const ConfigNode* passesNode = docOr->root().findNode("passes"))
                    {
                        if (passesNode->isArray())
                        {
                            for (const auto& passItem : passesNode->asArray())
                            {
                                const std::string tag = passItem.getString("pass_tag", passItem.getString("tag", ""));
                                if (!tag.empty() && !isDesktopOnlyPassTag(tag))
                                {
                                    addUniqueTag(tags, tag);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (tags.empty())
        {
            if (config.renderGraphPreset.find("Unlit") != std::string::npos)
            {
                addUniqueTag(tags, "ForwardOpaque");
                addUniqueTag(tags, "UI");
            }
            else
            {
                addUniqueTag(tags, "ForwardOpaque");
                addUniqueTag(tags, "ShadowCaster");
                addUniqueTag(tags, "UI");
                if (config.enableBloom)
                {
                    addUniqueTag(tags, "PostProcess");
                }
            }
        }
        return tags;
    }

    std::vector<ShaderVariantInput> DouyinShaderCooker::pruneVariantsForPassTags(
        std::span<const std::string> activePassTags,
        std::span<const ShaderVariantInput> candidates,
        std::vector<std::string>* outPrunedIds)
    {
        std::vector<ShaderVariantInput> kept;
        kept.reserve(candidates.size());

        for (const auto& candidate : candidates)
        {
            bool matched = false;
            if (!isDesktopOnlyPassTag(candidate.passTag))
            {
                for (const auto& activeTag : activePassTags)
                {
                    if (candidate.passTag == activeTag)
                    {
                        matched = true;
                        break;
                    }
                }
            }

            if (matched)
            {
                kept.push_back(candidate);
            }
            else if (outPrunedIds)
            {
                outPrunedIds->push_back(candidate.shaderId);
            }
        }
        return kept;
    }

    StatusOr<CookedShaderEntry> DouyinShaderCooker::transpileVariant(
        FCT::ShaderCrossCompiler& crossCompiler,
        const ShaderVariantInput& variant)
    {
        FCT::ShaderCrossCompilerOptions opts;
        opts.glslVersion = 300;
        opts.es = true;
        opts.enable420Pack = false;
        opts.removeUnusedVariables = true;
        opts.emitCombinedSamplers = true;
        opts.entryPoint = variant.entryPoint.empty() ? "main" : variant.entryPoint;
        opts.stage = variant.stage;

        StatusOr<FCT::ShaderCrossCompilerResult> resultOr = InvalidArgumentError("未提供 SPIR-V 或 HLSL 输入");
        if (!variant.spirvWords.empty())
        {
            resultOr = crossCompiler.transpileSpirvToGlslEs300(variant.spirvWords, opts);
        }
        else if (!variant.hlslSource.empty())
        {
            resultOr = crossCompiler.compileHlslToGlslEs300(
                variant.hlslSource,
                opts.entryPoint,
                variant.stage,
                opts);
        }

        if (!resultOr.ok())
        {
            return resultOr.status();
        }
        if (!resultOr->success || resultOr->glslSource.find("#version 300 es") == std::string::npos)
        {
            return InternalError("SPIRV-Cross 未生成有效的 #version 300 es 着色器源码: " + variant.shaderId);
        }

        CookedShaderEntry entry;
        entry.shaderId = variant.shaderId;
        entry.techniqueName = variant.techniqueName;
        entry.passTag = variant.passTag;
        entry.stage = variant.stage;
        entry.glslEs300Source = std::move(resultOr->glslSource);
        entry.combinedSamplers = std::move(resultOr->combinedSamplers);
        entry.uniformBlocks = std::move(resultOr->uniformBlocks);
        entry.vertexAttributes = std::move(resultOr->vertexAttributes);
        entry.storageBuffers = std::move(resultOr->storageBuffers);
        return entry;
    }

    std::vector<uint8_t> DouyinShaderCooker::serializeShaderPack(
        std::span<const CookedShaderEntry> entries)
    {
        BinaryWriter writer;
        writer.writeU32(kShaderPackMagic);
        writer.writeU32(kShaderPackVersion);
        writer.writeU32(static_cast<uint32_t>(entries.size()));

        for (const auto& entry : entries)
        {
            writer.writeString(entry.shaderId);
            writer.writeString(entry.techniqueName);
            writer.writeString(entry.passTag);
            writer.writeU32(static_cast<uint32_t>(entry.stage));
            writer.writeString(entry.glslEs300Source);

            writer.writeU32(static_cast<uint32_t>(entry.combinedSamplers.size()));
            for (const auto& cs : entry.combinedSamplers)
            {
                writer.writeString(cs.combinedSamplerName);
                writer.writeString(cs.textureName);
                writer.writeString(cs.samplerName);
                writer.writeU32(cs.textureSlot);
                writer.writeU32(cs.samplerSlot);
            }

            writer.writeU32(static_cast<uint32_t>(entry.uniformBlocks.size()));
            for (const auto& ub : entry.uniformBlocks)
            {
                writer.writeString(ub.blockName);
                writer.writeU32(ub.slot);
                writer.writeU32(static_cast<uint32_t>(ub.sizeBytes));
            }

            writer.writeU32(static_cast<uint32_t>(entry.vertexAttributes.size()));
            for (const auto& va : entry.vertexAttributes)
            {
                writer.writeString(va.attributeName);
                writer.writeU32(va.location);
                writer.writeU32(va.componentCount);
            }

            writer.writeU32(static_cast<uint32_t>(entry.storageBuffers.size()));
            for (const auto& sb : entry.storageBuffers)
            {
                writer.writeString(sb.bufferName);
                writer.writeString(sb.dataTextureName);
                writer.writeU32(sb.slot);
            }
        }
        return writer.take();
    }

    StatusOr<std::vector<CookedShaderEntry>> DouyinShaderCooker::deserializeShaderPack(
        std::span<const uint8_t> binaryData)
    {
        BinaryReader reader(binaryData);
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t count = 0;
        if (!reader.readU32(magic) || magic != kShaderPackMagic)
        {
            return InvalidArgumentError("shaders_wgl2.bin 魔数校验失败（期望 MQSW）");
        }
        if (!reader.readU32(version) || version != kShaderPackVersion)
        {
            return InvalidArgumentError("shaders_wgl2.bin 版本不受支持");
        }
        if (!reader.readU32(count) || count > 65536u)
        {
            return InvalidArgumentError("shaders_wgl2.bin 条目数量越界");
        }

        std::vector<CookedShaderEntry> entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            CookedShaderEntry e;
            uint32_t stageVal = 0;
            if (!reader.readString(e.shaderId) ||
                !reader.readString(e.techniqueName) ||
                !reader.readString(e.passTag) ||
                !reader.readU32(stageVal) ||
                !reader.readString(e.glslEs300Source))
            {
                return InvalidArgumentError("shaders_wgl2.bin 着色器主字段截断");
            }
            e.stage = static_cast<FCT::ShaderKind>(stageVal);

            uint32_t samplerCount = 0;
            if (!reader.readU32(samplerCount))
            {
                return InvalidArgumentError("shaders_wgl2.bin 采样器表截断");
            }
            for (uint32_t s = 0; s < samplerCount; ++s)
            {
                FCT::CombinedSamplerBinding cs;
                if (!reader.readString(cs.combinedSamplerName) ||
                    !reader.readString(cs.textureName) ||
                    !reader.readString(cs.samplerName) ||
                    !reader.readU32(cs.textureSlot) ||
                    !reader.readU32(cs.samplerSlot))
                {
                    return InvalidArgumentError("shaders_wgl2.bin 采样器记录损坏");
                }
                e.combinedSamplers.push_back(std::move(cs));
            }

            uint32_t uboCount = 0;
            if (!reader.readU32(uboCount))
            {
                return InvalidArgumentError("shaders_wgl2.bin UBO 表截断");
            }
            for (uint32_t u = 0; u < uboCount; ++u)
            {
                FCT::UniformBlockBinding ub;
                uint32_t sz = 0;
                if (!reader.readString(ub.blockName) ||
                    !reader.readU32(ub.slot) ||
                    !reader.readU32(sz))
                {
                    return InvalidArgumentError("shaders_wgl2.bin UBO 记录损坏");
                }
                ub.sizeBytes = sz;
                e.uniformBlocks.push_back(std::move(ub));
            }

            uint32_t attrCount = 0;
            if (!reader.readU32(attrCount))
            {
                return InvalidArgumentError("shaders_wgl2.bin 顶点属性表截断");
            }
            for (uint32_t a = 0; a < attrCount; ++a)
            {
                FCT::VertexAttributeLocation va;
                if (!reader.readString(va.attributeName) ||
                    !reader.readU32(va.location) ||
                    !reader.readU32(va.componentCount))
                {
                    return InvalidArgumentError("shaders_wgl2.bin 顶点属性记录损坏");
                }
                e.vertexAttributes.push_back(std::move(va));
            }

            uint32_t ssboCount = 0;
            if (!reader.readU32(ssboCount))
            {
                return InvalidArgumentError("shaders_wgl2.bin SSBO 表截断");
            }
            for (uint32_t b = 0; b < ssboCount; ++b)
            {
                FCT::StorageBufferBinding sb;
                if (!reader.readString(sb.bufferName) ||
                    !reader.readString(sb.dataTextureName) ||
                    !reader.readU32(sb.slot))
                {
                    return InvalidArgumentError("shaders_wgl2.bin SSBO 记录损坏");
                }
                e.storageBuffers.push_back(std::move(sb));
            }

            entries.push_back(std::move(e));
        }
        return entries;
    }

    StatusOr<ShaderCookReport> DouyinShaderCooker::cookVariants(
        std::span<const std::string> activePassTags,
        std::span<const ShaderVariantInput> candidates)
    {
        if (activePassTags.empty())
        {
            return InvalidArgumentError("目标 .pgraph 激活 Pass 标签集合不能为空");
        }

        ShaderCookReport report;
        report.totalCandidateVariants = static_cast<uint32_t>(candidates.size());
        report.activePassTags.assign(activePassTags.begin(), activePassTags.end());

        const std::vector<ShaderVariantInput> kept = pruneVariantsForPassTags(
            activePassTags,
            candidates,
            &report.prunedShaderIds);
        report.prunedVariantCount = static_cast<uint32_t>(report.prunedShaderIds.size());

        FCT::ShaderCrossCompiler crossCompiler;
        report.cookedEntries.reserve(kept.size());

        for (const auto& variant : kept)
        {
            auto cookedOr = transpileVariant(crossCompiler, variant);
            if (!cookedOr.ok())
            {
                return cookedOr.status();
            }
            report.ssboRewrittenCount += static_cast<uint32_t>(cookedOr->storageBuffers.size());
            report.combinedSamplerCount += static_cast<uint32_t>(cookedOr->combinedSamplers.size());
            report.cookedEntries.push_back(std::move(*cookedOr));
        }

        report.compiledVariantCount = static_cast<uint32_t>(report.cookedEntries.size());
        report.shaderPackBinary = serializeShaderPack(report.cookedEntries);
        return report;
    }

    StatusOr<ShaderCookReport> DouyinShaderCooker::cookProjectShaders(
        ResourceLoader* loader,
        const std::string& projectDir,
        const DouyinPackagerConfig& config)
    {
        DiskResourceLoader fallbackLoader;
        ResourceLoader* activeLoader = loader ? loader : &fallbackLoader;

        const std::vector<std::string> activeTags = resolveActivePassTags(activeLoader, projectDir, config);
        std::vector<ShaderVariantInput> candidates = buildBuiltinTechniqueCandidates();
        appendTechniquesFromProjectFiles(activeLoader, projectDir, candidates);

        return cookVariants(activeTags, candidates);
    }
} // namespace MQEngine::DouyinMiniGame
