//
// Created by Administrator on 2025/8/20.
//

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "DataError.h"
#include "Scene.h"
#include "SceneTrunk.h"
#include <unordered_map>
#include <memory>
#include <functional>

#include "./Camera.h"
#include "../core/EngineGlobal.h"
#include "./DataLoader.h"
#include "./FileDataLoader.h"

namespace MQEngine
{
    struct ModelUuidFile
    {
        std::string uuid;
        std::string modelRelativePath;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version) {
            ar & uuid;
            ar & modelRelativePath;
        }
    };

    struct SceneUuidFile
    {
        std::string uuid;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & uuid;
        }
    };

    struct ProjectSetting
    {
        std::string initialSceneUuid;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int version)
        {
            ar & initialSceneUuid;
        }
    };

    class ENGINE_API DataManager
    {
    public:
        DataManager()
        {
            m_dataLoader = std::make_unique<FileDataLoader>();
            m_modelLoader = UniquePtr(g_engineGlobal.rt->createModelLoader());
        }
        std::vector<std::string> getModelList()
        {
            updateModelPathList();
            return m_modelPathList;
        }
        void loadRes();
        void loadScenePathList();
        void newScene(const std::string& sceneName);
        void updateModelPathList();
        std::vector<entt::registry*> currentRegistries() const;
        void appendRegistry(entt::registry* registry);
        void removeRegistry(entt::registry* registry);
        void openScene(const std::string& uuid);
        void closeScene(const std::string& uuid);
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
        void loadScene(const std::string& uuid)
        {
            try {
                if (m_loadScenes.find(uuid) != m_loadScenes.end()) {
                    return;
                }
                loadScenePathList();

                auto sceneIt = m_uuidToScenePath.find(uuid);
                if (sceneIt == m_uuidToScenePath.end()) {
                    throw DataError("场景UUID不存在: " + uuid);
                }

                std::string scenePath = sceneIt->second;
                std::string sceneDataPath = scenePath + "/scene.dat";

                if (!m_dataLoader->directoryExists(scenePath)) {
                    throw DataError("场景目录不存在: " + scenePath);
                }

                auto scene = std::make_shared<Scene>(this,uuid);

                if (m_dataLoader->fileExists(sceneDataPath)) {
                    scene->load();
                } else {
                    scene->init();
                    scene->save();
                    scene->load();
                }

                m_loadScenes[uuid] = scene;

            } catch (const DataError& e) {
                throw;
            } catch (const std::exception& e) {
                throw DataError("加载场景失败: " + std::string(e.what()));
            }
        }
        bool saveScene(const std::string& uuid);
        std::string getCurrentSceneUuid() const;
        std::string getSceneNameByUuid(const std::string& uuid) const;
        DataLoader* getDataLoader() const;
        void setEditorMode(bool isEditorMode)
        {
            m_isEditorMode = isEditorMode;
        }
        std::string getModelPathByUuid(const std::string& uuid) const;
        std::string getModelRelativePathByUuid(const std::string& uuid) const;
        Scene* getCurrentScene() const;
    private:
        StatusOr<std::string> locateModelFile(const std::string& modelUuid) const;
        bool m_isEditorMode;
        std::string m_currentScene;
        std::unordered_map<std::string, std::shared_ptr<Scene>> m_loadScenes;
        std::vector<entt::registry*> m_currentRegistries;
        std::vector<std::string> m_scenePathList;
        std::unordered_map<std::string,std::string> m_uuidToModel;
        std::unordered_map<std::string,std::string> m_uuidToScenePath;
        std::unordered_map<std::string,std::string> m_uuidToSceneName;
        std::vector<std::string> m_modelPathList;
        std::unique_ptr<DataLoader> m_dataLoader;
        UniquePtr<ModelLoader> m_modelLoader;
        ProjectSetting m_projectSetting;

        void loadModelUuidMapping() {
            m_uuidToModel.clear();

            m_dataLoader->ensureDirectory("./res/models");

            try {
                std::vector<std::string> modelDirs = m_dataLoader->getSubDirectories("./res/models");

                for (const std::string& modelDir : modelDirs) {
                    std::string uuidFilePath = modelDir + "/model.uuid";

                    if (m_dataLoader->fileExists(uuidFilePath)) {
                        try {
                            auto inputStream = m_dataLoader->openBinaryInputStream(uuidFilePath);

                            if (inputStream && inputStream->is_open()) {
                                boost::archive::binary_iarchive archive(*inputStream);

                                ModelUuidFile modelUuidFile;
                                archive >> modelUuidFile;

                                m_uuidToModel[modelUuidFile.uuid] = modelDir;
                            }
                        }
                        catch (const DataError& e) {
                          throw;
                        }
                        catch (const std::exception& e) {

                        }
                    }
                }
            }
            catch (const DataError& e) {
                throw;
            }
            catch (const std::exception& e) {

            }
        }
        void loadSceneUuidMapping()
        {
            m_uuidToScenePath.clear();
            m_uuidToSceneName.clear();
            m_dataLoader->ensureDirectory("./res/scenes");

            try {
                std::vector<std::string> sceneDirs = m_dataLoader->getSubDirectories("./res/scenes");
                for (const std::string& sceneDir : sceneDirs) {
                    std::string uuidFilePath = sceneDir + "/scene.uuid";

                    if (m_dataLoader->fileExists(uuidFilePath)) {
                        try {
                            auto inputStream = m_dataLoader->openBinaryInputStream(uuidFilePath);

                            if (inputStream && inputStream->is_open()) {
                                boost::archive::binary_iarchive archive(*inputStream);

                                SceneUuidFile sceneUuidFile;
                                archive >> sceneUuidFile;

                                m_uuidToScenePath[sceneUuidFile.uuid] = sceneDir;
                                m_uuidToSceneName[sceneUuidFile.uuid] = std::filesystem::path(sceneDir).filename().string();
                            }
                        }
                        catch (const DataError& e) {
                            throw;
                        }
                        catch (const std::exception& e) {

                        }
                    }
                }
            }
            catch (const DataError& e) {
                throw;
            }
            catch (const std::exception& e) {

            }
        }
    };

}// namespace MQEngine

#endif //DATAMANAGER_H
