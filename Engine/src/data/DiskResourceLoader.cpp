#include "DiskResourceLoader.h"
#include <fstream>
#include <sstream>

namespace MQEngine {

DiskResourceFile::DiskResourceFile(const std::filesystem::path& path)
    : m_path(path) {}

std::string DiskResourceFile::extension() const {
    return m_path.extension().string();
}

std::string DiskResourceFile::stem() const {
    return m_path.stem().string();
}

std::string DiskResourceFile::path() const {
    return m_path.string();
}

StatusOr<std::string> DiskResourceFile::readContent() {
    std::ifstream file(m_path, std::ios::in | std::ios::binary);
    if (!file) {
        return NotFoundError(StrCat("Failed to open file: ", m_path.string()));
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    if (file.fail() && !file.eof()) {
        return InternalError(StrCat("Failed to read file content: ", m_path.string()));
    }
    return ss.str();
}

Status DiskResourceLoader::accessDir(const std::string& path) {
    std::filesystem::path p(path);
    if (!std::filesystem::exists(p)) {
        return NotFoundError(StrCat("Directory not found: ", path));
    }
    if (!std::filesystem::is_directory(p)) {
        return InvalidArgumentError(StrCat("Path is not a directory: ", path));
    }
    return OkStatus();
}

Status DiskResourceLoader::makeDir(const std::string& path) {
    std::filesystem::path p(path);
    try {
        if (std::filesystem::create_directories(p)) {
             return OkStatus();
        } else {
            if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                return OkStatus();
            }
             return InternalError(StrCat("Failed to create directory: ", path));
        }
    } catch (const std::exception& e) {
        return InternalError(StrCat("Exception creating directory: ", e.what()));
    }
}

Status DiskResourceLoader::renameFile(const ResourceFile* file, const std::string& newName) {
    const auto* diskFile = dynamic_cast<const DiskResourceFile*>(file);
    if (!diskFile) {
        return InvalidArgumentError("Invalid file type for DiskResourceLoader");
    }
    
    std::filesystem::path oldPath = diskFile->getPath();
    std::filesystem::path newPath = oldPath.parent_path() / newName;

    try {
        std::filesystem::rename(oldPath, newPath);
        return OkStatus();
    } catch (const std::exception& e) {
        return InternalError(StrCat("Failed to rename file: ", e.what()));
    }
}

Status DiskResourceLoader::removeFile(const ResourceFile* file) {
    const auto* diskFile = dynamic_cast<const DiskResourceFile*>(file);
    if (!diskFile) {
        return InvalidArgumentError("Invalid file type for DiskResourceLoader");
    }

    try {
        if (std::filesystem::remove(diskFile->getPath())) {
            return OkStatus();
        } else {
            return NotFoundError("File not found or could not be removed");
        }
    } catch (const std::exception& e) {
        return InternalError(StrCat("Failed to remove file: ", e.what()));
    }
}

Status DiskResourceLoader::saveFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        return InternalError(StrCat("Failed to open file for writing: ", path));
    }
    file << content;
    if (file.fail()) {
        return InternalError(StrCat("Failed to write content to file: ", path));
    }
    return OkStatus();
}

StatusOr<std::string> DiskResourceLoader::loadFile(const std::string& path) {
    DiskResourceFile file(path);
    return file.readContent();
}

Status DiskResourceLoader::unlink(const std::string& path) {
    try {
        if (std::filesystem::remove(path)) {
            return OkStatus();
        } else {
            if (!std::filesystem::exists(path)) {
                return NotFoundError(StrCat("File not found: ", path));
            }
            return InternalError(StrCat("Failed to remove file (locked or permission issue): ", path));
        }
    } catch (const std::exception& e) {
        return InternalError(StrCat("Exception removing file: ", e.what()));
    }
}

StatusOr<std::vector<UniquePtr<ResourceFile>>> DiskResourceLoader::readDir(const std::string& path) {
    std::vector<UniquePtr<ResourceFile>> files;
    std::filesystem::path root(path);

    Status accessStatus = accessDir(path);
    if (!accessStatus.ok()) {
        return accessStatus;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            files.push_back(FCT::makeUnique<DiskResourceFile>(entry.path()));
        }
    } catch (const std::exception& e) {
        files.clear();
        return InternalError(StrCat("Failed to read directory: ", e.what()));
    }

    return files;
}

} // namespace MQEngine