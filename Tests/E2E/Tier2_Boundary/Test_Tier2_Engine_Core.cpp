#include "Common/E2ECommon.h"
#include <MQEngine/asset/loader/AssetManifest.h>
#include <MQEngine/asset/loader/AsyncLoader.h>
#include <MQEngine/asset/loader/AsyncTextureUploader.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/hid/InputAction.h>
#include <MQEngine/domain/hid/InputMappingContext.h>
#include <MQEngine/domain/hid/InputModifier.h>
#include <MQEngine/domain/hid/InputTrigger.h>
#include <MQEngine/domain/renderer/DrawBucket.h>
#include <MQEngine/domain/renderer/data/GPUScene.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <Font/Composites/FontRuntimeComposite/BakedFont.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include <Font/Composites/FontRuntimeComposite/FontTypes.h>
#include <Font/Composites/FontRuntimeComposite/GlyphAtlasManager.h>
#include <Font/Composites/FontRuntimeComposite/InGameTextComponent.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <FCT_SAI/FileSystem.h>
#include <FCT_SAI/SystemFontProvider.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace MQEngine::Testing {

namespace {

using ::MQEngine::Asset::AssetManifest;
using ::MQEngine::Asset::AssetResolveResult;
using ::MQEngine::Asset::AsyncLoader;
using ::MQEngine::Asset::AsyncTextureUploader;
using ::MQEngine::Asset::BundleType;
using ::MQEngine::Asset::ManifestRecord;
using ::MQEngine::Asset::MqMeshHeader;
using ::MQEngine::Asset::StandardMeshVertex;
using ::MQEngine::Asset::SubMeshRange;
using ::MQEngine::Asset::TextureUploadDesc;
using ::MQEngine::Asset::TextureUploadResult;
using ::MQEngine::Domain::HID::ActionBindingMapping;
using ::MQEngine::Domain::HID::EInputAccumulationPolicy;
using ::MQEngine::Domain::HID::EInputActionValueType;
using ::MQEngine::Domain::HID::ESwizzleAxisOrder;
using ::MQEngine::Domain::HID::ETriggerState;
using ::MQEngine::Domain::HID::HIDSubsystem;
using ::MQEngine::Domain::HID::InputAction;
using ::MQEngine::Domain::HID::InputActionHandle;
using ::MQEngine::Domain::HID::InputMappingContext;
using ::MQEngine::Domain::HID::InputModifierDeadZone;
using ::MQEngine::Domain::HID::InputModifierNegate;
using ::MQEngine::Domain::HID::InputModifierNormalize2D;
using ::MQEngine::Domain::HID::InputModifierScalar;
using ::MQEngine::Domain::HID::InputModifierSwizzleAxis;
using ::MQEngine::Domain::HID::InputTriggerChordedAction;
using ::MQEngine::Domain::HID::InputTriggerHold;
using ::MQEngine::Domain::HID::InputTriggerHoldAndRelease;
using ::MQEngine::Domain::HID::InputTriggerTap;
using ::MQEngine::Domain::HID::InputValue;
using ::MQEngine::Domain::Renderer::AtlasShelfPacker;
using ::MQEngine::Domain::Renderer::DrawBucket;
using ::MQEngine::Domain::Renderer::GpuMaterialRecord;
using ::MQEngine::Domain::Renderer::GpuTextureRecord;
using ::MQEngine::Domain::Renderer::GPUScene;
namespace InputTags = ::MQEngine::Domain::Renderer::InputTags;
using ::MQEngine::Host::WorkHost;
using ::MQEngine::Resource::DecodedTextureData;
using ::MQEngine::Resource::GpuResourceState;
using ::MQEngine::Resource::GpuResourceTable;
using ::MQEngine::Resource::GpuResourceType;
using ::MQEngine::Resource::TextureDecoderRegistry;
using ::MQEngine::Resource::Font::BakedFont;
using ::MQEngine::Resource::Font::CachedUiGlyph;
using ::MQEngine::Resource::Font::FontBackendMode;
using ::MQEngine::Resource::Font::FontSystem;
using ::MQEngine::Resource::Font::FontTrack;
using ::MQEngine::Resource::Font::FontTrackClassifier;
using ::MQEngine::Resource::Font::GlyphAtlasManager;
using ::MQEngine::Resource::Font::GlyphMetric;
using ::MQEngine::Resource::Font::GpuInGameTextQuadInstance;
using ::MQEngine::Resource::Font::InGameTextComponent;
using ::MQEngine::Resource::Font::MqFontBinaryHeader;
using ::MQEngine::Resource::Font::TextureFormat;

std::atomic<uint64_t> g_t2cSandboxCounter{1};

struct T2C_ScopedSandboxRoots {
    std::string baseDir;
    std::string pkgDir;
    std::string cacheDir;
    std::string prevPkg;
    std::string prevCache;

    explicit T2C_ScopedSandboxRoots(std::string_view tag) {
        prevPkg = std::string(FCT::SAI::FileSystem::getPkgRoot());
        prevCache = std::string(FCT::SAI::FileSystem::getCacheRoot());
        const uint64_t id = g_t2cSandboxCounter.fetch_add(1, std::memory_order_relaxed);
        const auto rootPath = std::filesystem::temp_directory_path() /
                              ("mqe_t2c_" + std::string(tag) + "_" + std::to_string(id));
        baseDir = rootPath.generic_string();
        pkgDir = baseDir + "/pkg";
        cacheDir = baseDir + "/cache";
        (void)FCT::SAI::FileSystem::createDirectories(pkgDir);
        (void)FCT::SAI::FileSystem::createDirectories(cacheDir);
        (void)FCT::SAI::FileSystem::setPkgRoot(pkgDir);
        (void)FCT::SAI::FileSystem::setCacheRoot(cacheDir);
    }

    ~T2C_ScopedSandboxRoots() {
        if (!prevPkg.empty()) {
            (void)FCT::SAI::FileSystem::setPkgRoot(prevPkg);
        }
        if (!prevCache.empty()) {
            (void)FCT::SAI::FileSystem::setCacheRoot(prevCache);
        }
        if (!baseDir.empty()) {
            (void)FCT::SAI::FileSystem::removeAll(baseDir);
        }
    }
};

std::vector<uint8_t> T2C_MakeRawRgbaPayload(uint32_t w, uint32_t h, uint8_t fillByte) {
    const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
    std::vector<uint8_t> out(8u + pixelBytes, fillByte);
    std::memcpy(out.data(), &w, 4u);
    std::memcpy(out.data() + 4u, &h, 4u);
    return out;
}

StatusOr<DecodedTextureData> T2C_DecodeRawRgba(
    const uint8_t* data,
    size_t size,
    const std::string& filePath) {
    (void)filePath;
    if (!data || size < 8u) {
        return InvalidArgumentError("RawRGBA 头部不足");
    }
    uint32_t w = 0;
    uint32_t h = 0;
    std::memcpy(&w, data, 4u);
    std::memcpy(&h, data + 4u, 4u);
    const size_t expected = static_cast<size_t>(w) * h * 4u;
    if (size < 8u + expected) {
        return InvalidArgumentError("RawRGBA 载荷截断");
    }
    DecodedTextureData out;
    out.width = w;
    out.height = h;
    out.channels = 4u;
    out.mipLevels = 1u;
    out.formatName = ".rawrgba";
    out.pixels.assign(data + 8u, data + 8u + expected);
    return out;
}

StatusOr<DecodedTextureData> T2C_DecodeZeroByteTex(
    const uint8_t* data,
    size_t size,
    const std::string& filePath) {
    (void)data;
    (void)size;
    (void)filePath;
    DecodedTextureData out;
    out.width = 0u;
    out.height = 0u;
    out.channels = 4u;
    out.mipLevels = 1u;
    out.formatName = ".zerotex";
    return out;
}

struct T2C_DedupeWaiterState {
    std::atomic<uint32_t> okCount{0};
    std::atomic<uint64_t> byteSum{0};
};

struct T2C_DedupeWaiterFn {
    T2C_DedupeWaiterState* state{nullptr};
    void operator()(StatusOr<std::vector<uint8_t>> res) const {
        if (state && res.ok()) {
            state->okCount.fetch_add(1u, std::memory_order_relaxed);
            state->byteSum.fetch_add(res->size(), std::memory_order_relaxed);
        }
    }
};

struct T2C_UploadCapture {
    std::atomic<bool> invoked{false};
    std::atomic<bool> ok{false};
    TextureUploadResult result{};
};

struct T2C_UploadCaptureFn {
    T2C_UploadCapture* out{nullptr};
    void operator()(StatusOr<TextureUploadResult> res) const {
        if (!out) {
            return;
        }
        out->ok.store(res.ok(), std::memory_order_release);
        if (res.ok()) {
            out->result = *res;
        }
        out->invoked.store(true, std::memory_order_release);
    }
};

struct T2C_AtomicSwapState {
    GpuResourceTable* gpuTable{nullptr};
    GPUScene* gpuScene{nullptr};
    uint32_t textureSlot{2u};
    uint32_t materialSlot{0u};
    FCT::Vec4 targetUv{1.0f, 1.0f, 0.0f, 0.0f};
    uint32_t finalSlot{2u};
    bool done{false};
};

struct T2C_AtomicSwapCallbackFn {
    T2C_AtomicSwapState* state{nullptr};
    void operator()(StatusOr<TextureUploadResult> res) const {
        if (!state || !res.ok()) {
            return;
        }
        uint32_t slot = 4u;
        if (state->gpuTable && res->textureHandle) {
            state->gpuTable->registerResource(
                res->textureHandle,
                GpuResourceType::Texture,
                res->totalBytes,
                res->debugName,
                GpuResourceState::Ready);
            slot = state->gpuTable->allocateBindlessSlot(res->textureHandle);
        }
        state->finalSlot = slot;
        if (state->gpuScene) {
            GpuTextureRecord texRec{};
            texRec.uvScaleOffset = state->targetUv;
            texRec.bindlessOrPageIndex = slot;
            texRec.flags = 0u;
            state->gpuScene->updateTextureRecord(state->textureSlot, texRec);

            GpuMaterialRecord mat{};
            mat.albedoTexIndex = slot;
            (void)state->gpuScene->uploadMaterialData(
                std::span<const GpuMaterialRecord>(&mat, 1u), state->materialSlot);
        }
        state->done = true;
    }
};

struct T2C_ConcurrentUploadProducerFn {
    AsyncTextureUploader* uploader{nullptr};
    uint32_t threadIndex{0};
    uint32_t countPerThread{16u};
    std::atomic<uint32_t>* submitOkCount{nullptr};

    void operator()() const {
        if (!uploader) {
            return;
        }
        for (uint32_t i = 0; i < countPerThread; ++i) {
            TextureUploadDesc desc{};
            desc.filePath = "textures/mt_" + std::to_string(threadIndex) + "_" + std::to_string(i) + ".rawrgba";
            desc.memoryData = T2C_MakeRawRgbaPayload(8u, 8u, static_cast<uint8_t>(threadIndex + i));
            Status st = uploader->uploadTextureAsync(std::move(desc), nullptr);
            if (st.ok() && submitOkCount) {
                submitOkCount->fetch_add(1u, std::memory_order_relaxed);
            }
        }
    }
};

struct T2C_AtomicIncFn {
    std::atomic<uint32_t>* counter{nullptr};
    void operator()() const {
        if (counter) {
            counter->fetch_add(1u, std::memory_order_relaxed);
        }
    }
};

struct T2C_ReadErrorCapture {
    bool called{false};
    bool ok{true};
    absl::StatusCode code{absl::StatusCode::kOk};
};

struct T2C_StringReadCaptureFn {
    T2C_ReadErrorCapture* out{nullptr};
    void operator()(StatusOr<std::string> res) const {
        if (out) {
            out->called = true;
            out->ok = res.ok();
            out->code = res.status().code();
        }
    }
};

struct T2C_BinaryReadCaptureFn {
    T2C_ReadErrorCapture* out{nullptr};
    void operator()(StatusOr<std::vector<uint8_t>> res) const {
        if (out) {
            out->called = true;
            out->ok = res.ok();
            out->code = res.status().code();
        }
    }
};

struct T2C_ReentrantState {
    WorkHost* host{nullptr};
    uint32_t maxDepth{64u};
    std::atomic<uint32_t> executed{0};
};

struct T2C_ReentrantTaskFn {
    T2C_ReentrantState* state{nullptr};
    uint32_t depth{0};
    void operator()() const {
        if (!state) {
            return;
        }
        state->executed.fetch_add(1u, std::memory_order_relaxed);
        if (depth < state->maxDepth && state->host) {
            (void)state->host->postTask(T2C_ReentrantTaskFn{state, depth + 1u});
        }
    }
};

struct T2C_WorkHostContentionProducerFn {
    WorkHost* host{nullptr};
    uint32_t cpuPerThread{100u};
    uint32_t ioPerThread{25u};
    std::atomic<uint32_t>* cpuDone{nullptr};
    std::atomic<uint32_t>* ioDone{nullptr};

    void operator()() const {
        if (!host) {
            return;
        }
        for (uint32_t i = 0; i < cpuPerThread; ++i) {
            (void)host->postTask(T2C_AtomicIncFn{cpuDone});
        }
        for (uint32_t i = 0; i < ioPerThread; ++i) {
            (void)host->postIoTask(T2C_AtomicIncFn{ioDone});
        }
    }
};

struct T2C_FifoAppendFn {
    std::vector<uint32_t>* orderLog{nullptr};
    uint32_t value{0};
    void operator()() const {
        if (orderLog) {
            orderLog->push_back(value);
        }
    }
};

struct T2C_DualTrackBucketWorkerFn {
    DrawBucket* outTrackA{nullptr};
    DrawBucket* outTrackB{nullptr};
    std::atomic<uint32_t>* validCount{nullptr};

    void operator()() const {
        const FontTrack tA = FontTrackClassifier::classifyTrack("-8888! 暴击 闪避");
        const FontTrack tB = FontTrackClassifier::classifyTrack("世界聊天：公会集结");
        if (tA == FontTrack::InGameBaked && outTrackA) {
            *outTrackA = DrawBucket(0u, InputTags::Text | InputTags::InGameBaked, 256u);
        }
        if (tB == FontTrack::UiRuntime && outTrackB) {
            *outTrackB = DrawBucket(0u, InputTags::UI | InputTags::RmlUi, 64u);
        }
        if (tA == FontTrack::InGameBaked && tB == FontTrack::UiRuntime && validCount) {
            validCount->fetch_add(1u, std::memory_order_relaxed);
        }
    }
};

struct T2C_ActionFireCounter {
    uint32_t count{0};
    InputValue lastVal{};
};

struct T2C_ActionFireFn {
    T2C_ActionFireCounter* counter{nullptr};
    void operator()(const InputValue& v, ETriggerState) const {
        if (counter) {
            ++counter->count;
            counter->lastVal = v;
        }
    }
};

} // namespace

// ============================================================================
// [FEAT-16] 三级资产寻址边界与异常加固 (AssetManifest / AsyncLoader)
// ============================================================================

E2E_TEST(Tier2, FEAT_16, AssetManifest_MassiveRecordsLookupStress) {
    AssetManifest srcManifest;
    const std::string sampleHash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    for (uint32_t i = 0; i < 10000u; ++i) {
        ManifestRecord rec{};
        rec.virtualPath = "assets/tex_" + std::to_string(i) + ".ktx2";
        rec.sha256Hex = sampleHash;
        rec.fileSize = 1024u + i;
        rec.assetFormat = 2u;
        rec.bundleType = (i % 2u == 0u) ? BundleType::FirstPackage : BundleType::RemoteCache;
        srcManifest.registerRecord(std::move(rec));
    }

    const std::vector<uint8_t> serialized = srcManifest.saveToBuffer();
    AssetManifest dstManifest;
    E2E_ASSERT_STATUS_OK(dstManifest.loadFromBuffer(serialized));
    E2E_ASSERT_EQ(dstManifest.getRecordCount(), 10000u);
    E2E_ASSERT_TRUE(dstManifest.findRecord("assets/tex_0.ktx2") != nullptr);
    E2E_ASSERT_EQ(dstManifest.findRecord("assets/tex_9999.ktx2")->fileSize, 11023u);
    E2E_ASSERT_TRUE(dstManifest.findRecord("assets/tex_10000.ktx2") == nullptr);
    return true;
}

E2E_TEST(Tier2, FEAT_16, AssetManifest_EmptyBufferRejected) {
    AssetManifest manifest;
    const Status emptyStatus = manifest.loadFromBuffer({});
    E2E_ASSERT_FALSE(emptyStatus.ok());
    E2E_ASSERT_EQ(emptyStatus.code(), absl::StatusCode::kInvalidArgument);

    const uint8_t shortBytes[3] = {'M', 'Q', 'M'};
    E2E_ASSERT_FALSE(manifest.loadFromBuffer(shortBytes).ok());

    const uint8_t truncatedBinary[6] = {'M', 'Q', 'M', 'B', 0x01u, 0x00u};
    E2E_ASSERT_FALSE(manifest.loadFromBuffer(truncatedBinary).ok());
    return true;
}

E2E_TEST(Tier2, FEAT_16, ThreeTierAddressing_PathTraversalRejected) {
    AssetManifest manifest;
    E2E_ASSERT_EQ(manifest.resolveAsset("").status().code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(manifest.resolveAsset("../secret_config.toml").status().code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(manifest.resolveAsset("textures/../../etc/passwd").status().code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(manifest.resolveAsset("textures\\win_backslash.ktx2").status().code(), absl::StatusCode::kInvalidArgument);

    constexpr std::string_view badManifestLine =
        "../escape.bin|ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad|64|1\n";
    const Status lineStatus = manifest.loadFromBuffer(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(badManifestLine.data()), badManifestLine.size()));
    E2E_ASSERT_FALSE(lineStatus.ok());
    return true;
}

E2E_TEST(Tier2, FEAT_16, ThreeTierAddressing_CorruptedPayloadHashMismatch) {
    std::vector<uint8_t> payload(4096u, 0x5Au);
    const std::string expectedHex = AssetManifest::computeSha256Hex(payload);
    E2E_ASSERT_EQ(expectedHex.size(), 64u);

    std::string upperHex = expectedHex;
    for (char& ch : upperHex) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    E2E_ASSERT_TRUE(AssetManifest::verifySha256(payload, upperHex));

    // 单比特翻转必须被 FIPS 180-4 SHA-256 校验拦截
    payload[2047u] ^= 0x01u;
    E2E_ASSERT_FALSE(AssetManifest::verifySha256(payload, expectedHex));
    E2E_ASSERT_FALSE(AssetManifest::verifySha256(payload, "invalid_short_hash"));
    return true;
}

E2E_TEST(Tier2, FEAT_16, ThreeTierAddressing_ConcurrentDeduplication) {
    T2C_ScopedSandboxRoots sandbox("feat16_dedupe");
    const std::vector<uint8_t> fileData(512u, 0x3Cu);
    const std::string localAssetPath = sandbox.baseDir + "/shared_bundle.bin";
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(localAssetPath, fileData.data(), fileData.size()));

    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    T2C_DedupeWaiterState state{};
    const std::string url = "file://" + localAssetPath;
    for (uint32_t i = 0; i < 8u; ++i) {
        E2E_ASSERT_STATUS_OK(loader.submitAsyncNetworkFetch(url, T2C_DedupeWaiterFn{&state}));
    }

    // 8 路同 URL 请求合并为单一在途下载任务与单一底层读取
    E2E_ASSERT_EQ(loader.getInflightDownloadCount(), 1u);
    E2E_ASSERT_EQ(loader.getActiveIoCount(), 1u);

    workHost.drainInline(100000u);
    E2E_ASSERT_EQ(state.okCount.load(std::memory_order_relaxed), 8u);
    E2E_ASSERT_EQ(state.byteSum.load(std::memory_order_relaxed), 8ULL * 512ULL);
    E2E_ASSERT_EQ(loader.getTotalBytesRead(), 512ULL);
    E2E_ASSERT_EQ(loader.getInflightDownloadCount(), 0u);

    loader.term();
    workHost.term();
    return true;
}

// ============================================================================
// [FEAT-17] 纹理占位秒开与热替换边界验证
// ============================================================================

E2E_TEST(Tier2, FEAT_17, Placeholder_HighVolumeHandleAllocationStress) {
    GpuResourceTable gpuTable;
    std::unordered_set<uint32_t> uniqueSlots;

    for (uintptr_t i = 1u; i <= 2000u; ++i) {
        const void* handle = reinterpret_cast<const void*>(i * 16u);
        gpuTable.registerResource(handle, GpuResourceType::Texture, 4096ULL, "StressTex", GpuResourceState::Ready);
        const uint32_t slot = gpuTable.allocateBindlessSlot(handle);
        uniqueSlots.insert(slot);
    }

    E2E_ASSERT_EQ(gpuTable.activeCount(), 2000u);
    E2E_ASSERT_EQ(uniqueSlots.size(), 2000u);

    for (uintptr_t i = 1u; i <= 500u; ++i) {
        const void* handle = reinterpret_cast<const void*>(i * 16u);
        gpuTable.markResourceReleased(handle);
        gpuTable.finalizeUnregister(handle);
    }
    E2E_ASSERT_EQ(gpuTable.activeCount(), 1500u);
    return true;
}

E2E_TEST(Tier2, FEAT_17, Placeholder_EmptyUrlRejected) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    TextureUploadDesc emptyDesc{};
    const Status uploadStatus = uploader.uploadTextureAsync(std::move(emptyDesc), nullptr);
    E2E_ASSERT_FALSE(uploadStatus.ok());
    E2E_ASSERT_EQ(uploadStatus.code(), absl::StatusCode::kInvalidArgument);

    const Status netStatus = loader.submitAsyncNetworkFetch("", nullptr);
    E2E_ASSERT_FALSE(netStatus.ok());
    E2E_ASSERT_EQ(netStatus.code(), absl::StatusCode::kInvalidArgument);

    loader.term();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_17, Placeholder_NonExistentHandleReturnsZero) {
    GpuResourceTable gpuTable;
    const void* ghostPtr = reinterpret_cast<const void*>(0xDEAD0001ULL);

    E2E_ASSERT_FALSE(gpuTable.contains(ghostPtr));
    E2E_ASSERT_TRUE(gpuTable.getRecord(ghostPtr) == nullptr);
    E2E_ASSERT_EQ(gpuTable.getBindlessSlot(ghostPtr), UINT32_MAX);
    E2E_ASSERT_TRUE(gpuTable.resolveHandle(ghostPtr, GpuResourceType::Texture) == nullptr);
    return true;
}

E2E_TEST(Tier2, FEAT_17, Placeholder_CdnDownloadFailureFallbackSlotTwo) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());

    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    T2C_UploadCapture failCap{};
    const uint32_t texHandle = uploader.requestTextureWithPlaceholder(
        "textures/nonexistent_cdn_404.ktx2", T2C_UploadCaptureFn{&failCap});
    const uint32_t fallbackSlot = uploader.getEffectiveTextureSlot(texHandle);
    E2E_ASSERT_EQ(fallbackSlot, 2u);

    GpuMaterialRecord mat{};
    mat.albedoTexIndex = fallbackSlot;
    E2E_ASSERT_STATUS_OK(scene.uploadMaterialData(std::span<const GpuMaterialRecord>(&mat, 1u), 0u));

    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    // 远端拉取失败后纹理槽位继续稳妥保持内置 Slot 2 占位贴图状态
    E2E_ASSERT_TRUE(failCap.invoked.load(std::memory_order_acquire));
    E2E_ASSERT_FALSE(failCap.ok.load(std::memory_order_acquire));
    E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(texHandle), 2u);
    const GpuTextureRecord recAfterFail = scene.getTextureRecord(fallbackSlot);
    E2E_ASSERT_EQ(recAfterFail.bindlessOrPageIndex, 2u);
    E2E_ASSERT_EQ(recAfterFail.flags, 1u);

    scene.shutdown();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_17, HotReplacement_AtomicUvRefreshOnCompletion) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T2C_DecodeRawRgba);

    AtlasShelfPacker packer(1024u, 1024u, 1u);
    (void)packer.allocate(512u, 256u);
    const auto tile = packer.allocate(256u, 256u);
    E2E_ASSERT_TRUE(tile.success);
    E2E_ASSERT_EQ(tile.x, 512u);

    GpuResourceTable gpuTable;
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));
    GpuMaterialRecord initMat{};
    initMat.albedoTexIndex = 2u;
    E2E_ASSERT_STATUS_OK(
        scene.uploadMaterialData(std::span<const GpuMaterialRecord>(&initMat, 1u), 0u));

    T2C_AtomicSwapState swapState{};
    swapState.gpuTable = &gpuTable;
    swapState.gpuScene = &scene;
    swapState.textureSlot = 2u;
    swapState.materialSlot = 0u;
    swapState.targetUv = FCT::Vec4{0.25f, 0.25f, 0.50f, 0.0f};

    TextureUploadDesc desc{};
    desc.filePath = "textures/atomic_uv.rawrgba";
    desc.memoryData = T2C_MakeRawRgbaPayload(16u, 16u, 0x99u);
    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), T2C_AtomicSwapCallbackFn{&swapState}));

    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_TRUE(swapState.done);
    const GpuTextureRecord updated = scene.getTextureRecord(2u);
    E2E_ASSERT_EQ(updated.bindlessOrPageIndex, swapState.finalSlot);
    E2E_ASSERT_EQ(updated.flags, 0u);
    E2E_ASSERT_TRUE(std::abs(updated.uvScaleOffset.z - 0.50f) < 1e-5f);

    scene.shutdown();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

// ============================================================================
// [FEAT-18] .mqmesh 溢出防护与畸形流拦截 (AsyncLoader)
// ============================================================================

E2E_TEST(Tier2, FEAT_18, MqMesh_GiantMeshStrideCalculationOverflowSafety) {
    MqMeshHeader giantHdr{};
    giantHdr.magic = 0x4D514D53u;
    giantHdr.version = 1u;
    giantHdr.vertexCount = 0x40000000u;
    giantHdr.indexCount = 3u;
    giantHdr.vertexStride = 48u;

    std::vector<uint8_t> craftBuf(sizeof(MqMeshHeader) + 64u, 0u);
    std::memcpy(craftBuf.data(), &giantHdr, sizeof(MqMeshHeader));

    auto parsedOr = AsyncLoader::parseMqMeshBuffer(craftBuf);
    E2E_ASSERT_FALSE(parsedOr.ok());
    E2E_ASSERT_EQ(parsedOr.status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_18, MqMesh_EmptyMeshZeroVerticesParsedSuccessfully) {
    MqMeshHeader emptyHdr{};
    emptyHdr.magic = 0x4D514D53u;
    emptyHdr.version = 1u;
    emptyHdr.vertexCount = 0u;
    emptyHdr.indexCount = 0u;
    emptyHdr.subMeshCount = 0u;
    emptyHdr.vertexStride = sizeof(StandardMeshVertex);

    auto serializedOr = AsyncLoader::serializeMqMesh(emptyHdr, {}, {});
    E2E_ASSERT_STATUS_OK(serializedOr);

    auto parsedOr = AsyncLoader::parseMqMeshBuffer(*serializedOr);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->header.vertexCount, 0u);
    E2E_ASSERT_EQ(parsedOr->header.indexCount, 0u);
    E2E_ASSERT_TRUE(parsedOr->vertexBytes.empty());
    E2E_ASSERT_TRUE(parsedOr->indexBytes.empty());
    return true;
}

E2E_TEST(Tier2, FEAT_18, MqMesh_InvalidMagicHeaderRejected) {
    MqMeshHeader badMagic{};
    badMagic.magic = 0xDEADBEEFu;
    badMagic.version = 1u;
    std::vector<uint8_t> buf(sizeof(MqMeshHeader), 0u);
    std::memcpy(buf.data(), &badMagic, sizeof(MqMeshHeader));
    E2E_ASSERT_FALSE(AsyncLoader::parseMqMeshBuffer(buf).ok());
    E2E_ASSERT_FALSE(AsyncLoader::serializeMqMesh(badMagic, {}, {}).ok());

    MqMeshHeader badVer{};
    badVer.magic = 0x4D514D53u;
    badVer.version = 99u;
    std::memcpy(buf.data(), &badVer, sizeof(MqMeshHeader));
    E2E_ASSERT_FALSE(AsyncLoader::parseMqMeshBuffer(buf).ok());
    return true;
}

E2E_TEST(Tier2, FEAT_18, MqMesh_TruncatedStreamRejected) {
    const uint8_t tinyHeader[16] = {'M', 'Q', 'M', 'S'};
    E2E_ASSERT_FALSE(AsyncLoader::parseMqMeshBuffer(tinyHeader).ok());

    MqMeshHeader hdr{};
    hdr.magic = 0x4D514D53u;
    hdr.version = 1u;
    hdr.vertexCount = 10u;
    hdr.indexCount = 6u;
    hdr.vertexStride = 48u;

    // 声称 480B 顶点但实际仅给 64B 尾部载荷
    std::vector<uint8_t> truncatedVb(sizeof(MqMeshHeader) + 64u, 0u);
    std::memcpy(truncatedVb.data(), &hdr, sizeof(MqMeshHeader));
    E2E_ASSERT_FALSE(AsyncLoader::parseMqMeshBuffer(truncatedVb).ok());

    // 顶点完整但索引区截断 (6 个 uint16 至少需 12B，仅提供 4B)
    hdr.vertexCount = 2u;
    std::vector<uint8_t> truncatedIb(sizeof(MqMeshHeader) + 96u + 4u, 0u);
    std::memcpy(truncatedIb.data(), &hdr, sizeof(MqMeshHeader));
    E2E_ASSERT_FALSE(AsyncLoader::parseMqMeshBuffer(truncatedIb).ok());
    return true;
}

E2E_TEST(Tier2, FEAT_18, MqMesh_SubmeshIndexSliceOverflowRejected) {
    MqMeshHeader hdr{};
    hdr.magic = 0x4D514D53u;
    hdr.version = 1u;
    hdr.vertexCount = 4u;
    hdr.indexCount = 6u;

    const SubMeshRange outOfBounds[1] = {SubMeshRange{4u, 5u, 0u, 0u}};
    const Status st1 = AsyncLoader::validateSubmeshRanges(hdr, outOfBounds);
    E2E_ASSERT_FALSE(st1.ok());
    E2E_ASSERT_EQ(st1.code(), absl::StatusCode::kOutOfRange);

    const SubMeshRange u32Wrap[1] = {SubMeshRange{0xFFFFFFF0u, 32u, 0u, 0u}};
    const Status st2 = AsyncLoader::validateSubmeshRanges(hdr, u32Wrap);
    E2E_ASSERT_FALSE(st2.ok());
    E2E_ASSERT_EQ(st2.code(), absl::StatusCode::kOutOfRange);
    return true;
}

// ============================================================================
// [FEAT-19] 异步纹理上传超预算、零字节与多线程并发压测
// ============================================================================

E2E_TEST(Tier2, FEAT_19, TextureUploader_MassiveSingleTextureExceedingBudgetHandled) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T2C_DecodeRawRgba);

    // 单张 2.0MB 贴图超过 1.5MB 帧预算，首张破例放行避免永久饥饿死锁
    TextureUploadDesc giantDesc{};
    giantDesc.filePath = "textures/giant_2mb.rawrgba";
    giantDesc.memoryData = T2C_MakeRawRgbaPayload(1024u, 512u, 0xA5u);

    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(giantDesc), nullptr));
    workHost.drainInline(200000u);
    uploader.tick(0.016f);

    E2E_ASSERT_EQ(uploader.getCompletedCount(), 1u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 2097152ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_19, TextureUploader_ZeroByteTaskCompletesInstantly) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".zerotex", T2C_DecodeZeroByteTex);

    T2C_UploadCapture cap{};
    TextureUploadDesc zeroDesc{};
    zeroDesc.filePath = "textures/empty_mip.zerotex";
    zeroDesc.memoryData = {0x00u, 0x01u, 0x02u, 0x03u};

    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(zeroDesc), T2C_UploadCaptureFn{&cap}));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_TRUE(cap.invoked.load(std::memory_order_acquire));
    E2E_ASSERT_TRUE(cap.ok.load(std::memory_order_acquire));
    E2E_ASSERT_EQ(cap.result.totalBytes, 0u);
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 1u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 0ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_19, TextureUploader_NegativeBudgetClampedToZero) {
    AsyncTextureUploader uninitUploader;
    TextureUploadDesc preDesc{};
    preDesc.filePath = "textures/uninit.rawrgba";
    preDesc.memoryData = T2C_MakeRawRgbaPayload(4u, 4u, 0x11u);
    E2E_ASSERT_EQ(uninitUploader.uploadTextureAsync(std::move(preDesc), nullptr).code(),
                  absl::StatusCode::kFailedPrecondition);

    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T2C_DecodeRawRgba);

    // 负数或零帧间隔调用 tick 不产生无符号回绕异常
    uploader.tick(-1.0f);
    uploader.tick(0.0f);

    TextureUploadDesc desc{};
    desc.filePath = "textures/neg_dt_safe.rawrgba";
    desc.memoryData = T2C_MakeRawRgbaPayload(8u, 8u, 0x22u);
    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), nullptr));
    workHost.drainInline(100000u);
    uploader.tick(-0.016f);

    E2E_ASSERT_EQ(uploader.getCompletedCount(), 1u);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_19, TextureUploader_HeavyQueueOverloadDrainedAccurately) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T2C_DecodeRawRgba);

    for (uint32_t i = 0; i < 64u; ++i) {
        TextureUploadDesc desc{};
        desc.filePath = "textures/overload_" + std::to_string(i) + ".rawrgba";
        desc.memoryData = T2C_MakeRawRgbaPayload(16u, 16u, static_cast<uint8_t>(i));
        E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), nullptr));
    }

    workHost.drainInline(300000u);
    uploader.tick(0.016f);

    E2E_ASSERT_EQ(uploader.getPendingCount(), 0u);
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 64u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 64ULL * 1024ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier2, FEAT_19, TextureUploader_ConcurrentQueueingSafety) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T2C_DecodeRawRgba);

    std::atomic<uint32_t> submitOk{0};
    std::vector<std::thread> producers;
    producers.reserve(4u);
    for (uint32_t t = 0; t < 4u; ++t) {
        producers.emplace_back(T2C_ConcurrentUploadProducerFn{&uploader, t, 16u, &submitOk});
    }
    for (auto& th : producers) {
        th.join();
    }
    E2E_ASSERT_EQ(submitOk.load(std::memory_order_relaxed), 64u);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (uploader.getCompletedCount() < 64u && std::chrono::steady_clock::now() < deadline) {
        uploader.tick(0.016f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    E2E_ASSERT_EQ(uploader.getCompletedCount(), 64u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 64ULL * 256ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

// ============================================================================
// [FEAT-20] 沙箱磁盘配额精确水位边界与全钉住安全终止 (AsyncLoader)
// ============================================================================

E2E_TEST(Tier2, FEAT_20, DiskCache_ExactWatermarkBoundaryMatch) {
    T2C_ScopedSandboxRoots sandbox("feat20_exact");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("exact_1", 60ULL * kMb);
    loader.recordCacheAccess("exact_2", 60ULL * kMb);
    loader.recordCacheAccess("exact_3", 60ULL * kMb);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), AsyncLoader::DefaultCacheHighWatermarkBytes);

    // 恰好等于 180MB 高水位不触发淘汰
    loader.enforceCacheQuota();
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), AsyncLoader::DefaultCacheHighWatermarkBytes);

    // 超出高水位哪怕 1 字节即触发收缩至 <= 140MB 低水位
    loader.recordCacheAccess("exact_overflow_1b", 1ULL);
    loader.enforceCacheQuota();
    E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), AsyncLoader::DefaultCacheLowWatermarkBytes);

    loader.term();
    return true;
}

E2E_TEST(Tier2, FEAT_20, DiskCache_ZeroByteFileWrittenSafely) {
    T2C_ScopedSandboxRoots sandbox("feat20_zero");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    const uint8_t emptyDummy = 0u;
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile("/cache/zero_entry.bin", &emptyDummy, 0u));
    E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists("/cache/zero_entry.bin"));

    auto readOr = FCT::SAI::FileSystem::readBinaryFile("/cache/zero_entry.bin");
    E2E_ASSERT_STATUS_OK(readOr);
    E2E_ASSERT_EQ(readOr->size(), 0u);

    loader.recordCacheAccess("zero_entry", 0ULL);
    E2E_ASSERT_TRUE(loader.hasCacheEntry("zero_entry"));
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 0ULL);

    loader.term();
    return true;
}

E2E_TEST(Tier2, FEAT_20, DiskCache_AllFilesPinnedHaltsSafely) {
    T2C_ScopedSandboxRoots sandbox("feat20_allpin");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("pin_1", 80ULL * kMb);
    loader.recordCacheAccess("pin_2", 50ULL * kMb);
    loader.recordCacheAccess("pin_3", 110ULL * kMb);
    loader.pinCacheEntry("pin_1");
    loader.pinCacheEntry("pin_2");
    loader.pinCacheEntry("pin_3");

    // 全部条目被活跃引用钉住时安全终止，绝不死循环或强删在用资产
    loader.enforceCacheQuota();
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 240ULL * kMb);

    loader.unpinCacheEntry("pin_3");
    loader.enforceCacheQuota();
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 130ULL * kMb);
    E2E_ASSERT_FALSE(loader.hasCacheEntry("pin_3"));

    loader.term();
    return true;
}

E2E_TEST(Tier2, FEAT_20, DiskCache_EmergencyMemoryWarningWatermarkEnforced) {
    T2C_ScopedSandboxRoots sandbox("feat20_emerg");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("em_1", 40ULL * kMb);
    loader.recordCacheAccess("em_2", 40ULL * kMb);
    loader.recordCacheAccess("em_3", 35ULL * kMb);
    loader.recordCacheAccess("em_4", 35ULL * kMb);

    loader.onMemoryWarning(-1);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 150ULL * kMb);

    loader.onMemoryWarning(1);
    E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), AsyncLoader::EmergencyCacheWatermarkBytes);

    loader.onMemoryWarning(2);
    E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), 80ULL * kMb);

    loader.term();
    return true;
}

E2E_TEST(Tier2, FEAT_20, DiskCache_AccessTimestampUpdatePreservesFile) {
    T2C_ScopedSandboxRoots sandbox("feat20_preserve");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    const uint8_t tag[4] = {'K', 'E', 'E', 'P'};
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile("/cache/ts_a.bin", tag, sizeof(tag)));
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile("/cache/ts_b.bin", tag, sizeof(tag)));
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile("/cache/ts_c.bin", tag, sizeof(tag)));

    loader.recordCacheAccess("ts_a", 65ULL * kMb);
    loader.recordCacheAccess("ts_b", 65ULL * kMb);
    loader.recordCacheAccess("ts_c", 65ULL * kMb);
    loader.updateCacheAccessTime("ts_a");

    loader.enforceCacheQuota();
    E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists("/cache/ts_a.bin"));
    E2E_ASSERT_FALSE(FCT::SAI::FileSystem::exists("/cache/ts_b.bin"));

    loader.term();
    return true;
}

// ============================================================================
// [FEAT-21] WorkHost 单线程内联万级爆发与重入调度边界
// ============================================================================

E2E_TEST(Tier2, FEAT_21, WorkHost_TenThousandTasksBurstDrainedInline) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    E2E_ASSERT_EQ(host.getWorkerCount(), 0u);

    std::atomic<uint32_t> counter{0};
    for (uint32_t batch = 0; batch < 20u; ++batch) {
        for (uint32_t i = 0; i < 500u; ++i) {
            E2E_ASSERT_STATUS_OK(host.postTask(T2C_AtomicIncFn{&counter}));
        }
        host.drainInline(200000u);
    }

    E2E_ASSERT_EQ(counter.load(std::memory_order_relaxed), 10000u);
    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_21, WorkHost_EmptySyncReturnsImmediately) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    E2E_ASSERT_FALSE(host.executeNextContract());
    host.sync();
    host.waitAll();
    host.drainInline(5000u);
    E2E_ASSERT_EQ(host.pumpMainThreadTasks(64u), 0u);

    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_21, WorkHost_EmptyPathAsyncReadRejected) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    T2C_ReadErrorCapture hostOut{};
    T2C_ReadErrorCapture loaderOut{};
    E2E_ASSERT_STATUS_OK(host.submitAsyncFileRead("", T2C_StringReadCaptureFn{&hostOut}));
    E2E_ASSERT_STATUS_OK(loader.submitAsyncBinaryRead("", T2C_BinaryReadCaptureFn{&loaderOut}));

    host.drainInline(100000u);
    E2E_ASSERT_TRUE(hostOut.called);
    E2E_ASSERT_FALSE(hostOut.ok);
    E2E_ASSERT_TRUE(loaderOut.called);
    E2E_ASSERT_FALSE(loaderOut.ok);
    E2E_ASSERT_EQ(loaderOut.code, absl::StatusCode::kInvalidArgument);

    loader.term();
    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_21, WorkHost_ReentrantSchedulingDrainedSafely) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    T2C_ReentrantState state{};
    state.host = &host;
    state.maxDepth = 64u;
    E2E_ASSERT_STATUS_OK(host.postTask(T2C_ReentrantTaskFn{&state, 0u}));

    host.drainInline(200000u);
    E2E_ASSERT_EQ(state.executed.load(std::memory_order_relaxed), 65u);

    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_21, WorkHost_RapidSyncLoopStability) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    std::atomic<uint32_t> totalCompleted{0};
    for (uint32_t frame = 0; frame < 200u; ++frame) {
        E2E_ASSERT_STATUS_OK(host.postTask(T2C_AtomicIncFn{&totalCompleted}));
        E2E_ASSERT_STATUS_OK(host.postIoTask(T2C_AtomicIncFn{&totalCompleted}));
        host.enqueueMainThread(T2C_AtomicIncFn{&totalCompleted});
        host.drainInline(50000u);
        host.sync();
    }

    E2E_ASSERT_EQ(totalCompleted.load(std::memory_order_relaxed), 600u);
    host.term();
    return true;
}

// ============================================================================
// [FEAT-22] WorkHost 多线程高并发竞争、停机拦截与 FIFO 泵送
// ============================================================================

E2E_TEST(Tier2, FEAT_22, WorkHost_MaxWorkerThreadPoolInit) {
    const uint32_t hw = std::thread::hardware_concurrency();
    const uint32_t maxWorkers = std::max(4u, std::min(16u, (hw > 0u ? hw * 2u : 8u)));

    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, maxWorkers, 2u));
    E2E_ASSERT_EQ(host.getWorkerCount(), maxWorkers);
    E2E_ASSERT_EQ(host.getIoThreadCount(), 2u);

    host.term();
    E2E_ASSERT_EQ(host.getWorkerCount(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_22, WorkHost_EmptyDrainOnRunningPoolReturnsImmediately) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));

    host.sync();
    host.waitAll();
    E2E_ASSERT_EQ(host.pumpMainThreadTasks(64u), 0u);
    E2E_ASSERT_TRUE(host.isRunning());

    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_22, WorkHost_ScheduleAfterTermRejected) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
    host.term();

    std::atomic<uint32_t> dummy{0};
    const Status cpuSt = host.postTask(T2C_AtomicIncFn{&dummy});
    const Status ioSt = host.postIoTask(T2C_AtomicIncFn{&dummy});
    const Status fileSt = host.submitAsyncFileRead("none.txt", nullptr);

    E2E_ASSERT_EQ(cpuSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(ioSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(fileSt.code(), absl::StatusCode::kFailedPrecondition);
    E2E_ASSERT_EQ(dummy.load(std::memory_order_relaxed), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_22, WorkHost_MultiThreadContentionDrain) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 4u, 2u));

    std::atomic<uint32_t> cpuDone{0};
    std::atomic<uint32_t> ioDone{0};
    std::vector<std::thread> producers;
    producers.reserve(4u);
    for (uint32_t t = 0; t < 4u; ++t) {
        producers.emplace_back(T2C_WorkHostContentionProducerFn{&host, 100u, 25u, &cpuDone, &ioDone});
    }
    for (auto& th : producers) {
        th.join();
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((cpuDone.load(std::memory_order_acquire) < 400u || ioDone.load(std::memory_order_acquire) < 100u) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    E2E_ASSERT_EQ(cpuDone.load(std::memory_order_acquire), 400u);
    E2E_ASSERT_EQ(ioDone.load(std::memory_order_acquire), 100u);

    host.term();
    return true;
}

E2E_TEST(Tier2, FEAT_22, WorkHost_MainThreadCallbackQueueFifoPump) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    std::vector<uint32_t> orderLog;
    orderLog.reserve(32u);
    for (uint32_t i = 0; i < 32u; ++i) {
        host.enqueueMainThread(T2C_FifoAppendFn{&orderLog, i});
    }

    E2E_ASSERT_EQ(host.pumpMainThreadTasks(10u), 10u);
    E2E_ASSERT_EQ(orderLog.size(), 10u);
    E2E_ASSERT_EQ(host.pumpMainThreadTasks(64u), 22u);
    E2E_ASSERT_EQ(orderLog.size(), 32u);

    for (uint32_t i = 0; i < 32u; ++i) {
        E2E_ASSERT_EQ(orderLog[i], i);
    }

    host.term();
    return true;
}

// ============================================================================
// [FEAT-55] 双轨字体分类边界、PUA 路由与并发桶隔离
// ============================================================================

E2E_TEST(Tier2, FEAT_55, FontDualTrack_MixedLongStringClassification) {
    std::string longCombat;
    longCombat.reserve(4096u);
    for (int i = 0; i < 150; ++i) {
        longCombat += "-9999! +250% 暴击 闪避 ";
    }
    E2E_ASSERT_GT(longCombat.size(), 3000u);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(longCombat), FontTrack::InGameBaked);

    // 尾部混入单个开放汉字立即触发整串切换至轨道 B
    longCombat += "级";
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(longCombat), FontTrack::UiRuntime);
    return true;
}

E2E_TEST(Tier2, FEAT_55, FontDualTrack_EmptyStringClassificationSafe) {
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(""), FontTrack::UiRuntime);
    // 截断的半截 UTF-8 序列不发生越界读取，安全回退至 UiRuntime
    const char brokenUtf8[3] = {'\xE4', '\xB8', '\0'};
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(std::string_view(brokenUtf8, 2u)), FontTrack::UiRuntime);
    return true;
}

E2E_TEST(Tier2, FEAT_55, FontDualTrack_PrivateUseAreaRoutesToTrackB) {
    // U+E000 (EE 80 80) 与 U+F8FF (EF A3 BF) 均位于 BMP 私有保留区
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("100\xEE\x80\x80"), FontTrack::UiRuntime);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("999\xEF\xA3\xBF"), FontTrack::UiRuntime);
    return true;
}

E2E_TEST(Tier2, FEAT_55, FontDualTrack_TrackAZeroCpuRasterizationInvariant) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);
    InGameTextComponent textComp;

    for (int i = 0; i < 200; ++i) {
        textComp.spawnDamageNumber("暴击-98765!", FCT::Vec3<>{0.0f, 1.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, true);
    }
    std::vector<GpuInGameTextQuadInstance> quads;
    textComp.buildQuadInstances(font, quads);

    E2E_ASSERT_GT(quads.size(), 1000u);
    E2E_ASSERT_EQ(font.getFreeTypeInvocationCount(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_55, FontDualTrack_ConcurrentDistinctDrawBucketsEmitted) {
    DrawBucket trackA[4];
    DrawBucket trackB[4];
    std::atomic<uint32_t> okThreads{0};

    std::vector<std::thread> threads;
    threads.reserve(4u);
    for (uint32_t i = 0; i < 4u; ++i) {
        threads.emplace_back(T2C_DualTrackBucketWorkerFn{&trackA[i], &trackB[i], &okThreads});
    }
    for (auto& th : threads) {
        th.join();
    }

    E2E_ASSERT_EQ(okThreads.load(std::memory_order_relaxed), 4u);
    const uint64_t trackAMask = InputTags::Text | InputTags::InGameBaked;
    const uint64_t trackBMask = InputTags::UI | InputTags::RmlUi;
    for (uint32_t i = 0; i < 4u; ++i) {
        E2E_ASSERT_EQ(trackA[i].inputTagsMask & trackAMask, trackAMask);
        E2E_ASSERT_EQ(trackA[i].inputTagsMask & InputTags::RmlUi, 0ULL);
        E2E_ASSERT_EQ(trackB[i].inputTagsMask & trackBMask, trackBMask);
    }
    return true;
}

// ============================================================================
// [FEAT-56] .mqfont 精确满载装箱、空路径拒绝与溢出检测
// ============================================================================

E2E_TEST(Tier2, FEAT_56, MqFont_ExactAtlasCapacityPackingSucceeds) {
    AtlasShelfPacker exactPacker(128u, 128u, 1u);
    BakedFont font;
    font.setAtlasDimensions(128u, 128u);

    // 128x128 无边距图集精确塞满 16 个 32x32 字形块
    for (uint32_t i = 0; i < 16u; ++i) {
        const auto alloc = exactPacker.allocate(32u, 32u);
        E2E_ASSERT_TRUE(alloc.success);
        GlyphMetric m{};
        m.width = 32.0f;
        m.height = 32.0f;
        m.advanceX = 32.0f;
        m.uvMinMax[0] = static_cast<uint16_t>((alloc.x * 65535u) / 128u);
        m.uvMinMax[1] = static_cast<uint16_t>((alloc.y * 65535u) / 128u);
        m.uvMinMax[2] = static_cast<uint16_t>(((alloc.x + 32u) * 65535u) / 128u);
        m.uvMinMax[3] = static_cast<uint16_t>(((alloc.y + 32u) * 65535u) / 128u);
        font.registerGlyph(static_cast<char32_t>(U'A' + i), m);
    }

    const std::vector<uint8_t> blob = font.saveToBuffer();
    BakedFont reloaded;
    E2E_ASSERT_STATUS_OK(reloaded.loadFromBuffer(blob));
    E2E_ASSERT_EQ(reloaded.getClosedCharset().size(), 16u);
    return true;
}

E2E_TEST(Tier2, FEAT_56, MqFont_EmptyFontPathRejected) {
    BakedFont font;
    const Status emptyLoad = font.load("");
    E2E_ASSERT_FALSE(emptyLoad.ok());
    E2E_ASSERT_EQ(emptyLoad.code(), absl::StatusCode::kInvalidArgument);

    E2E_ASSERT_FALSE(font.loadFromBuffer({}).ok());

    FontSystem sys(FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_EQ(sys.registerFontFace("", "fonts/ui.ttf").code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(sys.registerFontFace("UIFont", "").code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_56, MqFont_UnbakedGlyphMetricFallback) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);

    E2E_ASSERT_FALSE(font.hasGlyph(U'齉'));
    const GlyphMetric fallback = font.getGlyphMetric(U'齉');
    E2E_ASSERT_TRUE(std::abs(fallback.advanceX - 10.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(fallback.height - 14.0f) < 1e-5f);
    E2E_ASSERT_EQ(font.getFreeTypeInvocationCount(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_56, MqFont_ShelfPackerOverflowDetected) {
    AtlasShelfPacker smallPacker(64u, 64u, 1u);
    for (int i = 0; i < 4; ++i) {
        E2E_ASSERT_TRUE(smallPacker.allocate(32u, 32u).success);
    }
    // 第 5 个 32x32 超出 64x64 总容量，必须准确报告失败
    E2E_ASSERT_FALSE(smallPacker.allocate(32u, 32u).success);

    AtlasShelfPacker widthOverflowPacker(64u, 64u, 1u);
    E2E_ASSERT_FALSE(widthOverflowPacker.allocate(128u, 16u).success);
    return true;
}

E2E_TEST(Tier2, FEAT_56, MqFont_RuntimeZeroFreeTypeCalls) {
    MqFontBinaryHeader badMagicHdr{};
    badMagicHdr.magic = 0x12345678u;
    std::vector<uint8_t> badBuf(sizeof(MqFontBinaryHeader), 0u);
    std::memcpy(badBuf.data(), &badMagicHdr, sizeof(MqFontBinaryHeader));

    BakedFont font;
    E2E_ASSERT_FALSE(font.loadFromBuffer(badBuf).ok());

    font.initDefaultCombatPreset(512u, 512u);
    const std::vector<uint8_t> validBuf = font.saveToBuffer();
    BakedFont runtimeFont;
    E2E_ASSERT_STATUS_OK(runtimeFont.loadFromBuffer(validBuf));
    for (int i = 0; i < 500; ++i) {
        (void)runtimeFont.getGlyphMetric(U'9');
        (void)runtimeFont.getGlyphMetric(U'暴');
    }
    E2E_ASSERT_EQ(runtimeFont.getFreeTypeInvocationCount(), 0u);
    return true;
}

// ============================================================================
// [FEAT-57] 万级跳字合批、零实例短路与极端时间步钳制 (InGameTextComponent)
// ============================================================================

E2E_TEST(Tier2, FEAT_57, InGameText_TenThousandQuadsDrawBatches) {
    E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(1024u, 1024u), 1u);
    E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(1025u, 1024u), 2u);
    E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(10000u, 1024u), 10u);

    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);
    InGameTextComponent comp;
    for (uint32_t i = 0; i < 1000u; ++i) {
        comp.spawnDamageNumber("0123456789", FCT::Vec3<>{0.0f, 0.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, false);
    }

    std::vector<GpuInGameTextQuadInstance> quads;
    comp.buildQuadInstances(font, quads);
    E2E_ASSERT_EQ(quads.size(), 10000u);
    return true;
}

E2E_TEST(Tier2, FEAT_57, InGameText_ZeroQuadsEmitsZeroDraws) {
    E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(0u, 1024u), 0u);
    E2E_ASSERT_EQ(InGameTextComponent::calculateDrawCallCount(500u, 0u), 0u);

    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);
    InGameTextComponent emptyComp;
    std::vector<GpuInGameTextQuadInstance> quads;
    emptyComp.buildQuadInstances(font, quads);
    E2E_ASSERT_TRUE(quads.empty());

    InGameTextComponent::evaluateBatchAnimations(quads, 0.016f);
    E2E_ASSERT_EQ(quads.size(), 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_57, InGameText_SizeAndAlignmentInspection) {
    E2E_ASSERT_EQ(sizeof(GpuInGameTextQuadInstance), 32u);
    E2E_ASSERT_EQ(alignof(GpuInGameTextQuadInstance), 16u);
    E2E_ASSERT_EQ(offsetof(GpuInGameTextQuadInstance, worldPosAndScale), 0u);
    E2E_ASSERT_EQ(offsetof(GpuInGameTextQuadInstance, uvMinMax), 16u);
    E2E_ASSERT_EQ(offsetof(GpuInGameTextQuadInstance, fillColorRgba8), 24u);
    E2E_ASSERT_EQ(offsetof(GpuInGameTextQuadInstance, outlineColorAndPx), 28u);

    std::vector<GpuInGameTextQuadInstance> alignedVec(8u);
    E2E_ASSERT_EQ(reinterpret_cast<uintptr_t>(alignedVec.data()) % 16u, 0u);
    return true;
}

E2E_TEST(Tier2, FEAT_57, InGameText_MassiveTimeDeltaScaleClamping) {
    std::vector<GpuInGameTextQuadInstance> quads(4u);
    for (auto& q : quads) {
        q.worldPosAndScale[1] = 1.0f;
        q.worldPosAndScale[3] = 1.5f;
    }

    // 极端卡顿帧 dt=100.0s 下缩放倍率严格钳制在 0.5f 下限，绝不翻转为负值
    InGameTextComponent::evaluateBatchAnimations(quads, 100.0f);
    for (const auto& q : quads) {
        E2E_ASSERT_TRUE(std::abs(q.worldPosAndScale[3] - 0.5f) < 1e-5f);
        E2E_ASSERT_TRUE(std::abs(q.worldPosAndScale[1] - 501.0f) < 1e-3f);
    }

    InGameTextComponent comp;
    comp.spawnDamageNumber("999", FCT::Vec3<>{0.0f, 0.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, false);
    comp.tickItems(100.0f);
    E2E_ASSERT_TRUE(comp.getActiveItems().empty());
    return true;
}

E2E_TEST(Tier2, FEAT_57, InGameText_BatchAnimationEvaluationSpeed) {
    std::vector<GpuInGameTextQuadInstance> quads(10000u);
    for (uint32_t i = 0; i < 10000u; ++i) {
        quads[i].worldPosAndScale[0] = 1.0f;
        quads[i].worldPosAndScale[1] = 10.0f;
        quads[i].worldPosAndScale[2] = -2.0f;
        quads[i].worldPosAndScale[3] = 1.0f;
    }

    InGameTextComponent::evaluateBatchAnimations(quads, 0.016f);
    E2E_ASSERT_TRUE(std::abs(quads[0].worldPosAndScale[1] - 10.08f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(quads[9999].worldPosAndScale[1] - 10.08f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(quads[9999].worldPosAndScale[3] - 0.9984f) < 1e-4f);
    return true;
}

// ============================================================================
// [FEAT-58] 千字批量预取、空批保护与子区域上传校验 (FontSystem)
// ============================================================================

E2E_TEST(Tier2, FEAT_58, FontSystem_BatchQueryThousandCodepoints) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    std::vector<char32_t> cjkBatch(1000u);
    for (uint32_t i = 0; i < 1000u; ++i) {
        cjkBatch[i] = static_cast<char32_t>(0x4E00u + i);
    }

    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(cjkBatch, 16u));
    E2E_ASSERT_EQ(fontSys.getLastBatchCodepointCount(), 1000u);

    CachedUiGlyph first{};
    CachedUiGlyph last{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(static_cast<char32_t>(0x4E00u), 16u, first));
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(static_cast<char32_t>(0x4E00u + 999u), 16u, last));
    E2E_ASSERT_GE(fontSys.getAtlasManager().getPageCount(), 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_58, FontSystem_EmptyBatchQuerySafe) {
    FontSystem fontSys(FontBackendMode::Desktop_FreeType);
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached({}, 16u));
    E2E_ASSERT_EQ(fontSys.getLastBatchCodepointCount(), 0u);

    const char32_t one[1] = {U'A'};
    E2E_ASSERT_EQ(fontSys.ensureGlyphsCached(one, 0u).code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(fontSys.queryFontMetrics("", 0u).status().code(), absl::StatusCode::kInvalidArgument);
    return true;
}

E2E_TEST(Tier2, FEAT_58, FontSystem_PrebakedSubsetHitVsMiss) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    const char32_t customSubset[2] = {U'钻', U'石'};
    fontSys.registerPrebakedSubset(customSubset);

    E2E_ASSERT_TRUE(fontSys.isGlyphInPrebakedSubset(U'钻'));
    E2E_ASSERT_FALSE(fontSys.queriesSystemFontProvider(U'钻'));
    E2E_ASSERT_FALSE(fontSys.isGlyphInPrebakedSubset(U'氪'));
    E2E_ASSERT_TRUE(fontSys.queriesSystemFontProvider(U'氪'));
    return true;
}

E2E_TEST(Tier2, FEAT_58, FontSystem_DesktopHintingInvariant) {
    FontSystem sys(FontBackendMode::Desktop_FreeType);
    E2E_ASSERT_TRUE(sys.isFreeTypeBackendActive());
    E2E_ASSERT_TRUE(sys.isHintingEnabled());

    FontSystem miniSys(FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_FALSE(miniSys.isFreeTypeBackendActive());
    E2E_ASSERT_FALSE(miniSys.isHintingEnabled());
    return true;
}

E2E_TEST(Tier2, FEAT_58, FontSystem_AtlasSubregionTextureUpdate) {
    GlyphAtlasManager atlas;
    uint32_t pageId = 0u;
    Domain::Renderer::AtlasAllocation alloc{};
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'M', 24u, 24u, pageId, alloc));

    // 空指针或长度不足必须被拒绝，越界 pageId 返回 NotFound
    const uint8_t tiny[8] = {0xFFu};
    E2E_ASSERT_EQ(atlas.uploadGlyphBitmap(pageId, alloc, nullptr, 0u).code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(atlas.uploadGlyphBitmap(pageId, alloc, tiny, sizeof(tiny)).code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(atlas.uploadGlyphBitmap(999u, alloc, tiny, sizeof(tiny)).code(), absl::StatusCode::kNotFound);

    const std::vector<uint8_t> validPixels(24u * 24u, 0xFFu);
    E2E_ASSERT_STATUS_OK(atlas.uploadGlyphBitmap(pageId, alloc, validPixels.data(), validPixels.size(), 24u));
    return true;
}

// ============================================================================
// [FEAT-59] 动态字形图集双格式边界、世代单调递增与紧急页回收
// ============================================================================

E2E_TEST(Tier2, FEAT_59, DynamicAtlas_SmpEmojiRgba8Format) {
    GlyphAtlasManager atlas;
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(0x1F600u), TextureFormat::R8G8B8A8_UNORM);
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(0x1F680u), TextureFormat::R8G8B8A8_UNORM);
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(0x26A1u), TextureFormat::R8G8B8A8_UNORM);

    uint32_t emojiPage = 0u;
    Domain::Renderer::AtlasAllocation alloc{};
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(0x1F680u, 32u, 32u, emojiPage, alloc));
    E2E_ASSERT_EQ(atlas.getAtlasPageFormat(emojiPage), TextureFormat::R8G8B8A8_UNORM);
    return true;
}

E2E_TEST(Tier2, FEAT_59, DynamicAtlas_AsciiR8Format) {
    GlyphAtlasManager atlas;
    for (char32_t cp = U'!'; cp <= U'~'; ++cp) {
        E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(cp), TextureFormat::R8_UNORM);
    }
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(0x4E00u), TextureFormat::R8_UNORM);

    // 超出单页 1024x1024 上限的超大字形请求返回 ResourceExhausted
    uint32_t pageId = 0u;
    Domain::Renderer::AtlasAllocation hugeAlloc{};
    E2E_ASSERT_EQ(atlas.allocateGlyph(U'A', 2048u, 64u, pageId, hugeAlloc).code(),
                  absl::StatusCode::kResourceExhausted);
    return true;
}

E2E_TEST(Tier2, FEAT_59, DynamicAtlas_GenerationMonotonicIncrement) {
    GlyphAtlasManager atlas;
    uint32_t prevGen = atlas.getAtlasGeneration();
    E2E_ASSERT_EQ(prevGen, 1u);

    for (int i = 0; i < 5; ++i) {
        atlas.onMemoryWarning();
        const uint32_t curGen = atlas.getAtlasGeneration();
        E2E_ASSERT_GT(curGen, prevGen);
        prevGen = curGen;
    }
    E2E_ASSERT_EQ(prevGen, 6u);
    return true;
}

E2E_TEST(Tier2, FEAT_59, DynamicAtlas_EmergencyPageEviction) {
    GlyphAtlasManager atlas;
    Domain::Renderer::AtlasAllocation alloc{};
    uint32_t lastPageId = 0u;

    // 每张图集页含 16 层 (1024x1024x16)，分配 49 个 (3*16+1) 600x600 字形以跨满 4 页
    for (uint32_t i = 0; i < 49u; ++i) {
        atlas.setCurrentFrame(10u + (i / 16u) * 10u);
        E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(static_cast<char32_t>(U'一' + i), 600u, 600u, lastPageId, alloc));
    }
    E2E_ASSERT_EQ(atlas.getPageCount(), 4u);

    // 连续 3 次紧急告警逐页淘汰最旧页直至仅剩 1 页，第 4 次原位重置该单页
    atlas.onMemoryWarning();
    E2E_ASSERT_EQ(atlas.getPageCount(), 3u);
    atlas.onMemoryWarning();
    E2E_ASSERT_EQ(atlas.getPageCount(), 2u);
    atlas.onMemoryWarning();
    E2E_ASSERT_EQ(atlas.getPageCount(), 1u);
    atlas.onMemoryWarning();
    E2E_ASSERT_EQ(atlas.getPageCount(), 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_59, DynamicAtlas_UiMeshRebuiltOnGenerationMismatch) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    const char32_t uiCps[3] = {U'公', U'告', U'栏'};
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(uiCps, 20u));

    CachedUiGlyph before{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'公', 20u, before));
    const uint32_t oldGen = before.generation;

    fontSys.onMemoryWarning();
    CachedUiGlyph missAfterEvict{};
    E2E_ASSERT_FALSE(fontSys.tryGetCachedGlyph(U'公', 20u, missAfterEvict));

    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(uiCps, 20u));
    CachedUiGlyph rebuilt{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'公', 20u, rebuilt));
    E2E_ASSERT_GT(rebuilt.generation, oldGen);
    return true;
}

// ============================================================================
// [FEAT-60] 跨设备语义等价与未注册动作安全查询 (HIDSubsystem)
// ============================================================================

E2E_TEST(Tier2, FEAT_60, ZeroRawExposure_MultiDeviceVelocityEquivalence) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    auto moveAction = makeUnique<InputAction>("IA_Move", 0u, EInputActionValueType::Axis2D);
    moveAction->addModifier(makeUnique<InputModifierNormalize2D>());
    hid.registerAction(std::move(moveAction));

    auto imc = makeUnique<InputMappingContext>("IMC_AllDevices", 0);
    ActionBindingMapping bKeyW{};
    bKeyW.inputSourceKey = "Key_W";
    bKeyW.actionName = "IA_Move";
    bKeyW.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
    imc->addBinding(std::move(bKeyW));

    ActionBindingMapping bKeyD{};
    bKeyD.inputSourceKey = "Key_D";
    bKeyD.actionName = "IA_Move";
    imc->addBinding(std::move(bKeyD));

    ActionBindingMapping bGamepad{};
    bGamepad.inputSourceKey = "Gamepad_LeftStick";
    bGamepad.actionName = "IA_Move";
    imc->addBinding(std::move(bGamepad));
    hid.addMappingContext(std::move(imc), 0);

    // 键盘 W+D 对角同时按下 -> 经 Normalize2D 钳制后模长严格为 1.0
    hid.setSourceInputValue("Key_W", InputValue(1.0f));
    hid.setSourceInputValue("Key_D", InputValue(1.0f));
    hid.update(0.016f);
    const float kbMag = hid.getActionValue("IA_Move").magnitude();

    // 手柄摇杆满偏 45 度 -> 模长同样严格为 1.0
    hid.clearSourceInputValues();
    hid.setSourceInputValue("Gamepad_LeftStick", InputValue(FCT::Vec2{0.70710678f, 0.70710678f}));
    hid.update(0.016f);
    const float padMag = hid.getActionValue("IA_Move").magnitude();

    E2E_ASSERT_TRUE(std::abs(kbMag - 1.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(kbMag - padMag) < 1e-4f);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_60, ZeroRawExposure_ZeroRawEventExposureToGameplay) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle aimHandle = hid.registerAction("IA_Aim", EInputActionValueType::Axis2D);

    // 触控事件如果被 UI 拦截，子系统内部直接归零，绝不向业务动作传递增量
    hid.onTouchDown(0u, 10.0f, 10.0f, true);
    hid.onTouchMove(0u, 90.0f, 90.0f, true);
    hid.update(0.016f);

    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(aimHandle).magnitude()) < 1e-6f);
    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_60, ZeroRawExposure_UnregisteredActionQuerySafe) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    E2E_ASSERT_FALSE(hid.isActionValid("IA_DoesNotExist"));
    E2E_ASSERT_EQ(hid.getActionHandle("IA_DoesNotExist"), 0u);
    E2E_ASSERT_TRUE(hid.findAction(99999u) == nullptr);
    E2E_ASSERT_FALSE(hid.isActionPressed(std::string_view("IA_DoesNotExist")));
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue("IA_DoesNotExist").magnitude()) < 1e-6f);
    E2E_ASSERT_EQ(hid.getActionTriggerState("IA_DoesNotExist"), ETriggerState::None);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_60, ZeroRawExposure_GameplayHasZeroDeviceReferences) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle interact = hid.registerAction("IA_Interact", EInputActionValueType::Digital);

    T2C_ActionFireCounter fire{};
    const uint64_t cbId = hid.bindActionEvent(interact, ETriggerState::Triggered, T2C_ActionFireFn{&fire});
    E2E_ASSERT_NE(cbId, 0ULL);
    E2E_ASSERT_EQ(hid.bindActionEvent(0u, ETriggerState::Triggered, T2C_ActionFireFn{&fire}), 0ULL);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(interact, InputValue(true), 0.016f));
    E2E_ASSERT_EQ(fire.count, 1u);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_60, ZeroRawExposure_SemanticPressedQuery) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle sprint = hid.registerAction("IA_Sprint", EInputActionValueType::Digital);
    E2E_ASSERT_FALSE(hid.isActionPressed(std::string_view("IA_Sprint")));

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(sprint, InputValue(true), 0.016f));
    E2E_ASSERT_TRUE(hid.isActionPressed(std::string_view("IA_Sprint")));

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(sprint, InputValue(false), 0.016f));
    E2E_ASSERT_FALSE(hid.isActionPressed(std::string_view("IA_Sprint")));

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-61] InputValue NaN/Inf 净化与多源融合边界
// ============================================================================

E2E_TEST(Tier2, FEAT_61, InputValue_3DAxisPrecisionBounds) {
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    const float infVal = std::numeric_limits<float>::infinity();

    // NaN / Inf 输入在构造期自动净化为 0.0f，杜绝污染下游物理与相机变换
    const InputValue sanitized(FCT::Vec3f{nanVal, infVal, -1.25f});
    E2E_ASSERT_EQ(sanitized.getValueType(), EInputActionValueType::Axis3D);
    E2E_ASSERT_TRUE(std::isfinite(sanitized.get<FCT::Vec3f>().x));
    E2E_ASSERT_TRUE(std::isfinite(sanitized.get<FCT::Vec3f>().y));
    E2E_ASSERT_TRUE(std::abs(sanitized.get<FCT::Vec3f>().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(sanitized.get<FCT::Vec3f>().y) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(sanitized.get<FCT::Vec3f>().z - (-1.25f)) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_61, InputValue_DefaultConstructedIsDigitalFalse) {
    const InputValue def;
    E2E_ASSERT_EQ(def.getValueType(), EInputActionValueType::Digital);
    E2E_ASSERT_FALSE(def.get<bool>());
    E2E_ASSERT_TRUE(std::abs(def.magnitude()) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_61, InputValue_Axis1DExtraction) {
    const InputValue v(FCT::Vec2{-0.85f, 0.40f});
    const InputValue as1D = v.convertTo(EInputActionValueType::Axis1D);
    E2E_ASSERT_EQ(as1D.getValueType(), EInputActionValueType::Axis1D);
    E2E_ASSERT_TRUE(std::abs(as1D.get<float>() - (-0.85f)) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_61, InputValue_CumulativeAccumulationOpposingVectors) {
    // A(-1) 与 D(+1) 反向对冲相加精确抵消为 0
    InputValue acc(FCT::Vec2{-1.0f, 0.5f});
    acc.accumulate(InputValue(FCT::Vec2{1.0f, -0.5f}), EInputAccumulationPolicy::Cumulative);
    E2E_ASSERT_TRUE(std::abs(acc.get<FCT::Vec2>().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(acc.get<FCT::Vec2>().y) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(acc.magnitude()) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_61, InputValue_MaxMagnitudeAccumulationPreservesPeak) {
    InputValue acc(FCT::Vec2{0.2f, 0.1f});
    acc.accumulate(InputValue(FCT::Vec2{-0.6f, -0.8f}), EInputAccumulationPolicy::MaxMagnitude);
    acc.accumulate(InputValue(FCT::Vec2{0.4f, 0.3f}), EInputAccumulationPolicy::MaxMagnitude);

    E2E_ASSERT_TRUE(std::abs(acc.get<FCT::Vec2>().x - (-0.6f)) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(acc.get<FCT::Vec2>().y - (-0.8f)) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(acc.magnitude() - 1.0f) < 1e-5f);
    return true;
}

// ============================================================================
// [FEAT-62] 输入修饰器零向量防除零与串联顺序敏感性 (InputModifier)
// ============================================================================

E2E_TEST(Tier2, FEAT_62, Modifier_DiagonalNormalize2DUnitClamp) {
    const InputModifierNormalize2D norm;
    const InputValue diag = norm.modify(InputValue(FCT::Vec2{3.0f, 4.0f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(diag.magnitude() - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(diag.get<FCT::Vec2>().x - 0.6f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(diag.get<FCT::Vec2>().y - 0.8f) < 1e-5f);
    return true;
}

E2E_TEST(Tier2, FEAT_62, Modifier_ZeroVectorNormalize2DSafe) {
    const InputModifierNormalize2D norm;
    const InputValue zeroOut = norm.modify(InputValue(FCT::Vec2{0.0f, 0.0f}), 0.016f);
    E2E_ASSERT_TRUE(std::isfinite(zeroOut.get<FCT::Vec2>().x));
    E2E_ASSERT_TRUE(std::isfinite(zeroOut.get<FCT::Vec2>().y));
    E2E_ASSERT_TRUE(std::abs(zeroOut.magnitude()) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_62, Modifier_DeadZoneSubThresholdFilter) {
    const InputModifierDeadZone dz(0.25f);
    // 严格小于 0.25f 置零，等于 0.25f 保留放行
    E2E_ASSERT_TRUE(std::abs(dz.modify(InputValue(0.249f), 0.016f).get<float>()) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(dz.modify(InputValue(0.250f), 0.016f).get<float>() - 0.250f) < 1e-6f);
    return true;
}

E2E_TEST(Tier2, FEAT_62, Modifier_NegateInversion) {
    const InputModifierNegate negXZ(true, false, true);
    const InputValue v = negXZ.modify(InputValue(FCT::Vec3f{2.0f, 3.0f, -4.0f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(v.get<FCT::Vec3f>().x - (-2.0f)) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(v.get<FCT::Vec3f>().y - 3.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(v.get<FCT::Vec3f>().z - 4.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier2, FEAT_62, Modifier_SwizzleAndScalarChained) {
    const InputModifierSwizzleAxis swizzle(ESwizzleAxisOrder::YXZ);
    const InputModifierNegate negateAll(true, true, true);
    const InputModifierScalar scale(0.5f);

    // 模拟 S 键向后移动: 1.0 -> Swizzle(0, 1) -> Negate(0, -1) -> Scalar(0, -0.5)
    InputValue v(1.0f);
    v = swizzle.modify(v, 0.016f);
    v = negateAll.modify(v, 0.016f);
    v = scale.modify(v, 0.016f);

    E2E_ASSERT_EQ(v.getValueType(), EInputActionValueType::Axis2D);
    E2E_ASSERT_TRUE(std::abs(v.get<FCT::Vec2>().x) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(v.get<FCT::Vec2>().y - (-0.5f)) < 1e-5f);
    return true;
}

// ============================================================================
// [FEAT-63] 五态触发器精确阈值边界校验 (InputTriggerTap / Hold / HoldAndRelease / Chord)
// ============================================================================

E2E_TEST(Tier2, FEAT_63, Trigger_LongHoldStateProgression) {
    InputTriggerHold continuousHold(0.40f, false);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(false), 0.016f), ETriggerState::None);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(true), 0.20f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(true), 0.20f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(true), 0.10f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(false), 0.016f), ETriggerState::Completed);
    E2E_ASSERT_EQ(continuousHold.update(InputValue(false), 0.016f), ETriggerState::None);
    return true;
}

E2E_TEST(Tier2, FEAT_63, Trigger_TapQualifiedUnderThreshold) {
    InputTriggerTap tap(0.20f);

    // 1. 0.0f 未按下保持 None
    E2E_ASSERT_EQ(tap.update(InputValue(0.0f), 0.016f), ETriggerState::None);

    // 2. 按下 0.15s (< 0.20s) 后释放触发 Triggered
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.15f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(tap.update(InputValue(false), 0.016f), ETriggerState::Triggered);

    // 3. 精确上界 0.10f + 0.10f == 0.20f 释放仍合法触发 Triggered
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.10f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.10f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(tap.update(InputValue(false), 0.016f), ETriggerState::Triggered);

    // 4. 越界 0.201f > 0.20f 立即进入 Canceled，后续释放亦为 Canceled
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.201f), ETriggerState::Canceled);
    E2E_ASSERT_EQ(tap.update(InputValue(false), 0.016f), ETriggerState::Canceled);
    return true;
}

E2E_TEST(Tier2, FEAT_63, Trigger_HoldOngoingProgression) {
    InputTriggerHold hold(0.50f, true);
    // 未满 0.50s 提前松手必须进入 Canceled
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.30f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(hold.update(InputValue(false), 0.016f), ETriggerState::Canceled);

    // 重新按下后计时器从 0 重新累积
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.30f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.20f), ETriggerState::Triggered);
    return true;
}

E2E_TEST(Tier2, FEAT_63, Trigger_HoldAndReleaseOnQualifiedRelease) {
    InputTriggerHoldAndRelease bow(0.50f);
    // 0.499f 提前松手 -> Canceled
    E2E_ASSERT_EQ(bow.update(InputValue(true), 0.499f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(bow.update(InputValue(false), 0.016f), ETriggerState::Canceled);

    // 精确达标 0.50f 后松手 -> Triggered
    E2E_ASSERT_EQ(bow.update(InputValue(true), 0.25f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(bow.update(InputValue(true), 0.25f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(bow.update(InputValue(false), 0.016f), ETriggerState::Triggered);
    return true;
}

E2E_TEST(Tier2, FEAT_63, Trigger_ChordedActionPrerequisiteDependency) {
    InputTriggerChordedAction chord(7u, "IA_Ctrl");
    chord.setChordActive(false);
    E2E_ASSERT_EQ(chord.update(InputValue(true), 0.016f), ETriggerState::None);

    chord.setChordActive(true);
    E2E_ASSERT_EQ(chord.update(InputValue(true), 0.016f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(chord.update(InputValue(false), 0.016f), ETriggerState::Completed);
    E2E_ASSERT_EQ(chord.update(InputValue(false), 0.016f), ETriggerState::None);
    return true;
}

// ============================================================================
// [FEAT-64] IMC 优先级栈遮蔽、弹出恢复与非法 TOML 防护
// ============================================================================

E2E_TEST(Tier2, FEAT_64, ImcStack_MultiPriorityLayerResolution) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    const int32_t priorities[4] = {10, -5, 100, 50};
    for (int i = 0; i < 4; ++i) {
        const std::string actName = "IA_Layer_" + std::to_string(i);
        (void)hid.registerAction(actName, EInputActionValueType::Digital);
        auto ctx = makeUnique<InputMappingContext>("IMC_" + std::to_string(priorities[i]), priorities[i]);
        ActionBindingMapping b{};
        b.inputSourceKey = "Key_F";
        b.actionName = actName;
        ctx->addBinding(std::move(b));
        hid.addMappingContext(std::move(ctx), priorities[i]);
    }

    E2E_ASSERT_EQ(hid.getContextStack().size(), 4u);
    E2E_ASSERT_EQ(hid.getContextStack()[0]->getPriority(), 100);
    E2E_ASSERT_EQ(hid.getContextStack()[3]->getPriority(), -5);
    E2E_ASSERT_EQ(hid.resolveKeyInput("Key_F").contextName, "IMC_100");

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_64, ImcStack_HigherPriorityConsumesInput) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle lowAct = hid.registerAction("IA_Low", EInputActionValueType::Digital);
    const InputActionHandle highAct = hid.registerAction("IA_High", EInputActionValueType::Digital);

    auto lowCtx = makeUnique<InputMappingContext>("IMC_Low", 0);
    ActionBindingMapping bLow{};
    bLow.inputSourceKey = "Key_Space";
    bLow.actionHandle = lowAct;
    lowCtx->addBinding(std::move(bLow));

    auto highCtx = makeUnique<InputMappingContext>("IMC_High", 50);
    ActionBindingMapping bHigh{};
    bHigh.inputSourceKey = "Key_Space";
    bHigh.actionHandle = highAct;
    bHigh.consumesInput = false;
    highCtx->addBinding(std::move(bHigh));

    hid.addMappingContext(std::move(lowCtx), 0);
    hid.addMappingContext(std::move(highCtx), 50);

    // consumesInput=false 时允许穿透同时驱动高低两层动作
    hid.setSourceInputValue("Key_Space", InputValue(true));
    hid.update(0.016f);
    E2E_ASSERT_TRUE(hid.getActionValue(highAct).get<bool>());
    E2E_ASSERT_TRUE(hid.getActionValue(lowAct).get<bool>());

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_64, ImcStack_PoppingRestoresUnderlyingInput) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle gameFire = hid.registerAction("IA_Fire", EInputActionValueType::Digital);
    const InputActionHandle menuClick = hid.registerAction("IA_MenuClick", EInputActionValueType::Digital);

    auto gameCtx = makeUnique<InputMappingContext>("IMC_Gameplay", 0);
    ActionBindingMapping bFire{};
    bFire.inputSourceKey = "Mouse_Left";
    bFire.actionHandle = gameFire;
    gameCtx->addBinding(std::move(bFire));

    auto pauseCtx = makeUnique<InputMappingContext>("IMC_Pause", 100);
    ActionBindingMapping bClick{};
    bClick.inputSourceKey = "Mouse_Left";
    bClick.actionHandle = menuClick;
    bClick.consumesInput = true;
    pauseCtx->addBinding(std::move(bClick));

    hid.addMappingContext(std::move(gameCtx), 0);
    hid.addMappingContext(std::move(pauseCtx), 100);
    hid.setSourceInputValue("Mouse_Left", InputValue(true));
    hid.update(0.016f);
    E2E_ASSERT_FALSE(hid.getActionValue(gameFire).get<bool>());

    // 移除不存在的上下文无副作用，移除 IMC_Pause 后底层开火动作恢复
    hid.removeMappingContext("IMC_NonExistent");
    hid.removeMappingContext("IMC_Pause");
    hid.update(0.016f);
    E2E_ASSERT_TRUE(hid.getActionValue(gameFire).get<bool>());

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_64, ImcStack_TomlConfigurationLoading) {
    InputMappingContext imc;
    E2E_ASSERT_EQ(imc.loadFromToml("").code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(imc.loadFromToml("[[bindings\nbroken_toml = ").code(), absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(imc.loadFromFile("").code(), absl::StatusCode::kInvalidArgument);

    constexpr std::string_view validToml = R"(
[context]
name = "IMC_TouchCombat"
priority = 25

[[bindings]]
source = "TouchSwipeDelta2D"
action = "IA_Look"
modifiers = [{ type = "Scalar", factor = 0.25 }, { type = "Negate", x = false, y = true, z = false }]
triggers = [{ type = "Hold", threshold = 0.35, one_shot = true }]
)";
    E2E_ASSERT_STATUS_OK(imc.loadFromToml(validToml));
    E2E_ASSERT_EQ(imc.getName(), "IMC_TouchCombat");
    E2E_ASSERT_EQ(imc.getPriority(), 25);
    E2E_ASSERT_EQ(imc.getActionBindingCount(), 1u);
    return true;
}

E2E_TEST(Tier2, FEAT_64, ImcStack_DesktopVsTouchPresetSwitch) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    (void)hid.registerAction("IA_Zoom", EInputActionValueType::Axis1D);

    // 同名上下文重复注册自动替换并刷新优先级，不产生悬空重复层
    auto presetV1 = makeUnique<InputMappingContext>("IMC_ActivePreset", 5);
    ActionBindingMapping bWheel{};
    bWheel.inputSourceKey = "Mouse_Wheel1D";
    bWheel.actionName = "IA_Zoom";
    presetV1->addBinding(std::move(bWheel));
    hid.addMappingContext(std::move(presetV1), 5);

    auto presetV2 = makeUnique<InputMappingContext>("IMC_ActivePreset", 30);
    ActionBindingMapping bPinch{};
    bPinch.inputSourceKey = "TouchPinchDelta1D";
    bPinch.actionName = "IA_Zoom";
    presetV2->addBinding(std::move(bPinch));
    hid.addMappingContext(std::move(presetV2), 30);

    E2E_ASSERT_EQ(hid.getContextStack().size(), 1u);
    E2E_ASSERT_EQ(hid.getContextStack()[0]->getPriority(), 30);
    E2E_ASSERT_FALSE(hid.resolveKeyInput("Mouse_Wheel1D").triggered);
    E2E_ASSERT_TRUE(hid.resolveKeyInput("TouchPinchDelta1D").triggered);

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-65] 触控虚拟手势源边界与多指独立追踪
// ============================================================================

E2E_TEST(Tier2, FEAT_65, VirtualSource_TouchSwipeDeltaToLookVector) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle swipeAct = hid.registerAction("TouchSwipeDelta2D", EInputActionValueType::Axis2D);

    hid.processTouchSwipe(100.0f, 100.0f, 145.0f, 70.0f, false);
    const FCT::Vec2 delta = hid.getActionValue(swipeAct).get<FCT::Vec2>();
    E2E_ASSERT_TRUE(std::abs(delta.x - 45.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(delta.y - (-30.0f)) < 1e-4f);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_65, VirtualSource_UiConsumedTouchesExcluded) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    // 起始未被 UI 消费，中途进入 UI 热区标记 consumedByUi=true 后立即被手势源剥离
    hid.onTouchDown(5u, 200.0f, 200.0f, false);
    hid.onTouchMove(5u, 220.0f, 200.0f, false);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x - 20.0f) < 1e-4f);

    hid.onTouchMove(5u, 280.0f, 200.0f, true);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDeltaForPointer(5u).x) < 1e-6f);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_65, VirtualSource_TouchPinchDeltaToZoom) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle pinchAct = hid.registerAction("TouchPinchDelta1D", EInputActionValueType::Axis1D);

    // 两指收缩: 初始间距 200px -> 收缩至 125px，产生 -75.0f 负向缩放增量
    hid.processTouchPinch(200.0f, 125.0f, false);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchPinchDelta() - (-75.0f)) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(pinchAct).get<float>() - (-75.0f)) < 1e-4f);

    hid.processTouchPinch(200.0f, 125.0f, true);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchPinchDelta()) < 1e-6f);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_65, VirtualSource_ResetDeltaOnTouchEnd) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    hid.onTouchDown(0u, 50.0f, 50.0f, false);
    hid.onTouchDown(1u, 150.0f, 50.0f, false);
    hid.onTouchMove(1u, 210.0f, 50.0f, false);
    E2E_ASSERT_GT(hid.getTouchPinchDelta(), 0.0f);

    hid.resetTouches();
    E2E_ASSERT_FALSE(hid.isTouchPinchActive());
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().y) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchPinchDelta()) < 1e-6f);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_65, VirtualSource_MultiFingerStability) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    // 三指并发按下：pointer 0 被 UI 摇杆消费，pointer 1 与 pointer 2 组成自由双指捏合
    hid.onTouchDown(0u, 80.0f, 600.0f, true);
    hid.onTouchDown(1u, 400.0f, 300.0f, false);
    hid.onTouchDown(2u, 500.0f, 300.0f, false);
    E2E_ASSERT_TRUE(hid.isTouchPinchActive());

    hid.onTouchMove(2u, 550.0f, 300.0f, false);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchPinchDelta() - 50.0f) < 1e-4f);

    // 抬起 pointer 2 后仅剩 pointer 1 单指自由触控，自动退回非捏合态
    hid.onTouchUp(2u, 550.0f, 300.0f);
    E2E_ASSERT_FALSE(hid.isTouchPinchActive());
    E2E_ASSERT_TRUE(std::abs(hid.getTouchPinchDelta()) < 1e-6f);

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-66] 千次动作序列重放、空序列与非法句柄拦截 (HIDSubsystem)
// ============================================================================

E2E_TEST(Tier2, FEAT_66, InjectAction_BatchReplayThousandActions) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle fireHandle = hid.registerAction("IA_RapidFire", EInputActionValueType::Digital);

    std::vector<HIDSubsystem::TimestampedAction> replaySeq;
    replaySeq.reserve(1000u);
    for (uint32_t i = 0; i < 1000u; ++i) {
        replaySeq.push_back(HIDSubsystem::TimestampedAction{
            static_cast<uint64_t>(i * 16u),
            fireHandle,
            InputValue((i % 2u) == 0u)
        });
    }

    uint32_t processed = 0u;
    E2E_ASSERT_STATUS_OK(hid.replayActionSequence(replaySeq, processed));
    E2E_ASSERT_EQ(processed, 1000u);
    E2E_ASSERT_FALSE(hid.getActionValue(fireHandle).get<bool>());

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_66, InjectAction_EmptyReplaySequenceSafe) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    const std::vector<HIDSubsystem::TimestampedAction> emptySeq;
    uint32_t processed = 999u;
    E2E_ASSERT_STATUS_OK(hid.replayActionSequence(emptySeq, processed));
    E2E_ASSERT_EQ(processed, 0u);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_66, InjectAction_InvalidHandleRejected) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    E2E_ASSERT_EQ(hid.injectInputForAction(0u, InputValue(true), 0.016f).code(),
                  absl::StatusCode::kInvalidArgument);
    E2E_ASSERT_EQ(hid.injectInputForAction(88888u, InputValue(true), 0.016f).code(),
                  absl::StatusCode::kNotFound);
    E2E_ASSERT_EQ(hid.injectInputForAction("IA_Unknown", InputValue(true), 0.016f).code(),
                  absl::StatusCode::kNotFound);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_66, InjectAction_ActionQueryReflectsInjectedState) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle steer = hid.registerAction("IA_Steer", EInputActionValueType::Axis2D);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(steer, InputValue(FCT::Vec2{-0.35f, 0.90f}), 0.016f));
    E2E_ASSERT_TRUE(hid.isActionPressed(std::string_view("IA_Steer")));
    E2E_ASSERT_EQ(hid.getActionTriggerState(steer), ETriggerState::Triggered);
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(steer).get<FCT::Vec2>().x - (-0.35f)) < 1e-5f);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(steer, InputValue(FCT::Vec2{0.0f, 0.0f}), 0.016f));
    E2E_ASSERT_FALSE(hid.isActionPressed(std::string_view("IA_Steer")));
    E2E_ASSERT_EQ(hid.getActionTriggerState(steer), ETriggerState::Completed);

    hid.term();
    return true;
}

E2E_TEST(Tier2, FEAT_66, InjectAction_RapidInjectionsIndependentHandles) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle moveH = hid.registerAction("IA_Move", EInputActionValueType::Axis2D);
    const InputActionHandle skillH = hid.registerAction("IA_SkillQ", EInputActionValueType::Digital);

    for (int i = 0; i < 100; ++i) {
        E2E_ASSERT_STATUS_OK(hid.injectInputForAction(moveH, InputValue(FCT::Vec2{0.25f, 0.75f}), 0.016f));
        E2E_ASSERT_STATUS_OK(hid.injectInputForAction(skillH, InputValue((i % 2) == 0), 0.016f));
    }

    // 高频交错注入互不串扰各自句柄的值与状态
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(moveH).get<FCT::Vec2>().y - 0.75f) < 1e-5f);
    E2E_ASSERT_FALSE(hid.getActionValue(skillH).get<bool>());

    hid.term();
    return true;
}

} // namespace MQEngine::Testing
