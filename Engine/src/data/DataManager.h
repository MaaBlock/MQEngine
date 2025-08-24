//
// Created by Administrator on 2025/8/20.
//

#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
#include "DataError.h"
#include "Scene.h"
#include "SceneTrunk.h"
#include <unordered_map>
#include <memory>
#include <functional>

#include "./DataLoader.h"
#include "./FileDataLoader.h"
#include "./Camera.h"

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

    class ENGINE_API DataManager
    {
    public:
        DataManager()
        {
            m_dataLoader = std::make_unique<FileDataLoader>();
        }
        std::vector<std::string> getModelList()
        {
            loadModelPathList();
            return m_modelPathList;
        }
        void loadRes()
        {
            m_dataLoader->ensureDirectory("./res");
            loadScenePathList();
            loadModelPathList();
        }
        void loadScenePathList()
        {
            m_dataLoader->ensureDirectory("./res/scenes");
            m_scenePathList = m_dataLoader->getSubDirectories("./res/scenes");
            loadSceneUuidMapping();
        }
        void newScene(const std::string& sceneName)
        {
            m_dataLoader->createDirectory("./res/scenes/" + sceneName);
        }
        void loadModelPathList()
        {
            m_dataLoader->ensureDirectory("./res/models");
            m_modelPathList = m_dataLoader->getSubDirectories("./res/models");
            loadModelUuidMapping();
        }
        std::vector<entt::registry*> currentRegistries() const
        {
            return m_currentRegistries;
        }
        void appendRegistry(entt::registry* registry)
        {
            m_currentRegistries.push_back(registry);
        }
        void removeRegistry(entt::registry* registry)
        {
            m_currentRegistries.erase(std::remove(m_currentRegistries.begin(), m_currentRegistries.end(), registry), m_currentRegistries.end());
        }

        void openScene(const std::string& uuid) {
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
                    //scenePtr.onOpen();

                }

            } catch (const DataError& e) {
                throw;
            } catch (const std::exception& e) {
                throw DataError("打开场景失败: " + std::string(e.what()));
            }
        }
        void closeScene(const std::string& uuid) {
            //scenePtr.onClose();
        }


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
        Scene* getCurrentScene() const;
    private:
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
        void loadModelUuidMapping() {
            m_uuidToModel.clear();

            m_dataLoader->ensureDirectory("./res/models");

            try {
                std::vector<std::string> modelDirs = m_dataLoader->getSubDirectories("./res/models");

                for (const std::string& modelDir : modelDirs) {
                    std::string uuidFilePath = "./res/models/" + modelDir + "/model.uuid";

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
