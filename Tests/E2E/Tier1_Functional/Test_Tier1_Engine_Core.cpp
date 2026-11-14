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
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/domain/gameplay/NativeScript.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/config/CommandLineParser.h>
#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <Font/Composites/FontRuntimeComposite/BakedFont.h>
#include <Font/Composites/FontRuntimeComposite/FontSystem.h>
#include <Font/Composites/FontRuntimeComposite/FontTypes.h>
#include <Font/Composites/FontRuntimeComposite/GlyphAtlasManager.h>
#include <Font/Composites/FontRuntimeComposite/InGameTextComponent.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <FCT/RHI/WGL2_StorageBuffer.h>
#include <FCT_SAI/FileSystem.h>
#include <FCT_SAI/SystemFontProvider.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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
using ::MQEngine::Domain::HID::InputTriggerDown;
using ::MQEngine::Domain::HID::InputTriggerHold;
using ::MQEngine::Domain::HID::InputTriggerHoldAndRelease;
using ::MQEngine::Domain::HID::InputTriggerPressed;
using ::MQEngine::Domain::HID::InputTriggerReleased;
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
using ::MQEngine::Resource::KtxContainerTextureDecoder;
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
using ::MQEngine::Resource::Font::MsdfGlyphBitmap;
using ::MQEngine::Resource::Font::TextureFormat;

std::atomic<uint64_t> g_t1cSandboxCounter{1};

struct T1C_ScopedSandboxRoots {
    std::string baseDir;
    std::string pkgDir;
    std::string cacheDir;
    std::string prevPkg;
    std::string prevCache;

    explicit T1C_ScopedSandboxRoots(std::string_view tag) {
        prevPkg = std::string(FCT::SAI::FileSystem::getPkgRoot());
        prevCache = std::string(FCT::SAI::FileSystem::getCacheRoot());
        const uint64_t id = g_t1cSandboxCounter.fetch_add(1, std::memory_order_relaxed);
        const auto rootPath = std::filesystem::temp_directory_path() /
                              ("mqe_t1c_" + std::string(tag) + "_" + std::to_string(id));
        baseDir = rootPath.generic_string();
        pkgDir = baseDir + "/pkg";
        cacheDir = baseDir + "/cache";
        (void)FCT::SAI::FileSystem::createDirectories(pkgDir);
        (void)FCT::SAI::FileSystem::createDirectories(cacheDir);
        (void)FCT::SAI::FileSystem::setPkgRoot(pkgDir);
        (void)FCT::SAI::FileSystem::setCacheRoot(cacheDir);
    }

    ~T1C_ScopedSandboxRoots() {
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

std::vector<uint8_t> T1C_BuildMqmbBinary(std::span<const ManifestRecord> records) {
    constexpr uint32_t kMagic = 0x424D514Du;
    const uint32_t count = static_cast<uint32_t>(records.size());
    std::vector<uint8_t> buf(8u, 0u);
    std::memcpy(buf.data(), &kMagic, 4u);
    std::memcpy(buf.data() + 4u, &count, 4u);
    for (const auto& r : records) {
        const uint16_t pathLen = static_cast<uint16_t>(r.virtualPath.size());
        const uint16_t hashLen = static_cast<uint16_t>(r.sha256Hex.size());
        const uint8_t bundleByte = (r.bundleType == BundleType::FirstPackage) ? 0u : 1u;
        const size_t recStart = buf.size();
        buf.resize(recStart + 12u + pathLen + hashLen, 0u);
        std::memcpy(buf.data() + recStart, &pathLen, 2u);
        std::memcpy(buf.data() + recStart + 2u, &hashLen, 2u);
        std::memcpy(buf.data() + recStart + 4u, &r.fileSize, 4u);
        std::memcpy(buf.data() + recStart + 8u, &r.assetFormat, 2u);
        buf[recStart + 10u] = bundleByte;
        std::memcpy(buf.data() + recStart + 12u, r.virtualPath.data(), pathLen);
        std::memcpy(buf.data() + recStart + 12u + pathLen, r.sha256Hex.data(), hashLen);
    }
    return buf;
}

std::vector<uint8_t> T1C_MakeRawRgbaPayload(uint32_t w, uint32_t h, uint8_t fillByte) {
    const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
    std::vector<uint8_t> out(8u + pixelBytes, fillByte);
    std::memcpy(out.data(), &w, 4u);
    std::memcpy(out.data() + 4u, &h, 4u);
    return out;
}

StatusOr<DecodedTextureData> T1C_DecodeRawRgba(
    const uint8_t* data,
    size_t size,
    const std::string& filePath) {
    (void)filePath;
    if (!data || size < 8u) {
        return InvalidArgumentError("RawRGBA 头部长度不足");
    }
    uint32_t w = 0;
    uint32_t h = 0;
    std::memcpy(&w, data, 4u);
    std::memcpy(&h, data + 4u, 4u);
    const size_t expected = static_cast<size_t>(w) * h * 4u;
    if (size < 8u + expected) {
        return InvalidArgumentError("RawRGBA 像素载荷截断");
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

std::vector<uint8_t> T1C_MakeKtx2Payload(uint32_t w, uint32_t h, uint32_t mipLevels) {
    const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
    std::vector<uint8_t> buf(68u + pixelBytes, 0xCCu);
    const uint8_t magic[12] = {0xABu, 'K', 'T', 'X', ' ', '2', '0', 0xBBu, '\r', '\n', 0x1Au, '\n'};
    std::memcpy(buf.data(), magic, 12u);
    std::memcpy(buf.data() + 20u, &w, 4u);
    std::memcpy(buf.data() + 24u, &h, 4u);
    std::memcpy(buf.data() + 40u, &mipLevels, 4u);
    return buf;
}

struct T1C_UploadOutcomeCapture {
    bool invoked{false};
    bool ok{false};
    TextureUploadResult result{};
};

struct T1C_UploadCaptureFn {
    T1C_UploadOutcomeCapture* target{nullptr};
    void operator()(StatusOr<TextureUploadResult> res) const {
        if (!target) {
            return;
        }
        target->invoked = true;
        target->ok = res.ok();
        if (res.ok()) {
            target->result = *res;
        }
    }
};

struct T1C_HotReplaceState {
    GpuResourceTable* gpuTable{nullptr};
    GPUScene* gpuScene{nullptr};
    uint32_t textureSlot{2u};
    uint32_t materialSlot{0u};
    uint32_t assignedSlot{2u};
    FCT::Vec4 uvScaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
    bool replaced{false};
};

struct T1C_HotReplaceCallbackFn {
    T1C_HotReplaceState* state{nullptr};
    void operator()(StatusOr<TextureUploadResult> res) const {
        if (!state || !res.ok()) {
            return;
        }
        uint32_t newSlot = 3u;
        if (state->gpuTable && res->textureHandle) {
            state->gpuTable->registerResource(
                res->textureHandle,
                GpuResourceType::Texture,
                res->totalBytes,
                res->debugName,
                GpuResourceState::Ready);
            newSlot = state->gpuTable->allocateBindlessSlot(res->textureHandle);
        }
        state->assignedSlot = newSlot;
        if (state->gpuScene) {
            GpuTextureRecord texRec{};
            texRec.uvScaleOffset = state->uvScaleOffset;
            texRec.bindlessOrPageIndex = newSlot;
            texRec.flags = 0u;
            state->gpuScene->updateTextureRecord(state->textureSlot, texRec);

            GpuMaterialRecord mat{};
            mat.baseColorFactor = FCT::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            mat.albedoTexIndex = newSlot;
            (void)state->gpuScene->uploadMaterialData(
                std::span<const GpuMaterialRecord>(&mat, 1u), state->materialSlot);
        }
        state->replaced = true;
    }
};

struct T1C_MqMeshReadCapture {
    bool called{false};
    bool ok{false};
    MqMeshHeader header{};
    std::vector<uint8_t> vb;
    std::vector<uint8_t> ib;
};

struct T1C_MqMeshReadFn {
    T1C_MqMeshReadCapture* out{nullptr};
    void operator()(StatusOr<MqMeshHeader> hdrOr, std::vector<uint8_t> vbBytes, std::vector<uint8_t> ibBytes) const {
        if (!out) {
            return;
        }
        out->called = true;
        out->ok = hdrOr.ok();
        if (hdrOr.ok()) {
            out->header = *hdrOr;
            out->vb = std::move(vbBytes);
            out->ib = std::move(ibBytes);
        }
    }
};

struct T1C_InlineContractState {
    std::atomic<uint32_t> executions{0};
    std::thread::id executedOn{};
};

struct T1C_SingleShotContractFn {
    T1C_InlineContractState* state{nullptr};
    void operator()() const {
        if (state) {
            state->executedOn = std::this_thread::get_id();
            state->executions.fetch_add(1u, std::memory_order_relaxed);
        }
    }
};

struct T1C_AtomicIncrementFn {
    std::atomic<uint32_t>* counter{nullptr};
    void operator()() const {
        if (counter) {
            counter->fetch_add(1u, std::memory_order_relaxed);
        }
    }
};

struct T1C_ChainContractState {
    std::atomic<uint32_t> clock{0};
    uint32_t step1Order{0};
    uint32_t step2Order{0};
    bcpp::work_contract step2Contract;
};

struct T1C_ChainStep1Fn {
    T1C_ChainContractState* state{nullptr};
    void operator()() const {
        if (state) {
            state->step1Order = state->clock.fetch_add(1u, std::memory_order_relaxed) + 1u;
            if (state->step2Contract.is_valid()) {
                state->step2Contract.schedule();
            }
        }
    }
};

struct T1C_ChainStep2Fn {
    T1C_ChainContractState* state{nullptr};
    void operator()() const {
        if (state) {
            state->step2Order = state->clock.fetch_add(1u, std::memory_order_relaxed) + 1u;
        }
    }
};

struct T1C_WorkerThreadRecordState {
    static constexpr uint32_t kTaskCount = 64u;
    std::atomic<uint32_t> completed{0};
    std::thread::id threadIds[kTaskCount]{};
};

struct T1C_RecordWorkerIdFn {
    T1C_WorkerThreadRecordState* state{nullptr};
    uint32_t slot{0};
    void operator()() const {
        if (state && slot < T1C_WorkerThreadRecordState::kTaskCount) {
            state->threadIds[slot] = std::this_thread::get_id();
            state->completed.fetch_add(1u, std::memory_order_release);
        }
    }
};

struct T1C_MainThreadPumpState {
    static constexpr uint32_t kCount = 16u;
    WorkHost* host{nullptr};
    std::atomic<uint32_t> workerEnqueued{0};
    std::atomic<uint32_t> mainExecuted{0};
    std::thread::id callbackThreads[kCount]{};
};

struct T1C_MainThreadCallbackFn {
    T1C_MainThreadPumpState* state{nullptr};
    uint32_t index{0};
    void operator()() const {
        if (state && index < T1C_MainThreadPumpState::kCount) {
            state->callbackThreads[index] = std::this_thread::get_id();
            state->mainExecuted.fetch_add(1u, std::memory_order_release);
        }
    }
};

struct T1C_WorkerEnqueueToMainFn {
    T1C_MainThreadPumpState* state{nullptr};
    uint32_t index{0};
    void operator()() const {
        if (state && state->host) {
            state->host->enqueueMainThread(T1C_MainThreadCallbackFn{state, index});
            state->workerEnqueued.fetch_add(1u, std::memory_order_release);
        }
    }
};

struct T1C_ActionCallbackRecord {
    uint32_t fireCount{0};
    InputValue lastValue{};
    ETriggerState lastState{ETriggerState::None};
};

struct T1C_ActionCallbackFn {
    T1C_ActionCallbackRecord* out{nullptr};
    void operator()(const InputValue& val, ETriggerState st) const {
        if (out) {
            ++out->fireCount;
            out->lastValue = val;
            out->lastState = st;
        }
    }
};

} // namespace

// ============================================================================
// [FEAT-16] 三级资产寻址协议 (AssetManifest + FCT::SAI::FileSystem)
// ============================================================================

E2E_TEST(Tier1, FEAT_16, AssetManifest_FlatBuffersManifestParsed) {
    const std::string hashA = AssetManifest::computeSha256Hex(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("hero_mesh"), 9u));
    const std::string hashB = AssetManifest::computeSha256Hex(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("albedo_tex"), 10u));

    const ManifestRecord records[2] = {
        ManifestRecord{"meshes/hero.mqmesh", hashA, 49152u, 1u, BundleType::FirstPackage},
        ManifestRecord{"textures/hero_albedo.ktx2", hashB, 262144u, 2u, BundleType::RemoteCache}
    };

    const std::vector<uint8_t> binaryBuf = T1C_BuildMqmbBinary(records);
    AssetManifest manifest;
    E2E_ASSERT_STATUS_OK(manifest.loadFromBuffer(binaryBuf));
    E2E_ASSERT_EQ(manifest.getRecordCount(), 2u);
    E2E_ASSERT_TRUE(manifest.hasEntry("meshes/hero.mqmesh"));
    E2E_ASSERT_TRUE(manifest.hasEntry("textures/hero_albedo.ktx2"));

    const ManifestRecord* rec = manifest.findRecord("textures/hero_albedo.ktx2");
    E2E_ASSERT_TRUE(rec != nullptr);
    E2E_ASSERT_EQ(rec->sha256Hex, hashB);
    E2E_ASSERT_EQ(rec->fileSize, 262144u);
    E2E_ASSERT_EQ(rec->assetFormat, 2u);
    E2E_ASSERT_EQ(rec->bundleType, BundleType::RemoteCache);
    return true;
}

E2E_TEST(Tier1, FEAT_16, ThreeTierAddressing_HitsPkgDirectoryFirst) {
    T1C_ScopedSandboxRoots sandbox("feat16_pkg");
    const uint8_t pkgPayload[4] = {'M', 'Q', 'M', 'S'};
    const std::string sha = AssetManifest::computeSha256Hex(pkgPayload);
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
        sandbox.pkgDir + "/meshes/sword.mqmesh", pkgPayload, sizeof(pkgPayload)));

    AssetManifest manifest;
    manifest.registerRecord(ManifestRecord{"meshes/sword.mqmesh", sha, 4u, 1u, BundleType::FirstPackage});

    auto resolvedOr = manifest.resolveAsset("meshes/sword.mqmesh");
    E2E_ASSERT_STATUS_OK(resolvedOr);
    E2E_ASSERT_EQ(resolvedOr->location, AssetResolveResult::Location::FirstPackagePkg);
    E2E_ASSERT_EQ(resolvedOr->resolvedPath, "/pkg/meshes/sword.mqmesh");
    E2E_ASSERT_TRUE(FCT::SAI::FileSystem::exists(resolvedOr->resolvedPath));
    return true;
}

E2E_TEST(Tier1, FEAT_16, ThreeTierAddressing_HitsLocalDiskCacheSecond) {
    T1C_ScopedSandboxRoots sandbox("feat16_cache");
    const uint8_t texBytes[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    const std::string sha = AssetManifest::computeSha256Hex(texBytes);
    const std::string virtualCachePath = "/cache/" + sha + ".bin";
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(virtualCachePath, texBytes, sizeof(texBytes)));

    AssetManifest manifest;
    manifest.registerRecord(ManifestRecord{"textures/shield.ktx2", sha, 8u, 2u, BundleType::RemoteCache});

    auto resolvedOr = manifest.resolveAsset("textures/shield.ktx2");
    E2E_ASSERT_STATUS_OK(resolvedOr);
    E2E_ASSERT_EQ(resolvedOr->location, AssetResolveResult::Location::LocalCache);
    E2E_ASSERT_EQ(resolvedOr->resolvedPath, virtualCachePath);
    E2E_ASSERT_TRUE(resolvedOr->downloadUrl.empty());
    return true;
}

E2E_TEST(Tier1, FEAT_16, ThreeTierAddressing_FallsBackToCdnDownload) {
    T1C_ScopedSandboxRoots sandbox("feat16_cdn");
    const uint8_t remoteSample[6] = {'r', 'e', 'm', 'o', 't', 'e'};
    const std::string sha = AssetManifest::computeSha256Hex(remoteSample);

    AssetManifest manifest;
    manifest.setCdnBaseUrl("https://cdn.mqengine.test/v2/");
    manifest.registerRecord(ManifestRecord{"textures/skybox.ktx2", sha, 6u, 2u, BundleType::RemoteCache});

    auto resolvedOr = manifest.resolveAsset("textures/skybox.ktx2");
    E2E_ASSERT_STATUS_OK(resolvedOr);
    E2E_ASSERT_EQ(resolvedOr->location, AssetResolveResult::Location::RemoteCdn);
    E2E_ASSERT_EQ(resolvedOr->downloadUrl, "https://cdn.mqengine.test/v2/" + sha + ".bin");
    E2E_ASSERT_EQ(resolvedOr->cacheDestPath, "/cache/" + sha + ".bin");
    return true;
}

E2E_TEST(Tier1, FEAT_16, ThreeTierAddressing_ValidatesDownloadedHash) {
    const uint8_t abc[3] = {'a', 'b', 'c'};
    const std::string nistAbcHex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    E2E_ASSERT_EQ(AssetManifest::computeSha256Hex(abc), nistAbcHex);
    E2E_ASSERT_TRUE(AssetManifest::verifySha256(abc, nistAbcHex));

    const uint8_t tampered[3] = {'a', 'b', 'x'};
    E2E_ASSERT_FALSE(AssetManifest::verifySha256(tampered, nistAbcHex));
    return true;
}

// ============================================================================
// [FEAT-17] 纹理占位秒开与无感热替换 (AsyncTextureUploader + GpuResourceTable + GPUScene)
// ============================================================================

E2E_TEST(Tier1, FEAT_17, Placeholder_ReturnsValidSlotImmediately) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());

    const uint32_t texHandle = uploader.requestTextureWithPlaceholder("textures/deferred_remote_01.ktx2");
    E2E_ASSERT_GE(texHandle, 10u);
    E2E_ASSERT_EQ(uploader.getEffectiveTextureSlot(texHandle), 2u);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_17, Placeholder_PointsToBuiltin1x1Slot) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());

    const uint32_t texHandle = uploader.requestTextureWithPlaceholder("textures/pending_wall.ktx2");
    E2E_ASSERT_GE(texHandle, 10u);
    const uint32_t placeholderSlot = uploader.getEffectiveTextureSlot(texHandle);
    E2E_ASSERT_EQ(placeholderSlot, 2u);

    GpuResourceTable gpuTable;
    const uint32_t builtinDummyTex = 0xFFFFFFFFu;
    gpuTable.setDefaultFallbackHandle(GpuResourceType::Texture, &builtinDummyTex);
    E2E_ASSERT_EQ(gpuTable.resolveHandle(nullptr, GpuResourceType::Texture), static_cast<const void*>(&builtinDummyTex));

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_17, Placeholder_FirstFrameRendersWithoutBlocking) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());

    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    const uint32_t texHandle = uploader.requestTextureWithPlaceholder("textures/streaming_armor.ktx2");
    const uint32_t slot = uploader.getEffectiveTextureSlot(texHandle);
    E2E_ASSERT_EQ(slot, 2u);
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 0u);

    const GpuTextureRecord placeholderRec = scene.getTextureRecord(slot);
    E2E_ASSERT_EQ(placeholderRec.bindlessOrPageIndex, 2u);
    E2E_ASSERT_EQ(placeholderRec.flags, 1u);

    GpuMaterialRecord firstFrameMat{};
    firstFrameMat.albedoTexIndex = slot;
    E2E_ASSERT_STATUS_OK(
        scene.uploadMaterialData(std::span<const GpuMaterialRecord>(&firstFrameMat, 1u), 0u));

    scene.shutdown();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_17, HotReplacement_UpdatesSlotOnDownloadComplete) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    GpuResourceTable gpuTable;
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    GpuMaterialRecord initialMat{};
    initialMat.albedoTexIndex = 2u;
    E2E_ASSERT_STATUS_OK(
        scene.uploadMaterialData(std::span<const GpuMaterialRecord>(&initialMat, 1u), 0u));
    E2E_ASSERT_EQ(scene.getTextureRecord(2u).flags, 1u);

    T1C_HotReplaceState state{};
    state.gpuTable = &gpuTable;
    state.gpuScene = &scene;
    state.textureSlot = 2u;
    state.materialSlot = 0u;

    TextureUploadDesc desc{};
    desc.filePath = "textures/hero_albedo.rawrgba";
    desc.debugName = "HeroAlbedoHotSwap";
    desc.memoryData = T1C_MakeRawRgbaPayload(32u, 32u, 0xAAu);

    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), T1C_HotReplaceCallbackFn{&state}));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_TRUE(state.replaced);
    E2E_ASSERT_GT(state.assignedSlot, 2u);
    const GpuTextureRecord updatedRec = scene.getTextureRecord(2u);
    E2E_ASSERT_EQ(updatedRec.bindlessOrPageIndex, state.assignedSlot);
    E2E_ASSERT_EQ(updatedRec.flags, 0u);

    scene.shutdown();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_17, HotReplacement_RefreshesUvScaleOffsetInTable) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    AtlasShelfPacker atlasPacker(1024u, 1024u, 1u);
    (void)atlasPacker.allocate(256u, 256u);
    const auto subRect = atlasPacker.allocate(256u, 256u);
    E2E_ASSERT_TRUE(subRect.success);
    E2E_ASSERT_EQ(subRect.x, 256u);
    E2E_ASSERT_EQ(subRect.y, 0u);

    GpuResourceTable gpuTable;
    GPUScene scene;
    E2E_ASSERT_STATUS_OK(scene.init(nullptr));

    T1C_HotReplaceState state{};
    state.gpuTable = &gpuTable;
    state.gpuScene = &scene;
    state.textureSlot = 2u;
    state.materialSlot = 0u;
    state.uvScaleOffset = FCT::Vec4{
        static_cast<float>(subRect.width) / 1024.0f,
        static_cast<float>(subRect.height) / 1024.0f,
        static_cast<float>(subRect.x) / 1024.0f,
        static_cast<float>(subRect.y) / 1024.0f
    };

    TextureUploadDesc desc{};
    desc.filePath = "textures/atlas_tile.rawrgba";
    desc.memoryData = T1C_MakeRawRgbaPayload(16u, 16u, 0x7Fu);
    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), T1C_HotReplaceCallbackFn{&state}));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    const GpuTextureRecord updated = scene.getTextureRecord(2u);
    E2E_ASSERT_TRUE(std::abs(updated.uvScaleOffset.x - 0.25f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(updated.uvScaleOffset.z - 0.25f) < 1e-5f);

    scene.shutdown();
    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

// ============================================================================
// [FEAT-18] 免 Assimp 二进制网格直通协议 (.mqmesh / AsyncLoader)
// ============================================================================

E2E_TEST(Tier1, FEAT_18, MqMesh_BinaryHeaderValidated) {
    MqMeshHeader hdr{};
    hdr.magic = 0x4D514D53u;
    hdr.version = 1u;
    hdr.vertexCount = 3u;
    hdr.indexCount = 3u;
    hdr.subMeshCount = 1u;
    hdr.vertexStride = sizeof(StandardMeshVertex);
    hdr.aabbMin[0] = -1.0f;
    hdr.aabbMax[0] = 1.0f;

    std::vector<StandardMeshVertex> verts(3u);
    const uint32_t indices[3] = {0u, 1u, 2u};
    auto bytesOr = AsyncLoader::serializeMqMesh(
        hdr,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(verts.data()), verts.size() * sizeof(StandardMeshVertex)),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices), sizeof(indices)));
    E2E_ASSERT_STATUS_OK(bytesOr);

    auto parsedOr = AsyncLoader::parseMqMeshBuffer(*bytesOr);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_EQ(parsedOr->header.magic, 0x4D514D53u);
    E2E_ASSERT_EQ(parsedOr->header.version, 1u);
    E2E_ASSERT_EQ(parsedOr->header.vertexCount, 3u);
    E2E_ASSERT_EQ(parsedOr->header.vertexStride, 48u);
    return true;
}

E2E_TEST(Tier1, FEAT_18, MqMesh_DirectStreamsToStandardVertexLayout) {
    E2E_ASSERT_EQ(sizeof(StandardMeshVertex), 48u);
    T1C_ScopedSandboxRoots sandbox("feat18_stream");

    StandardMeshVertex quadVerts[4]{};
    for (uint32_t i = 0; i < 4u; ++i) {
        quadVerts[i].position[0] = static_cast<float>(i) * 2.0f;
        quadVerts[i].normal[1] = 1.0f;
        quadVerts[i].tangent[0] = 1.0f;
        quadVerts[i].tangent[3] = 1.0f;
        quadVerts[i].texCoord[0] = 0.5f * static_cast<float>(i);
    }
    const uint32_t quadIndices[6] = {0u, 1u, 2u, 2u, 1u, 3u};

    MqMeshHeader hdr{};
    hdr.vertexCount = 4u;
    hdr.indexCount = 6u;
    hdr.vertexStride = sizeof(StandardMeshVertex);

    auto serializedOr = AsyncLoader::serializeMqMesh(
        hdr,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(quadVerts), sizeof(quadVerts)),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(quadIndices), sizeof(quadIndices)));
    E2E_ASSERT_STATUS_OK(serializedOr);

    const std::string meshPath = sandbox.baseDir + "/quad.mqmesh";
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile(
        meshPath, serializedOr->data(), serializedOr->size()));

    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    T1C_MqMeshReadCapture capture{};
    E2E_ASSERT_STATUS_OK(loader.submitAsyncMqMeshRead(meshPath, T1C_MqMeshReadFn{&capture}));
    workHost.drainInline(100000u);

    E2E_ASSERT_TRUE(capture.called);
    E2E_ASSERT_TRUE(capture.ok);
    E2E_ASSERT_EQ(capture.vb.size(), 4u * 48u);
    StandardMeshVertex v3{};
    std::memcpy(&v3, capture.vb.data() + 3u * sizeof(StandardMeshVertex), sizeof(StandardMeshVertex));
    E2E_ASSERT_TRUE(std::abs(v3.position[0] - 6.0f) < 1e-5f);

    loader.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_18, MqMesh_IndexBufferAndSubmeshesParsed) {
    MqMeshHeader hdr{};
    hdr.vertexCount = 6u;
    hdr.indexCount = 6u;
    hdr.subMeshCount = 2u;
    hdr.vertexStride = sizeof(StandardMeshVertex);

    std::vector<StandardMeshVertex> verts(6u);
    const uint32_t indices[6] = {0u, 1u, 2u, 3u, 4u, 5u};
    const SubMeshRange subMeshes[2] = {
        SubMeshRange{0u, 3u, 0u, 0u},
        SubMeshRange{3u, 3u, 0u, 7u}
    };

    auto serializedOr = AsyncLoader::serializeMqMesh(
        hdr,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(verts.data()), verts.size() * sizeof(StandardMeshVertex)),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices), sizeof(indices)),
        subMeshes);
    E2E_ASSERT_STATUS_OK(serializedOr);

    auto parsedOr = AsyncLoader::parseMqMeshBuffer(*serializedOr);
    E2E_ASSERT_STATUS_OK(parsedOr);
    E2E_ASSERT_TRUE(parsedOr->uses32BitIndices);
    E2E_ASSERT_EQ(parsedOr->subMeshes.size(), 2u);
    E2E_ASSERT_EQ(parsedOr->subMeshes[1].indexOffset, 3u);
    E2E_ASSERT_EQ(parsedOr->subMeshes[1].materialIndex, 7u);
    return true;
}

E2E_TEST(Tier1, FEAT_18, MqMesh_DirectGpuBufferCreation) {
    MqMeshHeader hdr{};
    hdr.vertexCount = 3u;
    hdr.indexCount = 3u;
    hdr.vertexStride = sizeof(StandardMeshVertex);

    StandardMeshVertex verts[3]{};
    verts[2].position[2] = 9.5f;
    const uint32_t indices[3] = {0u, 1u, 2u};

    auto serializedOr = AsyncLoader::serializeMqMesh(
        hdr,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(verts), sizeof(verts)),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices), sizeof(indices)));
    E2E_ASSERT_STATUS_OK(serializedOr);

    auto parsedOr = AsyncLoader::parseMqMeshBuffer(*serializedOr);
    E2E_ASSERT_STATUS_OK(parsedOr);

    FCT::RHI::WGL2_StorageBuffer gpuVb(nullptr);
    E2E_ASSERT_STATUS_OK(gpuVb.create(parsedOr->vertexBytes.size()));
    gpuVb.updateData(parsedOr->vertexBytes.data(), parsedOr->vertexBytes.size(), 0u);

    FCT::RHI::WGL2_StorageBuffer gpuIb(nullptr);
    E2E_ASSERT_STATUS_OK(gpuIb.create(parsedOr->indexBytes.size()));
    std::vector<uint8_t> alignedIb = parsedOr->indexBytes;
    alignedIb.resize(16u, 0u);
    gpuIb.updateData(alignedIb.data(), alignedIb.size(), 0u);

    E2E_ASSERT_EQ(gpuVb.size(), 144u);
    E2E_ASSERT_EQ(gpuIb.size(), 16u);
    E2E_ASSERT_NE(gpuVb.map(), nullptr);
    E2E_ASSERT_EQ(std::memcmp(gpuVb.map(), parsedOr->vertexBytes.data(), 144u), 0);
    return true;
}

E2E_TEST(Tier1, FEAT_18, MqMesh_ZeroAssimpDependency) {
    E2E_ASSERT_EQ(sizeof(MqMeshHeader), 48u);
    E2E_ASSERT_EQ(sizeof(SubMeshRange), 16u);

    MqMeshHeader hdr{};
    hdr.vertexCount = 3u;
    hdr.indexCount = 3u;
    hdr.vertexStride = sizeof(StandardMeshVertex);

    StandardMeshVertex verts[3]{};
    const uint16_t indices16[3] = {0u, 1u, 2u};

    auto serialized16Or = AsyncLoader::serializeMqMesh(
        hdr,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(verts), sizeof(verts)),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(indices16), sizeof(indices16)));
    E2E_ASSERT_STATUS_OK(serialized16Or);

    auto parsed16Or = AsyncLoader::parseMqMeshBuffer(*serialized16Or);
    E2E_ASSERT_STATUS_OK(parsed16Or);
    E2E_ASSERT_FALSE(parsed16Or->uses32BitIndices);
    E2E_ASSERT_EQ(parsed16Or->indexBytes.size(), 6u);
    return true;
}

// ============================================================================
// [FEAT-19] 显存上传带宽分帧限流 (AsyncTextureUploader <= 1.5MB/frame)
// ============================================================================

E2E_TEST(Tier1, FEAT_19, TextureUploader_EnforcesPerFrameBudget) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    TextureUploadDesc desc{};
    desc.filePath = "textures/budget_1mb.rawrgba";
    desc.memoryData = T1C_MakeRawRgbaPayload(512u, 512u, 0x80u);

    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), nullptr));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    constexpr uint64_t kFrameBudgetBytes = 1536ULL * 1024ULL;
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 1u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 1048576ULL);
    E2E_ASSERT_LE(uploader.getTotalUploadedBytes(), kFrameBudgetBytes);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_19, TextureUploader_DefersOverBudgetTasksToNextFrame) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    for (int i = 0; i < 2; ++i) {
        TextureUploadDesc desc{};
        desc.filePath = "textures/defer_" + std::to_string(i) + ".rawrgba";
        desc.memoryData = T1C_MakeRawRgbaPayload(512u, 512u, static_cast<uint8_t>(0x20 + i));
        E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), nullptr));
    }

    // 提交瞬间零主线程同步解码与上传，直到帧内联泵驱动完成
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 0u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 0u);

    workHost.drainInline(200000u);
    uploader.tick(0.016f);
    E2E_ASSERT_EQ(uploader.getCompletedCount(), 2u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 2097152ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_19, TextureUploader_BatchesMultipleSmallTextures) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    for (uint32_t i = 0; i < 8u; ++i) {
        TextureUploadDesc desc{};
        desc.filePath = "textures/icon_" + std::to_string(i) + ".rawrgba";
        desc.memoryData = T1C_MakeRawRgbaPayload(32u, 32u, static_cast<uint8_t>(i));
        E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), nullptr));
    }

    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_EQ(uploader.getCompletedCount(), 8u);
    E2E_ASSERT_EQ(uploader.getTotalUploadedBytes(), 8ULL * 4096ULL);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_19, TextureUploader_CopiesCompressedBlocksToAtlas) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());

    E2E_ASSERT_TRUE(uploader.isExtensionSupported(".ktx2"));
    E2E_ASSERT_TRUE(uploader.isExtensionSupported(".astc"));

    KtxContainerTextureDecoder ktxDecoder;
    const std::vector<uint8_t> ktx2Bytes = T1C_MakeKtx2Payload(64u, 64u, 3u);
    E2E_ASSERT_TRUE(ktxDecoder.canDecodeHeader(ktx2Bytes.data(), ktx2Bytes.size()));

    AtlasShelfPacker atlas(1024u, 1024u, 2u);
    const auto tileAlloc = atlas.allocate(64u, 64u);
    E2E_ASSERT_TRUE(tileAlloc.success);

    T1C_UploadOutcomeCapture out{};
    TextureUploadDesc desc{};
    desc.filePath = "textures/compressed_tile.ktx2";
    desc.memoryData = ktx2Bytes;
    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), T1C_UploadCaptureFn{&out}));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_TRUE(out.ok);
    E2E_ASSERT_EQ(out.result.width, 64u);
    E2E_ASSERT_EQ(out.result.height, 64u);
    E2E_ASSERT_EQ(out.result.mipLevels, 3u);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

E2E_TEST(Tier1, FEAT_19, TextureUploader_InvokesCallbackOnCompletion) {
    WorkHost workHost;
    E2E_ASSERT_STATUS_OK(workHost.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    TextureDecoderRegistry decoderReg;
    E2E_ASSERT_STATUS_OK(decoderReg.init());
    AsyncTextureUploader uploader;
    E2E_ASSERT_STATUS_OK(uploader.init());
    uploader.registerFormatDecoder(".rawrgba", T1C_DecodeRawRgba);

    T1C_UploadOutcomeCapture out{};
    TextureUploadDesc desc{};
    desc.filePath = "textures/cb_verify.rawrgba";
    desc.debugName = "T1_CallbackTex";
    desc.generateMips = true;
    desc.memoryData = T1C_MakeRawRgbaPayload(64u, 64u, 0xFEu);

    E2E_ASSERT_STATUS_OK(uploader.uploadTextureAsync(std::move(desc), T1C_UploadCaptureFn{&out}));
    workHost.drainInline(100000u);
    uploader.tick(0.016f);

    E2E_ASSERT_TRUE(out.invoked);
    E2E_ASSERT_TRUE(out.ok);
    E2E_ASSERT_TRUE(out.result.textureHandle != nullptr);
    E2E_ASSERT_EQ(out.result.debugName, "T1_CallbackTex");
    E2E_ASSERT_GE(out.result.mipLevels, 4u);

    uploader.term();
    decoderReg.term();
    workHost.term();
    return true;
}

// ============================================================================
// [FEAT-20] 200MB 沙箱磁盘配额治理与 LRU 自动淘汰 (AsyncLoader)
// ============================================================================

E2E_TEST(Tier1, FEAT_20, DiskCache_UsageLedgerTrackedAccurately) {
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("sha_entry_a", 40ULL * kMb);
    loader.recordCacheAccess("sha_entry_b", 65ULL * kMb);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 105ULL * kMb);
    E2E_ASSERT_TRUE(loader.hasCacheEntry("sha_entry_a"));

    // 同一哈希条目更新字节尺寸后账本净差额自动修正
    loader.recordCacheAccess("sha_entry_a", 50ULL * kMb);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 115ULL * kMb);

    loader.term();
    return true;
}

E2E_TEST(Tier1, FEAT_20, DiskCache_EvictsLruWhenExceedingWatermark) {
    T1C_ScopedSandboxRoots sandbox("feat20_evict");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    const uint8_t dummy[4] = {0x1u, 0x2u, 0x3u, 0x4u};
    E2E_ASSERT_STATUS_OK(FCT::SAI::FileSystem::writeBinaryFile("/cache/evict_01.bin", dummy, sizeof(dummy)));

    loader.recordCacheAccess("evict_01", 60ULL * kMb);
    loader.recordCacheAccess("evict_02", 70ULL * kMb);
    loader.recordCacheAccess("evict_03", 60ULL * kMb);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 190ULL * kMb);

    loader.enforceCacheQuota();
    E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), AsyncLoader::DefaultCacheLowWatermarkBytes);
    E2E_ASSERT_FALSE(loader.hasCacheEntry("evict_01"));
    E2E_ASSERT_FALSE(FCT::SAI::FileSystem::exists("/cache/evict_01.bin"));

    loader.term();
    return true;
}

E2E_TEST(Tier1, FEAT_20, DiskCache_OldestUnaccessedEvictedFirst) {
    T1C_ScopedSandboxRoots sandbox("feat20_lru");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("asset_first", 60ULL * kMb);
    loader.recordCacheAccess("asset_second", 60ULL * kMb);
    loader.recordCacheAccess("asset_third", 70ULL * kMb);

    // 访问刷新 asset_first，使 asset_second 成为最久未访问条目
    loader.updateCacheAccessTime("asset_first");
    loader.enforceCacheQuota();

    E2E_ASSERT_FALSE(loader.hasCacheEntry("asset_second"));
    E2E_ASSERT_TRUE(loader.hasCacheEntry("asset_first"));
    E2E_ASSERT_TRUE(loader.hasCacheEntry("asset_third"));

    loader.term();
    return true;
}

E2E_TEST(Tier1, FEAT_20, DiskCache_TriggeredByMemoryWarningEvent) {
    T1C_ScopedSandboxRoots sandbox("feat20_memwarn");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("warn_a", 50ULL * kMb);
    loader.recordCacheAccess("warn_b", 55ULL * kMb);
    loader.recordCacheAccess("warn_c", 55ULL * kMb);
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 160ULL * kMb);

    // 常规 180MB 水位下不触发淘汰，紧急内存告警将缓存收缩至 80MB 以下
    loader.enforceCacheQuota();
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 160ULL * kMb);

    loader.onMemoryWarning(2);
    E2E_ASSERT_LE(loader.getTotalCacheUsageBytes(), 80ULL * kMb);

    loader.term();
    return true;
}

E2E_TEST(Tier1, FEAT_20, DiskCache_ActiveAssetsProtectedFromEviction) {
    T1C_ScopedSandboxRoots sandbox("feat20_pin");
    AsyncLoader loader;
    E2E_ASSERT_STATUS_OK(loader.init());

    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    loader.recordCacheAccess("active_scene_mesh", 80ULL * kMb);
    loader.pinCacheEntry("active_scene_mesh");
    loader.recordCacheAccess("inactive_old_bundle", 110ULL * kMb);

    loader.enforceCacheQuota();
    E2E_ASSERT_TRUE(loader.hasCacheEntry("active_scene_mesh"));
    E2E_ASSERT_FALSE(loader.hasCacheEntry("inactive_old_bundle"));
    E2E_ASSERT_EQ(loader.getTotalCacheUsageBytes(), 80ULL * kMb);

    loader.term();
    return true;
}

// ============================================================================
// [FEAT-21] WorkHost 单线程时间片内联模式 (SingleThreadInline)
// ============================================================================

E2E_TEST(Tier1, FEAT_21, WorkHost_InitializesSingleThreadInlineMode) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));
    E2E_ASSERT_TRUE(host.isRunning());
    E2E_ASSERT_EQ(host.getMode(), WorkHost::Mode::SingleThreadInline);
    E2E_ASSERT_EQ(host.getWorkerCount(), 0u);
    E2E_ASSERT_EQ(host.getIoThreadCount(), 0u);
    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_21, WorkHost_ExecutesContractsInlineWithoutWorkers) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    T1C_InlineContractState state{};
    bcpp::work_contract contract = host.createContract(
        T1C_SingleShotContractFn{&state},
        bcpp::work_contract::initial_state::scheduled);

    E2E_ASSERT_EQ(host.getWorkerCount(), 0u);
    E2E_ASSERT_EQ(state.executions.load(std::memory_order_relaxed), 0u);

    const bool ran = host.executeNextContract();
    E2E_ASSERT_TRUE(ran);
    E2E_ASSERT_EQ(state.executions.load(std::memory_order_relaxed), 1u);
    E2E_ASSERT_EQ(state.executedOn, std::this_thread::get_id());

    contract.release();
    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_21, WorkHost_DrainsAllContractsAtSyncPoint) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    std::atomic<uint32_t> counter{0};
    for (uint32_t i = 0; i < 32u; ++i) {
        E2E_ASSERT_STATUS_OK(host.postTask(T1C_AtomicIncrementFn{&counter}));
    }
    E2E_ASSERT_EQ(counter.load(std::memory_order_relaxed), 0u);

    host.waitAll();
    E2E_ASSERT_EQ(counter.load(std::memory_order_relaxed), 32u);

    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_21, WorkHost_AsyncIoDelegatedWithoutBlocking) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    std::atomic<uint32_t> ioExecuted{0};
    std::atomic<uint32_t> pollerTicks{0};
    host.registerIoPoller("T1C_Poller", T1C_AtomicIncrementFn{&pollerTicks});
    E2E_ASSERT_STATUS_OK(host.postIoTask(T1C_AtomicIncrementFn{&ioExecuted}));
    E2E_ASSERT_EQ(ioExecuted.load(std::memory_order_relaxed), 0u);

    host.drainInline(50000u);
    E2E_ASSERT_EQ(ioExecuted.load(std::memory_order_relaxed), 1u);
    E2E_ASSERT_GE(pollerTicks.load(std::memory_order_relaxed), 1u);

    host.unregisterIoPoller("T1C_Poller");
    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_21, WorkHost_ZeroMutexDeadlockFree) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::SingleThreadInline, 0u, 0u));

    T1C_ChainContractState chain{};
    chain.step2Contract = host.createContract(
        T1C_ChainStep2Fn{&chain},
        bcpp::work_contract::initial_state::unscheduled);
    bcpp::work_contract step1Contract = host.createContract(
        T1C_ChainStep1Fn{&chain},
        bcpp::work_contract::initial_state::scheduled);

    host.drainInline(50000u);
    E2E_ASSERT_EQ(chain.step1Order, 1u);
    E2E_ASSERT_EQ(chain.step2Order, 2u);

    step1Contract.release();
    chain.step2Contract.release();
    host.term();
    return true;
}

// ============================================================================
// [FEAT-22] WorkHost 多线程工作线程组模式 (MultiThreadedWorkers)
// ============================================================================

E2E_TEST(Tier1, FEAT_22, WorkHost_SpawnsConfiguredWorkerThreads) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 3u, 1u));
    E2E_ASSERT_TRUE(host.isRunning());
    E2E_ASSERT_EQ(host.getMode(), WorkHost::Mode::MultiThreadedWorkers);
    E2E_ASSERT_EQ(host.getWorkerCount(), 3u);
    E2E_ASSERT_EQ(host.getIoThreadCount(), 1u);
    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_22, WorkHost_DistributesContractsAcrossWorkers) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 4u, 1u));

    T1C_WorkerThreadRecordState state{};
    for (uint32_t i = 0; i < T1C_WorkerThreadRecordState::kTaskCount; ++i) {
        E2E_ASSERT_STATUS_OK(host.postTask(T1C_RecordWorkerIdFn{&state, i}));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (state.completed.load(std::memory_order_acquire) < T1C_WorkerThreadRecordState::kTaskCount &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    E2E_ASSERT_EQ(state.completed.load(std::memory_order_acquire), T1C_WorkerThreadRecordState::kTaskCount);
    const std::thread::id mainTid = std::this_thread::get_id();
    uint32_t offMainCount = 0u;
    for (uint32_t i = 0; i < T1C_WorkerThreadRecordState::kTaskCount; ++i) {
        if (state.threadIds[i] != mainTid) {
            ++offMainCount;
        }
    }
    E2E_ASSERT_GT(offMainCount, 0u);

    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_22, WorkHost_LockfreeQueueOperation) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));

    std::atomic<uint32_t> cpuDone{0};
    std::atomic<uint32_t> ioDone{0};
    for (uint32_t i = 0; i < 128u; ++i) {
        E2E_ASSERT_STATUS_OK(host.postTask(T1C_AtomicIncrementFn{&cpuDone}));
    }
    for (uint32_t i = 0; i < 32u; ++i) {
        E2E_ASSERT_STATUS_OK(host.postIoTask(T1C_AtomicIncrementFn{&ioDone}));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((cpuDone.load(std::memory_order_acquire) < 128u || ioDone.load(std::memory_order_acquire) < 32u) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    E2E_ASSERT_EQ(cpuDone.load(std::memory_order_acquire), 128u);
    E2E_ASSERT_EQ(ioDone.load(std::memory_order_acquire), 32u);

    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_22, WorkHost_PumpsCompletionCallbacksToMainThread) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));

    T1C_MainThreadPumpState state{};
    state.host = &host;
    for (uint32_t i = 0; i < T1C_MainThreadPumpState::kCount; ++i) {
        E2E_ASSERT_STATUS_OK(host.postTask(T1C_WorkerEnqueueToMainFn{&state, i}));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (state.workerEnqueued.load(std::memory_order_acquire) < T1C_MainThreadPumpState::kCount &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    E2E_ASSERT_EQ(state.workerEnqueued.load(std::memory_order_acquire), T1C_MainThreadPumpState::kCount);
    E2E_ASSERT_EQ(state.mainExecuted.load(std::memory_order_acquire), 0u);

    const std::thread::id mainTid = std::this_thread::get_id();
    const size_t pumped = host.pumpMainThreadTasks(64u);
    E2E_ASSERT_EQ(pumped, T1C_MainThreadPumpState::kCount);
    for (uint32_t i = 0; i < T1C_MainThreadPumpState::kCount; ++i) {
        E2E_ASSERT_EQ(state.callbackThreads[i], mainTid);
    }

    host.term();
    return true;
}

E2E_TEST(Tier1, FEAT_22, WorkHost_CleanShutdownJoinsWorkers) {
    WorkHost host;
    E2E_ASSERT_STATUS_OK(host.init(WorkHost::Mode::MultiThreadedWorkers, 2u, 1u));
    E2E_ASSERT_EQ(host.getWorkerCount(), 2u);

    host.term();
    E2E_ASSERT_FALSE(host.isRunning());
    E2E_ASSERT_EQ(host.getWorkerCount(), 0u);
    E2E_ASSERT_EQ(host.getIoThreadCount(), 0u);

    std::atomic<uint32_t> neverRun{0};
    Status postAfterTerm = host.postTask(T1C_AtomicIncrementFn{&neverRun});
    E2E_ASSERT_FALSE(postAfterTerm.ok());
    return true;
}

// ============================================================================
// [FEAT-55] 封闭游戏内文字与开放 UI 文字物理分轨架构 (FontTrackClassifier)
// ============================================================================

E2E_TEST(Tier1, FEAT_55, FontDualTrack_IdentifiesTrackAByClosedCharset) {
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("-9999!"), FontTrack::InGameBaked);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("+250% CRIT"), FontTrack::InGameBaked);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("暴击 987654 闪避 格挡 免疫"), FontTrack::InGameBaked);
    return true;
}

E2E_TEST(Tier1, FEAT_55, FontDualTrack_IdentifiesTrackBByOpenCharset) {
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("玩家昵称：星穹旅人"), FontTrack::UiRuntime);
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack("任务提示：前往东门广场"), FontTrack::UiRuntime);
    return true;
}

E2E_TEST(Tier1, FEAT_55, FontDualTrack_TrackARoutesToStaticMsdf) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    fontSys.getBakedFont().initDefaultCombatPreset(512u, 512u);

    const std::string_view combatStr = "暴击 -12800!";
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(combatStr), FontTrack::InGameBaked);

    fontSys.getInGameText().spawnDamageNumber(std::string(combatStr), FCT::Vec3<>{0.0f, 2.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, true);
    std::vector<GpuInGameTextQuadInstance> quads;
    fontSys.getInGameText().buildQuadInstances(fontSys.getBakedFont(), quads);

    E2E_ASSERT_GT(quads.size(), 0u);
    E2E_ASSERT_EQ(fontSys.getBakedFont().getFreeTypeInvocationCount(), 0u);
    return true;
}

E2E_TEST(Tier1, FEAT_55, FontDualTrack_TrackBRoutesToDynamicAtlas) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    const std::string_view chatText = "世界频道：组队刷副本";
    E2E_ASSERT_EQ(FontTrackClassifier::classifyTrack(chatText), FontTrack::UiRuntime);

    const char32_t cps[3] = {U'世', U'界', U'频'};
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(cps, 20u));

    CachedUiGlyph glyph{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'世', 20u, glyph));
    E2E_ASSERT_GT(glyph.metric.advanceX, 0.0f);
    return true;
}

E2E_TEST(Tier1, FEAT_55, FontDualTrack_EmitsDistinctDrawBuckets) {
    const uint64_t trackAMask = InputTags::Text | InputTags::InGameBaked;
    const uint64_t trackBMask = InputTags::UI | InputTags::RmlUi;
    const DrawBucket trackABucket(0u, trackAMask, 512u);
    const DrawBucket trackBBucket(0u, trackBMask, 128u);

    E2E_ASSERT_NE(trackABucket.inputTagsMask, trackBBucket.inputTagsMask);
    E2E_ASSERT_EQ(trackABucket.inputTagsMask & trackAMask, trackAMask);
    E2E_ASSERT_EQ(trackABucket.inputTagsMask & InputTags::RmlUi, 0ULL);
    E2E_ASSERT_EQ(trackBBucket.inputTagsMask & trackBMask, trackBMask);
    return true;
}

// ============================================================================
// [FEAT-56] 离线预烘焙 .mqfont 与 MSDF 多通道距离场 (BakedFont / MsdfGlyphBitmap)
// ============================================================================

E2E_TEST(Tier1, FEAT_56, MqFont_ExtractsClosedCharsetTable) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);

    E2E_ASSERT_TRUE(font.isLoaded());
    E2E_ASSERT_GE(font.getClosedCharset().size(), 20u);
    E2E_ASSERT_TRUE(font.hasGlyph(U'9'));
    E2E_ASSERT_TRUE(font.hasGlyph(U'暴'));
    E2E_ASSERT_TRUE(font.hasGlyph(U'免'));
    E2E_ASSERT_FALSE(font.hasGlyph(U'龘'));
    return true;
}

E2E_TEST(Tier1, FEAT_56, MqFont_GeneratesMsdfGlyphDistanceFields) {
    const MsdfGlyphBitmap msdf = MsdfGlyphBitmap::generate(U'8', 32u, 32u, 4.0f);
    E2E_ASSERT_EQ(msdf.channels, 3u);
    E2E_ASSERT_EQ(msdf.distanceField.size(), 32u * 32u * 3u);

    const float centerDist = msdf.sampleDistance(16u, 16u, 1u);
    const float cornerDist = msdf.sampleDistance(0u, 0u, 1u);
    E2E_ASSERT_GT(centerDist, 0.5f);
    E2E_ASSERT_LT(cornerDist, 0.5f);
    return true;
}

E2E_TEST(Tier1, FEAT_56, MqFont_SerializesBinaryMetricTable) {
    BakedFont srcFont;
    srcFont.initDefaultCombatPreset(512u, 512u);
    srcFont.setAtlasPixels(std::vector<uint8_t>(256u, 0x7Fu));

    const std::vector<uint8_t> blob = srcFont.saveToBuffer();
    E2E_ASSERT_GT(blob.size(), sizeof(MqFontBinaryHeader));

    MqFontBinaryHeader hdr{};
    std::memcpy(&hdr, blob.data(), sizeof(MqFontBinaryHeader));
    E2E_ASSERT_EQ(hdr.magic, BakedFont::Magic);

    BakedFont dstFont;
    E2E_ASSERT_STATUS_OK(dstFont.loadFromBuffer(blob));
    E2E_ASSERT_EQ(dstFont.getClosedCharset().size(), srcFont.getClosedCharset().size());
    E2E_ASSERT_EQ(dstFont.getGlyphMetric(U'暴').uvMinMax[2], srcFont.getGlyphMetric(U'暴').uvMinMax[2]);
    return true;
}

E2E_TEST(Tier1, FEAT_56, MqFont_PacksGlyphsIntoStaticAtlas) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);

    const GlyphMetric mDigit = font.getGlyphMetric(U'5');
    const GlyphMetric mCjk = font.getGlyphMetric(U'击');
    E2E_ASSERT_LT(mDigit.uvMinMax[0], mDigit.uvMinMax[2]);
    E2E_ASSERT_LT(mDigit.uvMinMax[1], mDigit.uvMinMax[3]);
    E2E_ASSERT_LT(mCjk.uvMinMax[0], mCjk.uvMinMax[2]);
    // 不同字形在静态图集中占据不重叠的架子槽位
    E2E_ASSERT_TRUE(mDigit.uvMinMax[0] != mCjk.uvMinMax[0] || mDigit.uvMinMax[1] != mCjk.uvMinMax[1]);
    return true;
}

E2E_TEST(Tier1, FEAT_56, MqFont_LoadsAtRuntimeWithZeroFreeTypeCalls) {
    BakedFont offlineBaked;
    offlineBaked.initDefaultCombatPreset(512u, 512u);
    const std::vector<uint8_t> binaryMqFont = offlineBaked.saveToBuffer();

    BakedFont runtimeFont;
    E2E_ASSERT_STATUS_OK(runtimeFont.loadFromBuffer(binaryMqFont));
    (void)runtimeFont.getGlyphMetric(U'9');
    (void)runtimeFont.getGlyphMetric(U'暴');

    E2E_ASSERT_TRUE(runtimeFont.isLoaded());
    E2E_ASSERT_EQ(runtimeFont.getFreeTypeInvocationCount(), 0u);
    return true;
}

// ============================================================================
// [FEAT-57] 32B 紧凑跳字实例与单 Pass 千级合批 (InGameTextComponent)
// ============================================================================

E2E_TEST(Tier1, FEAT_57, InGameText_Strictly32BytesAligned) {
    E2E_ASSERT_EQ(sizeof(GpuInGameTextQuadInstance), 32u);
    E2E_ASSERT_EQ(alignof(GpuInGameTextQuadInstance), 16u);
    return true;
}

E2E_TEST(Tier1, FEAT_57, InGameText_PacksWorldPosAndUvMinMax) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);

    InGameTextComponent comp;
    comp.spawnDamageNumber("95", FCT::Vec3<>{10.0f, 4.0f, -3.0f}, 1.25f, 0xFFFFFFFFu, false);

    std::vector<GpuInGameTextQuadInstance> quads;
    comp.buildQuadInstances(font, quads);
    E2E_ASSERT_EQ(quads.size(), 2u);
    E2E_ASSERT_TRUE(std::abs(quads[0].worldPosAndScale[0] - 10.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(quads[0].worldPosAndScale[1] - 4.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(quads[0].worldPosAndScale[3] - 1.25f) < 1e-5f);
    E2E_ASSERT_GT(quads[1].worldPosAndScale[0], quads[0].worldPosAndScale[0]);
    E2E_ASSERT_GT(quads[0].uvMinMax[2], quads[0].uvMinMax[0]);
    return true;
}

E2E_TEST(Tier1, FEAT_57, InGameText_PacksFillAndOutlineColors) {
    BakedFont font;
    font.initDefaultCombatPreset(512u, 512u);

    InGameTextComponent comp;
    comp.spawnDamageNumber("8", FCT::Vec3<>{0.0f, 0.0f, 0.0f}, 1.0f, 0xFF4422FFu, false);
    comp.spawnDamageNumber("9", FCT::Vec3<>{0.0f, 1.0f, 0.0f}, 1.0f, 0xFFFFFFFFu, true);

    std::vector<GpuInGameTextQuadInstance> quads;
    comp.buildQuadInstances(font, quads);
    E2E_ASSERT_EQ(quads.size(), 2u);
    E2E_ASSERT_EQ(quads[0].fillColorRgba8, 0xFF4422FFu);
    E2E_ASSERT_EQ(quads[0].outlineColorAndPx, 0x00000002u);
    E2E_ASSERT_EQ(quads[1].fillColorRgba8, 0xFFD700FFu);
    E2E_ASSERT_TRUE(std::abs(quads[1].worldPosAndScale[3] - 1.5f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_57, InGameText_Batches1000QuadsInMaxTwoDrawCalls) {
    const uint32_t drawCalls = InGameTextComponent::calculateDrawCallCount(1000u);
    E2E_ASSERT_GE(drawCalls, 1u);
    E2E_ASSERT_LE(drawCalls, 2u);
    E2E_ASSERT_EQ(drawCalls, 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_57, InGameText_SimdBatchAnimationEvaluation) {
    std::vector<GpuInGameTextQuadInstance> quads(64u);
    for (uint32_t i = 0; i < 64u; ++i) {
        quads[i].worldPosAndScale[0] = static_cast<float>(i);
        quads[i].worldPosAndScale[1] = 2.0f;
        quads[i].worldPosAndScale[2] = -5.0f;
        quads[i].worldPosAndScale[3] = 1.2f;
    }

    InGameTextComponent::evaluateBatchAnimations(quads, 0.2f);
    E2E_ASSERT_TRUE(std::abs(quads[0].worldPosAndScale[1] - 3.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(quads[63].worldPosAndScale[3] - 1.18f) < 1e-5f);
    return true;
}

// ============================================================================
// [FEAT-58] 开放 UI 跨端字体后端分流 (FontSystem + SystemFontProvider)
// ============================================================================

E2E_TEST(Tier1, FEAT_58, FontSystem_DesktopBackend_FreeTypeWithHinting) {
    FontSystem desktopFont(FontBackendMode::Desktop_FreeType);
    E2E_ASSERT_STATUS_OK(desktopFont.init(FontBackendMode::Desktop_FreeType));
    E2E_ASSERT_EQ(desktopFont.getBackendMode(), FontBackendMode::Desktop_FreeType);
    E2E_ASSERT_TRUE(desktopFont.isFreeTypeBackendActive());
    E2E_ASSERT_TRUE(desktopFont.isHintingEnabled());
    desktopFont.term();
    return true;
}

E2E_TEST(Tier1, FEAT_58, FontSystem_MinigameBackend_ChecksPrebakedSubset) {
    FontSystem miniFont(FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_EQ(miniFont.getBackendMode(), FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_FALSE(miniFont.isFreeTypeBackendActive());
    E2E_ASSERT_TRUE(miniFont.isGlyphInPrebakedSubset(U'确'));
    E2E_ASSERT_TRUE(miniFont.isGlyphInPrebakedSubset(U'定'));
    E2E_ASSERT_FALSE(miniFont.queriesSystemFontProvider(U'确'));
    return true;
}

E2E_TEST(Tier1, FEAT_58, FontSystem_MinigameBackend_FallbackToSystemProvider) {
    FontSystem miniFont(FontBackendMode::MiniGame_Hybrid);
    E2E_ASSERT_STATUS_OK(miniFont.init(FontBackendMode::MiniGame_Hybrid));

    // 生僻字不在预烘焙子集中，自动走 SystemFontProvider 系统字库光栅化回退
    E2E_ASSERT_FALSE(miniFont.isGlyphInPrebakedSubset(U'霁'));
    E2E_ASSERT_TRUE(miniFont.queriesSystemFontProvider(U'霁'));

    const char32_t rareCps[1] = {U'霁'};
    E2E_ASSERT_STATUS_OK(miniFont.ensureGlyphsCached(rareCps, 24u));
    CachedUiGlyph cached{};
    E2E_ASSERT_TRUE(miniFont.tryGetCachedGlyph(U'霁', 24u, cached));
    E2E_ASSERT_GT(cached.metric.advanceX, 0.0f);

    miniFont.term();
    return true;
}

E2E_TEST(Tier1, FEAT_58, FontSystem_BatchesMissingCodepoints) {
    FontSystem fontSys(FontBackendMode::Desktop_FreeType);
    const char32_t batch[5] = {U'背', U'包', U'装', U'备', U'栏'};
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(batch, 18u));
    E2E_ASSERT_EQ(fontSys.getLastBatchCodepointCount(), 5u);

    CachedUiGlyph firstHit{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'装', 18u, firstHit));

    // 二次批量查询命中同代缓存，UV 坐标与图集页保持完全稳定且不新增图集页
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(batch, 18u));
    CachedUiGlyph secondHit{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'装', 18u, secondHit));
    E2E_ASSERT_EQ(secondHit.pageId, firstHit.pageId);
    E2E_ASSERT_EQ(secondHit.metric.uvMinMax[0], firstHit.metric.uvMinMax[0]);
    E2E_ASSERT_EQ(fontSys.getAtlasManager().getPageCount(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_58, FontSystem_UpdatesAtlasViaSubRegion) {
    GlyphAtlasManager atlas;
    uint32_t pageId = UINT32_MAX;
    Domain::Renderer::AtlasAllocation alloc{};
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'A', 16u, 20u, pageId, alloc));
    E2E_ASSERT_TRUE(alloc.success);

    const std::vector<uint8_t> bitmap(16u * 20u, 0xE0u);
    E2E_ASSERT_STATUS_OK(atlas.uploadGlyphBitmap(pageId, alloc, bitmap.data(), bitmap.size(), 16u));
    E2E_ASSERT_EQ(atlas.getPageCount(), 1u);
    return true;
}

// ============================================================================
// [FEAT-59] R8/RGBA8 动态字形图集与内存告警 LRU 淘汰 (GlyphAtlasManager)
// ============================================================================

E2E_TEST(Tier1, FEAT_59, DynamicAtlas_AllocatesR8UnormForMonochrome) {
    GlyphAtlasManager atlas;
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(U'A'), TextureFormat::R8_UNORM);
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(U'汉'), TextureFormat::R8_UNORM);

    uint32_t pageId = UINT32_MAX;
    Domain::Renderer::AtlasAllocation alloc{};
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'汉', 24u, 24u, pageId, alloc));
    E2E_ASSERT_EQ(atlas.getAtlasPageFormat(pageId), TextureFormat::R8_UNORM);
    return true;
}

E2E_TEST(Tier1, FEAT_59, DynamicAtlas_AllocatesRgba8ForColorEmoji) {
    GlyphAtlasManager atlas;
    constexpr char32_t kFireEmoji = 0x1F525u;
    E2E_ASSERT_EQ(GlyphAtlasManager::getGlyphFormat(kFireEmoji), TextureFormat::R8G8B8A8_UNORM);

    uint32_t pageId = UINT32_MAX;
    Domain::Renderer::AtlasAllocation alloc{};
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(kFireEmoji, 32u, 32u, pageId, alloc));
    E2E_ASSERT_EQ(atlas.getAtlasPageFormat(pageId), TextureFormat::R8G8B8A8_UNORM);
    E2E_ASSERT_EQ(atlas.getPageCount(), 2u);
    return true;
}

E2E_TEST(Tier1, FEAT_59, DynamicAtlas_EvictsLruPageWhenFull) {
    GlyphAtlasManager atlas;
    uint32_t page0 = 0u;
    uint32_t page1 = 0u;
    Domain::Renderer::AtlasAllocation alloc{};

    // 单张 2D 字形图集页为 1024x1024x1，前 4 个 480x480 字形填满第 0 页 (2x2 货架)，第 5 个字形溢出至第 1 页
    atlas.setCurrentFrame(10u);
    for (uint32_t i = 0; i < 4u; ++i) {
        E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(static_cast<char32_t>(U'旧' + i), 480u, 480u, page0, alloc));
    }

    atlas.setCurrentFrame(25u);
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'新', 480u, 480u, page1, alloc));
    E2E_ASSERT_EQ(atlas.getPageCount(), 2u);
    E2E_ASSERT_NE(page0, page1);

    E2E_ASSERT_TRUE(atlas.evictLruPage());
    E2E_ASSERT_EQ(atlas.getPageCount(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_59, DynamicAtlas_IncrementsGenerationOnEviction) {
    GlyphAtlasManager atlas;
    const uint32_t genBefore = atlas.getAtlasGeneration();

    uint32_t page0 = 0u;
    uint32_t page1 = 0u;
    Domain::Renderer::AtlasAllocation alloc{};
    atlas.setCurrentFrame(1u);
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'甲', 600u, 600u, page0, alloc));
    atlas.setCurrentFrame(2u);
    E2E_ASSERT_STATUS_OK(atlas.allocateGlyph(U'乙', 600u, 600u, page1, alloc));

    E2E_ASSERT_TRUE(atlas.evictLruPage());
    E2E_ASSERT_GT(atlas.getAtlasGeneration(), genBefore);
    return true;
}

E2E_TEST(Tier1, FEAT_59, DynamicAtlas_ClientsRefreshUvOnGenerationChange) {
    FontSystem fontSys(FontBackendMode::MiniGame_Hybrid);
    const char32_t cps[2] = {U'商', U'城'};
    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(cps, 22u));

    const uint32_t gen1 = fontSys.getAtlasGeneration();
    CachedUiGlyph glyphBefore{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'商', 22u, glyphBefore));
    E2E_ASSERT_EQ(glyphBefore.generation, gen1);

    // 内存告警清空旧图集页并递增代数，旧缓存条目自动失效直至客户端重拉
    fontSys.onMemoryWarning();
    E2E_ASSERT_GT(fontSys.getAtlasGeneration(), gen1);
    CachedUiGlyph staleCheck{};
    E2E_ASSERT_FALSE(fontSys.tryGetCachedGlyph(U'商', 22u, staleCheck));

    E2E_ASSERT_STATUS_OK(fontSys.ensureGlyphsCached(cps, 22u));
    CachedUiGlyph glyphRefreshed{};
    E2E_ASSERT_TRUE(fontSys.tryGetCachedGlyph(U'商', 22u, glyphRefreshed));
    E2E_ASSERT_EQ(glyphRefreshed.generation, fontSys.getAtlasGeneration());
    return true;
}

// ============================================================================
// [FEAT-60] 零原始事件暴露与动作中心范式 (HIDSubsystem)
// ============================================================================

E2E_TEST(Tier1, FEAT_60, ZeroRawExposure_TouchEventsTerminateInSubsystem) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle lookHandle = hid.registerAction("IA_Look", EInputActionValueType::Axis2D);

    auto touchImc = makeUnique<InputMappingContext>("IMC_Touch", 10);
    ActionBindingMapping swipeBinding{};
    swipeBinding.inputSourceKey = "TouchSwipeDelta2D";
    swipeBinding.actionHandle = lookHandle;
    touchImc->addBinding(std::move(swipeBinding));
    hid.addMappingContext(std::move(touchImc), 10);

    hid.onTouchDown(1u, 100.0f, 200.0f, false);
    hid.onTouchMove(1u, 124.0f, 188.0f, false);
    hid.update(0.016f);

    const FCT::Vec2 lookVec = hid.getActionValue(lookHandle).get<FCT::Vec2>();
    E2E_ASSERT_TRUE(std::abs(lookVec.x - 24.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(lookVec.y - (-12.0f)) < 1e-4f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_60, ZeroRawExposure_GameplaySubscribesOnlyToAction) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle jumpHandle = hid.registerAction("IA_Jump", EInputActionValueType::Digital);

    T1C_ActionCallbackRecord record{};
    const uint64_t cbId = hid.bindActionEvent(jumpHandle, ETriggerState::Triggered, T1C_ActionCallbackFn{&record});
    E2E_ASSERT_NE(cbId, 0ULL);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(jumpHandle, InputValue(true), 0.016f));
    E2E_ASSERT_EQ(record.fireCount, 1u);
    E2E_ASSERT_TRUE(record.lastValue.get<bool>());

    hid.unbindActionEvent(cbId);
    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_60, ZeroRawExposure_IdenticalLogicAcrossPlatforms) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle moveHandle = hid.registerAction("IA_Move", EInputActionValueType::Axis2D);

    auto desktopImc = makeUnique<InputMappingContext>("IMC_Desktop", 0);
    ActionBindingMapping keyW{};
    keyW.inputSourceKey = "Key_W";
    keyW.actionHandle = moveHandle;
    keyW.modifiers.push_back(makeUnique<InputModifierSwizzleAxis>(ESwizzleAxisOrder::YXZ));
    desktopImc->addBinding(std::move(keyW));

    ActionBindingMapping vJoy{};
    vJoy.inputSourceKey = "VirtualJoystick_Left";
    vJoy.actionHandle = moveHandle;
    desktopImc->addBinding(std::move(vJoy));
    hid.addMappingContext(std::move(desktopImc), 0);

    hid.setSourceInputValue("Key_W", InputValue(1.0f));
    hid.update(0.016f);
    const FCT::Vec2 fromDesktop = hid.getActionValue(moveHandle).get<FCT::Vec2>();

    hid.clearSourceInputValues();
    hid.setSourceInputValue("VirtualJoystick_Left", InputValue(FCT::Vec2{0.0f, 1.0f}));
    hid.update(0.016f);
    const FCT::Vec2 fromMiniGame = hid.getActionValue(moveHandle).get<FCT::Vec2>();

    E2E_ASSERT_TRUE(std::abs(fromDesktop.x - fromMiniGame.x) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(fromDesktop.y - fromMiniGame.y) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(fromMiniGame.y - 1.0f) < 1e-5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_60, ZeroRawExposure_ActionQueriesReturnSemanticValues) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle throttle = hid.registerAction("IA_Throttle", EInputActionValueType::Axis1D);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction("IA_Throttle", InputValue(0.75f), 0.016f));
    E2E_ASSERT_TRUE(hid.isActionPressed(std::string_view("IA_Throttle")));
    E2E_ASSERT_EQ(hid.getActionValue(throttle).getValueType(), EInputActionValueType::Axis1D);
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue("IA_Throttle").get<float>() - 0.75f) < 1e-5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_60, ZeroRawExposure_DeviceAbstractionIntegrity) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle attackHandle = hid.registerAction("IA_PrimaryAttack", EInputActionValueType::Digital);

    E2E_ASSERT_TRUE(hid.isActionValid("IA_PrimaryAttack"));
    E2E_ASSERT_EQ(hid.getActionHandle("IA_PrimaryAttack"), attackHandle);
    E2E_ASSERT_TRUE(hid.findAction(attackHandle) != nullptr);
    E2E_ASSERT_EQ(hid.findAction(attackHandle)->getName(), "IA_PrimaryAttack");

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-61] 多维动作值类型与多源融合策略 (InputValue)
// ============================================================================

E2E_TEST(Tier1, FEAT_61, InputValue_DigitalBoolEvaluation) {
    const InputValue offVal(false);
    const InputValue onVal(true);
    E2E_ASSERT_EQ(onVal.getValueType(), EInputActionValueType::Digital);
    E2E_ASSERT_FALSE(offVal.get<bool>());
    E2E_ASSERT_TRUE(onVal.get<bool>());
    E2E_ASSERT_TRUE(std::abs(onVal.magnitude() - 1.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_61, InputValue_Axis1DFloatEvaluation) {
    const InputValue axis(-0.625f);
    E2E_ASSERT_EQ(axis.getValueType(), EInputActionValueType::Axis1D);
    E2E_ASSERT_TRUE(std::abs(axis.get<float>() - (-0.625f)) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(axis.magnitude() - 0.625f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_61, InputValue_Axis2DVec2Evaluation) {
    const InputValue move(FCT::Vec2{0.6f, 0.8f});
    E2E_ASSERT_EQ(move.getValueType(), EInputActionValueType::Axis2D);
    E2E_ASSERT_TRUE(std::abs(move.get<FCT::Vec2>().x - 0.6f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(move.get<FCT::Vec2>().y - 0.8f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(move.magnitude() - 1.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_61, InputValue_Axis3DVec3Evaluation) {
    const InputValue flight(FCT::Vec3f{1.0f, 2.0f, 2.0f});
    E2E_ASSERT_EQ(flight.getValueType(), EInputActionValueType::Axis3D);
    E2E_ASSERT_TRUE(std::abs(flight.get<FCT::Vec3f>().z - 2.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(flight.magnitude() - 3.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_61, InputValue_MultiSourceAccumulationPolicies) {
    InputValue sum(FCT::Vec2{1.0f, 0.0f});
    sum.accumulate(InputValue(FCT::Vec2{-1.0f, 0.5f}), EInputAccumulationPolicy::Cumulative);
    E2E_ASSERT_TRUE(std::abs(sum.get<FCT::Vec2>().x) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(sum.get<FCT::Vec2>().y - 0.5f) < 1e-5f);

    InputValue peak(0.35f);
    peak.accumulate(InputValue(-0.90f), EInputAccumulationPolicy::MaxMagnitude);
    peak.accumulate(InputValue(0.50f), EInputAccumulationPolicy::MaxMagnitude);
    E2E_ASSERT_TRUE(std::abs(peak.get<float>() - (-0.90f)) < 1e-5f);
    return true;
}

// ============================================================================
// [FEAT-62] 顺序输入修饰器链 (InputModifier)
// ============================================================================

E2E_TEST(Tier1, FEAT_62, Modifier_SwizzleAxis_RemapsComponents) {
    const InputModifierSwizzleAxis swizzleYxz(ESwizzleAxisOrder::YXZ);
    const InputValue remapped1D = swizzleYxz.modify(InputValue(1.0f), 0.016f);
    E2E_ASSERT_EQ(remapped1D.getValueType(), EInputActionValueType::Axis2D);
    E2E_ASSERT_TRUE(std::abs(remapped1D.get<FCT::Vec2>().x) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(remapped1D.get<FCT::Vec2>().y - 1.0f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_62, Modifier_Negate_InvertsSpecifiedAxes) {
    const InputModifierNegate invY(false, true, false);
    const InputValue out = invY.modify(InputValue(FCT::Vec2{0.5f, 0.75f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(out.get<FCT::Vec2>().x - 0.5f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(out.get<FCT::Vec2>().y - (-0.75f)) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_62, Modifier_DeadZone_FiltersSubThresholdInput) {
    const InputModifierDeadZone deadZone(0.20f);
    const InputValue drifted = deadZone.modify(InputValue(FCT::Vec2{0.10f, 0.08f}), 0.016f);
    const InputValue active = deadZone.modify(InputValue(FCT::Vec2{0.0f, 0.50f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(drifted.magnitude()) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(active.get<FCT::Vec2>().y - 0.50f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_62, Modifier_Normalize2D_ClampsDiagonalToUnitCircle) {
    const InputModifierNormalize2D norm2D;
    const InputValue diagonal = norm2D.modify(InputValue(FCT::Vec2{1.0f, 1.0f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(diagonal.magnitude() - 1.0f) < 1e-5f);

    const InputValue gentle = norm2D.modify(InputValue(FCT::Vec2{0.3f, 0.4f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(gentle.magnitude() - 0.5f) < 1e-5f);
    return true;
}

E2E_TEST(Tier1, FEAT_62, Modifier_Scalar_AppliesSensitivityMultiplier) {
    const InputModifierScalar sensitivity(2.5f);
    const InputValue scaled = sensitivity.modify(InputValue(FCT::Vec2{0.4f, -0.2f}), 0.016f);
    E2E_ASSERT_TRUE(std::abs(scaled.get<FCT::Vec2>().x - 1.0f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(scaled.get<FCT::Vec2>().y - (-0.5f)) < 1e-5f);
    return true;
}

// ============================================================================
// [FEAT-63] 五态触发器状态机 (InputTrigger)
// ============================================================================

E2E_TEST(Tier1, FEAT_63, Trigger_DownPressedReleased_StateTransitions) {
    InputTriggerDown downTrig;
    E2E_ASSERT_EQ(downTrig.update(InputValue(true), 0.016f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(downTrig.update(InputValue(true), 0.016f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(downTrig.update(InputValue(false), 0.016f), ETriggerState::Completed);

    InputTriggerPressed pressedTrig;
    E2E_ASSERT_EQ(pressedTrig.update(InputValue(true), 0.016f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(pressedTrig.update(InputValue(true), 0.016f), ETriggerState::None);

    InputTriggerReleased releasedTrig;
    E2E_ASSERT_EQ(releasedTrig.update(InputValue(true), 0.016f), ETriggerState::Started);
    E2E_ASSERT_EQ(releasedTrig.update(InputValue(false), 0.016f), ETriggerState::Triggered);
    return true;
}

E2E_TEST(Tier1, FEAT_63, Trigger_Tap_FiresWhenReleasedUnderThreshold) {
    InputTriggerTap tap(0.20f);
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.12f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(tap.update(InputValue(false), 0.016f), ETriggerState::Triggered);

    // 超过 0.20s 阈值即刻进入 Canceled
    E2E_ASSERT_EQ(tap.update(InputValue(true), 0.25f), ETriggerState::Canceled);
    E2E_ASSERT_EQ(tap.update(InputValue(false), 0.016f), ETriggerState::Canceled);
    return true;
}

E2E_TEST(Tier1, FEAT_63, Trigger_Hold_EmitsOngoingThenTriggered) {
    InputTriggerHold hold(0.40f, true);
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.20f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.25f), ETriggerState::Triggered);
    E2E_ASSERT_EQ(hold.update(InputValue(true), 0.10f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(hold.update(InputValue(false), 0.016f), ETriggerState::Completed);
    return true;
}

E2E_TEST(Tier1, FEAT_63, Trigger_HoldAndRelease_FiresOnQualifiedRelease) {
    InputTriggerHoldAndRelease chargeBow(0.50f);
    E2E_ASSERT_EQ(chargeBow.update(InputValue(true), 0.30f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(chargeBow.update(InputValue(false), 0.016f), ETriggerState::Canceled);

    E2E_ASSERT_EQ(chargeBow.update(InputValue(true), 0.60f), ETriggerState::Ongoing);
    E2E_ASSERT_EQ(chargeBow.update(InputValue(false), 0.016f), ETriggerState::Triggered);
    return true;
}

E2E_TEST(Tier1, FEAT_63, Trigger_ChordedAction_RequiresPrerequisite) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle altHandle = hid.registerAction("IA_ModifierAlt", EInputActionValueType::Digital);

    auto ultAction = makeUnique<InputAction>("IA_UltimateSkill", 0u, EInputActionValueType::Digital);
    ultAction->addTrigger(makeUnique<InputTriggerChordedAction>(altHandle, "IA_ModifierAlt"));
    hid.registerAction(std::move(ultAction));
    const InputActionHandle ultHandle = hid.getActionHandle("IA_UltimateSkill");

    // 先决动作未按下时组合动作不触发
    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(ultHandle, InputValue(true), 0.016f));
    E2E_ASSERT_EQ(hid.getActionTriggerState(ultHandle), ETriggerState::None);

    // 激活先决动作后再次触发成功发射 Triggered
    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(altHandle, InputValue(true), 0.016f));
    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(ultHandle, InputValue(true), 0.016f));
    E2E_ASSERT_EQ(hid.getActionTriggerState(ultHandle), ETriggerState::Triggered);

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-64] 优先级输入映射上下文栈 (InputMappingContext)
// ============================================================================

E2E_TEST(Tier1, FEAT_64, ImcStack_HigherPriorityConsumesInput) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle jumpHandle = hid.registerAction("IA_Jump", EInputActionValueType::Digital);
    const InputActionHandle confirmHandle = hid.registerAction("IA_UiConfirm", EInputActionValueType::Digital);

    auto combatImc = makeUnique<InputMappingContext>("IMC_Combat", 0);
    ActionBindingMapping bJump{};
    bJump.inputSourceKey = "Key_Space";
    bJump.actionHandle = jumpHandle;
    combatImc->addBinding(std::move(bJump));

    auto dialogImc = makeUnique<InputMappingContext>("IMC_Dialog", 100);
    ActionBindingMapping bConfirm{};
    bConfirm.inputSourceKey = "Key_Space";
    bConfirm.actionHandle = confirmHandle;
    bConfirm.consumesInput = true;
    dialogImc->addBinding(std::move(bConfirm));

    hid.addMappingContext(std::move(combatImc), 0);
    hid.addMappingContext(std::move(dialogImc), 100);

    hid.setSourceInputValue("Key_Space", InputValue(true));
    hid.update(0.016f);

    E2E_ASSERT_TRUE(hid.getActionValue(confirmHandle).get<bool>());
    E2E_ASSERT_FALSE(hid.getActionValue(jumpHandle).get<bool>());

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_64, ImcStack_PoppingRestoresUnderlyingContext) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle jumpHandle = hid.registerAction("IA_Jump", EInputActionValueType::Digital);
    const InputActionHandle confirmHandle = hid.registerAction("IA_UiConfirm", EInputActionValueType::Digital);

    auto combatImc = makeUnique<InputMappingContext>("IMC_Combat", 0);
    ActionBindingMapping bJump{};
    bJump.inputSourceKey = "Key_Space";
    bJump.actionHandle = jumpHandle;
    combatImc->addBinding(std::move(bJump));

    auto dialogImc = makeUnique<InputMappingContext>("IMC_Dialog", 100);
    ActionBindingMapping bConfirm{};
    bConfirm.inputSourceKey = "Key_Space";
    bConfirm.actionHandle = confirmHandle;
    dialogImc->addBinding(std::move(bConfirm));

    hid.addMappingContext(std::move(combatImc), 0);
    hid.addMappingContext(std::move(dialogImc), 100);
    hid.removeMappingContext("IMC_Dialog");
    E2E_ASSERT_FALSE(hid.hasActiveContext("IMC_Dialog"));

    hid.setSourceInputValue("Key_Space", InputValue(true));
    hid.update(0.016f);
    E2E_ASSERT_TRUE(hid.getActionValue(jumpHandle).get<bool>());

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_64, ImcStack_LoadsFromTomlConfiguration) {
    constexpr std::string_view kToml = R"(
[context]
name = "IMC_DefaultDesktop"
priority = 10

[[bindings]]
source = "Key_W"
action = "IA_Move"
consumes_input = true
modifiers = ["SwizzleYXZ", "Normalize2D"]
triggers = ["Down"]

[[bindings]]
source = "Key_Space"
action = "IA_Jump"
triggers = [{ type = "Tap", threshold = 0.2 }]
)";

    InputMappingContext imc;
    E2E_ASSERT_STATUS_OK(imc.loadFromToml(kToml));
    E2E_ASSERT_EQ(imc.getName(), "IMC_DefaultDesktop");
    E2E_ASSERT_EQ(imc.getPriority(), 10);
    E2E_ASSERT_EQ(imc.getActionBindingCount(), 2u);
    E2E_ASSERT_EQ(imc.getBindings()[0].modifiers.size(), 2u);
    E2E_ASSERT_EQ(imc.getBindings()[1].triggers.size(), 1u);
    return true;
}

E2E_TEST(Tier1, FEAT_64, ImcStack_MultiPriorityResolution) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    (void)hid.registerAction("IA_BaseWalk", EInputActionValueType::Digital);
    (void)hid.registerAction("IA_VehicleSteer", EInputActionValueType::Digital);
    (void)hid.registerAction("IA_CutsceneSkip", EInputActionValueType::Digital);

    auto baseCtx = makeUnique<InputMappingContext>("IMC_Exploration", 0);
    ActionBindingMapping b0{};
    b0.inputSourceKey = "Key_E";
    b0.actionName = "IA_BaseWalk";
    baseCtx->addBinding(std::move(b0));

    auto vehicleCtx = makeUnique<InputMappingContext>("IMC_Vehicle", 50);
    ActionBindingMapping b1{};
    b1.inputSourceKey = "Key_E";
    b1.actionName = "IA_VehicleSteer";
    vehicleCtx->addBinding(std::move(b1));

    auto cutsceneCtx = makeUnique<InputMappingContext>("IMC_Cutscene", 200);
    ActionBindingMapping b2{};
    b2.inputSourceKey = "Key_E";
    b2.actionName = "IA_CutsceneSkip";
    cutsceneCtx->addBinding(std::move(b2));

    hid.addMappingContext(std::move(baseCtx), 0);
    hid.addMappingContext(std::move(cutsceneCtx), 200);
    hid.addMappingContext(std::move(vehicleCtx), 50);

    const auto resolved = hid.resolveKeyInput("Key_E");
    E2E_ASSERT_TRUE(resolved.triggered);
    E2E_ASSERT_EQ(resolved.contextName, "IMC_Cutscene");
    E2E_ASSERT_EQ(resolved.actionName, "IA_CutsceneSkip");

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_64, ImcStack_SwitchesBetweenDesktopAndTouchPresets) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle lookHandle = hid.registerAction("IA_Look", EInputActionValueType::Axis2D);

    auto desktopImc = makeUnique<InputMappingContext>("IMC_DesktopPreset", 10);
    ActionBindingMapping mouseBind{};
    mouseBind.inputSourceKey = "Mouse_Delta2D";
    mouseBind.actionHandle = lookHandle;
    desktopImc->addBinding(std::move(mouseBind));
    hid.addMappingContext(std::move(desktopImc), 10);
    E2E_ASSERT_EQ(hid.resolveKeyInput("Mouse_Delta2D").contextName, "IMC_DesktopPreset");

    hid.removeMappingContext("IMC_DesktopPreset");
    auto touchImc = makeUnique<InputMappingContext>("IMC_TouchPreset", 10);
    ActionBindingMapping swipeBind{};
    swipeBind.inputSourceKey = "TouchSwipeDelta2D";
    swipeBind.actionHandle = lookHandle;
    touchImc->addBinding(std::move(swipeBind));
    hid.addMappingContext(std::move(touchImc), 10);

    E2E_ASSERT_FALSE(hid.hasActiveContext("IMC_DesktopPreset"));
    E2E_ASSERT_TRUE(hid.hasActiveContext("IMC_TouchPreset"));
    E2E_ASSERT_EQ(hid.resolveKeyInput("TouchSwipeDelta2D").actionName, "IA_Look");

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-65] 虚拟触控手势源 (TouchSwipeDelta2D / TouchPinchDelta1D)
// ============================================================================

E2E_TEST(Tier1, FEAT_65, VirtualSource_TouchSwipeDelta2D_MapsToLook) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle lookHandle = hid.registerAction("IA_Look", EInputActionValueType::Axis2D);

    auto imc = makeUnique<InputMappingContext>("IMC_TouchLook", 10);
    ActionBindingMapping b{};
    b.inputSourceKey = "TouchSwipeDelta2D";
    b.actionHandle = lookHandle;
    b.modifiers.push_back(makeUnique<InputModifierScalar>(0.5f));
    imc->addBinding(std::move(b));
    hid.addMappingContext(std::move(imc), 10);

    hid.onTouchDown(0u, 300.0f, 300.0f, false);
    hid.onTouchMove(0u, 340.0f, 280.0f, false);
    hid.update(0.016f);

    const FCT::Vec2 look = hid.getActionValue(lookHandle).get<FCT::Vec2>();
    E2E_ASSERT_TRUE(std::abs(look.x - 20.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(look.y - (-10.0f)) < 1e-4f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_65, VirtualSource_TouchPinchDelta1D_MapsToZoom) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle zoomHandle = hid.registerAction("IA_CameraZoom", EInputActionValueType::Axis1D);

    auto imc = makeUnique<InputMappingContext>("IMC_TouchZoom", 10);
    ActionBindingMapping b{};
    b.inputSourceKey = "TouchPinchDelta1D";
    b.actionHandle = zoomHandle;
    imc->addBinding(std::move(b));
    hid.addMappingContext(std::move(imc), 10);

    hid.onTouchDown(0u, 100.0f, 200.0f, false);
    hid.onTouchDown(1u, 200.0f, 200.0f, false);
    E2E_ASSERT_TRUE(hid.isTouchPinchActive());

    // 双指间距由 100px 拉开至 160px，产生 +60.0f 缩放增量
    hid.onTouchMove(1u, 260.0f, 200.0f, false);
    hid.update(0.016f);
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(zoomHandle).get<float>() - 60.0f) < 1e-4f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_65, VirtualSource_ExcludesUiConsumedTouches) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    // 落在 HtmlUI 摇杆上的触点标记 consumedByUi=true，不得泄漏至视角滑动增量
    hid.onTouchDown(0u, 80.0f, 600.0f, true);
    hid.onTouchMove(0u, 150.0f, 520.0f, true);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().y) < 1e-6f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_65, VirtualSource_ResetsDeltaOnTouchEnd) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    hid.onTouchDown(2u, 400.0f, 300.0f, false);
    hid.onTouchMove(2u, 435.0f, 315.0f, false);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x - 35.0f) < 1e-4f);

    hid.onTouchUp(2u, 435.0f, 315.0f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().y) < 1e-6f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_65, VirtualSource_MultiFingerStability) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    // 左手 UI 摇杆触点 (pointer 0, consumedByUi=true) + 右手自由视角滑动触点 (pointer 1, consumedByUi=false)
    hid.onTouchDown(0u, 100.0f, 500.0f, true);
    hid.onTouchDown(1u, 600.0f, 300.0f, false);
    hid.onTouchMove(0u, 140.0f, 460.0f, true);
    hid.onTouchMove(1u, 618.0f, 290.0f, false);

    E2E_ASSERT_FALSE(hid.isTouchPinchActive());
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDeltaForPointer(0u).x) < 1e-6f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().x - 18.0f) < 1e-4f);
    E2E_ASSERT_TRUE(std::abs(hid.getTouchSwipeDelta().y - (-10.0f)) < 1e-4f);

    hid.term();
    return true;
}

// ============================================================================
// [FEAT-66] 程序化动作注入桥接 (HIDSubsystem::injectInputForAction)
// ============================================================================

E2E_TEST(Tier1, FEAT_66, InjectAction_DirectValueInjection) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());
    const InputActionHandle moveHandle = hid.registerAction("IA_Move", EInputActionValueType::Axis2D);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(moveHandle, InputValue(FCT::Vec2{0.5f, -0.75f}), 0.016f));
    const FCT::Vec2 v = hid.getActionValue(moveHandle).get<FCT::Vec2>();
    E2E_ASSERT_TRUE(std::abs(v.x - 0.5f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(v.y - (-0.75f)) < 1e-5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_66, InjectAction_ProcessesThroughModifiers) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    auto moveAction = makeUnique<InputAction>("IA_MoveNormalized", 0u, EInputActionValueType::Axis2D);
    moveAction->addModifier(makeUnique<InputModifierDeadZone>(0.20f));
    moveAction->addModifier(makeUnique<InputModifierNormalize2D>());
    hid.registerAction(std::move(moveAction));
    const InputActionHandle h = hid.getActionHandle("IA_MoveNormalized");

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(h, InputValue(FCT::Vec2{0.05f, 0.05f}), 0.016f));
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(h).magnitude()) < 1e-6f);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(h, InputValue(FCT::Vec2{2.0f, 2.0f}), 0.016f));
    E2E_ASSERT_TRUE(std::abs(hid.getActionValue(h).magnitude() - 1.0f) < 1e-5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_66, InjectAction_DrivesTriggerStateMachine) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    auto heavyAttack = makeUnique<InputAction>("IA_HeavyAttack", 0u, EInputActionValueType::Digital);
    heavyAttack->addTrigger(makeUnique<InputTriggerHold>(0.30f, true));
    hid.registerAction(std::move(heavyAttack));
    const InputActionHandle h = hid.getActionHandle("IA_HeavyAttack");

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(h, InputValue(true), 0.15f));
    E2E_ASSERT_EQ(hid.getActionTriggerState(h), ETriggerState::Ongoing);

    E2E_ASSERT_STATUS_OK(hid.injectInputForAction(h, InputValue(true), 0.20f));
    E2E_ASSERT_EQ(hid.getActionTriggerState(h), ETriggerState::Triggered);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_66, InjectAction_VirtualJoystickDrivesMoveVector) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    auto moveAction = makeUnique<InputAction>("IA_Move", 0u, EInputActionValueType::Axis2D);
    moveAction->addModifier(makeUnique<InputModifierNormalize2D>());
    hid.registerAction(std::move(moveAction));

    // HtmlUI 虚拟摇杆拖动注入超单位圆向量，经 Normalize2D 钳制后输出单位方向向量
    E2E_ASSERT_STATUS_OK(hid.injectInputForAction("IA_Move", InputValue(FCT::Vec2{0.6f, 0.8f}), 0.016f));
    const FCT::Vec2 dir = hid.getActionValue("IA_Move").get<FCT::Vec2>();
    E2E_ASSERT_TRUE(std::abs(dir.x - 0.6f) < 1e-5f);
    E2E_ASSERT_TRUE(std::abs(dir.y - 0.8f) < 1e-5f);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_66, InjectAction_AutomatedTestReplaySupport) {
    HIDSubsystem hid;
    E2E_ASSERT_STATUS_OK(hid.init());

    auto dashAction = makeUnique<InputAction>("IA_Dash", 0u, EInputActionValueType::Digital);
    dashAction->addTrigger(makeUnique<InputTriggerTap>(0.20f));
    hid.registerAction(std::move(dashAction));
    const InputActionHandle dashHandle = hid.getActionHandle("IA_Dash");

    T1C_ActionCallbackRecord cbRec{};
    (void)hid.bindActionEvent(dashHandle, ETriggerState::Triggered, T1C_ActionCallbackFn{&cbRec});

    const std::vector<HIDSubsystem::TimestampedAction> sequence = {
        HIDSubsystem::TimestampedAction{1000ULL, dashHandle, InputValue(true)},
        HIDSubsystem::TimestampedAction{1120ULL, dashHandle, InputValue(false)}
    };
    uint32_t processed = 0u;
    E2E_ASSERT_STATUS_OK(hid.replayActionSequence(sequence, processed));
    E2E_ASSERT_EQ(processed, 2u);
    E2E_ASSERT_EQ(cbRec.fireCount, 1u);
    E2E_ASSERT_EQ(hid.getActionTriggerState(dashHandle), ETriggerState::Triggered);

    hid.term();
    return true;
}

E2E_TEST(Tier1, FEAT_INFRA, CommandLineParser_StripsOuterQuotesAndParsesSceneFlag) {
    ::MQEngine::Resource::CommandLineParser parser;
    parser.parse({
        "Editor.exe",
        "--project=\"G:/MQEngine/CityGame\"",
        "--scene='Scenes/CityMain.mqscene'"
    });

    std::string parsedProject;
    std::string parsedScene;
    for (const auto& arg : parser.getParsedArgs()) {
        if (arg.matchesKey("project")) {
            parsedProject = arg.value;
        } else if (arg.matchesKey("scene")) {
            parsedScene = arg.value;
        }
    }
    E2E_ASSERT_EQ(parsedProject, "G:/MQEngine/CityGame");
    E2E_ASSERT_EQ(parsedScene, "Scenes/CityMain.mqscene");
    return true;
}

E2E_TEST(Tier1, FEAT_INFRA, SceneSubsystem_EmptyInit_And_MqSceneLoadPreservesEditorAndProcedural) {
    T1C_ScopedSandboxRoots sandbox("mqscene");
    ::MQEngine::DiskResourceLoader loader;
    ::MQEngine::Domain::Scene::SceneSubsystem scene;
    E2E_ASSERT_STATUS_OK(scene.init());
    scene.setEditorMetadataEnabled(true);

    // 启动后 Main 默认必须为空场景，且 Editor 隔离分区已预创建
    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::LocalTransform>().size(), 0u);
    E2E_ASSERT_EQ(scene.getActiveEntityCount(), 0u);
    E2E_ASSERT_TRUE(scene.getRegistryByName("Editor") != nullptr);

    // 在 Editor 分区创建编辑器相机，确保加载或清空 Main 时不被破坏
    const auto edCam = scene.createEntityInRegistry("Editor");
    auto* edReg = scene.getRegistryByName("Editor");
    E2E_ASSERT_TRUE(edReg != nullptr);
    edReg->emplace_or_replace<::MQEngine::Domain::Scene::EditorNameComponent>(edCam, "EditorCamera");
    edReg->emplace_or_replace<::MQEngine::Domain::Scene::UuidComponent>(edCam, "ed-cam");

    const auto camEnt = scene.createEntity();
    auto& mainReg = scene.getRegistry();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::EditorNameComponent>(camEnt, "OrthoMainCam");
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::UuidComponent>(camEnt, "cam-main-01");
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::CameraComponent>(camEnt,
        ::MQEngine::Domain::Scene::CameraComponent{
            .fov = 55.0f,
            .active = true,
            .orthographic = true,
            .orthoSize = 18.5f,
            .priority = 5,
            .targetName = "MainWindow"
        });
    scene.update();

    const std::string savedScenePath = sandbox.baseDir + "/SavedTest.mqscene";
    E2E_ASSERT_STATUS_OK(scene.saveSceneToFile(savedScenePath, &loader));
    E2E_ASSERT_STATUS_OK(scene.loadSceneFromFile(savedScenePath, &loader));

    E2E_ASSERT_EQ(edReg->view<::MQEngine::Domain::Scene::EditorNameComponent>().size(), 1u);
    auto loadedCamView = scene.getRegistry().view<::MQEngine::Domain::Scene::CameraComponent>();
    E2E_ASSERT_EQ(loadedCamView.size(), 1u);
    const auto loadedCam = loadedCamView.get<::MQEngine::Domain::Scene::CameraComponent>(*loadedCamView.begin());
    E2E_ASSERT_TRUE(loadedCam.active);
    E2E_ASSERT_TRUE(loadedCam.orthographic);
    E2E_ASSERT_TRUE(std::abs(loadedCam.orthoSize - 18.5f) < 1e-4f);
    E2E_ASSERT_EQ(loadedCam.priority, 5);
    E2E_ASSERT_EQ(loadedCam.targetName, "MainWindow");

    // 加载无 entities 键的元信息 .mqscene（如 CityMain.mqscene）绝不能清空已存在的程序化实体
    const auto proceduralEnt = scene.createEntity();
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::EditorNameComponent>(proceduralEnt, "ProceduralRoad");
    mainReg.emplace_or_replace<::MQEngine::Domain::Scene::UuidComponent>(proceduralEnt, "proc-1");
    const std::string metaOnlyPath = sandbox.baseDir + "/MetaOnly.mqscene";
    E2E_ASSERT_STATUS_OK(loader.saveFile(
        metaOnlyPath,
        "scene_name = \"CityMeta\"\n[camera]\northographic = true\northo_size = 32.0\n"));
    E2E_ASSERT_STATUS_OK(scene.loadSceneFromFile(metaOnlyPath, &loader));

    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::EditorNameComponent>().size(), 2u);
    const auto updatedCam = scene.getRegistry().get<::MQEngine::Domain::Scene::CameraComponent>(*loadedCamView.begin());
    E2E_ASSERT_TRUE(std::abs(updatedCam.orthoSize - 32.0f) < 1e-4f);

    // 切换至纯 Game 模式（无 Editor / CliEditor 标签）：加载 .mqscene 不得挂载任何 EditorNameComponent，且 active=false 转为 DisabledTag 并被剔除过滤
    scene.setEditorMetadataEnabled(false);
    const std::string gameScenePath = sandbox.baseDir + "/GameModeFilter.mqscene";
    E2E_ASSERT_STATUS_OK(loader.saveFile(
        gameScenePath,
        "scene_name = \"GameFilterTest\"\n"
        "[[entities]]\n"
        "name = \"ActiveMesh\"\n"
        "uuid = \"game-active-1\"\n"
        "active = true\n"
        "position = [0.0, 0.0, 10.0]\n"
        "[entities.static_mesh]\n"
        "primitive_type = 0\n"
        "visible = true\n"
        "[[entities]]\n"
        "name = \"DisabledMesh\"\n"
        "uuid = \"game-disabled-1\"\n"
        "active = false\n"
        "position = [0.0, 0.0, 10.0]\n"
        "[entities.static_mesh]\n"
        "primitive_type = 0\n"
        "visible = true\n"));
    E2E_ASSERT_STATUS_OK(scene.loadSceneFromFile(gameScenePath, &loader));

    // 纯 Game 模式下创建运行时实体也不得挂载 EditorNameComponent
    const auto runtimeTransientEnt = scene.createEntity();
    E2E_ASSERT_FALSE(scene.getRegistry().all_of<::MQEngine::Domain::Scene::EditorNameComponent>(runtimeTransientEnt));

    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::EditorNameComponent>().size(), 0u);
    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::UuidComponent>().size(), 2u);
    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::DisabledTag>().size(), 1u);

    auto meshView = scene.getRegistry().view<::MQEngine::Domain::Scene::WorldTransform, ::MQEngine::Domain::Scene::StaticMeshComponent>();
    for (auto ent : meshView) {
        auto& world = meshView.get<::MQEngine::Domain::Scene::WorldTransform>(ent);
        world.aabbMin = FCT::Vec3<>(-1.0f, -1.0f, 9.0f);
        world.aabbMax = FCT::Vec3<>(1.0f, 1.0f, 11.0f);
        world.isVisible = true;
    }

    const FCT::Mat4 viewMat = FCT::Mat4::LookAt(
        FCT::Vec3<>(0.0f, 0.0f, 0.0f),
        FCT::Vec3<>(0.0f, 0.0f, 1.0f),
        FCT::Vec3<>(0.0f, 1.0f, 0.0f));
    const FCT::Mat4 projMat = FCT::Mat4::Perspective(60.0f, 1.0f, 1.0f, 100.0f);
    ::MQEngine::Domain::Scene::Frustum frustum;
    frustum.extractFromViewProj(projMat * viewMat, false);

    std::vector<FCT::ECS::entity> visibleEntities;
    scene.getCullingSystem().cullScene(scene.getRegistry(), frustum, visibleEntities);
    E2E_ASSERT_EQ(visibleEntities.size(), 1u);
    E2E_ASSERT_FALSE(scene.getRegistry().all_of<::MQEngine::Domain::Scene::DisabledTag>(visibleEntities.front()));

    scene.term();
    return true;
}

namespace T1C_NativeScriptTestDetail
{
    struct T1C_NativeScriptLifecycleProbe
    {
        std::vector<std::string> events;
        float accumulatedDistance = 0.0f;
        int32_t observedBudget = 0;
    };

    inline T1C_NativeScriptLifecycleProbe* g_t1cScriptProbe = nullptr;

    class T1C_WorldScript : public ::MQEngine::NativeScript
    {
    public:
        struct Properties
        {
            int32_t budget = 1000;
            float speed = 2.5f;
            bool spawnHelper = true;
            std::string cityName = "DefaultCity";
            FCT::Vec3<> anchor{1.0f, 2.0f, 3.0f};
        };

        Properties props{};

        void onCreate() override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back("WorldCreate");
                g_t1cScriptProbe->observedBudget = props.budget;
            }
        }

        void onStart() override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back("WorldStart");
            }
        }

        void onUpdate(float dt) override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->accumulatedDistance += props.speed * dt;
            }
            if (auto* tf = tryGetComponent<::MQEngine::Domain::Scene::LocalTransform>()) {
                tf->position.x += props.speed * dt;
            }
        }

        void onDestroy() override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back("WorldDestroy");
            }
        }
    };

    class T1C_HudLogicScript : public ::MQEngine::NativeScript
    {
    public:
        struct Properties
        {
            bool visible = true;
        };

        Properties props{};
        bool foundWorldInStart = false;

        void onCreate() override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back("HudCreate");
            }
        }

        void onStart() override
        {
            foundWorldInStart = (findFirstScript<T1C_WorldScript>() != nullptr);
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back(foundWorldInStart ? "HudStartWithWorld" : "HudStartMissingWorld");
            }
        }

        void onDestroy() override
        {
            if (g_t1cScriptProbe) {
                g_t1cScriptProbe->events.push_back("HudDestroy");
            }
        }
    };
} // namespace T1C_NativeScriptTestDetail

E2E_TEST(Tier1, FEAT_INFRA, NativeScript_EntityMounted_Lifecycle_BoostPfrProps_And_MqSceneRoundTrip) {
    using namespace T1C_NativeScriptTestDetail;
    T1C_ScopedSandboxRoots sandbox("nativescript_mqscene");
    ::MQEngine::DiskResourceLoader loader;
    ::MQEngine::Domain::Scene::SceneSubsystem scene;
    ::MQEngine::Domain::GamePlay::GamePlaySubsystem gameplay;
    E2E_ASSERT_STATUS_OK(scene.init());
    E2E_ASSERT_STATUS_OK(gameplay.init());
    scene.setEditorMetadataEnabled(true);

    gameplay.registerNativeScript<T1C_WorldScript>("T1C_WorldScript", "TestModule", "Native/src/T1C_WorldScript.h");
    gameplay.registerNativeScript<T1C_HudLogicScript>("T1C_HudLogicScript", "TestModule", "Native/src/T1C_HudLogicScript.h");
    E2E_ASSERT_TRUE(gameplay.getNativeScriptDescriptor("T1C_WorldScript") != nullptr);
    E2E_ASSERT_EQ(gameplay.getNativeScriptDescriptor("T1C_WorldScript")->properties.size(), 5u);

    auto& reg = scene.getRegistry();

    // 1. 挂载空间实体脚本 + 纯逻辑无 Transform 实体脚本 + TransientTag 临时分块实体
    const auto worldEnt = scene.createEntity("WorldActor");
    reg.emplace_or_replace<::MQEngine::Domain::Scene::UuidComponent>(worldEnt, "world-actor-01");
    ::MQEngine::Domain::Scene::NativeScriptComponent worldScriptComp{};
    ::MQEngine::Domain::Scene::NativeScriptSlot worldSlot{};
    worldSlot.className = "T1C_WorldScript";
    worldSlot.enabled = true;
    ::MQEngine::Domain::Scene::UpsertNativeScriptProperty(
        worldSlot, "budget", ::MQEngine::Domain::Scene::ScriptPropertyValue::fromInt32(88000));
    ::MQEngine::Domain::Scene::UpsertNativeScriptProperty(
        worldSlot, "speed", ::MQEngine::Domain::Scene::ScriptPropertyValue::fromFloat(10.0f));
    ::MQEngine::Domain::Scene::UpsertNativeScriptProperty(
        worldSlot, "cityName", ::MQEngine::Domain::Scene::ScriptPropertyValue::fromString("NeoHarbor"));
    ::MQEngine::Domain::Scene::UpsertNativeScriptProperty(
        worldSlot, "anchor", ::MQEngine::Domain::Scene::ScriptPropertyValue::fromVec3(FCT::Vec3<>(4.0f, 5.0f, 6.0f)));
    worldScriptComp.scripts.push_back(std::move(worldSlot));
    ::MQEngine::Domain::Scene::NormalizeNativeScriptSlots(worldScriptComp);
    reg.emplace_or_replace<::MQEngine::Domain::Scene::NativeScriptComponent>(worldEnt, std::move(worldScriptComp));

    const auto hudEnt = scene.createEntity("HudLogicActor");
    reg.remove<::MQEngine::Domain::Scene::LocalTransform>(hudEnt);
    reg.remove<::MQEngine::Domain::Scene::WorldTransform>(hudEnt);
    reg.emplace_or_replace<::MQEngine::Domain::Scene::UuidComponent>(hudEnt, "hud-actor-02");
    ::MQEngine::Domain::Scene::NativeScriptComponent hudScriptComp{};
    hudScriptComp.scriptClassName = "T1C_HudLogicScript";
    ::MQEngine::Domain::Scene::NormalizeNativeScriptSlots(hudScriptComp);
    reg.emplace_or_replace<::MQEngine::Domain::Scene::NativeScriptComponent>(hudEnt, std::move(hudScriptComp));

    const auto transientChunk = scene.createEntity("RuntimeChunk_0_0");
    reg.emplace_or_replace<::MQEngine::Domain::Scene::TransientTag>(transientChunk);

    // 2. 保存并重新加载 .mqscene：TransientTag 实体必须被排除，空间实体与无 Transform 实体及其脚本属性完整往返
    const std::string sceneFile = sandbox.baseDir + "/NativeScriptRoundTrip.mqscene";
    E2E_ASSERT_STATUS_OK(scene.saveSceneToFile(sceneFile, &loader));
    E2E_ASSERT_STATUS_OK(scene.loadSceneFromFile(sceneFile, &loader));

    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::UuidComponent>().size(), 2u);
    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::LocalTransform>().size(), 1u);
    E2E_ASSERT_EQ(scene.getRegistry().view<::MQEngine::Domain::Scene::NativeScriptComponent>().size(), 2u);

    // 3. 验证两阶段确定性生命周期（所有新脚本先完成 onCreate，再统一执行 onStart -> onUpdate）
    T1C_NativeScriptLifecycleProbe probe{};
    g_t1cScriptProbe = &probe;

    gameplay.update(0.5f, scene.getRegistry());
    E2E_ASSERT_EQ(probe.observedBudget, 88000);
    E2E_ASSERT_TRUE(std::abs(probe.accumulatedDistance - 5.0f) < 1e-4f);
    E2E_ASSERT_EQ(probe.events.size(), 4u);
    E2E_ASSERT_TRUE(
        std::find(probe.events.begin(), probe.events.begin() + 2, "WorldCreate") != probe.events.begin() + 2);
    E2E_ASSERT_TRUE(
        std::find(probe.events.begin(), probe.events.begin() + 2, "HudCreate") != probe.events.begin() + 2);
    E2E_ASSERT_TRUE(
        std::find(probe.events.begin() + 2, probe.events.end(), "HudStartWithWorld") != probe.events.end());

    // 4. 验证运行时属性热修改同步与 PIE 停止全量逆序销毁
    FCT::ECS::entity loadedWorldEnt = FCT::ECS::null;
    for (auto ent : scene.getRegistry().view<::MQEngine::Domain::Scene::LocalTransform>()) {
        loadedWorldEnt = ent;
    }
    E2E_ASSERT_TRUE(loadedWorldEnt != FCT::ECS::null);
    auto& loadedNs = scene.getRegistry().get<::MQEngine::Domain::Scene::NativeScriptComponent>(loadedWorldEnt);
    ::MQEngine::Domain::Scene::UpsertNativeScriptProperty(
        loadedNs.scripts[0], "speed", ::MQEngine::Domain::Scene::ScriptPropertyValue::fromFloat(20.0f));
    gameplay.applySlotPropertiesToLiveScript(scene.getRegistry(), loadedWorldEnt, loadedNs.scripts[0]);

    gameplay.update(0.5f, scene.getRegistry());
    E2E_ASSERT_TRUE(std::abs(probe.accumulatedDistance - 15.0f) < 1e-4f);

    gameplay.shutdownAllNativeScriptInstances(&scene.getRegistry());
    E2E_ASSERT_TRUE(gameplay.getLiveNativeScript(scene.getRegistry(), loadedWorldEnt, "T1C_WorldScript") == nullptr);
    E2E_ASSERT_EQ(probe.events.size(), 6u);

    g_t1cScriptProbe = nullptr;
    gameplay.term();
    scene.term();
    return true;
}

} // namespace MQEngine::Testing
