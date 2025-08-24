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
        std::string getModelUuid(const std::string& modelName);
        void loadSelectedModelInfo(const std::filesystem::path& modelDir);
        void importModel(const std::string& modelPath);
        void saveModelIndex(const std::filesystem::path& targetDir, const std::string& modelBaseName,
                            const std::string& originalPath);
        void generateUuidFile(const std::filesystem::path& targetDir, const std::string& originalPath);
        void saveModelTimestamp(const std::filesystem::path& targetDir, const std::string& modelPath);
    private:
        DataManager* m_dataManager;
        FCT::ModelLoader* m_modelLoader;
        std::set<std::string> m_supportedExtensions;
        std::string m_selectedModel;
        FCT::ModelInfo::SceneInfo m_selectedModelInfo;
        FCT::Context* m_ctx;
        FCT::Image* m_meshIcon;
        FCT::Image* m_materialIcon;
        FCT::Image* m_textureIcon;
    private:
        bool m_isWindowHovered = false;
        bool m_isWindowFocused = false;
    };
}



#endif //MODELMANAGER_H
