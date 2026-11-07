#include "Repository.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include <cassert>

namespace MQEngine
{
    namespace Resource
    {
        Repository::Repository() = default;

        Repository::~Repository()
        {
            term();
        }

        void Repository::registerInfo()
        {
            resourceLoader.registerInfo();
            resourceActiveSystem = makeUnique<ResourceActiveSystem>(nullptr);

            g_engineGlobal.resourceLoader = &resourceLoader;
            g_engineGlobal.resourceActiveSystem = resourceActiveSystem.get();
        }

        Status Repository::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = Runtime::Engine::getInstance();
            assert(engine != nullptr && "Engine 实例不能为空");
            assert(resourceActiveSystem != nullptr && "ResourceActiveSystem 不能为空");

            g_engineGlobal.resourceLoader = &resourceLoader;
            g_engineGlobal.resourceActiveSystem = resourceActiveSystem.get();

            if (engine->foundation->isProjectOpen() &&
                !engine->foundation->getAssetRoot().empty()) {
                m_assetRoot = engine->foundation->getAssetRoot();
            } else {
                m_assetRoot = "./Assets";
            }

            resourceActiveSystem->setDataManager(engine->scene->getDataManager());
            engine->host->getSystemManager()->requestAddSystem("ResourceActiveSystem", resourceActiveSystem.get());

            m_initialized = true;
            return OkStatus();
        }

        void Repository::term()
        {
            if (!m_initialized && !resourceActiveSystem) {
                return;
            }
            m_initialized = false;

            if (g_engineGlobal.resourceActiveSystem == resourceActiveSystem.get()) {
                g_engineGlobal.resourceActiveSystem = nullptr;
            }
            resourceActiveSystem.reset();

            if (g_engineGlobal.resourceLoader == &resourceLoader) {
                g_engineGlobal.resourceLoader = nullptr;
            }
        }

    } // namespace Resource
} // namespace MQEngine
