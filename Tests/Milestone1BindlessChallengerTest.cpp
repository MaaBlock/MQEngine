#include <iostream>
#include <vector>
#include <cassert>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/domain/renderer/BindlessSet0Manager.h>
#include <MQEngine/domain/renderer/ShaderGenerator.h>
#include <MQEngine/resource/table/GpuResourceTable.h>

using namespace MQEngine;
using namespace MQEngine::Domain::Renderer;
using namespace MQEngine::Resource;

#define CHALLENGE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[CHALLENGE_FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while(0)

// =============================================================================
// Test 1: Binary std430 Layout & Struct Offsets Verification
// =============================================================================
bool test_BinaryLayoutStd430()
{
    std::cout << "\n=== [Test 1] Binary std430 Layout & Struct Offsets ===" << std::endl;

    // 1.1 GpuInstanceRow
    CHALLENGE_CHECK(sizeof(GpuInstanceRow) == 160, "sizeof(GpuInstanceRow) must be 160 bytes");
    CHALLENGE_CHECK(alignof(GpuInstanceRow) == 16, "alignof(GpuInstanceRow) must be 16 bytes");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, modelMatrix) == 0, "offset of modelMatrix must be 0");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, invTransposeMatrix) == 64, "offset of invTransposeMatrix must be 64");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, worldSphereBounds) == 128, "offset of worldSphereBounds must be 128");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, meshSlot) == 144, "offset of meshSlot must be 144");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, materialSlot) == 148, "offset of materialSlot must be 148");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, entityId) == 152, "offset of entityId must be 152");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, flags) == 156, "offset of flags must be 156");

    // 1.2 GpuMaterialRecord
    CHALLENGE_CHECK(sizeof(GpuMaterialRecord) == 64, "sizeof(GpuMaterialRecord) must be 64 bytes");
    CHALLENGE_CHECK(alignof(GpuMaterialRecord) == 16, "alignof(GpuMaterialRecord) must be 16 bytes");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, baseColorFactor) == 0, "offset of baseColorFactor must be 0");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, metallicRoughnessNormalOcclusion) == 16, "offset of metallicRoughnessNormalOcclusion must be 16");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, emissiveFactor) == 32, "offset of emissiveFactor must be 32");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, albedoTexIndex) == 48, "offset of albedoTexIndex must be 48");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, normalTexIndex) == 52, "offset of normalTexIndex must be 52");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, ormTexIndex) == 56, "offset of ormTexIndex must be 56");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, shadingModelId) == 60, "offset of shadingModelId must be 60");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, pipelineBucket) == 62, "offset of pipelineBucket must be 62");

    // 1.3 GpuTextureRecord
    CHALLENGE_CHECK(sizeof(GpuTextureRecord) == 32, "sizeof(GpuTextureRecord) must be 32 bytes");
    CHALLENGE_CHECK(alignof(GpuTextureRecord) == 16, "alignof(GpuTextureRecord) must be 16 bytes");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, uvScaleOffset) == 0, "offset of uvScaleOffset must be 0");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, bindlessOrPageIndex) == 16, "offset of bindlessOrPageIndex must be 16");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, flags) == 20, "offset of flags must be 20");

    // 1.4 GpuSceneUniformData
    CHALLENGE_CHECK(sizeof(GpuSceneUniformData) == 512, "sizeof(GpuSceneUniformData) must be 512 bytes");
    CHALLENGE_CHECK(alignof(GpuSceneUniformData) == 16, "alignof(GpuSceneUniformData) must be 16 bytes");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewMatrix) == 0, "offset of viewMatrix must be 0");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, projectionMatrix) == 64, "offset of projectionMatrix must be 64");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewProjectionMatrix) == 128, "offset of viewProjectionMatrix must be 128");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, cameraWorldPos) == 192, "offset of cameraWorldPos must be 192");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, directionalLightDirection) == 208, "offset of directionalLightDirection must be 208");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, directionalLightColorIntensity) == 224, "offset of directionalLightColorIntensity must be 224");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewportSize) == 240, "offset of viewportSize must be 240");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, lightViewProjectionMatrix) == 256, "offset of lightViewProjectionMatrix must be 256");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, shadowParams) == 384, "offset of shadowParams must be 384");

    std::cout << "[PASS] Test 1: All std430 binary structs are 100% byte-aligned and offset-accurate." << std::endl;
    return true;
}

// =============================================================================
// Test 2: GpuResourceTable Lock-Free Slot Acquisition & Recycling Stress Test
// =============================================================================
bool test_GpuResourceTableSlotRecycling()
{
    std::cout << "\n=== [Test 2] GpuResourceTable Slot Acquisition & Recycling ===" << std::endl;
    GpuResourceTable table;

    // 2.1 Single resource registration
    int dummyA = 100;
    table.registerResource(&dummyA, GpuResourceType::Texture, 1024, "TexA");
    uint32_t slotA = table.getBindlessSlot(&dummyA);
    CHALLENGE_CHECK(slotA == 0, "First allocated slot must be 0");

    int dummyB = 200;
    table.registerResource(&dummyB, GpuResourceType::Texture, 2048, "TexB");
    uint32_t slotB = table.getBindlessSlot(&dummyB);
    CHALLENGE_CHECK(slotB == 1, "Second allocated slot must be 1");

    // 2.2 Unregister dummyA, verify slot recycling
    table.unregisterResource(&dummyA);
    CHALLENGE_CHECK(!table.contains(&dummyA), "dummyA must be unregistered");

    // 2.3 Register new dummyC, must reuse slot 0
    int dummyC = 300;
    table.registerResource(&dummyC, GpuResourceType::Texture, 4096, "TexC");
    uint32_t slotC = table.getBindlessSlot(&dummyC);
    CHALLENGE_CHECK(slotC == 0, "dummyC must reuse recycled slot 0 from dummyA");

    // 2.4 Multi-threaded slot churn stress test
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 200;
    std::vector<std::thread> workers;
    std::atomic<bool> start{false};

    std::vector<std::vector<int>> threadDummies(kThreads, std::vector<int>(kOpsPerThread, 0));

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kOpsPerThread; ++i) {
                void* handle = &threadDummies[t][i];
                table.registerResource(handle, GpuResourceType::Texture, 512, "StressTex");
                uint32_t slot = table.getBindlessSlot(handle);
                assert(slot < 4096 && "Slot must remain < 4096");
                table.unregisterResource(handle);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& w : workers) {
        w.join();
    }

    // After all freed, registering one should succeed within 4096
    int dummyD = 400;
    table.registerResource(&dummyD, GpuResourceType::Texture, 1024, "TexD");
    uint32_t slotD = table.getBindlessSlot(&dummyD);
    CHALLENGE_CHECK(slotD < 4096, "Recycled slot must be < 4096");

    std::cout << "[PASS] Test 2: GpuResourceTable slot recycling and concurrent registration passed." << std::endl;
    return true;
}

// =============================================================================
// Test 3: BindlessSet0Manager CPU Data Mirror & Edge Cases
// =============================================================================
bool test_BindlessSet0ManagerDataMirror()
{
    std::cout << "\n=== [Test 3] BindlessSet0Manager Data Mirror & Edge Cases ===" << std::endl;
    BindlessSet0Manager mgr;

    // 3.1 Initial state: uninitialized
    CHALLENGE_CHECK(!mgr.isInitialized(), "Manager must start uninitialized");
    CHALLENGE_CHECK(!mgr.isNativeBindlessSupported(), "Default tier must be Tier0");

    // 3.2 Update frame data with empty vectors
    GpuSceneUniformData sceneUniform{};
    mgr.updateFrameData(0, {}, {}, {}, sceneUniform);
    const auto& insts0 = mgr.getInstances(0);
    CHALLENGE_CHECK(insts0.empty(), "Empty instances vector must be preserved without error");

    // 3.3 Update with 10 instances and 10 materials
    std::vector<GpuInstanceRow> rows(10);
    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i].meshSlot = static_cast<uint32_t>(i);
        rows[i].materialSlot = static_cast<uint32_t>(i * 2);
    }
    std::vector<GpuMaterialRecord> mats(10);
    for (size_t i = 0; i < mats.size(); ++i) {
        mats[i].albedoTexIndex = static_cast<uint32_t>(i);
    }
    mgr.updateFrameData(1, rows, mats, {}, sceneUniform);

    const auto& insts1 = mgr.getInstances(1);
    CHALLENGE_CHECK(insts1.size() == 10, "Frame 1 must have 10 instances");
    CHALLENGE_CHECK(insts1[5].materialSlot == 10, "Row 5 materialSlot must be 10");

    const auto& mats1 = mgr.getMaterials(1);
    CHALLENGE_CHECK(mats1.size() == 10, "Frame 1 must have 10 materials");

    // Frame 0 should remain unaffected (triple buffering isolation)
    CHALLENGE_CHECK(mgr.getInstances(0).empty(), "Frame 0 instances must still be empty");

    // 3.4 EMPIRICAL CHALLENGE BUG AUDIT: Inspect m_textures behavior for native slots
    // In updateTextureSlot, if m_tier != Tier0, does m_textures ever get populated for slot >= 3?
    const auto& texs = mgr.getTextures(0);
    std::cout << "[AUDIT INFO] mgr.getTextures(0).size() = " << texs.size() << std::endl;
    // Initial size is 0 before init, or after initTripleBufferedTables it is 3.

    std::cout << "[PASS] Test 3: BindlessSet0Manager frame data mirroring and isolation verified." << std::endl;
    return true;
}

// =============================================================================
// Test 4: ShaderGenerator Output Verification
// =============================================================================
bool test_ShaderGeneratorOutput()
{
    std::cout << "\n=== [Test 4] ShaderGenerator Output Verification ===" << std::endl;

    ShaderGenerator gen;
    std::string vs = gen.generatePbrVertexShader(ShaderFeatureBits::None);
    std::string ps = gen.generatePbrPixelShader(ShaderFeatureBits::None);

    CHALLENGE_CHECK(vs.find("#include \"BindlessCommon.hlsli\"") != std::string::npos, "VS must include BindlessCommon.hlsli");
    CHALLENGE_CHECK(vs.find("SV_InstanceID") != std::string::npos, "VS must declare SV_InstanceID");
    CHALLENGE_CHECK(vs.find("g_Instances[input.instanceId]") != std::string::npos, "VS must index g_Instances");
    CHALLENGE_CHECK(vs.find("output.materialSlot = instance.materialSlot;") != std::string::npos, "VS must output materialSlot");

    CHALLENGE_CHECK(ps.find("#include \"BindlessCommon.hlsli\"") != std::string::npos, "PS must include BindlessCommon.hlsli");
    CHALLENGE_CHECK(ps.find("g_Materials[input.materialSlot]") != std::string::npos, "PS must index g_Materials");
    CHALLENGE_CHECK(ps.find("SampleBindlessTexture(mat.albedoTexIndex, input.uv)") != std::string::npos, "PS must call SampleBindlessTexture for albedo");

    std::cout << "[PASS] Test 4: ShaderGenerator produces compliant Bindless HLSL code." << std::endl;
    return true;
}

int main(int argc, char** argv)
{
    std::cout << "========================================================" << std::endl;
    std::cout << " MQEngine Milestone 1 Bindless Challenger Empirical Test" << std::endl;
    std::cout << "========================================================" << std::endl;

    bool allPassed = true;
    allPassed &= test_BinaryLayoutStd430();
    allPassed &= test_GpuResourceTableSlotRecycling();
    allPassed &= test_BindlessSet0ManagerDataMirror();
    allPassed &= test_ShaderGeneratorOutput();

    if (allPassed) {
        std::cout << "\n>>> ALL 4 EMPIRICAL TEST SUITES PASSED <<<" << std::endl;
        return 0;
    } else {
        std::cerr << "\n>>> EMPIRICAL TEST SUITE FAILED <<<" << std::endl;
        return 1;
    }
}
