#ifndef MQENGINE_HOST_CORE_CONFIG_H
#define MQENGINE_HOST_CORE_CONFIG_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief Host 模块运行配置类
         */
        class ENGINE_API Config
        {
        public:
            struct HasMaxFramesFlag
            {
                bool value = false;

                constexpr operator bool() const noexcept { return value; }
                constexpr bool operator()() const noexcept { return value; }
                constexpr HasMaxFramesFlag& operator=(bool v) noexcept
                {
                    value = v;
                    return *this;
                }
            };

            bool headless = false;
            HasMaxFramesFlag hasMaxFrames;
            int32_t maxFrames = 0;

            bool isHeadless() const noexcept { return headless; }
            void setHeadless(bool value) noexcept { headless = value; }

            int32_t getMaxFrames() const noexcept { return maxFrames; }
            void setMaxFrames(int32_t frames) noexcept
            {
                maxFrames = frames;
                hasMaxFrames = true;
            }

            bool getHasMaxFrames() const noexcept { return hasMaxFrames.value; }
            void setHasMaxFrames(bool value) noexcept { hasMaxFrames = value; }
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_CORE_CONFIG_H
