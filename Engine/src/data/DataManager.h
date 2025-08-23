//
// Created by Administrator on 2025/8/20.
//

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
#include "DataError.h"
#include "Scene.h"
#include "SceneTrunk.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include "./DataLoader.h"

namespace MQEngine {

    class ENGINE_API DataManager {
    public:
        void loadRes()
        {
            m_dataLoader->ensureDirectory("./res");
            loadSceneList();
            loadModelList();
        }
        void loadSceneList()
        {
            m_dataLoader->ensureDirectory("./res/scenes");
            m_sceneList = m_dataLoader->getSubDirectories("./res/scenes");
        }
        void newScene(const std::string& sceneName)
        {
            m_dataLoader->createDirectory("./res/scenes/" + sceneName);
        }
        void loadModelList()
        {
            m_dataLoader->ensureDirectory("./res/models");
            m_modelList = m_dataLoader->getSubDirectories("./res/models");
        }
    private:
        std::vector<std::string> m_sceneList;
        std::vector<std::string> m_modelList;
        std::unordered_map<std::string, std::shared_ptr<Scene>> m_loadScenes;
        std::unique_ptr<DataLoader> m_dataLoader;
    };
    
} // namespace MQEngine

#endif //DATAMANAGER_H
