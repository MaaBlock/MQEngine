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
        auto& tickerGraph = m_ctx->submitTickers();
        tickerGraph.removeNode(RenderGraphSubmitTickerName);
        tickerGraph.removeNode(RenderGraphExcutePassSubmitTickerName);
        tickerGraph["executeCmd"] = {
            [this]()
            {
                auto cmdBuf = m_ctx->getCmdBuf(m_wnd, 0);
                cmdBuf->reset();
                cmdBuf->begin();
                cmdBuf->end();
                cmdBuf->submit();
            },
            {},
            {SwapBufferSubmitTicker}
        };
        tickerGraph.update();
        m_ctx->create();
        m_wnd->bind(m_ctx);
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
