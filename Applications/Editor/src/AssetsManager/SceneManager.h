//
// Created by Administrator on 2025/8/24.
//

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H
#include "../thirdparty/thirdparty.h"
#include "../imgui/ContentBrowser.h"

namespace MQEngine {

    class SceneManager : public IContentProvider {
    public:
        SceneManager();
        
        // IContentProvider implementation
        void init() override;
        void render() override;
        void term() override {}
        std::string getName() const override { return "Scenes"; }

        std::string getSceneUuid(const std::string& sceneName);
        void newScene(const std::string& sceneName);
        void openScene(const std::string& sceneName);
        void refreshSceneList();
        
    private:
        void renderCreateSceneDialog();
        void renderDeleteSceneDialog();

        DataManager* m_dataManager;
        std::vector<std::string> m_sceneList;
        int m_selectedSceneIndex = -1;

        bool m_showCreateDialog = false;
        bool m_showDeleteDialog = false;
        int m_deleteSceneIndex = -1;

        std::string m_errorMessage;

        std::chrono::steady_clock::time_point m_lastSceneOpenTime;

    };

} // MQEngine

#endif //SCENEMANAGER_H
