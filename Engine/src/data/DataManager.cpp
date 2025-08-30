#include "DataManager.h"

#include "../core/EngineGlobal.h"

namespace MQEngine
{
    void DataManager::loadRes()
    {
        m_dataLoader->ensureDirectory("./res");
        loadScenePathList();
        updateModelPathList();
    }

    void DataManager::loadScenePathList()
    {
        m_dataLoader->ensureDirectory("./res/scenes");
        m_scenePathList = m_dataLoader->getSubDirectories("./res/scenes");
        loadSceneUuidMapping();
    }

    void DataManager::newScene(const std::string& sceneName)
    {
        m_dataLoader->createDirectory("./res/scenes/" + sceneName);
    }

    void DataManager::updateModelPathList()
    {
        m_dataLoader->ensureDirectory("./res/models");
        m_modelPathList = m_dataLoader->getSubDirectories("./res/models");
        loadModelUuidMapping();
    }

    std::vector<entt::registry*> DataManager::currentRegistries() const
    {
        return m_currentRegistries;
    }

    void DataManager::appendRegistry(entt::registry* registry)
    {
        m_currentRegistries.push_back(registry);
    }

    void DataManager::removeRegistry(entt::registry* registry)
    {
        m_currentRegistries.erase(std::remove(m_currentRegistries.begin(), m_currentRegistries.end(), registry), m_currentRegistries.end());
    }

    void DataManager::openScene(const std::string& uuid)
    {
        try {
            if (m_currentScene == uuid) {
                return;
            }

            auto sceneIt = m_uuidToScenePath.find(uuid);
            if (sceneIt == m_uuidToScenePath.end()) {
                throw DataError("场景UUID不存在: " + uuid);
            }

            if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
                loadScene(uuid);
            }

            if (!m_currentScene.empty()) {
                closeScene(m_currentScene);
            }

            m_currentScene = uuid;

            auto scenePtr = m_loadScenes[uuid];
            if (scenePtr) {
                appendRegistry(&scenePtr->getRegistry());

                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            appendRegistry(&trunk->getRegistry());
                        }
                    }
                }
            }
            g_engineGlobal.sceneEventPipe.trigger<SceneEvent::EntryScene>(
            {
                .uuid = uuid,
                .scene = scenePtr.get()
            });
        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("打开场景失败: " + std::string(e.what()));
        }
    }

    void DataManager::closeScene(const std::string& uuid)
    {
        if (m_loadScenes.find(uuid) != m_loadScenes.end()) {
            auto scenePtr = m_loadScenes[uuid];
            if (scenePtr) {
                // 从当前实体表列表中移除场景的主registry
                removeRegistry(&scenePtr->getRegistry());
                
                // 从当前实体表列表中移除所有已加载的trunk的registry
                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            removeRegistry(&trunk->getRegistry());
                        }
                    }
                }
            }
        }
        
        g_engineGlobal.sceneEventPipe.trigger<SceneEvent::ExitScene>(
            {
                .uuid = uuid,
                .scene = m_loadScenes[uuid].get()
            });
    }

    bool DataManager::saveScene(const std::string& uuid)
    {
        if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
            return true;
        }
        m_loadScenes[uuid]->save();
        return true;
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

    std::string DataManager::getModelPathByUuid(const std::string& uuid) const
    {
        auto it = m_uuidToModel.find(uuid);
        if (it != m_uuidToModel.end()) {
            return it->second;
        }
        return "";
    }

    std::string DataManager::getModelRelativePathByUuid(const std::string& uuid) const
    {
        std::string modelPath = getModelPathByUuid(uuid);
        if (modelPath.empty()) {
            return "";
        }
        
        std::string modelUuidFilePath = modelPath + "/model.uuid";
        std::ifstream file(modelUuidFilePath);
        if (!file.is_open()) {
            return "";
        }
        
        try {
            boost::archive::binary_iarchive ia(file);
            ModelUuidFile modelUuidFile;
            ia >> modelUuidFile;
            return modelUuidFile.modelRelativePath;
        } catch (const std::exception& e) {
            FCT::fout << "读取模型UUID文件失败: " << e.what() << std::endl;
            return "";
        }
    }
}
