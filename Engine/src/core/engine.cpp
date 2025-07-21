//
// Created by Administrator on 2025/7/21.
//
#include "../engineapi.h"
namespace MQEngine
{
    Engine* Engine::s_instance = nullptr;
    const char* getEngineVersion()
    {
        return "0.0.1";
    }
    Engine& getEngine() {
        return Engine::getInstance();
    }

    void Engine::init()
    {
        m_systemManager.init();
    }

    void Engine::loop()
    {
    }

    void Engine::term()
    {
        m_systemManager.term();
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }
}
