#include <MQEngine/resource/core/FileDataLoader.h>

namespace MQEngine
{
    Status FileDataLoader::ensureDirectory(const std::string& dir) const
    {
        return SAI::FileSystem::ensureDirectory(dir);
    }

    Status FileDataLoader::createDirectory(const std::string& dir) const
    {
        if (SAI::FileSystem::exists(dir)) {
            if (SAI::FileSystem::isDirectory(dir)) {
                return AlreadyExistsError(StrCat("目录已存在: ", dir));
            }
            return InvalidArgumentError(StrCat("路径已存在但不是目录: ", dir));
        }
        return SAI::FileSystem::createDirectory(dir);
    }

    StatusOr<std::vector<std::string>> FileDataLoader::getSubDirectories(const std::string& dir) const
    {
        if (!SAI::FileSystem::isDirectory(dir)) {
            return NotFoundError(StrCat("目录不存在或不是有效目录: ", dir));
        }
        return SAI::FileSystem::listSubDirectories(dir);
    }

    StatusOr<std::vector<std::string>> FileDataLoader::getSubDirectoriesName(const std::string& dir) const
    {
        if (!SAI::FileSystem::isDirectory(dir)) {
            return NotFoundError(StrCat("目录不存在或不是有效目录: ", dir));
        }
        return SAI::FileSystem::listSubDirectoryNames(dir);
    }

    bool FileDataLoader::fileExists(const std::string& filePath) const
    {
        return SAI::FileSystem::exists(filePath);
    }

    bool FileDataLoader::directoryExists(const std::string& dirPath) const
    {
        return SAI::FileSystem::isDirectory(dirPath);
    }

    StatusOr<std::vector<uint8_t>> FileDataLoader::readBinaryFile(const std::string& filePath) const
    {
        return SAI::FileSystem::readBinaryFile(filePath);
    }

    Status FileDataLoader::writeBinaryFile(const std::string& filePath, const void* data, size_t size) const
    {
        return SAI::FileSystem::writeBinaryFile(filePath, data, size, true);
    }

    std::vector<std::string> FileDataLoader::getFileNamesWithExtension(
        const std::string& dir, const std::string& extension) const
    {
        return SAI::FileSystem::listFileStems(dir, extension);
    }

    std::vector<std::string> FileDataLoader::getFilePathsWithExtension(
        const std::string& dir, const std::string& extension) const
    {
        return SAI::FileSystem::listFiles(dir, extension);
    }
} // namespace MQEngine
