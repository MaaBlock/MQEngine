/**
 * @file AssetManifest.h
 * @brief 三级资产寻址清单协议与 CDN / 本地沙箱路由 (PRD 03 §1, FEAT-16)
 */
#ifndef MQENGINE_ASSET_LOADER_ASSETMANIFEST_H
#define MQENGINE_ASSET_LOADER_ASSETMANIFEST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine::Asset
{
    enum class BundleType : uint8_t
    {
        FirstPackage = 0, // 首包只读区 (/pkg/...)
        RemoteCache  = 1  // 远端/本地缓存区 (/cache/<Hash>.bin)
    };

    struct ManifestRecord
    {
        std::string virtualPath;
        std::string sha256Hex;
        uint32_t fileSize{0};
        uint16_t assetFormat{0};
        BundleType bundleType{BundleType::RemoteCache};
    };

    struct AssetResolveResult
    {
        enum class Location
        {
            FirstPackagePkg,
            LocalCache,
            RemoteCdn
        };

        Location location{Location::RemoteCdn};
        std::string resolvedPath;
        std::string downloadUrl;
        std::string cacheDestPath;
    };

    /**
     * @brief 基于内容哈希的 AssetManifest 三级寻址清单管理器
     */
    class ENGINE_API AssetManifest
    {
    public:
        AssetManifest() = default;
        ~AssetManifest() = default;

        static AssetManifest& getInstance() noexcept;

        Status loadFromBuffer(std::span<const uint8_t> buffer);
        Status loadFromFile(std::string_view manifestPath = "/pkg/asset_manifest.bin");
        [[nodiscard]] std::vector<uint8_t> saveToBuffer() const;

        [[nodiscard]] bool hasEntry(std::string_view virtualPath) const noexcept;
        [[nodiscard]] const ManifestRecord* findRecord(std::string_view virtualPath) const noexcept;
        [[nodiscard]] size_t getRecordCount() const noexcept { return m_records.size(); }

        void setCdnBaseUrl(std::string_view url);
        [[nodiscard]] std::string_view getCdnBaseUrl() const noexcept { return m_cdnBaseUrl; }

        StatusOr<AssetResolveResult> resolveAsset(std::string_view virtualPath) const;

        void registerRecord(ManifestRecord record);
        void insertRecord(ManifestRecord record) { registerRecord(std::move(record)); }
        void clear() noexcept;

        /**
         * @brief 计算字节流的 FIPS 180-4 标准 SHA-256 十六进制小写摘要
         */
        [[nodiscard]] static std::string computeSha256Hex(std::span<const uint8_t> data);

        /**
         * @brief 校验字节流是否与预期 SHA-256 摘要一致
         */
        [[nodiscard]] static bool verifySha256(std::span<const uint8_t> data, std::string_view expectedHex);

    private:
        std::string m_cdnBaseUrl{"https://cdn.minigame.douyin.com/assets"};
        std::unordered_map<std::string, ManifestRecord> m_records;
    };
}

#endif // MQENGINE_ASSET_LOADER_ASSETMANIFEST_H
