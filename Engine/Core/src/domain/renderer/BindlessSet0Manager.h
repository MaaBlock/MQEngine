#ifndef MQENGINE_DOMAIN_RENDERER_BINDLESSSET0MANAGER_H
#define MQENGINE_DOMAIN_RENDERER_BINDLESSSET0MANAGER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/base/CoreEngineManager.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/resource/table/GpuResourceTable.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            /**
             * @brief GPU 环形宿主缓冲区记录（支持 HostVisible+HostCoherent 持久映射）
             */
            struct GpuBufferResource
            {
                vk::Buffer buffer;
                vk::DeviceMemory memory;
                void* mappedPtr = nullptr;
                size_t capacityBytes = 0;
            };

            /**
             * @brief 全局 Bindless Set 0 渲染管线管理器 (100% 真实 GPU 驱动实现)
             *        集中管理全局 Set 0 绑定 (Binding 0-7) 与三缓冲 GPU 存储
             */
            class ENGINE_API BindlessSet0Manager : public CoreEngineManager<BindlessSet0Manager>
            {
            public:
                static constexpr uint32_t kMaxFramesInFlight = 3;
                static constexpr uint32_t kDefaultMaxBindlessTextures = 4096;

                // 预分配缓冲区容量基线
                static constexpr size_t kInitialInstanceCount = 4096;   // 4096 * 160B = 640KB
                static constexpr size_t kInitialMaterialCount = 1024;   // 1024 * 64B  = 64KB
                static constexpr size_t kInitialTextureMetaCount = 4096;// 4096 * 32B  = 128KB

                BindlessSet0Manager();
                ~BindlessSet0Manager();

                BindlessSet0Manager(const BindlessSet0Manager&) = delete;
                BindlessSet0Manager& operator=(const BindlessSet0Manager&) = delete;

                /**
                 * @brief 初始化 GPU 描述符集布局、分配三缓冲 Set 0 并在显存创建 UBO/SSBO
                 */
                Status init(FCT::Context* ctx);

                /**
                 * @brief 终止并释放所有 GPU 描述符、缓冲区与显存
                 */
                void term() noexcept;

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }
                DescriptorIndexingTier getTier() const noexcept { return m_tier; }
                bool isNativeBindlessSupported() const noexcept
                {
                    return m_tier != DescriptorIndexingTier::Tier0_AtlasFallback;
                }

                /**
                 * @brief 将当前提交帧的实例、材质、元数据与场景 Uniform 写入 GPU 映射显存
                 */
                void uploadFrameData(uint32_t frameIndex);

                void updateFrameData(
                    uint32_t frameIndex,
                    const std::vector<GpuInstanceRow>& instances,
                    const std::vector<GpuMaterialRecord>& materials,
                    const std::vector<GpuTextureRecord>& textures,
                    const GpuSceneUniformData& sceneUniform);

                /**
                 * @brief 原地更新 GPU 全局描述符集中 Binding 4 的指定槽位（调用 vkUpdateDescriptorSets）
                 */
                void updateTextureSlot(uint32_t slotIndex, FCT::Image* image);

                // 获取 GPU 描述符集与管线布局，供管线创建与绘制绑定
                vk::DescriptorSetLayout getDescriptorSetLayout() const noexcept { return m_descriptorSetLayout; }
                vk::PipelineLayout getPipelineLayout() const noexcept { return m_pipelineLayout; }
                void bind(FCT::RHI::CommandBuffer* cmdBuf, uint32_t frameIndex, void* overridePipelineLayout = nullptr, vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics);
                vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const noexcept
                {
                    if (m_descriptorSets.empty()) {
                        return nullptr;
                    }
                    return m_descriptorSets[frameIndex % kMaxFramesInFlight];
                }
                void* getGlobalDescriptorSetHandle(uint32_t frameIndex) const noexcept
                {
                    if (m_descriptorSets.empty()) {
                        return nullptr;
                    }
                    return reinterpret_cast<void*>(static_cast<VkDescriptorSet>(m_descriptorSets[frameIndex % kMaxFramesInFlight]));
                }

                // CPU 端镜像数据读取
                const std::vector<GpuInstanceRow>& getInstances(uint32_t frameIndex) const;
                const std::vector<GpuMaterialRecord>& getMaterials(uint32_t frameIndex) const;
                const std::vector<GpuTextureRecord>& getTextures(uint32_t frameIndex) const;
                const GpuSceneUniformData& getSceneUniform(uint32_t frameIndex) const;

                FCT::Image* getWhiteFallbackTexture() const noexcept { return m_whiteFallbackImage; }
                FCT::Image* getNormalFallbackTexture() const noexcept { return m_normalFallbackImage; }
                FCT::Image* getCheckerFallbackTexture() const noexcept { return m_checkerFallbackImage; }
                AtlasShelfPacker* getAtlasPacker() noexcept { return &m_atlasPacker; }

            private:
                void probeHardwareTier(FCT::Context* ctx);
                void createDefaultFallbackTextures(FCT::Context* ctx);
                void initTripleBufferedTables();
                Status createGpuBuffers();
                Status createDescriptorSetLayout();
                Status allocateDescriptorSets();
                void writeInitialDescriptorSets();
                void destroyGpuBuffers() noexcept;

                Status createMappedGpuBuffer(size_t size, vk::BufferUsageFlags usage, GpuBufferResource& outRes);
                void destroyGpuBuffer(GpuBufferResource& res) noexcept;

                std::atomic<bool> m_initialized{false};
                FCT::Context* m_ctx = nullptr;
                vk::Device m_vkDevice;
                DescriptorIndexingTier m_tier = DescriptorIndexingTier::Tier0_AtlasFallback;
                bool m_rtSupported = false;

                // 默认回退纹理 (Slots 0, 1, 2)
                FCT::Image* m_whiteFallbackImage = nullptr;
                FCT::Image* m_normalFallbackImage = nullptr;
                FCT::Image* m_checkerFallbackImage = nullptr;

                // 全局采样器 (Binding 6)
                FCT::Sampler* m_linearSampler = nullptr;

                // 三缓冲 GPU 存储缓冲区
                std::array<GpuBufferResource, kMaxFramesInFlight> m_sceneUniformBuffers; // Binding 0
                std::array<GpuBufferResource, kMaxFramesInFlight> m_instanceBuffers;     // Binding 1
                std::array<GpuBufferResource, kMaxFramesInFlight> m_materialBuffers;     // Binding 2
                std::array<GpuBufferResource, kMaxFramesInFlight> m_textureMetaBuffers; // Binding 3

                // Set 0 描述符集布局与多帧实例
                vk::DescriptorSetLayout m_descriptorSetLayout;
                vk::PipelineLayout m_pipelineLayout;
                std::vector<vk::DescriptorSet> m_descriptorSets;

                // 三缓冲 CPU 数据镜像
                std::array<std::vector<GpuInstanceRow>, kMaxFramesInFlight> m_instances;
                std::array<std::vector<GpuMaterialRecord>, kMaxFramesInFlight> m_materials;
                std::array<std::vector<GpuTextureRecord>, kMaxFramesInFlight> m_textures;
                std::array<GpuSceneUniformData, kMaxFramesInFlight> m_sceneUniforms;

                // CPU 端槽位贴图指针记录
                std::vector<FCT::Image*> m_registeredTextures;

                // Tier 0 全局装箱
                AtlasShelfPacker m_atlasPacker{2048, 2048, 16};
                FCT::Image* m_globalAtlasImage = nullptr;
            };

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_RENDERER_BINDLESSSET0MANAGER_H
