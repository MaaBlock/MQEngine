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
    };
} // MQEngine

#endif //FILEDATALOADER_H
