#ifndef MQENGINE_RESOURCE_CORE_REPOSITORY_H
#define MQENGINE_RESOURCE_CORE_REPOSITORY_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/resource/config/Config.h>
#include <MQEngine/resource/config/CommandLineParser.h>
#include <MQEngine/resource/core/MemoryTracker.h>
#include <MQEngine/resource/core/Arena.h>
#include <MQEngine/resource/core/BlockPool.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/resource/table/TableRegistry.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/FileDataLoader.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief Resource 模块顶层门面类：
         *        统管 CPU 内存管理（MemoryTracker / Arena / BlockPool）、GPU 显存管理（GpuMemoryManager）、
         *        内存数据库（TableRegistry / EcsTable / GpuResourceTable / TypeMetaTable / SchemaTable）、
         *        配置与命令行解析（config）、文件资源与数据读写加载器以及可扩展纹理格式解码器注册表（TextureDecoderRegistry）
         */
        class ENGINE_API Resource : public CoreEngineModule<Resource>
        {
        public:
            Resource();
            ~Resource();

            Resource(const Resource&) = delete;
            Resource& operator=(const Resource&) = delete;

            void registerInfo();
            Status init();
            void term();

            Config* getConfig() const noexcept { return const_cast<Config*>(&config); }
            const std::string& getStartupFilePath() const noexcept { return config.getStartupFilePath(); }
            const std::string& getEngineDir() const noexcept { return config.getEngineDir(); }
            const std::string& getProjectDir() const noexcept { return config.getProjectDir(); }
            std::string getEnginePluginsDir() const { return config.getEnginePluginsDir(); }
            std::string getProjectPluginsDir() const { return config.getProjectPluginsDir(); }
            std::string getProjectNativeDir() const { return config.getProjectNativeDir(); }
            std::string getProjectAssetsDir() const { return config.getProjectAssetsDir(); }
            std::string getProjectScenesDir() const { return config.getProjectScenesDir(); }
            ResourceLoader* getResourceLoader() const noexcept { return const_cast<DiskResourceLoader*>(&resourceLoader); }
            FileDataLoader* getDataLoader() const noexcept { return const_cast<FileDataLoader*>(&dataLoader); }
            CommandLineParser* getCommandLineParser() const noexcept { return const_cast<CommandLineParser*>(&commandLineParser); }
            TableRegistry* getTableRegistry() const noexcept { return const_cast<TableRegistry*>(&tableRegistry); }
            DataBase& getDataBase() noexcept { return tableRegistry.dataBase(); }
            const DataBase& getDataBase() const noexcept { return tableRegistry.dataBase(); }
            TextureTable& getTextureTable() noexcept { return tableRegistry.textureTable(); }
            MeshTable& getMeshTable() noexcept { return tableRegistry.meshTable(); }
            MaterialTable& getMaterialTable() noexcept { return tableRegistry.materialTable(); }
            PipelineTable& getPipelineTable() noexcept { return tableRegistry.pipelineTable(); }
            ShaderTable& getShaderTable() noexcept { return tableRegistry.shaderTable(); }
            MemoryTracker* getMemoryTracker() const noexcept { return const_cast<MemoryTracker*>(&memoryTracker); }
            Arena* getArena() const noexcept { return const_cast<Arena*>(&arena); }
            BlockPoolManager* getBlockPoolManager() const noexcept { return const_cast<BlockPoolManager*>(&blockPoolManager); }
            GpuMemoryManager* getGpuMemoryManager() const noexcept { return const_cast<GpuMemoryManager*>(&gpuMemoryManager); }
            TextureDecoderRegistry* getTextureDecoderRegistry() const noexcept { return const_cast<TextureDecoderRegistry*>(&textureDecoderRegistry); }

            void registerTextureDecoder(UniquePtr<ITextureFormatDecoder> decoder, int priority = 10)
            {
                textureDecoderRegistry.registerDecoder(std::move(decoder), priority);
            }

            void registerTextureFormatDecoder(
                const std::string& extension,
                TextureDecoderRegistry::FormatDecoderFn decoderFn,
                int priority = 20)
            {
                textureDecoderRegistry.registerFormatDecoder(extension, std::move(decoderFn), priority);
            }

            bool isTextureExtensionSupported(const std::string& extension) const
            {
                return textureDecoderRegistry.isExtensionSupported(extension);
            }

            std::vector<std::string> getSupportedTextureExtensions() const
            {
                return textureDecoderRegistry.getSupportedExtensions();
            }

            StatusOr<DecodedTextureData> decodeTextureFromMemory(
                const uint8_t* data,
                size_t size,
                const std::string& filePath = "")
            {
                return textureDecoderRegistry.decodeFromMemory(data, size, filePath);
            }

            StatusOr<DecodedTextureData> decodeTextureFromFile(const std::string& filePath)
            {
                return textureDecoderRegistry.decodeFromFile(filePath);
            }

            void setCommandLineArgs(int argc, char* argv[]) { commandLineParser.setArgs(argc, argv); }
            void parseCommandLine(int argc, char* argv[]) { commandLineParser.parse(argc, argv); }
            void parseCommandLine() { commandLineParser.parse(); }

            void registerCommandLineHandler(UniquePtr<ICommandLineHandler> handler)
            {
                commandLineParser.addHandler(std::move(handler));
            }

            void registerCommandLineHandler(ICommandLineHandler* handler)
            {
                commandLineParser.addExternalHandler(handler);
            }

            void registerCommandLineHandler(CommandLineParser::HandleCallback callback)
            {
                commandLineParser.registerHandler(std::move(callback));
            }

            const std::string& getAssetRoot() const noexcept { return m_assetRoot; }
            void setAssetRoot(const std::string& assetRoot) { m_assetRoot = assetRoot; }

        public:
            MemoryTracker memoryTracker;
            Arena arena;
            BlockPoolManager blockPoolManager;
            GpuMemoryManager gpuMemoryManager;
            Config config;
            DiskResourceLoader resourceLoader;
            FileDataLoader dataLoader;
            CommandLineParser commandLineParser;
            TableRegistry tableRegistry;
            TextureDecoderRegistry textureDecoderRegistry;

        private:
            std::string m_assetRoot = "./Assets";
            bool m_initialized = false;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_REPOSITORY_H
