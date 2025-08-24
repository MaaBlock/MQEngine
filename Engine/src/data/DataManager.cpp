#include "DataManager.h"

namespace MQEngine
{
    bool DataManager::saveScene(const std::string& uuid)
    {
        if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
            return true;
        }
        m_loadScenes[uuid]->save();
    }

    std::string DataManager::getCurrentSceneUuid() const
    {
        return m_currentScene;
    }

    std::string DataManager::getSceneNameByUuid(const std::string& uuid) const
    {

        if (m_uuidToSceneName.count(uuid))
        {
            return m_uuidToSceneName.at(uuid);
        }
        return "";
    }

    DataLoader* DataManager::getDataLoader() const
    {
        return m_dataLoader.get();
    }

    Scene* DataManager::getCurrentScene() const
    {
        if (m_currentScene.empty()) {
            return nullptr;
        }
        return m_loadScenes.at(m_currentScene).get();
    }
}
