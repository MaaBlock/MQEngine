#ifndef MQENGINE_RESOURCE_CORE_DATALOADER_H
#define MQENGINE_RESOURCE_CORE_DATALOADER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    /**
     * @brief 结构化与二进制数据加载抽象接口（无异常，统一使用 Status / StatusOr）
     */
    class ENGINE_API DataLoader
    {
    public:
        virtual ~DataLoader() = default;

        virtual Status ensureDirectory(const std::string& dir) const = 0;
        virtual Status createDirectory(const std::string& dir) const = 0;

        virtual StatusOr<std::vector<std::string>> getSubDirectories(const std::string& dir) const = 0;
        virtual StatusOr<std::vector<std::string>> getSubDirectoriesName(const std::string& dir) const = 0;

        virtual bool fileExists(const std::string& filePath) const = 0;
        virtual bool directoryExists(const std::string& dirPath) const = 0;

        virtual StatusOr<std::vector<uint8_t>> readBinaryFile(const std::string& filePath) const = 0;
        virtual Status writeBinaryFile(const std::string& filePath, const void* data, size_t size) const = 0;

        virtual std::vector<std::string> getFileNamesWithExtension(
            const std::string& dir, const std::string& extension) const = 0;
        virtual std::vector<std::string> getFilePathsWithExtension(
            const std::string& dir, const std::string& extension) const = 0;
    };
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_DATALOADER_H
