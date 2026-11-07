//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENE_H
#define SCENE_H

#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "SceneTrunk.h"

namespace MQEngine {
    class DataManager;

    namespace Scene {
        class ENGINE_API Scene
        {
        public:
            Scene(DataManager* dataManager, const std::string& uuid);
            void init();
            Status saveTrunk(const std::string& trunkName);
            Status loadTrunk(const std::string& trunkName);
            Status save();
            Status load();
            void onLoad()
            {
                if (m_isChunked)
                {

                } else
                {

                }
            }
            std::string getName();
            Status updateTrunkList();
            DataManager* getDataManager() { return m_dataManager; }
            const std::vector<std::string>& getTrunkList() const
            {
                return m_sceneTrunk;
            }
            bool isLoad(std::string trunkName)
            {
                return m_loadedSceneTrunks.find(trunkName) != m_loadedSceneTrunks.end();
            }
            entt::registry* getRegistry() { return m_registry.get(); }
            SceneTrunk* getLoadedTrunk(const std::string& trunkName)
            {
                auto it = m_loadedSceneTrunks.find(trunkName);
                if (it != m_loadedSceneTrunks.end())
                {
                    return it->second.get();
                }
                return nullptr;
            }
        private:
            std::string buildScenePath();
            Status saveSceneConfig(const std::string& sceneDataPath);
            Status saveSceneRegistry(const std::string& registryDataPath);
            Status saveLoadedTrunks();
            Status loadSceneConfig(const std::string& sceneDataPath);
            Status loadSceneRegistry(const std::string& registryDataPath);
            Status loadDefaultTrunks();

            friend class boost::serialization::access;
            template<class Archive>
            void serialize(Archive & ar, const unsigned int version) {
                ar & m_isChunked;
            }
            DataManager* m_dataManager = nullptr;
            bool m_isChunked = false;
            std::vector<std::string> m_sceneTrunk;
            std::map<std::string, UniquePtr<SceneTrunk>> m_loadedSceneTrunks;
            UniquePtr<entt::registry> m_registry;
            std::string m_uuid;
        };
    } // namespace Scene
} // namespace MQEngine

#endif //SCENE_H
