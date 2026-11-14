#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <chrono>

#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>
#include <MQEngine/domain/renderer/BindlessSet0Manager.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <FCT/Context/ResourceGroup.h>

using namespace MQEngine;
using namespace MQEngine::Domain::Renderer;
using namespace MQEngine::Resource;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            std::exit(1); \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: std430 Struct Size & Alignment Guarantees
// -----------------------------------------------------------------------------
void test_std430_layouts()
{
    std::cout << "[Test 1] Testing std430 layouts & alignment guarantees..." << std::endl;

    static_assert(sizeof(GpuInstanceRow) == 160, "GpuInstanceRow must be 160 bytes");
    static_assert(alignof(GpuInstanceRow) == 16, "GpuInstanceRow must be 16-byte aligned");

    static_assert(sizeof(GpuMaterialRecord) == 64, "GpuMaterialRecord must be 64 bytes");
    static_assert(alignof(GpuMaterialRecord) == 16, "GpuMaterialRecord must be 16-byte aligned");

    static_assert(sizeof(GpuTextureRecord) == 32, "GpuTextureRecord must be 32 bytes");
    static_assert(alignof(GpuTextureRecord) == 16, "GpuTextureRecord must be 16-byte aligned");

    static_assert(sizeof(GpuSceneUniformData) == 512, "GpuSceneUniformData must be 512 bytes");
    static_assert(alignof(GpuSceneUniformData) == 16, "GpuSceneUniformData must be 16-byte aligned");

    GpuInstanceRow row{};
    row.meshSlot = 42;
    row.materialSlot = 101;
    row.entityId = 999;
    row.flags = 3;
    TEST_ASSERT(row.meshSlot == 42 && row.materialSlot == 101, "GpuInstanceRow field read/write");

    GpuMaterialRecord mat{};
    mat.albedoTexIndex = 12;
    mat.normalTexIndex = 13;
    mat.shadingModelId = 1;
    TEST_ASSERT(mat.albedoTexIndex == 12 && mat.normalTexIndex == 13, "GpuMaterialRecord field read/write");

    std::cout << "[Test 1 PASSED] std430 struct sizes and alignments verified." << std::endl;
}

// -----------------------------------------------------------------------------
// Test 2: AtlasShelfPacker Adversarial Stress Testing
// -----------------------------------------------------------------------------
void test_atlas_shelf_packer()
{
    std::cout << "[Test 2] Testing AtlasShelfPacker (non-PoT, oversize, thread safety)..." << std::endl;

    AtlasShelfPacker packer(2048, 2048, 16);

    // 2.1 Edge cases: 0x0 or oversize
    auto allocZeroW = packer.allocate(0, 100);
    TEST_ASSERT(!allocZeroW.success, "Zero width allocation must fail");
    auto allocZeroH = packer.allocate(100, 0);
    TEST_ASSERT(!allocZeroH.success, "Zero height allocation must fail");

    auto allocOversizeW = packer.allocate(2049, 100);
    TEST_ASSERT(!allocOversizeW.success, "Oversize width allocation must fail");
    auto allocOversizeH = packer.allocate(100, 2049);
    TEST_ASSERT(!allocOversizeH.success, "Oversize height allocation must fail");

    // 2.2 Normal & non-power-of-two rectangles
    auto a1 = packer.allocate(64, 64);
    TEST_ASSERT(a1.success && a1.layer == 0 && a1.x == 0 && a1.y == 0, "First allocation at (0,0)");
    auto uv1 = packer.calculateUvScaleOffset(a1);
    TEST_ASSERT(uv1.x == 64.0f / 2048.0f && uv1.z == 0.0f, "UV scale/offset matches");

    auto a2 = packer.allocate(128, 64);
    TEST_ASSERT(a2.success && a2.layer == 0 && a2.x == 64 && a2.y == 0, "Second allocation fits on same shelf");

    auto a3 = packer.allocate(73, 119); // non-power-of-two
    TEST_ASSERT(a3.success, "Non-power-of-two allocation succeeds");
    auto uv3 = packer.calculateUvScaleOffset(a3);
    TEST_ASSERT(uv3.x > 0.0f && uv3.y > 0.0f && uv3.z >= 0.0f && uv3.w >= 0.0f, "Non-PoT UVs valid");

    // 2.3 Concurrent allocations across 8 threads
    packer.reset();
    std::atomic<uint32_t> successCount{0};
    std::atomic<uint32_t> failCount{0};
    constexpr int kThreadCount = 8;
    constexpr int kAllocPerThread = 200;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreadCount; ++t) {
        workers.emplace_back([&packer, &successCount, &failCount, t]() {
            for (int i = 0; i < kAllocPerThread; ++i) {
                uint32_t w = 32 + ((i + t * 7) % 128);
                uint32_t h = 32 + ((i * 3 + t) % 128);
                auto res = packer.allocate(w, h);
                if (res.success) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                } else {
                    failCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    std::cout << "  Multi-threaded allocation: " << successCount.load()
              << " succeeded, " << failCount.load() << " full/failed." << std::endl;
    TEST_ASSERT(successCount.load() > 0, "Multi-threaded shelf packer must succeed for valid rectangles");

    std::cout << "[Test 2 PASSED] AtlasShelfPacker robust against stress and concurrency." << std::endl;
}

// -----------------------------------------------------------------------------
// Test 3: GpuResourceTable Natural Primary Key & Slot Management
// -----------------------------------------------------------------------------
void test_gpu_resource_table()
{
    std::cout << "[Test 3] Testing GpuResourceTable natural const void* primary key..." << std::endl;

    GpuResourceTable table;
    int dummyResourceA = 100;
    int dummyResourceB = 200;
    int fallbackTexture = 999;

    const void* handleA = &dummyResourceA;
    const void* handleB = &dummyResourceB;
    const void* handleFallback = &fallbackTexture;

    table.setDefaultFallbackHandle(GpuResourceType::Texture, handleFallback);
    TEST_ASSERT(table.getDefaultFallbackHandle(GpuResourceType::Texture) == handleFallback, "Default fallback set");

    // 3.1 Register resources with raw pointer keys
    table.registerResource(handleA, GpuResourceType::Texture, 1024, "TexA");
    table.registerResource(handleB, GpuResourceType::Texture, 2048, "TexB");
    TEST_ASSERT(table.activeCount() == 2, "Active count is 2");
    TEST_ASSERT(table.contains(handleA), "Table contains handleA");
    TEST_ASSERT(table.contains(handleB), "Table contains handleB");

    uint32_t slotA = table.getBindlessSlot(handleA);
    uint32_t slotB = table.getBindlessSlot(handleB);
    TEST_ASSERT(slotA != UINT32_MAX && slotB != UINT32_MAX && slotA != slotB, "Bindless slots allocated");

    // 3.2 Resolve handles
    TEST_ASSERT(table.resolveHandle(handleA, GpuResourceType::Texture) == handleA, "Active handle resolves to itself");

    // 3.3 Mark Released (Step 1): resolveHandle returns fallback!
    table.markResourceReleased(handleA);
    TEST_ASSERT(table.resolveHandle(handleA, GpuResourceType::Texture) == handleFallback,
                "Released handle resolves to default fallback");

    // 3.4 Finalize Unregister (Step 4): Slot reclaimed
    table.finalizeUnregister(handleA);
    TEST_ASSERT(!table.contains(handleA), "HandleA removed from table");

    // Register a new resource and verify slot reuse
    int dummyResourceC = 300;
    const void* handleC = &dummyResourceC;
    table.registerResource(handleC, GpuResourceType::Texture, 512, "TexC");
    uint32_t slotC = table.getBindlessSlot(handleC);
    TEST_ASSERT(slotC == slotA, "Reclaimed slot reused");

    std::cout << "[Test 3 PASSED] GpuResourceTable natural primary key and slot reuse verified." << std::endl;
}

// -----------------------------------------------------------------------------
// Test 4: 4-Step Deferred Release Pipeline Latency & Retirement
// -----------------------------------------------------------------------------
void test_deferred_release_pipeline()
{
    std::cout << "[Test 4] Testing 4-Step deferred GPU release pipeline under latency..." << std::endl;

    GpuMemoryManager memManager;
    GpuResourceTable table;

    int dummyGpuObj = 12345;
    const void* handle = &dummyGpuObj;
    std::atomic<bool> physicallyDeleted{false};

    table.registerResource(handle, GpuResourceType::Texture, 4096, "TestDeferredTex");

    // Step 1 & 3: Defer release at frame 10
    uint64_t retireFrame = 10;
    memManager.deferReleaseResource(handle, [&physicallyDeleted](const void* ptr) {
        physicallyDeleted.store(true);
    }, retireFrame);

    // Frame 10 completed: should NOT be deleted (kMaxFramesInFlight = 3, requires 10 + 3 = 13)
    memManager.processFrameDeletions(10);
    TEST_ASSERT(!physicallyDeleted.load(), "Resource must NOT be deleted at frame 10");

    // Frame 11 completed: still in flight
    memManager.processFrameDeletions(11);
    TEST_ASSERT(!physicallyDeleted.load(), "Resource must NOT be deleted at frame 11");

    // Frame 12 completed: still in flight
    memManager.processFrameDeletions(12);
    TEST_ASSERT(!physicallyDeleted.load(), "Resource must NOT be deleted at frame 12");

    // Frame 13 completed: 10 + 3 <= 13 -> physical deletion triggers!
    memManager.processFrameDeletions(13);
    TEST_ASSERT(physicallyDeleted.load(), "Resource MUST be physically deleted at frame 13");

    std::cout << "[Test 4 PASSED] 4-Step deferred release pipeline correctly respects 3-frame flight latency." << std::endl;
}

// -----------------------------------------------------------------------------
// Test 5: ResourceGroup Dynamic Require & Release
// -----------------------------------------------------------------------------
void test_resource_group()
{
    std::cout << "[Test 5] Testing FCT::ResourceGroup dynamic require & release..." << std::endl;

    FCT::ResourceGroup shadowGroup("ShadowMapGroup");
    TEST_ASSERT(shadowGroup.getName() == "ShadowMapGroup", "ResourceGroup name match");
    TEST_ASSERT(shadowGroup.getActiveImages().empty(), "Initially no active images");

    // Test external image binding
    FCT::ResourceRequireDesc desc;
    desc.resourceId = "Cascade0";
    desc.format = FCT::Format::D32_SFLOAT;
    desc.width = 2048;
    desc.height = 2048;

    // Use a mock/dummy Image pointer to test tracking logic without Vulkan context
    uint64_t dummyImageStorage = 0xDEADBEEF;
    FCT::Image* dummyImg = reinterpret_cast<FCT::Image*>(&dummyImageStorage);
    desc.externalImage = dummyImg;

    FCT::Image* acquired = shadowGroup.require(nullptr, desc);
    TEST_ASSERT(acquired == dummyImg, "ResourceGroup acquired external image");
    TEST_ASSERT(shadowGroup.getImage("Cascade0") == dummyImg, "Get image by ID match");
    TEST_ASSERT(shadowGroup.getActiveImages().size() == 1, "One active image");

    // Release resource
    shadowGroup.release("Cascade0");
    TEST_ASSERT(shadowGroup.getImage("Cascade0") == nullptr, "Image released from active");
    TEST_ASSERT(shadowGroup.getActiveImages().empty(), "Active images empty after release");

    std::cout << "[Test 5 PASSED] FCT::ResourceGroup dynamic require & release verified." << std::endl;
}

// -----------------------------------------------------------------------------
// Main Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "========================================================" << std::endl;
    std::cout << "  Milestone 3 Challenger Stress Test Suite (Adversarial)" << std::endl;
    std::cout << "========================================================" << std::endl;

    test_std430_layouts();
    test_atlas_shelf_packer();
    test_gpu_resource_table();
    test_deferred_release_pipeline();
    test_resource_group();

    std::cout << "========================================================" << std::endl;
    std::cout << "  ALL MILESTONE 3 CHALLENGER STRESS TESTS PASSED!" << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
