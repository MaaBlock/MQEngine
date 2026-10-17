//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENE_H
#define SCENE_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "SceneTrunk.h"

namespace MQEngine {
    class DataManager;
    
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
        std::vector<std::string> getTrunkList()
        {
            Status st = updateTrunkList();
            if (!st.ok()) {
                spdlog::warn("更新场景块列表失败: {}", st.message());
            }
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
        std::map<std::string,std::unique_ptr<SceneTrunk>> m_loadedSceneTrunks;
        UniquePtr<entt::registry> m_registry;
        std::string m_uuid;
    };

} // namespace MQEngine

#endif //SCENE_H


