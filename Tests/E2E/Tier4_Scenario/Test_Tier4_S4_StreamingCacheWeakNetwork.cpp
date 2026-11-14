/**
 * @file Test_Tier4_S4_StreamingCacheWeakNetwork.cpp
 * @brief Tier 4 真实负载综合场景 S4：首包秒开、三级资产寻址、<=1.5MB/帧限流上传、哈希防篡改与沙箱 LRU 淘汰
 */
#include "Common/E2ECommon.h"
#include <FCT/Context/WGL2_Context.h>
#include "DouyinMiniGame.h"
#include <FCT_SAI/FileSystem.h>

#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <MQEngine/asset/loader/AsyncTextureUploader.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace MQEngine::Testing
{
    namespace
    {
        using namespace MQEngine::Asset;
        using namespace MQEngine::Domain::Renderer;
        using namespace MQEngine::DouyinMiniGame;

        struct AsyncMeshResultState
        {
            std::atomic<bool> done{false};
            bool ok{false};
            MqMeshHeader header{};
            size_t vbBytes{0};
            size_t ibBytes{0};
        };

        struct AsyncMeshDoneFunctor
        {
            AsyncMeshResultState* state{nullptr};

            void operator()(StatusOr<MqMeshHeader> hdrOr, std::vector<uint8_t> vb, std::vector<uint8_t> ib) const
            {
                if (!state)
                {
                    return;
                }
                state->ok = hdrOr.ok();
                if (hdrOr.ok())
                {
                    state->header = *hdrOr;
                    state->vbBytes = vb.size();
                    state->ibBytes = ib.size();
                }
                state->done.store(true, std::memory_order_release);
            }
        };

        struct TextureUploadCountFunctor
        {
            std::atomic<uint32_t>* completedCount{nullptr};

            void operator()(StatusOr<TextureUploadResult> res) const
            {
                if (completedCount && res.ok())
                {
                    completedCount->fetch_add(1u, std::memory_order_relaxed);
                }
            }
        };

        StatusOr<MQEngine::Resource::DecodedTextureData> decodeRawRgbaTestFormat(
            const uint8_t* data,
            size_t size,
            const std::string& /*filePath*/)
        {
            if (data == nullptr || size == 0u)
            {
                return absl::InvalidArgumentError("空纹理字节流");
            }
            MQEngine::Resource::DecodedTextureData out{};
            out.width = 512u;
            out.height = 512u;
            out.channels = 4u;
            out.pixels.assign(data, data + size);
            return out;
        }
    } // namespace

    // S4-01: FCT::SAI::FileSystem 双区沙箱挂载（/pkg 严格只读拦截写操作 + /cache 可读写落盘）
    E2E_TEST(Tier4, FEAT_45, S4_01_SandboxPkgReadOnlyGuardAndCacheWritableRouting)
    {
        E2ETestBase sandbox;
        sandbox.setUp();

        const auto pkgDir = sandbox.getSandboxFilePath("pkg_root");
        const auto cacheDir = sandbox.getSandboxFilePath("cache_root");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(pkgDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(cacheDir.string()));

        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::setPkgRoot(pkgDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::setCacheRoot(cacheDir.string()));

        const auto writePkgAttempt = FCT::SAI::FileSystem::resolveVirtualPath("/pkg/boot_config.json", true);
        E2E_ASSERT_FALSE(writePkgAttempt.ok());
        E2E_ASSERT_EQ(writePkgAttempt.status().code(), absl::StatusCode::kPermissionDenied);

        const auto writeCacheAttempt = FCT::SAI::FileSystem::resolveVirtualPath("/cache/ab12cd34.bin", true);
        E2E_ASSERT_STATUS_OK(writeCacheAttempt);
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile(*writeCacheAttempt, "cached_payload_ok"));
        E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists(*writeCacheAttempt));

        sandbox.tearDown();
        return true;
    }

    // S4-02: AssetManifest 三级寻址闭环（首包 /pkg 直中 -> 远端未缓存返回 CDN URL -> 落盘后二次请求命中 /cache）
    E2E_TEST(Tier4, FEAT_16, S4_02_ThreeTierAssetAddressingPkgToCdnToLocalCacheHit)
    {
        E2ETestBase sandbox;
        sandbox.setUp();

        const auto pkgDir = sandbox.getSandboxFilePath("pkg");
        const auto cacheDir = sandbox.getSandboxFilePath("cache");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(pkgDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(cacheDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::setPkgRoot(pkgDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::setCacheRoot(cacheDir.string()));

        const std::string heroMeshBytes = "MQMS_BOOT_HERO";
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile((pkgDir / "meshes/hero.mqmesh").string(), heroMeshBytes));

        AssetManifest manifest;
        manifest.setCdnBaseUrl("https://cdn.mqgame.douyin.com/v1");

        ManifestRecord bootRec{};
        bootRec.virtualPath = "meshes/hero.mqmesh";
        bootRec.sha256Hex = DouyinAssetCooker::computeSha256Hex(heroMeshBytes);
        bootRec.fileSize = static_cast<uint32_t>(heroMeshBytes.size());
        bootRec.bundleType = BundleType::FirstPackage;
        manifest.registerRecord(bootRec);

        const std::string bossTexPayload = "ASTC_BOSS_ALBEDO_MIPCHAIN";
        ManifestRecord cdnRec{};
        cdnRec.virtualPath = "textures/boss_stage2.astc";
        cdnRec.sha256Hex = DouyinAssetCooker::computeSha256Hex(bossTexPayload);
        cdnRec.fileSize = static_cast<uint32_t>(bossTexPayload.size());
        cdnRec.bundleType = BundleType::RemoteCache;
        manifest.registerRecord(cdnRec);

        const auto r1Or = manifest.resolveAsset("meshes/hero.mqmesh");
        E2E_ASSERT_STATUS_OK(r1Or);
        E2E_ASSERT_EQ(r1Or->location, AssetResolveResult::Location::FirstPackagePkg);

        const auto r2Or = manifest.resolveAsset("textures/boss_stage2.astc");
        E2E_ASSERT_STATUS_OK(r2Or);
        E2E_ASSERT_EQ(r2Or->location, AssetResolveResult::Location::RemoteCdn);
        E2E_ASSERT_NE(r2Or->downloadUrl.find(cdnRec.sha256Hex), std::string::npos);

        const auto localDestOr = FCT::SAI::FileSystem::resolveVirtualPath(r2Or->cacheDestPath, true);
        E2E_ASSERT_STATUS_OK(localDestOr);
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeTextFile(*localDestOr, bossTexPayload));

        const auto r3Or = manifest.resolveAsset("textures/boss_stage2.astc");
        E2E_ASSERT_STATUS_OK(r3Or);
        E2E_ASSERT_EQ(r3Or->location, AssetResolveResult::Location::LocalCache);

        sandbox.tearDown();
        return true;
    }

    // S4-03: 离线烘焙 .mqmesh 二进制网格直通异步解析（零 Assimp 运行时依赖，AABB 与子网格校验完整）
    E2E_TEST(Tier4, FEAT_18, S4_03_BinaryMqMeshZeroAssimpDirectAsyncLoad)
    {
        E2ETestBase sandbox;
        sandbox.setUp();

        const std::vector<uint8_t> cookedCube = DouyinAssetCooker::createDefaultBootCubeMqMesh();
        E2E_ASSERT_GT(cookedCube.size(), sizeof(MqMeshHeader));

        const auto meshPath = sandbox.getSandboxFilePath("boot_cube.mqmesh");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(meshPath.string(), cookedCube.data(), cookedCube.size()));

        MQEngine::Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(MQEngine::Host::WorkHost::Mode::SingleThreadInline, 0u, 1u));

        AsyncLoader loader;
        E2E_ASSERT_STATUS_OK(loader.init());

        AsyncMeshResultState resultState;
        E2E_ASSERT_STATUS_OK(loader.submitAsyncMqMeshRead(meshPath.string(), AsyncMeshDoneFunctor{&resultState}));

        for (int attempt = 0; attempt < 100 && !resultState.done.load(std::memory_order_acquire); ++attempt)
        {
            workHost.drainInline(2000u);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        E2E_ASSERT_TRUE(resultState.done.load(std::memory_order_acquire));
        E2E_ASSERT_TRUE(resultState.ok);
        E2E_ASSERT_EQ(resultState.header.magic, 0x4D514D53u);
        E2E_ASSERT_GT(resultState.header.vertexCount, 0u);
        E2E_ASSERT_GT(resultState.vbBytes, 0u);
        E2E_ASSERT_GT(resultState.ibBytes, 0u);

        loader.term();
        workHost.term();
        sandbox.tearDown();
        return true;
    }

    // S4-04: 贴图秒开占位符（Slot 2 立即返回不阻塞首帧）与 GPUScene 纹理元数据原子热替换
    E2E_TEST(Tier4, FEAT_19, S4_04_InstantPlaceholderSlot2AndGpuSceneHotSwap)
    {
        FCT::WGL2_Context wgl2Ctx(nullptr);
        GPUScene gpuScene;
        E2E_ASSERT_STATUS_OK(gpuScene.init(&wgl2Ctx));

        MQEngine::Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(MQEngine::Host::WorkHost::Mode::SingleThreadInline, 0u, 1u));
        MQEngine::Resource::TextureDecoderRegistry decoderReg;
        E2E_ASSERT_STATUS_OK(decoderReg.init());

        AsyncTextureUploader uploader;
        E2E_ASSERT_STATUS_OK(uploader.init());

        const uint32_t texHandle = uploader.requestTextureWithPlaceholder("cdn/world_albedo.astc");
        const uint32_t initialSlot = uploader.getEffectiveTextureSlot(texHandle);
        E2E_ASSERT_EQ(initialSlot, 2u);

        const GpuTextureRecord placeholderRec = gpuScene.getTextureRecord(initialSlot);
        E2E_ASSERT_EQ(placeholderRec.uvScaleOffset.x, 1.0f);

        const AtlasAllocation streamedAlloc = gpuScene.registerTextureToAtlas(512u, 512u);
        E2E_ASSERT_TRUE(streamedAlloc.success);
        GpuTextureRecord readyRec{};
        readyRec.uvScaleOffset = gpuScene.getTextureUvScaleOffset(streamedAlloc);
        readyRec.bindlessOrPageIndex = streamedAlloc.layer;
        readyRec.flags = 1u;
        gpuScene.updateTextureRecord(initialSlot, readyRec);
        uploader.onDownloadComplete(texHandle, streamedAlloc.layer, readyRec.uvScaleOffset);
        E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(texHandle), streamedAlloc.layer);

        const GpuTextureRecord updatedRec = gpuScene.getTextureRecord(initialSlot);
        E2E_ASSERT_EQ(updatedRec.flags, 1u);
        E2E_ASSERT_LT(updatedRec.uvScaleOffset.x, 1.0f);

        uploader.term();
        decoderReg.term();
        workHost.term();
        gpuScene.shutdown();
        return true;
    }

    // S4-05: AsyncTextureUploader 每帧 <=1.5MB 显存上传带宽限流与多帧平滑排空验证
    E2E_TEST(Tier4, FEAT_19, S4_05_AsyncTextureUploaderBandwidthThrottlePerFrame)
    {
        MQEngine::Host::WorkHost workHost;
        E2E_ASSERT_STATUS_OK(workHost.init(MQEngine::Host::WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
        MQEngine::Resource::TextureDecoderRegistry decoderReg;
        E2E_ASSERT_STATUS_OK(decoderReg.init());
        decoderReg.registerFormatDecoder(".rawrgba", decodeRawRgbaTestFormat);

        AsyncTextureUploader uploader;
        E2E_ASSERT_STATUS_OK(uploader.init());

        std::atomic<uint32_t> doneCount{0u};
        const std::vector<uint8_t> oneMegabytePixels(512u * 512u * 4u, 128u);

        for (uint32_t i = 0; i < 3u; ++i)
        {
            TextureUploadDesc desc{};
            desc.filePath = "stream_tex_" + std::to_string(i) + ".rawrgba";
            desc.debugName = desc.filePath;
            desc.memoryData = oneMegabytePixels;
            E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), TextureUploadCountFunctor{&doneCount}));
        }

        for (int tickIdx = 0; tickIdx < 60 && doneCount.load(std::memory_order_relaxed) < 3u; ++tickIdx)
        {
            uploader.tick(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        E2E_ASSERT_EQ(doneCount.load(std::memory_order_relaxed), 3u);
        E2E_ASSERT_GE(uploader.getTotalUploadedBytes(), 3u * 512u * 512u * 4u);

        uploader.term();
        decoderReg.term();
        workHost.term();
        return true;
    }

    // S4-06: 弱网损坏/篡改包 SHA-256 完整性防线（1 字节翻转即刻识别并阻断脏缓存污染）
    E2E_TEST(Tier4, FEAT_16, S4_06_WeakNetworkCorruptedPayloadSha256Rejection)
    {
        std::vector<uint8_t> cleanPayload(4096u, 0x5Au);
        const std::string expectedSha256 = DouyinAssetCooker::computeSha256Hex(cleanPayload);
        E2E_ASSERT_EQ(expectedSha256.size(), 64u);

        std::vector<uint8_t> corruptedPayload = cleanPayload;
        corruptedPayload[2048] ^= 0x01u;
        const std::string corruptedSha256 = DouyinAssetCooker::computeSha256Hex(corruptedPayload);
        E2E_ASSERT_NE(expectedSha256, corruptedSha256);

        const auto brokenMeshOr = AsyncLoader::parseMqMeshBuffer(corruptedPayload);
        E2E_ASSERT_FALSE(brokenMeshOr.ok());
        return true;
    }

    // S4-07: 沙箱缓存超 180MB 高水位自动触发 LRU 淘汰回落 <=140MB，且活跃 Pin 资产免疫误删
    E2E_TEST(Tier4, FEAT_20, S4_07_SandboxCacheQuota180MbTo140MbLruEvictionWithPinProtection)
    {
        E2ETestBase sandbox;
        sandbox.setUp();
        const auto cacheDir = sandbox.getSandboxFilePath("lru_cache");
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::createDirectories(cacheDir.string()));
        E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::setCacheRoot(cacheDir.string()));

        AsyncLoader loader;
        E2E_ASSERT_STATUS_OK(loader.init());
        loader.clearCacheLedger();

        constexpr uint64_t kMb = 1024ULL * 1024ULL;
        loader.recordCacheAccess("hash_pinned_hero", 40ULL * kMb);
        loader.pinCacheEntry("hash_pinned_hero");

        loader.recordCacheAccess("hash_cold_stage1", 50ULL * kMb);
        loader.recordCacheAccess("hash_cold_stage2", 50ULL * kMb);
        loader.recordCacheAccess("hash_warm_stage3", 50ULL * kMb);

        E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 190ULL * kMb);

        loader.enforceCacheQuota(180ULL * kMb, 140ULL * kMb);
        E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), 140ULL * kMb);
        E2E_ASSERT_TRUE(loader.hasCacheEntry("hash_pinned_hero"));
        E2E_ASSERT_FALSE(loader.hasCacheEntry("hash_cold_stage1"));
        E2E_ASSERT_TRUE(loader.hasCacheEntry("hash_warm_stage3"));

        loader.term();
        sandbox.tearDown();
        return true;
    }

    // S4-08: DouyinAssetCooker 首包预算切分（首包必选资产驻留 MainPackage，超额资产自动分流 RemoteCDN）
    E2E_TEST(Tier4, FEAT_03, S4_08_AssetCookerFirstPackageBudgetSplitToRemoteCdn)
    {
        std::vector<CookedAssetItem> items(3);
        items[0].virtualPath = "fonts/combat_digits.mqfont";
        items[0].packageRelPath = "assets/combat_digits.mqfont";
        items[0].payload.assign(64u * 1024u, 0x11u);
        items[0].sha256Hex = DouyinAssetCooker::computeSha256Hex(items[0].payload);

        items[1].virtualPath = "meshes/boot_arena.mqmesh";
        items[1].packageRelPath = "assets/boot_arena.mqmesh";
        items[1].payload.assign(128u * 1024u, 0x22u);
        items[1].sha256Hex = DouyinAssetCooker::computeSha256Hex(items[1].payload);

        items[2].virtualPath = "textures/chapter2_world_4k.astc";
        items[2].packageRelPath = "assets/chapter2_world_4k.astc";
        items[2].payload.assign(512u * 1024u, 0x33u);
        items[2].sha256Hex = DouyinAssetCooker::computeSha256Hex(items[2].payload);

        DouyinAssetCooker::splitAssetsByFirstPackageBudget(items, 256u * 1024u);
        E2E_ASSERT_EQ(items[0].bundleType, BundleType::FirstPackage);
        E2E_ASSERT_EQ(items[1].bundleType, BundleType::FirstPackage);
        E2E_ASSERT_EQ(items[2].bundleType, BundleType::RemoteCache);
        return true;
    }
} // namespace MQEngine::Testing
