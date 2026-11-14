/**
 * @file AsyncLoader.h
 * @brief 基于 WorkHost 异步 I/O 线程组与 CPU 契约池的通用资产流式加载器 (PRD 03, FEAT-16, FEAT-18, FEAT-20)
 */
#ifndef MQENGINE_ASSET_LOADER_ASYNCLOADER_H
#define MQENGINE_ASSET_LOADER_ASYNCLOADER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/asset/classdef.h>
#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Asset
    {
#pragma pack(push, 1)
        struct MqMeshHeader
        {
            uint32_t magic{0x4D514D53};     // "MQMS"
            uint32_t version{1};
            uint32_t vertexCount{0};
            uint32_t indexCount{0};
            uint32_t subMeshCount{1};
            uint32_t vertexStride{48};
            float aabbMin[3]{0.0f, 0.0f, 0.0f};
            float aabbMax[3]{0.0f, 0.0f, 0.0f};
        };

        struct StandardMeshVertex
        {
            float position[3];
            float normal[3];
            float tangent[4];
            float texCoord[2];
        };

        struct SubMeshRange
        {
            uint32_t indexOffset{0};
            uint32_t indexCount{0};
            uint32_t baseVertex{0};
            uint32_t materialIndex{0};
        };
#pragma pack(pop)

        struct ParsedMqMesh
        {
            MqMeshHeader header{};
            std::vector<uint8_t> vertexBytes;
            std::vector<uint8_t> indexBytes;
            std::vector<SubMeshRange> subMeshes;
            bool uses32BitIndices{true};
        };

        struct CacheLedgerEntry
        {
            std::string sha256Hex;
            uint64_t sizeBytes{0};
            uint64_t lastAccessTime{0};
            bool pinned{false};
        };

        /**
         * @brief 异步资产流式加载器：
         *        提供免 Assimp .mqmesh 二进制直通解析、并发 URL 下载合并去重、
         *        以及 180MB/200MB 磁盘沙箱配额 LRU 淘汰与内存告警收缩。
         */
        class ENGINE_API AsyncLoader : public CoreEngineManager<AsyncLoader>
        {
        public:
            using TextReadCallback = std::function<void(StatusOr<std::string>)>;
            using BinaryReadCallback = std::function<void(StatusOr<std::vector<uint8_t>>)>;
            using FileWriteCallback = std::function<void(Status)>;
            using ChunkCallback = std::function<bool(StatusOr<StreamChunk>)>;
            using StreamFinishedCallback = std::function<void(Status)>;

            static constexpr uint64_t DefaultCacheHardLimitBytes = 200ULL * 1024ULL * 1024ULL;
            static constexpr uint64_t DefaultCacheHighWatermarkBytes = 180ULL * 1024ULL * 1024ULL;
            static constexpr uint64_t DefaultCacheLowWatermarkBytes = 140ULL * 1024ULL * 1024ULL;
            static constexpr uint64_t EmergencyCacheWatermarkBytes = 100ULL * 1024ULL * 1024ULL;

            AsyncLoader();
            ~AsyncLoader();

            AsyncLoader(const AsyncLoader&) = delete;
            AsyncLoader& operator=(const AsyncLoader&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            // ==================== 1. 本地磁盘异步 I/O ====================
            Status submitAsyncTextRead(const std::string& path, TextReadCallback onComplete);
            Status submitAsyncBinaryRead(const std::string& path, BinaryReadCallback onComplete);
            Status submitAsyncFileWrite(const std::string& path, std::string content, FileWriteCallback onComplete);

            // ==================== 2. 网络流式下载与并发去重 ====================
            Status submitAsyncNetworkFetch(const std::string& url, BinaryReadCallback onComplete);
            uint32_t getInflightDownloadCount() const;

            // ==================== 3. 超大资产分块流式读取 ====================
            Status submitAsyncChunkedStream(
                const std::string& path,
                uint64_t chunkSize,
                ChunkCallback onChunk,
                StreamFinishedCallback onFinished);

            // ==================== 4. 二进制网格直通 (.mqmesh, FEAT-18) ====================
            static Status validateSubmeshRanges(
                const MqMeshHeader& header,
                std::span<const SubMeshRange> subMeshes);

            static StatusOr<std::vector<uint8_t>> serializeMqMesh(
                const MqMeshHeader& header,
                std::span<const uint8_t> vertexBytes,
                std::span<const uint8_t> indexBytes,
                std::span<const SubMeshRange> subMeshes = {});

            static StatusOr<ParsedMqMesh> parseMqMeshBuffer(std::span<const uint8_t> buffer);

            Status submitAsyncMqMeshRead(
                const std::string& path,
                std::function<void(StatusOr<MqMeshHeader>, std::vector<uint8_t>, std::vector<uint8_t>)> onComplete);

            // ==================== 5. 沙箱磁盘配额与 LRU 淘汰 (FEAT-20) ====================
            void recordCacheAccess(std::string_view sha256Hex, uint64_t sizeBytes);
            void updateCacheAccessTime(std::string_view sha256Hex);
            void pinCacheEntry(std::string_view sha256Hex);
            void unpinCacheEntry(std::string_view sha256Hex);
            [[nodiscard]] bool hasCacheEntry(std::string_view sha256Hex) const;
            void clearCacheLedger();

            void enforceCacheQuota(
                uint64_t highWatermarkBytes = DefaultCacheHighWatermarkBytes,
                uint64_t lowWatermarkBytes = DefaultCacheLowWatermarkBytes);

            void onMemoryWarning(int level = 2);

            uint64_t getTotalCacheUsageBytes() const noexcept { return m_totalCacheUsageBytes.load(std::memory_order_relaxed); }

            // ==================== 6. 统计与监控 ====================
            uint64_t getTotalBytesRead() const noexcept { return m_totalBytesRead.load(std::memory_order_relaxed); }
            uint64_t getTotalBytesWritten() const noexcept { return m_totalBytesWritten.load(std::memory_order_relaxed); }
            uint32_t getActiveIoCount() const noexcept { return m_activeIoCount.load(std::memory_order_relaxed); }

        private:
            StatusOr<std::vector<uint8_t>> readLocalBinaryViaEngine(const std::string& path);

            std::atomic<uint64_t> m_totalBytesRead{0};
            std::atomic<uint64_t> m_totalBytesWritten{0};
            std::atomic<uint32_t> m_activeIoCount{0};
            std::atomic<uint64_t> m_totalCacheUsageBytes{0};
            std::atomic<uint64_t> m_accessSequence{1};

            mutable std::mutex m_ledgerMutex;
            std::unordered_map<std::string, CacheLedgerEntry> m_cacheLedger;

            mutable std::mutex m_inflightMutex;
            std::unordered_map<std::string, std::vector<BinaryReadCallback>> m_inflightDownloads;

            bool m_initialized = false;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_LOADER_ASYNCLOADER_H
