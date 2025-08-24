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

    std::vector<std::string> FileDataLoader::getSubDirectoriesName(const std::string& dir) const
    {
        std::vector<std::string> subDirectories;

        try {
            std::filesystem::path dirPath(dir);

            if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath)) {
                throw DataError("目录不存在或不是有效目录: " + dir);
            }

            for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
                if (entry.is_directory()) {
                    subDirectories.push_back(entry.path().filename().string());
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


    std::unique_ptr<std::ofstream> FileDataLoader::openBinaryOutputStream(const std::string& filePath) const
    {
        try {
            std::filesystem::path path(filePath);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }

            auto stream = std::make_unique<std::ofstream>(filePath, std::ios::binary | std::ios::out);

            if (!stream->is_open()) {
                throw DataError("无法打开文件进行写入: " + filePath);
            }

            return stream;

        } catch (const std::filesystem::filesystem_error& e) {
            throw DataError("文件系统错误: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw DataError("打开输出流失败: " + std::string(e.what()));
        }
    }

    std::vector<std::string> FileDataLoader::getFileNamesWithExtension(const std::string& dir, const std::string& extension) const
    {
        std::vector<std::string> fileNames;

        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            return fileNames;
        }

        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string fileExtension = entry.path().extension().string();

                    // 比较扩展名（不区分大小写）
                    if (fileExtension.size() == extension.size()) {
                        bool match = true;
                        for (size_t i = 0; i < extension.size(); ++i) {
                            if (std::tolower(fileExtension[i]) != std::tolower(extension[i])) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            // 获取文件名（不含扩展名）
                            std::string fileName = entry.path().stem().string();
                            fileNames.push_back(fileName);
                        }
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            // 可以选择抛出异常或记录日志
            // throw DataError("获取文件名列表失败: " + std::string(e.what()));
        }

        return fileNames;
    }

    bool FileDataLoader::directoryExists(const std::string& string)
    {
        return std::filesystem::exists(string) && std::filesystem::is_directory(string);
    }
} // MQEngine