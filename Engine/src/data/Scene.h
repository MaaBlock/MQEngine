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
    
    class ENGINE_API Scene
    {
    public:

        
    private:
        std::vector<std::string> m_sceneTrunk;
        std::map<std::string,SceneTrunk*> m_loadedSceneTrunks;
    };
    
} // namespace MQEngine

#endif //SCENE_H
