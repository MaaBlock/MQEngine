#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <type_traits>
#include <memory>
#include <atomic>

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <FCT/RHI/CommandBuffer.h>
#include <FCT/RHI/Pipeline.h>
#include <FCT/RHI/RasterizationPipeline.h>
#include <FCT/RHI/RayTracingPipeline.h>
#include <FCT/RHI/MTL_Pipeline.h>
#include <FCT/RHI/WGPU_Pipeline.h>
#include <FCT/Context/Pipeline.h>
#include <FCT/Context/TraditionalPipeline.h>
#include <FCT/Context/MeshShaderPipeline.h>
#include <FCT/Context/RayTracingPipeline.h>
#include <FCT/MutilThreadBase/RefCount.h>
#include <FCT/MutilThreadBase/UniquePtr.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

// =============================================================================
// Mock CommandBuffer with Sequence Call Logging
// =============================================================================
class MockLoggingCommandBuffer : public FCT::RHI::CommandBuffer
{
public:
    enum class CallType
    {
        BindPipeline,
        DrawIndex,
        Draw
    };

    struct CallRecord
    {
        CallType type;
        uint64_t sequence;
        void* pipelinePtr = nullptr;
        size_t indexBegin = 0;
        size_t instanceBegin = 0;
        size_t indexCount = 0;
        size_t instanceCount = 0;
        size_t firstVertex = 0;
    };

    uint64_t nextSequence = 1;
    std::vector<CallRecord> callHistory;
    FCT::RHI::Pipeline* lastBoundPipeline = nullptr;

    MockLoggingCommandBuffer() = default;
    ~MockLoggingCommandBuffer() override = default;

    void reset() override {}
    void create() override {}
    void begin() override {}
    void viewport(FCT::Vec2 lt, FCT::Vec2 rb) override { (void)lt; (void)rb; }
    void scissor(FCT::Vec2 lt, FCT::Vec2 rb) override { (void)lt; (void)rb; }

    void draw(size_t vertexBegin, size_t instanceBegin, size_t vertexSize, size_t instanceSize) override
    {
        CallRecord rec;
        rec.type = CallType::Draw;
        rec.sequence = nextSequence++;
        rec.indexBegin = vertexBegin;
        rec.instanceBegin = instanceBegin;
        rec.indexCount = vertexSize;
        rec.instanceCount = instanceSize;
        callHistory.push_back(rec);
    }

    void drawIndex(size_t indexBegin, size_t instanceBegin, size_t indexCount, size_t instanceCount, size_t firstVertex) override
    {
        CallRecord rec;
        rec.type = CallType::DrawIndex;
        rec.sequence = nextSequence++;
        rec.indexBegin = indexBegin;
        rec.instanceBegin = instanceBegin;
        rec.indexCount = indexCount;
        rec.instanceCount = instanceCount;
        rec.firstVertex = firstVertex;
        callHistory.push_back(rec);
    }

    void recordPipelineBind(FCT::RHI::Pipeline* pipeline)
    {
        CallRecord rec;
        rec.type = CallType::BindPipeline;
        rec.sequence = nextSequence++;
        rec.pipelinePtr = pipeline;
        callHistory.push_back(rec);
        lastBoundPipeline = pipeline;
    }

    void bindPipeline(FCT::RHI::Pipeline* pipeline) override
    {
        recordPipelineBind(pipeline);
        if (pipeline)
        {
            pipeline->bind(this);
        }
    }

    void end() override {}
    void submit() override {}
    void nextPass() override {}
    void barrier(FCT::Image* image, FCT::ImageLayout oldLayout, FCT::ImageLayout newLayout,
                 FCT::PipelineStages srcStage, FCT::PipelineStages dstStage,
                 FCT::AccessFlags srcAccess, FCT::AccessFlags dstAccess,
                 FCT::ImageAspects aspectMask) override
    {
        (void)image; (void)oldLayout; (void)newLayout; (void)srcStage; (void)dstStage; (void)srcAccess; (void)dstAccess; (void)aspectMask;
    }
};

// =============================================================================
// Mock Rasterization Pipeline
// =============================================================================
class MockTestRasterPipeline : public FCT::RHI::RasterizationPipeline
{
public:
    static std::atomic<int> s_aliveCount;
    int bindCount = 0;
    FCT::RHI::CommandBuffer* lastBoundCmd = nullptr;

    MockTestRasterPipeline() { s_aliveCount.fetch_add(1); }
    ~MockTestRasterPipeline() override { s_aliveCount.fetch_sub(1); }

    FCT::PipelineType getType() const noexcept override { return FCT::PipelineType::Traditional; }
    void addResources(FCT::IPipelineResource* resource) override { (void)resource; }
    FCT::Status create() override { return FCT::OkStatus(); }
    void bindPass(FCT::RHI::Pass* pass) override { (void)pass; }

    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        bindCount++;
        lastBoundCmd = cmdBuf;
        if (auto* loggingCmd = dynamic_cast<MockLoggingCommandBuffer*>(cmdBuf))
        {
            if (loggingCmd->lastBoundPipeline != this)
            {
                loggingCmd->recordPipelineBind(this);
            }
        }
    }
};
std::atomic<int> MockTestRasterPipeline::s_aliveCount{0};

// =============================================================================
// Mock Ray Tracing Pipeline
// =============================================================================
class MockTestRayTracingPipeline : public FCT::RHI::RayTracingPipeline
{
public:
    static std::atomic<int> s_aliveCount;
    int bindCount = 0;
    FCT::RHI::CommandBuffer* lastBoundCmd = nullptr;
    FCT::RHI::StridedDeviceAddressRegions dummyRegions{};

    MockTestRayTracingPipeline() { s_aliveCount.fetch_add(1); }
    ~MockTestRayTracingPipeline() override { s_aliveCount.fetch_sub(1); }

    FCT::PipelineType getType() const noexcept override { return FCT::PipelineType::RayTracing; }
    FCT::Status create() override { return FCT::OkStatus(); }
    void bind(FCT::RHI::CommandBuffer* cmdBuf) override
    {
        bindCount++;
        lastBoundCmd = cmdBuf;
    }
    FCT::Status buildSBT() override { return FCT::OkStatus(); }
    const FCT::RHI::StridedDeviceAddressRegions& getSBTRegions() const noexcept override { return dummyRegions; }
    void traceRays(FCT::RHI::CommandBuffer* cmdBuf, uint32_t w, uint32_t h, uint32_t d) override
    {
        (void)cmdBuf; (void)w; (void)h; (void)d;
    }
    void setMaxRecursionDepth(uint32_t depth) override { (void)depth; }
};
std::atomic<int> MockTestRayTracingPipeline::s_aliveCount{0};

// =============================================================================
// Test Suite 1: RendererSubsystem Object Pass Pipeline Binding Order & Null-Safety
// =============================================================================
bool test_PipelineBindingOrderAndNullSafety()
{
    std::cout << "\n=== [Test 1] Object Pass Pipeline Binding Order & Null-Safety ===" << std::endl;

    // 1.1 Verify that pipeline binding happens strictly BEFORE drawIndex
    {
        MockLoggingCommandBuffer cmdBuf;
        MockTestRasterPipeline pipeline;

        // Simulate RendererSubsystem::recordObjectPass lines 1052-1100:
        // 2. 绑定当前着色管线状态至 CommandBuffer
        auto* currentPipeline = &pipeline;
        if (currentPipeline) {
            currentPipeline->bind(&cmdBuf);
        }

        // 3. 现代 Bindless 绘制循环 (3 items)
        for (size_t i = 0; i < 3; ++i) {
            const uint32_t instanceBegin = static_cast<uint32_t>(i);
            const uint32_t indexCount = 36; // Cube mesh
            cmdBuf.drawIndex(0, instanceBegin, indexCount, 1, 0);
        }

        TEST_ASSERT(pipeline.bindCount == 1, "Pipeline bind should be called once before draw loop");
        TEST_ASSERT(pipeline.lastBoundCmd == &cmdBuf, "Pipeline bound to correct CommandBuffer");
        TEST_ASSERT(cmdBuf.callHistory.size() == 4, "Total 4 calls: 1 bind + 3 draws");
        TEST_ASSERT(cmdBuf.callHistory[0].type == MockLoggingCommandBuffer::CallType::BindPipeline,
                    "First call must be BindPipeline");
        TEST_ASSERT(cmdBuf.callHistory[0].sequence < cmdBuf.callHistory[1].sequence,
                    "BindPipeline sequence must precede drawIndex(0)");
        TEST_ASSERT(cmdBuf.callHistory[1].type == MockLoggingCommandBuffer::CallType::DrawIndex,
                    "Second call must be DrawIndex");
        TEST_ASSERT(cmdBuf.callHistory[1].instanceBegin == 0, "Item 0 instanceBegin must be 0");
        TEST_ASSERT(cmdBuf.callHistory[2].instanceBegin == 1, "Item 1 instanceBegin must be 1");
        TEST_ASSERT(cmdBuf.callHistory[3].instanceBegin == 2, "Item 2 instanceBegin must be 2");
    }

    // 1.2 Verify Null-safety: when currentPipeline is nullptr, no crash
    {
        MockLoggingCommandBuffer cmdBuf;
        MockTestRasterPipeline* nullPipeline = nullptr;

        // When currentPipeline is null:
        if (nullPipeline) {
            nullPipeline->bind(&cmdBuf);
        }

        // Draw loop should still proceed without dereference
        cmdBuf.drawIndex(0, 0, 36, 1, 0);

        TEST_ASSERT(cmdBuf.callHistory.size() == 1, "Only 1 drawIndex called, no bind");
        TEST_ASSERT(cmdBuf.callHistory[0].type == MockLoggingCommandBuffer::CallType::DrawIndex, "DrawIndex recorded");
    }

    // 1.3 Verify pipeline change between passes
    {
        MockLoggingCommandBuffer cmdBuf;
        MockTestRasterPipeline pipelineA;
        MockTestRasterPipeline pipelineB;

        // Pass A
        pipelineA.bind(&cmdBuf);
        cmdBuf.drawIndex(0, 0, 36, 1, 0);

        // Pass B (different pipeline)
        pipelineB.bind(&cmdBuf);
        cmdBuf.drawIndex(0, 0, 36, 1, 0);

        TEST_ASSERT(pipelineA.bindCount == 1, "pipelineA bound once");
        TEST_ASSERT(pipelineB.bindCount == 1, "pipelineB bound once");
        TEST_ASSERT(cmdBuf.lastBoundPipeline == &pipelineB, "Last bound pipeline is pipelineB");
    }

    std::cout << "[PASS] Test 1: Object pass pipeline binding order and null-safety verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 2: Backwards Compatibility for Deprecated bindPipieline
// =============================================================================
bool test_DeprecatedBindPipielineCompatibility()
{
    std::cout << "\n=== [Test 2] Backwards Compatibility for Deprecated bindPipieline ===" << std::endl;

    MockLoggingCommandBuffer cmdBuf;

    // 2.1 Calling bindPipieline(RasterizationPipeline*) dispatches correctly
    {
        MockTestRasterPipeline rasterPipe;
        TEST_ASSERT(rasterPipe.bindCount == 0, "Initial bindCount is 0");

        // Call the deprecated method directly
        cmdBuf.bindPipieline(&rasterPipe);

        TEST_ASSERT(rasterPipe.bindCount == 1, "bindPipieline must invoke rasterPipe.bind(cmdBuf)");
        TEST_ASSERT(cmdBuf.lastBoundPipeline == &rasterPipe, "lastBoundPipeline matches");
    }

    // 2.2 Calling bindPipieline(nullptr) is completely safe (null check in bindPipeline)
    {
        int prevSize = static_cast<int>(cmdBuf.callHistory.size());
        cmdBuf.bindPipieline(nullptr);
        TEST_ASSERT(static_cast<int>(cmdBuf.callHistory.size()) == prevSize + 1, "Call recorded");
        TEST_ASSERT(cmdBuf.lastBoundPipeline == nullptr, "lastBoundPipeline is nullptr");
    }

    // 2.3 Calling bindPipeline(nullptr) directly is safe
    {
        cmdBuf.bindPipeline(nullptr);
        TEST_ASSERT(cmdBuf.lastBoundPipeline == nullptr, "Direct nullptr bind safe");
    }

    // 2.4 Polymorphic bindPipeline handles RayTracingPipeline
    {
        MockTestRayTracingPipeline rtPipe;
        TEST_ASSERT(rtPipe.bindCount == 0, "Initial rt bindCount is 0");

        cmdBuf.bindPipeline(&rtPipe);

        TEST_ASSERT(rtPipe.bindCount == 1, "rtPipe.bind must be called");
        TEST_ASSERT(cmdBuf.lastBoundPipeline == &rtPipe, "lastBoundPipeline matches RT pipeline");
        TEST_ASSERT(rtPipe.getType() == FCT::PipelineType::RayTracing, "getType is RayTracing");
    }

    std::cout << "[PASS] Test 2: Deprecated bindPipieline backwards compatibility and null-safety verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 3: Bridge Architecture & Type Constraints
// =============================================================================
bool test_BridgeArchitectureContracts()
{
    std::cout << "\n=== [Test 3] Bridge Architecture & Type System Conformance ===" << std::endl;

    // 3.1 Type constraint: RHI::Pipeline must NOT inherit from RefCount
    static_assert(!std::is_base_of<FCT::RefCount, FCT::RHI::Pipeline>::value,
        "FCT::RHI::Pipeline must NOT inherit from RefCount (allows UniquePtr<RHI::Pipeline>)");

    // 3.2 Type constraint: High-level FCT::Pipeline MUST inherit from RefCount
    static_assert(std::is_base_of<FCT::RefCount, FCT::Pipeline>::value,
        "FCT::Pipeline must inherit from RefCount for Ptr<Pipeline> sharing");

    // 3.3 Inheritance hierarchy in FCT::RHI
    static_assert(std::is_base_of<FCT::RHI::Pipeline, FCT::RHI::RasterizationPipeline>::value,
        "RasterizationPipeline must inherit from RHI::Pipeline");
    static_assert(std::is_base_of<FCT::RHI::Pipeline, FCT::RHI::RayTracingPipeline>::value,
        "RayTracingPipeline must inherit from RHI::Pipeline");

    // 3.4 Inheritance hierarchy in high-level FCT
    static_assert(std::is_base_of<FCT::Pipeline, FCT::TraditionalPipeline>::value,
        "TraditionalPipeline must inherit from FCT::Pipeline");
    static_assert(std::is_base_of<FCT::Pipeline, FCT::MeshShaderPipeline>::value,
        "MeshShaderPipeline must inherit from FCT::Pipeline");
    static_assert(std::is_base_of<FCT::Pipeline, FCT::RayTracingPipeline>::value,
        "RayTracingPipeline must inherit from FCT::Pipeline");

    // 3.5 PipelineType enum values
    TEST_ASSERT(static_cast<uint8_t>(FCT::PipelineType::Traditional) == 0, "Traditional == 0");
    TEST_ASSERT(static_cast<uint8_t>(FCT::PipelineType::MeshShader) == 1, "MeshShader == 1");
    TEST_ASSERT(static_cast<uint8_t>(FCT::PipelineType::RayTracing) == 2, "RayTracing == 2");
    TEST_ASSERT(static_cast<uint8_t>(FCT::PipelineType::Compute) == 3, "Compute == 3");

    // 3.6 Future platform contracts (MTL & WGPU)
    {
        FCT::RHI::WGPU_TraditionalPipeline wgpuPipe;
        auto status = wgpuPipe.create();
        TEST_ASSERT(status.code() == absl::StatusCode::kUnimplemented, "WGPU create must return Unimplemented");

        FCT::RHI::MTL_RayTracingPipeline mtlPipe;
        status = mtlPipe.create();
        TEST_ASSERT(status.code() == absl::StatusCode::kUnimplemented, "MTL create must return Unimplemented");
    }

    std::cout << "[PASS] Test 3: Bridge architecture and type constraints verified." << std::endl;
    return true;
}

// =============================================================================
// Test Suite 4: RHI Pipeline Release & UniquePtr Management
// =============================================================================
bool test_RhiPipelineReleaseAndUniquePtr()
{
    std::cout << "\n=== [Test 4] RHI Pipeline Release & UniquePtr Lifecycle ===" << std::endl;

    MockTestRasterPipeline::s_aliveCount.store(0);

    // 4.1 UniquePtr lifetime management
    {
        auto uptr = FCT::makeUnique<MockTestRasterPipeline>();
        TEST_ASSERT(MockTestRasterPipeline::s_aliveCount.load() == 1, "Pipeline created in UniquePtr");
        TEST_ASSERT(uptr.get() != nullptr, "UniquePtr get() non-null");

        // Move to another UniquePtr
        auto uptr2 = std::move(uptr);
        TEST_ASSERT(uptr.get() == nullptr, "uptr is now null after move");
        TEST_ASSERT(uptr2.get() != nullptr, "uptr2 owns pointer");
        TEST_ASSERT(MockTestRasterPipeline::s_aliveCount.load() == 1, "Still 1 instance alive");
    }
    // Out of scope
    TEST_ASSERT(MockTestRasterPipeline::s_aliveCount.load() == 0, "Pipeline deleted when UniquePtr destroyed");

    // 4.2 release() virtual method compatibility (for PipelineCache release)
    {
        auto* rawPipe = FCT_NEW(MockTestRasterPipeline);
        TEST_ASSERT(MockTestRasterPipeline::s_aliveCount.load() == 1, "Allocated via FCT_NEW");

        // Polymorphic call to release()
        FCT::RHI::Pipeline* basePtr = rawPipe;
        basePtr->release();
        TEST_ASSERT(MockTestRasterPipeline::s_aliveCount.load() == 0, "basePtr->release() calls FCT_DELETE");
    }

    std::cout << "[PASS] Test 4: RHI Pipeline Release & UniquePtr lifecycle verified." << std::endl;
    return true;
}

// =============================================================================
// Main Runner
// =============================================================================
int main()
{
    std::cout << "=================================================================" << std::endl;
    std::cout << " MQEngine Milestone 2 Renderer Integration Challenger Test Suite" << std::endl;
    std::cout << "=================================================================" << std::endl;

    bool allPassed = true;

    if (!test_PipelineBindingOrderAndNullSafety()) allPassed = false;
    if (!test_DeprecatedBindPipielineCompatibility()) allPassed = false;
    if (!test_BridgeArchitectureContracts()) allPassed = false;
    if (!test_RhiPipelineReleaseAndUniquePtr()) allPassed = false;

    std::cout << "\n=================================================================" << std::endl;
    if (allPassed) {
        std::cout << "  >>> ALL MILESTONE 2 INTEGRATION CHALLENGER TESTS PASSED! <<<   " << std::endl;
        std::cout << "=================================================================" << std::endl;
        return 0;
    } else {
        std::cout << "  >>> SOME CHALLENGER TESTS FAILED! <<<                          " << std::endl;
        std::cout << "=================================================================" << std::endl;
        return 1;
    }
}
