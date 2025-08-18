//
// Created by Administrator on 2025/8/18.
//

#ifndef GLOBAL_H
#define GLOBAL_H
namespace MQEngine{
    struct Global
    {
        FCT::Window* wnd;
        FCT::Context* ctx;
        FCT::ImguiModule* imguiModule;
    };
    extern Global g_global;
}
#endif //GLOBAL_H
