#ifndef MQENGINE_GPU_DRIVEN_SYSTEM_H
#define MQENGINE_GPU_DRIVEN_SYSTEM_H

#include "../../thirdparty/thirdparty.h"
#include "../../runtime/system/ISystem.h"
#include "../../scene/manager/DataManager.h"
#include "../../scene/data/Component.h"

namespace MQEngine {

/**
 * @brief Vulkan 1.0/1.1 兼容的 GPU Driven 物体包围球与索引绘制参数（对齐 std430，无 Bindless 依赖）
 */
struct alignas(16) GpuDrivenObjectData {
    FCT::Vec4 worldBoundingSphere = FCT::Vec4(0.0f, 0.0f, 0.0f, 1.0f); // xyz: 世界空间球心, w: 世界空间半径
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t flags = 1; // bit 0: 启用 GPU 视锥剔除, bit 1: 强制可见
};

/**
 * @brief Vulkan 1.0 兼容的 Compute Shader 视锥剔除 PushConstants（共 112 字节，低于 Vulkan 1.0 128 字节上限）
 */
struct alignas(16) GpuDrivenCullPushConstants {
    FCT::Vec4 frustumPlanes[6];
    uint32_t objectCount = 0;
    uint32_t enableFrustumCulling = 1;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};

/**
 * @brief 基于低版本 Vulkan (Vulkan 1.0/1.1) 的无 Bindless GPU Driven 渲染系统
 * @details 不依赖 VK_EXT_descriptor_indexing / Bindless，仅使用 Vulkan 1.0 核心 Compute Shader + SSBO + vkCmdDrawIndexedIndirect
 */
class ENGINE_API GpuDrivenSystem : public ISystem {
public:
    GpuDrivenSystem(FCT::Context* ctx, DataManager* dataManager);
    ~GpuDrivenSystem() override;

    void onActivate() override;
    void onDeactivate() override;
    void updateLogic() override;
    void updateRender() override;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled && m_pipelineReady; }

    void setFrustumCullingEnabled(bool enabled) { m_frustumCullingEnabled = enabled; }
    bool isFrustumCullingEnabled() const { return m_frustumCullingEnabled; }

    /**
     * @brief 是否使用 Bindless 描述符索引扩展（固定返回 false，遵循低版本 Vulkan 规范）
     */
    bool usesBindless() const { return false; }

    /**
     * @brief 返回当前 GPU Driven 管线面向的最低 Vulkan 标准版本
     */
    const char* getTargetVulkanVersion() const { return "Vulkan 1.0"; }

    /**
     * @brief 设置自定义视锥平面（平面方程 ax + by + cz + d >= 0 为视锥内）
     */
    void setCustomFrustumPlanes(const FCT::Vec4 planes[6]);
    void clearCustomFrustumPlanes();

    /**
     * @brief 从 View * Projection 矩阵提取 6 个归一化世界空间视锥平面
     */
    static void extractFrustumPlanesFromViewProj(
        const FCT::Mat4& viewProj,
        FCT::Vec4 outPlanes[6],
        bool reverseZ = true);

    /**
     * @brief 计算 StaticMesh 的局部空间包围球
     */
    static CacheGpuDrivenBounds computeMeshLocalBounds(FCT::StaticMesh<uint32_t>* mesh);

    /**
     * @brief 查询指定实体在当前渲染帧对应的 VkDrawIndexedIndirectCommand 缓冲句柄与字节偏移
     */
    bool tryGetEntityIndirectDraw(
        const entt::registry& registry,
        entt::entity entity,
        void*& outIndirectBuffer,
        size_t& outByteOffset) const;

    /**
     * @brief 手动提交一批物体数据并在 GPU 上立即执行 Vulkan 1.0 Compute Shader 视锥剔除（供测试与独立验证使用）
     */
    Status executeStandaloneBatchCulling(
        const std::vector<GpuDrivenObjectData>& objects,
        const FCT::Vec4 frustumPlanes[6],
        std::vector<FCT::RHI::DrawIndexedIndirectCommand>& outCommands);

    uint32_t getLastDispatchedObjectCount() const { return m_lastDispatchedObjectCount; }
    uint32_t getLastVisibleObjectCount() const { return m_lastVisibleObjectCount; }
    void* getCurrentIndirectBufferHandle() const;
    std::vector<FCT::RHI::DrawIndexedIndirectCommand> readbackCurrentIndirectCommands() const;

private:
    static constexpr size_t kMaxFrameSlots = 3;
    static constexpr uint32_t kInitialObjectCapacity = 4096;

    struct EntityKey {
        const entt::registry* registry = nullptr;
        uint32_t entityId = 0;

        bool operator==(const EntityKey& other) const {
            return registry == other.registry && entityId == other.entityId;
        }
    };

    struct EntityKeyHash {
        size_t operator()(const EntityKey& key) const noexcept {
            size_t h1 = std::hash<const void*>{}(key.registry);
            size_t h2 = std::hash<uint32_t>{}(key.entityId);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    struct FrameBufferSlot {
        vk::Buffer objectBuffer;
        vk::DeviceMemory objectMemory;
        void* mappedObjects = nullptr;

        vk::Buffer indirectBuffer;
        vk::DeviceMemory indirectMemory;
        void* mappedIndirect = nullptr;

        vk::DescriptorSet descriptorSet;
        uint32_t capacity = 0;
        uint32_t activeCount = 0;
    };

    bool initComputePipeline();
    void destroyComputeResources();
    bool ensureFrameSlotCapacity(FrameBufferSlot& slot, uint32_t requiredCapacity);
    void destroyFrameSlotBuffers(FrameBufferSlot& slot);
    void updateFrameSlotDescriptors(FrameBufferSlot& slot);

    void updateActiveCameraFrustum();
    void collectSceneRenderableObjects(std::vector<GpuDrivenObjectData>& outObjects);
    void dispatchComputeCullingForSlot(
        FrameBufferSlot& slot,
        const std::vector<GpuDrivenObjectData>& objects,
        const FCT::Vec4 frustumPlanes[6]);

private:
    FCT::Context* m_ctx = nullptr;
    FCT::VK_Context* m_vkCtx = nullptr;
    DataManager* m_dataManager = nullptr;

    bool m_enabled = true;
    bool m_frustumCullingEnabled = true;
    bool m_pipelineReady = false;
    bool m_useCustomFrustum = false;

    FCT::Vec4 m_activeFrustumPlanes[6];
    uint32_t m_currentSlotIndex = 0;
    uint32_t m_lastDispatchedObjectCount = 0;
    uint32_t m_lastVisibleObjectCount = 0;

    vk::ShaderModule m_computeShaderModule;
    vk::DescriptorSetLayout m_descriptorSetLayout;
    vk::PipelineLayout m_pipelineLayout;
    vk::Pipeline m_computePipeline;
    vk::DescriptorPool m_descriptorPool;

    FrameBufferSlot m_frameSlots[kMaxFrameSlots];
    mutable std::mutex m_mutex;
    std::vector<GpuDrivenObjectData> m_stagedObjects;
    std::unordered_map<EntityKey, uint32_t, EntityKeyHash> m_entityToIndirectSlot;
    std::unordered_map<FCT::StaticMesh<uint32_t>*, CacheGpuDrivenBounds> m_meshBoundsCache;
};

} // namespace MQEngine

#endif // MQENGINE_GPU_DRIVEN_SYSTEM_H
