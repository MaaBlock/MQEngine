//
// Created by Administrator on 2025/8/20.
//

#ifndef SCENETRUNK_H
#define SCENETRUNK_H

#include "../EnginePCH.h"
#include "../thirdparty/thirdparty.h"
#include "Component.h"

namespace MQEngine {
    class Scene;

    class ENGINE_API SceneTrunk
    {
    public:
        SceneTrunk(std::string name, Scene* scene);
        void init();
        Status save();
        Status load();
        entt::registry* getRegistry() { return m_registry.get(); }
    private:
        std::string buildTrunksPath() const;
        Status saveTrunkConfig(const std::string& trunkDataPath);
        Status saveTrunkRegistry(const std::string& trunkRegistryPath);
        Status loadTrunkConfig(const std::string& trunkDataPath);
        Status loadTrunkRegistry(const std::string& trunkRegistryPath);

        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & m_name;
        }
        UniquePtr<entt::registry> m_registry;
        std::string m_name;
        Scene* m_scene;
    };

} // namespace MQEngine

#endif //SCENETRUNK_H


