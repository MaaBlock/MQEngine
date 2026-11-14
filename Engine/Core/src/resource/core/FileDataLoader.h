#ifndef MQENGINE_RESOURCE_CORE_FILEDATALOADER_H
#define MQENGINE_RESOURCE_CORE_FILEDATALOADER_H

#include <MQEngine/resource/core/DataLoader.h>

namespace MQEngine
{
    /**
     * @brief 基于本地磁盘文件系统的 DataLoader 实现
     */
    class ENGINE_API FileDataLoader : public DataLoader, public CoreEngineManager<FileDataLoader>
    {
    public:
        Status ensureDirectory(const std::string& dir) const override;
        Status createDirectory(const std::string& dir) const override;

        StatusOr<std::vector<std::string>> getSubDirectories(const std::string& dir) const override;
        StatusOr<std::vector<std::string>> getSubDirectoriesName(const std::string& dir) const override;

        bool fileExists(const std::string& filePath) const override;
        bool directoryExists(const std::string& dirPath) const override;

        StatusOr<std::vector<uint8_t>> readBinaryFile(const std::string& filePath) const override;
        Status writeBinaryFile(const std::string& filePath, const void* data, size_t size) const override;

        std::vector<std::string> getFileNamesWithExtension(
            const std::string& dir, const std::string& extension) const override;
        std::vector<std::string> getFilePathsWithExtension(
            const std::string& dir, const std::string& extension) const override;
    };
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_FILEDATALOADER_H
