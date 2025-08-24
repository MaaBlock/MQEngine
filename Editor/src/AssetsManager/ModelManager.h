//
// Created by Administrator on 2025/8/23.
//

#ifndef MODELMANAGER_H
#define MODELMANAGER_H
#include "../Thirdparty/thirdparty.h"
#include "../core/Global.h"
namespace MQEngine {
    class ModelManager {
    public:
        ModelManager(DataManager* dataManager);
        ~ModelManager();
        void render();
        void loadSelectedModelInfo(const std::filesystem::path& modelDir);
        void importModel(const std::string& modelPath);
        void saveModelIndex(const std::filesystem::path& targetDir, const std::string& modelBaseName,
                            const std::string& originalPath);
        void saveModelTimestamp(const std::filesystem::path& targetDir, const std::string& modelPath);

    private:
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;
        std::set<std::string> m_supportedExtensions;
        std::string m_selectedModel;
        FCT::ModelInfo::SceneInfo m_selectedModelInfo;
    private:
        bool m_isWindowHovered = false;
        bool m_isWindowFocused = false;
    };
}



#endif //MODELMANAGER_H
