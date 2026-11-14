/**
 * @file DouyinAssetCooker.h
 * @brief Step 2：ASTC 压缩纹理、0KB 中文字体 (.mqfont)、二进制网格 (.mqmesh) 离线烘焙与三级寻址清单生成器（FEAT-03）
 */
#ifndef MQENGINE_DOUYIN_ASSET_COOKER_H
#define MQENGINE_DOUYIN_ASSET_COOKER_H

#include "thirdparty/thirdparty.h"
#include "DouyinMiniGameApi.h"
#include "DouyinPackagerConfig.h"
#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <Font/Composites/FontRuntimeComposite/BakedFont.h>
#include <Font/Composites/FontRuntimeComposite/FontTypes.h>

#include <span>

namespace MQEngine::DouyinMiniGame
{
    /**
     * @brief 单个离线烘焙产出的资产条目
     */
    struct CookedAssetItem
    {
        std::string virtualPath;
        std::string packageRelPath;
        std::string sha256Hex;
        std::string category;
        Asset::BundleType bundleType{Asset::BundleType::FirstPackage};
        std::vector<uint8_t> payload;
    };

    /**
     * @brief Step 2 资产烘焙与三级寻址清单汇总报告
     */
    struct AssetCookReport
    {
        std::vector<CookedAssetItem> items;
        std::vector<Asset::ManifestRecord> manifestRecords;
        std::vector<uint8_t> manifestBinary;
        uint64_t firstPackageAssetBytes{0};
        uint64_t remoteCdnAssetBytes{0};
        uint32_t astcTextureCount{0};
        uint32_t bakedFontCount{0};
        uint32_t bakedMeshCount{0};
    };

    /**
     * @brief 抖音小游戏离线资产烘焙与内容哈希寻址生成器
     */
    class DOUYIN_PACKAGER_CLI_API DouyinAssetCooker
    {
    public:
        static constexpr uint32_t kMqFontMagic = Resource::Font::BakedFont::Magic; // 0x4D514654
        static constexpr uint32_t kMqFontVersion = 1u;

        static std::string computeSha256Hex(std::span<const uint8_t> data);
        static std::string computeSha256Hex(std::string_view text);

        static std::string sanitizePackageRelativePath(std::string_view rawRelPath);

        static StatusOr<std::vector<uint8_t>> cookTextureToAstc(
            std::span<const uint8_t> sourceData,
            uint32_t width,
            uint32_t height,
            std::string_view astcBlockQuality);

        static StatusOr<std::vector<uint8_t>> bakeFontToMqFont(
            std::string_view fontIdentifier,
            std::u32string_view closedCharset = U"0123456789+-x!%:/.ABCDEFGHIJKLMNOPQRSTUVWXYZ");

        static StatusOr<std::vector<uint8_t>> bakeObjToMqMesh(std::string_view objContent);
        static std::vector<uint8_t> createDefaultBootCubeMqMesh();

        static std::vector<uint8_t> serializeManifest(
            std::span<const Asset::ManifestRecord> records,
            std::string_view cdnBaseUrl);

        static StatusOr<std::vector<Asset::ManifestRecord>> parseManifestBuffer(
            std::span<const uint8_t> manifestBytes);

        static void splitAssetsByFirstPackageBudget(
            std::vector<CookedAssetItem>& items,
            uint64_t maxFirstPackageAssetBytes);

        static StatusOr<AssetCookReport> cookProjectAssets(
            ResourceLoader* loader,
            const std::string& projectDir,
            const DouyinPackagerConfig& config,
            uint64_t maxFirstPackageAssetBytes);
    };
} // namespace MQEngine::DouyinMiniGame

#endif // MQENGINE_DOUYIN_ASSET_COOKER_H
