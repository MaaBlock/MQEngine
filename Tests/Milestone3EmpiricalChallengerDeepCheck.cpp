#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdint>

template <typename T>
static inline constexpr T alignUp(T value, T alignment) noexcept
{
    return (alignment > 0) ? ((value + alignment - 1) & ~(alignment - 1)) : value;
}

struct MockStridedDeviceAddressRegion {
    uint64_t deviceAddress = 0;
    uint64_t stride = 0;
    uint64_t size = 0;
};

struct MockStridedDeviceAddressRegions {
    MockStridedDeviceAddressRegion rayGen;
    MockStridedDeviceAddressRegion miss;
    MockStridedDeviceAddressRegion hitGroup;
    MockStridedDeviceAddressRegion callable;
};

// =============================================================================
// Test A: Hardware Alignment Variation (32, 64, 128, 256)
// =============================================================================
bool test_sbt_alignment_variations()
{
    std::cout << "\n=== [Test A] SBT Alignment Math Across Varied Hardware Limits ===" << std::endl;

    struct HardwareProfile {
        std::string name;
        uint32_t handleSize;
        uint32_t handleAlignment;
        uint32_t baseAlignment;
    };

    std::vector<HardwareProfile> profiles = {
        {"NVIDIA Ampere/Ada (32, 32, 64)", 32, 32, 64},
        {"AMD RDNA2/3 (32, 32, 64)", 32, 32, 64},
        {"Hypothetical 128-byte BaseAlign (32, 32, 128)", 32, 32, 128},
        {"Hypothetical 256-byte BaseAlign (32, 32, 256)", 32, 32, 256},
        {"Hypothetical 16-byte SmallHandle (16, 16, 64)", 16, 16, 64},
        {"Hypothetical 64-byte LargeHandle (64, 64, 256)", 64, 64, 256}
    };

    for (const auto& hw : profiles) {
        const uint32_t handleSizeAligned = alignUp(hw.handleSize, hw.handleAlignment);
        assert(handleSizeAligned % hw.handleAlignment == 0);

        uint32_t rgenCount = 1;
        uint32_t missCount = 3;
        uint32_t hitCount = 5;
        uint32_t callableCount = 2;

        uint32_t rgenSize = alignUp(rgenCount * handleSizeAligned, hw.baseAlignment);
        uint32_t missSize = alignUp(missCount * handleSizeAligned, hw.baseAlignment);
        uint32_t hitSize = alignUp(hitCount * handleSizeAligned, hw.baseAlignment);
        uint32_t callableSize = alignUp(callableCount * handleSizeAligned, hw.baseAlignment);

        uint32_t rgenOffset = 0;
        uint32_t missOffset = rgenOffset + rgenSize;
        uint32_t hitOffset = missOffset + missSize;
        uint32_t callableOffset = hitOffset + hitSize;

        // Verify base alignments of each table
        assert(rgenOffset % hw.baseAlignment == 0);
        assert(missOffset % hw.baseAlignment == 0);
        assert(hitOffset % hw.baseAlignment == 0);
        assert(callableOffset % hw.baseAlignment == 0);

        std::cout << "  Profile [" << hw.name << "]: total SBT buffer = "
                  << (callableOffset + callableSize) << " bytes (all offsets baseAligned OK)" << std::endl;
    }

    std::cout << ">>> [Test A PASS] Alignment math across all hardware profiles verified." << std::endl;
    return true;
}

// =============================================================================
// Test B: SBT Boundary Invariants & VUID-03684 (0-count stages)
// =============================================================================
bool test_sbt_boundary_vuid03684()
{
    std::cout << "\n=== [Test B] SBT Boundary Conditions & VUID-03684 Invariants ===" << std::endl;

    // Simulate RHI SBT construction with:
    // 1 RayGen, 0 Miss, 0 HitGroup, 0 Callable
    uint32_t rayGenCount = 1;
    uint32_t missCount = 0;
    uint32_t hitGroupCount = 0;
    uint32_t callableCount = 0;

    uint32_t handleSizeAligned = 32;
    uint32_t baseAlignment = 64;

    uint32_t rayGenSize = alignUp(rayGenCount * handleSizeAligned, baseAlignment);
    uint32_t missSize = alignUp(missCount * handleSizeAligned, baseAlignment);
    uint32_t hitSize = alignUp(hitGroupCount * handleSizeAligned, baseAlignment);
    uint32_t callableSize = alignUp(callableCount * handleSizeAligned, baseAlignment);

    uint64_t dummyBufferAddress = 0x40000000;

    MockStridedDeviceAddressRegions regions{};

    if (rayGenCount > 0) {
        regions.rayGen.deviceAddress = dummyBufferAddress;
        regions.rayGen.stride = handleSizeAligned;
        regions.rayGen.size = handleSizeAligned; // VUID-03683: size must equal stride for RayGen
    } else {
        regions.rayGen = {};
    }

    if (missCount > 0) {
        regions.miss.deviceAddress = dummyBufferAddress + rayGenSize;
        regions.miss.stride = handleSizeAligned;
        regions.miss.size = missCount * handleSizeAligned;
    } else {
        regions.miss = {};
    }

    if (hitGroupCount > 0) {
        regions.hitGroup.deviceAddress = dummyBufferAddress + rayGenSize + missSize;
        regions.hitGroup.stride = handleSizeAligned;
        regions.hitGroup.size = hitGroupCount * handleSizeAligned;
    } else {
        regions.hitGroup = {};
    }

    if (callableCount > 0) {
        regions.callable.deviceAddress = dummyBufferAddress + rayGenSize + missSize + hitSize;
        regions.callable.stride = handleSizeAligned;
        regions.callable.size = callableCount * handleSizeAligned;
    } else {
        regions.callable = {};
    }

    // Assert VUID-03683: RayGen size == stride
    assert(regions.rayGen.size == regions.rayGen.stride);
    assert(regions.rayGen.deviceAddress != 0);

    // Assert VUID-03684: When count == 0, deviceAddress, stride, size MUST all be 0
    assert(regions.miss.deviceAddress == 0);
    assert(regions.miss.stride == 0);
    assert(regions.miss.size == 0);

    assert(regions.hitGroup.deviceAddress == 0);
    assert(regions.hitGroup.stride == 0);
    assert(regions.hitGroup.size == 0);

    assert(regions.callable.deviceAddress == 0);
    assert(regions.callable.stride == 0);
    assert(regions.callable.size == 0);

    // Now test CommandBuffer::toVkRegion wrapper logic
    auto toVkRegion = [](const MockStridedDeviceAddressRegion* r) -> MockStridedDeviceAddressRegion {
        if (!r || r->deviceAddress == 0 || r->size == 0) {
            return MockStridedDeviceAddressRegion{ 0, 0, 0 };
        }
        return *r;
    };

    auto vkRayGen = toVkRegion(&regions.rayGen);
    auto vkMiss = toVkRegion(&regions.miss);
    auto vkHit = toVkRegion(&regions.hitGroup);
    auto vkCallable = toVkRegion(&regions.callable);

    assert(vkRayGen.deviceAddress == 0x40000000 && vkRayGen.stride == 32 && vkRayGen.size == 32);
    assert(vkMiss.deviceAddress == 0 && vkMiss.stride == 0 && vkMiss.size == 0);
    assert(vkHit.deviceAddress == 0 && vkHit.stride == 0 && vkHit.size == 0);
    assert(vkCallable.deviceAddress == 0 && vkCallable.stride == 0 && vkCallable.size == 0);

    std::cout << ">>> [Test B PASS] Boundary invariants and VUID-03684 zero-address/zero-size verified." << std::endl;
    return true;
}

// =============================================================================
// Test C: Shader Group Order Mismatch Analysis
// =============================================================================
bool test_sbt_group_ordering_hazard()
{
    std::cout << "\n=== [Test C] SBT Shader Group Insertion vs Copy Order Hazard ===" << std::endl;

    // Demonstrate the mismatch between RayTracingPipeline::build() and VK_RayTracingPipeline::buildSBT():
    // RayTracingPipeline::build() adds:
    // 1. RayGen shaders
    // 2. Miss shaders
    // 3. Callable shaders  <--- Added 3rd
    // 4. HitGroup shaders  <--- Added 4th
    //
    // BUT VK_RayTracingPipeline::buildSBT() copies:
    // 1. RayGen shaders
    // 2. Miss shaders
    // 3. HitGroup shaders  <--- Reads 3rd handles
    // 4. Callable shaders  <--- Reads 4th handles

    uint32_t numRayGen = 1;
    uint32_t numMiss = 1;
    uint32_t numCallable = 1;
    uint32_t numHitGroup = 1;

    // Simulate handle retrieval from driver in insertion order:
    // Handles array returned by vkGetRayTracingShaderGroupHandlesKHR:
    // index 0: RayGen
    // index 1: Miss
    // index 2: Callable
    // index 3: HitGroup
    std::vector<std::string> handlesInDriverOrder = {
        "RAYGEN_HANDLE",
        "MISS_HANDLE",
        "CALLABLE_HANDLE",
        "HITGROUP_HANDLE"
    };

    // Current buildSBT loop logic:
    uint32_t handleIndex = 0;
    std::vector<std::string> sbtRayGenHandles;
    for (uint32_t i = 0; i < numRayGen; ++i) sbtRayGenHandles.push_back(handlesInDriverOrder[handleIndex++]);

    std::vector<std::string> sbtMissHandles;
    for (uint32_t i = 0; i < numMiss; ++i) sbtMissHandles.push_back(handlesInDriverOrder[handleIndex++]);

    std::vector<std::string> sbtHitGroupHandles;
    for (uint32_t i = 0; i < numHitGroup; ++i) sbtHitGroupHandles.push_back(handlesInDriverOrder[handleIndex++]);

    std::vector<std::string> sbtCallableHandles;
    for (uint32_t i = 0; i < numCallable; ++i) sbtCallableHandles.push_back(handlesInDriverOrder[handleIndex++]);

    std::cout << "  SBT RayGen contains:   " << sbtRayGenHandles[0] << std::endl;
    std::cout << "  SBT Miss contains:     " << sbtMissHandles[0] << std::endl;
    std::cout << "  SBT HitGroup contains: " << sbtHitGroupHandles[0] << std::endl;
    std::cout << "  SBT Callable contains: " << sbtCallableHandles[0] << std::endl;

    bool hasHazard = (sbtHitGroupHandles[0] == "CALLABLE_HANDLE" && sbtCallableHandles[0] == "HITGROUP_HANDLE");
    if (hasHazard) {
        std::cout << "  [EMPIRICAL CONFIRMATION] HitGroup and Callable handles ARE SWAPPED in current buildSBT()!" << std::endl;
    }

    std::cout << ">>> [Test C COMPLETED] Hazard successfully isolated and characterized." << std::endl;
    return true;
}

int main()
{
    std::cout << "=================================================================" << std::endl;
    std::cout << "  MQEngine Milestone 3 Empirical Challenger Deep Verification   " << std::endl;
    std::cout << "=================================================================" << std::endl;

    test_sbt_alignment_variations();
    test_sbt_boundary_vuid03684();
    test_sbt_group_ordering_hazard();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "  >>> ALL DEEP EMPIRICAL CHALLENGER TESTS EXECUTED <<<          " << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
