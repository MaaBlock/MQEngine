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

        m_imguiPass = m_ctx->createPass();
        m_imguiPass->enableClear(ClearType::color | ClearType::depthStencil,
            Vec4(1,1,1,1));
        m_imguiPass->bindTarget(0,m_wnd->getCurrentTarget()->targetImage());
        m_defaultPassGroup = m_ctx->createPassGroup();
        m_defaultPassGroup->addPass(m_imguiPass);
        m_defaultPassGroup->create();

        m_imguiCtx = m_imguiModule.createContext(m_wnd,m_ctx);
        m_imguiCtx->create(m_imguiPass);

        //setting up ticker graph
        auto& tickerGraph = m_ctx->submitTickers();
        tickerGraph.removeNode(RenderGraphSubmitTickerName);
        tickerGraph.removeNode(RenderGraphExcutePassSubmitTickerName);
        tickerGraph["executeCmd"] = {
            [this]()
            {
                auto cmdBuf = m_ctx->getCmdBuf(m_wnd, 0);
                cmdBuf->reset();
                cmdBuf->begin();
                m_defaultPassGroup->beginSubmit(cmdBuf);
                m_imguiPass->beginSubmit(cmdBuf);
                m_imguiCtx->submit(cmdBuf);
                m_imguiPass->endSubmit();
                m_defaultPassGroup->endSubmit(cmdBuf);
                cmdBuf->end();
                cmdBuf->submit();
            },
            {},
            {SwapBufferSubmitTicker}
        };
        tickerGraph.update();

        auto &syncGraph = m_ctx->syncTickers();
        syncGraph.removeNode(RenderGraphSyncTicker_SwapJobQueueName);
        syncGraph["syncImgui"] = {
            [this]()
            {
                m_imguiCtx->swapBuffer();
            },
            {},
            {}
        };
        syncGraph.update();

    }

    void Engine::loop()
    {
        while (m_wnd->isRunning())
        {
            m_imguiCtx->push([]()
            {
                ImGui::Begin("MQ Engine");
                ImGui::Text("Version: %s", getEngineVersion());
                ImGui::End();
            });
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
