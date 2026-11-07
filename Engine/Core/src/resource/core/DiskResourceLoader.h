/**
 * @file DiskResourceLoader.h
 * @brief Disk-based resource loader implementation
 */
#ifndef DISK_RESOURCE_LOADER_H
#define DISK_RESOURCE_LOADER_H

#include "ResourceLoader.h"
#include "../../base/CoreEngineManager.h"
#include <string>

namespace MQEngine {

class ENGINE_API DiskResourceFile : public ResourceFile {
public:
    explicit DiskResourceFile(std::string path);
    virtual ~DiskResourceFile() = default;

    const std::string& getPath() const { return m_path; }
    std::string extension() const override;
    std::string stem() const override;
    std::string path() const override;
    StatusOr<std::string> readContent() override;

private:
    std::string m_path;
};

class ENGINE_API DiskResourceLoader : public ResourceLoader, public CoreEngineManager<DiskResourceLoader> {
public:
    DiskResourceLoader() = default;
    virtual ~DiskResourceLoader() = default;

    void registerInfo() {}

    StatusOr<std::vector<UniquePtr<ResourceFile>>> readDir(const std::string& path) override;
    Status accessDir(const std::string& path) override;
    Status makeDir(const std::string& path) override;
    Status renameFile(const ResourceFile* file, const std::string& newName) override;
    Status removeFile(const ResourceFile* file) override;
    Status saveFile(const std::string& path, const std::string& content) override;
    StatusOr<std::string> loadFile(const std::string& path) override;
    Status unlink(const std::string& path) override;
    Status copyFile(const std::string& srcPath, const std::string& dstPath, bool overwrite = true) override;
    int64_t getLastWriteTimeSeconds(const std::string& path) const override;
    bool exists(const std::string& path) const override;
};

} // namespace MQEngine

#endif // DISK_RESOURCE_LOADER_H