#ifndef MQENGINE_CLASSDEF_H
#define MQENGINE_CLASSDEF_H

namespace MQEngine
{
    class SystemManager;

    namespace Runtime
    {
        enum class PluginScope;
        enum class PluginState;
        enum class UserCodeState;

        class Runtime;
        class Engine;
        class Host;
        class Application;
        class Composite;
        class IPlugin;
        class NativeCode;

        struct CompositeEntry;
        struct PluginEntry;
        class PluginManager;

        struct UserCodeEntry;
        class UserCodeManager;
        class LogicTickManager;
    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_CLASSDEF_H
