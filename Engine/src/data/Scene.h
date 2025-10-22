//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENE_H
#define SCENE_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "DataError.h"
#include "SceneTrunk.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>

namespace MQEngine {
    class DataManager;
    
    class ENGINE_API Scene
    {
    public:
        Scene(DataManager* dataManager, const std::string& uuid);
        void init();
        void saveTrunk(const std::string& trunkName);
        void loadTrunk(const std::string& trunkName);
        void save();
        void load();
        void onLoad()
        {
            if (m_isChunked)
            {

            } else
            {

            }
        }
        std::string getName();
        void updateTrunkList();
        DataManager* getDataManager() { return m_dataManager; }
        std::vector<std::string> getTrunkList()
        {
            updateTrunkList();
            return m_sceneTrunk;
        }
        bool isLoad(std::string trunkName)
        {
            return m_loadedSceneTrunks.find(trunkName)!= m_loadedSceneTrunks.end();
        }
        entt::registry& getRegistry() { return m_registry; }
        SceneTrunk* getLoadedTrunk(const std::string& trunkName)
        {
            auto it = m_loadedSceneTrunks.find(trunkName);
            if (it!= m_loadedSceneTrunks.end())
            {
                return it->second.get();
            }
            return nullptr;
        }
    private:
        friend class boost::serialization::access;
        template<class Archive>
        void serialize(Archive & ar, const unsigned int version) {
            ar & m_isChunked;
        }
        DataManager* m_dataManager = nullptr;
        bool m_isChunked = false;
        std::vector<std::string> m_sceneTrunk;
        std::map<std::string,std::unique_ptr<SceneTrunk>> m_loadedSceneTrunks;
        entt::registry m_registry;
        std::string m_uuid;
    };

} // namespace MQEngine

#endif //SCENE_H

