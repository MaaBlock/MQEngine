//
// Created by Administrator on 2025/8/23.
//

#include "FileDataLoader.h"

namespace MQEngine {
    void FileDataLoader::ensureDirectory(const std::string& dir) const
    {
        Status st = SAI::FileSystem::ensureDirectory(dir);
        if (!st.ok()) {
            throw DataError(std::string(st.message()));
        }
    }

    std::vector<std::string> FileDataLoader::getSubDirectories(const std::string& dir) const
    {
        if (!SAI::FileSystem::isDirectory(dir)) {
            throw DataError("目录不存在或不是有效目录: " + dir);
        }
        return SAI::FileSystem::listSubDirectories(dir);
    }

    std::vector<std::string> FileDataLoader::getSubDirectoriesName(const std::string& dir) const
    {
        if (!SAI::FileSystem::isDirectory(dir)) {
            throw DataError("目录不存在或不是有效目录: " + dir);
        }
        return SAI::FileSystem::listSubDirectoryNames(dir);
    }

    void FileDataLoader::createDirectory(const std::string& dir) const
    {
        if (SAI::FileSystem::exists(dir)) {
            if (SAI::FileSystem::isDirectory(dir)) {
                throw DataError("目录已存在: " + dir);
            }
            throw DataError("路径已存在但不是目录: " + dir);
        }
        Status st = SAI::FileSystem::createDirectory(dir);
        if (!st.ok()) {
            throw DataError(std::string(st.message()));
        }
    }

    bool FileDataLoader::fileExists(const std::string& filePath) const
    {
        return SAI::FileSystem::exists(filePath);
    }

    std::unique_ptr<std::ifstream> FileDataLoader::openBinaryInputStream(const std::string& filePath) const
    {
        auto streamRes = SAI::FileSystem::openBinaryInputStream(filePath);
        if (!streamRes.ok()) {
            return nullptr;
        }
        return std::move(streamRes.value());
    }

    std::unique_ptr<std::ofstream> FileDataLoader::openBinaryOutputStream(const std::string& filePath) const
    {
        auto streamRes = SAI::FileSystem::openBinaryOutputStream(filePath, true);
        if (!streamRes.ok()) {
            throw DataError(std::string(streamRes.status().message()));
        }
        return std::move(streamRes.value());
    }

    std::vector<std::string> FileDataLoader::getFileNamesWithExtension(const std::string& dir, const std::string& extension) const
    {
        return SAI::FileSystem::listFileStems(dir, extension);
    }

    std::vector<std::string> FileDataLoader::getFilePathsWithExtension(const std::string& dir, const std::string& extension) const
    {
        return SAI::FileSystem::listFiles(dir, extension);
    }

    bool FileDataLoader::directoryExists(const std::string& string)
    {
        return SAI::FileSystem::isDirectory(string);
    }
} // MQEngine