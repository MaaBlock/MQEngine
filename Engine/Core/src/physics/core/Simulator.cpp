#include "Simulator.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"

namespace MQEngine
{
    namespace Physics
    {
        Simulator::Simulator() = default;

        Simulator::~Simulator()
        {
            term();
        }

        void Simulator::registerInfo()
        {
            physicsSystem = makeUnique<PhysicsSystem>(g_engineGlobal.dataManager);
            g_engineGlobal.physicsSystem = physicsSystem.get();
        }

        Status Simulator::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            assert(physicsSystem != nullptr && "PhysicsSystem 不能为空");

            physicsSystem->init();
            g_engineGlobal.physicsSystem = physicsSystem.get();
            engine->host->getSystemManager()->requestAddSystem("PhysicsSystem", physicsSystem.get());

            m_initialized = true;
            return OkStatus();
        }

        void Simulator::term()
        {
            if (!m_initialized && !physicsSystem) {
                return;
            }
            m_initialized = false;

            if (g_engineGlobal.physicsSystem == physicsSystem.get()) {
                g_engineGlobal.physicsSystem = nullptr;
            }
            physicsSystem.reset();
        }

    } // namespace Physics
} // namespace MQEngine
