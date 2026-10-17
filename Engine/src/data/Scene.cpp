#include "Scene.h"
#include "EnttArchiveWrapper.h"
#include "DataManager.h"
#include "ResourceLoader.h"
#include "SavedComponentsList.h"
#include "../core/EngineGlobal.h"
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
        m_loadedSceneTrunks["default"] = std::make_unique<SceneTrunk>("default", this);
        auto trunk = m_loadedSceneTrunks["default"].get();
        trunk->init();
    }

    std::string Scene::buildScenePath()
    {
        return StrCat(m_dataManager->getAssetRoot(), "/scenes/", getName());
    }

    Status Scene::saveTrunk(const std::string& trunkName)
    {
        if (!m_dataManager) {
            return InternalError("DataManager为空，无法保存场景块");
        }

        auto it = m_loadedSceneTrunks.find(trunkName);
        if (it == m_loadedSceneTrunks.end()) {
            return NotFoundError(StrCat("场景块 '", trunkName, "' 不存在，无法保存"));
        }

        auto trunk = it->second.get();
        CHECK_STATUS(trunk->save());
        return OkStatus();
    }

    Status Scene::loadTrunk(const std::string& trunkName)
    {
        if (!m_dataManager) {
            return InternalError("DataManager为空，无法加载场景块");
        }

        if (m_loadedSceneTrunks.find(trunkName) != m_loadedSceneTrunks.end()) {
            return OkStatus();
        }

        CHECK_STATUS(updateTrunkList());

        auto it = std::find(m_sceneTrunk.begin(), m_sceneTrunk.end(), trunkName);
        if (it == m_sceneTrunk.end()) {
            return NotFoundError(StrCat("场景块 '", trunkName, "' 不存在于trunk列表中，无法加载"));
        }

        auto trunk = std::make_unique<SceneTrunk>(trunkName, this);
        CHECK_STATUS(trunk->load());

        m_loadedSceneTrunks[trunkName] = std::move(trunk);

        if (m_dataManager->getCurrentSceneUuid() == m_uuid) {
            m_dataManager->appendRegistry(m_loadedSceneTrunks[trunkName]->getRegistry());
        }

        return OkStatus();
    }

    Status Scene::saveSceneConfig(const std::string& sceneDataPath)
    {
        try {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive archive(ss);
                archive << *this;
            }
            CHECK_STATUS(g_engineGlobal.resourceLoader->saveFile(sceneDataPath, ss.str()));
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("保存场景配置失败: ", e.what()));
        }
    }

    Status Scene::saveSceneRegistry(const std::string& registryDataPath)
    {
        if (!m_registry) {
            return InternalError("场景Registry为空，无法保存");
        }
        try {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive archive(ss);
                EnttOutputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot{*m_registry}, wrapper);
            }
            CHECK_STATUS(g_engineGlobal.resourceLoader->saveFile(registryDataPath, ss.str()));
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("保存场景Registry失败: ", e.what()));
        }
    }

    Status Scene::saveLoadedTrunks()
    {
        for (const auto& pair : m_loadedSceneTrunks) {
            CHECK_STATUS(saveTrunk(pair.first));
        }
        return OkStatus();
    }

    Status Scene::save()
    {
        if (!m_dataManager) {
            return InternalError("DataManager为空，无法保存场景");
        }
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader为空，无法保存场景");
        }

        std::string scenePath = buildScenePath();
        CHECK_STATUS(g_engineGlobal.resourceLoader->ensureDir(scenePath));
        CHECK_STATUS(saveSceneConfig(StrCat(scenePath, "/scene.dat")));
        CHECK_STATUS(saveSceneRegistry(StrCat(scenePath, "/scene.reg")));
        CHECK_STATUS(saveLoadedTrunks());
        return OkStatus();
    }

    Status Scene::loadSceneConfig(const std::string& sceneDataPath)
    {
        auto contentOr = g_engineGlobal.resourceLoader->loadFile(sceneDataPath);
        if (IsNotFound(contentOr.status())) {
            m_isChunked = false;
            m_sceneTrunk.clear();
            m_sceneTrunk.push_back("default");
            return OkStatus();
        }
        CHECK_STATUS(contentOr);

        try {
            std::stringstream ss(contentOr.value(), std::ios::in | std::ios::binary);
            boost::archive::binary_iarchive archive(ss);
            archive >> *this;
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("加载场景配置失败: ", e.what()));
        }
    }

    Status Scene::loadSceneRegistry(const std::string& registryDataPath)
    {
        auto contentOr = g_engineGlobal.resourceLoader->loadFile(registryDataPath);
        if (IsNotFound(contentOr.status())) {
            return OkStatus();
        }
        CHECK_STATUS(contentOr);

        try {
            m_registry->clear();
            std::stringstream ss(contentOr.value(), std::ios::in | std::ios::binary);
            boost::archive::binary_iarchive archive(ss);
            EnttInputArchiveWrapper wrapper(archive);
            SerializeComponents(entt::snapshot_loader{*m_registry}, wrapper);
            g_engineGlobal.registriesManager->storage(m_registry.get());
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("加载场景Registry失败: ", e.what()));
        }
    }

    Status Scene::loadDefaultTrunks()
    {
        m_loadedSceneTrunks.clear();
        if (!m_isChunked) {
            if (m_loadedSceneTrunks.find("default") == m_loadedSceneTrunks.end()) {
                CHECK_STATUS(loadTrunk("default"));
            }
        }
        return OkStatus();
    }

    Status Scene::load()
    {
        if (!m_dataManager) {
            return InternalError("DataManager为空，无法加载场景");
        }
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader为空，无法加载场景");
        }

        std::string scenePath = buildScenePath();
        CHECK_STATUS(loadSceneConfig(StrCat(scenePath, "/scene.dat")));

        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());
        CHECK_STATUS(loadSceneRegistry(StrCat(scenePath, "/scene.reg")));
        CHECK_STATUS(loadDefaultTrunks());
        return OkStatus();
    }

    std::string Scene::getName()
    {
        m_dataManager->loadScenePathList();
        return m_dataManager->getSceneNameByUuid(m_uuid);
    }

    Status Scene::updateTrunkList()
    {
        if (!m_dataManager) {
            return InternalError("DataManager为空，无法更新场景块列表");
        }
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader为空，无法更新场景块列表");
        }

        std::string trunksPath = StrCat(buildScenePath(), "/trunks");
        m_sceneTrunk.clear();

        Status accessSt = g_engineGlobal.resourceLoader->accessDir(trunksPath);
        if (IsNotFound(accessSt)) {
            return OkStatus();
        }
        CHECK_STATUS(accessSt);

        auto filesOr = g_engineGlobal.resourceLoader->readDir(trunksPath);
        CHECK_STATUS(filesOr);

        for (const auto& file : filesOr.value()) {
            if (file->extension() == ".dat") {
                m_sceneTrunk.push_back(file->stem());
            }
        }
        return OkStatus();
    }
}


