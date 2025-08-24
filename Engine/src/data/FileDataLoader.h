#ifndef FILEDATALOADER_H
#define FILEDATALOADER_H
#include "DataLoader.h"

namespace MQEngine {
    class ENGINE_API FileDataLoader : public DataLoader
    {
    public:
        void ensureDirectory(const std::string& dir) const override;
        std::vector<std::string> getSubDirectories(const std::string& dir) const override;
        void createDirectory(const std::string& dir) const override;
        bool fileExists(const std::string& filePath) const override;
        std::unique_ptr<std::ifstream> openBinaryInputStream(const std::string& filePath) const override;
    };
} // MQEngine

#endif //FILEDATALOADER_H
