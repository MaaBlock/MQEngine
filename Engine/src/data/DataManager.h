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
#include "./FileDataLoader.h"
#include "./Camera.h"

namespace MQEngine {

    class ENGINE_API DataManager {
    public:
        DataManager()
        {
            m_dataLoader = std::make_unique<FileDataLoader>();
        }
        std::vector<std::string> getModelList()
        {
            loadModelList();
            return m_modelList;
        }
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
        std::vector<entt::registry*> currentRegistries() const
        {
            return m_currentRegistries;
        }
        void appendRegistry(entt::registry* registry)
        {
            m_currentRegistries.push_back(registry);
        }
        void removeRegistry(entt::registry* registry)
        {
            m_currentRegistries.erase(std::remove(m_currentRegistries.begin(), m_currentRegistries.end(), registry), m_currentRegistries.end());
        }
    private:
        std::vector<entt::registry*> m_currentRegistries;
        std::vector<std::string> m_sceneList;
        std::vector<std::string> m_modelList;
        std::unordered_map<std::string, std::shared_ptr<Scene>> m_loadScenes;
        std::unique_ptr<DataLoader> m_dataLoader;
    };
    
} // namespace MQEngine

#endif //DATAMANAGER_H
