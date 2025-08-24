//
// Created by Administrator on 2025/8/24.
//

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H
#include "../Thirdparty/thirdparty.h"
namespace MQEngine {

    class SceneManager {
    public:
        SceneManager();
        std::string getSceneUuid(const std::string& sceneName);
        void newScene(const std::string& sceneName);
        void openScene(const std::string& sceneName);
        void render();
        void refreshSceneList();
        void renderCreateSceneDialog();
        void renderDeleteSceneDialog();
    private:
        DataManager* m_dataManager;
        std::vector<std::string> m_sceneList;
        int m_selectedSceneIndex = -1;

        bool m_showCreateDialog = false;
        bool m_showDeleteDialog = false;
        int m_deleteSceneIndex = -1;

        std::string m_errorMessage;

    };

} // MQEngine

#endif //SCENEMANAGER_H
