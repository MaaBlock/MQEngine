//
// Created by Administrator on 2025/8/20.
//

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../../thirdparty/thirdparty.h"
#include "../../resource/data/DataError.h"
#include "../core/Scene.h"
#include "../core/SceneTrunk.h"
#include "../../render/data/Camera.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../resource/core/DataLoader.h"
#include "../../resource/core/FileDataLoader.h"
#include "../../resource/core/ModelAssetSerializer.h"

namespace MQEngine
{
    /**
     * @brief 后台Proactor线程预读并反序列化完成的资产目录与UUID快照
     */
    struct AssetResourceSnapshot
    {
        std::vector<std::string> scenePathList;
        std::unordered_map<std::string, std::string> uuidToScenePath;
        std::unordered_map<std::string, std::string> uuidToSceneName;
        std::vector<std::string> modelPathList;
        std::unordered_map<std::string, std::string> uuidToModel;
        std::set<std::string> modifiedSceneNames;
        bool scenesUpdated = false;
        bool modelsUpdated = false;
    };

    class ENGINE_API DataManager : public CoreEngineManager<DataManager>
    {
    public:
        DataManager();
        ~DataManager();

        void registerInfo();
        void term();

        std::vector<std::string> getModelList() const
        {
            return m_modelPathList;
        }
        void loadRes();
        void loadScenePathList();
        void newScene(const std::string& sceneName);
        void updateModelPathList();
        /**
         * @brief 主线程每帧调用的Proactor完成队列消费接口，检测后台读取完结信号并同步至主线程与Scene
         */
        void updateResource();
        /**
         * @brief 通知后台工作任务重新扫描并预读资产目录
         */
        void requestAsyncResourceReload(bool reloadScenes = true, bool reloadModels = true);
        /**
         * @brief 停止并关闭绑定在主线程Node事件循环上的文件监视器句柄
         */
        void shutdownWatchers();

        std::vector<entt::registry*> currentRegistries() const;
        void appendRegistry(entt::registry* registry);
        void removeRegistry(entt::registry* registry);
        void openScene(const std::string& uuid);
        void closeScene(const std::string& uuid);
        void closeAllScenes();
        void loadProjectSetting();
        void saveProjectSetting(const ProjectSetting& setting);
        ProjectSetting getProjectSetting() const;
        void setInitialSceneUuid(const std::string& uuid);
        std::string getInitialSceneUuid() const;
        /*
         * @brief 从模型里获取 内嵌 Image数据
         */
        StatusOr<std::vector<unsigned char>> extractImage(const std::string& modelUuid, const std::string& texturePath);
        /**
         * @brief 从纹理相对模型的路径转换为纹理相对路径
         */
        StatusOr<std::string> getModelTexturePath(const std::string& modelUuid, const std::string& texturePath);
        void loadScene(const std::string& uuid);
        bool saveScene(const std::string& uuid);
        void notifySelfSceneSave();
        std::string getCurrentSceneUuid() const;
        std::string getSceneNameByUuid(const std::string& uuid) const;
        DataLoader* getDataLoader() const;
        std::string getModelPathByUuid(const std::string& uuid) const;
        std::string getModelRelativePathByUuid(const std::string& uuid) const;
        StatusOr<std::string> locateModelFile(const std::string& modelUuid) const;
        Scene::Scene* getCurrentScene() const;

        /**
         * @brief 动态设置/重定向资产根目录并重新加载资产清单
         */
        void setAssetRoot(const std::string& assetRoot);

        const std::string& getAssetRoot() const
        {
            return m_assetRoot;
        }

        const std::unordered_map<std::string, std::string>& getSceneUuidToNameMap() const
        {
            return m_uuidToSceneName;
        }

        std::vector<std::string> getLoadedTrunkNames() const;
        uint64_t getTrunkRegistryAddress(const std::string& trunkName) const;
        std::vector<uint32_t> getTrunkEntityIds(const std::string& trunkName) const;
        uint32_t createEntity(const std::string& trunkName, const std::string& name);
        bool destroyEntity(const std::string& trunkName, uint32_t entityId);
        uint32_t findEntityByName(const std::string& trunkName, const std::string& name) const;
        bool setParent(const std::string& trunkName, uint32_t childEntityId, uint32_t parentEntityId);
        bool clearParent(const std::string& trunkName, uint32_t childEntityId);
        uint32_t getParent(const std::string& trunkName, uint32_t entityId) const;
        std::vector<uint32_t> getChildren(const std::string& trunkName, uint32_t entityId) const;
        FCT::Vec3<> getWorldPosition(const std::string& trunkName, uint32_t entityId) const;
        bool setWorldPosition(const std::string& trunkName, uint32_t entityId, FCT::Vec3<> worldPos);
        uint32_t getActiveCameraEntity(const std::string& trunkName) const;
        bool setActiveCamera(const std::string& trunkName, uint32_t cameraEntityId);
        bool lookAt(const std::string& trunkName, uint32_t entityId, FCT::Vec3<> target);
        std::string importModel(const std::string& modelPath);
        std::vector<std::string> getModelMeshNames(const std::string& modelUuid) const;
        bool instantiateModelToEntity(const std::string& trunkName, uint32_t entityId, const std::string& modelUuid);
        bool bindScriptClass(const std::string& trunkName, uint32_t entityId, const std::string& className);
        bool bindTickerScript(const std::string& trunkName, uint32_t entityId, const std::string& functionName);

        static bool setRegistryEntityParent(entt::registry* reg, entt::entity child, entt::entity parent);
        static bool isRegistryEntityAncestor(entt::registry* reg, entt::entity candidateAncestor, entt::entity entity);
        static void destroyRegistryEntityHierarchy(entt::registry* reg, entt::entity entity);

    private:
        void loadModelUuidMapping();
        void loadSceneUuidMapping();

        uv_loop_t* resolveMainLoop() const;
        void closeFsEventHandle(uv_fs_event_t*& handle);
        void rebindWatchersOnMainLoop();
        void scheduleReloadWorkOnMainLoop();
        void buildSceneSnapshotOnWorker(const std::string& assetRoot, AssetResourceSnapshot& outSnapshot);
        void buildModelSnapshotOnWorker(const std::string& assetRoot, AssetResourceSnapshot& outSnapshot);
        void publishSnapshotFromWorker(AssetResourceSnapshot&& snapshot);
        void applySceneSnapshot(AssetResourceSnapshot& snapshot);
        void applyModelSnapshot(AssetResourceSnapshot& snapshot);
        void reloadModifiedScenes(const std::set<std::string>& modifiedSceneNames);

        static void onScenesFsEvent(uv_fs_event_t* handle, const char* filename, int events, int status);
        static void onModelsFsEvent(uv_fs_event_t* handle, const char* filename, int events, int status);
        static void onReloadWork(uv_work_t* req);
        static void onReloadAfterWork(uv_work_t* req, int status);
        static void onHandleClosed(uv_handle_t* handle);

        std::string m_currentScene;
        std::string m_assetRoot = "./Assets";
        std::unordered_map<std::string, std::shared_ptr<Scene::Scene>> m_loadScenes;
        std::vector<std::string> m_scenePathList;
        std::unordered_map<std::string, std::string> m_uuidToModel;
        std::unordered_map<std::string, std::string> m_uuidToScenePath;
        std::unordered_map<std::string, std::string> m_uuidToSceneName;
        std::vector<std::string> m_modelPathList;
        UniquePtr<DataLoader> m_dataLoader;
        UniquePtr<ModelLoader> m_modelLoader;
        ProjectSetting m_projectSetting;

        uv_loop_t* m_boundLoop = nullptr;
        uv_fs_event_t* m_scenesFsEvent = nullptr;
        uv_fs_event_t* m_modelsFsEvent = nullptr;
        uv_work_t m_reloadWork{};
        bool m_watchersClosing = false;
        bool m_workInFlight = false;
        std::atomic<bool> m_proactorRebindRequested{false};
        std::atomic<bool> m_workerReloadScenes{false};
        std::atomic<bool> m_workerReloadModels{false};

        std::mutex m_proactorConfigMutex;
        std::string m_workerAssetRoot = "./Assets";
        std::set<std::string> m_workerModifiedSceneNames;

        std::string m_activeWorkAssetRoot;
        bool m_activeWorkDoScenes = false;
        bool m_activeWorkDoModels = false;
        std::set<std::string> m_activeWorkModifiedSceneNames;
        AssetResourceSnapshot m_activeWorkSnapshot;

        std::mutex m_snapshotMutex;
        AssetResourceSnapshot m_pendingSnapshot;
        std::atomic<bool> m_hasPendingSnapshot{false};
        std::atomic<int64_t> m_ignoreSceneFsEventsUntilMs{0};
    };

}// namespace MQEngine

#endif //DATAMANAGER_H
