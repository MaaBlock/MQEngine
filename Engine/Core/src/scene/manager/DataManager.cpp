#include "DataManager.h"
#include "../data/Component.h"
#include "../../resource/core/ResourceLoader.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../manager/RegistriesManager.h"
#include "../../script/system/ScriptSystem.h"
#include "../../render/system/MatrixCacheSystem.h"
#include "../../render/system/CameraSystem.h"

namespace MQEngine
{
    DataManager::DataManager()
    {
        m_dataLoader = makeUnique<FileDataLoader>();
        if (g_engineGlobal.rt) {
            m_modelLoader = UniquePtr(g_engineGlobal.rt->createModelLoader());
        }
        m_reloadWork.data = this;
    }

    DataManager::~DataManager()
    {
        shutdownWatchers();
    }

    void DataManager::registerInfo()
    {
        if (!m_modelLoader) {
            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            m_modelLoader = UniquePtr<ModelLoader>(engine->platform->getFctRuntime().createModelLoader());
        }
    }

    void DataManager::term()
    {
        shutdownWatchers();
        closeAllScenes();
        m_modelLoader.reset();
    }

    void DataManager::loadRes()
    {
        ZoneScoped();
        m_dataLoader->ensureDirectory(m_assetRoot);
        loadProjectSetting();
        loadScenePathList();
        updateModelPathList();

        {
            std::lock_guard<std::mutex> lock(m_proactorConfigMutex);
            m_workerAssetRoot = m_assetRoot;
        }
        m_proactorRebindRequested.store(true, std::memory_order_release);
    }

    void DataManager::setAssetRoot(const std::string& assetRoot)
    {
        if (m_assetRoot != assetRoot)
        {
            if (!m_currentScene.empty())
            {
                try {
                    closeScene(m_currentScene);
                } catch (...) {}
                m_currentScene.clear();
            }
            m_loadScenes.clear();
        }
        m_assetRoot = assetRoot;
        if (!m_assetRoot.empty() && m_dataLoader) {
            try {
                loadRes();
            } catch (...) {}
        }
    }

    void DataManager::loadScenePathList()
    {
        ZoneScoped();
        AssetResourceSnapshot snapshot;
        buildSceneSnapshotOnWorker(m_assetRoot, snapshot);
        applySceneSnapshot(snapshot);
    }

    void DataManager::newScene(const std::string& sceneName)
    {
        m_dataLoader->createDirectory(m_assetRoot + "/Scenes/" + sceneName);
        requestAsyncResourceReload(true, false);
    }

    void DataManager::updateModelPathList()
    {
        ZoneScoped();
        AssetResourceSnapshot snapshot;
        buildModelSnapshotOnWorker(m_assetRoot, snapshot);
        applyModelSnapshot(snapshot);
    }

    void DataManager::loadModelUuidMapping()
    {
        updateModelPathList();
    }

    void DataManager::loadSceneUuidMapping()
    {
        loadScenePathList();
    }

    void DataManager::requestAsyncResourceReload(bool reloadScenes, bool reloadModels)
    {
        if (reloadScenes) {
            m_workerReloadScenes.store(true, std::memory_order_release);
        }
        if (reloadModels) {
            m_workerReloadModels.store(true, std::memory_order_release);
        }
        scheduleReloadWorkOnMainLoop();
    }

    void DataManager::updateResource()
    {
        ZoneScoped();
        if (m_proactorRebindRequested.load(std::memory_order_acquire)) {
            rebindWatchersOnMainLoop();
        }
        scheduleReloadWorkOnMainLoop();

        if (!m_hasPendingSnapshot.exchange(false, std::memory_order_acquire)) {
            return;
        }

        AssetResourceSnapshot readySnapshot;
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            readySnapshot = std::move(m_pendingSnapshot);
            m_pendingSnapshot = AssetResourceSnapshot{};
        }

        if (readySnapshot.scenesUpdated) {
            applySceneSnapshot(readySnapshot);
        }
        if (readySnapshot.modelsUpdated) {
            applyModelSnapshot(readySnapshot);
        }
        if (!readySnapshot.modifiedSceneNames.empty()) {
            reloadModifiedScenes(readySnapshot.modifiedSceneNames);
        }
    }

    void DataManager::applySceneSnapshot(AssetResourceSnapshot& snapshot)
    {
        m_scenePathList = std::move(snapshot.scenePathList);
        m_uuidToScenePath = std::move(snapshot.uuidToScenePath);
        m_uuidToSceneName = std::move(snapshot.uuidToSceneName);
    }

    void DataManager::applyModelSnapshot(AssetResourceSnapshot& snapshot)
    {
        m_modelPathList = std::move(snapshot.modelPathList);
        m_uuidToModel = std::move(snapshot.uuidToModel);
    }

    void DataManager::reloadModifiedScenes(const std::set<std::string>& modifiedSceneNames)
    {
        ZoneScoped();
        for (const auto& sceneName : modifiedSceneNames) {
            for (const auto& [uuid, name] : m_uuidToSceneName) {
                if (name != sceneName) {
                    continue;
                }
                auto loadedIt = m_loadScenes.find(uuid);
                if (loadedIt != m_loadScenes.end() && loadedIt->second) {
                    Status st = loadedIt->second->load();
                    if (!st.ok()) {
                        spdlog::warn("Proactor热更新场景 [{}] 失败: {}", sceneName, st.message());
                    } else {
                        spdlog::info("Proactor已完成场景热更新: {} ({})", sceneName, uuid);
                    }
                }
            }
        }
    }

    uv_loop_t* DataManager::resolveMainLoop() const
    {
        if (g_engineGlobal.scriptSystem && g_engineGlobal.scriptSystem->getNodeEnvironment()) {
            return g_engineGlobal.scriptSystem->getNodeEnvironment()->loop();
        }
        return nullptr;
    }

    void DataManager::closeFsEventHandle(uv_fs_event_t*& handle)
    {
        if (!handle) {
            return;
        }
        handle->data = nullptr;
        uv_fs_event_stop(handle);
        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(handle))) {
            uv_close(reinterpret_cast<uv_handle_t*>(handle), &DataManager::onHandleClosed);
        }
        handle = nullptr;
    }

    void DataManager::rebindWatchersOnMainLoop()
    {
        if (m_watchersClosing) {
            return;
        }
        uv_loop_t* loop = resolveMainLoop();
        if (!loop) {
            return;
        }
        m_boundLoop = loop;
        m_proactorRebindRequested.store(false, std::memory_order_release);

        closeFsEventHandle(m_scenesFsEvent);
        closeFsEventHandle(m_modelsFsEvent);

        std::string root;
        {
            std::lock_guard<std::mutex> lock(m_proactorConfigMutex);
            root = m_workerAssetRoot;
        }
        if (root.empty()) {
            return;
        }

        std::string scenesDir = root + "/Scenes";
        std::string modelsDir = root + "/Models";
        m_dataLoader->ensureDirectory(scenesDir);
        m_dataLoader->ensureDirectory(modelsDir);

        m_scenesFsEvent = new uv_fs_event_t{};
        if (uv_fs_event_init(loop, m_scenesFsEvent) == 0) {
            m_scenesFsEvent->data = this;
            if (uv_fs_event_start(m_scenesFsEvent, &DataManager::onScenesFsEvent, scenesDir.c_str(), UV_FS_EVENT_RECURSIVE) != 0) {
                closeFsEventHandle(m_scenesFsEvent);
            }
        } else {
            delete m_scenesFsEvent;
            m_scenesFsEvent = nullptr;
        }

        m_modelsFsEvent = new uv_fs_event_t{};
        if (uv_fs_event_init(loop, m_modelsFsEvent) == 0) {
            m_modelsFsEvent->data = this;
            if (uv_fs_event_start(m_modelsFsEvent, &DataManager::onModelsFsEvent, modelsDir.c_str(), UV_FS_EVENT_RECURSIVE) != 0) {
                closeFsEventHandle(m_modelsFsEvent);
            }
        } else {
            delete m_modelsFsEvent;
            m_modelsFsEvent = nullptr;
        }
    }

    void DataManager::shutdownWatchers()
    {
        m_watchersClosing = true;
        m_proactorRebindRequested.store(false, std::memory_order_release);
        m_workerReloadScenes.store(false, std::memory_order_release);
        m_workerReloadModels.store(false, std::memory_order_release);
        closeFsEventHandle(m_scenesFsEvent);
        closeFsEventHandle(m_modelsFsEvent);
        m_boundLoop = nullptr;
    }

    void DataManager::onHandleClosed(uv_handle_t* handle)
    {
        delete reinterpret_cast<uv_fs_event_t*>(handle);
    }

    void DataManager::onScenesFsEvent(uv_fs_event_t* handle, const char* filename, int events, int status)
    {
        if (status < 0) {
            return;
        }
        auto* self = static_cast<DataManager*>(handle->data);
        if (!self || self->m_watchersClosing) {
            return;
        }
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nowMs < self->m_ignoreSceneFsEventsUntilMs.load(std::memory_order_acquire)) {
            return;
        }
        if (filename && (events & UV_CHANGE)) {
            if (SAI::Path::hasAnyExtension(filename, { ".dat", ".reg" })) {
                std::string normalized = SAI::Path::lexicallyNormal(filename);
                auto slashPos = normalized.find('/');
                std::string firstSeg = (slashPos == std::string::npos) ? normalized : normalized.substr(0, slashPos);
                if (!firstSeg.empty()) {
                    std::lock_guard<std::mutex> lock(self->m_proactorConfigMutex);
                    self->m_workerModifiedSceneNames.insert(firstSeg);
                }
            }
        }
        self->m_workerReloadScenes.store(true, std::memory_order_release);
        self->scheduleReloadWorkOnMainLoop();
    }

    void DataManager::onModelsFsEvent(uv_fs_event_t* handle, const char* filename, int events, int status)
    {
        (void)filename;
        (void)events;
        if (status < 0) {
            return;
        }
        auto* self = static_cast<DataManager*>(handle->data);
        if (!self || self->m_watchersClosing) {
            return;
        }
        self->m_workerReloadModels.store(true, std::memory_order_release);
        self->scheduleReloadWorkOnMainLoop();
    }

    void DataManager::scheduleReloadWorkOnMainLoop()
    {
        if (m_watchersClosing || m_workInFlight) {
            return;
        }
        bool doScenes = m_workerReloadScenes.exchange(false, std::memory_order_acq_rel);
        bool doModels = m_workerReloadModels.exchange(false, std::memory_order_acq_rel);

        std::string root;
        std::set<std::string> modifiedScenes;
        {
            std::lock_guard<std::mutex> lock(m_proactorConfigMutex);
            if (!doScenes && !doModels && m_workerModifiedSceneNames.empty()) {
                return;
            }
            root = m_workerAssetRoot;
            modifiedScenes = std::move(m_workerModifiedSceneNames);
            m_workerModifiedSceneNames.clear();
        }

        uv_loop_t* loop = m_boundLoop ? m_boundLoop : resolveMainLoop();
        if (!loop) {
            AssetResourceSnapshot snapshot;
            if (doScenes) {
                buildSceneSnapshotOnWorker(root, snapshot);
            }
            if (doModels) {
                buildModelSnapshotOnWorker(root, snapshot);
            }
            if (!modifiedScenes.empty()) {
                snapshot.modifiedSceneNames = std::move(modifiedScenes);
            }
            publishSnapshotFromWorker(std::move(snapshot));
            return;
        }

        m_activeWorkAssetRoot = std::move(root);
        m_activeWorkDoScenes = doScenes;
        m_activeWorkDoModels = doModels;
        m_activeWorkModifiedSceneNames = std::move(modifiedScenes);
        m_activeWorkSnapshot = AssetResourceSnapshot{};
        m_reloadWork.data = this;
        m_workInFlight = true;

        if (uv_queue_work(loop, &m_reloadWork, &DataManager::onReloadWork, &DataManager::onReloadAfterWork) != 0) {
            m_workInFlight = false;
            onReloadWork(&m_reloadWork);
            onReloadAfterWork(&m_reloadWork, 0);
        }
    }

    void DataManager::onReloadWork(uv_work_t* req)
    {
        auto* self = static_cast<DataManager*>(req->data);
        if (!self) {
            return;
        }
        if (self->m_activeWorkDoScenes) {
            self->buildSceneSnapshotOnWorker(self->m_activeWorkAssetRoot, self->m_activeWorkSnapshot);
        }
        if (self->m_activeWorkDoModels) {
            self->buildModelSnapshotOnWorker(self->m_activeWorkAssetRoot, self->m_activeWorkSnapshot);
        }
        if (!self->m_activeWorkModifiedSceneNames.empty()) {
            self->m_activeWorkSnapshot.modifiedSceneNames = std::move(self->m_activeWorkModifiedSceneNames);
            self->m_activeWorkModifiedSceneNames.clear();
        }
    }

    void DataManager::onReloadAfterWork(uv_work_t* req, int status)
    {
        (void)status;
        auto* self = static_cast<DataManager*>(req->data);
        if (!self) {
            return;
        }
        self->m_workInFlight = false;
        self->publishSnapshotFromWorker(std::move(self->m_activeWorkSnapshot));
        self->m_activeWorkSnapshot = AssetResourceSnapshot{};
        if (!self->m_watchersClosing) {
            self->scheduleReloadWorkOnMainLoop();
        }
    }

    void DataManager::buildSceneSnapshotOnWorker(const std::string& assetRoot, AssetResourceSnapshot& outSnapshot)
    {
        ZoneScoped();
        std::string scenesDir = assetRoot + "/Scenes";
        m_dataLoader->ensureDirectory(scenesDir);

        outSnapshot.scenePathList.clear();
        outSnapshot.uuidToScenePath.clear();
        outSnapshot.uuidToSceneName.clear();

        try {
            outSnapshot.scenePathList = m_dataLoader->getSubDirectories(scenesDir);
            for (const std::string& sceneDir : outSnapshot.scenePathList) {
                std::string uuidFilePath = sceneDir + "/scene.uuid";
                if (!m_dataLoader->fileExists(uuidFilePath)) {
                    continue;
                }
                auto uuidRes = ModelAssetSerializer::loadSceneUuid(g_engineGlobal.resourceLoader, uuidFilePath);
                if (uuidRes.ok() && !uuidRes->uuid.empty()) {
                    outSnapshot.uuidToScenePath[uuidRes->uuid] = sceneDir;
                    outSnapshot.uuidToSceneName[uuidRes->uuid] = SAI::Path::fileName(sceneDir);
                }
            }
        } catch (const std::exception&) {
        }
        outSnapshot.scenesUpdated = true;
    }

    void DataManager::buildModelSnapshotOnWorker(const std::string& assetRoot, AssetResourceSnapshot& outSnapshot)
    {
        ZoneScoped();
        std::string modelsDir = assetRoot + "/Models";
        m_dataLoader->ensureDirectory(modelsDir);

        outSnapshot.modelPathList.clear();
        outSnapshot.uuidToModel.clear();

        try {
            outSnapshot.modelPathList = m_dataLoader->getSubDirectories(modelsDir);
            for (const std::string& modelDir : outSnapshot.modelPathList) {
                std::string uuidFilePath = modelDir + "/model.uuid";
                if (!m_dataLoader->fileExists(uuidFilePath)) {
                    continue;
                }
                auto uuidRes = ModelAssetSerializer::loadModelUuid(g_engineGlobal.resourceLoader, uuidFilePath);
                if (uuidRes.ok() && !uuidRes->uuid.empty()) {
                    outSnapshot.uuidToModel[uuidRes->uuid] = modelDir;
                }
            }
        } catch (const std::exception&) {
        }
        outSnapshot.modelsUpdated = true;
    }

    void DataManager::publishSnapshotFromWorker(AssetResourceSnapshot&& snapshot)
    {
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            if (snapshot.scenesUpdated) {
                m_pendingSnapshot.scenePathList = std::move(snapshot.scenePathList);
                m_pendingSnapshot.uuidToScenePath = std::move(snapshot.uuidToScenePath);
                m_pendingSnapshot.uuidToSceneName = std::move(snapshot.uuidToSceneName);
                m_pendingSnapshot.scenesUpdated = true;
            }
            if (snapshot.modelsUpdated) {
                m_pendingSnapshot.modelPathList = std::move(snapshot.modelPathList);
                m_pendingSnapshot.uuidToModel = std::move(snapshot.uuidToModel);
                m_pendingSnapshot.modelsUpdated = true;
            }
            for (const auto& name : snapshot.modifiedSceneNames) {
                m_pendingSnapshot.modifiedSceneNames.insert(name);
            }
        }
        m_hasPendingSnapshot.store(true, std::memory_order_release);
    }

    void DataManager::loadScene(const std::string& uuid)
    {
        try {
            if (m_loadScenes.find(uuid) != m_loadScenes.end()) {
                return;
            }
            if (m_uuidToScenePath.find(uuid) == m_uuidToScenePath.end()) {
                loadScenePathList();
            }

            auto sceneIt = m_uuidToScenePath.find(uuid);
            if (sceneIt == m_uuidToScenePath.end()) {
                throw DataError("场景UUID不存在: " + uuid);
            }

            std::string scenePath = sceneIt->second;
            std::string sceneDataPath = scenePath + "/scene.dat";

            if (!m_dataLoader->directoryExists(scenePath)) {
                throw DataError("场景目录不存在: " + scenePath);
            }

            auto scene = std::make_shared<Scene::Scene>(this, uuid);

            if (m_dataLoader->fileExists(sceneDataPath) ||
                m_dataLoader->fileExists(scenePath + "/scene.reg") ||
                m_dataLoader->fileExists(scenePath + "/trunks/default.reg")) {
                Status loadSt = scene->load();
                if (!loadSt.ok()) {
                    spdlog::warn("加载场景数据存在版本差异或损坏 ({})，正在重新初始化场景: {}", loadSt.message(), scenePath);
                    scene->init();
                    Status saveSt = scene->save();
                    if (!saveSt.ok()) {
                        spdlog::error("重新初始化保存场景失败: {}", saveSt.message());
                    }
                }
            } else {
                scene->init();
                Status saveSt = scene->save();
                if (!saveSt.ok()) {
                    spdlog::error("初始化保存场景失败: {}", saveSt.message());
                }
                Status loadSt = scene->load();
                if (!loadSt.ok()) {
                    spdlog::error("初始化加载场景失败: {}", loadSt.message());
                }
            }

            m_loadScenes[uuid] = scene;

        } catch (const DataError&) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("加载场景失败: " + std::string(e.what()));
        }
    }

    std::vector<entt::registry*> DataManager::currentRegistries() const
    {
        return g_engineGlobal.registriesManager->currentRegistries();
    }

    void DataManager::appendRegistry(entt::registry* registry)
    {
        if (registry)
        {
            g_engineGlobal.registriesManager->storage(registry);
        }
        g_engineGlobal.registriesManager->requestAddRegistries(boost::make_ready_future(registry));
    }

    void DataManager::removeRegistry(entt::registry* registry)
    {
        if (g_engineGlobal.registriesManager) {
            g_engineGlobal.registriesManager->removeRegistryImmediately(registry);
            g_engineGlobal.registriesManager->requestRemoveRegistries(registry);
        }
    }

    void DataManager::openScene(const std::string& uuid)
    {
        ZoneScoped();
        try {
            if (m_currentScene == uuid) {
                return;
            }
            if (m_uuidToScenePath.find(uuid) == m_uuidToScenePath.end()) {
                loadScenePathList();
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
                appendRegistry(scenePtr->getRegistry());

                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            appendRegistry(trunk->getRegistry());
                        }
                    }
                }
            }
            g_engineGlobal.sceneEventPipe.trigger<SceneEvent::EntryScene>(
            {
                .uuid = uuid,
                .scene = scenePtr.get()
            });
        } catch (const DataError&) {
            throw;
        } catch (const std::exception& e) {
            throw DataError("打开场景失败: " + std::string(e.what()));
        }
    }

    void DataManager::closeScene(const std::string& uuid)
    {
        ZoneScoped();
        if (m_loadScenes.find(uuid) != m_loadScenes.end()) {
            auto scenePtr = m_loadScenes[uuid];
            if (scenePtr) {
                removeRegistry(scenePtr->getRegistry());
                for (const auto& trunkName : scenePtr->getTrunkList()) {
                    if (scenePtr->isLoad(trunkName)) {
                        auto trunk = scenePtr->getLoadedTrunk(trunkName);
                        if (trunk) {
                            removeRegistry(trunk->getRegistry());
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

    void DataManager::closeAllScenes()
    {
        ZoneScoped();
        std::vector<std::string> loadedUuids;
        loadedUuids.reserve(m_loadScenes.size());
        for (const auto& [uuid, scene] : m_loadScenes) {
            loadedUuids.push_back(uuid);
        }
        for (const auto& uuid : loadedUuids) {
            closeScene(uuid);
        }
        m_loadScenes.clear();
        m_currentScene.clear();
    }

    bool DataManager::saveScene(const std::string& uuid)
    {
        ZoneScoped();
        if (m_loadScenes.find(uuid) == m_loadScenes.end()) {
            return true;
        }
        notifySelfSceneSave();
        Status st = m_loadScenes[uuid]->save();
        notifySelfSceneSave();
        if (!st.ok()) {
            spdlog::error("保存场景失败: {}", st.message());
            return false;
        }
        return true;
    }

    void DataManager::notifySelfSceneSave()
    {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        m_ignoreSceneFsEventsUntilMs.store(nowMs + 1500, std::memory_order_release);
    }

    std::string DataManager::getCurrentSceneUuid() const
    {
        return m_currentScene;
    }

    std::string DataManager::getSceneNameByUuid(const std::string& uuid) const
    {
        auto it = m_uuidToSceneName.find(uuid);
        if (it != m_uuidToSceneName.end())
        {
            return it->second;
        }
        return "";
    }

    DataLoader* DataManager::getDataLoader() const
    {
        return m_dataLoader.get();
    }

    Scene::Scene* DataManager::getCurrentScene() const
    {
        if (m_currentScene.empty())
        {
            return nullptr;
        }
        auto it = m_loadScenes.find(m_currentScene);
        if (it == m_loadScenes.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    StatusOr<std::string> DataManager::locateModelFile(const std::string& modelUuid) const
    {
        auto it = m_uuidToModel.find(modelUuid);
        if (it == m_uuidToModel.end())
            return NotFoundError("模型UUID不存在: " + modelUuid);
        const std::string& modelPath = it->second;
        const std::string modelUuidFilePath = SAI::Path::join(modelPath, "model.uuid");
        auto modelUuidOr = ModelAssetSerializer::loadModelUuid(g_engineGlobal.resourceLoader, modelUuidFilePath);
        CHECK_STATUS(modelUuidOr);
        if (modelUuidOr->modelRelativePath.empty())
            return InternalError("模型元数据文件中的相对路径为空: " + modelUuidFilePath);
        return SAI::Path::join(modelPath, modelUuidOr->modelRelativePath);
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
        
        std::string modelUuidFilePath = SAI::Path::join(modelPath, "model.uuid");
        auto modelUuidOr = ModelAssetSerializer::loadModelUuid(g_engineGlobal.resourceLoader, modelUuidFilePath);
        if (!modelUuidOr.ok()) {
            FCT::fout << "读取模型UUID文件失败: " << modelUuidOr.status().message() << std::endl;
            return "";
        }
        return modelUuidOr->modelRelativePath;
    }

    void DataManager::loadProjectSetting()
    {
        std::string settingFilePath = m_assetRoot + "/project.setting";
        
        if (!m_dataLoader->fileExists(settingFilePath)) {
            m_projectSetting.initialSceneUuid = "";
            return;
        }
        
        auto settingOr = ModelAssetSerializer::loadProjectSetting(g_engineGlobal.resourceLoader, settingFilePath);
        if (settingOr.ok()) {
            m_projectSetting = settingOr.value();
        } else {
            FCT::fout << "读取项目设置文件失败: " << settingOr.status().message() << std::endl;
            m_projectSetting.initialSceneUuid = "";
        }
    }

    void DataManager::saveProjectSetting(const ProjectSetting& setting)
    {
        std::string settingFilePath = m_assetRoot + "/project.setting";
        m_projectSetting = setting;
        
        Status st = ModelAssetSerializer::saveProjectSetting(g_engineGlobal.resourceLoader, settingFilePath, m_projectSetting);
        if (!st.ok()) {
            FCT::fout << "保存项目设置文件失败: " << st.message() << std::endl;
        }
    }

    ProjectSetting DataManager::getProjectSetting() const
    {
        return m_projectSetting;
    }

    void DataManager::setInitialSceneUuid(const std::string& uuid)
    {
        m_projectSetting.initialSceneUuid = uuid;
        saveProjectSetting(m_projectSetting);
    }

    std::string DataManager::getInitialSceneUuid() const { return m_projectSetting.initialSceneUuid; }

    StatusOr<int> ParseEmbeddedTextureIndex(std::string_view texturePath)
    {
        if (texturePath.empty() || texturePath.front() != '*')
            return InvalidArgumentError(
                StrCat(
                    "无效的内嵌纹理路径 '",
                    texturePath,
                    "'：必须以 '*' 开头。"));
        std::string_view indexSv = texturePath.substr(1);
        if (indexSv.empty())
            return InvalidArgumentError(
                StrCat(
                    "不完整的内嵌纹理路径 '",
                    texturePath,
                     "'：'*' 后面缺少索引数字。"));
        std::string indexStr(indexSv);
        int textureIndex;
        std::size_t pos;
        try {
            textureIndex = std::stoi(indexStr, &pos);
        } catch (const std::invalid_argument&) {
            return InvalidArgumentError(
                StrCat("无效的纹理索引 '", indexStr, "'：无法解析为数字。"));
        } catch (const std::out_of_range&) {
            return OutOfRangeError(
               StrCat("纹理索引 '", indexStr, "' 超出有效范围。"));
        }
        if (pos != indexStr.length())
            return InvalidArgumentError(
                StrCat("纹理索引 '", indexStr, "' 后包含无效字符。"));
        if (textureIndex < 0)
            return InvalidArgumentError(
                StrCat("纹理索引不能为负数：", textureIndex));
        return textureIndex;
    }

    StatusOr<std::vector<unsigned char>> DataManager::extractImage(const std::string& modelUuid,
                                                                   const std::string& texturePath)
    {
        if (texturePath[0] == '*')
        {
            std::vector<unsigned char> ret;
            auto path = locateModelFile(modelUuid);
            CHECK_STATUS(path);
            auto index = ParseEmbeddedTextureIndex(texturePath);
            CHECK_STATUS(index);
            if (m_modelLoader->getEmbeddedTextureData(path.value(), index.value(), ret))
            {
                return ret;
            }
            return UnknownError("未能成功从模型" + path.value() + "找到内嵌纹理:" + texturePath);
        }
        assert(false && "目前仅支持内嵌纹理。");
        return UnimplementedError("目前仅支持内嵌纹理。");
    }

    StatusOr<std::string> DataManager::getModelTexturePath(const std::string& modelUuid, const std::string& texturePath)
    {
        if (texturePath[0] == '*')
            return InvalidArgumentError("错误的传递了内嵌纹理路径，此函数仅用于处理外部文件纹理。");
        auto modelPathResult = locateModelFile(modelUuid);
        CHECK_STATUS(modelPathResult);
        const std::string& modelFilePath = modelPathResult.value();
        return SAI::Path::join(SAI::Path::parentPath(modelFilePath), texturePath);
    }

    std::vector<std::string> DataManager::getLoadedTrunkNames() const
    {
        std::vector<std::string> names;
        Scene::Scene* scene = getCurrentScene();
        if (!scene) {
            return names;
        }
        if (scene->getRegistry()) {
            names.push_back("scene");
        }
        for (const auto& trunkName : scene->getTrunkList()) {
            if (scene->isLoad(trunkName) && scene->getLoadedTrunk(trunkName) && scene->getLoadedTrunk(trunkName)->getRegistry()) {
                names.push_back(trunkName);
            }
        }
        return names;
    }

    uint64_t DataManager::getTrunkRegistryAddress(const std::string& trunkName) const
    {
        Scene::Scene* scene = getCurrentScene();
        if (!scene) {
            return 0;
        }
        const std::string target = trunkName.empty() ? "default" : trunkName;
        if (target == "scene" && scene->getRegistry()) {
            return reinterpret_cast<uint64_t>(scene->getRegistry());
        }
        if (scene->isLoad(target) && scene->getLoadedTrunk(target) && scene->getLoadedTrunk(target)->getRegistry()) {
            return reinterpret_cast<uint64_t>(scene->getLoadedTrunk(target)->getRegistry());
        }
        if (trunkName.empty() || trunkName == "default") {
            if (scene->getRegistry()) {
                return reinterpret_cast<uint64_t>(scene->getRegistry());
            }
            for (const auto& name : scene->getTrunkList()) {
                if (scene->isLoad(name) && scene->getLoadedTrunk(name) && scene->getLoadedTrunk(name)->getRegistry()) {
                    return reinterpret_cast<uint64_t>(scene->getLoadedTrunk(name)->getRegistry());
                }
            }
        }
        return 0;
    }

    std::vector<uint32_t> DataManager::getTrunkEntityIds(const std::string& trunkName) const
    {
        std::vector<uint32_t> ids;
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return ids;
        }
        const auto& entityStorage = reg->storage<entt::entity>();
        for (auto entity : entityStorage) {
            if (!reg->valid(entity)) {
                continue;
            }
            if (const auto* subTag = reg->try_get<RuntimeSubEntityTag>(entity)) {
                if (subTag->hideInHierarchy) {
                    continue;
                }
            }
            ids.push_back(static_cast<uint32_t>(entity));
        }
        return ids;
    }

    uint32_t DataManager::createEntity(const std::string& trunkName, const std::string& name)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return static_cast<uint32_t>(entt::null);
        }
        if (g_engineGlobal.isRunning && g_engineGlobal.registriesManager) {
            auto deferred = g_engineGlobal.registriesManager->createEntityDeferred(reg);
            if (!name.empty()) {
                g_engineGlobal.registriesManager->emplaceDeferred<NameTag>(reg, deferred, NameTag{name});
            }
            return static_cast<uint32_t>(deferred);
        }
        entt::entity entity = reg->create();
        if (!name.empty()) {
            reg->emplace_or_replace<NameTag>(entity, NameTag{name});
        }
        return static_cast<uint32_t>(entity);
    }

    bool DataManager::isRegistryEntityAncestor(entt::registry* reg, entt::entity candidateAncestor, entt::entity entity)
    {
        if (!reg || candidateAncestor == entt::null || entity == entt::null) {
            return false;
        }
        entt::entity current = entity;
        for (int depth = 0; depth < 32 && current != entt::null && reg->valid(current); ++depth) {
            if (current == candidateAncestor) {
                return true;
            }
            const auto* hierarchy = reg->try_get<HierarchyComponent>(current);
            if (!hierarchy) {
                break;
            }
            current = static_cast<entt::entity>(hierarchy->parentEntity);
        }
        return false;
    }

    bool DataManager::setRegistryEntityParent(entt::registry* reg, entt::entity child, entt::entity parent)
    {
        if (!reg || !reg->valid(child)) {
            return false;
        }
        if (parent != entt::null && (!reg->valid(parent) || isRegistryEntityAncestor(reg, child, parent))) {
            return false;
        }

        auto& childTag = reg->get_or_emplace<NameTag>(child);
        if (childTag.uuid.empty()) {
            childTag = NameTag{childTag.name.empty() ? "entity_" + std::to_string(static_cast<uint32_t>(child)) : childTag.name};
        }

        if (auto* childHierarchy = reg->try_get<HierarchyComponent>(child)) {
            entt::entity oldParent = static_cast<entt::entity>(childHierarchy->parentEntity);
            if (oldParent != entt::null && reg->valid(oldParent)) {
                if (auto* oldParentHierarchy = reg->try_get<HierarchyComponent>(oldParent)) {
                    auto& vec = oldParentHierarchy->childrenUuids;
                    vec.erase(std::remove(vec.begin(), vec.end(), childTag.uuid), vec.end());
                }
            }
        }

        if (parent == entt::null) {
            if (auto* childHierarchy = reg->try_get<HierarchyComponent>(child)) {
                childHierarchy->parentUuid.clear();
                childHierarchy->parentEntity = static_cast<uint32_t>(entt::null);
            }
            return true;
        }

        auto& parentTag = reg->get_or_emplace<NameTag>(parent);
        if (parentTag.uuid.empty()) {
            parentTag = NameTag{parentTag.name.empty() ? "entity_" + std::to_string(static_cast<uint32_t>(parent)) : parentTag.name};
        }

        auto& childHierarchy = reg->get_or_emplace<HierarchyComponent>(child);
        childHierarchy.parentEntity = static_cast<uint32_t>(parent);
        childHierarchy.parentUuid = parentTag.uuid;

        auto& parentHierarchy = reg->get_or_emplace<HierarchyComponent>(parent);
        if (std::find(parentHierarchy.childrenUuids.begin(), parentHierarchy.childrenUuids.end(), childTag.uuid) ==
            parentHierarchy.childrenUuids.end()) {
            parentHierarchy.childrenUuids.push_back(childTag.uuid);
        }
        return true;
    }

    void DataManager::destroyRegistryEntityHierarchy(entt::registry* reg, entt::entity entity)
    {
        if (!reg || !reg->valid(entity)) {
            return;
        }
        setRegistryEntityParent(reg, entity, entt::null);

        std::vector<entt::entity> childrenToDestroy;
        auto view = reg->view<HierarchyComponent>();
        for (auto candidate : view) {
            const auto& h = view.get<HierarchyComponent>(candidate);
            if (static_cast<entt::entity>(h.parentEntity) == entity) {
                childrenToDestroy.push_back(candidate);
            }
        }
        for (auto child : childrenToDestroy) {
            if (reg->valid(child)) {
                destroyRegistryEntityHierarchy(reg, child);
            }
        }
        if (reg->valid(entity)) {
            reg->destroy(entity);
        }
    }

    bool DataManager::destroyEntity(const std::string& trunkName, uint32_t entityId)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        DeferredEntity deferred(static_cast<entt::entity>(entityId), reg);
        if (g_engineGlobal.isRunning && g_engineGlobal.registriesManager) {
            g_engineGlobal.registriesManager->destroyEntityDeferred(reg, deferred, true);
            return true;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }
        destroyRegistryEntityHierarchy(reg, entity);
        return true;
    }

    uint32_t DataManager::findEntityByName(const std::string& trunkName, const std::string& name) const
    {
        auto findInRegistry = [&](entt::registry* reg) -> uint32_t {
            if (!reg) {
                return static_cast<uint32_t>(entt::null);
            }
            auto view = reg->view<NameTag>();
            for (auto entity : view) {
                if (view.get<NameTag>(entity).name == name) {
                    return static_cast<uint32_t>(entity);
                }
            }
            return static_cast<uint32_t>(entt::null);
        };

        entt::registry* primaryReg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        uint32_t found = findInRegistry(primaryReg);
        if (found != static_cast<uint32_t>(entt::null)) {
            return found;
        }

        if (trunkName.empty() || trunkName == "default") {
            entt::registry* defaultReg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress("default"));
            if (defaultReg && defaultReg != primaryReg) {
                found = findInRegistry(defaultReg);
                if (found != static_cast<uint32_t>(entt::null)) {
                    return found;
                }
            }
            entt::registry* sceneReg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress("scene"));
            if (sceneReg && sceneReg != primaryReg) {
                found = findInRegistry(sceneReg);
                if (found != static_cast<uint32_t>(entt::null)) {
                    return found;
                }
            }
        }
        return static_cast<uint32_t>(entt::null);
    }

    bool DataManager::setParent(const std::string& trunkName, uint32_t childEntityId, uint32_t parentEntityId)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        return setRegistryEntityParent(
            reg,
            static_cast<entt::entity>(childEntityId),
            static_cast<entt::entity>(parentEntityId));
    }

    bool DataManager::clearParent(const std::string& trunkName, uint32_t childEntityId)
    {
        return setParent(trunkName, childEntityId, static_cast<uint32_t>(entt::null));
    }

    uint32_t DataManager::getParent(const std::string& trunkName, uint32_t entityId) const
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return static_cast<uint32_t>(entt::null);
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return static_cast<uint32_t>(entt::null);
        }
        if (const auto* hierarchy = reg->try_get<HierarchyComponent>(entity)) {
            entt::entity parent = static_cast<entt::entity>(hierarchy->parentEntity);
            if (parent != entt::null && reg->valid(parent)) {
                return static_cast<uint32_t>(parent);
            }
        }
        return static_cast<uint32_t>(entt::null);
    }

    std::vector<uint32_t> DataManager::getChildren(const std::string& trunkName, uint32_t entityId) const
    {
        std::vector<uint32_t> children;
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return children;
        }
        entt::entity parent = static_cast<entt::entity>(entityId);
        if (!reg->valid(parent)) {
            return children;
        }
        std::string parentUuid;
        if (const auto* tag = reg->try_get<NameTag>(parent)) {
            parentUuid = tag->uuid;
        }
        auto view = reg->view<HierarchyComponent>();
        for (auto entity : view) {
            if (!reg->valid(entity)) {
                continue;
            }
            if (const auto* subTag = reg->try_get<RuntimeSubEntityTag>(entity)) {
                if (subTag->hideInHierarchy) {
                    continue;
                }
            }
            const auto& h = view.get<HierarchyComponent>(entity);
            if (static_cast<entt::entity>(h.parentEntity) == parent ||
                (!parentUuid.empty() && h.parentUuid == parentUuid)) {
                children.push_back(static_cast<uint32_t>(entity));
            }
        }
        return children;
    }

    FCT::Vec3<> DataManager::getWorldPosition(const std::string& trunkName, uint32_t entityId) const
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
        }
        if (g_engineGlobal.matrixCacheSystem) {
            return g_engineGlobal.matrixCacheSystem->computeEntityWorldPosition(reg, entity);
        }
        if (const auto* pos = reg->try_get<PositionComponent>(entity)) {
            return pos->position;
        }
        return FCT::Vec3<>(0.0f, 0.0f, 0.0f);
    }

    bool DataManager::setWorldPosition(const std::string& trunkName, uint32_t entityId, FCT::Vec3<> worldPos)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }

        FCT::Vec3<> localPos = worldPos;
        if (const auto* hierarchy = reg->try_get<HierarchyComponent>(entity)) {
            entt::entity parent = static_cast<entt::entity>(hierarchy->parentEntity);
            if (parent != entt::null && reg->valid(parent)) {
                if (g_engineGlobal.matrixCacheSystem) {
                    FCT::Mat4 parentWorld = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(reg, parent);
                    FCT::Mat4 invParent = parentWorld.inverse();
                    FCT::Vec4 local4 = invParent * FCT::Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
                    localPos = FCT::Vec3<>(local4.x, local4.y, local4.z);
                } else if (const auto* parentPos = reg->try_get<PositionComponent>(parent)) {
                    localPos = worldPos - parentPos->position;
                }
            }
        }
        reg->emplace_or_replace<PositionComponent>(entity, PositionComponent{localPos});
        return true;
    }

    uint32_t DataManager::getActiveCameraEntity(const std::string& trunkName) const
    {
        auto findActiveInReg = [](entt::registry* reg) -> uint32_t {
            if (!reg) {
                return static_cast<uint32_t>(entt::null);
            }
            auto view = reg->view<CameraComponent>();
            for (auto entity : view) {
                if (view.get<CameraComponent>(entity).active) {
                    return static_cast<uint32_t>(entity);
                }
            }
            for (auto entity : view) {
                return static_cast<uint32_t>(entity);
            }
            return static_cast<uint32_t>(entt::null);
        };

        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        uint32_t found = findActiveInReg(reg);
        if (found != static_cast<uint32_t>(entt::null)) {
            return found;
        }
        if (trunkName.empty() || trunkName == "default") {
            entt::registry* defReg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress("default"));
            if (defReg && defReg != reg) {
                found = findActiveInReg(defReg);
                if (found != static_cast<uint32_t>(entt::null)) {
                    return found;
                }
            }
        }
        return static_cast<uint32_t>(entt::null);
    }

    bool DataManager::setActiveCamera(const std::string& trunkName, uint32_t cameraEntityId)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg || !g_engineGlobal.cameraSystem) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(cameraEntityId);
        if (!reg->valid(entity) || !reg->all_of<CameraComponent>(entity)) {
            return false;
        }
        g_engineGlobal.cameraSystem->setActiveCamera(reg, entity);
        return true;
    }

    bool DataManager::lookAt(const std::string& trunkName, uint32_t entityId, FCT::Vec3<> target)
    {
        entt::registry* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }
        FCT::Vec3<> eyePos = getWorldPosition(trunkName, entityId);
        FCT::Vec3<> diff = target - eyePos;
        if (diff.length() < 1e-5f) {
            return false;
        }
        RotationComponent rotComp = CameraSystem::calculateRotationFromDirection(diff);
        reg->emplace_or_replace<RotationComponent>(entity, rotComp);
        return true;
    }

    std::string DataManager::importModel(const std::string& modelPath)
    {
        if (modelPath.empty() || !SAI::FileSystem::exists(modelPath)) {
            spdlog::warn("importModel: 模型文件不存在 [{}]", modelPath);
            return "";
        }

        if (!m_modelLoader && g_engineGlobal.rt) {
            m_modelLoader = UniquePtr(g_engineGlobal.rt->createModelLoader());
        }
        if (!m_modelLoader) {
            spdlog::error("importModel: ModelLoader 未就绪");
            return "";
        }

        std::string modelsRoot = (m_assetRoot.empty() ? "./Assets" : m_assetRoot) + "/Models";
        (void)SAI::FileSystem::createDirectories(modelsRoot);

        std::string modelBaseName = SAI::Path::stem(modelPath);
        std::string originalFileName = SAI::Path::fileName(modelPath);

        for (const auto& existingDir : SAI::FileSystem::listSubDirectories(modelsRoot)) {
            std::string existingSource = SAI::Path::join(existingDir, "source", originalFileName);
            std::string existingUuidFile = SAI::Path::join(existingDir, "model.uuid");
            if (SAI::FileSystem::exists(existingSource) && SAI::FileSystem::exists(existingUuidFile)) {
                auto uuidRes = ModelAssetSerializer::loadModelUuid(g_engineGlobal.resourceLoader, existingUuidFile);
                if (uuidRes.ok() && !uuidRes->uuid.empty()) {
                    m_uuidToModel[uuidRes->uuid] = existingDir;
                    return uuidRes->uuid;
                }
            }
        }

        std::string targetDirName = SAI::FileSystem::getUniqueDirectoryName(modelsRoot, modelBaseName);
        std::string targetDir = SAI::Path::join(modelsRoot, targetDirName);
        (void)SAI::FileSystem::createDirectories(targetDir);

        std::set<std::string> allModelPaths = m_modelLoader->resolveModePaths(modelPath);
        if (allModelPaths.empty()) {
            allModelPaths.insert(modelPath);
        }
        std::string sourceBaseDir = SAI::Path::parentPath(modelPath);
        std::string modelSubDir = SAI::Path::join(targetDir, "source");
        (void)SAI::FileSystem::createDirectories(modelSubDir);

        for (const auto& filePath : allModelPaths) {
            if (SAI::FileSystem::exists(filePath)) {
                (void)SAI::FileSystem::copyFilePreservingRelative(filePath, sourceBaseDir, modelSubDir);
            }
        }

        std::string indexFile = SAI::Path::join(targetDir, "index.edit.dat");
        try {
            FCT::ModelInfo::SceneInfo sceneInfo = m_modelLoader->loadModelInfo(modelPath);
            Status idxSt = ModelAssetSerializer::saveModelIndex(g_engineGlobal.resourceLoader, indexFile, sceneInfo);
            if (!idxSt.ok()) {
                spdlog::warn("importModel: 保存模型索引失败 [{}]: {}", modelPath, idxSt.message());
            }
        } catch (const std::exception& e) {
            spdlog::warn("importModel: 保存模型索引失败 [{}]: {}", modelPath, e.what());
        }

        std::string uuidString;
        try {
            static boost::uuids::random_generator uuidGen;
            uuidString = boost::uuids::to_string(uuidGen());

            ModelUuidFile modelUuidFile;
            modelUuidFile.uuid = uuidString;
            modelUuidFile.modelRelativePath = SAI::Path::join("source", originalFileName);

            std::string uuidFile = SAI::Path::join(targetDir, "model.uuid");
            Status uuidSt = ModelAssetSerializer::saveModelUuid(g_engineGlobal.resourceLoader, uuidFile, modelUuidFile);
            if (!uuidSt.ok()) {
                spdlog::error("importModel: 生成 UUID 文件失败: {}", uuidSt.message());
                return "";
            }
        } catch (const std::exception& e) {
            spdlog::error("importModel: 生成 UUID 文件失败: {}", e.what());
            return "";
        }

        std::string timestampFile = SAI::Path::join(targetDir, "timestamp.dat");
        int64_t timestamp = SAI::FileSystem::getLastWriteTimeSeconds(modelPath);
        (void)ModelAssetSerializer::saveModelTimestamp(g_engineGlobal.resourceLoader, timestampFile, timestamp, modelPath);

        updateModelPathList();
        return uuidString;
    }

    std::vector<std::string> DataManager::getModelMeshNames(const std::string& modelUuid) const
    {
        std::vector<std::string> meshNames;
        std::string modelDir = getModelPathByUuid(modelUuid);
        if (modelDir.empty()) {
            return meshNames;
        }
        std::string indexFile = SAI::Path::join(modelDir, "index.edit.dat");
        auto sceneInfoOr = ModelAssetSerializer::loadModelIndex(g_engineGlobal.resourceLoader, indexFile);
        if (sceneInfoOr.ok()) {
            for (const auto& meshInfo : sceneInfoOr->meshInfos) {
                meshNames.push_back(meshInfo.name);
            }
        }
        return meshNames;
    }

    bool DataManager::instantiateModelToEntity(const std::string& trunkName, uint32_t entityId, const std::string& modelUuid)
    {
        if (modelUuid.empty()) {
            return false;
        }
        auto* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }

        std::string modelDir = getModelPathByUuid(modelUuid);
        if (modelDir.empty()) {
            updateModelPathList();
            modelDir = getModelPathByUuid(modelUuid);
        }
        if (modelDir.empty()) {
            return false;
        }

        std::string indexFile = SAI::Path::join(modelDir, "index.edit.dat");
        auto sceneInfoOr = ModelAssetSerializer::loadModelIndex(g_engineGlobal.resourceLoader, indexFile);
        if (!sceneInfoOr.ok()) {
            return false;
        }

        const auto& sceneInfo = sceneInfoOr.value();
        if (sceneInfo.meshInfos.empty()) {
            return false;
        }
        if (sceneInfo.meshInfos.size() == 1) {
            reg->emplace_or_replace<StaticMeshInstance>(entity, modelUuid, sceneInfo.meshInfos[0].name);
            if (!sceneInfo.textureInfos.empty()) {
                reg->emplace_or_replace<DiffuseTextureComponent>(entity, modelUuid, sceneInfo.textureInfos[0].path);
            }
        } else {
            for (const auto& meshInfo : sceneInfo.meshInfos) {
                entt::entity childEnt = reg->create();
                reg->emplace<NameTag>(childEnt, meshInfo.name);
                reg->emplace<PositionComponent>(childEnt, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
                reg->emplace<RotationComponent>(childEnt, FCT::Vec3{ 0.0f, 0.0f, 0.0f });
                reg->emplace<ScaleComponent>(childEnt, FCT::Vec3{ 1.0f, 1.0f, 1.0f });
                reg->emplace<StaticMeshInstance>(childEnt, modelUuid, meshInfo.name);
                if (!sceneInfo.textureInfos.empty()) {
                    reg->emplace<DiffuseTextureComponent>(childEnt, modelUuid, sceneInfo.textureInfos[0].path);
                }
                setRegistryEntityParent(reg, childEnt, entity);
            }
        }
        bool hasSkeleton = (sceneInfo.boneCount > 0 || !sceneInfo.animationNames.empty());
        std::string defaultClip = sceneInfo.animationNames.empty() ? "" : sceneInfo.animationNames[0];
        if (!hasSkeleton && g_engineGlobal.skeletalAnimationSystem) {
            auto skeleton = g_engineGlobal.skeletalAnimationSystem->getOrLoadSkeleton(modelUuid);
            if (skeleton && (skeleton->hasSkeleton() || !skeleton->animations.empty())) {
                hasSkeleton = true;
                if (defaultClip.empty() && !skeleton->animations.empty()) {
                    defaultClip = skeleton->animations[0].name;
                }
            }
        }
        if (hasSkeleton) {
            reg->emplace_or_replace<SkeletalAnimatorComponent>(entity, SkeletalAnimatorComponent{modelUuid, defaultClip});
        }
        return true;
    }

    bool DataManager::bindScriptClass(const std::string& trunkName, uint32_t entityId, const std::string& className)
    {
        auto* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }
        auto& table = reg->get_or_emplace<ScriptFunctionTableComponent>(entity);
        table.className = className;
        if (reg->all_of<CacheScriptComponent>(entity)) {
            auto& cache = reg->get<CacheScriptComponent>(entity);
            delete cache.m_jsObject;
            cache.m_jsObject = nullptr;
            delete cache.m_pyObject;
            cache.m_pyObject = nullptr;
            reg->remove<CacheScriptComponent>(entity);
        }
        return true;
    }

    bool DataManager::bindTickerScript(const std::string& trunkName, uint32_t entityId, const std::string& functionName)
    {
        auto* reg = reinterpret_cast<entt::registry*>(getTrunkRegistryAddress(trunkName));
        if (!reg || functionName.empty()) {
            return false;
        }
        entt::entity entity = static_cast<entt::entity>(entityId);
        if (!reg->valid(entity)) {
            return false;
        }
        auto& table = reg->get_or_emplace<ScriptFunctionTableComponent>(entity);
        if (std::find(table.onTicker.begin(), table.onTicker.end(), functionName) == table.onTicker.end()) {
            table.onTicker.push_back(functionName);
        }
        if (reg->all_of<CacheScriptComponent>(entity)) {
            auto& cache = reg->get<CacheScriptComponent>(entity);
            delete cache.m_jsObject;
            cache.m_jsObject = nullptr;
            delete cache.m_pyObject;
            cache.m_pyObject = nullptr;
            reg->remove<CacheScriptComponent>(entity);
        }
        return true;
    }
} // namespace MQEngine
