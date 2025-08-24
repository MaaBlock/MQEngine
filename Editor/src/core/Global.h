//
// Created by Administrator on 2025/8/18.
//

#ifndef GLOBAL_H
#define GLOBAL_H
#include "../Thirdparty/thirdparty.h"

namespace MQEngine{
    struct Global
    {
        FCT::Window* wnd;
        FCT::Context* ctx;
        FCT::ImguiModule* imguiModule;
        DataManager* dataManager;
        FCT::Runtime* rt;
        entt::registry* editorRegistry;
        FCT::ImguiContext* imguiContext;
    };
    extern Global g_global;
}
#endif //GLOBAL_H
