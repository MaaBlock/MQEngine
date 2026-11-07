#ifndef MQENGINE_RUNTIME_CORE_HOST_H
#define MQENGINE_RUNTIME_CORE_HOST_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../classdef.h"
#include "../manager/LogicTickManager.h"
#include "../manager/PluginManager.h"
#include "../manager/SystemManager.h"
#include "../manager/UserCodeManager.h"
#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief Runtime 模块在 Engine 中的值内嵌聚合容器（Host）：
         *        统一持有并暴露 SystemManager、PluginManager、UserCodeManager 与 LogicTickManager 子模块
         */
        class ENGINE_API Host : public CoreEngineModule<Host>
        {
        public:
            Host();
            ~Host();

            Host(const Host&) = delete;
            Host& operator=(const Host&) = delete;

            void registerInfo();
            Status init();
            void term();

            SystemManager* getSystemManager() const noexcept { return const_cast<SystemManager*>(&systemManager); }
            PluginManager* getPluginManager() const noexcept { return const_cast<PluginManager*>(&pluginManager); }
            UserCodeManager* getUserCodeManager() const noexcept { return const_cast<UserCodeManager*>(&userCodeManager); }
            LogicTickManager* getLogicTickManager() const noexcept { return const_cast<LogicTickManager*>(&logicTickManager); }

        public:
            SystemManager systemManager;
            PluginManager pluginManager;
            UserCodeManager userCodeManager;
            LogicTickManager logicTickManager;

        private:
            bool m_initialized = false;
        };

    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_RUNTIME_CORE_HOST_H
