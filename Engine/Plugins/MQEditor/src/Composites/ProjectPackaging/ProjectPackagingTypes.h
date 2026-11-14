/**
 * @file ProjectPackagingTypes.h
 * @brief 工程 4 阶段独立游戏打包流水线目标平台、配置、阶段状态与 .mqpak 归档条目定义
 */
#ifndef MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_TYPES_H
#define MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_TYPES_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 发布目标平台枚举
         */
        enum class PackageTargetPlatform : uint8_t
        {
            WindowsX64Vulkan = 0,
            LinuxX64Vulkan
        };

        /**
         * @brief 4 阶段打包流水线状态枚举
         */
        enum class PackageStage : uint8_t
        {
            Idle = 0,
            Stage1_DependencyWalk,
            Stage2_DllStripping,
            Stage3_AssetCooking,
            Stage4_DistGeneration,
            Completed,
            Failed
        };

        using PackagingStage = PackageStage;

        /**
         * @brief .mqpak 虚拟文件系统归档条目头
         */
        struct PakEntryHeader
        {
            std::string virtualPath;
            uint64_t offsetBytes = 0;
            uint64_t sizeBytes = 0;
            uint32_t crc32 = 0;
        };

        /**
         * @brief 工程打包构建参数配置
         */
        struct PackageBuildConfig
        {
            std::string projectName = "SampleGame";
            std::string targetPlatform = "Windows x64 (Vulkan)";
            PackageTargetPlatform platformEnum = PackageTargetPlatform::WindowsX64Vulkan;
            std::string outputDistDir = "Dist/SampleGame";
            std::string initialScene = "Scenes/Main.mqscene";
            bool packagePakArchive = false;
            bool includeExternalDropSupport = true;
        };

        using PackagingConfig = PackageBuildConfig;

        /**
         * @brief 打包作业运行状态与实时日志遥测
         */
        struct PackageJobState
        {
            std::atomic<bool> isRunning{false};
            std::atomic<float> progressRatio{0.0f};
            PackageStage stage = PackageStage::Idle;
            std::string currentTaskDesc;
            std::vector<std::string> logMessages;
        };

        using PackagingTelemetry = PackageJobState;

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_PROJECT_PACKAGING_TYPES_H
