/**
 * @file GPUScene.h
 * @brief GPUScene 全局纹理数组图集跨材质合批与 2D 数据纹理管理器 (PRD 02 §3, FEAT-13~15)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_DATA_GPUSCENE_H
#define MQENGINE_DOMAIN_RENDERER_DATA_GPUSCENE_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/renderer/data/AtlasShelfPacker.h>
#include <MQEngine/domain/renderer/data/GpuInstance.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <span>
#include <unordered_map>
#include <vector>

namespace MQEngine::Domain::Renderer
{
    /**
     * @brief 1024 宽 2D 数据纹理局部更新行范围描述
     */
    struct DataTextureDirtyRowSpan
    {
        uint32_t startRow{0};
        uint32_t rowCount{0};
    };

    /**
     * @brief GPUScene 跨材质合批与 2D 数据纹理管理器
     */
    class ENGINE_API GPUScene
    {
    public:
        static constexpr uint32_t DataTextureWidth = 1024;       // 1024 宽 GL_RGBA32UI (16KB/行)
        static constexpr uint32_t MaxTextureArrayLayers = 256;   // GL_TEXTURE_2D_ARRAY 最大图层数

        GPUScene();
        ~GPUScene() = default;

        Status init(FCT::Context* ctx);
        void shutdown() noexcept;

        AtlasAllocation registerTextureToAtlas(uint32_t width, uint32_t height);
        FCT::Vec4 getTextureUvScaleOffset(const AtlasAllocation& alloc) const noexcept;

        /**
         * @brief 分配全局 GL_TEXTURE_2D_ARRAY 图层槽位（超 256 层返回 ResourceExhaustedError）
         */
        Status allocateTextureLayer(
            uint32_t layerIndex,
            const FCT::Vec4& uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f});

        /**
         * @brief 查询指定纹理槽位元数据（非法或越界槽位安全回退至 Slot 0 默认白贴图）
         */
        [[nodiscard]] GpuTextureRecord getTextureRecord(uint32_t slot) const noexcept;

        /**
         * @brief 更新指定槽位的纹理记录（用于占位贴图异步下载完成后的原子热替换）
         */
        void updateTextureRecord(uint32_t slot, const GpuTextureRecord& record);

        Status uploadInstanceData(std::span<const GpuInstanceRow> instances, uint32_t startSlot);
        Status uploadMaterialData(std::span<const GpuMaterialRecord> materials, uint32_t startSlot);

        /**
         * @brief 计算 1024 宽 2D 数据纹理的脏行跨度（startSlot > endSlot 返回 InvalidArgumentError）
         */
        static StatusOr<DataTextureDirtyRowSpan> computeDirtyTextureRows(
            uint32_t startSlot,
            uint32_t endSlot,
            uint32_t recordTexelSize = 1) noexcept;

        /**
         * @brief 两级 CPU SIMD128 视锥剔除并生成 PerInstance R32_UINT 实例槽位流
         */
        std::vector<uint32_t> extractVisibleInstanceSlotStream(
            std::span<const GpuInstanceRow> instances,
            const FCT::Mat4& viewProjMatrix);

        static void TexelCoordFromSlot(uint32_t slot, uint32_t recordTexelSize, uint32_t& outX, uint32_t& outY) noexcept;

        [[nodiscard]] AtlasShelfPacker* getAtlasPacker() noexcept { return &m_shelfPacker; }
        [[nodiscard]] FCT::RHI::StorageBuffer* getDataTextureBuffer() const noexcept { return m_dataTextureStorage.get(); }
        [[nodiscard]] DescriptorIndexingTier getDescriptorTier() const noexcept { return m_descriptorTier; }
        void setDescriptorTier(DescriptorIndexingTier tier) noexcept { m_descriptorTier = tier; }

    private:
        void initBuiltinTextureRecords();

        AtlasShelfPacker m_shelfPacker{2048, 2048, 16};
        DescriptorIndexingTier m_descriptorTier{DescriptorIndexingTier::Tier0_AtlasFallback};
        Ptr<FCT::RHI::StorageBuffer> m_dataTextureStorage;
        Ptr<FCT::Image> m_texture2DArray;
        std::vector<uint8_t> m_localDataTextureBytes;
        std::unordered_map<uint32_t, GpuTextureRecord> m_textureRecords;
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_DATA_GPUSCENE_H
