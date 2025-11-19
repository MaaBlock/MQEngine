#include "Scene.h"
#include "EnttArchiveWrapper.h"
#include <boost/archive/binary_oarchive.hpp>

#include "DataManager.h"
#include "SavedComponentsList.h"
#include "../manager/RegistriesManager.h"
namespace MQEngine
{

    Scene::Scene(DataManager* dataManager, const std::string& uuid)
    {
        m_dataManager = dataManager;
        m_uuid = uuid;
    }

    void Scene::init()
    {
        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());
        m_loadedSceneTrunks["default"] = std::make_unique<SceneTrunk>("default",this);
        auto trunk = m_loadedSceneTrunks["default"].get();
        trunk->init();
    }

    void Scene::saveTrunk(const std::string& trunkName)
    {
        if (!m_dataManager) {
            throw DataError("DataManager为空，无法保存场景块");
        }

        auto it = m_loadedSceneTrunks.find(trunkName);
        if (it == m_loadedSceneTrunks.end()) {
            throw DataError("场景块 '" + trunkName + "' 不存在，无法保存");
        }

        auto trunk = it->second.get();
        trunk->save();
    }

    void Scene::loadTrunk(const std::string& trunkName)
    {
        if (!m_dataManager) {
            throw DataError("DataManager为空，无法加载场景块");
        }

        try {
            if (m_loadedSceneTrunks.find(trunkName) != m_loadedSceneTrunks.end()) {
                return;
            }

            updateTrunkList();

            auto it = std::find(m_sceneTrunk.begin(), m_sceneTrunk.end(), trunkName);
            if (it == m_sceneTrunk.end()) {
                throw DataError("场景块 '" + trunkName + "' 不存在于trunk列表中，无法加载");
            }

            auto trunk = std::make_unique<SceneTrunk>(trunkName, this);

            trunk->load();

            m_loadedSceneTrunks[trunkName] = std::move(trunk);

            if (m_dataManager->getCurrentSceneUuid() == m_uuid) {
                m_dataManager->appendRegistry(m_loadedSceneTrunks[trunkName]->getRegistry());
            }

        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("加载场景块 '" + trunkName + "' 失败: " + std::string(e.what()));
        }
    }


    void Scene::save()
    {
        if (!m_dataManager) {
            throw DataError("DataManager为空，无法保存场景");
        }

        try {
            std::string sceneName = getName();
            std::string scenePath = "./res/scenes/" + sceneName;
            std::string sceneDataPath = scenePath + "/scene.dat";
            std::string registryDataPath = scenePath + "/scene.reg";

            m_dataManager->getDataLoader()->ensureDirectory(scenePath);

            auto sceneStream = m_dataManager->getDataLoader()->openBinaryOutputStream(sceneDataPath);
            if (sceneStream && sceneStream->is_open()) {
                boost::archive::binary_oarchive archive(*sceneStream);
                archive << *this;
            } else {
                throw DataError("无法创建场景配置文件: " + sceneDataPath);
            }

            auto registryStream = m_dataManager->getDataLoader()->openBinaryOutputStream(registryDataPath);
            if (registryStream && registryStream->is_open()) {
                boost::archive::binary_oarchive archive(*registryStream);
                EnttOutputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot{*m_registry}, wrapper);
            } else {
                throw DataError("无法创建registry文件: " + registryDataPath);
            }

            for (const auto& pair : m_loadedSceneTrunks) {
                saveTrunk(pair.first);
            }

        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("保存场景失败: " + std::string(e.what()));
        }
    }


    void Scene::load()
    {
        if (!m_dataManager) {
            throw DataError("DataManager为空，无法加载场景");
        }

        try {
            std::string sceneName = getName();
            std::string scenePath = "./res/scenes/" + sceneName;
            std::string sceneDataPath = scenePath + "/scene.dat";
            std::string registryDataPath = scenePath + "/scene.reg";

            if (m_dataManager->getDataLoader()->fileExists(sceneDataPath)) {
                auto sceneStream = m_dataManager->getDataLoader()->openBinaryInputStream(sceneDataPath);
                if (sceneStream && sceneStream->is_open()) {
                    boost::archive::binary_iarchive archive(*sceneStream);

                    archive >> *this;

                }
            } else {
                m_isChunked = false;
                m_sceneTrunk.clear();
                m_sceneTrunk.push_back("default");
            }

            // Use RegistriesManager to create registry
            m_registry.reset(g_engineGlobal.registriesManager->createRegistry());

            if (m_dataManager->getDataLoader()->fileExists(registryDataPath)) {
                auto registryStream = m_dataManager->getDataLoader()->openBinaryInputStream(registryDataPath);
                if (registryStream && registryStream->is_open()) {
                    m_registry->clear();
                    boost::archive::binary_iarchive archive(*registryStream);
                    EnttInputArchiveWrapper wrapper(archive);
                    SerializeComponents(entt::snapshot_loader{*m_registry}, wrapper);
                    g_engineGlobal.registriesManager->storage(m_registry.get());
                }
            }

            m_loadedSceneTrunks.clear();
            if (!m_isChunked) {
                if (m_loadedSceneTrunks.find("default") == m_loadedSceneTrunks.end()) {
                    loadTrunk("default");
                }
            }

        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("加载场景失败: " + std::string(e.what()));
        }
    }

    std::string Scene::getName()
    {
        m_dataManager->loadScenePathList();
        return m_dataManager->getSceneNameByUuid(m_uuid);
    }

    void Scene::updateTrunkList()
    {
        if (!m_dataManager) {
            throw DataError("DataManager为空，无法更新场景块列表");
        }

        try {
            std::string sceneName = getName();
            std::string scenePath = "./res/scenes/" + sceneName;
            std::string trunksPath = scenePath + "/trunks";

            m_sceneTrunk.clear();

            if (m_dataManager->getDataLoader()->directoryExists(trunksPath)) {
                auto trunkNames = m_dataManager->getDataLoader()->getFileNamesWithExtension(trunksPath, ".dat");
                /**
                 *todo: 使用=
                 **/
                for (const auto& trunkName : trunkNames) {
                    m_sceneTrunk.push_back(trunkName);
                }
            }
        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("更新场景块列表失败: " + std::string(e.what()));
        }
    }
}

