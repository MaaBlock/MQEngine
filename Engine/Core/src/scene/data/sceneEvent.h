//
// Created by Administrator on 2025/8/25.
//

#ifndef SCENEEVENT_H
#define SCENEEVENT_H
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
namespace MQEngine
{
    class SceneTrunk;
    namespace SceneEvent
    {
        struct Load
        {
            std::string uuid;
            Scene::Scene* scene;
        };
        struct Unload
        {
            std::string uuid;
            Scene::Scene* scene;
        };
        struct LoadTrunk
        {
            std::string uuid;
            std::string trunkName;
            Scene::Scene* scene;
            SceneTrunk* trunk;
        };
        struct UnloadTrunk
        {
            std::string uuid;
            std::string trunkName;
            Scene::Scene* scene;
            SceneTrunk* trunk;
        };
        struct EntryScene
        {
            std::string uuid;
            Scene::Scene* scene;
        };
        struct ExitScene
        {
            std::string uuid;
            Scene::Scene* scene;
        };
    }
}
#endif //SCENEEVENT_H
