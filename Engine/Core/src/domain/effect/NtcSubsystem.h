/**
 * @file NtcSubsystem.h
 * @brief 神经纹理压缩 (Neural Texture Compression, NTC) 运行时子系统与材质包规范 (Docs 02 & Docs 05)
 */
#ifndef MQENGINE_DOMAIN_EFFECT_NTCSUBSYSTEM_H
#define MQENGINE_DOMAIN_EFFECT_NTCSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace MQEngine
{
    namespace Domain
    {
        namespace Effect
        {
            /**
             * @brief NTC 二进制文件头规范 (64字节定长对齐头)
             */
            #pragma pack(push, 1)
            struct NtcHeader
            {
                char magic[4] = {'N', 'T', 'C', 0x02};
                uint32_t version = 2;
                uint32_t resolution = 1024;
                uint32_t channels = 4;        // RGBA 通道数
                uint32_t blockSize = 8;        // 潜特征分块尺寸 (8x8)
                uint32_t gridWidth = 128;      // 分块网格宽 (resolution / blockSize)
                uint32_t gridHeight = 128;     // 分块网格高 (resolution / blockSize)
                uint32_t latentDims = 4;       // 每个分块的潜特征维度
                uint32_t mlpLayers = 3;        // 解码 MLP 层数
                uint32_t hiddenDims = 32;      // MLP 隐藏层宽度
                float finalPsnr = 0.0f;        // 拟合峰值信噪比
                float finalMse = 0.0f;         // 拟合均方误差
                uint32_t latentBytes = 0;      // 潜特征网格二进制大小
                uint32_t weightBytes = 0;      // MLP 权重二进制大小
            };
            #pragma pack(pop)

            /**
             * @brief 神经纹理压缩 (NTC) 材质包核心数据结构
             */
            struct ENGINE_API NtcMaterialPack
            {
                NtcHeader header;
                std::vector<float> latentGrid; // gridWidth * gridHeight * latentDims
                std::vector<float> mlpWeights; // 平铺的 MLP 权重
                std::vector<float> mlpBiases;  // 平铺的 MLP 偏置
                std::string assetPath;
                bool isLoaded = false;
            };

            /**
             * @brief NTC 运行时子系统：统一管理神经材质包注册、潜特征网格采样与 MLP 解码
             */
            class ENGINE_API NtcSubsystem
            {
            public:
                NtcSubsystem();
                ~NtcSubsystem();

                NtcSubsystem(const NtcSubsystem&) = delete;
                NtcSubsystem& operator=(const NtcSubsystem&) = delete;

                void registerPack(const std::string& path, UniquePtr<NtcMaterialPack> pack);
                void unregisterPack(const std::string& path);
                const NtcMaterialPack* getPack(const std::string& path) const noexcept;
                bool hasPack(const std::string& path) const noexcept;

                /**
                 * @brief 实时神经解码单个 UV 点的 PBR 通道值
                 */
                void decodePixel(const NtcMaterialPack& pack, float u, float v, float* outChannels) const;

                /**
                 * @brief 解码指定块的全部像素
                 */
                void decodeBlock(const NtcMaterialPack& pack, uint32_t blockX, uint32_t blockY, float* outBlockPixels) const;

                size_t getRegisteredPackCount() const noexcept { return m_packs.size(); }
                void clear() noexcept { m_packs.clear(); }

            private:
                std::unordered_map<std::string, UniquePtr<NtcMaterialPack>> m_packs;
            };

        } // namespace Effect
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_EFFECT_NTCSUBSYSTEM_H
