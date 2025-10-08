//
// Created by MaaBlock on 2025/9/27.
//

#ifndef GRAPHICSENV_H
#define GRAPHICSENV_H
#include "application.h"

namespace FCT
{
    class Runtime;
    class Context;
    class Window;
}
namespace MQEngine
{
    class GraphicsEnv
    {
    public:
        GraphicsEnv(FCT::Runtime& rt,RenderConfig renderConfig) : m_rt(rt)
        {
            m_ctx = rt.createContext();
            m_wnd = rt.createWindow(800, 600, renderConfig.windowTitle);
            m_ctx->create();
            m_wnd->bind(m_ctx);
        }
        ~GraphicsEnv()
        {
            m_ctx->release();
            m_wnd->release();
        }
        FCT::Window* wnd() const { return m_wnd; }
        FCT::Context* ctx() const { return m_ctx; }
    private:
        FCT::Runtime& m_rt;
        FCT::Context* m_ctx;
        FCT::Window* m_wnd;
    };
}
#endif //GRAPHICSENV_H
