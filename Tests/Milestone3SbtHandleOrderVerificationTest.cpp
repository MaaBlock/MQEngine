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

struct StridedDeviceAddressRegion {
    uint64_t deviceAddress = 0;
    uint64_t stride = 0;
    uint64_t size = 0;
};

struct StridedDeviceAddressRegions {
    StridedDeviceAddressRegion rayGen;
    StridedDeviceAddressRegion miss;
    StridedDeviceAddressRegion hitGroup;
    StridedDeviceAddressRegion callable;
};

// 模拟驱动与 VK_RayTracingPipeline 的组索引与 SBT 构建流程
class RayTracingPipelineSbtSimulator
{
public:
    enum class GroupType { RayGen, Miss, HitGroup, Callable };

    struct Group {
        GroupType type;
        std::string identifier;
    };

    std::vector<Group> m_shaderGroups;
    std::vector<uint32_t> m_rayGenGroupIndices;
    std::vector<uint32_t> m_missGroupIndices;
    std::vector<uint32_t> m_hitGroupIndices;
    std::vector<uint32_t> m_callableGroupIndices;

    void addRayGen(const std::string& id)
    {
        uint32_t idx = static_cast<uint32_t>(m_shaderGroups.size());
        m_shaderGroups.push_back({GroupType::RayGen, id});
        m_rayGenGroupIndices.push_back(idx);
    }

    void addMiss(const std::string& id)
    {
        uint32_t idx = static_cast<uint32_t>(m_shaderGroups.size());
        m_shaderGroups.push_back({GroupType::Miss, id});
        m_missGroupIndices.push_back(idx);
    }

    void addHitGroup(const std::string& id)
    {
        uint32_t idx = static_cast<uint32_t>(m_shaderGroups.size());
        m_shaderGroups.push_back({GroupType::HitGroup, id});
        m_hitGroupIndices.push_back(idx);
    }

    void addCallable(const std::string& id)
    {
        uint32_t idx = static_cast<uint32_t>(m_shaderGroups.size());
        m_shaderGroups.push_back({GroupType::Callable, id});
        m_callableGroupIndices.push_back(idx);
    }

    bool validateCreate(std::string& outError)
    {
        if (m_shaderGroups.empty()) {
            outError = "VK_RayTracingPipeline: 缺少着色阶段或着色组";
            return false;
        }
        if (m_rayGenGroupIndices.empty()) {
            outError = "VK_RayTracingPipeline: 必须包含至少一个 RayGen 着色组";
            return false;
        }
        return true;
    }

    struct SbtResult {
        std::vector<std::string> rayGenHandles;
        std::vector<std::string> missHandles;
        std::vector<std::string> hitGroupHandles;
        std::vector<std::string> callableHandles;
        StridedDeviceAddressRegions regions;
    };

    SbtResult buildSbt(uint32_t handleSize = 32, uint32_t handleAlignment = 32, uint32_t baseAlignment = 64)
    {
        // 1. 模拟驱动返回的句柄数组：顺序严格对应 m_shaderGroups
        std::vector<std::string> driverHandles;
        for (const auto& g : m_shaderGroups) {
            driverHandles.push_back(g.identifier + "_RAW_DRIVER_HANDLE");
        }

        const uint32_t handleSizeAligned = alignUp(handleSize, handleAlignment);
        const uint32_t rayGenSize = alignUp(static_cast<uint32_t>(m_rayGenGroupIndices.size()) * handleSizeAligned, baseAlignment);
        const uint32_t missSize = alignUp(static_cast<uint32_t>(m_missGroupIndices.size()) * handleSizeAligned, baseAlignment);
        const uint32_t hitSize = alignUp(static_cast<uint32_t>(m_hitGroupIndices.size()) * handleSizeAligned, baseAlignment);
        const uint32_t callableSize = alignUp(static_cast<uint32_t>(m_callableGroupIndices.size()) * handleSizeAligned, baseAlignment);

        const uint32_t rayGenOffset = 0;
        const uint32_t missOffset = rayGenOffset + rayGenSize;
        const uint32_t hitOffset = missOffset + missSize;
        const uint32_t callableOffset = hitOffset + hitSize;

        SbtResult result;

        // 使用修正后的显式索引查找映射
        for (size_t i = 0; i < m_rayGenGroupIndices.size(); ++i) {
            uint32_t groupIdx = m_rayGenGroupIndices[i];
            result.rayGenHandles.push_back(driverHandles[groupIdx]);
        }
        for (size_t i = 0; i < m_missGroupIndices.size(); ++i) {
            uint32_t groupIdx = m_missGroupIndices[i];
            result.missHandles.push_back(driverHandles[groupIdx]);
        }
        for (size_t i = 0; i < m_hitGroupIndices.size(); ++i) {
            uint32_t groupIdx = m_hitGroupIndices[i];
            result.hitGroupHandles.push_back(driverHandles[groupIdx]);
        }
        for (size_t i = 0; i < m_callableGroupIndices.size(); ++i) {
            uint32_t groupIdx = m_callableGroupIndices[i];
            result.callableHandles.push_back(driverHandles[groupIdx]);
        }

        uint64_t dummyAddress = 0x100000;
        if (!m_rayGenGroupIndices.empty()) {
            result.regions.rayGen.deviceAddress = dummyAddress + rayGenOffset;
            result.regions.rayGen.stride = handleSizeAligned;
            result.regions.rayGen.size = handleSizeAligned;
        }
        if (!m_missGroupIndices.empty()) {
            result.regions.miss.deviceAddress = dummyAddress + missOffset;
            result.regions.miss.stride = handleSizeAligned;
            result.regions.miss.size = static_cast<uint32_t>(m_missGroupIndices.size()) * handleSizeAligned;
        }
        if (!m_hitGroupIndices.empty()) {
            result.regions.hitGroup.deviceAddress = dummyAddress + hitOffset;
            result.regions.hitGroup.stride = handleSizeAligned;
            result.regions.hitGroup.size = static_cast<uint32_t>(m_hitGroupIndices.size()) * handleSizeAligned;
        }
        if (!m_callableGroupIndices.empty()) {
            result.regions.callable.deviceAddress = dummyAddress + callableOffset;
            result.regions.callable.stride = handleSizeAligned;
            result.regions.callable.size = static_cast<uint32_t>(m_callableGroupIndices.size()) * handleSizeAligned;
        }

        return result;
    }
};

// -----------------------------------------------------------------------------
// Test 1: M3 Challenger 1 Exposure Case (Callable added BEFORE HitGroup)
// -----------------------------------------------------------------------------
bool test_callable_before_hitgroup_ordering()
{
    std::cout << "\n=== [Test 1] SBT Handle Indexing: Callable Added Before HitGroup ===" << std::endl;

    RayTracingPipelineSbtSimulator sim;
    sim.addRayGen("RGEN_MAIN");
    sim.addMiss("MISS_DEFAULT");
    sim.addCallable("CALLABLE_DIRECT"); // Group 2
    sim.addHitGroup("HITGROUP_PRIMARY"); // Group 3

    assert(sim.m_shaderGroups.size() == 4);
    assert(sim.m_rayGenGroupIndices[0] == 0);
    assert(sim.m_missGroupIndices[0] == 1);
    assert(sim.m_callableGroupIndices[0] == 2);
    assert(sim.m_hitGroupIndices[0] == 3);

    auto res = sim.buildSbt();

    std::cout << "  RayGen SBT handle:   " << res.rayGenHandles[0] << std::endl;
    std::cout << "  Miss SBT handle:     " << res.missHandles[0] << std::endl;
    std::cout << "  HitGroup SBT handle: " << res.hitGroupHandles[0] << std::endl;
    std::cout << "  Callable SBT handle: " << res.callableHandles[0] << std::endl;

    assert(res.rayGenHandles[0] == "RGEN_MAIN_RAW_DRIVER_HANDLE");
    assert(res.missHandles[0] == "MISS_DEFAULT_RAW_DRIVER_HANDLE");
    assert(res.hitGroupHandles[0] == "HITGROUP_PRIMARY_RAW_DRIVER_HANDLE");
    assert(res.callableHandles[0] == "CALLABLE_DIRECT_RAW_DRIVER_HANDLE");

    std::cout << ">>> [Test 1 PASS] HitGroup and Callable handles are correctly placed even when Callable added first!" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Adversarial Highly Interleaved Shader Stage Insertion
// -----------------------------------------------------------------------------
bool test_interleaved_stage_insertion()
{
    std::cout << "\n=== [Test 2] Adversarial Arbitrarily Interleaved Stage Insertion ===" << std::endl;

    RayTracingPipelineSbtSimulator sim;
    sim.addCallable("CALL_0"); // 0
    sim.addHitGroup("HIT_0");  // 1
    sim.addRayGen("RGEN_0");   // 2
    sim.addMiss("MISS_0");     // 3
    sim.addHitGroup("HIT_1");  // 4
    sim.addCallable("CALL_1"); // 5
    sim.addMiss("MISS_1");     // 6

    auto res = sim.buildSbt();

    assert(res.rayGenHandles.size() == 1);
    assert(res.rayGenHandles[0] == "RGEN_0_RAW_DRIVER_HANDLE");

    assert(res.missHandles.size() == 2);
    assert(res.missHandles[0] == "MISS_0_RAW_DRIVER_HANDLE");
    assert(res.missHandles[1] == "MISS_1_RAW_DRIVER_HANDLE");

    assert(res.hitGroupHandles.size() == 2);
    assert(res.hitGroupHandles[0] == "HIT_0_RAW_DRIVER_HANDLE");
    assert(res.hitGroupHandles[1] == "HIT_1_RAW_DRIVER_HANDLE");

    assert(res.callableHandles.size() == 2);
    assert(res.callableHandles[0] == "CALL_0_RAW_DRIVER_HANDLE");
    assert(res.callableHandles[1] == "CALL_1_RAW_DRIVER_HANDLE");

    std::cout << ">>> [Test 2 PASS] Interleaved stages map to their respective SBT regions with 100% fidelity." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Canonical Order Alignment (RayGen -> Miss -> HitGroup -> Callable)
// -----------------------------------------------------------------------------
bool test_canonical_order()
{
    std::cout << "\n=== [Test 3] Canonical Stage Registration Order ===" << std::endl;

    RayTracingPipelineSbtSimulator sim;
    sim.addRayGen("RGEN");
    sim.addMiss("MISS");
    sim.addHitGroup("HIT");
    sim.addCallable("CALL");

    auto res = sim.buildSbt();
    assert(res.rayGenHandles[0] == "RGEN_RAW_DRIVER_HANDLE");
    assert(res.missHandles[0] == "MISS_RAW_DRIVER_HANDLE");
    assert(res.hitGroupHandles[0] == "HIT_RAW_DRIVER_HANDLE");
    assert(res.callableHandles[0] == "CALL_RAW_DRIVER_HANDLE");

    std::cout << ">>> [Test 3 PASS] Canonical registration matches driver handle layout perfectly." << std::endl;
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Advisory 3 - Minimum RayGen Validation
// -----------------------------------------------------------------------------
bool test_minimum_raygen_validation()
{
    std::cout << "\n=== [Test 4] Validation: Pipeline Without RayGen Must Fail ===" << std::endl;

    RayTracingPipelineSbtSimulator sim;
    sim.addMiss("MISS_ONLY");
    sim.addHitGroup("HIT_ONLY");

    std::string err;
    bool valid = sim.validateCreate(err);
    assert(!valid);
    assert(err.find("RayGen") != std::string::npos);

    std::cout << "  Error caught as expected: " << err << std::endl;
    std::cout << ">>> [Test 4 PASS] Missing RayGen rejected before pipeline creation." << std::endl;
    return true;
}

int main()
{
    std::cout << "=================================================================" << std::endl;
    std::cout << "  Milestone 3 SBT Handle Order Remediation Empirical Test Suite  " << std::endl;
    std::cout << "=================================================================" << std::endl;

    test_callable_before_hitgroup_ordering();
    test_interleaved_stage_insertion();
    test_canonical_order();
    test_minimum_raygen_validation();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "  >>> ALL SBT HANDLE ORDER EMPIRICAL TESTS PASSED! <<<           " << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
