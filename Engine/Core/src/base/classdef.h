#ifndef MQENGINE_BASE_CLASSDEF_H
#define MQENGINE_BASE_CLASSDEF_H

#include "./CoreEngineManager.h"
#include "./CoreEngineModule.h"
#include "./StaticTokenGraph.h"

namespace MQEngine
{
    enum class ConfigFormat;
    class ConfigNode;
    class ConfigDocument;
    struct ProjectConfig;
    struct BaseModuleConfig;
    struct RenderModuleConfig;
    struct CLIOptionItem;
    class ICLIOptionHandler;
    class EngineCLIOption;
    class SingleQueueEventSystem;
    class JobSystem;
    class MemoryManager;

    namespace Base
    {
        class Foundation;
        template <typename... ModuleOrConfigs>
        class EngineCoreConfig;
    }
} // namespace MQEngine

#endif // MQENGINE_BASE_CLASSDEF_H
