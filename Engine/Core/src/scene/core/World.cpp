#include "World.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include <cassert>

namespace MQEngine
{
    namespace Scene
    {
        World::World() = default;

        World::~World()
        {
            term();
        }

        void World::registerInfo()
        {
            registriesManager.registerInfo();
            dataManager.registerInfo();

            g_engineGlobal.registriesManager = &registriesManager;
            g_engineGlobal.dataManager = &dataManager;
        }

        Status World::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");

            g_engineGlobal.registriesManager = &registriesManager;
            g_engineGlobal.dataManager = &dataManager;

            assert(engine->resource->getResourceActiveSystem() != nullptr && "ResourceActiveSystem 不能为空");
            engine->resource->getResourceActiveSystem()->setDataManager(&dataManager);

            if (!engine->resource->getAssetRoot().empty()) {
                dataManager.setAssetRoot(engine->resource->getAssetRoot());
            }
            if (engine->foundation->isProjectOpen()) {
                const auto& cfg = engine->foundation->getCurrentConfig();
                if (!cfg.initialSceneUuid.empty()) {
                    dataManager.setInitialSceneUuid(cfg.initialSceneUuid);
                }
            }
            dataManager.loadRes();

            m_initialized = true;
            return OkStatus();
        }

        void World::term()
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;

            for (entt::registry* reg : registriesManager.currentRegistries()) {
                if (reg) {
                    reg->clear();
                }
            }
            dataManager.term();
            if (g_engineGlobal.dataManager == &dataManager) {
                g_engineGlobal.dataManager = nullptr;
            }
            if (g_engineGlobal.registriesManager == &registriesManager) {
                g_engineGlobal.registriesManager = nullptr;
            }
        }
    } // namespace Scene
} // namespace MQEngine
