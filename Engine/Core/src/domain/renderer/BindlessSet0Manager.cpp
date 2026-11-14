#include "BindlessSet0Manager.h"
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <FCT/Context/VK_Context.h>
#include <FCT/RHI/VK_TextureView.h>
#include <FCT/RHI/VK_DescriptorPool.h>
#include <FCT/RHI/VK_Sampler.h>
#include <FCT/RHI/VK_CommandBuffer.h>
#include <cassert>
#include <cstring>

namespace MQEngine
{
    namespace Domain
    {
        namespace Renderer
        {
            BindlessSet0Manager::BindlessSet0Manager()
            {
                m_registeredTextures.resize(kDefaultMaxBindlessTextures, nullptr);
            }

            BindlessSet0Manager::~BindlessSet0Manager()
            {
                term();
            }

            Status BindlessSet0Manager::init(FCT::Context* ctx)
            {
                if (m_initialized.load(std::memory_order_acquire)) {
                    return OkStatus();
                }

                if (!ctx) {
                    return InvalidArgumentError("BindlessSet0Manager::init: ctx 不能为空");
                }

                m_ctx = ctx;
                probeHardwareTier(ctx);

#if defined(FCT_USE_VULKAN)
                if (auto* vkCtx = dynamic_cast<FCT::VK_Context*>(ctx)) {
                    m_vkDevice = vkCtx->device();
                }
#endif

                createDefaultFallbackTextures(ctx);
                initTripleBufferedTables();

                if (m_vkDevice) {
                    CHECK_STATUS(createGpuBuffers());
                    CHECK_STATUS(createDescriptorSetLayout());
                    CHECK_STATUS(allocateDescriptorSets());
                    writeInitialDescriptorSets();
                }

                m_initialized.store(true, std::memory_order_release);
                return OkStatus();
            }

            void BindlessSet0Manager::term() noexcept
            {
                if (!m_initialized.load(std::memory_order_acquire)) {
                    return;
                }

                if (m_vkDevice) {
                    vkDeviceWaitIdle(static_cast<VkDevice>(m_vkDevice));
                }

                destroyGpuBuffers();

                if (m_pipelineLayout && m_vkDevice) {
                    m_vkDevice.destroyPipelineLayout(m_pipelineLayout);
                    m_pipelineLayout = nullptr;
                }

                if (auto* vkCtx = dynamic_cast<FCT::VK_Context*>(m_ctx)) {
                    vkCtx->setBindlessSetLayout(nullptr);
                }

                if (m_descriptorSetLayout && m_vkDevice) {
                    m_vkDevice.destroyDescriptorSetLayout(m_descriptorSetLayout);
                    m_descriptorSetLayout = nullptr;
                }
                m_descriptorSets.clear();

                if (m_linearSampler) {
                    m_linearSampler->release();
                    m_linearSampler = nullptr;
                }

                if (auto* gpuTable = Resource::GpuResourceTable::InstancePtr()) {
                    if (m_whiteFallbackImage) {
                        gpuTable->unregisterResource(m_whiteFallbackImage);
                        m_whiteFallbackImage->release();
                        m_whiteFallbackImage = nullptr;
                    }
                    if (m_normalFallbackImage) {
                        gpuTable->unregisterResource(m_normalFallbackImage);
                        m_normalFallbackImage->release();
                        m_normalFallbackImage = nullptr;
                    }
                    if (m_checkerFallbackImage) {
                        gpuTable->unregisterResource(m_checkerFallbackImage);
                        m_checkerFallbackImage->release();
                        m_checkerFallbackImage = nullptr;
                    }
                    if (m_globalAtlasImage) {
                        gpuTable->unregisterResource(m_globalAtlasImage);
                        m_globalAtlasImage->release();
                        m_globalAtlasImage = nullptr;
                    }
                }

                m_initialized.store(false, std::memory_order_release);
            }

            void BindlessSet0Manager::probeHardwareTier(FCT::Context* ctx)
            {
                if (!ctx || !ctx->isBindlessSupported()) {
                    m_tier = DescriptorIndexingTier::Tier0_AtlasFallback;
                    return;
                }

#if defined(FCT_USE_VULKAN)
                if (auto* vkCtx = dynamic_cast<FCT::VK_Context*>(ctx)) {
                    vk::PhysicalDevice phyDevice = vkCtx->getPhysicalDevice();
                    uint32_t apiVersion = phyDevice.getProperties().apiVersion;

                    if (apiVersion >= VK_API_VERSION_1_2) {
                        vk::PhysicalDeviceDescriptorIndexingFeatures supportedDescriptorIndexingFeatures{};
                        vk::PhysicalDeviceFeatures2 deviceFeatures2{};
                        deviceFeatures2.pNext = &supportedDescriptorIndexingFeatures;
                        phyDevice.getFeatures2(&deviceFeatures2);

                        if (supportedDescriptorIndexingFeatures.descriptorBindingPartiallyBound &&
                            supportedDescriptorIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind &&
                            supportedDescriptorIndexingFeatures.runtimeDescriptorArray) {
                            if (supportedDescriptorIndexingFeatures.descriptorBindingStorageBufferUpdateAfterBind &&
                                supportedDescriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing) {
                                m_tier = DescriptorIndexingTier::Tier3_UnboundedFull;
                            } else {
                                m_tier = DescriptorIndexingTier::Tier2_UpdateAfterBind;
                            }
                            return;
                        }
                    }
                    m_tier = DescriptorIndexingTier::Tier1_BasicBindless;
                    return;
                }
#endif
                m_tier = DescriptorIndexingTier::Tier1_BasicBindless;
            }

            void BindlessSet0Manager::createDefaultFallbackTextures(FCT::Context* ctx)
            {
                if (!ctx) {
                    return;
                }

                auto* gpuTable = Resource::GpuResourceTable::InstancePtr();

                // 1. 槽位 0：1x1 纯白纹理 (0xFFFFFFFF)
                if (!m_whiteFallbackImage) {
                    uint32_t whitePixel = 0xFFFFFFFF;
                    auto* img = FCT_NEW(FCT::SingleBufferImage, ctx);
                    img->width(1);
                    img->height(1);
                    img->format(FCT::Format::R8G8B8A8_UNORM);
                    img->as(FCT::ImageUsage::Texture);
                    img->initData(&whitePixel, static_cast<int>(sizeof(whitePixel)));
                    img->create();
                    m_whiteFallbackImage = img;

                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_whiteFallbackImage,
                            Resource::GpuResourceType::Texture,
                            sizeof(whitePixel),
                            "Fallback_White1x1");
                        (void)gpuTable->allocateBindlessSlot(m_whiteFallbackImage);
                        gpuTable->setDefaultFallbackHandle(Resource::GpuResourceType::Texture, m_whiteFallbackImage);
                    }
                    m_registeredTextures[0] = m_whiteFallbackImage;
                }

                // 2. 槽位 1：1x1 默认平坦法线纹理 (0xFFFF8080 即 Normal=(0,0,1))
                if (!m_normalFallbackImage) {
                    uint32_t normalPixel = 0xFFFF8080;
                    auto* img = FCT_NEW(FCT::SingleBufferImage, ctx);
                    img->width(1);
                    img->height(1);
                    img->format(FCT::Format::R8G8B8A8_UNORM);
                    img->as(FCT::ImageUsage::Texture);
                    img->initData(&normalPixel, static_cast<int>(sizeof(normalPixel)));
                    img->create();
                    m_normalFallbackImage = img;

                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_normalFallbackImage,
                            Resource::GpuResourceType::Texture,
                            sizeof(normalPixel),
                            "Fallback_Normal1x1");
                        (void)gpuTable->allocateBindlessSlot(m_normalFallbackImage);
                    }
                    m_registeredTextures[1] = m_normalFallbackImage;
                }

                // 3. 槽位 2：16x16 紫黑相间棋盘格占位纹理
                if (!m_checkerFallbackImage) {
                    constexpr uint32_t kGridDim = 16;
                    std::vector<uint32_t> checkerPixels(kGridDim * kGridDim);
                    for (uint32_t y = 0; y < kGridDim; ++y) {
                        for (uint32_t x = 0; x < kGridDim; ++x) {
                            const bool isMagenta = ((x / 4) + (y / 4)) % 2 == 0;
                            checkerPixels[y * kGridDim + x] = isMagenta ? 0xFFFF00FF : 0xFF000000;
                        }
                    }

                    auto* img = FCT_NEW(FCT::SingleBufferImage, ctx);
                    img->width(kGridDim);
                    img->height(kGridDim);
                    img->format(FCT::Format::R8G8B8A8_UNORM);
                    img->as(FCT::ImageUsage::Texture);
                    img->initData(
                        checkerPixels.data(),
                        static_cast<int>(checkerPixels.size() * sizeof(uint32_t)));
                    img->create();
                    m_checkerFallbackImage = img;

                    if (gpuTable) {
                        gpuTable->registerResource(
                            m_checkerFallbackImage,
                            Resource::GpuResourceType::Texture,
                            checkerPixels.size() * sizeof(uint32_t),
                            "Fallback_Checker16x16");
                        (void)gpuTable->allocateBindlessSlot(m_checkerFallbackImage);
                    }
                    m_registeredTextures[2] = m_checkerFallbackImage;
                }

                // 全局线性采样器 (Binding 6)
                if (!m_linearSampler) {
                    m_linearSampler = ctx->createResource<FCT::Sampler>();
                    if (m_linearSampler) {
                        m_linearSampler->setFilter(FCT::FilterMode::Linear, FCT::FilterMode::Linear, FCT::FilterMode::Linear);
                        m_linearSampler->setAddressMode(FCT::AddressMode::Repeat, FCT::AddressMode::Repeat, FCT::AddressMode::Repeat);
                        m_linearSampler->create();
                    }
                }
            }

            void BindlessSet0Manager::initTripleBufferedTables()
            {
                for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
                    m_instances[i].clear();
                    m_instances[i].reserve(kInitialInstanceCount);

                    m_materials[i].clear();
                    m_materials[i].reserve(kInitialMaterialCount);

                    m_textures[i].clear();
                    m_textures[i].resize(3);
                    // 槽位 0：White
                    m_textures[i][0].uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
                    m_textures[i][0].bindlessOrPageIndex = 0;
                    // 槽位 1：Normal
                    m_textures[i][1].uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
                    m_textures[i][1].bindlessOrPageIndex = 1;
                    // 槽位 2：Checker
                    m_textures[i][2].uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
                    m_textures[i][2].bindlessOrPageIndex = 2;

                    m_sceneUniforms[i] = GpuSceneUniformData{};
                }
            }

            Status BindlessSet0Manager::createMappedGpuBuffer(size_t size, vk::BufferUsageFlags usage, GpuBufferResource& outRes)
            {
                if (size == 0 || !m_vkDevice || !m_ctx) {
                    return InvalidArgumentError("BindlessSet0Manager: 无效的显存分配参数");
                }

                auto* vkCtx = dynamic_cast<FCT::VK_Context*>(m_ctx);
                if (!vkCtx) {
                    return InternalError("BindlessSet0Manager: 上下文非 VK_Context");
                }

                destroyGpuBuffer(outRes);
                outRes.capacityBytes = size;

                vk::BufferCreateInfo bufferInfo{};
                bufferInfo.size = size;
                bufferInfo.usage = usage | vk::BufferUsageFlagBits::eTransferDst;
                if (vkCtx->isBufferDeviceAddressSupported()) {
                    bufferInfo.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
                }
                bufferInfo.sharingMode = vk::SharingMode::eExclusive;

                outRes.buffer = m_vkDevice.createBuffer(bufferInfo);
                assert(outRes.buffer && "BindlessSet0Manager: 创建 vk::Buffer 失败");

                vk::MemoryRequirements memReq = m_vkDevice.getBufferMemoryRequirements(outRes.buffer);

                vk::MemoryAllocateInfo allocInfo{};
                allocInfo.allocationSize = memReq.size;
                allocInfo.memoryTypeIndex = vkCtx->findMemoryType(
                    memReq.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
                );

                vk::MemoryAllocateFlagsInfo flagsInfo{};
                if (bufferInfo.usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
                    flagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
                    allocInfo.pNext = &flagsInfo;
                }

                outRes.memory = m_vkDevice.allocateMemory(allocInfo);
                assert(outRes.memory && "BindlessSet0Manager: 分配 vk::DeviceMemory 失败");

                m_vkDevice.bindBufferMemory(outRes.buffer, outRes.memory, 0);

                outRes.mappedPtr = m_vkDevice.mapMemory(outRes.memory, 0, size);
                assert(outRes.mappedPtr && "BindlessSet0Manager: 映射显存指针失败");

                return OkStatus();
            }

            void BindlessSet0Manager::destroyGpuBuffer(GpuBufferResource& res) noexcept
            {
                if (res.mappedPtr && m_vkDevice && res.memory) {
                    m_vkDevice.unmapMemory(res.memory);
                    res.mappedPtr = nullptr;
                }
                if (res.buffer && m_vkDevice) {
                    m_vkDevice.destroyBuffer(res.buffer);
                    res.buffer = nullptr;
                }
                if (res.memory && m_vkDevice) {
                    m_vkDevice.freeMemory(res.memory);
                    res.memory = nullptr;
                }
                res.capacityBytes = 0;
            }

            Status BindlessSet0Manager::createGpuBuffers()
            {
                for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                    // Binding 0: Scene Uniform Buffer (256B)
                    CHECK_STATUS(createMappedGpuBuffer(
                        sizeof(GpuSceneUniformData),
                        vk::BufferUsageFlagBits::eUniformBuffer,
                        m_sceneUniformBuffers[f]));

                    // Binding 1: Instance Data Storage Buffer (640KB)
                    CHECK_STATUS(createMappedGpuBuffer(
                        kInitialInstanceCount * sizeof(GpuInstanceRow),
                        vk::BufferUsageFlagBits::eStorageBuffer,
                        m_instanceBuffers[f]));

                    // Binding 2: Material Data Storage Buffer (64KB)
                    CHECK_STATUS(createMappedGpuBuffer(
                        kInitialMaterialCount * sizeof(GpuMaterialRecord),
                        vk::BufferUsageFlagBits::eStorageBuffer,
                        m_materialBuffers[f]));

                    // Binding 3: Texture Metadata Storage Buffer (128KB)
                    CHECK_STATUS(createMappedGpuBuffer(
                        kInitialTextureMetaCount * sizeof(GpuTextureRecord),
                        vk::BufferUsageFlagBits::eStorageBuffer,
                        m_textureMetaBuffers[f]));
                }
                return OkStatus();
            }

            void BindlessSet0Manager::destroyGpuBuffers() noexcept
            {
                for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                    destroyGpuBuffer(m_sceneUniformBuffers[f]);
                    destroyGpuBuffer(m_instanceBuffers[f]);
                    destroyGpuBuffer(m_materialBuffers[f]);
                    destroyGpuBuffer(m_textureMetaBuffers[f]);
                }
            }

            Status BindlessSet0Manager::createDescriptorSetLayout()
            {
                std::vector<vk::DescriptorSetLayoutBinding> bindings;
                std::vector<vk::DescriptorBindingFlags> bindingFlags;
                const vk::ShaderStageFlags allStages = vk::ShaderStageFlagBits::eAll;

                // Binding 0: Scene Uniform Buffer (UBO)
                bindings.emplace_back(0, vk::DescriptorType::eUniformBuffer, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 1: Instance Data Storage Buffer (SSBO)
                bindings.emplace_back(1, vk::DescriptorType::eStorageBuffer, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 2: Material Data Storage Buffer (SSBO)
                bindings.emplace_back(2, vk::DescriptorType::eStorageBuffer, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 3: Texture Metadata Storage Buffer (SSBO)
                bindings.emplace_back(3, vk::DescriptorType::eStorageBuffer, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 4: Global Bindless Texture Array (Texture2D g_Textures[])
                bindings.emplace_back(4, vk::DescriptorType::eSampledImage, kDefaultMaxBindlessTextures, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 5: Global Texture Atlas (Texture2D g_AtlasTexture)
                bindings.emplace_back(5, vk::DescriptorType::eSampledImage, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                // Binding 6: Linear Sampler (SamplerState g_LinearSampler)
                bindings.emplace_back(6, vk::DescriptorType::eSampler, 1, allStages, nullptr);
                bindingFlags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);

                vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
                flagsInfo.setBindingFlags(bindingFlags);

                vk::DescriptorSetLayoutCreateInfo layoutInfo{};
                layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
                layoutInfo.setBindings(bindings);
                layoutInfo.setPNext(&flagsInfo);

                m_descriptorSetLayout = m_vkDevice.createDescriptorSetLayout(layoutInfo);
                assert(m_descriptorSetLayout && "BindlessSet0Manager: 无法创建 Set 0 布局");
                if (auto* vkCtx = dynamic_cast<FCT::VK_Context*>(m_ctx)) {
                    vkCtx->setBindlessSetLayout(m_descriptorSetLayout);
                }

                vk::PipelineLayoutCreateInfo pipeLayoutInfo{};
                pipeLayoutInfo.setLayoutCount = 1;
                pipeLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
                m_pipelineLayout = m_vkDevice.createPipelineLayout(pipeLayoutInfo);
                assert(m_pipelineLayout && "BindlessSet0Manager: 无法创建 Set 0 管线布局");

                return OkStatus();
            }

            Status BindlessSet0Manager::allocateDescriptorSets()
            {
                auto* descPoolObj = dynamic_cast<FCT::RHI::VK_DescriptorPool*>(m_ctx->getDescriptorPool());
                if (!descPoolObj || !descPoolObj->getPool()) {
                    return InternalError("BindlessSet0Manager: VK_DescriptorPool 未就绪");
                }

                std::vector<vk::DescriptorSetLayout> layouts(kMaxFramesInFlight, m_descriptorSetLayout);
                vk::DescriptorSetAllocateInfo allocInfo{};
                allocInfo.setDescriptorPool(descPoolObj->getPool());
                allocInfo.setDescriptorSetCount(kMaxFramesInFlight);
                allocInfo.setSetLayouts(layouts);

                m_descriptorSets = m_vkDevice.allocateDescriptorSets(allocInfo);
                assert(m_descriptorSets.size() == kMaxFramesInFlight && "BindlessSet0Manager: 分配描述符集失败");

                return OkStatus();
            }

            void BindlessSet0Manager::writeInitialDescriptorSets()
            {
                if (!m_vkDevice || m_descriptorSets.empty()) {
                    return;
                }

                vk::ImageView whiteView = nullptr;
                if (m_whiteFallbackImage && m_whiteFallbackImage->currentTextureView()) {
                    if (auto* vkView = dynamic_cast<FCT::RHI::VK_TextureView*>(m_whiteFallbackImage->currentTextureView())) {
                        whiteView = vkView->view();
                    }
                }

                vk::Sampler samplerHandle = nullptr;
                if (m_linearSampler) {
                    if (auto* vkSampler = dynamic_cast<FCT::RHI::VK_Sampler*>(m_linearSampler)) {
                        samplerHandle = vkSampler->getSampler();
                    }
                }

                for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                    std::vector<vk::WriteDescriptorSet> writes;

                    // Binding 0: Scene Uniform Buffer
                    vk::DescriptorBufferInfo uboInfo{};
                    uboInfo.setBuffer(m_sceneUniformBuffers[f].buffer);
                    uboInfo.setOffset(0);
                    uboInfo.setRange(sizeof(GpuSceneUniformData));

                    vk::WriteDescriptorSet uboWrite{};
                    uboWrite.setDstSet(m_descriptorSets[f]);
                    uboWrite.setDstBinding(0);
                    uboWrite.setDstArrayElement(0);
                    uboWrite.setDescriptorType(vk::DescriptorType::eUniformBuffer);
                    uboWrite.setDescriptorCount(1);
                    uboWrite.setPBufferInfo(&uboInfo);
                    writes.push_back(uboWrite);

                    // Binding 1: Instance Data SSBO
                    vk::DescriptorBufferInfo instInfo{};
                    instInfo.setBuffer(m_instanceBuffers[f].buffer);
                    instInfo.setOffset(0);
                    instInfo.setRange(m_instanceBuffers[f].capacityBytes);

                    vk::WriteDescriptorSet instWrite{};
                    instWrite.setDstSet(m_descriptorSets[f]);
                    instWrite.setDstBinding(1);
                    instWrite.setDstArrayElement(0);
                    instWrite.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                    instWrite.setDescriptorCount(1);
                    instWrite.setPBufferInfo(&instInfo);
                    writes.push_back(instWrite);

                    // Binding 2: Material Data SSBO
                    vk::DescriptorBufferInfo matInfo{};
                    matInfo.setBuffer(m_materialBuffers[f].buffer);
                    matInfo.setOffset(0);
                    matInfo.setRange(m_materialBuffers[f].capacityBytes);

                    vk::WriteDescriptorSet matWrite{};
                    matWrite.setDstSet(m_descriptorSets[f]);
                    matWrite.setDstBinding(2);
                    matWrite.setDstArrayElement(0);
                    matWrite.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                    matWrite.setDescriptorCount(1);
                    matWrite.setPBufferInfo(&matInfo);
                    writes.push_back(matWrite);

                    // Binding 3: Texture Metadata SSBO
                    vk::DescriptorBufferInfo metaInfo{};
                    metaInfo.setBuffer(m_textureMetaBuffers[f].buffer);
                    metaInfo.setOffset(0);
                    metaInfo.setRange(m_textureMetaBuffers[f].capacityBytes);

                    vk::WriteDescriptorSet metaWrite{};
                    metaWrite.setDstSet(m_descriptorSets[f]);
                    metaWrite.setDstBinding(3);
                    metaWrite.setDstArrayElement(0);
                    metaWrite.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                    metaWrite.setDescriptorCount(1);
                    metaWrite.setPBufferInfo(&metaInfo);
                    writes.push_back(metaWrite);

                    // Binding 5: Global Texture Atlas (保底初始化为白纹理)
                    vk::DescriptorImageInfo atlasInfo{};
                    atlasInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                    atlasInfo.setImageView(whiteView);
                    atlasInfo.setSampler(nullptr);

                    vk::WriteDescriptorSet atlasWrite{};
                    atlasWrite.setDstSet(m_descriptorSets[f]);
                    atlasWrite.setDstBinding(5);
                    atlasWrite.setDstArrayElement(0);
                    atlasWrite.setDescriptorType(vk::DescriptorType::eSampledImage);
                    atlasWrite.setDescriptorCount(1);
                    atlasWrite.setPImageInfo(&atlasInfo);
                    writes.push_back(atlasWrite);

                    // Binding 6: Linear Sampler
                    vk::DescriptorImageInfo samplerInfo{};
                    samplerInfo.setSampler(samplerHandle);

                    vk::WriteDescriptorSet samplerWrite{};
                    samplerWrite.setDstSet(m_descriptorSets[f]);
                    samplerWrite.setDstBinding(6);
                    samplerWrite.setDstArrayElement(0);
                    samplerWrite.setDescriptorType(vk::DescriptorType::eSampler);
                    samplerWrite.setDescriptorCount(1);
                    samplerWrite.setPImageInfo(&samplerInfo);
                    writes.push_back(samplerWrite);

                    m_vkDevice.updateDescriptorSets(writes, nullptr);
                }

                // 初始化 Binding 4 中的前 3 个回退槽位
                if (m_whiteFallbackImage) {
                    updateTextureSlot(0, m_whiteFallbackImage);
                }
                if (m_normalFallbackImage) {
                    updateTextureSlot(1, m_normalFallbackImage);
                }
                if (m_checkerFallbackImage) {
                    updateTextureSlot(2, m_checkerFallbackImage);
                }
            }

            void BindlessSet0Manager::updateTextureSlot(uint32_t slotIndex, FCT::Image* image)
            {
                if (slotIndex >= kDefaultMaxBindlessTextures) {
                    assert(false && "BindlessSet0Manager: 纹理槽位索引超出 4096 上限");
                    return;
                }

                if (slotIndex >= m_registeredTextures.size()) {
                    m_registeredTextures.resize(std::max(slotIndex + 1, static_cast<uint32_t>(m_registeredTextures.size() * 2)), nullptr);
                }
                m_registeredTextures[slotIndex] = image;

                // 若在 Tier 0 图集模式下，为新纹理装箱分配 UV 空间
                if (m_tier == DescriptorIndexingTier::Tier0_AtlasFallback && image) {
                    const uint32_t imgW = static_cast<uint32_t>(image->width());
                    const uint32_t imgH = static_cast<uint32_t>(image->height());
                    const auto alloc = m_atlasPacker.allocate(imgW, imgH);
                    if (alloc.success) {
                        for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                            if (slotIndex >= m_textures[f].size()) {
                                m_textures[f].resize(slotIndex + 1);
                            }
                            m_textures[f][slotIndex].uvScaleOffset = m_atlasPacker.calculateUvScaleOffset(alloc);
                            m_textures[f][slotIndex].bindlessOrPageIndex = alloc.layer;
                        }
                    }
                } else {
                    for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                        if (slotIndex >= m_textures[f].size()) {
                            m_textures[f].resize(slotIndex + 1);
                        }
                        m_textures[f][slotIndex].uvScaleOffset = FCT::Vec4(1.0f, 1.0f, 0.0f, 0.0f);
                        m_textures[f][slotIndex].bindlessOrPageIndex = slotIndex;
                    }
                }

                // 解析有效 vk::ImageView；若为 nullptr 则安全回退至 1x1 白纹理
                vk::ImageView targetView = nullptr;
                if (image && image->currentTextureView()) {
                    if (auto* vkView = dynamic_cast<FCT::RHI::VK_TextureView*>(image->currentTextureView())) {
                        targetView = vkView->view();
                    }
                }
                if (!targetView && m_whiteFallbackImage && m_whiteFallbackImage->currentTextureView()) {
                    if (auto* vkView = dynamic_cast<FCT::RHI::VK_TextureView*>(m_whiteFallbackImage->currentTextureView())) {
                        targetView = vkView->view();
                    }
                }

                if (!targetView || !m_vkDevice || m_descriptorSets.empty()) {
                    return;
                }

                vk::DescriptorImageInfo imageInfo{};
                imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                imageInfo.setImageView(targetView);
                imageInfo.setSampler(nullptr);

                // 原地动态更新三缓冲描述符集中 Binding 4 槽位
                std::array<vk::WriteDescriptorSet, kMaxFramesInFlight> writes;
                for (uint32_t f = 0; f < kMaxFramesInFlight; ++f) {
                    writes[f].setDstSet(m_descriptorSets[f]);
                    writes[f].setDstBinding(4);
                    writes[f].setDstArrayElement(slotIndex);
                    writes[f].setDescriptorType(vk::DescriptorType::eSampledImage);
                    writes[f].setDescriptorCount(1);
                    writes[f].setPImageInfo(&imageInfo);
                }

                m_vkDevice.updateDescriptorSets(writes, nullptr);
            }

            void BindlessSet0Manager::updateFrameData(
                uint32_t frameIndex,
                const std::vector<GpuInstanceRow>& instances,
                const std::vector<GpuMaterialRecord>& materials,
                const std::vector<GpuTextureRecord>& textures,
                const GpuSceneUniformData& sceneUniform)
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;
                m_instances[slot] = instances;
                m_materials[slot] = materials;

                if (!textures.empty()) {
                    m_textures[slot] = textures;
                }

                m_sceneUniforms[slot] = sceneUniform;
            }

            void BindlessSet0Manager::uploadFrameData(uint32_t frameIndex)
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;

                // 1. 上传全局 SceneUniform (Binding 0)
                if (m_sceneUniformBuffers[slot].mappedPtr) {
                    memcpy(m_sceneUniformBuffers[slot].mappedPtr, &m_sceneUniforms[slot], sizeof(GpuSceneUniformData));
                }

                // 2. 上传实例表 (Binding 1)
                const auto& instances = m_instances[slot];
                if (!instances.empty() && m_instanceBuffers[slot].mappedPtr) {
                    const size_t bytes = std::min(instances.size() * sizeof(GpuInstanceRow), m_instanceBuffers[slot].capacityBytes);
                    memcpy(m_instanceBuffers[slot].mappedPtr, instances.data(), bytes);
                }

                // 3. 上传材质表 (Binding 2)
                const auto& materials = m_materials[slot];
                if (!materials.empty() && m_materialBuffers[slot].mappedPtr) {
                    const size_t bytes = std::min(materials.size() * sizeof(GpuMaterialRecord), m_materialBuffers[slot].capacityBytes);
                    memcpy(m_materialBuffers[slot].mappedPtr, materials.data(), bytes);
                }

                // 4. 上传纹理元数据表 (Binding 3)
                const auto& textures = m_textures[slot];
                if (!textures.empty() && m_textureMetaBuffers[slot].mappedPtr) {
                    const size_t bytes = std::min(textures.size() * sizeof(GpuTextureRecord), m_textureMetaBuffers[slot].capacityBytes);
                    memcpy(m_textureMetaBuffers[slot].mappedPtr, textures.data(), bytes);
                }
            }

            const std::vector<GpuInstanceRow>& BindlessSet0Manager::getInstances(uint32_t frameIndex) const
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;
                return m_instances[slot];
            }

            const std::vector<GpuMaterialRecord>& BindlessSet0Manager::getMaterials(uint32_t frameIndex) const
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;
                return m_materials[slot];
            }

            const std::vector<GpuTextureRecord>& BindlessSet0Manager::getTextures(uint32_t frameIndex) const
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;
                return m_textures[slot];
            }

            const GpuSceneUniformData& BindlessSet0Manager::getSceneUniform(uint32_t frameIndex) const
            {
                const uint32_t slot = frameIndex % kMaxFramesInFlight;
                return m_sceneUniforms[slot];
            }

            void BindlessSet0Manager::bind(FCT::RHI::CommandBuffer* cmdBuf, uint32_t frameIndex, void* overridePipelineLayout, vk::PipelineBindPoint bindPoint)
            {
                if (!cmdBuf || m_descriptorSets.empty()) {
                    return;
                }
#if defined(FCT_USE_VULKAN)
                auto* vkCmd = dynamic_cast<FCT::RHI::VK_CommandBuffer*>(cmdBuf);
                if (!vkCmd) {
                    return;
                }
                vk::PipelineLayout pipeLayout = overridePipelineLayout
                    ? vk::PipelineLayout(reinterpret_cast<VkPipelineLayout>(overridePipelineLayout))
                    : m_pipelineLayout;
                if (!pipeLayout) {
                    return;
                }
                vk::DescriptorSet vkSet = m_descriptorSets[frameIndex % kMaxFramesInFlight];
                vkCmd->commandBuffer().bindDescriptorSets(
                    bindPoint,
                    pipeLayout,
                    0,
                    1,
                    &vkSet,
                    0,
                    nullptr
                );
#endif
            }

        } // namespace Renderer
    } // namespace Domain
} // namespace MQEngine
