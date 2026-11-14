/**
 * @file RenderGraphAsset.cpp
 * @brief 纯数据 .pgraph 资产解析器与四大预设工厂实现
 */
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/thirdparty/TomlBackend.h>
#include <cctype>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace MQEngine::Domain::Renderer
{
    namespace
    {
        bool isBlankText(std::string_view text) noexcept
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

        bool isWindowTargetName(std::string_view name) noexcept
        {
            return name == "Window" || name == "$Window" || name == "Backbuffer" || name == "$Backbuffer";
        }

        bool isDepthFormatName(std::string_view format) noexcept
        {
            return format.starts_with("D16") || format.starts_with("D24") || format.starts_with("D32") ||
                   format.find("DEPTH") != std::string_view::npos;
        }

        FCT::Format mapStringToFctFormat(std::string_view fmtStr, bool isDepth) noexcept
        {
            if (fmtStr == "RGBA8_UNORM" || fmtStr == "R8G8B8A8_UNORM") return FCT::Format::R8G8B8A8_UNORM;
            if (fmtStr == "RGBA16_SFLOAT" || fmtStr == "R16G16B16A16_SFLOAT" || fmtStr == "RGBA16F") return FCT::Format::R16G16B16A16_SFLOAT;
            if (fmtStr == "R16_SFLOAT" || fmtStr == "R16F") return FCT::Format::R16_SFLOAT;
            if (fmtStr == "RG16_SFLOAT" || fmtStr == "R16G16_SFLOAT") return FCT::Format::R16G16_SFLOAT;
            if (fmtStr == "D16_UNORM") return FCT::Format::D16_UNORM;
            if (fmtStr == "D24_UNORM_S8_UINT" || fmtStr == "D24S8") return FCT::Format::D24_UNORM_S8_UINT;
            if (fmtStr == "D32_SFLOAT" || fmtStr == "D32F") return FCT::Format::D32_SFLOAT;
            return isDepth ? FCT::Format::D32_SFLOAT : FCT::Format::R16G16B16A16_SFLOAT;
        }

        void parseStringArray(const toml::array* arr, std::vector<std::string>& outList)
        {
            if (!arr)
            {
                return;
            }
            for (const auto& elem : *arr)
            {
                if (auto val = elem.value<std::string>())
                {
                    outList.push_back(*val);
                }
            }
        }

        Status parseSinglePassTable(const toml::table& passTbl, std::string_view fallbackName, RenderGraphPassNodeDesc& outPass)
        {
            // 强制红线：纯数据 Pass 节点严禁携带任何 Shader 字段 (FEAT-68)
            if (passTbl.contains("shader") ||
                passTbl.contains("vertex_shader") ||
                passTbl.contains("pixel_shader") ||
                passTbl.contains("fragment_shader") ||
                passTbl.contains("compute_shader"))
            {
                return InvalidArgumentError("纯数据 .pgraph Pass 节点严禁包含 Shader 字段");
            }

            outPass.name = passTbl["name"].value_or(std::string(fallbackName));
            if (outPass.name.empty())
            {
                return InvalidArgumentError("RenderGraphAsset Pass 缺少名称");
            }

            parseStringArray(passTbl["tags"].as_array(), outPass.tags);
            parseStringArray(passTbl["pass_tags"].as_array(), outPass.tags);
            outPass.tagMask = TagRegistry::StringsToPassMask(outPass.tags);

            if (auto* clearColorArr = passTbl["clear_color"].as_array())
            {
                outPass.hasClearColor = true;
                float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                size_t idx = 0;
                for (const auto& cv : *clearColorArr)
                {
                    if (idx < 4)
                    {
                        c[idx++] = static_cast<float>(cv.value_or(0.0));
                    }
                }
                outPass.clearColor = FCT::Vec4{c[0], c[1], c[2], c[3]};
            }

            if (auto cd = passTbl["clear_depth"].value<double>())
            {
                outPass.hasClearDepth = true;
                outPass.clearDepth = static_cast<float>(*cd);
            }

            outPass.discardDepthAfterPass = passTbl["discard_depth"].value_or(false);
            if (auto storeOp = passTbl["depth_store_op"].value<std::string>())
            {
                if (*storeOp == "DontCare" || *storeOp == "Discard")
                {
                    outPass.discardDepthAfterPass = true;
                }
            }

            if (auto* targetsArr = passTbl["targets"].as_array())
            {
                for (const auto& tgt : *targetsArr)
                {
                    if (auto tgtStr = tgt.value<std::string>())
                    {
                        PassTargetBinding tb;
                        tb.isWindow = isWindowTargetName(*tgtStr);
                        tb.imageName = *tgtStr;
                        outPass.targets.push_back(std::move(tb));
                    }
                    else if (const auto* tgtTbl = tgt.as_table())
                    {
                        PassTargetBinding tb;
                        tb.imageName = (*tgtTbl)["image"].value_or((*tgtTbl)["name"].value_or(std::string{}));
                        if (auto explicitWin = (*tgtTbl)["is_window"].value<bool>())
                        {
                            tb.isWindow = *explicitWin;
                        }
                        else
                        {
                            tb.isWindow = isWindowTargetName(tb.imageName);
                        }
                        if (!tb.imageName.empty() || tb.isWindow)
                        {
                            if (tb.imageName.empty() && tb.isWindow)
                            {
                                tb.imageName = "Window";
                            }
                            outPass.targets.push_back(std::move(tb));
                        }
                    }
                }
            }

            outPass.depthStencilImage = passTbl["depth_stencil"].value_or(
                passTbl["depth_target"].value_or(std::string{}));

            if (auto* texArr = passTbl["textures"].as_array())
            {
                for (const auto& texElem : *texArr)
                {
                    if (auto* texTbl = texElem.as_table())
                    {
                        PassTextureInput inp;
                        inp.slot = (*texTbl)["slot"].value_or(std::string{});
                        inp.source = (*texTbl)["source"].value_or(std::string{});
                        inp.fallback = (*texTbl)["fallback"].value_or(std::string{});
                        outPass.textures.push_back(std::move(inp));
                    }
                }
            }

            parseStringArray(passTbl["read_groups"].as_array(), outPass.readGroups);
            parseStringArray(passTbl["write_groups"].as_array(), outPass.writeGroups);
            return OkStatus();
        }
    } // namespace

    const RenderGraphPassNodeDesc* RenderGraphAssetDesc::findPass(std::string_view passName) const noexcept
    {
        for (const auto& p : passes)
        {
            if (p.name == passName)
            {
                return &p;
            }
        }
        return nullptr;
    }

    const RenderGraphImageDesc* RenderGraphAssetDesc::findImage(std::string_view imageName) const noexcept
    {
        for (const auto& img : images)
        {
            if (img.name == imageName)
            {
                return &img;
            }
        }
        return nullptr;
    }

    bool RenderGraphAssetDesc::isDirectToBackbuffer() const noexcept
    {
        if (passes.empty())
        {
            return false;
        }
        for (const auto& p : passes)
        {
            for (const auto& t : p.targets)
            {
                if (!t.isWindow)
                {
                    return false;
                }
            }
        }
        return true;
    }

    size_t RenderGraphAssetDesc::countOffscreenColorImages() const noexcept
    {
        std::unordered_set<std::string> offscreenNames;
        for (const auto& img : images)
        {
            if (!isDepthFormatName(img.format) && !isWindowTargetName(img.name))
            {
                offscreenNames.insert(img.name);
            }
        }
        for (const auto& p : passes)
        {
            for (const auto& t : p.targets)
            {
                if (!t.isWindow && !t.imageName.empty())
                {
                    offscreenNames.insert(t.imageName);
                }
            }
        }
        return offscreenNames.size();
    }

    StatusOr<std::string> RenderGraphAssetDesc::resolvePinSlot(
        std::string_view passName,
        std::string_view slotName) const
    {
        const RenderGraphPassNodeDesc* pass = findPass(passName);
        if (!pass)
        {
            return NotFoundError(StrCat("未找到指定 Pass: ", passName));
        }

        for (const auto& tex : pass->textures)
        {
            if (tex.slot == slotName)
            {
                if (!tex.source.empty())
                {
                    return tex.source;
                }
                if (!tex.fallback.empty())
                {
                    return tex.fallback;
                }
                return NotFoundError(StrCat("Pin Slot 未绑定有效纹理且无 fallback: ", slotName));
            }
        }

        return NotFoundError(StrCat("Pass [", passName, "] 未声明 Pin Slot: ", slotName));
    }

    Status RenderGraphAssetDesc::validateDepthSamplingHazard() const
    {
        std::unordered_set<std::string> discardedDepthImages;
        for (const auto& p : passes)
        {
            for (const auto& tex : p.textures)
            {
                if (!tex.source.empty() && discardedDepthImages.find(tex.source) != discardedDepthImages.end())
                {
                    return FailedPreconditionError(
                        StrCat("深度附件 [", tex.source, "] 已在上游执行丢弃 (glInvalidateFramebuffer)，禁止在下游 Pass [", p.name, "] 中采样"));
                }
            }
            if (p.discardDepthAfterPass && !p.depthStencilImage.empty())
            {
                discardedDepthImages.insert(p.depthStencilImage);
            }
        }
        return OkStatus();
    }

    uint64_t RenderGraphAssetDesc::collectUnionPassTagMask() const noexcept
    {
        uint64_t unionMask = 0;
        for (const auto& p : passes)
        {
            unionMask |= p.tagMask;
        }
        return unionMask;
    }

    bool RenderGraphAssetDesc::isAcyclic() const
    {
        const size_t n = passes.size();
        if (n <= 1)
        {
            return true;
        }

        std::unordered_map<std::string, size_t> resourceWriter;
        for (size_t i = 0; i < n; ++i)
        {
            for (const auto& t : passes[i].targets)
            {
                if (!t.isWindow && !t.imageName.empty())
                {
                    resourceWriter[t.imageName] = i;
                }
            }
            if (!passes[i].depthStencilImage.empty())
            {
                resourceWriter[passes[i].depthStencilImage] = i;
            }
            for (const auto& g : passes[i].writeGroups)
            {
                resourceWriter[g] = i;
            }
        }

        std::vector<std::unordered_set<size_t>> adj(n);
        std::vector<uint32_t> inDegree(n, 0);

        for (size_t i = 0; i < n; ++i)
        {
            for (const auto& tex : passes[i].textures)
            {
                auto it = resourceWriter.find(tex.source);
                if (it != resourceWriter.end() && it->second != i)
                {
                    const size_t u = it->second;
                    if (adj[u].insert(i).second)
                    {
                        inDegree[i]++;
                    }
                }
            }
            for (const auto& rg : passes[i].readGroups)
            {
                auto it = resourceWriter.find(rg);
                if (it != resourceWriter.end() && it->second != i)
                {
                    const size_t u = it->second;
                    if (adj[u].insert(i).second)
                    {
                        inDegree[i]++;
                    }
                }
            }
        }

        std::queue<size_t> q;
        for (size_t i = 0; i < n; ++i)
        {
            if (inDegree[i] == 0)
            {
                q.push(i);
            }
        }

        size_t visited = 0;
        while (!q.empty())
        {
            const size_t u = q.front();
            q.pop();
            visited++;

            for (size_t v : adj[u])
            {
                if (--inDegree[v] == 0)
                {
                    q.push(v);
                }
            }
        }

        return visited == n;
    }

    StatusOr<RenderGraphAssetDesc> RenderGraphAsset::LoadFromToml(std::string_view tomlContent)
    {
        if (isBlankText(tomlContent))
        {
            return InvalidArgumentError("RenderGraphAsset TOML 内容不能为空");
        }

        toml::parse_result result = toml::parse(tomlContent);
        if (result.failed())
        {
            return InvalidArgumentError(StrCat("TOML 解析失败: ", result.error().description()));
        }

        const auto& tbl = result.table();
        RenderGraphAssetDesc asset;
        asset.name = tbl["name"].value_or(std::string{"UnnamedGraph"});
        asset.version = tbl["version"].value_or(std::string{"3.0"});

        if (const auto* graphTbl = tbl["graph"].as_table())
        {
            if (auto gName = (*graphTbl)["name"].value<std::string>())
            {
                asset.name = *gName;
            }
            if (auto gVer = (*graphTbl)["version"].value<std::string>())
            {
                asset.version = *gVer;
            }
        }

        if (auto* groups = tbl["resource_groups"].as_array())
        {
            for (const auto& g : *groups)
            {
                if (auto val = g.value<std::string>())
                {
                    asset.resourceGroups.push_back(*val);
                }
                else if (const auto* gTbl = g.as_table())
                {
                    if (auto tblName = (*gTbl)["name"].value<std::string>())
                    {
                        asset.resourceGroups.push_back(*tblName);
                    }
                }
            }
        }

        std::unordered_set<std::string> declaredImages;
        if (auto* images = tbl["images"].as_array())
        {
            for (const auto& elem : *images)
            {
                if (auto* imgTbl = elem.as_table())
                {
                    RenderGraphImageDesc img;
                    img.name = (*imgTbl)["name"].value_or(std::string{});
                    img.format = (*imgTbl)["format"].value_or(std::string{"RGBA8_UNORM"});
                    img.scaleX = static_cast<float>((*imgTbl)["scale_x"].value_or(1.0));
                    img.scaleY = static_cast<float>((*imgTbl)["scale_y"].value_or(1.0));
                    if (const auto* scaleArr = (*imgTbl)["scale"].as_array())
                    {
                        if (scaleArr->size() >= 1) img.scaleX = static_cast<float>((*scaleArr)[0].value_or(1.0));
                        if (scaleArr->size() >= 2) img.scaleY = static_cast<float>((*scaleArr)[1].value_or(1.0));
                    }
                    img.width = static_cast<uint32_t>((*imgTbl)["width"].value_or(0));
                    img.height = static_cast<uint32_t>((*imgTbl)["height"].value_or(0));
                    if (!img.name.empty())
                    {
                        declaredImages.insert(img.name);
                        asset.images.push_back(std::move(img));
                    }
                }
            }
        }

        if (auto* passes = tbl["passes"].as_array())
        {
            for (const auto& elem : *passes)
            {
                if (auto* passTbl = elem.as_table())
                {
                    RenderGraphPassNodeDesc p;
                    CHECK_STATUS(parseSinglePassTable(*passTbl, "", p));
                    asset.passes.push_back(std::move(p));
                }
            }
        }

        if (const auto* passDict = tbl["pass"].as_table())
        {
            for (const auto& [key, node] : *passDict)
            {
                if (const auto* passTbl = node.as_table())
                {
                    RenderGraphPassNodeDesc p;
                    CHECK_STATUS(parseSinglePassTable(*passTbl, key.str(), p));
                    asset.passes.push_back(std::move(p));
                }
            }
        }

        if (asset.passes.empty())
        {
            return InvalidArgumentError("RenderGraphAsset 至少需要包含一个有效的 Pass 节点");
        }

        // 校验资源引用合法性：当显式声明了 [[images]] 时，所有非窗口附件与深度目标必须已在 images 中注册
        if (!declaredImages.empty())
        {
            for (const auto& p : asset.passes)
            {
                for (const auto& tgt : p.targets)
                {
                    if (!tgt.isWindow && declaredImages.find(tgt.imageName) == declaredImages.end())
                    {
                        return InvalidArgumentError(StrCat("Pass [", p.name, "] 引用了未定义的颜色附件资源: ", tgt.imageName));
                    }
                }
                if (!p.depthStencilImage.empty() && declaredImages.find(p.depthStencilImage) == declaredImages.end())
                {
                    return InvalidArgumentError(StrCat("Pass [", p.name, "] 引用了未定义的深度附件资源: ", p.depthStencilImage));
                }
            }
        }

        if (!asset.isAcyclic())
        {
            return InvalidArgumentError("RenderGraphAsset 存在有向循环依赖环");
        }

        CHECK_STATUS(asset.validateDepthSamplingHazard());
        return asset;
    }

    StatusOr<RenderGraphAssetDesc> RenderGraphAsset::LoadFromFile(std::string_view assetPath)
    {
        if (assetPath.empty())
        {
            return InvalidArgumentError("RenderGraphAsset 文件路径不能为空");
        }

        const std::string pathStr(assetPath);
        if (auto* res = Resource::Resource::InstancePtr())
        {
            if (auto* loader = res->getResourceLoader())
            {
                auto contentOr = loader->loadFile(pathStr);
                if (contentOr.ok())
                {
                    return LoadFromToml(*contentOr);
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
            return LoadFromToml(text);
        }

        return NotFoundError(StrCat("未找到 .pgraph 渲染图资产: ", pathStr));
    }

    RenderGraphAssetDesc RenderGraphAsset::CreateDesktopDeferredHdrPreset()
    {
        RenderGraphAssetDesc desc;
        desc.name = "Desktop_DeferredHDR";
        desc.version = "3.0";

        desc.images.push_back({"GBuffer0", "RGBA8_UNORM", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"GBuffer1", "RGBA8_UNORM", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"GBuffer2", "RGBA16_SFLOAT", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"SceneDepth", "D32_SFLOAT", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"SceneHdrColor", "RGBA16_SFLOAT", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"ShadowMap", "D32_SFLOAT", 0.0f, 0.0f, 2048, 2048});

        RenderGraphPassNodeDesc shadowPass;
        shadowPass.name = "ShadowMapPass";
        shadowPass.tags = {"Shadow"};
        shadowPass.tagMask = TagRegistry::StringsToPassMask(shadowPass.tags);
        shadowPass.hasClearDepth = true;
        shadowPass.clearDepth = 1.0f;
        shadowPass.depthStencilImage = "ShadowMap";
        desc.passes.push_back(shadowPass);

        RenderGraphPassNodeDesc gbufferPass;
        gbufferPass.name = "DeferredGBufferPass";
        gbufferPass.tags = {"Geometry", "DeferredGBuffer", "Opaque"};
        gbufferPass.tagMask = TagRegistry::StringsToPassMask(gbufferPass.tags);
        gbufferPass.hasClearColor = true;
        gbufferPass.clearColor = FCT::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        gbufferPass.hasClearDepth = true;
        gbufferPass.clearDepth = 1.0f;
        gbufferPass.targets = {{"GBuffer0", false}, {"GBuffer1", false}, {"GBuffer2", false}};
        gbufferPass.depthStencilImage = "SceneDepth";
        desc.passes.push_back(gbufferPass);

        RenderGraphPassNodeDesc lightingPass;
        lightingPass.name = "DeferredLightingPass";
        lightingPass.tags = {"Lighting"};
        lightingPass.tagMask = TagRegistry::StringsToPassMask(lightingPass.tags);
        lightingPass.targets = {{"SceneHdrColor", false}};
        lightingPass.textures = {
            {"GBuffer0", "GBuffer0", ""},
            {"GBuffer1", "GBuffer1", ""},
            {"GBuffer2", "GBuffer2", ""},
            {"Depth", "SceneDepth", ""},
            {"Shadow", "ShadowMap", ""}
        };
        desc.passes.push_back(lightingPass);

        RenderGraphPassNodeDesc skyboxPass;
        skyboxPass.name = "SkyboxPass";
        skyboxPass.tags = {"Skybox"};
        skyboxPass.tagMask = TagRegistry::StringsToPassMask(skyboxPass.tags);
        skyboxPass.targets = {{"SceneHdrColor", false}};
        skyboxPass.depthStencilImage = "SceneDepth";
        desc.passes.push_back(skyboxPass);

        RenderGraphPassNodeDesc transPass;
        transPass.name = "ForwardTransPass";
        transPass.tags = {"Geometry", "Translucent"};
        transPass.tagMask = TagRegistry::StringsToPassMask(transPass.tags);
        transPass.targets = {{"SceneHdrColor", false}};
        transPass.depthStencilImage = "SceneDepth";
        desc.passes.push_back(transPass);

        RenderGraphPassNodeDesc postPass;
        postPass.name = "PostProcessPass";
        postPass.tags = {"PostProcess", "ToneMap", "Bloom"};
        postPass.tagMask = TagRegistry::StringsToPassMask(postPass.tags);
        postPass.targets = {{"Window", true}};
        postPass.textures = {{"SceneColor", "SceneHdrColor", "@builtin/black_1x1"}};
        desc.passes.push_back(postPass);

        RenderGraphPassNodeDesc uiPass;
        uiPass.name = "UiOverlayPass";
        uiPass.tags = {"Overlay", "UI"};
        uiPass.tagMask = TagRegistry::StringsToPassMask(uiPass.tags);
        uiPass.targets = {{"Window", true}};
        desc.passes.push_back(uiPass);

        return desc;
    }

    RenderGraphAssetDesc RenderGraphAsset::CreateDesktopHybridRayTracingPreset()
    {
        RenderGraphAssetDesc desc = CreateDesktopDeferredHdrPreset();
        desc.name = "Desktop_HybridRayTracing";
        desc.images.push_back({"RTShadow", "R16_SFLOAT", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"RTReflection", "RGBA16_SFLOAT", 1.0f, 1.0f, 0, 0});

        RenderGraphPassNodeDesc rtPass;
        rtPass.name = "RayTracingPass";
        rtPass.tags = {"RayTracing"};
        rtPass.tagMask = TagRegistry::StringsToPassMask(rtPass.tags);
        rtPass.targets = {{"RTShadow", false}, {"RTReflection", false}};
        rtPass.textures = {{"SceneDepth", "SceneDepth", ""}, {"GBuffer0", "GBuffer0", ""}};

        desc.passes.insert(desc.passes.begin() + 2, rtPass);
        return desc;
    }

    RenderGraphAssetDesc RenderGraphAsset::CreateMiniGameForwardPlusPreset()
    {
        RenderGraphAssetDesc desc;
        desc.name = "MiniGame_ForwardPlus";
        desc.version = "3.0";

        desc.images.push_back({"ShadowMapAtlas", "D24_UNORM_S8_UINT", 0.0f, 0.0f, 2048, 2048});
        desc.images.push_back({"SceneHdrColor", "RGBA16_SFLOAT", 1.0f, 1.0f, 0, 0});
        desc.images.push_back({"SceneDepth", "D24_UNORM_S8_UINT", 1.0f, 1.0f, 0, 0});

        RenderGraphPassNodeDesc shadowPass;
        shadowPass.name = "ShadowMapPass";
        shadowPass.tags = {"Shadow"};
        shadowPass.tagMask = TagRegistry::StringsToPassMask(shadowPass.tags);
        shadowPass.hasClearDepth = true;
        shadowPass.clearDepth = 1.0f;
        shadowPass.depthStencilImage = "ShadowMapAtlas";
        desc.passes.push_back(shadowPass);

        RenderGraphPassNodeDesc opaquePass;
        opaquePass.name = "ForwardOpaquePass";
        opaquePass.tags = {"Geometry", "ForwardPlus", "Opaque"};
        opaquePass.tagMask = TagRegistry::StringsToPassMask(opaquePass.tags);
        opaquePass.hasClearColor = true;
        opaquePass.clearColor = FCT::Vec4{0.1f, 0.12f, 0.15f, 1.0f};
        opaquePass.hasClearDepth = true;
        opaquePass.clearDepth = 1.0f;
        opaquePass.targets = {{"SceneHdrColor", false}};
        opaquePass.depthStencilImage = "SceneDepth";
        opaquePass.textures = {{"ShadowAtlas", "ShadowMapAtlas", ""}};
        desc.passes.push_back(opaquePass);

        RenderGraphPassNodeDesc skyboxPass;
        skyboxPass.name = "SkyboxPass";
        skyboxPass.tags = {"Skybox"};
        skyboxPass.tagMask = TagRegistry::StringsToPassMask(skyboxPass.tags);
        skyboxPass.targets = {{"SceneHdrColor", false}};
        skyboxPass.depthStencilImage = "SceneDepth";
        desc.passes.push_back(skyboxPass);

        RenderGraphPassNodeDesc transPass;
        transPass.name = "ForwardTransPass";
        transPass.tags = {"Geometry", "Translucent"};
        transPass.tagMask = TagRegistry::StringsToPassMask(transPass.tags);
        transPass.targets = {{"SceneHdrColor", false}};
        transPass.depthStencilImage = "SceneDepth";
        transPass.discardDepthAfterPass = true;
        desc.passes.push_back(transPass);

        RenderGraphPassNodeDesc postPass;
        postPass.name = "PostProcessPass";
        postPass.tags = {"PostProcess", "ToneMap"};
        postPass.tagMask = TagRegistry::StringsToPassMask(postPass.tags);
        postPass.targets = {{"Window", true}};
        postPass.textures = {{"SceneColor", "SceneHdrColor", "@builtin/black_1x1"}};
        desc.passes.push_back(postPass);

        RenderGraphPassNodeDesc uiPass;
        uiPass.name = "UiOverlayPass";
        uiPass.tags = {"Overlay", "UI"};
        uiPass.tagMask = TagRegistry::StringsToPassMask(uiPass.tags);
        uiPass.targets = {{"Window", true}};
        desc.passes.push_back(uiPass);

        return desc;
    }

    RenderGraphAssetDesc RenderGraphAsset::CreateMiniGameForwardLitePreset()
    {
        RenderGraphAssetDesc desc;
        desc.name = "MiniGame_ForwardLite";
        desc.version = "3.0";

        desc.images.push_back({"SceneDepth", "D16_UNORM", 1.0f, 1.0f, 0, 0});

        RenderGraphPassNodeDesc litePass;
        litePass.name = "ForwardLitePass";
        litePass.tags = {"Geometry", "ForwardLite", "Opaque", "UI"};
        litePass.tagMask = TagRegistry::StringsToPassMask(litePass.tags);
        litePass.hasClearColor = true;
        litePass.clearColor = FCT::Vec4{0.1f, 0.12f, 0.15f, 1.0f};
        litePass.hasClearDepth = true;
        litePass.clearDepth = 1.0f;
        litePass.discardDepthAfterPass = true;
        litePass.targets = {{"Window", true}};
        litePass.depthStencilImage = "SceneDepth";
        desc.passes.push_back(litePass);

        return desc;
    }

    Status RenderGraphAsset::ApplyToFctRenderGraph(
        const RenderGraphAssetDesc& desc,
        FCT::RenderGraph* rg,
        FCT::Window* window,
        FCT::Image* externalColorTarget,
        FCT::Image* externalDepthTarget)
    {
        if (!rg)
        {
            return InvalidArgumentError("RenderGraph 实例为空");
        }

        CHECK_STATUS(desc.validateDepthSamplingHazard());

        for (const auto& passNode : desc.passes)
        {
            FCT::PassDesc pd(passNode.name);
            uint32_t clearType = 0;
            if (passNode.hasClearColor) clearType |= static_cast<uint32_t>(FCT::ClearType::color);
            if (passNode.hasClearDepth) clearType |= static_cast<uint32_t>(FCT::ClearType::depth);

            if (clearType != 0)
            {
                pd.clear = FCT::EnablePassClear(
                    FCT::ClearTypes(clearType),
                    FCT::Vec4(passNode.clearColor),
                    passNode.hasClearDepth ? passNode.clearDepth : 1.0f,
                    static_cast<uint8_t>(0));
            }

            for (const auto& t : passNode.targets)
            {
                if (t.isWindow)
                {
                    if (externalColorTarget)
                    {
                        pd.targets.push_back(FCT::Target("SceneColorTarget", externalColorTarget));
                    }
                    else if (window)
                    {
                        pd.targets.push_back(FCT::Target("SceneColorTarget", window));
                    }
                    else
                    {
                        pd.targets.push_back(FCT::Target(t.imageName, FCT::Format::R8G8B8A8_UNORM));
                    }
                }
                else
                {
                    FCT::Format fmt = FCT::Format::R16G16B16A16_SFLOAT;
                    if (const auto* imgDesc = desc.findImage(t.imageName))
                    {
                        fmt = mapStringToFctFormat(imgDesc->format, false);
                    }
                    pd.targets.push_back(FCT::Target(t.imageName, fmt));
                }
            }

            if (!passNode.depthStencilImage.empty())
            {
                if (externalDepthTarget && passNode.depthStencilImage == "SceneDepth")
                {
                    pd.depthStencils.push_back(FCT::DepthStencil("SceneDepthTarget", externalDepthTarget));
                }
                else
                {
                    FCT::Format depthFmt = FCT::Format::D32_SFLOAT;
                    if (const auto* imgDesc = desc.findImage(passNode.depthStencilImage))
                    {
                        depthFmt = mapStringToFctFormat(imgDesc->format, true);
                    }
                    pd.depthStencils.push_back(FCT::DepthStencil(passNode.depthStencilImage, depthFmt));
                }
            }

            for (const auto& texInput : passNode.textures)
            {
                if (!texInput.source.empty())
                {
                    pd.textures.push_back(FCT::Texture(texInput.source, FCT::ShaderStage::Fragment));
                }
            }

            rg->addPassDesc(pd);
        }

        return OkStatus();
    }
}
