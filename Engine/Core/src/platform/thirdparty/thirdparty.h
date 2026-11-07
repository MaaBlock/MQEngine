//
// Created by MaaBlock on 2026/11/6.
//

#ifndef MQENGINE_PLATFORM_THIRDPARTY_H
#define MQENGINE_PLATFORM_THIRDPARTY_H
#include <FCT/Runtime/runtime.h>
namespace MQEngine
{
    namespace Platform
    {
        using Runtime = FCT::Runtime;
        using Context = FCT::Context;
        using Window = FCT::Window;
        namespace WindowModule
        {
            using namespace FCT::WindowModule;
        }
    }
}


#endif // MQENGINE_THIRDPARTY_H
