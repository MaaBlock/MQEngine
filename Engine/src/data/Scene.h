//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENE_H
#define SCENE_H

#include "../EnginePCH.h"
#include "../Thirdparty/thirdparty.h"
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
