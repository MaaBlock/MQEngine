#include <MQEngine/resource/core/DiskResourceLoader.h>

namespace MQEngine {

DiskResourceFile::DiskResourceFile(std::string path)
    : m_path(std::move(path)) {}

std::string DiskResourceFile::extension() const {
    return SAI::Path::extension(m_path);
}

std::string DiskResourceFile::stem() const {
    return SAI::Path::stem(m_path);
}

std::string DiskResourceFile::path() const {
    return m_path;
}

StatusOr<std::string> DiskResourceFile::readContent() {
    return SAI::FileSystem::readTextFile(m_path);
}

Status DiskResourceLoader::accessDir(const std::string& path) {
    if (!SAI::FileSystem::exists(path)) {
        return NotFoundError(StrCat("Directory not found: ", path));
    }
    if (!SAI::FileSystem::isDirectory(path)) {
        return InvalidArgumentError(StrCat("Path is not a directory: ", path));
    }
    return OkStatus();
}

Status DiskResourceLoader::makeDir(const std::string& path) {
    return SAI::FileSystem::createDirectories(path);
}

Status DiskResourceLoader::renameFile(const ResourceFile* file, const std::string& newName) {
    const auto* diskFile = dynamic_cast<const DiskResourceFile*>(file);
    if (!diskFile) {
        return InvalidArgumentError("Invalid file type for DiskResourceLoader");
    }

    const std::string& oldPath = diskFile->getPath();
    std::string newPath = SAI::Path::join(SAI::Path::parentPath(oldPath), newName);
    return SAI::FileSystem::rename(oldPath, newPath);
}

Status DiskResourceLoader::removeFile(const ResourceFile* file) {
    const auto* diskFile = dynamic_cast<const DiskResourceFile*>(file);
    if (!diskFile) {
        return InvalidArgumentError("Invalid file type for DiskResourceLoader");
    }

    return SAI::FileSystem::removeFile(diskFile->getPath());
}

Status DiskResourceLoader::saveFile(const std::string& path, const std::string& content) {
    return SAI::FileSystem::writeTextFile(path, content, true);
}

StatusOr<std::string> DiskResourceLoader::loadFile(const std::string& path) {
    DiskResourceFile file(path);
    return file.readContent();
}

Status DiskResourceLoader::unlink(const std::string& path) {
    return SAI::FileSystem::removeFile(path);
}

Status DiskResourceLoader::copyFile(const std::string& srcPath, const std::string& dstPath, bool overwrite) {
    return SAI::FileSystem::copyFile(srcPath, dstPath, overwrite, true);
}

int64_t DiskResourceLoader::getLastWriteTimeSeconds(const std::string& path) const {
    return SAI::FileSystem::getLastWriteTimeSeconds(path);
}

bool DiskResourceLoader::exists(const std::string& path) const {
    return SAI::FileSystem::exists(path);
}

StatusOr<std::vector<UniquePtr<ResourceFile>>> DiskResourceLoader::readDir(const std::string& path) {
    CHECK_STATUS(accessDir(path));

    std::vector<UniquePtr<ResourceFile>> files;
    for (const auto& entry : SAI::FileSystem::listEntries(path)) {
        files.push_back(FCT::makeUnique<DiskResourceFile>(entry.path));
    }
    return files;
}

} // namespace MQEngine