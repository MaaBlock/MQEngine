//
// Created by MaaBlock on 2026/11/6.
//

#ifndef MQENGINE_RUNTIME_THIRDPARTY_H
#define MQENGINE_RUNTIME_THIRDPARTY_H
#include "../../thirdparty/thirdparty.h"

#ifndef ENGINE_CLI_OPTION
#define ENGINE_CLI_OPTION(Type, name, default_value, help) \
    ABSL_FLAG(Type, name, default_value, help)
#endif

namespace MQEngine
{
    namespace Runtime
    {
        using PlatformRuntime = FCT::Runtime;
    }
}
#endif // MQENGINE_RUNTIME_THIRDPARTY_H
