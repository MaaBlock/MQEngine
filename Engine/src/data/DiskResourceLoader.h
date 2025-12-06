/**
 * @file DiskResourceLoader.h
 * @brief Disk-based resource loader implementation
 */
#ifndef DISK_RESOURCE_LOADER_H
#define DISK_RESOURCE_LOADER_H

#include "ResourceLoader.h"
#include <filesystem>
#include <string>

namespace MQEngine {

class ENGINE_API DiskResourceFile : public ResourceFile {
public:
    explicit DiskResourceFile(const std::filesystem::path& path);
    virtual ~DiskResourceFile() = default;

    const std::filesystem::path& getPath() const { return m_path; }
    std::string extension() const override;
    std::string stem() const override;
    std::string path() const override;
    StatusOr<std::string> readContent() override;

private:
    std::filesystem::path m_path;
};

class ENGINE_API DiskResourceLoader : public ResourceLoader {
public:
    DiskResourceLoader() = default;
    virtual ~DiskResourceLoader() = default;

    StatusOr<std::vector<UniquePtr<ResourceFile>>> readDir(const std::string& path) override;
    Status accessDir(const std::string& path) override;
    Status makeDir(const std::string& path) override;
    Status renameFile(const ResourceFile* file, const std::string& newName) override;
    Status removeFile(const ResourceFile* file) override;
    Status saveFile(const std::string& path, const std::string& content) override;
    StatusOr<std::string> loadFile(const std::string& path) override;
    Status unlink(const std::string& path) override;
};

} // namespace MQEngine

#endif // DISK_RESOURCE_LOADER_H