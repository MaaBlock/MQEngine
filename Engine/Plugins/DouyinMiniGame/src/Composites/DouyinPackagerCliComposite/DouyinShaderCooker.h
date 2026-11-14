/**
 * @file DouyinShaderCooker.h
 * @brief Step 1：基于目标 .pgraph 与 .mqtech 的静态着色器变体裁剪及 SPIRV-Cross WebGL 2.0 离线转译器（FEAT-03）
 */
#ifndef MQENGINE_DOUYIN_SHADER_COOKER_H
#define MQENGINE_DOUYIN_SHADER_COOKER_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinPackagerConfig.h"

#include <span>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 待裁剪与转译的着色器变体输入描述
     */
    struct ShaderVariantInput
    {
        std::string shaderId;
        std::string techniqueName;
        std::string passTag;
        std::string entryPoint{"main"};
        FCT::ShaderKind stage{FCT::ShaderKind::VertexShader};
        std::string hlslSource;
        std::vector<uint32_t> spirvWords;
    };

    /**
     * @brief 离线转译产出的单个 WebGL 2.0 (#version 300 es) 着色器条目及反射表
     */
    struct CookedShaderEntry
    {
        std::string shaderId;
        std::string techniqueName;
        std::string passTag;
        FCT::ShaderKind stage{FCT::ShaderKind::VertexShader};
        std::string glslEs300Source;
        std::vector<FCT::CombinedSamplerBinding> combinedSamplers;
        std::vector<FCT::UniformBlockBinding> uniformBlocks;
        std::vector<FCT::VertexAttributeLocation> vertexAttributes;
        std::vector<FCT::StorageBufferBinding> storageBuffers;
    };

    /**
     * @brief Step 1 着色器离线裁剪与转译统计报告
     */
    struct ShaderCookReport
    {
        uint32_t totalCandidateVariants{0};
        uint32_t prunedVariantCount{0};
        uint32_t compiledVariantCount{0};
        uint32_t ssboRewrittenCount{0};
        uint32_t combinedSamplerCount{0};
        std::vector<std::string> activePassTags;
        std::vector<std::string> prunedShaderIds;
        std::vector<CookedShaderEntry> cookedEntries;
        std::vector<uint8_t> shaderPackBinary;
    };

    /**
     * @brief 抖音小游戏离线着色器裁剪与 #version 300 es 转译器
     */
    class DOUYIN_PACKAGER_CLI_API DouyinShaderCooker
    {
    public:
        static constexpr uint32_t kShaderPackMagic = 0x5753514Du; // "MQSW"
        static constexpr uint32_t kShaderPackVersion = 1u;

        static std::vector<std::string> resolveActivePassTags(
            ResourceLoader* loader,
            const std::string& projectDir,
            const DouyinPackagerConfig& config);

        static bool isDesktopOnlyPassTag(std::string_view passTag) noexcept;

        static std::vector<ShaderVariantInput> pruneVariantsForPassTags(
            std::span<const std::string> activePassTags,
            std::span<const ShaderVariantInput> candidates,
            std::vector<std::string>* outPrunedIds = nullptr);

        static StatusOr<CookedShaderEntry> transpileVariant(
            FCT::ShaderCrossCompiler& crossCompiler,
            const ShaderVariantInput& variant);

        static std::vector<uint8_t> serializeShaderPack(
            std::span<const CookedShaderEntry> entries);

        static StatusOr<std::vector<CookedShaderEntry>> deserializeShaderPack(
            std::span<const uint8_t> binaryData);

        static StatusOr<ShaderCookReport> cookVariants(
            std::span<const std::string> activePassTags,
            std::span<const ShaderVariantInput> candidates);

        static StatusOr<ShaderCookReport> cookProjectShaders(
            ResourceLoader* loader,
            const std::string& projectDir,
            const DouyinPackagerConfig& config);
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_SHADER_COOKER_H
