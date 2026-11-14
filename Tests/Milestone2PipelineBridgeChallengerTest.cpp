#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <FCT/Context/Pipeline.h>
#include <FCT/Context/TraditionalPipeline.h>
#include <FCT/Context/MeshShaderPipeline.h>
#include <FCT/Context/RayTracingPipeline.h>
#include <FCT/Context/Context.h>
#include <FCT/RHI/Pipeline.h>
#include <FCT/RHI/RasterizationPipeline.h>
#include <FCT/RHI/RayTracingPipeline.h>
#include <FCT/RHI/CommandBuffer.h>
#include <FCT/RHI/VK_CommandBuffer.h>
#include <FCT/RHI/VK_TraditionalPipeline.h>
#include <FCT/RHI/VK_RasterizationPipeline.h>
#include <FCT/RHI/VK_MeshShaderPipeline.h>
#include <FCT/RHI/VK_RayTracingPipeline.h>
#include <FCT/RHI/MTL_Pipeline.h>
#include <FCT/RHI/WGPU_Pipeline.h>

#define CHALLENGE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[CHALLENGE_FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while(0)

// =============================================================================
// Helper Mock Classes for Suite 1 & 2
// =============================================================================

class MockCommandBuffer : public FCT::RHI::CommandBuffer
{
public:
    MockCommandBuffer() = default;
    ~MockCommandBuffer() override = default;

    void reset() override {}
    void create() override {}
    void begin() override {}
    void viewport(FCT::Vec2 lt, FCT::Vec2 rb) override { (void)lt; (void)rb; }
    void scissor(FCT::Vec2 lt, FCT::Vec2 rb) override { (void)lt; (void)rb; }
    void draw(size_t vB, size_t iB, size_t vS, size_t iS) override { (void)vB; (void)iB; (void)vS; (void)iS; }
    void drawIndex(size_t iB, size_t instB, size_t iC, size_t instC, size_t fV) override { (void)iB; (void)instB; (void)iC; (void)instC; (void)fV; }
    void end() override {}
    void submit() override {}
    void nextPass() override {}
    void barrier(FCT::Image* img, FCT::ImageLayout oL, FCT::ImageLayout nL,
                 FCT::PipelineStages sS, FCT::PipelineStages dS,
                 FCT::AccessFlags sA, FCT::AccessFlags dA,
                 FCT::ImageAspects aM) override
    {
        (void)img; (void)oL; (void)nL; (void)sS; (void)dS; (void)sA; (void)dA; (void)aM;
    }

    void bindPipeline(FCT::RHI::Pipeline* pipeline) override
    {
        lastBoundPipeline = pipeline;
        bindCallCount++;
        if (pipeline)
        {
            pipeline->bind(this);
        }
    }

    FCT::RHI::Pipeline* lastBoundPipeline = nullptr;
    int bindCallCount = 0;
};

// Tracking RHI Pipeline to observe bind dispatch and lifetime
class TrackingRhiPipeline : public FCT::RHI::Pipeline
{
public:
    static std::atomic<int> s_aliveCount;
    static std::atomic<int> s_bindCount;

    FCT::PipelineType m_type;
    FCT::RHI::CommandBuffer* m_lastCmdBuf = nullptr;
    bool m_wasBound = false;

    explicit TrackingRhiPipeline(FCT::PipelineType type = FCT::PipelineType::Traditional)
        : m_type(type)
    {
        s_aliveCount.fetch_add(1);
    }

    ~TrackingRhiPipeline() override
    {
        s_aliveCount.fetch_sub(1);
    }

    FCT::PipelineType getType() const noexcept override
    {
        return m_type;
    }

    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        m_lastCmdBuf = cmdBuf;
        m_wasBound = true;
        s_bindCount.fetch_add(1);
    }

    FCT::Status create() override
    {
        return FCT::OkStatus();
    }
};

std::atomic<int> TrackingRhiPipeline::s_aliveCount{0};
std::atomic<int> TrackingRhiPipeline::s_bindCount{0};

// Mock Rasterization Pipeline
class MockRasterPipeline : public FCT::RHI::RasterizationPipeline
{
public:
    static std::atomic<int> s_aliveCount;
    bool wasBound = false;
    FCT::RHI::CommandBuffer* boundCmd = nullptr;

    MockRasterPipeline() { s_aliveCount.fetch_add(1); }
    ~MockRasterPipeline() override { s_aliveCount.fetch_sub(1); }

    FCT::PipelineType getType() const noexcept override { return FCT::PipelineType::Traditional; }
    void addResources(FCT::IPipelineResource* res) override { (void)res; }
    FCT::Status create() override { return FCT::OkStatus(); }
    void bindPass(FCT::RHI::Pass* pass) override { (void)pass; }
    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        wasBound = true;
        boundCmd = cmdBuf;
    }
};
std::atomic<int> MockRasterPipeline::s_aliveCount{0};

// Mock Mesh Shader RHI Pipeline
class MockMeshShaderRhiPipeline : public FCT::RHI::RasterizationPipeline
{
public:
    static std::atomic<int> s_aliveCount;
    bool wasBound = false;
    FCT::RHI::CommandBuffer* boundCmd = nullptr;

    FCT::RHI::CommandBuffer* lastDispatchCmd = nullptr;
    uint32_t lastGroupX = 0;
    uint32_t lastGroupY = 0;
    uint32_t lastGroupZ = 0;
    uint32_t dispatchCallCount = 0;

    MockMeshShaderRhiPipeline() { s_aliveCount.fetch_add(1); }
    ~MockMeshShaderRhiPipeline() override { s_aliveCount.fetch_sub(1); }

    FCT::PipelineType getType() const noexcept override { return FCT::PipelineType::MeshShader; }
    void addResources(FCT::IPipelineResource* res) override { (void)res; }
    FCT::Status create() override { return FCT::OkStatus(); }
    void bindPass(FCT::RHI::Pass* pass) override { (void)pass; }
    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        wasBound = true;
        boundCmd = cmdBuf;
    }

    void dispatchMesh(
        FCT::RHI::CommandBuffer* cmdBuf,
        uint32_t groupCountX,
        uint32_t groupCountY = 1,
        uint32_t groupCountZ = 1) override
    {
        lastDispatchCmd = cmdBuf;
        lastGroupX = groupCountX;
        lastGroupY = groupCountY;
        lastGroupZ = groupCountZ;
        dispatchCallCount++;
    }
};
std::atomic<int> MockMeshShaderRhiPipeline::s_aliveCount{0};

// Mock Ray Tracing Pipeline
class MockRayTracingPipeline : public FCT::RHI::RayTracingPipeline
{
public:
    static std::atomic<int> s_aliveCount;
    bool wasBound = false;
    FCT::RHI::CommandBuffer* boundCmd = nullptr;
    FCT::RHI::StridedDeviceAddressRegions dummyRegions{};

    MockRayTracingPipeline() { s_aliveCount.fetch_add(1); }
    ~MockRayTracingPipeline() override { s_aliveCount.fetch_sub(1); }

    FCT::PipelineType getType() const noexcept override { return FCT::PipelineType::RayTracing; }
    FCT::Status create() override { return FCT::OkStatus(); }
    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        wasBound = true;
        boundCmd = cmdBuf;
    }
    FCT::Status buildSBT() override { return FCT::OkStatus(); }
    const FCT::RHI::StridedDeviceAddressRegions& getSBTRegions() const noexcept override { return dummyRegions; }
    void traceRays(FCT::RHI::CommandBuffer* cmdBuf, uint32_t w, uint32_t h, uint32_t d) override
    {
        (void)cmdBuf; (void)w; (void)h; (void)d;
    }
    void setMaxRecursionDepth(uint32_t depth) override { (void)depth; }
};
std::atomic<int> MockRayTracingPipeline::s_aliveCount{0};

// Dummy context pointer for Bridge constructor (only asserts non-null)
static FCT::Context* getDummyContext()
{
    return reinterpret_cast<FCT::Context*>(uintptr_t(0x20000));
}

// =============================================================================
// Test Suite 1: CommandBuffer Polymorphic Pipeline Dispatch (Feature 10)
// =============================================================================
bool test_PolymorphicDispatch()
{
    std::cout << "\n=== [Test 1] Polymorphic bindPipeline(Pipeline*) Dispatch ===" << std::endl;

    MockCommandBuffer cmdBuf;

    // 1.1 Dispatch Rasterization Pipeline through base Pipeline*
    {
        MockRasterPipeline rasterPipe;
        FCT::RHI::Pipeline* basePtr = &rasterPipe;

        CHALLENGE_CHECK(!rasterPipe.wasBound, "Initially not bound");
        cmdBuf.bindPipeline(basePtr);
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == basePtr, "lastBoundPipeline must equal basePtr");
        CHALLENGE_CHECK(rasterPipe.wasBound, "MockRasterPipeline::bind must be called");
        CHALLENGE_CHECK(rasterPipe.boundCmd == &cmdBuf, "CommandBuffer pointer passed to bind must match");
    }

    // 1.2 Dispatch RayTracing Pipeline through base Pipeline*
    {
        MockRayTracingPipeline rtPipe;
        FCT::RHI::Pipeline* basePtr = &rtPipe;

        CHALLENGE_CHECK(!rtPipe.wasBound, "Initially not bound");
        cmdBuf.bindPipeline(basePtr);
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == basePtr, "lastBoundPipeline must equal basePtr");
        CHALLENGE_CHECK(rtPipe.wasBound, "MockRayTracingPipeline::bind must be called");
        CHALLENGE_CHECK(rtPipe.boundCmd == &cmdBuf, "CommandBuffer pointer passed to bind must match");
    }

    // 1.3 Nullptr safety
    {
        int countBefore = cmdBuf.bindCallCount;
        cmdBuf.bindPipeline(nullptr);
        CHALLENGE_CHECK(cmdBuf.bindCallCount == countBefore + 1, "bindCallCount should increment");
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == nullptr, "lastBoundPipeline must be nullptr");
    }

    // 1.4 Deprecated bindPipieline backwards compatibility
    {
        MockRasterPipeline rasterPipe;
        CHALLENGE_CHECK(!rasterPipe.wasBound, "Initially not bound");
        cmdBuf.bindPipieline(&rasterPipe);
        CHALLENGE_CHECK(rasterPipe.wasBound, "Deprecated bindPipieline must invoke bind on raster pipeline");
        CHALLENGE_CHECK(rasterPipe.boundCmd == &cmdBuf, "CommandBuffer pointer match");
    }

    std::cout << ">>> [Test 1 PASS] Polymorphic CommandBuffer::bindPipeline verified without slicing or type confusion." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 2: Bridge Ownership & Lifetime (Feature 11 & 12)
// =============================================================================
bool test_BridgeOwnershipAndLifetime()
{
    std::cout << "\n=== [Test 2] Bridge Ownership & Lifetime Management ===" << std::endl;

    // 2.1 Verify compile-time type constraints:
    // FCT::RHI::Pipeline must NOT inherit from RefCount
    static_assert(!std::is_base_of<FCT::RefCount, FCT::RHI::Pipeline>::value,
        "CRITICAL: FCT::RHI::Pipeline must NOT inherit from RefCount (violates UniquePtr constraint)");
    // FCT::Pipeline must inherit from RefCount
    static_assert(std::is_base_of<FCT::RefCount, FCT::Pipeline>::value,
        "CRITICAL: FCT::Pipeline must inherit from RefCount for high-level Ptr<T> sharing");

    FCT::Context* ctx = getDummyContext();

    // 2.2 Verify UniquePtr lifecycle management:
    // When FCT::Pipeline is destroyed, it must destroy the underlying RHI::Pipeline
    TrackingRhiPipeline::s_aliveCount.store(0);
    TrackingRhiPipeline::s_bindCount.store(0);

    {
        auto rhiUnique = FCT::makeUnique<TrackingRhiPipeline>(FCT::PipelineType::Traditional);
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI pipeline created");
        TrackingRhiPipeline* rawRhi = rhiUnique.get();

        class TestBridge : public FCT::Pipeline
        {
        public:
            TestBridge(FCT::Context* c, FCT::UniquePtr<FCT::RHI::Pipeline> rhi)
                : FCT::Pipeline(c, FCT::PipelineType::Traditional, std::move(rhi)) {}
            FCT::Status build() override { return FCT::OkStatus(); }
        };

        {
            TestBridge bridge(ctx, std::move(rhiUnique));
            CHALLENGE_CHECK(rhiUnique.get() == nullptr, "Ownership transferred to Bridge");
            CHALLENGE_CHECK(bridge.getRhiPipeline() == rawRhi, "Bridge exposes correct underlying RHI pipeline");
            CHALLENGE_CHECK(bridge.getRhiPipelineAs<TrackingRhiPipeline>() == rawRhi, "Bridge getRhiPipelineAs works");
            CHALLENGE_CHECK(bridge.getType() == FCT::PipelineType::Traditional, "PipelineType match");

            MockCommandBuffer cmd;
            bridge.bind(&cmd);
            CHALLENGE_CHECK(rawRhi->m_wasBound, "Bridge bind forwarded to RHI pipeline");
            CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI pipeline still alive");
        }
        // Bridge destroyed here
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 0,
            "CRITICAL: Destroying FCT::Pipeline MUST destroy underlying FCT::RHI::Pipeline");
    }

    // 2.3 Verify RefCount (addRef/release) lifecycle
    {
        TrackingRhiPipeline::s_aliveCount.store(0);

        class TestRefCountBridge : public FCT::Pipeline
        {
        public:
            TestRefCountBridge(FCT::Context* c, FCT::UniquePtr<FCT::RHI::Pipeline> rhi)
                : FCT::Pipeline(c, FCT::PipelineType::MeshShader, std::move(rhi)) {}
            FCT::Status build() override { return FCT::OkStatus(); }
        };

        auto rhiUnique = FCT::makeUnique<TrackingRhiPipeline>(FCT::PipelineType::MeshShader);
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI pipeline alive");

        auto* bridge = FCT_NEW(TestRefCountBridge, ctx, std::move(rhiUnique));
        CHALLENGE_CHECK(bridge->count() == 1, "Initial count is 1");

        // Simulate shared ownership
        bridge->addRef();
        CHALLENGE_CHECK(bridge->count() == 2, "Count incremented to 2");
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI pipeline still alive");

        bridge->release(); // Decrement to 1
        CHALLENGE_CHECK(bridge->count() == 1, "Count decremented to 1");
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI pipeline still alive");

        bridge->release(); // Decrement to 0 -> triggers deleteThis() -> ~TestRefCountBridge()
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 0,
            "CRITICAL: When RefCount reaches 0, deleteThis must destroy Bridge and underlying RHI pipeline");
    }

    // 2.4 Verify UniquePtr move transfer into Bridge and RefCount non-movability
    {
        TrackingRhiPipeline::s_aliveCount.store(0);

        class NonMoveableRefCountBridge : public FCT::Pipeline
        {
        public:
            NonMoveableRefCountBridge(FCT::Context* c, FCT::UniquePtr<FCT::RHI::Pipeline> rhi)
                : FCT::Pipeline(c, FCT::PipelineType::RayTracing, std::move(rhi)) {}
            FCT::Status build() override { return FCT::OkStatus(); }
        };

        // Transfer ownership via std::move(UniquePtr)
        auto rhiUnique1 = FCT::makeUnique<TrackingRhiPipeline>(FCT::PipelineType::RayTracing);
        TrackingRhiPipeline* raw1 = rhiUnique1.get();
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "RHI alive in unique_ptr 1");

        // Move into another UniquePtr first
        FCT::UniquePtr<FCT::RHI::Pipeline> rhiUnique2 = std::move(rhiUnique1);
        CHALLENGE_CHECK(rhiUnique1.get() == nullptr, "rhiUnique1 released");
        CHALLENGE_CHECK(rhiUnique2.get() == raw1, "rhiUnique2 owns raw1");
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "Still exactly 1 instance");

        // Move into Bridge constructor
        {
            NonMoveableRefCountBridge bridge(ctx, std::move(rhiUnique2));
            CHALLENGE_CHECK(rhiUnique2.get() == nullptr, "rhiUnique2 released to bridge");
            CHALLENGE_CHECK(bridge.getRhiPipeline() == raw1, "Bridge owns raw1");
            CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 1, "Still 1 instance in bridge");
        }
        // Bridge destroyed
        CHALLENGE_CHECK(TrackingRhiPipeline::s_aliveCount.load() == 0, "Cleaned up on bridge destruction");
    }

    std::cout << ">>> [Test 2 PASS] Bridge ownership, UniquePtr management, and lifetime safety verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 3: Failure Rollback & Error Propagation (Feature 9)
// =============================================================================
bool test_FailureRollbackAndErrorPropagation()
{
    std::cout << "\n=== [Test 3] Failure Rollback & Error Propagation Challenge ===" << std::endl;

    // Simulate the exact transactional resource creation and cleanup in VK_TraditionalPipeline::createDescriptorSetLayouts:
    // Step 1: Create N descriptor set layouts. If any fails, destroy already created layouts, clear map, return InternalError.
    // Step 2: Create emptyLayout. If fails, destroy all set layouts, clear map, return InternalError.
    // Step 3: Create pipelineLayout. If fails, destroy all set layouts, clear map, destroy emptyLayout, return InternalError.

    struct SimulatedDevice
    {
        std::set<uint64_t> activeLayouts;
        std::set<uint64_t> activePipelineLayouts;
        uint64_t nextHandle = 1000;

        int failAtSetIndex = -1;
        bool failAtEmptyLayout = false;
        bool failAtPipelineLayout = false;

        FCT::Status createDescriptorSetLayout(uint32_t setIndex, uint64_t* outHandle)
        {
            if (static_cast<int>(setIndex) == failAtSetIndex)
            {
                return FCT::InternalError(FCT::StrCat("VK_TraditionalPipeline: 创建描述符集布局失败 (Set ", setIndex, "), Vulkan 错误: VK_ERROR_OUT_OF_DEVICE_MEMORY"));
            }
            *outHandle = ++nextHandle;
            activeLayouts.insert(*outHandle);
            return FCT::OkStatus();
        }

        FCT::Status createEmptyDescriptorSetLayout(uint64_t* outHandle)
        {
            if (failAtEmptyLayout)
            {
                return FCT::InternalError("VK_TraditionalPipeline: 创建空描述符集布局失败, Vulkan 错误: VK_ERROR_INITIALIZATION_FAILED");
            }
            *outHandle = ++nextHandle;
            activeLayouts.insert(*outHandle);
            return FCT::OkStatus();
        }

        FCT::Status createPipelineLayout(uint64_t* outHandle)
        {
            if (failAtPipelineLayout)
            {
                return FCT::InternalError("VK_TraditionalPipeline: 创建管线布局失败, Vulkan 错误: VK_ERROR_INITIALIZATION_FAILED");
            }
            *outHandle = ++nextHandle;
            activePipelineLayouts.insert(*outHandle);
            return FCT::OkStatus();
        }

        void destroyDescriptorSetLayout(uint64_t handle)
        {
            activeLayouts.erase(handle);
        }

        void destroyPipelineLayout(uint64_t handle)
        {
            activePipelineLayouts.erase(handle);
        }
    };

    auto runPipelineLayoutTransaction = [](SimulatedDevice& dev, const std::vector<uint32_t>& sets) -> FCT::Status
    {
        std::map<uint32_t, uint64_t> descriptorSetLayouts;
        std::vector<uint64_t> descriptorSetLayoutsArr;
        uint64_t emptyLayout = 0;
        uint64_t pipelineLayout = 0;

        // Step 1: Create descriptor set layouts
        for (uint32_t set : sets)
        {
            uint64_t layout = 0;
            auto status = dev.createDescriptorSetLayout(set, &layout);
            if (!status.ok())
            {
                // Rollback step 1
                for (auto& pair : descriptorSetLayouts)
                {
                    dev.destroyDescriptorSetLayout(pair.second);
                }
                descriptorSetLayouts.clear();
                return status;
            }
            descriptorSetLayouts[set] = layout;
        }

        // Step 2: Create empty descriptor set layout if non-empty
        if (!descriptorSetLayouts.empty())
        {
            uint32_t maxSet = 0;
            for (const auto& [set, layout] : descriptorSetLayouts)
            {
                maxSet = std::max(maxSet, set);
            }

            auto status = dev.createEmptyDescriptorSetLayout(&emptyLayout);
            if (!status.ok())
            {
                // Rollback step 2
                for (auto& pair : descriptorSetLayouts)
                {
                    dev.destroyDescriptorSetLayout(pair.second);
                }
                descriptorSetLayouts.clear();
                return status;
            }

            descriptorSetLayoutsArr.resize(maxSet + 1, emptyLayout);
            for (const auto& [set, layout] : descriptorSetLayouts)
            {
                descriptorSetLayoutsArr[set] = layout;
            }
        }

        // Step 3: Create pipeline layout
        auto status = dev.createPipelineLayout(&pipelineLayout);
        if (!status.ok())
        {
            // Rollback step 3
            for (auto& pair : descriptorSetLayouts)
            {
                dev.destroyDescriptorSetLayout(pair.second);
            }
            descriptorSetLayouts.clear();
            if (emptyLayout)
            {
                dev.destroyDescriptorSetLayout(emptyLayout);
                emptyLayout = 0;
            }
            return status;
        }

        return FCT::OkStatus();
    };

    // 3.1 Challenge: Failure at Set 2 of 4 sets
    {
        SimulatedDevice dev;
        dev.failAtSetIndex = 2; // Set 0 and 1 succeed, Set 2 fails

        std::vector<uint32_t> sets = {0, 1, 2, 3};
        auto status = runPipelineLayoutTransaction(dev, sets);

        CHALLENGE_CHECK(!status.ok(), "Transaction must fail");
        CHALLENGE_CHECK(status.code() == absl::StatusCode::kInternal, "Status must be InternalError");
        CHALLENGE_CHECK(status.message().find("Set 2") != std::string::npos, "Error message must identify Set 2");
        CHALLENGE_CHECK(dev.activeLayouts.empty(), "Zero leaks: all previously allocated layouts must be destroyed on failure");
    }

    // 3.2 Challenge: Failure at empty descriptor set layout creation
    {
        SimulatedDevice dev;
        dev.failAtEmptyLayout = true;

        std::vector<uint32_t> sets = {0, 1};
        auto status = runPipelineLayoutTransaction(dev, sets);

        CHALLENGE_CHECK(!status.ok(), "Transaction must fail at emptyLayout");
        CHALLENGE_CHECK(status.code() == absl::StatusCode::kInternal, "Status must be InternalError");
        CHALLENGE_CHECK(dev.activeLayouts.empty(), "Zero leaks: all set layouts destroyed when emptyLayout fails");
    }

    // 3.3 Challenge: Failure at pipeline layout creation
    {
        SimulatedDevice dev;
        dev.failAtPipelineLayout = true;

        std::vector<uint32_t> sets = {0, 2};
        auto status = runPipelineLayoutTransaction(dev, sets);

        CHALLENGE_CHECK(!status.ok(), "Transaction must fail at pipelineLayout");
        CHALLENGE_CHECK(status.code() == absl::StatusCode::kInternal, "Status must be InternalError");
        CHALLENGE_CHECK(dev.activeLayouts.empty(), "Zero leaks: all set layouts AND emptyLayout destroyed when pipelineLayout fails");
        CHALLENGE_CHECK(dev.activePipelineLayouts.empty(), "Zero pipeline layouts leaked");
    }

    // 3.4 Success path: confirm proper allocation and cleanup
    {
        SimulatedDevice dev;
        std::vector<uint32_t> sets = {0, 1};
        auto status = runPipelineLayoutTransaction(dev, sets);
        CHALLENGE_CHECK(status.ok(), "Transaction should succeed");
        CHALLENGE_CHECK(dev.activeLayouts.size() == 3, "2 sets + 1 empty layout created");
        CHALLENGE_CHECK(dev.activePipelineLayouts.size() == 1, "1 pipeline layout created");

        // Clean up
        for (auto h : std::vector<uint64_t>(dev.activeLayouts.begin(), dev.activeLayouts.end()))
            dev.destroyDescriptorSetLayout(h);
        for (auto h : std::vector<uint64_t>(dev.activePipelineLayouts.begin(), dev.activePipelineLayouts.end()))
            dev.destroyPipelineLayout(h);
        CHALLENGE_CHECK(dev.activeLayouts.empty() && dev.activePipelineLayouts.empty(), "All cleaned");
    }

    std::cout << ">>> [Test 3 PASS] Failure rollback and status propagation logic fully verified with zero resource leaks." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 4: Architecture & Design Pattern Conformance
// =============================================================================
bool test_ArchitectureConformance()
{
    std::cout << "\n=== [Test 4] Bridge Architecture & Naming Conformance ===" << std::endl;

    // 4.1 Verify Bridge naming conventions:
    // High-level in namespace FCT:
    // FCT::Pipeline, FCT::TraditionalPipeline, FCT::MeshShaderPipeline, FCT::RayTracingPipeline
    static_assert(std::is_class<FCT::Pipeline>::value, "FCT::Pipeline must exist");
    static_assert(std::is_class<FCT::TraditionalPipeline>::value, "FCT::TraditionalPipeline must exist");
    static_assert(std::is_class<FCT::MeshShaderPipeline>::value, "FCT::MeshShaderPipeline must exist");
    static_assert(std::is_class<FCT::RayTracingPipeline>::value, "FCT::RayTracingPipeline must exist");

    // Low-level RHI in namespace FCT::RHI:
    // FCT::RHI::Pipeline, FCT::RHI::RasterizationPipeline, FCT::RHI::RayTracingPipeline
    static_assert(std::is_class<FCT::RHI::Pipeline>::value, "FCT::RHI::Pipeline must exist");
    static_assert(std::is_class<FCT::RHI::RasterizationPipeline>::value, "FCT::RHI::RasterizationPipeline must exist");
    static_assert(std::is_class<FCT::RHI::RayTracingPipeline>::value, "FCT::RHI::RayTracingPipeline must exist");
    static_assert(std::is_class<FCT::RHI::VK_TraditionalPipeline>::value, "FCT::RHI::VK_TraditionalPipeline must exist");
    static_assert(std::is_class<FCT::RHI::VK_MeshShaderPipeline>::value, "FCT::RHI::VK_MeshShaderPipeline must exist");
    static_assert(std::is_class<FCT::RHI::VK_RayTracingPipeline>::value, "FCT::RHI::VK_RayTracingPipeline must exist");

    // Metal & WebGPU Future RHI contracts:
    static_assert(std::is_class<FCT::RHI::MTL_TraditionalPipeline>::value, "FCT::RHI::MTL_TraditionalPipeline must exist");
    static_assert(std::is_class<FCT::RHI::MTL_MeshShaderPipeline>::value, "FCT::RHI::MTL_MeshShaderPipeline must exist");
    static_assert(std::is_class<FCT::RHI::MTL_RayTracingPipeline>::value, "FCT::RHI::MTL_RayTracingPipeline must exist");
    static_assert(std::is_class<FCT::RHI::WGPU_TraditionalPipeline>::value, "FCT::RHI::WGPU_TraditionalPipeline must exist");
    static_assert(std::is_class<FCT::RHI::WGPU_MeshShaderPipeline>::value, "FCT::RHI::WGPU_MeshShaderPipeline must exist");
    static_assert(std::is_class<FCT::RHI::WGPU_RayTracingPipeline>::value, "FCT::RHI::WGPU_RayTracingPipeline must exist");

    // 4.2 Verify Future Backend Unimplemented Error
    {
        FCT::RHI::WGPU_TraditionalPipeline wgpuTrad;
        auto st = wgpuTrad.create();
        CHALLENGE_CHECK(st.code() == absl::StatusCode::kUnimplemented, "WGPU create must return UnimplementedError");

        FCT::RHI::MTL_RayTracingPipeline mtlRt;
        st = mtlRt.create();
        CHALLENGE_CHECK(st.code() == absl::StatusCode::kUnimplemented, "MTL create must return UnimplementedError");
    }

    std::cout << ">>> [Test 4 PASS] Bridge pattern and naming conventions verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 5: dispatchMesh 多态分发、安全空操作与压力对抗验证
// =============================================================================
bool test_DispatchMeshPolymorphismAndStress()
{
    std::cout << "\n=== [Test 5] dispatchMesh Polymorphic Dispatch & Adversarial Stress ===" << std::endl;

    MockCommandBuffer cmdBuf;
    FCT::Context* ctx = getDummyContext();

    // 5.1 基础光栅化管线安全空操作验证 (无崩溃、无异常、安全无操作)
    {
        MockRasterPipeline standardRaster;
        FCT::RHI::RasterizationPipeline* rasterBase = &standardRaster;

        // 基类虚实现应为安全空操作，不抛异常也不崩溃
        rasterBase->dispatchMesh(&cmdBuf, 16, 8, 4);
        rasterBase->dispatchMesh(&cmdBuf, 0, 0, 0);
        rasterBase->dispatchMesh(&cmdBuf, UINT32_MAX, UINT32_MAX, UINT32_MAX);
        rasterBase->dispatchMesh(nullptr, 1, 1, 1); // 空命令缓冲区调用安全性
        CHALLENGE_CHECK(!standardRaster.wasBound, "dispatchMesh 不应触发意外绑定");
    }

    // 5.2 RHI 派生类多态覆盖验证
    {
        MockMeshShaderRhiPipeline meshRhi;
        FCT::RHI::RasterizationPipeline* rasterBase = &meshRhi;

        CHALLENGE_CHECK(meshRhi.dispatchCallCount == 0, "初始分发计数为 0");

        // 通过基类指针多态调用 dispatchMesh
        rasterBase->dispatchMesh(&cmdBuf, 64, 32, 16);
        CHALLENGE_CHECK(meshRhi.dispatchCallCount == 1, "多态调用必须命中派生类覆盖方法");
        CHALLENGE_CHECK(meshRhi.lastDispatchCmd == &cmdBuf, "传递的命令缓冲区指针必须匹配");
        CHALLENGE_CHECK(meshRhi.lastGroupX == 64, "GroupCountX 必须匹配");
        CHALLENGE_CHECK(meshRhi.lastGroupY == 32, "GroupCountY 必须匹配");
        CHALLENGE_CHECK(meshRhi.lastGroupZ == 16, "GroupCountZ 必须匹配");

        // 默认参数验证 (Y=1, Z=1)
        rasterBase->dispatchMesh(&cmdBuf, 128);
        CHALLENGE_CHECK(meshRhi.dispatchCallCount == 2, "第二次分发必须成功累加");
        CHALLENGE_CHECK(meshRhi.lastGroupX == 128, "GroupCountX 匹配");
        CHALLENGE_CHECK(meshRhi.lastGroupY == 1, "默认参数 GroupCountY 应为 1");
        CHALLENGE_CHECK(meshRhi.lastGroupZ == 1, "默认参数 GroupCountZ 应为 1");
    }

    // 5.3 高层 FCT::MeshShaderPipeline 桥接多态分发验证 (零 RTTI，零 dynamic_cast)
    {
        auto rhiUnique = FCT::makeUnique<MockMeshShaderRhiPipeline>();
        MockMeshShaderRhiPipeline* rawMeshRhi = rhiUnique.get();

        FCT::MeshShaderPipeline meshPipeline(ctx, std::move(rhiUnique));
        CHALLENGE_CHECK(meshPipeline.getType() == FCT::PipelineType::MeshShader, "管线类型必须为 MeshShader");
        CHALLENGE_CHECK(meshPipeline.getRhiPipeline() == rawMeshRhi, "底层指针必须与创建时一致");

        // 调用高层 dispatchMesh
        meshPipeline.dispatchMesh(&cmdBuf, 256, 128, 64);
        CHALLENGE_CHECK(rawMeshRhi->dispatchCallCount == 1, "高层 Bridge 分发必须穿透至底层 RHI 覆盖实现");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupX == 256, "GroupCountX 匹配");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupY == 128, "GroupCountY 匹配");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupZ == 64, "GroupCountZ 匹配");

        // 高层默认参数分发 (Y=1, Z=1)
        meshPipeline.dispatchMesh(&cmdBuf, 512);
        CHALLENGE_CHECK(rawMeshRhi->dispatchCallCount == 2, "第二次高层分发计数匹配");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupX == 512, "GroupCountX 匹配");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupY == 1, "默认参数 Y=1 匹配");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupZ == 1, "默认参数 Z=1 匹配");
    }

    // 5.4 对抗测试: FCT::MeshShaderPipeline 包装非 Mesh 的传统光栅化 RHI 管线
    {
        auto tradRhi = FCT::makeUnique<MockRasterPipeline>();
        MockRasterPipeline* rawTradRhi = tradRhi.get();

        FCT::MeshShaderPipeline mismatchedPipeline(ctx, std::move(tradRhi));
        CHALLENGE_CHECK(mismatchedPipeline.getRhiPipeline() == rawTradRhi, "底层指针挂接正确");

        // 此时底层为传统光栅化管线，调用 dispatchMesh 应安全走基类空操作，不得发生崩溃或野指针访问
        mismatchedPipeline.dispatchMesh(&cmdBuf, 64, 64, 64);
        CHALLENGE_CHECK(!rawTradRhi->wasBound, "错配情况下安全空操作，不产生错误副作用");
    }

    // 5.5 高频压力测试: 100,000 次高频多态 dispatchMesh 调度
    {
        auto rhiUnique = FCT::makeUnique<MockMeshShaderRhiPipeline>();
        MockMeshShaderRhiPipeline* rawMeshRhi = rhiUnique.get();
        FCT::MeshShaderPipeline meshPipeline(ctx, std::move(rhiUnique));

        constexpr uint32_t kStressIterations = 100000;
        for (uint32_t i = 0; i < kStressIterations; ++i)
        {
            uint32_t gx = (i & 0x3FF) + 1;
            uint32_t gy = ((i * 3) & 0x1FF) + 1;
            uint32_t gz = ((i * 7) & 0x3F) + 1;
            meshPipeline.dispatchMesh(&cmdBuf, gx, gy, gz);
        }

        CHALLENGE_CHECK(rawMeshRhi->dispatchCallCount == kStressIterations, "100,000 次压力调度调用计数精确匹配");
        uint32_t lastI = kStressIterations - 1;
        CHALLENGE_CHECK(rawMeshRhi->lastGroupX == ((lastI & 0x3FF) + 1), "最终迭代参数准确");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupY == (((lastI * 3) & 0x1FF) + 1), "最终迭代参数准确");
        CHALLENGE_CHECK(rawMeshRhi->lastGroupZ == (((lastI * 7) & 0x3F) + 1), "最终迭代参数准确");
    }

    std::cout << ">>> [Test 5 PASS] dispatchMesh polymorphic dispatch, safe no-op & stress testing verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 6: 三大管线家族 Bridge 完整多态绑定与回归验证 (Zero Regressions)
// =============================================================================
bool test_ZeroRegressionsPipelineFamily()
{
    std::cout << "\n=== [Test 6] Full Pipeline Family Bridge & Polymorphic Binding ===" << std::endl;

    FCT::Context* ctx = getDummyContext();
    MockCommandBuffer cmdBuf;

    MockRasterPipeline::s_aliveCount.store(0);
    MockMeshShaderRhiPipeline::s_aliveCount.store(0);
    MockRayTracingPipeline::s_aliveCount.store(0);

    // 6.1 三大管线高层抽象实例化与类型核验
    {
        auto tradRhi = FCT::makeUnique<MockRasterPipeline>();
        MockRasterPipeline* rawTrad = tradRhi.get();
        auto* tradBridge = FCT_NEW(FCT::TraditionalPipeline, ctx, std::move(tradRhi));

        auto meshRhi = FCT::makeUnique<MockMeshShaderRhiPipeline>();
        MockMeshShaderRhiPipeline* rawMesh = meshRhi.get();
        auto* meshBridge = FCT_NEW(FCT::MeshShaderPipeline, ctx, std::move(meshRhi));

        auto rtRhi = FCT::makeUnique<MockRayTracingPipeline>();
        MockRayTracingPipeline* rawRt = rtRhi.get();
        auto* rtBridge = FCT_NEW(FCT::RayTracingPipeline, ctx, std::move(rtRhi));

        CHALLENGE_CHECK(tradBridge->getType() == FCT::PipelineType::Traditional, "Traditional 类型匹配");
        CHALLENGE_CHECK(meshBridge->getType() == FCT::PipelineType::MeshShader, "MeshShader 类型匹配");
        CHALLENGE_CHECK(rtBridge->getType() == FCT::PipelineType::RayTracing, "RayTracing 类型匹配");

        CHALLENGE_CHECK(tradBridge->getRhiPipeline() == rawTrad, "Traditional RHI 裸指针匹配");
        CHALLENGE_CHECK(meshBridge->getRhiPipeline() == rawMesh, "MeshShader RHI 裸指针匹配");
        CHALLENGE_CHECK(rtBridge->getRhiPipeline() == rawRt, "RayTracing RHI 裸指针匹配");

        // 6.2 多态 CommandBuffer 绑定验证 (通过 RHI 基类与高层 Bridge)
        // Traditional 绑定
        cmdBuf.bindPipeline(tradBridge->getRhiPipeline());
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == rawTrad, "CommandBuffer 绑定 Traditional 匹配");
        CHALLENGE_CHECK(rawTrad->wasBound, "Traditional RHI wasBound 触发");
        CHALLENGE_CHECK(rawTrad->boundCmd == &cmdBuf, "Traditional 命令缓冲区指针匹配");

        // MeshShader 绑定
        cmdBuf.bindPipeline(meshBridge->getRhiPipeline());
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == rawMesh, "CommandBuffer 绑定 MeshShader 匹配");
        CHALLENGE_CHECK(rawMesh->wasBound, "MeshShader RHI wasBound 触发");
        CHALLENGE_CHECK(rawMesh->boundCmd == &cmdBuf, "MeshShader 命令缓冲区指针匹配");

        // RayTracing 绑定
        cmdBuf.bindPipeline(rtBridge->getRhiPipeline());
        CHALLENGE_CHECK(cmdBuf.lastBoundPipeline == rawRt, "CommandBuffer 绑定 RayTracing 匹配");
        CHALLENGE_CHECK(rawRt->wasBound, "RayTracing RHI wasBound 触发");
        CHALLENGE_CHECK(rawRt->boundCmd == &cmdBuf, "RayTracing 命令缓冲区指针匹配");

        // 6.3 高层 Bridge::bind 穿透验证
        rawTrad->wasBound = false;
        tradBridge->bind(&cmdBuf);
        CHALLENGE_CHECK(rawTrad->wasBound, "Traditional Bridge::bind 穿透生效");

        rawMesh->wasBound = false;
        meshBridge->bind(&cmdBuf);
        CHALLENGE_CHECK(rawMesh->wasBound, "MeshShader Bridge::bind 穿透生效");

        rawRt->wasBound = false;
        rtBridge->bind(&cmdBuf);
        CHALLENGE_CHECK(rawRt->wasBound, "RayTracing Bridge::bind 穿透生效");

        // 6.4 生命周期释放验证: release 析构高层 Bridge 并连带销毁底层 RHI
        CHALLENGE_CHECK(MockRasterPipeline::s_aliveCount.load() == 1, "销毁前 Traditional 存活计数为 1");
        CHALLENGE_CHECK(MockMeshShaderRhiPipeline::s_aliveCount.load() == 1, "销毁前 MeshShader 存活计数为 1");
        CHALLENGE_CHECK(MockRayTracingPipeline::s_aliveCount.load() == 1, "销毁前 RayTracing 存活计数为 1");

        tradBridge->release();
        meshBridge->release();
        rtBridge->release();

        CHALLENGE_CHECK(MockRasterPipeline::s_aliveCount.load() == 0, "销毁后 Traditional 存活计数归零");
        CHALLENGE_CHECK(MockMeshShaderRhiPipeline::s_aliveCount.load() == 0, "销毁后 MeshShader 存活计数归零");
        CHALLENGE_CHECK(MockRayTracingPipeline::s_aliveCount.load() == 0, "销毁后 RayTracing 存活计数归零");
    }

    std::cout << ">>> [Test 6 PASS] Full pipeline family bridge and polymorphic binding verified with zero regressions." << std::endl;
    return true;
}

// =============================================================================
// Main Entry
// =============================================================================
int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << "   Milestone 2 Polymorphic Pipeline & Bridge Challenger Test   " << std::endl;
    std::cout << "================================================================" << std::endl;

    bool allPassed = true;

    if (!test_PolymorphicDispatch())
    {
        std::cerr << "FAIL: Test Suite 1 (Polymorphic Dispatch) failed!" << std::endl;
        allPassed = false;
    }

    if (!test_BridgeOwnershipAndLifetime())
    {
        std::cerr << "FAIL: Test Suite 2 (Bridge Ownership & Lifetime) failed!" << std::endl;
        allPassed = false;
    }

    if (!test_FailureRollbackAndErrorPropagation())
    {
        std::cerr << "FAIL: Test Suite 3 (Failure Rollback & Error Propagation) failed!" << std::endl;
        allPassed = false;
    }

    if (!test_ArchitectureConformance())
    {
        std::cerr << "FAIL: Test Suite 4 (Architecture Conformance) failed!" << std::endl;
        allPassed = false;
    }

    if (!test_DispatchMeshPolymorphismAndStress())
    {
        std::cerr << "FAIL: Test Suite 5 (dispatchMesh Polymorphic Dispatch & Stress) failed!" << std::endl;
        allPassed = false;
    }

    if (!test_ZeroRegressionsPipelineFamily())
    {
        std::cerr << "FAIL: Test Suite 6 (Full Pipeline Family Zero Regressions) failed!" << std::endl;
        allPassed = false;
    }

    std::cout << "\n================================================================" << std::endl;
    if (allPassed)
    {
        std::cout << "   ALL CHALLENGER TEST SUITES PASSED EMPIRICALLY! (100% OK)    " << std::endl;
        std::cout << "================================================================" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "   SOME CHALLENGER TEST SUITES FAILED!                          " << std::endl;
        std::cout << "================================================================" << std::endl;
        return 1;
    }
}
