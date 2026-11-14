#ifndef MQENGINE_RESOURCE_CONFIG_ENGINECONFIG_H
#define MQENGINE_RESOURCE_CONFIG_ENGINECONFIG_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/config/Config.h>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief EngineConfig 类型别名与辅助门面，与 Resource::Config 保持完全兼容
         */
        using EngineConfig = Config;

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CONFIG_ENGINECONFIG_H
