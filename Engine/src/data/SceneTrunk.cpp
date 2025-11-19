#include "SceneTrunk.h"

#include "DataManager.h"
#include "EnttArchiveWrapper.h"
#include "SavedComponentsList.h"
#include "Scene.h"
#include "../manager/RegistriesManager.h"

namespace MQEngine {
    SceneTrunk::SceneTrunk(std::string name, Scene* scene)
    {
        m_name = name;
        m_scene = scene;
    }

    void SceneTrunk::init()
    {
        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());
    }

    Status SceneTrunk::save()
    {
        if (!m_scene || !m_scene->getDataManager())
            return InternalError("Scene或DataManager为空，场景保存失败");

        try {
            std::string sceneName = m_scene->getName();
            std::string scenePath = "./res/scenes/" + sceneName;
            std::string trunksPath = scenePath + "/trunks";
            std::string trunkDataPath = trunksPath + "/" + m_name + ".dat";
            std::string trunkRegistryPath = trunksPath + "/" + m_name + ".reg";

            auto dataManager = m_scene->getDataManager();
            dataManager->getDataLoader()->ensureDirectory(trunksPath);

            auto trunkStream = dataManager->getDataLoader()->openBinaryOutputStream(trunkDataPath);
            if (trunkStream && trunkStream->is_open()) {
                boost::archive::binary_oarchive archive(*trunkStream);
                archive << *this;
            } else {
                throw DataError("无法创建场景块配置文件: " + trunkDataPath);
            }

            auto registryStream = dataManager->getDataLoader()->openBinaryOutputStream(trunkRegistryPath);
            if (registryStream && registryStream->is_open()) {
                boost::archive::binary_oarchive archive(*registryStream);
                EnttOutputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot{*m_registry}, wrapper);

            } else {
                throw DataError("无法创建场景块registry文件: " + trunkRegistryPath);
            }

        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("保存场景块 '" + m_name + "' 失败: " + std::string(e.what()));
        }
        return OkStatus();
    }

    void SceneTrunk::load()
    {
        if (!m_scene || !m_scene->getDataManager()) {
            throw DataError("Scene或DataManager为空，无法加载场景块");
        }

        try {
            std::string sceneName = m_scene->getName();
            std::string scenePath = "./res/scenes/" + sceneName;
            std::string trunksPath = scenePath + "/trunks";
            std::string trunkDataPath = trunksPath + "/" + m_name + ".dat";
            std::string trunkRegistryPath = trunksPath + "/" + m_name + ".reg";

            auto dataManager = m_scene->getDataManager();

            m_registry.reset(g_engineGlobal.registriesManager->createRegistry());

            if (dataManager->getDataLoader()->fileExists(trunkDataPath)) {
                auto trunkStream = dataManager->getDataLoader()->openBinaryInputStream(trunkDataPath);
                if (trunkStream && trunkStream->is_open()) {
                    boost::archive::binary_iarchive archive(*trunkStream);
                    archive >> *this;
                }
            }

            if (dataManager->getDataLoader()->fileExists(trunkRegistryPath)) {
                auto registryStream = dataManager->getDataLoader()->openBinaryInputStream(trunkRegistryPath);
                if (registryStream && registryStream->is_open()) {
                    m_registry->clear();
                    boost::archive::binary_iarchive archive(*registryStream);
                    EnttInputArchiveWrapper wrapper(archive);
                    SerializeComponents(entt::snapshot_loader{*m_registry}, wrapper);
                    g_engineGlobal.registriesManager->storage(m_registry.get());
                }
            }

        } catch (const DataError& e) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("加载场景块 '" + m_name + "' 失败: " + std::string(e.what()));
        }
    }
}
