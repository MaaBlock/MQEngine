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
        m_wnd = m_rt.createWindow(800,600,"MQ Engine");
        m_ctx = m_rt.createContext();
        m_ctx->create();
        m_wnd->bind(m_ctx);
        m_ctx->compilePasses();
    }

    void Engine::loop()
    {
        while (m_wnd->isRunning())
        {
            m_ctx->flush();
        }
    }

    void Engine::term()
    {
        m_ctx->release();
        m_wnd->release();
        m_systemManager.term();
    }

    Engine& Engine::getInstance() {
        return *s_instance;
    }
}
