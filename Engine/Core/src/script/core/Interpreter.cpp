#include "Interpreter.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../scene/manager/DataManager.h"

namespace MQEngine
{
    namespace Script
    {
        Interpreter::Interpreter() = default;

        Interpreter::~Interpreter()
        {
            term();
        }

        void Interpreter::registerInfo()
        {
            scriptSystem = makeUnique<ScriptSystem>();
            scriptCacheSystem = makeUnique<ScriptCacheSystem>(g_engineGlobal.dataManager, scriptSystem.get());

            g_engineGlobal.scriptSystem = scriptSystem.get();
            g_engineGlobal.scriptCacheSystem = scriptCacheSystem.get();
        }

        Status Interpreter::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            assert(scriptSystem != nullptr && "ScriptSystem 不能为空");
            assert(scriptCacheSystem != nullptr && "ScriptCacheSystem 不能为空");

            g_engineGlobal.scriptSystem = scriptSystem.get();
            g_engineGlobal.scriptCacheSystem = scriptCacheSystem.get();

            scriptSystem->init();

            DataManager* dm = engine->scene->getDataManager();
            assert(dm != nullptr && "DataManager 不能为空");
            dm->updateResource();

            engine->host->getSystemManager()->requestAddSystem("ScriptCacheSystem", scriptCacheSystem.get());
            engine->host->getSystemManager()->requestSetSystemEnabled("ScriptCacheSystem", false);

            m_initialized = true;
            return OkStatus();
        }

        void Interpreter::term()
        {
            if (!m_initialized && !scriptSystem && !scriptCacheSystem) {
                return;
            }
            m_initialized = false;

            if (auto* engine = Runtime::Engine::getInstance()) {
                engine->scene->dataManager.shutdownWatchers();
            }

            if (scriptSystem) {
                scriptSystem->cleanUp();
                if (g_engineGlobal.scriptSystem == scriptSystem.get()) {
                    g_engineGlobal.scriptSystem = nullptr;
                }
                scriptSystem.reset();
            }

            if (g_engineGlobal.scriptCacheSystem == scriptCacheSystem.get()) {
                g_engineGlobal.scriptCacheSystem = nullptr;
            }
            scriptCacheSystem.reset();
        }

    } // namespace Script
} // namespace MQEngine
