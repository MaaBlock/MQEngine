#ifndef MQENGINE_PHYSICS_CORE_SIMULATOR_H
#define MQENGINE_PHYSICS_CORE_SIMULATOR_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../system/PhysicsSystem.h"

namespace MQEngine
{
    namespace Physics
    {
        class ENGINE_API Simulator : public CoreEngineModule<Simulator>
        {
        public:
            Simulator();
            ~Simulator();

            Simulator(const Simulator&) = delete;
            Simulator& operator=(const Simulator&) = delete;

            void registerInfo();
            Status init();
            void term();

            PhysicsSystem* getPhysicsSystem() const noexcept { return physicsSystem.get(); }

        public:
            UniquePtr<PhysicsSystem> physicsSystem;

        private:
            bool m_initialized = false;
        };

    } // namespace Physics
} // namespace MQEngine

#endif // MQENGINE_PHYSICS_CORE_SIMULATOR_H
