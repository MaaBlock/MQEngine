/**
 * @file classdef.h
 * @brief Asset 模块前向类型声明与基础结构定义
 */
#ifndef MQENGINE_ASSET_CLASSDEF_H
#define MQENGINE_ASSET_CLASSDEF_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace MQEngine
{
    namespace Runtime
    {
        class Engine;
        class Application;
    } // namespace Runtime

    namespace Host
    {
        class WorkHost;
        class RenderHost;
    } // namespace Host

    namespace Asset
    {
        class Asset;
        class Composite;
        class CompositeManager;
        class AsyncLoader;
        class AsyncTextureUploader;
        class PluginPackage;

        /**
         * @brief 单个独立 Composite DLL 的清单描述（用于在 LoadLibrary 前按 tags 过滤、拓扑排序与职责边界审计）
         */
        struct CompositeManifest
        {
            std::string name;
            std::string pluginName;
            std::string pluginPath;
            std::string dllPath;
            std::string entrySymbolName;
            std::string description;
            std::vector<std::string> tags;
            std::vector<std::string> dependencies;
            std::vector<std::string> responsibilities;
            std::vector<std::string> dockWindows;
            std::vector<std::string> nonGoals;
        };

        /**
         * @brief 插件包元信息（纯目录分组与元信息描述，每个 Composite 独立为一个 DLL）
         */
        struct PluginMetadata
        {
            std::string name;
            std::string version = "1.0.0";
            std::string author = "MQEngine";
            std::string description;
            std::string signature;
            std::string license = "MIT";
            std::string path;
            std::vector<std::string> dependencies;
            std::vector<std::string> tags;
            std::vector<std::string> composites;
            std::vector<CompositeManifest> compositeManifests;
            bool isBuiltin = false;
        };

        /**
         * @brief 异步纹理上传请求参数
         */
        struct TextureUploadDesc
        {
            std::string filePath;
            std::vector<uint8_t> memoryData;
            bool generateMips = false;
            bool sRGB = true;
            uint32_t priority = 0;
            std::string debugName;
        };

        /**
         * @brief 异步纹理上传结果
         */
        struct TextureUploadResult
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t channels = 0;
            uint32_t mipLevels = 1;
            void* textureHandle = nullptr;
            size_t totalBytes = 0;
            std::string debugName;
        };

        /**
         * @brief 异步大文件分块数据
         */
        struct StreamChunk
        {
            uint64_t offset = 0;
            std::vector<uint8_t> data;
            bool isLastChunk = false;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_CLASSDEF_H
