//
// Created by Administrator on 2025/8/23.
//

#include "FileDataLoader.h"

namespace MQEngine {
    void FileDataLoader::ensureDirectory(const std::string& dir) const
    {
        try {
            std::filesystem::path dirPath(dir);

            if (std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath)) {
                return;
            }

            std::filesystem::create_directories(dirPath);

        } catch (const std::filesystem::filesystem_error& ex) {
            throw DataError("创建目录失败: " + dir + " - " + ex.what());
        }
    }

    std::vector<std::string> FileDataLoader::getSubDirectories(const std::string& dir) const
    {
        std::vector<std::string> subDirectories;

        try {
            std::filesystem::path dirPath(dir);

            if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath)) {
                throw DataError("目录不存在或不是有效目录: " + dir);
            }

            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (entry.is_directory()) {
                    subDirectories.push_back(entry.path().string());
                }
            }

        } catch (const std::filesystem::filesystem_error& ex) {
            throw DataError("读取子目录失败: " + dir + " - " + ex.what());
        }

        return subDirectories;
    }

    void FileDataLoader::createDirectory(const std::string& dir) const
    {
        try {
            std::filesystem::path dirPath(dir);

            if (std::filesystem::exists(dirPath)) {
                if (std::filesystem::is_directory(dirPath)) {
                    throw DataError("目录已存在: " + dir);
                } else {
                    throw DataError("路径已存在但不是目录: " + dir);
                }
            }

            if (!std::filesystem::create_directory(dirPath)) {
                throw DataError("创建目录失败: " + dir);
            }

        } catch (const std::filesystem::filesystem_error& ex) {
            throw DataError("创建目录时发生错误: " + dir + " - " + ex.what());
        }
    }

    bool FileDataLoader::fileExists(const std::string& filePath) const
    {
        return std::filesystem::exists(filePath);
    }

    std::unique_ptr<std::ifstream> FileDataLoader::openBinaryInputStream(const std::string& filePath) const
    {
        auto stream = std::make_unique<std::ifstream>(filePath, std::ios::binary);
        if (!stream->is_open()) {
            return nullptr;
        }
        return stream;
    }
} // MQEngine