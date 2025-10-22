#ifndef DATALOADER_H
#define DATALOADER_H
#include <filesystem>

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "./DataError.h"
namespace MQEngine
{
    class ENGINE_API DataLoader {
    public:
        virtual ~DataLoader() = default;
        virtual void ensureDirectory(const std::string& dir) const = 0;
        virtual std::vector<std::string> getSubDirectories(const std::string& dir) const = 0;
        virtual std::vector<std::string> getSubDirectoriesName(const std::string& dir) const = 0;
        virtual void createDirectory(const std::string& dir) const = 0;
        virtual bool fileExists(const std::string& filePath) const = 0 ;
        virtual std::unique_ptr<std::ifstream> openBinaryInputStream(const std::string& filePath) const = 0;
        virtual std::unique_ptr<std::ofstream> openBinaryOutputStream(const std::string& filePath) const = 0;
        virtual bool directoryExists(const std::string& string) = 0;
        virtual std::vector<std::string> getFileNamesWithExtension(const std::string& dir, const std::string& extension) const = 0;
        virtual std::vector<std::string> getFilePathsWithExtension(const std::string& dir, const std::string& extension) const = 0;

    };
} // MQEngine

#endif //DATALOADER_H
