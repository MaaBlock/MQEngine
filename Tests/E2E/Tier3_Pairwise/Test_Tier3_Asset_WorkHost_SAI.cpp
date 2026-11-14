/**
 * @file Test_Tier3_Asset_WorkHost_SAI.cpp
 * @brief Tier 3 正交组合测试：三级资产寻址清单 × 异步纹理占位热替换与限流 × .mqmesh 直通 × WorkHost 双轨并发/内联降级 × FCT_SAI 沙箱双区与软键盘
 *        覆盖特性：FEAT_16, FEAT_17, FEAT_18, FEAT_19, FEAT_20, FEAT_21, FEAT_22, FEAT_42, FEAT_43, FEAT_46, FEAT_70
 */
#include "Common/E2ECommon.h"

#ifndef E2E_ASSERT_NEAR
#define E2E_ASSERT_NEAR(val1, val2, absError) \
    E2E_ASSERT(std::abs((val1) - (val2)) <= (absError), \
               ::absl::StrCat("期望近似相等: |", #val1, " - ", #val2, "| <= ", #absError))
#endif

#include <FCT/Context/WGL2_Context.h>
#include <FCT_SAI/FileSystem.h>
#include <FCT_SAI/VirtualKeyboard.h>

#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <MQEngine/asset/loader/AsyncTextureUploader.h>
#include <MQEngine/domain/renderer/RenderGraphAsset.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include "DouyinMiniGame.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        // RAII 沙箱双区目录守卫：自动配置 /pkg 与 /cache 物理映射并在析构时还原清理
        struct T3Asset_SandboxZonesGuard
        {
            std::string prevPkgRoot;
            std::string prevCacheRoot;
            std::filesystem::path baseDir;
            std::string pkgDir;
            std::string cacheDir;
            std::string cdnStagingDir;

            explicit T3Asset_SandboxZonesGuard(std::string_view tag)
            {
                prevPkgRoot = std::string(FCT::SAI::FileSystem::getPkgRoot());
                prevCacheRoot = std::string(FCT::SAI::FileSystem::getCacheRoot());

                const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
                baseDir = std::filesystem::temp_directory_path() /
                          absl::StrCat("mq_t3_asset_", tag, "_", tick);
                const auto pkgPath = baseDir / "pkg_ro";
                const auto cachePath = baseDir / "cache_rw";
                const auto cdnPath = baseDir / "cdn_origin";

                std::error_code ec;
                std::filesystem::create_directories(pkgPath, ec);
                std::filesystem::create_directories(cachePath, ec);
                std::filesystem::create_directories(cdnPath, ec);

                pkgDir = pkgPath.generic_string();
                cacheDir = cachePath.generic_string();
                cdnStagingDir = cdnPath.generic_string();

                (void)FCT::SAI::FileSystem::setPkgRoot(pkgDir);
                (void)FCT::SAI::FileSystem::setCacheRoot(cacheDir);
            }

            ~T3Asset_SandboxZonesGuard()
            {
                if (!prevPkgRoot.empty())
                {
                    (void)FCT::SAI::FileSystem::setPkgRoot(prevPkgRoot);
                }
                if (!prevCacheRoot.empty())
                {
                    (void)FCT::SAI::FileSystem::setCacheRoot(prevCacheRoot);
                }
                std::error_code ec;
                std::filesystem::remove_all(baseDir, ec);
            }
        };

        // 通用无异常异步完成同步闩锁
        struct T3Asset_AsyncLatch
        {
            std::mutex mtx;
            std::condition_variable cv;
            uint32_t completed{0};
            uint32_t target{1};

            explicit T3Asset_AsyncLatch(uint32_t expectedCount = 1u)
                : target(expectedCount) {}

            void countDown()
            {
                std::lock_guard<std::mutex> lock(mtx);
                ++completed;
                cv.notify_all();
            }

            bool waitForMs(uint32_t timeoutMs = 3000u)
            {
                std::unique_lock<std::mutex> lock(mtx);
                while (completed < target)
                {
                    if (cv.wait_for(lock, std::chrono::milliseconds(timeoutMs)) == std::cv_status::timeout)
                    {
                        return completed >= target;
                    }
                }
                return true;
            }
        };

        struct T3Asset_StatusCallbackFunctor
        {
            Status* outStatus{nullptr};
            T3Asset_AsyncLatch* latch{nullptr};

            void operator()(Status st) const
            {
                if (outStatus != nullptr)
                {
                    *outStatus = std::move(st);
                }
                if (latch != nullptr)
                {
                    latch->countDown();
                }
            }
        };

        struct T3Asset_BinaryCallbackFunctor
        {
            StatusOr<std::vector<uint8_t>>* outData{nullptr};
            T3Asset_AsyncLatch* latch{nullptr};

            void operator()(StatusOr<std::vector<uint8_t>> res) const
            {
                if (outData != nullptr)
                {
                    *outData = std::move(res);
                }
                if (latch != nullptr)
                {
                    latch->countDown();
                }
            }
        };

        struct T3Asset_TextCallbackFunctor
        {
            StatusOr<std::string>* outText{nullptr};
            bool* doneFlag{nullptr};

            void operator()(StatusOr<std::string> res) const
            {
                if (outText != nullptr)
                {
                    *outText = std::move(res);
                }
                if (doneFlag != nullptr)
                {
                    *doneFlag = true;
                }
            }
        };

        struct T3Asset_DedupFetchSlotFunctor
        {
            std::mutex* sharedMtx{nullptr};
            uint32_t* okCount{nullptr};
            size_t* lastByteCount{nullptr};
            std::string expectedSha256;
            bool* allHashesValid{nullptr};
            T3Asset_AsyncLatch* latch{nullptr};

            void operator()(StatusOr<std::vector<uint8_t>> res) const
            {
                if (sharedMtx != nullptr)
                {
                    std::lock_guard<std::mutex> lock(*sharedMtx);
                    if (res.ok())
                    {
                        if (okCount != nullptr) ++(*okCount);
                        if (lastByteCount != nullptr) *lastByteCount = res->size();
                        if (allHashesValid != nullptr &&
                            !Asset::AssetManifest::verifySha256(*res, expectedSha256))
                        {
                            *allHashesValid = false;
                        }
                    }
                    else if (allHashesValid != nullptr)
                    {
                        *allHashesValid = false;
                    }
                }
                if (latch != nullptr)
                {
                    latch->countDown();
                }
            }
        };

        // 自定义 ASTC 16 字节容器头解码函数对象（验证扩展解码器 + AsyncTextureUploader 全链路）
        struct T3Asset_AstcContainerDecoderFunctor
        {
            StatusOr<Resource::DecodedTextureData> operator()(
                const uint8_t* data,
                size_t size,
                const std::string& filePath) const
            {
                (void)filePath;
                if (data == nullptr || size < 16u)
                {
                    return InvalidArgumentError("ASTC 字节流长度小于 16 字节文件头");
                }
                // ASTC 标准 4 字节魔数: 0x13, 0xAB, 0xA1, 0x5C
                if (data[0] != 0x13u || data[1] != 0xABu || data[2] != 0xA1u || data[3] != 0x5Cu)
                {
                    return InvalidArgumentError("ASTC 文件头魔数不匹配");
                }
                const uint32_t dimX = static_cast<uint32_t>(data[7]) |
                                      (static_cast<uint32_t>(data[8]) << 8u) |
                                      (static_cast<uint32_t>(data[9]) << 16u);
                const uint32_t dimY = static_cast<uint32_t>(data[10]) |
                                      (static_cast<uint32_t>(data[11]) << 8u) |
                                      (static_cast<uint32_t>(data[12]) << 16u);

                Resource::DecodedTextureData out{};
                out.width = (dimX > 0u) ? dimX : 4u;
                out.height = (dimY > 0u) ? dimY : 4u;
                out.channels = 4u;
                out.mipLevels = 1u;
                out.formatName = "ASTC_6x6_UNORM";
                out.pixels.assign(data + 16u, data + size);
                return out;
            }
        };

        struct T3Asset_TextureUploadCallbackFunctor
        {
            StatusOr<Asset::TextureUploadResult>* outResult{nullptr};
            std::atomic<uint32_t>* completedCounter{nullptr};

            void operator()(StatusOr<Asset::TextureUploadResult> res) const
            {
                if (outResult != nullptr)
                {
                    *outResult = std::move(res);
                }
                if (completedCounter != nullptr)
                {
                    completedCounter->fetch_add(1u, std::memory_order_release);
                }
            }
        };

        struct T3Asset_MqMeshAsyncCallbackFunctor
        {
            StatusOr<Asset::MqMeshHeader>* outHeader{nullptr};
            std::vector<uint8_t>* outVtx{nullptr};
            std::vector<uint8_t>* outIdx{nullptr};
            T3Asset_AsyncLatch* latch{nullptr};

            void operator()(
                StatusOr<Asset::MqMeshHeader> hdr,
                std::vector<uint8_t> vtx,
                std::vector<uint8_t> idx) const
            {
                if (outHeader != nullptr) *outHeader = std::move(hdr);
                if (outVtx != nullptr) *outVtx = std::move(vtx);
                if (outIdx != nullptr) *outIdx = std::move(idx);
                if (latch != nullptr) latch->countDown();
            }
        };

        struct T3Asset_ChecksumContractFunctor
        {
            const std::vector<uint8_t>* sharedPayload{nullptr};
            std::atomic<uint64_t>* outChecksum{nullptr};
            Host::WorkHost* host{nullptr};
            std::atomic<uint32_t>* mainThreadCommitCount{nullptr};
            T3Asset_AsyncLatch* latch{nullptr};

            struct MainThreadCommitTask
            {
                std::atomic<uint32_t>* commitCounter{nullptr};
                T3Asset_AsyncLatch* doneLatch{nullptr};

                void operator()() const
                {
                    if (commitCounter != nullptr)
                    {
                        commitCounter->fetch_add(1u, std::memory_order_release);
                    }
                    if (doneLatch != nullptr)
                    {
                        doneLatch->countDown();
                    }
                }
            };

            void operator()() const
            {
                uint64_t sum = 0ULL;
                if (sharedPayload != nullptr)
                {
                    for (uint8_t b : *sharedPayload)
                    {
                        sum += static_cast<uint64_t>(b);
                    }
                }
                if (outChecksum != nullptr)
                {
                    outChecksum->store(sum, std::memory_order_release);
                }
                if (host != nullptr)
                {
                    host->enqueueMainThread(MainThreadCommitTask{mainThreadCommitCount, latch});
                }
            }
        };

        struct T3Asset_StoreReadBufferFunctor
        {
            std::vector<uint8_t>* targetBuf{nullptr};
            Status* readStatus{nullptr};

            void operator()(StatusOr<std::vector<uint8_t>> dataOr) const
            {
                if (readStatus != nullptr)
                {
                    *readStatus = dataOr.status();
                }
                if (dataOr.ok() && targetBuf != nullptr)
                {
                    *targetBuf = std::move(dataOr).value();
                }
            }
        };

        struct T3Asset_ChunkAccumulateFunctor
        {
            std::vector<uint8_t>* reassembled{nullptr};
            uint32_t* chunkCount{nullptr};
            bool* sawLastChunk{nullptr};
            uint32_t abortAfterChunks{0}; // 0 表示不提前中止

            bool operator()(StatusOr<Asset::StreamChunk> chunkOr) const
            {
                if (!chunkOr.ok())
                {
                    return false;
                }
                const auto& ch = chunkOr.value();
                if (reassembled != nullptr)
                {
                    reassembled->insert(reassembled->end(), ch.data.begin(), ch.data.end());
                }
                if (chunkCount != nullptr)
                {
                    ++(*chunkCount);
                }
                if (ch.isLastChunk && sawLastChunk != nullptr)
                {
                    *sawLastChunk = true;
                }
                if (abortAfterChunks > 0u && chunkCount != nullptr && *chunkCount >= abortAfterChunks)
                {
                    return false;
                }
                return true;
            }
        };

        struct T3Asset_InlineIncrementTaskFunctor
        {
            std::atomic<uint32_t>* counter{nullptr};
            uint32_t sleepMicros{0};

            void operator()() const
            {
                if (sleepMicros > 0u)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(sleepMicros));
                }
                if (counter != nullptr)
                {
                    counter->fetch_add(1u, std::memory_order_relaxed);
                }
            }
        };

        struct T3Asset_KeyboardEventRelayFunctor
        {
            Host::WorkHost* workHost{nullptr};
            std::string* targetSlot{nullptr};
            uint32_t* eventCounter{nullptr};

            struct ApplyOnMainThread
            {
                std::string* dst{nullptr};
                uint32_t* cnt{nullptr};
                std::string capturedText;

                void operator()() const
                {
                    if (dst != nullptr)
                    {
                        *dst = capturedText;
                    }
                    if (cnt != nullptr)
                    {
                        ++(*cnt);
                    }
                }
            };

            void operator()(std::string_view text) const
            {
                if (workHost != nullptr)
                {
                    workHost->enqueueMainThread(ApplyOnMainThread{targetSlot, eventCounter, std::string(text)});
                }
            }
        };
    } // namespace

    // 1. FEAT_16 × FEAT_03 × FEAT_42: DouyinAssetCooker 清单烘焙 -> AssetManifest 加载 -> 首包/缓存/CDN 三级路由跃迁
    E2E_TEST(Tier3, FEAT_16, Pair_AssetWh_CookedManifestRoundtripAndThreeTierRouteResolution)
    {
        T3Asset_SandboxZonesGuard zones("manifest_route");

        const std::string bootMeshBytes = "MQMS_BOOT_CUBE_PAYLOAD";
        const std::string bossTexBytes = "ASTC_BOSS_STAGE2_TEXTURE_PAYLOAD";
        const std::string bootSha = DouyinMiniGame::DouyinAssetCooker::computeSha256Hex(bootMeshBytes);
        const std::string bossSha = DouyinMiniGame::DouyinAssetCooker::computeSha256Hex(bossTexBytes);

        // 验证离线烘焙器与运行时 AssetManifest 的 FIPS 180-4 SHA-256 摘要逐位一致
        std::span<const uint8_t> bossSpan(
            reinterpret_cast<const uint8_t*>(bossTexBytes.data()), bossTexBytes.size());
        E2E_ASSERT_EQ(bossSha, Asset::AssetManifest::computeSha256Hex(bossSpan));

        std::vector<Asset::ManifestRecord> cookedRecords(2);
        cookedRecords[0].virtualPath = "meshes/boot_cube.mqmesh";
        cookedRecords[0].sha256Hex = bootSha;
        cookedRecords[0].fileSize = static_cast<uint32_t>(bootMeshBytes.size());
        cookedRecords[0].bundleType = Asset::BundleType::FirstPackage;

        cookedRecords[1].virtualPath = "textures/boss_albedo.astc";
        cookedRecords[1].sha256Hex = bossSha;
        cookedRecords[1].fileSize = static_cast<uint32_t>(bossTexBytes.size());
        cookedRecords[1].bundleType = Asset::BundleType::RemoteCache;

        const auto manifestBytes = DouyinMiniGame::DouyinAssetCooker::serializeManifest(
            cookedRecords, "https://cdn.example.com/minigame/v1");

        Asset::AssetManifest runtimeManifest;
        runtimeManifest.setCdnBaseUrl("https://cdn.example.com/minigame/v1/");
        E2E_ASSERT_STATUS_OK(runtimeManifest.loadFromBuffer(manifestBytes));
        E2E_ASSERT_EQ(runtimeManifest.getRecordCount(), 2u);

        // Tier 1: 首包资产直接命中 /pkg/...
        auto bootRouteOr = runtimeManifest.resolveAsset("meshes/boot_cube.mqmesh");
        E2E_ASSERT_STATUS_OK(bootRouteOr);
        E2E_ASSERT_EQ(bootRouteOr->location, Asset::AssetResolveResult::Location::FirstPackagePkg);
        E2E_ASSERT_EQ(bootRouteOr->resolvedPath, "/pkg/meshes/boot_cube.mqmesh");

        // Tier 3: 远端资产尚未落盘至 /cache 时解析为 RemoteCdn，附带去重尾斜杠后的完整 URL
        auto bossColdOr = runtimeManifest.resolveAsset("textures/boss_albedo.astc");
        E2E_ASSERT_STATUS_OK(bossColdOr);
        E2E_ASSERT_EQ(bossColdOr->location, Asset::AssetResolveResult::Location::RemoteCdn);
        E2E_ASSERT_EQ(bossColdOr->downloadUrl, absl::StrCat("https://cdn.example.com/minigame/v1/", bossSha, ".bin"));
        E2E_ASSERT_EQ(bossColdOr->cacheDestPath, absl::StrCat("/cache/", bossSha, ".bin"));

        // 将下载载荷写入 /cache/<sha256>.bin 后，同一路径再次解析自动跃迁至 Tier 2 LocalCache
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            bossColdOr->cacheDestPath, bossTexBytes.data(), bossTexBytes.size()));

        auto bossWarmOr = runtimeManifest.resolveAsset("textures/boss_albedo.astc");
        E2E_ASSERT_STATUS_OK(bossWarmOr);
        E2E_ASSERT_EQ(bossWarmOr->location, Asset::AssetResolveResult::Location::LocalCache);
        E2E_ASSERT_EQ(bossWarmOr->resolvedPath, bossColdOr->cacheDestPath);

        return true;
    }

    // 2. FEAT_16 × FEAT_42: AssetManifest 目录穿越拦截、损坏清单防呆与包相对路径净化联动
    E2E_TEST(Tier3, FEAT_16, Pair_AssetWh_ManifestPathTraversalGuardAndCorruptPayloadRejection)
    {
        Asset::AssetManifest manifest;
        Asset::ManifestRecord rec;
        rec.virtualPath = "ui/hud_atlas.astc";
        rec.sha256Hex = std::string(64u, 'a');
        rec.fileSize = 2048u;
        rec.bundleType = Asset::BundleType::FirstPackage;
        manifest.registerRecord(rec);

        // 目录穿越 .. 与反斜杠路径必须立即返回 InvalidArgumentError
        auto traversalOr = manifest.resolveAsset("../secret/config.toml");
        E2E_ASSERT_FALSE(traversalOr.ok());
        E2E_ASSERT_EQ(traversalOr.status().code(), absl::StatusCode::kInvalidArgument);

        auto backslashOr = manifest.resolveAsset("ui\\hud_atlas.astc");
        E2E_ASSERT_FALSE(backslashOr.ok());
        E2E_ASSERT_EQ(backslashOr.status().code(), absl::StatusCode::kInvalidArgument);

        // 未登记资产返回 NotFoundError
        auto missingOr = manifest.resolveAsset("audio/non_existent.ogg");
        E2E_ASSERT_FALSE(missingOr.ok());
        E2E_ASSERT_EQ(missingOr.status().code(), absl::StatusCode::kNotFound);

        // 截断的 MQMB 二进制头与越界 BundleType 文本行均须无异常返回错误 Status
        const uint8_t truncatedMqmb[6] = {'M', 'Q', 'M', 'B', 0x05u, 0x00u};
        Status badBinStatus = manifest.loadFromBuffer(truncatedMqmb);
        E2E_ASSERT_FALSE(badBinStatus.ok());

        const std::string badBundleLine = "ui/bad.astc|" + std::string(64u, 'b') + "|128|9\n";
        Status badTextStatus = manifest.loadFromBuffer(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(badBundleLine.data()), badBundleLine.size()));
        E2E_ASSERT_FALSE(badTextStatus.ok());

        // 离线打包器侧同步净化前导斜杠与反斜杠
        const std::string sanitized = DouyinMiniGame::DouyinAssetCooker::sanitizePackageRelativePath("\\ui\\hud_atlas.astc");
        E2E_ASSERT_EQ(sanitized.find('\\'), std::string::npos);

        return true;
    }

    // 3. FEAT_42 × FEAT_21: FCT::SAI::FileSystem /pkg 只读写保护、连续斜杠逃逸拦截与 AsyncLoader 异步写权限传导
    E2E_TEST(Tier3, FEAT_42, Pair_AssetWh_SaiDualZonePkgReadOnlyGuardAndInjectionDefense)
    {
        T3Asset_SandboxZonesGuard zones("sai_dual_zone");

        // 预置首包只读文件（通过物理首包目录在打包阶段落盘）
        const std::string bootCfgPath = zones.pkgDir + "/game_boot.toml";
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile(bootCfgPath, "fps = 60\n"));

        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::isVirtualPath("/pkg/game_boot.toml"));
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::isVirtualPath("/cache/user_save.json"));
        E2E_ASSERT_FALSE(FCT::SAI::FileSystem::isVirtualPath("relative/local.txt"));

        // /pkg 读取放行，但任何写解析 (forWrite=true) 或 writeTextFile 必须返回 PermissionDeniedError
        auto readPkgOr = FCT::SAI::FileSystem::readTextFile("/pkg/game_boot.toml");
        E2E_ASSERT_STATUS_OK(readPkgOr);
        E2E_ASSERT_EQ(*readPkgOr, "fps = 60\n");

        auto writePkgResolveOr = FCT::SAI::FileSystem::resolveVirtualPath("/pkg/game_boot.toml", true);
        E2E_ASSERT_FALSE(writePkgResolveOr.ok());
        E2E_ASSERT_EQ(writePkgResolveOr.status().code(), absl::StatusCode::kPermissionDenied);

        Status directWritePkg = FCT::SAI::FileSystem::writeTextFile("/pkg/tamper.txt", "illegal");
        E2E_ASSERT_FALSE(directWritePkg.ok());
        E2E_ASSERT_EQ(directWritePkg.code(), absl::StatusCode::kPermissionDenied);

        // 连续斜杠根注入与 .. 越界逃逸拦截
        auto slashInjectOr = FCT::SAI::FileSystem::resolveVirtualPath("/cache//escape.bin", false);
        E2E_ASSERT_FALSE(slashInjectOr.ok());
        E2E_ASSERT_EQ(slashInjectOr.status().code(), absl::StatusCode::kPermissionDenied);

        auto dotEscapeOr = FCT::SAI::FileSystem::resolveVirtualPath("/cache/../../escape.bin", true);
        E2E_ASSERT_FALSE(dotEscapeOr.ok());

        // 联动 WorkHost + AsyncLoader：向 /pkg 异步写入报错传导，向 /cache 异步写入成功落盘
        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 1u, 1u));
        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());

        Status pkgAsyncWriteSt = OkStatus();
        T3Asset_AsyncLatch pkgLatch(1u);
        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncFileWrite(
            "/pkg/async_tamper.bin", "nope", T3Asset_StatusCallbackFunctor{&pkgAsyncWriteSt, &pkgLatch}));
        E2E_ASSERT_TRUE(pkgLatch.waitForMs());
        E2E_ASSERT_FALSE(pkgAsyncWriteSt.ok());

        Status cacheAsyncWriteSt = InternalError("pending");
        T3Asset_AsyncLatch cacheLatch(1u);
        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncFileWrite(
            "/cache/profile/save_slot0.json", "{\"gold\":888}",
            T3Asset_StatusCallbackFunctor{&cacheAsyncWriteSt, &cacheLatch}));
        E2E_ASSERT_TRUE(cacheLatch.waitForMs());
        E2E_ASSERT_STATUS_OK(cacheAsyncWriteSt);

        auto savedJsonOr = FCT::SAI::FileSystem::readTextFile("/cache/profile/save_slot0.json");
        E2E_ASSERT_STATUS_OK(savedJsonOr);
        E2E_ASSERT_EQ(*savedJsonOr, "{\"gold\":888}");

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 4. FEAT_43 × FEAT_16: FCT::SAI::FileSystem 流式下载直写 /cache 校验 SHA-256 并驱动 AssetManifest 命中本地缓存
    E2E_TEST(Tier3, FEAT_43, Pair_AssetWh_SaiStreamDownloadToCacheAndManifestUpgrade)
    {
        T3Asset_SandboxZonesGuard zones("sai_stream_dl");

        // 构造 128KB 模拟远端 CDN 原始分包文件，验证 64KB 分块流式落盘零截断
        std::vector<uint8_t> originBytes(128u * 1024u);
        for (size_t i = 0; i < originBytes.size(); ++i)
        {
            originBytes[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
        }
        const std::string expectedSha = Asset::AssetManifest::computeSha256Hex(originBytes);
        const std::string cdnLocalFile = zones.cdnStagingDir + "/" + expectedSha + ".bin";
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            cdnLocalFile, originBytes.data(), originBytes.size()));

        Asset::AssetManifest manifest;
        Asset::ManifestRecord rec;
        rec.virtualPath = "levels/chapter2_terrain.mqmesh";
        rec.sha256Hex = expectedSha;
        rec.fileSize = static_cast<uint32_t>(originBytes.size());
        rec.bundleType = Asset::BundleType::RemoteCache;
        manifest.setCdnBaseUrl(absl::StrCat("file://", zones.cdnStagingDir));
        manifest.registerRecord(rec);

        // 1) 下载目标若指向 /pkg 必须被 downloadFileToPathAsync 立即拒绝
        Status badDestStatus = OkStatus();
        T3Asset_AsyncLatch badDestLatch(1u);
        FCT::SAI::FileSystem::downloadFileToPathAsync(
            absl::StrCat("file://", cdnLocalFile),
            "/pkg/illegal_target.bin",
            T3Asset_StatusCallbackFunctor{&badDestStatus, &badDestLatch});
        E2E_ASSERT_TRUE(badDestLatch.waitForMs());
        E2E_ASSERT_FALSE(badDestStatus.ok());
        E2E_ASSERT_EQ(badDestStatus.code(), absl::StatusCode::kInvalidArgument);

        // 2) 按 AssetManifest 解析出的 URL 与 /cache/<sha256>.bin 执行流式下载落盘
        auto routeBeforeOr = manifest.resolveAsset("levels/chapter2_terrain.mqmesh");
        E2E_ASSERT_STATUS_OK(routeBeforeOr);
        E2E_ASSERT_EQ(routeBeforeOr->location, Asset::AssetResolveResult::Location::RemoteCdn);

        Status dlStatus = InternalError("pending");
        T3Asset_AsyncLatch dlLatch(1u);
        FCT::SAI::FileSystem::downloadFileToPathAsync(
            routeBeforeOr->downloadUrl,
            routeBeforeOr->cacheDestPath,
            T3Asset_StatusCallbackFunctor{&dlStatus, &dlLatch});
        E2E_ASSERT_TRUE(dlLatch.waitForMs());
        E2E_ASSERT_STATUS_OK(dlStatus);

        // 3) 通过异步读取接口读回落盘文件并校验 SHA-256 完整性，同时验证清单路由已自动升级为 LocalCache
        StatusOr<std::vector<uint8_t>> asyncReadRes = NotFoundError("pending");
        T3Asset_AsyncLatch readLatch(1u);
        FCT::SAI::FileSystem::readBinaryFileAsync(
            routeBeforeOr->cacheDestPath,
            T3Asset_BinaryCallbackFunctor{&asyncReadRes, &readLatch});
        E2E_ASSERT_TRUE(readLatch.waitForMs());
        E2E_ASSERT_STATUS_OK(asyncReadRes);
        E2E_ASSERT_EQ(asyncReadRes->size(), originBytes.size());
        E2E_ASSERT_TRUE(Asset::AssetManifest::verifySha256(*asyncReadRes, expectedSha));

        auto routeAfterOr = manifest.resolveAsset("levels/chapter2_terrain.mqmesh");
        E2E_ASSERT_STATUS_OK(routeAfterOr);
        E2E_ASSERT_EQ(routeAfterOr->location, Asset::AssetResolveResult::Location::LocalCache);

        return true;
    }

    // 5. FEAT_43 × FEAT_21: AsyncLoader 同一远端 URL 并发请求自动合并去重与全等待者广播唤醒
    E2E_TEST(Tier3, FEAT_43, Pair_AssetWh_AsyncLoaderConcurrentNetworkFetchDeduplication)
    {
        T3Asset_SandboxZonesGuard zones("fetch_dedup");

        std::vector<uint8_t> sharedPayload(48u * 1024u, 0x5Au);
        for (size_t i = 0; i < sharedPayload.size(); i += 64u)
        {
            sharedPayload[i] = static_cast<uint8_t>((i >> 6u) & 0xFFu);
        }
        const std::string shaHex = Asset::AssetManifest::computeSha256Hex(sharedPayload);
        const std::string srcFile = zones.cdnStagingDir + "/shared_fx_bundle.bin";
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            srcFile, sharedPayload.data(), sharedPayload.size()));

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());

        const std::string fileUrl = absl::StrCat("file://", srcFile);
        std::mutex cbMtx;
        uint32_t successCallbacks = 0u;
        size_t observedBytes = 0u;
        bool hashesMatch = true;
        T3Asset_AsyncLatch latch(4u);

        for (uint32_t i = 0u; i < 4u; ++i)
        {
            E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncNetworkFetch(
                fileUrl,
                T3Asset_DedupFetchSlotFunctor{
                    &cbMtx, &successCallbacks, &observedBytes, shaHex, &hashesMatch, &latch}));
        }

        E2E_ASSERT_TRUE(latch.waitForMs());
        E2E_ASSERT_EQ(successCallbacks, 4u);
        E2E_ASSERT_EQ(observedBytes, sharedPayload.size());
        E2E_ASSERT_TRUE(hashesMatch);
        E2E_ASSERT_EQ(asyncLoader.getInflightDownloadCount(), 0u);

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 6. FEAT_17 × FEAT_13: 纹理占位秒开立即返回 Slot 2 句柄与下载就绪后 GPUScene 原子热替换闭环
    E2E_TEST(Tier3, FEAT_17, Pair_AssetWh_PlaceholderSlotTwoInstantReturnAndGpuSceneHotSwap)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        wgl2Ctx.createPlatform(FCT::ContextCreateFlags(0));

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::SingleThreadInline));
        Resource::TextureDecoderRegistry decoderReg;
        E2E_ASSERT_STATUS_OK(decoderReg.init());
        Asset::AsyncTextureUploader uploader;
        E2E_ASSERT_STATUS_OK(uploader.init());

        Domain::Renderer::GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));

        // 1) GPUScene 内置槽位 2 默认即为占位标记贴图 (flags == 1)
        const auto builtinPlaceholderRec = gpuScene.getTextureRecord(Asset::AsyncTextureUploader::PlaceholderTextureSlot);
        E2E_ASSERT_EQ(builtinPlaceholderRec.flags, 1u);

        // 2) 异步请求远端贴图，零阻塞立即返回逻辑句柄 (>= 10)，就绪前自动解析到占位槽位 2
        const uint32_t texHandle = uploader.requestTextureWithPlaceholder("textures/hero_cape.astc");
        E2E_ASSERT_GE(texHandle, 10u);
        E2E_ASSERT_FALSE(uploader.isTextureReady(texHandle));
        E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(texHandle), Asset::AsyncTextureUploader::PlaceholderTextureSlot);

        // 3) 模拟贴图下载解码就绪：在 GPUScene Shelf 图集中分配真实子区并执行热替换
        const auto atlasAlloc = gpuScene.registerTextureToAtlas(512u, 256u);
        E2E_ASSERT_TRUE(atlasAlloc.success);
        const FCT::Vec4 realUv = gpuScene.getTextureUvScaleOffset(atlasAlloc);
        constexpr uint32_t kRealMaterialTexSlot = 18u;

        Domain::Renderer::GpuTextureRecord readyRec{};
        readyRec.uvScaleOffset = realUv;
        readyRec.bindlessOrPageIndex = atlasAlloc.layer;
        readyRec.flags = 0u;
        gpuScene.updateTextureRecord(kRealMaterialTexSlot, readyRec);

        uploader.onDownloadComplete(texHandle, kRealMaterialTexSlot, realUv);
        E2E_ASSERT_TRUE(uploader.isTextureReady(texHandle));
        E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(texHandle), kRealMaterialTexSlot);
        E2E_ASSERT_NEAR(uploader.getTextureUvScaleOffset(texHandle).x, realUv.x, 1e-4f);
        E2E_ASSERT_NEAR(uploader.getTextureUvScaleOffset(texHandle).y, realUv.y, 1e-4f);

        // 4) 若另一贴图下载失败，句柄保持回退至占位槽位 2，绝不产生黑块或崩溃
        const uint32_t failedHandle = uploader.requestTextureWithPlaceholder("textures/corrupted_remote.astc");
        uploader.onDownloadFailed(failedHandle);
        E2E_ASSERT_FALSE(uploader.isTextureReady(failedHandle));
        E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(failedHandle), Asset::AsyncTextureUploader::PlaceholderTextureSlot);

        gpuScene.shutdown();
        uploader.term();
        decoderReg.term();
        workHost.term();
        return true;
    }

    // 7. FEAT_17 × FEAT_03: DouyinAssetCooker 离线烘焙 ASTC 容器流经自定义解码器与 AsyncTextureUploader 异步上传
    E2E_TEST(Tier3, FEAT_17, Pair_AssetWh_CustomAstcFormatDecoderAsyncUploadToGpuQueue)
    {
        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
        Resource::TextureDecoderRegistry decoderReg;
        E2E_ASSERT_STATUS_OK(decoderReg.init());
        Asset::AsyncTextureUploader uploader;
        E2E_ASSERT_STATUS_OK(uploader.init());

        uploader.registerFormatDecoder(".astc", T3Asset_AstcContainerDecoderFunctor{});
        E2E_ASSERT_TRUE(uploader.isExtensionSupported(".astc"));

        // 使用真实 DouyinAssetCooker 烘焙一张 64x64 RGBA 源图至 ASTC 6x6 容器字节流
        std::vector<uint8_t> rawRgba(64u * 64u * 4u, 0xCCu);
        auto astcBytesOr = DouyinMiniGame::DouyinAssetCooker::cookTextureToAstc(rawRgba, 64u, 64u, "6x6");
        E2E_ASSERT_STATUS_OK(astcBytesOr);
        E2E_ASSERT_GT(astcBytesOr->size(), 16u);

        Asset::TextureUploadDesc uploadDesc;
        uploadDesc.filePath = "virtual_mem/prop_crate.astc";
        uploadDesc.memoryData = *astcBytesOr;
        uploadDesc.sRGB = true;
        uploadDesc.debugName = "PropCrateAstc";

        StatusOr<Asset::TextureUploadResult> uploadRes = NotFoundError("pending");
        std::atomic<uint32_t> completedCount{0u};
        E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(
            std::move(uploadDesc),
            T3Asset_TextureUploadCallbackFunctor{&uploadRes, &completedCount}));

        // 等待后台工作契约完成 CPU 解码并排入 GPU 分帧上传队列，随后驱动 tick() 提交
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (completedCount.load(std::memory_order_acquire) == 0u &&
               std::chrono::steady_clock::now() < deadline)
        {
            uploader.tick(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        E2E_ASSERT_EQ(completedCount.load(std::memory_order_acquire), 1u);
        E2E_ASSERT_STATUS_OK(uploadRes);
        E2E_ASSERT_EQ(uploadRes->width, 64u);
        E2E_ASSERT_EQ(uploadRes->height, 64u);
        E2E_ASSERT_NE(uploadRes->textureHandle, nullptr);
        E2E_ASSERT_GT(uploadRes->totalBytes, 0u);

        uploader.term();
        decoderReg.term();
        workHost.term();
        return true;
    }

    // 8. FEAT_19 × FEAT_21: AsyncTextureUploader 每帧 <= 1.5MB 显存上传带宽平滑限流跨多帧排空验证
    E2E_TEST(Tier3, FEAT_19, Pair_AssetWh_FrameBandwidthBudget1536KBThrottlingAcrossMultipleTicks)
    {
        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::SingleThreadInline));
        Resource::TextureDecoderRegistry decoderReg;
        E2E_ASSERT_STATUS_OK(decoderReg.init());
        Asset::AsyncTextureUploader uploader;
        E2E_ASSERT_STATUS_OK(uploader.init());

        // 默认预算严格等于 1.5 MB (1,572,864 B)
        E2E_ASSERT_EQ(uploader.getUploadBudgetPerFrame(), Asset::AsyncTextureUploader::DefaultMaxUploadBudgetPerFrame);

        // 连续入队 4 张各 1.0 MB (1,048,576 B) 的纹理任务；每帧 1.5 MB 预算最多放行 1 张（2 张 = 2.0 MB > 1.5 MB）
        constexpr uint64_t kOneMegabyte = 1024ULL * 1024ULL;
        std::atomic<uint32_t> finishedUploads{0u};
        for (uint32_t i = 0u; i < 4u; ++i)
        {
            uploader.enqueueStagedUpload(
                kOneMegabyte,
                absl::StrCat("StagedLightmap_", i),
                T3Asset_TextureUploadCallbackFunctor{nullptr, &finishedUploads});
        }
        E2E_ASSERT_EQ(uploader.getQueuedGpuUploadCount(), 4u);

        for (uint32_t frame = 1u; frame <= 4u; ++frame)
        {
            uploader.tick(0.016667f);
            E2E_ASSERT_LE(uploader.getUploadedBytesThisFrame(), uploader.getUploadBudgetPerFrame());
            E2E_ASSERT_EQ(finishedUploads.load(std::memory_order_acquire), frame);
            E2E_ASSERT_EQ(uploader.getQueuedGpuUploadCount(), 4u - frame);
        }

        E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 4ULL * kOneMegabyte);

        uploader.term();
        decoderReg.term();
        workHost.term();
        return true;
    }

    // 9. FEAT_18 × FEAT_03: DouyinAssetCooker 烘焙 48B 顶点步幅 .mqmesh 经 AsyncLoader 免 Assimp 二进制直通加载
    E2E_TEST(Tier3, FEAT_18, Pair_AssetWh_DouyinBakedMqMeshPassthroughViaAsyncLoader)
    {
        T3Asset_SandboxZonesGuard zones("mqmesh_passthrough");

        const std::string triangleObj =
            "v -1.0 0.0 0.0\n"
            "v  1.0 0.0 0.0\n"
            "v  0.0 2.0 0.0\n"
            "vn 0.0 0.0 1.0\n"
            "vt 0.0 0.0\n"
            "vt 1.0 0.0\n"
            "vt 0.5 1.0\n"
            "f 1/1/1 2/2/1 3/3/1\n";

        auto bakedBytesOr = DouyinMiniGame::DouyinAssetCooker::bakeObjToMqMesh(triangleObj);
        E2E_ASSERT_STATUS_OK(bakedBytesOr);
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            "/cache/meshes/hero_blade.mqmesh", bakedBytesOr->data(), bakedBytesOr->size()));

        // 1) 静态零拷贝头与子网格范围防呆校验
        auto parsedOr = Asset::AsyncLoader::parseMqMeshBuffer(*bakedBytesOr);
        E2E_ASSERT_STATUS_OK(parsedOr);
        E2E_ASSERT_EQ(parsedOr->header.magic, 0x4D514D53u);
        E2E_ASSERT_EQ(parsedOr->header.vertexStride, sizeof(Asset::StandardMeshVertex));
        E2E_ASSERT_EQ(sizeof(Asset::StandardMeshVertex), 48u);
        E2E_ASSERT_EQ(parsedOr->header.vertexCount, 3u);
        E2E_ASSERT_EQ(parsedOr->header.indexCount, 3u);
        E2E_ASSERT_NEAR(parsedOr->header.aabbMax[1], 2.0f, 1e-4f);

        Asset::SubMeshRange badSubMesh{};
        badSubMesh.indexOffset = 2u;
        badSubMesh.indexCount = 5u; // 2 + 5 = 7 > indexCount(3)，必须拦截
        const Asset::SubMeshRange badRanges[] = {badSubMesh};
        Status badRangeSt = Asset::AsyncLoader::validateSubmeshRanges(parsedOr->header, badRanges);
        E2E_ASSERT_FALSE(badRangeSt.ok());

        // 2) 通过 WorkHost + AsyncLoader::submitAsyncMqMeshRead 异步流式直通读取顶点与索引缓冲
        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 1u, 1u));
        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());

        StatusOr<Asset::MqMeshHeader> asyncHdr = NotFoundError("pending");
        std::vector<uint8_t> asyncVtx;
        std::vector<uint8_t> asyncIdx;
        T3Asset_AsyncLatch meshLatch(1u);

        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncMqMeshRead(
            "/cache/meshes/hero_blade.mqmesh",
            T3Asset_MqMeshAsyncCallbackFunctor{&asyncHdr, &asyncVtx, &asyncIdx, &meshLatch}));
        E2E_ASSERT_TRUE(meshLatch.waitForMs());
        E2E_ASSERT_STATUS_OK(asyncHdr);
        E2E_ASSERT_EQ(asyncHdr->vertexCount, 3u);
        E2E_ASSERT_EQ(asyncVtx.size(), 3u * sizeof(Asset::StandardMeshVertex));
        E2E_ASSERT_EQ(asyncIdx.size(), 3u * sizeof(uint32_t));

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 10. FEAT_20 × FEAT_42 × FEAT_08: 180MB/140MB 沙箱磁盘配额 LRU 淘汰、Pinned 首包锁存保护与内存告警紧急收缩
    E2E_TEST(Tier3, FEAT_20, Pair_AssetWh_CacheLedgerLruEvictionWithPinnedProtectionAndDiskCleanup)
    {
        T3Asset_SandboxZonesGuard zones("cache_lru_quota");

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::SingleThreadInline));
        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());
        asyncLoader.clearCacheLedger();

        const std::string shaPinned(64u, '1'); // 最早写入，但被 Pin 锁存保护 (55 MB)
        const std::string shaHot(64u, '2');    // 次早写入，随后被再次访问续命为热点 (50 MB)
        const std::string shaColdA(64u, '3');  // 未续命冷资产 A (45 MB) -> 最先被 LRU 淘汰
        const std::string shaColdB(64u, '4');  // 未续命冷资产 B (45 MB) -> 次先被 LRU 淘汰

        const std::string dummyDiskBytes = "cached_binary_stub";
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile("/cache/" + shaPinned + ".bin", dummyDiskBytes));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile("/cache/" + shaHot + ".bin", dummyDiskBytes));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile("/cache/" + shaColdA + ".bin", dummyDiskBytes));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile("/cache/" + shaColdB + ".bin", dummyDiskBytes));

        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        asyncLoader.recordCacheAccess(shaPinned, 55ULL * kMB);
        asyncLoader.pinCacheEntry(shaPinned);
        asyncLoader.recordCacheAccess(shaHot, 50ULL * kMB);
        asyncLoader.recordCacheAccess(shaColdA, 45ULL * kMB);
        asyncLoader.recordCacheAccess(shaColdB, 45ULL * kMB);
        // 刷新 shaHot 访问序号，使冷旧顺序变为：shaColdA < shaColdB < shaHot（shaPinned 免淘汰）
        asyncLoader.updateCacheAccessTime(shaHot);

        // 当前账本总计 195 MB > 高水位 180 MB，触发淘汰直至 <= 低水位 140 MB（淘汰 shaColdA 45MB + shaColdB 45MB -> 剩 105MB）
        E2E_ASSERT_EQ(asyncLoader.getTotalCacheUsageBytes(), 195ULL * kMB);
        asyncLoader.enforceCacheQuota();
        E2E_ASSERT_LE(asyncLoader.getTotalCacheUsageBytes(), Asset::AsyncLoader::DefaultCacheLowWatermarkBytes);
        E2E_ASSERT_EQ(asyncLoader.getTotalCacheUsageBytes(), 105ULL * kMB);

        E2E_ASSERT_TRUE(asyncLoader.hasCacheEntry(shaPinned));
        E2E_ASSERT_TRUE(asyncLoader.hasCacheEntry(shaHot));
        E2E_ASSERT_FALSE(asyncLoader.hasCacheEntry(shaColdA));
        E2E_ASSERT_FALSE(asyncLoader.hasCacheEntry(shaColdB));

        // 物理沙箱磁盘上的冷文件已同步清理，而 Pinned 与 Hot 文件完好保留
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists("/cache/" + shaPinned + ".bin"));
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists("/cache/" + shaHot + ".bin"));
        E2E_ASSERT_FALSE(FCT::SAI::FileSystem::exists("/cache/" + shaColdA + ".bin"));
        E2E_ASSERT_FALSE(FCT::SAI::FileSystem::exists("/cache/" + shaColdB + ".bin"));

        // 收到系统级紧急内存告警 (level >= 2)：进一步压缩至 <= 80MB，淘汰未锁存的 shaHot，仅保留 Pinned (55MB)
        asyncLoader.onMemoryWarning(2);
        E2E_ASSERT_EQ(asyncLoader.getTotalCacheUsageBytes(), 55ULL * kMB);
        E2E_ASSERT_TRUE(asyncLoader.hasCacheEntry(shaPinned));
        E2E_ASSERT_FALSE(asyncLoader.hasCacheEntry(shaHot));

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 11. FEAT_21 × FEAT_42: WorkHost 并发度推荐策略、I/O 线程异步读完直驱 CPU WorkContract 与主线程回传队列
    E2E_TEST(Tier3, FEAT_21, Pair_AssetWh_WorkHostMultiThreadedContractsAndAsyncReadDispatch)
    {
        T3Asset_SandboxZonesGuard zones("workhost_pipeline");

        // Wasm 小游戏启用 pthread 时 Worker 数严格钳位在 [2, 3]；缺失 SharedArrayBuffer/pthread 时返回 0 降级
        E2E_ASSERT_EQ(Host::WorkHost::resolveRecommendedWorkerCount(8u, false, true), 0u);
        const uint32_t miniGameWorkers = Host::WorkHost::resolveRecommendedWorkerCount(16u, true, true);
        E2E_ASSERT_GE(miniGameWorkers, 2u);
        E2E_ASSERT_LE(miniGameWorkers, 3u);

        // 准备 4096 字节确定性二进制资产（全字节值 = 3，预期总和 = 12288）
        std::vector<uint8_t> rawBytes(4096u, 3u);
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            "/cache/shader_blob.bin", rawBytes.data(), rawBytes.size()));

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
        E2E_ASSERT_EQ(workHost.getMode(), Host::WorkHost::Mode::MultiThreadedWorkers);
        E2E_ASSERT_EQ(workHost.getWorkerCount(), 2u);
        E2E_ASSERT_EQ(workHost.getIoThreadCount(), 1u);

        std::vector<uint8_t> ioLoadedBuffer;
        Status ioReadStatus = InternalError("pending");
        std::atomic<uint64_t> computedChecksum{0ULL};
        std::atomic<uint32_t> mainThreadCommits{0u};
        T3Asset_AsyncLatch pipelineLatch(1u);

        bcpp::work_contract cpuContract = workHost.createContract(
            T3Asset_ChecksumContractFunctor{
                &ioLoadedBuffer, &computedChecksum, &workHost, &mainThreadCommits, &pipelineLatch});

        // I/O 线程完成磁盘读取 -> 填充 ioLoadedBuffer -> 自动 schedule 唤醒 CPU WorkContract -> 入队主线程回调
        workHost.asyncReadAndDispatch(
            "/cache/shader_blob.bin",
            std::move(cpuContract),
            T3Asset_StoreReadBufferFunctor{&ioLoadedBuffer, &ioReadStatus});

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (mainThreadCommits.load(std::memory_order_acquire) == 0u &&
               std::chrono::steady_clock::now() < deadline)
        {
            (void)workHost.pumpMainThreadTasks();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        (void)pipelineLatch.waitForMs(100u);

        E2E_ASSERT_STATUS_OK(ioReadStatus);
        E2E_ASSERT_EQ(computedChecksum.load(std::memory_order_acquire), 4096ULL * 3ULL);
        E2E_ASSERT_EQ(mainThreadCommits.load(std::memory_order_acquire), 1u);

        workHost.term();
        return true;
    }

    // 12. FEAT_21 × FEAT_16: AsyncLoader 超大文件分块流式读取完整重组与消费者提前中止契约
    E2E_TEST(Tier3, FEAT_21, Pair_AssetWh_AsyncChunkedStreamWithEarlyAbortAndFullRead)
    {
        T3Asset_SandboxZonesGuard zones("chunked_stream");

        std::vector<uint8_t> largeFile(160u * 1024u);
        for (size_t i = 0; i < largeFile.size(); ++i)
        {
            largeFile[i] = static_cast<uint8_t>((i * 13u + 5u) & 0xFFu);
        }
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
            "/cache/audio/bgm_stream.bin", largeFile.data(), largeFile.size()));

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::MultiThreadedWorkers, 1u, 1u));
        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());

        // 1) 按 32KB 分块完整流式读取（共 5 块），校验重组字节与原文件 SHA-256 完全一致
        std::vector<uint8_t> fullReassembled;
        uint32_t fullChunkCount = 0u;
        bool sawLast = false;
        Status fullFinishSt = InternalError("pending");
        T3Asset_AsyncLatch fullLatch(1u);

        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncChunkedStream(
            "/cache/audio/bgm_stream.bin",
            32u * 1024u,
            T3Asset_ChunkAccumulateFunctor{&fullReassembled, &fullChunkCount, &sawLast, 0u},
            T3Asset_StatusCallbackFunctor{&fullFinishSt, &fullLatch}));
        E2E_ASSERT_TRUE(fullLatch.waitForMs());
        E2E_ASSERT_STATUS_OK(fullFinishSt);
        E2E_ASSERT_EQ(fullChunkCount, 5u);
        E2E_ASSERT_TRUE(sawLast);
        E2E_ASSERT_EQ(fullReassembled, largeFile);

        // 2) 消费者在第 2 块返回 false 主动中止流读取，验证不会继续读取后续 3 块
        std::vector<uint8_t> partialBytes;
        uint32_t partialChunks = 0u;
        bool partialSawLast = false;
        Status abortFinishSt = OkStatus();
        T3Asset_AsyncLatch abortLatch(1u);

        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncChunkedStream(
            "/cache/audio/bgm_stream.bin",
            32u * 1024u,
            T3Asset_ChunkAccumulateFunctor{&partialBytes, &partialChunks, &partialSawLast, 2u},
            T3Asset_StatusCallbackFunctor{&abortFinishSt, &abortLatch}));
        E2E_ASSERT_TRUE(abortLatch.waitForMs());
        E2E_ASSERT_EQ(partialChunks, 2u);
        E2E_ASSERT_EQ(partialBytes.size(), 64u * 1024u);
        E2E_ASSERT_FALSE(partialSawLast);

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 13. FEAT_22 × FEAT_42: WorkHost SingleThreadInline 零工作线程降级模式与微秒级时间片分帧排空
    E2E_TEST(Tier3, FEAT_22, Pair_AssetWh_WorkHostSingleThreadInlineTimeSliceDrainDegradation)
    {
        T3Asset_SandboxZonesGuard zones("inline_degrade");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile("/cache/inline_note.txt", "SingleThreadFallbackOK"));

        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::SingleThreadInline));
        E2E_ASSERT_EQ(workHost.getMode(), Host::WorkHost::Mode::SingleThreadInline);
        E2E_ASSERT_EQ(workHost.getWorkerCount(), 0u);
        E2E_ASSERT_EQ(workHost.getIoThreadCount(), 0u);

        Asset::AsyncLoader asyncLoader;
        E2E_ASSERT_STATUS_OK(asyncLoader.init());

        // 入队 6 个每个耗时约 1.5ms 的通用 CPU 任务，单次给定 2ms 预算不得一次性阻塞跑完所有任务，多次帧末排空后全部完成
        std::atomic<uint32_t> executedTasks{0u};
        for (uint32_t i = 0u; i < 6u; ++i)
        {
            E2E_ASSERT_STATUS_OK(workHost.postTask(T3Asset_InlineIncrementTaskFunctor{&executedTasks, 1500u}));
        }
        E2E_ASSERT_EQ(executedTasks.load(std::memory_order_relaxed), 0u); // drainInline 前零后台偷跑

        workHost.drainInline(2000u);
        const uint32_t afterFirstSlice = executedTasks.load(std::memory_order_relaxed);
        E2E_ASSERT_GE(afterFirstSlice, 1u);
        E2E_ASSERT_LT(afterFirstSlice, 6u);

        // 同步提交异步文本读取，后续多帧 drainInline 排空全部剩余 CPU 任务与 I/O 任务
        StatusOr<std::string> inlineReadRes = NotFoundError("pending");
        bool readDone = false;
        E2E_ASSERT_STATUS_OK(asyncLoader.submitAsyncTextRead(
            "/cache/inline_note.txt", T3Asset_TextCallbackFunctor{&inlineReadRes, &readDone}));

        for (uint32_t safety = 0u; safety < 16u && (executedTasks.load() < 6u || !readDone); ++safety)
        {
            workHost.drainInline(5000u);
        }

        E2E_ASSERT_EQ(executedTasks.load(std::memory_order_relaxed), 6u);
        E2E_ASSERT_TRUE(readDone);
        E2E_ASSERT_STATUS_OK(inlineReadRes);
        E2E_ASSERT_EQ(*inlineReadRes, "SingleThreadFallbackOK");

        asyncLoader.term();
        workHost.term();
        return true;
    }

    // 14. FEAT_46 × FEAT_21: FCT::SAI::VirtualKeyboard 软键盘生命周期、C FFI 宿主注入与 WorkHost 主线程安全派发
    E2E_TEST(Tier3, FEAT_46, Pair_AssetWh_SaiVirtualKeyboardLifecycleAndCFfiMainThreadRelay)
    {
        Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(Host::WorkHost::Mode::SingleThreadInline));

        FCT::SAI::VirtualKeyboard::clearCallbacks();
        (void)FCT::SAI::VirtualKeyboard::hide();
        E2E_ASSERT_FALSE(FCT::SAI::VirtualKeyboard::isShowing());

        std::string latestInput;
        std::string confirmedText;
        std::string completedText;
        uint32_t inputEvents = 0u;
        uint32_t confirmEvents = 0u;
        uint32_t completeEvents = 0u;

        FCT::SAI::VirtualKeyboard::setOnInput(
            T3Asset_KeyboardEventRelayFunctor{&workHost, &latestInput, &inputEvents});
        FCT::SAI::VirtualKeyboard::setOnConfirm(
            T3Asset_KeyboardEventRelayFunctor{&workHost, &confirmedText, &confirmEvents});
        FCT::SAI::VirtualKeyboard::setOnComplete(
            T3Asset_KeyboardEventRelayFunctor{&workHost, &completedText, &completeEvents});

        FCT::SAI::VirtualKeyboardConfig kbCfg;
        kbCfg.defaultValue = "游客";
        kbCfg.maxLength = 16;
        kbCfg.multiple = false;
        kbCfg.confirmHold = false;
        kbCfg.confirmType = FCT::SAI::KeyboardConfirmType::Send;

        E2E_ASSERT_STATUS_OK(FCT::SAI::VirtualKeyboard::show(kbCfg));
        E2E_ASSERT_TRUE(FCT::SAI::VirtualKeyboard::isShowing());
        E2E_ASSERT_EQ(FCT::SAI::VirtualKeyboard::getCurrentConfig().maxLength, 16);
        E2E_ASSERT_EQ(FCT::SAI::VirtualKeyboard::getCurrentConfig().confirmType, FCT::SAI::KeyboardConfirmType::Send);

        // 通过抖音 WebJS / Emscripten 桥接导出的真实 C FFI 符号触发 UTF-8 中文输入与发送确认
        fct_keyboard_dispatch_input("无双剑姬_99");
        fct_keyboard_dispatch_confirm("无双剑姬_99");
        fct_keyboard_dispatch_complete("无双剑姬_99");

        const size_t pumped = workHost.pumpMainThreadTasks();
        E2E_ASSERT_EQ(pumped, 3u);
        E2E_ASSERT_EQ(inputEvents, 1u);
        E2E_ASSERT_EQ(confirmEvents, 1u);
        E2E_ASSERT_EQ(completeEvents, 1u);
        E2E_ASSERT_EQ(latestInput, "无双剑姬_99");
        E2E_ASSERT_EQ(confirmedText, "无双剑姬_99");

        E2E_ASSERT_STATUS_OK(FCT::SAI::VirtualKeyboard::hide());
        E2E_ASSERT_FALSE(FCT::SAI::VirtualKeyboard::isShowing());
        FCT::SAI::VirtualKeyboard::clearCallbacks();

        workHost.term();
        return true;
    }

    // 15. FEAT_70 × FEAT_16: .pgraph PassTextureInput 引脚未连线回退声明自动衔接 AssetManifest 首包默认贴图寻址
    E2E_TEST(Tier3, FEAT_70, Pair_AssetWh_RenderGraphPinSlotFallbackResolvedThroughAssetManifest)
    {
        const std::string pgraphToml = R"(
name = "PinFallbackGraph"
version = "3.0"

[[images]]
name = "ShadowAtlas"
format = "D24_UNORM_S8_UINT"
width = 1024
height = 1024

[[passes]]
name = "ShadowMapPass"
tags = ["Shadow"]
depth_stencil = "ShadowAtlas"

[[passes]]
name = "ForwardLitPass"
tags = ["Opaque", "ForwardPlus"]
textures = [
    { slot = "u_ShadowAtlas", source = "ShadowAtlas", fallback = "textures/default_white.astc" },
    { slot = "u_EnvBrdfLut", source = "", fallback = "textures/default_brdf_lut.astc" }
]
targets = [{ image = "WindowBackbuffer", is_window = true }]
)";

        auto graphOr = Domain::Renderer::RenderGraphAsset::LoadFromToml(pgraphToml);
        E2E_ASSERT_STATUS_OK(graphOr);
        E2E_ASSERT_TRUE(graphOr->isAcyclic());

        // 已连线引脚解析至上游瞬态图像名 ShadowAtlas；未连线引脚自动回退至 fallback 默认资产路径
        auto connectedPinOr = graphOr->resolvePinSlot("ForwardLitPass", "u_ShadowAtlas");
        E2E_ASSERT_STATUS_OK(connectedPinOr);
        E2E_ASSERT_EQ(*connectedPinOr, "ShadowAtlas");

        auto fallbackPinOr = graphOr->resolvePinSlot("ForwardLitPass", "u_EnvBrdfLut");
        E2E_ASSERT_STATUS_OK(fallbackPinOr);
        E2E_ASSERT_EQ(*fallbackPinOr, "textures/default_brdf_lut.astc");

        auto unknownPinOr = graphOr->resolvePinSlot("ForwardLitPass", "u_NonExistentSlot");
        E2E_ASSERT_FALSE(unknownPinOr.ok());

        // 将 fallback 资产路径交由 AssetManifest 解析，精确落入首包只读区 /pkg/textures/default_brdf_lut.astc
        Asset::AssetManifest manifest;
        Asset::ManifestRecord fallbackRec;
        fallbackRec.virtualPath = *fallbackPinOr;
        fallbackRec.sha256Hex = std::string(64u, 'f');
        fallbackRec.fileSize = 4096u;
        fallbackRec.bundleType = Asset::BundleType::FirstPackage;
        manifest.registerRecord(fallbackRec);

        auto resolvedFallbackOr = manifest.resolveAsset(*fallbackPinOr);
        E2E_ASSERT_STATUS_OK(resolvedFallbackOr);
        E2E_ASSERT_EQ(resolvedFallbackOr->location, Asset::AssetResolveResult::Location::FirstPackagePkg);
        E2E_ASSERT_EQ(resolvedFallbackOr->resolvedPath, "/pkg/textures/default_brdf_lut.astc");

        return true;
    }

} // namespace MQEngine::Testing
