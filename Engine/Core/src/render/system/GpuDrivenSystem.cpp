#include "GpuDrivenSystem.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "CameraSystem.h"
#include "MatrixCacheSystem.h"
#include "../../scene/core/Scene.h"

namespace MQEngine {

namespace {

// 基于标准 Vulkan 1.0 GLSL 450 的无 Bindless 视锥剔除与 Indirect 命令生成 Compute Shader
constexpr const char* kVulkan10CullingComputeShaderSource = R"(#version 450

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct ObjectData {
    vec4 sphere;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    uint flags;
};

struct DrawIndexedIndirectCmd {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

layout(std430, set = 0, binding = 0) readonly buffer ObjectDataBuffer {
    ObjectData objects[];
} u_objects;

layout(std430, set = 0, binding = 1) writeonly buffer IndirectCmdBuffer {
    DrawIndexedIndirectCmd commands[];
} u_commands;

layout(push_constant) uniform CullPushConstants {
    vec4 frustumPlanes[6];
    uint objectCount;
    uint enableFrustumCulling;
    uint pad0;
    uint pad1;
} u_pc;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_pc.objectCount) {
        return;
    }

    ObjectData obj = u_objects.objects[idx];
    bool visible = (obj.indexCount > 0u);

    if (visible && u_pc.enableFrustumCulling != 0u && (obj.flags & 1u) != 0u && (obj.flags & 2u) == 0u) {
        vec3 center = obj.sphere.xyz;
        float radius = max(obj.sphere.w, 0.0);
        for (int i = 0; i < 6; ++i) {
            vec4 plane = u_pc.frustumPlanes[i];
            float dist = dot(plane.xyz, center) + plane.w;
            if (dist < -radius) {
                visible = false;
                break;
            }
        }
    }

    DrawIndexedIndirectCmd cmd;
    cmd.indexCount = obj.indexCount;
    cmd.instanceCount = visible ? 1u : 0u;
    cmd.firstIndex = obj.firstIndex;
    cmd.vertexOffset = obj.vertexOffset;
    cmd.firstInstance = 0u;
    u_commands.commands[idx] = cmd;
}
)";

FCT::Vec4 normalizePlaneEquation(const FCT::Vec4& rawPlane) {
    float len = std::sqrt(rawPlane.x * rawPlane.x + rawPlane.y * rawPlane.y + rawPlane.z * rawPlane.z);
    if (len < 1e-7f) {
        return FCT::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
    }
    float invLen = 1.0f / len;
    return FCT::Vec4(rawPlane.x * invLen, rawPlane.y * invLen, rawPlane.z * invLen, rawPlane.w * invLen);
}

float extractMaxMatrixScale(const FCT::Mat4& mat) {
    float sx2 = mat.m[0] * mat.m[0] + mat.m[4] * mat.m[4] + mat.m[8] * mat.m[8];
    float sy2 = mat.m[1] * mat.m[1] + mat.m[5] * mat.m[5] + mat.m[9] * mat.m[9];
    float sz2 = mat.m[2] * mat.m[2] + mat.m[6] * mat.m[6] + mat.m[10] * mat.m[10];
    float maxScale2 = std::max({sx2, sy2, sz2, 1e-6f});
    return std::sqrt(maxScale2);
}

} // namespace

GpuDrivenSystem::GpuDrivenSystem(FCT::Context* ctx, DataManager* dataManager)
    : m_ctx(ctx)
    , m_vkCtx(dynamic_cast<FCT::VK_Context*>(ctx))
    , m_dataManager(dataManager) {
    for (int i = 0; i < 6; ++i) {
        m_activeFrustumPlanes[i] = FCT::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}

GpuDrivenSystem::~GpuDrivenSystem() {
    destroyComputeResources();
}

void GpuDrivenSystem::onActivate() {
    if (!m_pipelineReady) {
        initComputePipeline();
    }
}

void GpuDrivenSystem::onDeactivate() {
    destroyComputeResources();
}

void GpuDrivenSystem::updateLogic() {
    // 场景渲染对象收集与视锥平面提取统一在渲染提交线程的 updateRender()（紧接 Sync 屏障之后、RenderGraphSubmit 之前）执行，
    // 从而彻底消除逻辑线程与渲染提交线程对 m_entityToIndirectSlot / m_stagedObjects 的跨线程数据竞争，
    // 并确保本帧在 Sync 阶段（如 Cesium3DTilesSyncTicker）新建或显隐切换的瓦片实体在同一渲染帧内 100% 同步参与 GPU Culling。
}

bool GpuDrivenSystem::initComputePipeline() {
    if (m_pipelineReady) {
        return true;
    }
    if (!m_vkCtx || !m_ctx) {
        return false;
    }

    vk::Device device = m_vkCtx->getDevice();
    if (!device) {
        return false;
    }

    // 确保当前模块(Engine.dll)内的 Vulkan 动态分发器已初始化
    VULKAN_HPP_DEFAULT_DISPATCHER.init();
    if (m_vkCtx->getVkInstance()) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkCtx->getVkInstance());
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetSpirv(shaderc_spirv_version_1_0);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

    auto compileRes = compiler.CompileGlslToSpv(
        kVulkan10CullingComputeShaderSource,
        std::strlen(kVulkan10CullingComputeShaderSource),
        shaderc_glsl_compute_shader,
        "Vulkan10Culling.comp",
        "main",
        options);
    if (compileRes.GetCompilationStatus() != shaderc_compilation_status_success) {
        spdlog::error("GpuDrivenSystem: Vulkan 1.0 视锥剔除 Compute Shader 编译失败: {}",
                      compileRes.GetErrorMessage());
        return false;
    }

    std::vector<uint32_t> spvWords(compileRes.cbegin(), compileRes.cend());
    if (spvWords.empty()) {
        return false;
    }

    vk::ShaderModuleCreateInfo shaderInfo{};
    shaderInfo.setCodeSize(spvWords.size() * sizeof(uint32_t));
    shaderInfo.setPCode(spvWords.data());
    if (device.createShaderModule(&shaderInfo, nullptr, &m_computeShaderModule) != vk::Result::eSuccess) {
        return false;
    }

    // 标准 Vulkan 1.0 Storage Buffer 绑定（0: 物体包围球缓冲，1: 间接绘制命令缓冲）
    vk::DescriptorSetLayoutBinding bindings[2]{};
    bindings[0].setBinding(0);
    bindings[0].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    bindings[0].setDescriptorCount(1);
    bindings[0].setStageFlags(vk::ShaderStageFlagBits::eCompute);

    bindings[1].setBinding(1);
    bindings[1].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    bindings[1].setDescriptorCount(1);
    bindings[1].setStageFlags(vk::ShaderStageFlagBits::eCompute);

    vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.setBindingCount(2);
    setLayoutInfo.setPBindings(bindings);
    if (device.createDescriptorSetLayout(&setLayoutInfo, nullptr, &m_descriptorSetLayout) != vk::Result::eSuccess) {
        destroyComputeResources();
        return false;
    }

    vk::PushConstantRange pcRange{};
    pcRange.setStageFlags(vk::ShaderStageFlagBits::eCompute);
    pcRange.setOffset(0);
    pcRange.setSize(sizeof(GpuDrivenCullPushConstants));

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setSetLayoutCount(1);
    pipelineLayoutInfo.setPSetLayouts(&m_descriptorSetLayout);
    pipelineLayoutInfo.setPushConstantRangeCount(1);
    pipelineLayoutInfo.setPPushConstantRanges(&pcRange);
    if (device.createPipelineLayout(&pipelineLayoutInfo, nullptr, &m_pipelineLayout) != vk::Result::eSuccess) {
        destroyComputeResources();
        return false;
    }

    vk::PipelineShaderStageCreateInfo stageInfo{};
    stageInfo.setStage(vk::ShaderStageFlagBits::eCompute);
    stageInfo.setModule(m_computeShaderModule);
    stageInfo.setPName("main");

    vk::ComputePipelineCreateInfo computePipelineInfo{};
    computePipelineInfo.setStage(stageInfo);
    computePipelineInfo.setLayout(m_pipelineLayout);
    auto pipeRes = device.createComputePipeline(nullptr, computePipelineInfo);
    if (pipeRes.result != vk::Result::eSuccess) {
        destroyComputeResources();
        return false;
    }
    m_computePipeline = pipeRes.value;

    vk::DescriptorPoolSize poolSize{};
    poolSize.setType(vk::DescriptorType::eStorageBuffer);
    poolSize.setDescriptorCount(static_cast<uint32_t>(kMaxFrameSlots * 2));

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.setMaxSets(static_cast<uint32_t>(kMaxFrameSlots));
    poolInfo.setPoolSizeCount(1);
    poolInfo.setPPoolSizes(&poolSize);
    if (device.createDescriptorPool(&poolInfo, nullptr, &m_descriptorPool) != vk::Result::eSuccess) {
        destroyComputeResources();
        return false;
    }

    std::vector<vk::DescriptorSetLayout> layouts(kMaxFrameSlots, m_descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.setDescriptorPool(m_descriptorPool);
    allocInfo.setDescriptorSetCount(static_cast<uint32_t>(kMaxFrameSlots));
    allocInfo.setPSetLayouts(layouts.data());

    std::vector<vk::DescriptorSet> allocatedSets(kMaxFrameSlots);
    if (device.allocateDescriptorSets(&allocInfo, allocatedSets.data()) != vk::Result::eSuccess) {
        destroyComputeResources();
        return false;
    }

    for (size_t i = 0; i < kMaxFrameSlots; ++i) {
        m_frameSlots[i].descriptorSet = allocatedSets[i];
        if (!ensureFrameSlotCapacity(m_frameSlots[i], kInitialObjectCapacity)) {
            destroyComputeResources();
            return false;
        }
    }

    m_pipelineReady = true;
    spdlog::info("GpuDrivenSystem: Vulkan 1.0 无 Bindless Compute Culling + Indirect Draw 管线初始化完成");
    return true;
}

void GpuDrivenSystem::destroyFrameSlotBuffers(FrameBufferSlot& slot) {
    if (!m_vkCtx) {
        return;
    }
    vk::Device device = m_vkCtx->getDevice();
    if (!device) {
        return;
    }

    if (slot.mappedObjects && slot.objectMemory) {
        device.unmapMemory(slot.objectMemory);
        slot.mappedObjects = nullptr;
    }
    if (slot.objectBuffer) {
        device.destroyBuffer(slot.objectBuffer);
        slot.objectBuffer = nullptr;
    }
    if (slot.objectMemory) {
        device.freeMemory(slot.objectMemory);
        slot.objectMemory = nullptr;
    }

    if (slot.mappedIndirect && slot.indirectMemory) {
        device.unmapMemory(slot.indirectMemory);
        slot.mappedIndirect = nullptr;
    }
    if (slot.indirectBuffer) {
        device.destroyBuffer(slot.indirectBuffer);
        slot.indirectBuffer = nullptr;
    }
    if (slot.indirectMemory) {
        device.freeMemory(slot.indirectMemory);
        slot.indirectMemory = nullptr;
    }

    slot.capacity = 0;
    slot.activeCount = 0;
}

bool GpuDrivenSystem::ensureFrameSlotCapacity(FrameBufferSlot& slot, uint32_t requiredCapacity) {
    if (!m_vkCtx) {
        return false;
    }
    uint32_t targetCapacity = std::max(requiredCapacity, kInitialObjectCapacity);
    if (slot.capacity >= targetCapacity && slot.objectBuffer && slot.indirectBuffer) {
        return true;
    }

    uint32_t newCapacity = slot.capacity > 0 ? slot.capacity : kInitialObjectCapacity;
    while (newCapacity < targetCapacity) {
        newCapacity *= 2;
    }

    vk::Device device = m_vkCtx->getDevice();
    device.waitIdle();
    destroyFrameSlotBuffers(slot);

    auto allocateHostVisibleBuffer = [&](
        vk::DeviceSize byteSize,
        vk::BufferUsageFlags usage,
        vk::Buffer& outBuffer,
        vk::DeviceMemory& outMemory,
        void*& outMapped) -> bool {
        vk::BufferCreateInfo bufInfo{};
        bufInfo.setSize(byteSize);
        bufInfo.setUsage(usage);
        bufInfo.setSharingMode(vk::SharingMode::eExclusive);
        if (device.createBuffer(&bufInfo, nullptr, &outBuffer) != vk::Result::eSuccess) {
            return false;
        }

        vk::MemoryRequirements memReq = device.getBufferMemoryRequirements(outBuffer);
        vk::MemoryAllocateInfo memAlloc{};
        memAlloc.setAllocationSize(memReq.size);
        memAlloc.setMemoryTypeIndex(m_vkCtx->findMemoryType(
            memReq.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

        if (device.allocateMemory(&memAlloc, nullptr, &outMemory) != vk::Result::eSuccess) {
            device.destroyBuffer(outBuffer);
            outBuffer = nullptr;
            return false;
        }
        device.bindBufferMemory(outBuffer, outMemory, 0);
        if (device.mapMemory(outMemory, 0, byteSize, vk::MemoryMapFlags{}, &outMapped) != vk::Result::eSuccess) {
            device.destroyBuffer(outBuffer);
            device.freeMemory(outMemory);
            outBuffer = nullptr;
            outMemory = nullptr;
            return false;
        }
        std::memset(outMapped, 0, static_cast<size_t>(byteSize));
        return true;
    };

    vk::DeviceSize objByteSize = static_cast<vk::DeviceSize>(newCapacity) * sizeof(GpuDrivenObjectData);
    vk::DeviceSize cmdByteSize = static_cast<vk::DeviceSize>(newCapacity) * sizeof(FCT::RHI::DrawIndexedIndirectCommand);

    if (!allocateHostVisibleBuffer(
            objByteSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            slot.objectBuffer,
            slot.objectMemory,
            slot.mappedObjects)) {
        return false;
    }

    if (!allocateHostVisibleBuffer(
            cmdByteSize,
            vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eIndirectBuffer |
                vk::BufferUsageFlagBits::eTransferSrc,
            slot.indirectBuffer,
            slot.indirectMemory,
            slot.mappedIndirect)) {
        destroyFrameSlotBuffers(slot);
        return false;
    }

    slot.capacity = newCapacity;
    updateFrameSlotDescriptors(slot);
    return true;
}

void GpuDrivenSystem::updateFrameSlotDescriptors(FrameBufferSlot& slot) {
    if (!m_vkCtx || !slot.descriptorSet || !slot.objectBuffer || !slot.indirectBuffer) {
        return;
    }
    vk::Device device = m_vkCtx->getDevice();

    vk::DescriptorBufferInfo objBufInfo{};
    objBufInfo.setBuffer(slot.objectBuffer);
    objBufInfo.setOffset(0);
    objBufInfo.setRange(static_cast<vk::DeviceSize>(slot.capacity) * sizeof(GpuDrivenObjectData));

    vk::DescriptorBufferInfo cmdBufInfo{};
    cmdBufInfo.setBuffer(slot.indirectBuffer);
    cmdBufInfo.setOffset(0);
    cmdBufInfo.setRange(static_cast<vk::DeviceSize>(slot.capacity) * sizeof(FCT::RHI::DrawIndexedIndirectCommand));

    vk::WriteDescriptorSet writes[2]{};
    writes[0].setDstSet(slot.descriptorSet);
    writes[0].setDstBinding(0);
    writes[0].setDescriptorCount(1);
    writes[0].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[0].setPBufferInfo(&objBufInfo);

    writes[1].setDstSet(slot.descriptorSet);
    writes[1].setDstBinding(1);
    writes[1].setDescriptorCount(1);
    writes[1].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[1].setPBufferInfo(&cmdBufInfo);

    device.updateDescriptorSets(2, writes, 0, nullptr);
}

void GpuDrivenSystem::destroyComputeResources() {
    if (!m_vkCtx) {
        m_pipelineReady = false;
        return;
    }
    vk::Device device = m_vkCtx->getDevice();
    if (!device) {
        m_pipelineReady = false;
        return;
    }

    device.waitIdle();
    for (size_t i = 0; i < kMaxFrameSlots; ++i) {
        destroyFrameSlotBuffers(m_frameSlots[i]);
        m_frameSlots[i].descriptorSet = nullptr;
    }

    if (m_descriptorPool) {
        device.destroyDescriptorPool(m_descriptorPool);
        m_descriptorPool = nullptr;
    }
    if (m_computePipeline) {
        device.destroyPipeline(m_computePipeline);
        m_computePipeline = nullptr;
    }
    if (m_pipelineLayout) {
        device.destroyPipelineLayout(m_pipelineLayout);
        m_pipelineLayout = nullptr;
    }
    if (m_descriptorSetLayout) {
        device.destroyDescriptorSetLayout(m_descriptorSetLayout);
        m_descriptorSetLayout = nullptr;
    }
    if (m_computeShaderModule) {
        device.destroyShaderModule(m_computeShaderModule);
        m_computeShaderModule = nullptr;
    }

    m_entityToIndirectSlot.clear();
    m_meshBoundsCache.clear();
    m_pipelineReady = false;
}

void GpuDrivenSystem::setCustomFrustumPlanes(const FCT::Vec4 planes[6]) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < 6; ++i) {
        m_activeFrustumPlanes[i] = normalizePlaneEquation(planes[i]);
    }
    m_useCustomFrustum = true;
}

void GpuDrivenSystem::clearCustomFrustumPlanes() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_useCustomFrustum = false;
}

void GpuDrivenSystem::extractFrustumPlanesFromViewProj(
    const FCT::Mat4& vp,
    FCT::Vec4 outPlanes[6],
    bool /*reverseZ*/) {
    // Gribb-Hartmann 方法：从行主序矩阵提取世界空间 6 个视锥平面（法线朝向视锥内部）
    FCT::Vec4 row0(vp.m[0], vp.m[1], vp.m[2], vp.m[3]);
    FCT::Vec4 row1(vp.m[4], vp.m[5], vp.m[6], vp.m[7]);
    FCT::Vec4 row2(vp.m[8], vp.m[9], vp.m[10], vp.m[11]);
    FCT::Vec4 row3(vp.m[12], vp.m[13], vp.m[14], vp.m[15]);

    // Left: row3 + row0 >= 0
    outPlanes[0] = normalizePlaneEquation(FCT::Vec4(row3.x + row0.x, row3.y + row0.y, row3.z + row0.z, row3.w + row0.w));
    // Right: row3 - row0 >= 0
    outPlanes[1] = normalizePlaneEquation(FCT::Vec4(row3.x - row0.x, row3.y - row0.y, row3.z - row0.z, row3.w - row0.w));
    // Bottom: row3 + row1 >= 0
    outPlanes[2] = normalizePlaneEquation(FCT::Vec4(row3.x + row1.x, row3.y + row1.y, row3.z + row1.z, row3.w + row1.w));
    // Top: row3 - row1 >= 0
    outPlanes[3] = normalizePlaneEquation(FCT::Vec4(row3.x - row1.x, row3.y - row1.y, row3.z - row1.z, row3.w - row1.w));
    // Near / Far (Vulkan 0 <= z_clip <= w_clip)
    outPlanes[4] = normalizePlaneEquation(row2);
    outPlanes[5] = normalizePlaneEquation(FCT::Vec4(row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w));
}

CacheGpuDrivenBounds GpuDrivenSystem::computeMeshLocalBounds(FCT::StaticMesh<uint32_t>* mesh) {
    CacheGpuDrivenBounds bounds{};
    if (!mesh || !mesh->getVertexBuffer() || mesh->getVertexCount() == 0) {
        return bounds;
    }

    FCT::VertexBuffer* vb = mesh->getVertexBuffer();
    const FCT::VertexLayout& layout = vb->getLayout();
    int posIndex = layout.getElementIndexByType(FCT::VtxType::Position3f);
    if (posIndex < 0) {
        posIndex = layout.getElementIndexByType(FCT::VtxType::Position4f);
    }
    if (posIndex < 0) {
        bounds.localRadius = 100000.0f;
        bounds.valid = true;
        return bounds;
    }

    size_t stride = layout.getStride();
    size_t posOffset = layout.getElementOffset(static_cast<size_t>(posIndex));
    const uint8_t* rawBytes = static_cast<const uint8_t*>(vb->getData());
    size_t vtxCount = vb->getVertexCount();
    if (!rawBytes || stride == 0 || vtxCount == 0) {
        return bounds;
    }

    FCT::Vec3<> minPt(1e30f, 1e30f, 1e30f);
    FCT::Vec3<> maxPt(-1e30f, -1e30f, -1e30f);
    for (size_t i = 0; i < vtxCount; ++i) {
        const float* p = reinterpret_cast<const float*>(rawBytes + i * stride + posOffset);
        minPt.x = std::min(minPt.x, p[0]);
        minPt.y = std::min(minPt.y, p[1]);
        minPt.z = std::min(minPt.z, p[2]);
        maxPt.x = std::max(maxPt.x, p[0]);
        maxPt.y = std::max(maxPt.y, p[1]);
        maxPt.z = std::max(maxPt.z, p[2]);
    }

    bounds.localCenter = (minPt + maxPt) * 0.5f;
    FCT::Vec3<> halfExtents = (maxPt - minPt) * 0.5f;
    bounds.localRadius = std::max(halfExtents.length(), 0.01f);
    bounds.enableGpuCulling = true;
    bounds.valid = true;
    return bounds;
}

void GpuDrivenSystem::updateActiveCameraFrustum() {
    if (m_useCustomFrustum) {
        return;
    }

    if (!g_engineGlobal.cameraSystem) {
        return;
    }

    FCT::Vec3<> camPos;
    FCT::Vec3<> camFwd;
    FCT::Vec3<> camUp;
    CameraComponent camComp{};
    if (!g_engineGlobal.cameraSystem->getActiveCameraWorldTransform(camPos, camFwd, camUp, camComp)) {
        return;
    }

    FCT::Mat4 view = FCT::Mat4::LookAt(camPos, camPos + camFwd, camUp);
    float aspect = 16.0f / 9.0f;
    if (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->platform->getWindow() &&
        Runtime::Engine::getInstance()->platform->getWindow()->getWidth() > 0 &&
        Runtime::Engine::getInstance()->platform->getWindow()->getHeight() > 0) {
        aspect = static_cast<float>(Runtime::Engine::getInstance()->platform->getWindow()->getWidth()) /
                 static_cast<float>(Runtime::Engine::getInstance()->platform->getWindow()->getHeight());
    }
    float fovDeg = std::clamp(camComp.fov, 10.0f, 160.0f);
    float nearP = std::max(camComp.nearPlane, 0.05f);
    float farP = std::max(camComp.farPlane, nearP + 100.0f);
    FCT::Mat4 proj = camComp.reverseZ
        ? FCT::Mat4::PerspectiveReverseZ(fovDeg, aspect, nearP, farP)
        : FCT::Mat4::Perspective(fovDeg, aspect, nearP, farP);
    FCT::Mat4 viewProj = proj * view;

    extractFrustumPlanesFromViewProj(viewProj, m_activeFrustumPlanes, camComp.reverseZ);
}

void GpuDrivenSystem::collectSceneRenderableObjects(std::vector<GpuDrivenObjectData>& outObjects) {
    outObjects.clear();
    m_entityToIndirectSlot.clear();
    if (!m_dataManager) {
        return;
    }

    auto registries = m_dataManager->currentRegistries();
    for (auto* reg : registries) {
        if (!reg) {
            continue;
        }
        auto view = std::as_const(*reg).view<StaticMeshInstance>();
        for (auto entity : view) {
            const auto& meshInst = view.get<StaticMeshInstance>(entity);
            if (!meshInst.mesh || meshInst.mesh->getIndexCount() == 0) {
                continue;
            }

            CacheGpuDrivenBounds localBounds{};
            if (const auto* cachedComp = reg->try_get<CacheGpuDrivenBounds>(entity);
                cachedComp && cachedComp->valid) {
                localBounds = *cachedComp;
            } else {
                auto cacheIt = m_meshBoundsCache.find(meshInst.mesh);
                if (cacheIt != m_meshBoundsCache.end()) {
                    localBounds = cacheIt->second;
                } else {
                    localBounds = computeMeshLocalBounds(meshInst.mesh);
                    m_meshBoundsCache[meshInst.mesh] = localBounds;
                }
            }

            FCT::Vec3<> worldCenter = localBounds.localCenter;
            float worldRadius = localBounds.localRadius;
            if (g_engineGlobal.matrixCacheSystem) {
                entt::entity transformEntity = entity;
                if (!reg->any_of<PositionComponent, RotationComponent, ScaleComponent, HierarchyComponent>(entity)) {
                    if (const auto* subTag = reg->try_get<RuntimeSubEntityTag>(entity)) {
                        entt::entity owner = static_cast<entt::entity>(subTag->ownerEntity);
                        if (owner != entt::null && reg->valid(owner)) {
                            transformEntity = owner;
                        }
                    }
                }
                FCT::Mat4 worldMat = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(reg, transformEntity);
                FCT::Vec4 wc4 = worldMat * FCT::Vec4(localBounds.localCenter.x, localBounds.localCenter.y, localBounds.localCenter.z, 1.0f);
                worldCenter = FCT::Vec3<>(wc4.x, wc4.y, wc4.z);
                worldRadius = localBounds.localRadius * extractMaxMatrixScale(worldMat);
            }

            GpuDrivenObjectData objData{};
            objData.worldBoundingSphere = FCT::Vec4(worldCenter.x, worldCenter.y, worldCenter.z, worldRadius);
            objData.indexCount = static_cast<uint32_t>(meshInst.mesh->getIndexCount());
            objData.firstIndex = 0;
            objData.vertexOffset = 0;
            objData.flags = localBounds.enableGpuCulling ? 1u : 3u;

            uint32_t slotIdx = static_cast<uint32_t>(outObjects.size());
            outObjects.push_back(objData);
            m_entityToIndirectSlot[EntityKey{reg, static_cast<uint32_t>(entity)}] = slotIdx;
        }
    }
}

void GpuDrivenSystem::dispatchComputeCullingForSlot(
    FrameBufferSlot& slot,
    const std::vector<GpuDrivenObjectData>& objects,
    const FCT::Vec4 frustumPlanes[6]) {
    uint32_t count = static_cast<uint32_t>(objects.size());
    slot.activeCount = count;
    m_lastDispatchedObjectCount = count;
    m_lastVisibleObjectCount = 0;
    if (count == 0 || !m_pipelineReady || !m_vkCtx) {
        return;
    }

    if (!ensureFrameSlotCapacity(slot, count)) {
        return;
    }

    std::memcpy(slot.mappedObjects, objects.data(), static_cast<size_t>(count) * sizeof(GpuDrivenObjectData));

    GpuDrivenCullPushConstants pc{};
    for (int i = 0; i < 6; ++i) {
        pc.frustumPlanes[i] = frustumPlanes[i];
    }
    pc.objectCount = count;
    pc.enableFrustumCulling = m_frustumCullingEnabled ? 1u : 0u;

    vk::CommandBuffer cmdBuf = m_vkCtx->beginSingleTimeCommands();
    cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline);
    cmdBuf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        m_pipelineLayout,
        0,
        1,
        &slot.descriptorSet,
        0,
        nullptr);
    cmdBuf.pushConstants(
        m_pipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        sizeof(GpuDrivenCullPushConstants),
        &pc);

    uint32_t groupCountX = (count + 63u) / 64u;
    cmdBuf.dispatch(groupCountX, 1, 1);

    // 标准 Vulkan 1.0 BufferMemoryBarrier：保证 Compute Shader 写入的 Indirect 命令对后续 vkCmdDrawIndexedIndirect 可见
    vk::BufferMemoryBarrier barrier{};
    barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
    barrier.setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eHostRead);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setBuffer(slot.indirectBuffer);
    barrier.setOffset(0);
    barrier.setSize(static_cast<vk::DeviceSize>(count) * sizeof(FCT::RHI::DrawIndexedIndirectCommand));

    cmdBuf.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eHost,
        vk::DependencyFlags{},
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr);

    m_vkCtx->endSingleTimeCommands(cmdBuf);

    const auto* cmds = static_cast<const FCT::RHI::DrawIndexedIndirectCommand*>(slot.mappedIndirect);
    uint32_t visibleCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (cmds[i].instanceCount > 0) {
            ++visibleCount;
        }
    }
    m_lastVisibleObjectCount = visibleCount;
}

void GpuDrivenSystem::updateRender() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled) {
        m_stagedObjects.clear();
        m_entityToIndirectSlot.clear();
        return;
    }
    if (!m_pipelineReady && !initComputePipeline()) {
        return;
    }

    updateActiveCameraFrustum();
    collectSceneRenderableObjects(m_stagedObjects);

    m_currentSlotIndex = (m_currentSlotIndex + 1) % static_cast<uint32_t>(kMaxFrameSlots);
    FrameBufferSlot& slot = m_frameSlots[m_currentSlotIndex];
    dispatchComputeCullingForSlot(slot, m_stagedObjects, m_activeFrustumPlanes);
}

bool GpuDrivenSystem::tryGetEntityIndirectDraw(
    const entt::registry& registry,
    entt::entity entity,
    void*& outIndirectBuffer,
    size_t& outByteOffset) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled || !m_pipelineReady) {
        return false;
    }
    const FrameBufferSlot& slot = m_frameSlots[m_currentSlotIndex];
    if (!slot.indirectBuffer || slot.activeCount == 0 || slot.capacity == 0) {
        return false;
    }

    auto it = m_entityToIndirectSlot.find(EntityKey{&registry, static_cast<uint32_t>(entity)});
    if (it == m_entityToIndirectSlot.end()) {
        return false;
    }

    uint32_t slotIdx = it->second;
    if (slotIdx >= slot.activeCount || slotIdx >= slot.capacity) {
        return false;
    }

    size_t byteOffset = static_cast<size_t>(slotIdx) * sizeof(FCT::RHI::DrawIndexedIndirectCommand);
    size_t capacityBytes = static_cast<size_t>(slot.capacity) * sizeof(FCT::RHI::DrawIndexedIndirectCommand);
    if (byteOffset + sizeof(FCT::RHI::DrawIndexedIndirectCommand) > capacityBytes) {
        return false;
    }

    outIndirectBuffer = static_cast<VkBuffer>(slot.indirectBuffer);
    outByteOffset = byteOffset;
    return true;
}

Status GpuDrivenSystem::executeStandaloneBatchCulling(
    const std::vector<GpuDrivenObjectData>& objects,
    const FCT::Vec4 frustumPlanes[6],
    std::vector<FCT::RHI::DrawIndexedIndirectCommand>& outCommands) {
    std::lock_guard<std::mutex> lock(m_mutex);
    outCommands.clear();
    if (!m_pipelineReady && !initComputePipeline()) {
        return InternalError("GpuDrivenSystem: Vulkan 1.0 Compute Culling 管线未就绪");
    }

    FCT::Vec4 normalizedPlanes[6];
    for (int i = 0; i < 6; ++i) {
        normalizedPlanes[i] = normalizePlaneEquation(frustumPlanes[i]);
    }

    FrameBufferSlot& slot = m_frameSlots[m_currentSlotIndex];
    dispatchComputeCullingForSlot(slot, objects, normalizedPlanes);

    outCommands.resize(objects.size());
    if (!objects.empty() && slot.mappedIndirect) {
        std::memcpy(
            outCommands.data(),
            slot.mappedIndirect,
            objects.size() * sizeof(FCT::RHI::DrawIndexedIndirectCommand));
    }
    return OkStatus();
}

void* GpuDrivenSystem::getCurrentIndirectBufferHandle() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pipelineReady) {
        return nullptr;
    }
    return static_cast<VkBuffer>(m_frameSlots[m_currentSlotIndex].indirectBuffer);
}

std::vector<FCT::RHI::DrawIndexedIndirectCommand> GpuDrivenSystem::readbackCurrentIndirectCommands() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FCT::RHI::DrawIndexedIndirectCommand> result;
    if (!m_pipelineReady) {
        return result;
    }
    const FrameBufferSlot& slot = m_frameSlots[m_currentSlotIndex];
    if (slot.activeCount == 0 || !slot.mappedIndirect) {
        return result;
    }
    result.resize(slot.activeCount);
    std::memcpy(
        result.data(),
        slot.mappedIndirect,
        static_cast<size_t>(slot.activeCount) * sizeof(FCT::RHI::DrawIndexedIndirectCommand));
    return result;
}

} // namespace MQEngine
