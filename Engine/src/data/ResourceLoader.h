/**
 * @file ResourceLoader.h
 * @brief Abstract base classes for resource loading
 */
#ifndef RESOURCE_LOADER_H
#define RESOURCE_LOADER_H

#include "../thirdparty/thirdparty.h"
#include "../EnginePCH.h"
#include <vector>
#include <string>

namespace MQEngine {

class ENGINE_API ResourceFile {
public:
    virtual ~ResourceFile() = default;
    
    /**
     * @brief Get the file extension
     * @return The file extension as a string (including the dot, e.g., ".png")
     */
    virtual std::string extension() const = 0;

    /**
     * @brief Get the file name without extension
     * @return The file name as a string
     */
    virtual std::string stem() const = 0;

    /**
     * @brief Get the full path string of the file
     * @return The full path as a string
     */
    virtual std::string path() const = 0;

    /**
     * @brief Read the entire content of the file
     * @return The file content as a string or an error status
     */
    virtual StatusOr<std::string> readContent() = 0;
};

class ENGINE_API ResourceLoader {
public:
    virtual ~ResourceLoader() = default;
    
    /**
     * @brief Reads the directory and returns a list of ResourceFiles.
     * @param path The path to the directory to read.
     * @return StatusOr<std::vector<UniquePtr<ResourceFile>>> List of resource files or error status.
     */
    virtual StatusOr<std::vector<UniquePtr<ResourceFile>>> readDir(const std::string& path) = 0;

    /**
     * @brief Checks if a directory is accessible and exists.
     * @param path The path to the directory to check.
     * @return Status indicating success or a specific error (e.g., NotFoundError, InvalidArgumentError).
     */
    virtual Status accessDir(const std::string& path) = 0;

    /**
     * @brief Creates a directory at the specified path.
     * @param path The path to the directory to create.
     * @return Status indicating success or failure.
     */
    virtual Status makeDir(const std::string& path) = 0;

    /**
     * @brief Ensures that a directory exists at the specified path.
     */
    virtual Status ensureDir(const std::string& path) {
        Status status = accessDir(path);
        if (status.ok()) {
            return status;
        }
        if (status.code() == absl::StatusCode::kNotFound) {
            return makeDir(path);
        }
        return status;
    }

    /**
     * @brief Renames a resource file.
     * @param file The file to rename.
     * @param newName The new name (including extension, or relative path).
     * @return Status
     */
    virtual Status renameFile(const ResourceFile* file, const std::string& newName) = 0;

    /**
     * @brief Removes a resource file.
     * @param file The file to remove.
     * @return Status
     */
    virtual Status removeFile(const ResourceFile* file) = 0;

    /**
     * @brief Saves content to a file.
     * @param path The path to the file.
     * @param content The content to write.
     * @return Status
     */
    virtual Status saveFile(const std::string& path, const std::string& content) = 0;
    
    /**
     * @brief Reads a single file from the given path.
     * @param path The path to the file.
     * @return Content string or error status.
     */
    virtual StatusOr<std::string> loadFile(const std::string& path) = 0;

    /**
     * @brief Deletes a name and the file it refers to (Node.js style unlink).
     * @param path The path to the file to remove.
     * @return Status
     */
    virtual Status unlink(const std::string& path) = 0;
};

} // namespace MQEngine

#endif // RESOURCE_LOADER_H