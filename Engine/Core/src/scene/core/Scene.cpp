#include "Scene.h"
#include "../data/EnttArchiveWrapper.h"
#include "../manager/DataManager.h"
#include "../../resource/core/ResourceLoader.h"
#include "../data/Component.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../manager/RegistriesManager.h"
#include "../system/SceneSystem.h"

namespace MQEngine
{
namespace Scene
{
    Scene::Scene(DataManager* dataManager, const std::string& uuid)
    {
        m_dataManager = dataManager;
        m_uuid = uuid;
    }

    void Scene::init()
    {
        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());
        m_loadedSceneTrunks["default"] = makeUnique<SceneTrunk>("default", this);
        auto trunk = m_loadedSceneTrunks["default"].get();
        trunk->init();
        (void)updateTrunkList();
    }

    std::string Scene::buildScenePath()
    {
        return StrCat(m_dataManager->getAssetRoot(), "/Scenes/", getName());
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

        auto trunk = makeUnique<SceneTrunk>(trunkName, this);
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
            entt::registry snapshotRegistry;
            CopyPersistentRegistryForSnapshot(*m_registry, snapshotRegistry);
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            {
                boost::archive::binary_oarchive archive(ss);
                EnttOutputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot{snapshotRegistry}, wrapper);
                SceneSystem::saveRegistryExtBinary(*m_registry, archive);
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

        m_dataManager->notifySelfSceneSave();
        std::string scenePath = buildScenePath();
        CHECK_STATUS(g_engineGlobal.resourceLoader->ensureDir(scenePath));
        CHECK_STATUS(saveSceneConfig(StrCat(scenePath, "/scene.dat")));
        CHECK_STATUS(saveSceneRegistry(StrCat(scenePath, "/scene.reg")));
        CHECK_STATUS(saveLoadedTrunks());
        (void)updateTrunkList();
        m_dataManager->notifySelfSceneSave();
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
        m_registry->clear();
        auto contentOr = g_engineGlobal.resourceLoader->loadFile(registryDataPath);
        if (contentOr.ok()) {
            const std::string& rawBytes = contentOr.value();
            const bool hasExtSection = SceneSystem::hasExtBinarySection(rawBytes);
            try {
                constexpr char kExtSig[8] = {
                    static_cast<char>(0x58), static_cast<char>(0x45),
                    static_cast<char>(0x51), static_cast<char>(0x4D),
                    static_cast<char>(0x01), static_cast<char>(0x00),
                    static_cast<char>(0x00), static_cast<char>(0x00)
                };
                size_t extOffset = std::string_view(rawBytes).find(std::string_view(kExtSig, sizeof(kExtSig)));
                std::string snapshotBytes = (extOffset != std::string::npos) ? rawBytes.substr(0, extOffset) : rawBytes;
                std::stringstream ss(snapshotBytes, std::ios::in | std::ios::binary);
                boost::archive::binary_iarchive archive(ss);
                EnttInputArchiveWrapper wrapper(archive);
                SerializeComponents(entt::snapshot_loader{*m_registry}, wrapper);
                if (hasExtSection) {
                    (void)SceneSystem::recoverRegistryFromExtSection(*m_registry, rawBytes);
                }
            } catch (const std::exception& e) {
                m_registry->clear();
                if (!SceneSystem::recoverRegistryFromExtSection(*m_registry, rawBytes)) {
                    m_registry->clear();
                    return InternalError(StrCat("加载场景Registry失败: ", e.what()));
                }
            }
        } else if (!IsNotFound(contentOr.status())) {
            return contentOr.status();
        }

        SceneSystem::purgeRuntimeSubEntities(*m_registry);
        g_engineGlobal.registriesManager->storage(m_registry.get());
        return OkStatus();
    }

    Status Scene::loadDefaultTrunks()
    {
        if (m_dataManager) {
            for (const auto& [name, trunk] : m_loadedSceneTrunks) {
                if (trunk && trunk->getRegistry()) {
                    m_dataManager->removeRegistry(trunk->getRegistry());
                }
            }
        }
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

        if (m_registry) {
            m_dataManager->removeRegistry(m_registry.get());
        }
        m_registry.reset(g_engineGlobal.registriesManager->createRegistry());
        CHECK_STATUS(loadSceneRegistry(StrCat(scenePath, "/scene.reg")));
        if (m_dataManager->getCurrentSceneUuid() == m_uuid) {
            m_dataManager->appendRegistry(m_registry.get());
        }
        CHECK_STATUS(loadDefaultTrunks());
        (void)updateTrunkList();
        return OkStatus();
    }

    std::string Scene::getName()
    {
        std::string name = m_dataManager->getSceneNameByUuid(m_uuid);
        if (name.empty()) {
            m_dataManager->loadScenePathList();
            name = m_dataManager->getSceneNameByUuid(m_uuid);
        }
        return name;
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
        std::vector<std::string> discoveredTrunks;

        Status accessSt = g_engineGlobal.resourceLoader->accessDir(trunksPath);
        if (IsNotFound(accessSt)) {
            if (!m_isChunked) {
                discoveredTrunks.push_back("default");
            }
            if (m_sceneTrunk != discoveredTrunks) {
                m_sceneTrunk = std::move(discoveredTrunks);
            }
            return OkStatus();
        }
        CHECK_STATUS(accessSt);

        auto filesOr = g_engineGlobal.resourceLoader->readDir(trunksPath);
        CHECK_STATUS(filesOr);

        for (const auto& file : filesOr.value()) {
            if (file->extension() == ".dat") {
                discoveredTrunks.push_back(file->stem());
            }
        }
        if (!m_isChunked && std::find(discoveredTrunks.begin(), discoveredTrunks.end(), "default") == discoveredTrunks.end()) {
            discoveredTrunks.push_back("default");
        }
        if (m_sceneTrunk != discoveredTrunks) {
            m_sceneTrunk = std::move(discoveredTrunks);
        }
        return OkStatus();
    }
} // namespace Scene
} // namespace MQEngine
