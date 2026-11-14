/**
 * @file NtcBakerTypes.h
 * @brief 神经纹理压缩 (NTC) 烘焙模式、质量预设、通道描述、指标遥测与文件头定义
 */
#ifndef MQENGINE_MQ_EDITOR_NTC_BAKER_TYPES_H
#define MQENGINE_MQ_EDITOR_NTC_BAKER_TYPES_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 纹理压缩烘焙编码模式枚举
         */
        enum class NtcBakeMode : uint8_t
        {
            NeuralMlpLatent = 0,
            Bc7BlockCompressed
        };

        /**
         * @brief 烘焙质量预设枚举
         */
        enum class NtcBakeQuality : uint8_t
        {
            Fast = 0,
            Balanced,
            High,
            Ultra
        };

        using NtcQualityPreset = NtcBakeQuality;

        /**
         * @brief 输入贴图通道配置项
         */
        struct NtcChannelItem
        {
            std::string channelName;
            std::string sourcePath;
            uint32_t channelCount = 3;
        };

        /**
         * @brief NTC 离线烘焙任务参数配置
         */
        struct NtcBakeConfig
        {
            std::string albedoPath = "Assets/Textures/Rock_Albedo.png";
            std::string normalPath = "Assets/Textures/Rock_Normal.png";
            std::string ormPath = "Assets/Textures/Rock_ORM.png";
            std::string outputPath = "Assets/Materials/Rock.ntc";

            uint32_t resolution = 1024;
            NtcBakeMode mode = NtcBakeMode::NeuralMlpLatent;
            NtcBakeQuality quality = NtcBakeQuality::Balanced;
            uint32_t mlpLayers = 4;
            uint32_t hiddenDims = 64;
            uint32_t epochs = 500;
            float targetPsnr = 38.5f;
        };

        /**
         * @brief NTC 烘焙进度与质量评估指标 (Loss / PSNR / SSIM)
         */
        struct NtcBakeMetrics
        {
            std::atomic<bool> isBaking{false};
            std::atomic<float> progressRatio{0.0f};
            std::atomic<uint32_t> currentEpoch{0};
            std::atomic<float> currentLoss{0.0f};
            std::atomic<float> currentPsnr{0.0f};
            std::atomic<float> currentSsim{0.0f};
            std::string statusText;
        };

        using NtcBakeProgress = NtcBakeMetrics;

        /**
         * @brief .ntc 离线神经纹理包文件头结构
         */
        struct NtcFileHeader
        {
            char magic[4] = {'N', 'T', 'C', 0x02};
            uint32_t version = 2;
            uint32_t resolution = 1024;
            uint32_t channels = 4;
            uint32_t blockSize = 8;
            uint32_t gridWidth = 128;
            uint32_t gridHeight = 128;
            uint32_t latentDims = 4;
            uint32_t mlpLayers = 3;
            uint32_t hiddenDims = 64;
            float finalPsnr = 0.0f;
            float finalMse = 0.0f;
            uint32_t latentBytes = 0;
            uint32_t weightBytes = 0;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_NTC_BAKER_TYPES_H
