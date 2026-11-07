#ifndef MQENGINE_SCENE_CORE_WORLD_H
#define MQENGINE_SCENE_CORE_WORLD_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../manager/DataManager.h"
#include "../manager/RegistriesManager.h"

namespace MQEngine
{
    namespace Scene
    {
        class ENGINE_API World : public CoreEngineModule<World>
        {
        public:
            World();
            ~World();

            World(const World&) = delete;
            World& operator=(const World&) = delete;

            void registerInfo();
            Status init();
            void term();

            RegistriesManager* getRegistriesManager() const noexcept { return const_cast<RegistriesManager*>(&registriesManager); }
            DataManager* getDataManager() const noexcept { return const_cast<DataManager*>(&dataManager); }

        public:
            RegistriesManager registriesManager;
            DataManager dataManager;

        private:
            bool m_initialized = false;
        };
    } // namespace Scene
} // namespace MQEngine

#endif // MQENGINE_SCENE_CORE_WORLD_H
