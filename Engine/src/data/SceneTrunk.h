//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENETRUNK_H
#define SCENETRUNK_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "DataError.h"
#include "Component.h"
#include <fstream>
#include <filesystem>

namespace MQEngine
{
    class Scene;
}

namespace MQEngine {
    
    class ENGINE_API SceneTrunk
    {
    public:
        SceneTrunk(std::string name,Scene* scene);
        void init();
        void save();
        void load();
        entt::registry& getRegistry() { return m_registry; }
    private:
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & m_name;
        }
        entt::registry m_registry;
        std::string m_name;
        Scene* m_scene;
    };

} // namespace MQEngine

#endif //SCENETRUNK_H

