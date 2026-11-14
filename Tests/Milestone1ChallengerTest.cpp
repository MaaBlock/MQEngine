#include <iostream>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include <algorithm>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/thirdparty/thirdparty.h>

using namespace MQEngine;
using namespace MQEngine::Domain::Renderer;
using namespace MQEngine::Resource;

#define CHALLENGE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[CHALLENGE FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

// =============================================================================
// Test Suite 1: std430 Memory Alignment, Offsets & Binary Layout Oracle
// =============================================================================
bool test_std430_memory_alignment()
{
    std::cout << "\n=== [Suite 1] std430 Memory Alignment, Offsets & Binary Layout ===" << std::endl;

    // 1.1 GpuInstanceRow (160 bytes, alignas 16)
    CHALLENGE_CHECK(sizeof(GpuInstanceRow) == 160, "sizeof(GpuInstanceRow) must be exactly 160 bytes");
    CHALLENGE_CHECK(alignof(GpuInstanceRow) == 16, "alignof(GpuInstanceRow) must be 16");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, modelMatrix) == 0, "modelMatrix must be at offset 0");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, invTransposeMatrix) == 64, "invTransposeMatrix must be at offset 64");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, worldSphereBounds) == 128, "worldSphereBounds must be at offset 128");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, meshSlot) == 144, "meshSlot must be at offset 144");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, materialSlot) == 148, "materialSlot must be at offset 148");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, entityId) == 152, "entityId must be at offset 152");
    CHALLENGE_CHECK(offsetof(GpuInstanceRow, flags) == 156, "flags must be at offset 156");

    // 1.2 GpuMaterialRecord (64 bytes, alignas 16)
    CHALLENGE_CHECK(sizeof(GpuMaterialRecord) == 64, "sizeof(GpuMaterialRecord) must be exactly 64 bytes");
    CHALLENGE_CHECK(alignof(GpuMaterialRecord) == 16, "alignof(GpuMaterialRecord) must be 16");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, baseColorFactor) == 0, "baseColorFactor must be at offset 0");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, metallicRoughnessNormalOcclusion) == 16, "metallicRoughnessNormalOcclusion must be at offset 16");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, emissiveFactor) == 32, "emissiveFactor must be at offset 32");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, albedoTexIndex) == 48, "albedoTexIndex must be at offset 48");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, normalTexIndex) == 52, "normalTexIndex must be at offset 52");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, ormTexIndex) == 56, "ormTexIndex must be at offset 56");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, shadingModelId) == 60, "shadingModelId must be at offset 60");
    CHALLENGE_CHECK(offsetof(GpuMaterialRecord, pipelineBucket) == 62, "pipelineBucket must be at offset 62");

    // 1.3 GpuTextureRecord (32 bytes, alignas 16)
    CHALLENGE_CHECK(sizeof(GpuTextureRecord) == 32, "sizeof(GpuTextureRecord) must be exactly 32 bytes");
    CHALLENGE_CHECK(alignof(GpuTextureRecord) == 16, "alignof(GpuTextureRecord) must be 16");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, uvScaleOffset) == 0, "uvScaleOffset must be at offset 0");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, bindlessOrPageIndex) == 16, "bindlessOrPageIndex must be at offset 16");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, flags) == 20, "flags must be at offset 20");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, pad0) == 24, "pad0 must be at offset 24");
    CHALLENGE_CHECK(offsetof(GpuTextureRecord, pad1) == 28, "pad1 must be at offset 28");

    // 1.4 GpuSceneUniformData (512 bytes, alignas 16)
    CHALLENGE_CHECK(sizeof(GpuSceneUniformData) == 512, "sizeof(GpuSceneUniformData) must be exactly 512 bytes");
    CHALLENGE_CHECK(alignof(GpuSceneUniformData) == 16, "alignof(GpuSceneUniformData) must be 16");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewMatrix) == 0, "viewMatrix must be at offset 0");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, projectionMatrix) == 64, "projectionMatrix must be at offset 64");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewProjectionMatrix) == 128, "viewProjectionMatrix must be at offset 128");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, cameraWorldPos) == 192, "cameraWorldPos must be at offset 192");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, directionalLightDirection) == 208, "directionalLightDirection must be at offset 208");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, directionalLightColorIntensity) == 224, "directionalLightColorIntensity must be at offset 224");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, viewportSize) == 240, "viewportSize must be at offset 240");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, lightViewProjectionMatrix) == 256, "lightViewProjectionMatrix must be at offset 256");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, invViewProjectionMatrix) == 320, "invViewProjectionMatrix must be at offset 320");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, shadowParams) == 384, "shadowParams must be at offset 384");
    CHALLENGE_CHECK(offsetof(GpuSceneUniformData, pointLightPosRange0) == 400, "pointLightPosRange0 must be at offset 400");

    // 1.5 Bit-packing compatibility test for GpuMaterialRecord offset 60-63
    GpuMaterialRecord mat;
    mat.shadingModelId = 0x1234;
    mat.pipelineBucket = 0x5678;
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(&mat);
    uint32_t packedValue = *reinterpret_cast<const uint32_t*>(rawBytes + 60);
    // On little-endian x86_64: low 16 bits = shadingModelId, high 16 bits = pipelineBucket
    CHALLENGE_CHECK((packedValue & 0xFFFF) == 0x1234, "shadingModelId must occupy low 16 bits");
    CHALLENGE_CHECK((packedValue >> 16) == 0x5678, "pipelineBucket must occupy high 16 bits");

    std::cout << "[PASS] Suite 1: All std430 struct sizes, 16-byte alignments, member offsets and bit packing verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 2: GpuResourceTable Rapid Allocate/Free Cycles & Recycling
// =============================================================================
bool test_rapid_allocate_free_recycling()
{
    std::cout << "\n=== [Suite 2] GpuResourceTable Rapid Allocate/Free Cycles ===" << std::endl;
    GpuResourceTable table;

    int dummy = 100;
    const void* handle = &dummy;

    // 2.1 Rapid allocate and free 5000 cycles on a single resource handle
    uint32_t initialSlot = UINT32_MAX;
    for (int i = 0; i < 5000; ++i) {
        table.registerResource(handle, GpuResourceType::Texture, 1024, "TempTexture");
        uint32_t slot = table.getBindlessSlot(handle);
        CHALLENGE_CHECK(slot < 4096, "Allocated slot must be < 4096");
        if (i == 0) {
            initialSlot = slot;
        } else {
            // Because we free every turn, the slot must be recycled and should NOT grow
            CHALLENGE_CHECK(slot == initialSlot, "Freed slot must be immediately recycled on next allocation");
        }
        table.finalizeUnregister(handle);
        CHALLENGE_CHECK(!table.contains(handle), "Resource must be removed after finalizeUnregister");
    }

    // 2.2 Verify that active count is 0 and VRAM is 0
    std::cout << "  [Observation] activeCount() = " << table.activeCount() << " (0x" << std::hex << table.activeCount() << std::dec << ")" << std::endl;
    CHALLENGE_CHECK(table.activeCount() == 0, "Active count must be 0 after all unregistrations");
    CHALLENGE_CHECK(table.totalVramBytes() == 0, "Total VRAM must be 0 after all unregistrations");

    std::cout << "[PASS] Suite 2: 5000 rapid allocate/free cycles recycled slot " << initialSlot << " without monotonic slot leakage." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 3: Multi-Threaded Concurrent Slot Allocation & Free Stress
// =============================================================================
bool test_concurrent_slot_allocation_stress()
{
    std::cout << "\n=== [Suite 3] Multi-Threaded Concurrent Slot Allocation Stress ===" << std::endl;
    GpuResourceTable table;

    constexpr int kNumThreads = 8;
    constexpr int kOpsPerThread = 200;
    std::vector<std::thread> workers;
    std::atomic<bool> startFlag{false};

    // Pre-allocate dummy handles so pointers are valid and distinct
    std::vector<std::vector<int>> dummyData(kNumThreads, std::vector<int>(kOpsPerThread));
    std::vector<std::vector<uint32_t>> threadSlots(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            threadSlots[t].reserve(kOpsPerThread);
            for (int i = 0; i < kOpsPerThread; ++i) {
                const void* h = &dummyData[t][i];
                table.registerResource(h, GpuResourceType::Texture, 512, "ThreadTex");
                uint32_t s = table.getBindlessSlot(h);
                threadSlots[t].push_back(s);
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& w : workers) {
        w.join();
    }

    // Verify all allocated slots across all threads are completely unique
    std::unordered_set<uint32_t> uniqueSlots;
    for (int t = 0; t < kNumThreads; ++t) {
        for (uint32_t s : threadSlots[t]) {
            CHALLENGE_CHECK(s < 4096, "Slot must be within [0, 4096)");
            auto [it, inserted] = uniqueSlots.insert(s);
            CHALLENGE_CHECK(inserted, "Concurrent slot allocations must never yield duplicate slots!");
        }
    }
    CHALLENGE_CHECK(uniqueSlots.size() == kNumThreads * kOpsPerThread, "All slots must be unique");
    CHALLENGE_CHECK(table.activeCount() == kNumThreads * kOpsPerThread, "Active count must match total inserted");

    // Now unregister all concurrently from multiple threads
    workers.clear();
    startFlag.store(false, std::memory_order_release);
    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            while (!startFlag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kOpsPerThread; ++i) {
                const void* h = &dummyData[t][i];
                table.finalizeUnregister(h);
            }
        });
    }

    startFlag.store(true, std::memory_order_release);
    for (auto& w : workers) {
        w.join();
    }

    CHALLENGE_CHECK(table.activeCount() == 0, "All items must be unregistered");
    CHALLENGE_CHECK(table.totalVramBytes() == 0, "Total VRAM must be zero");

    // Now verify that all 1600 freed slots can be recycled without exceeding 4096
    for (int i = 0; i < kNumThreads * kOpsPerThread; ++i) {
        const void* h = &dummyData[0][0] + i;
        table.registerResource(h, GpuResourceType::Texture, 128, "Recycled");
        uint32_t s = table.getBindlessSlot(h);
        CHALLENGE_CHECK(s < 4096, "Recycled slot must be < 4096");
    }
    CHALLENGE_CHECK(table.activeCount() == kNumThreads * kOpsPerThread, "Active count matches");

    table.clear();
    CHALLENGE_CHECK(table.activeCount() == 0, "Table cleared");

    std::cout << "[PASS] Suite 3: 8 threads, 1600 concurrent allocations and frees verified with 0 duplicates and 100% recycling." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 4: Capacity Boundary & Slot Recycling Exhaustion
// =============================================================================
bool test_capacity_boundary_and_recycling()
{
    std::cout << "\n=== [Suite 4] Capacity Boundary (up to 4096 slots) & Bulk Recycling ===" << std::endl;
    GpuResourceTable table;

    constexpr uint32_t kMaxSlots = 4096;
    std::vector<int> dummyResources(kMaxSlots);

    // 4.1 Fill all 4096 slots
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        const void* h = &dummyResources[i];
        table.registerResource(h, GpuResourceType::Texture, 64, "Tex");
        uint32_t s = table.getBindlessSlot(h);
        CHALLENGE_CHECK(s < kMaxSlots, "Slot must be < 4096");
    }
    CHALLENGE_CHECK(table.activeCount() == kMaxSlots, "Active count must be 4096");

    // 4.2 Free all 4096 slots
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        const void* h = &dummyResources[i];
        table.finalizeUnregister(h);
    }
    CHALLENGE_CHECK(table.activeCount() == 0, "Active count must be 0");

    // 4.3 Re-allocate all 4096 slots again — they MUST come from m_freeSlots
    std::unordered_set<uint32_t> recycledSlots;
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        const void* h = &dummyResources[i];
        table.registerResource(h, GpuResourceType::Texture, 64, "TexRecycled");
        uint32_t s = table.getBindlessSlot(h);
        CHALLENGE_CHECK(s < kMaxSlots, "Recycled slot must be < 4096");
        recycledSlots.insert(s);
    }
    CHALLENGE_CHECK(recycledSlots.size() == kMaxSlots, "All 4096 slots must be recycled without overflow");

    table.clear();
    std::cout << "[PASS] Suite 4: All 4096 slots allocated, fully released, and recycled without overflow." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 5: Fallback Texture Resolution & Unregister State
// =============================================================================
bool test_fallback_resolution_and_unregister()
{
    std::cout << "\n=== [Suite 5] Fallback Texture Resolution & Unregister State ===" << std::endl;
    GpuResourceTable table;

    int dummyTexture = 111;
    int fallbackTexture = 999;
    const void* hTex = &dummyTexture;
    const void* hFallback = &fallbackTexture;

    table.setDefaultFallbackHandle(GpuResourceType::Texture, hFallback);
    CHALLENGE_CHECK(table.getDefaultFallbackHandle(GpuResourceType::Texture) == hFallback, "Fallback handle matches");

    table.registerResource(hTex, GpuResourceType::Texture, 2048, "MyTexture");
    CHALLENGE_CHECK(table.resolveHandle(hTex, GpuResourceType::Texture) == hTex, "Active texture resolves to itself");

    // Step 1: Mark released
    table.markResourceReleased(hTex);
    CHALLENGE_CHECK(table.resolveHandle(hTex, GpuResourceType::Texture) == hFallback, "Released texture resolves to fallback");

    // Step 4: Finalize unregister
    table.finalizeUnregister(hTex);
    CHALLENGE_CHECK(!table.contains(hTex), "Table no longer contains hTex");
    CHALLENGE_CHECK(table.resolveHandle(hTex, GpuResourceType::Texture) == hFallback, "Unregistered texture resolves to fallback");

    std::cout << "[PASS] Suite 5: Fallback resolution and 4-step deferred unregister behavior verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 6: Empirical Exploration of Non-Texture Slot Allocation
// =============================================================================
bool test_non_texture_slot_behavior()
{
    std::cout << "\n=== [Suite 6] Empirical Analysis of Non-Texture Slot Allocation ===" << std::endl;
    GpuResourceTable table;

    int dummyBuffer = 42;
    int dummyShader = 43;
    const void* hBuf = &dummyBuffer;
    const void* hShader = &dummyShader;

    table.registerResource(hBuf, GpuResourceType::Buffer, 1024, "VertexBuffer");
    table.registerResource(hShader, GpuResourceType::Shader, 2048, "VertexShader");

    uint32_t bufSlot = table.getBindlessSlot(hBuf);
    uint32_t shaderSlot = table.getBindlessSlot(hShader);

    std::cout << "  [Observation] GpuResourceType::Buffer slot = " << bufSlot << std::endl;
    std::cout << "  [Observation] GpuResourceType::Shader slot = " << shaderSlot << std::endl;

    CHALLENGE_CHECK(bufSlot == UINT32_MAX, "Non-texture Buffer must not allocate bindless slot");
    CHALLENGE_CHECK(shaderSlot == UINT32_MAX, "Non-texture Shader must not allocate bindless slot");

    // 非纹理资源注销不应影响后续纹理槽位分配
    table.finalizeUnregister(hBuf);
    table.finalizeUnregister(hShader);

    // 验证纹理能够正常分配有效槽位
    int dummyTex = 44;
    const void* hTex = &dummyTex;
    table.registerResource(hTex, GpuResourceType::Texture, 512, "TextureAfterBuffer");
    uint32_t texSlot = table.getBindlessSlot(hTex);
    std::cout << "  [Observation] GpuResourceType::Texture slot = " << texSlot << std::endl;
    CHALLENGE_CHECK(texSlot < 4096, "Texture must allocate a valid bindless slot");

    table.clear();
    std::cout << "[PASS] Suite 6: Non-texture slot allocation correctly restricted (non-texture slots == UINT32_MAX)." << std::endl;
    return true;
}

int main()
{
    std::cout << "==========================================================" << std::endl;
    std::cout << "  MQEngine Milestone 1 Adversarial Challenger Test Suite  " << std::endl;
    std::cout << "==========================================================" << std::endl;

    bool allPassed = true;
    allPassed &= test_std430_memory_alignment();
    allPassed &= test_rapid_allocate_free_recycling();
    allPassed &= test_concurrent_slot_allocation_stress();
    allPassed &= test_capacity_boundary_and_recycling();
    allPassed &= test_fallback_resolution_and_unregister();
    allPassed &= test_non_texture_slot_behavior();

    std::cout << "\n==========================================================" << std::endl;
    if (allPassed) {
        std::cout << "  RESULT: ALL CHALLENGER TESTS PASSED (100% EMPIRICAL PASS)" << std::endl;
    } else {
        std::cout << "  RESULT: CHALLENGER TESTS FAILED (BUGS DETECTED)" << std::endl;
    }
    std::cout << "==========================================================" << std::endl;

    return allPassed ? 0 : 1;
}
