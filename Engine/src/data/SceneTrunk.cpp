#include "SceneTrunk.h"

#include "DataManager.h"
#include "EnttArchiveWrapper.h"
#include "ResourceLoader.h"
#include "SavedComponentsList.h"
#include "Scene.h"
#include "../core/EngineGlobal.h"
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

    std::string SceneTrunk::buildTrunksPath() const
    {
        std::string sceneName = m_scene->getName();
        return StrCat(m_scene->getDataManager()->getAssetRoot(), "/scenes/", sceneName, "/trunks");
    }

    Status SceneTrunk::saveTrunkConfig(const std::string& trunkDataPath)
    {
        try {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive archive(ss);
                archive << *this;
            }
            CHECK_STATUS(g_engineGlobal.resourceLoader->saveFile(trunkDataPath, ss.str()));
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("保存场景块 '", m_name, "' 配置失败: ", e.what()));
        }
    }

    Status SceneTrunk::saveTrunkRegistry(const std::string& trunkRegistryPath)
    {
        if (!m_registry) {
            return InternalError(StrCat("场景块 '", m_name, "' Registry为空，无法保存"));
        }
        try {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive archive(ss);
                EnttOutputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot{*m_registry}, wrapper);
            }
            CHECK_STATUS(g_engineGlobal.resourceLoader->saveFile(trunkRegistryPath, ss.str()));
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("保存场景块 '", m_name, "' Registry失败: ", e.what()));
        }
    }

    Status SceneTrunk::save()
    {
        if (!m_scene || !m_scene->getDataManager()) {
            return InternalError("Scene或DataManager为空，场景块保存失败");
        }
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader为空，场景块保存失败");
        }

        std::string trunksPath = buildTrunksPath();
        CHECK_STATUS(g_engineGlobal.resourceLoader->ensureDir(trunksPath));
        CHECK_STATUS(saveTrunkConfig(StrCat(trunksPath, "/", m_name, ".dat")));
        CHECK_STATUS(saveTrunkRegistry(StrCat(trunksPath, "/", m_name, ".reg")));
        return OkStatus();
    }

    Status SceneTrunk::loadTrunkConfig(const std::string& trunkDataPath)
    {
        auto contentOr = g_engineGlobal.resourceLoader->loadFile(trunkDataPath);
        if (IsNotFound(contentOr.status())) {
            return OkStatus();
        }
        CHECK_STATUS(contentOr);

        try {
            std::stringstream ss(contentOr.value(), std::ios::in | std::ios::binary);
            boost::archive::binary_iarchive archive(ss);
            archive >> *this;
            return OkStatus();
        } catch (const std::exception& e) {
            return InternalError(StrCat("加载场景块 '", m_name, "' 配置失败: ", e.what()));
        }
    }

    Status SceneTrunk::loadTrunkRegistry(const std::string& trunkRegistryPath)
    {
        auto contentOr = g_engineGlobal.resourceLoader->loadFile(trunkRegistryPath);
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
            return InternalError(StrCat("加载场景块 '", m_name, "' Registry失败: ", e.what()));
        }
    }

    Status SceneTrunk::load()
    {
        if (!m_scene || !m_scene->getDataManager()) {
            return InternalError("Scene或DataManager为空，无法加载场景块");
        }
        if (!g_engineGlobal.resourceLoader) {
            return InternalError("ResourceLoader为空，无法加载场景块");
        }

        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());

        std::string trunksPath = buildTrunksPath();
        CHECK_STATUS(loadTrunkConfig(StrCat(trunksPath, "/", m_name, ".dat")));
        CHECK_STATUS(loadTrunkRegistry(StrCat(trunksPath, "/", m_name, ".reg")));
        return OkStatus();
    }
}

