#ifndef MQENGINE_HOST_CORE_HOST_H
#define MQENGINE_HOST_CORE_HOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/host/classdef.h>
#include <MQEngine/host/core/Config.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/host/physics/PhysicsHost.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/host/work/WorkHost.h>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief Host 模块顶层门面类：统一聚合与协调五大宿主（PhysicsHost、LogicHost、RenderHost、UIHost、WorkHost）及 Host::Config 与平台运行时
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
            void term() noexcept;

            Config* getConfig() const noexcept { return const_cast<Config*>(&config); }
            PhysicsHost* getPhysicsHost() const noexcept { return const_cast<PhysicsHost*>(&physicsHost); }
            LogicHost* getLogicHost() const noexcept { return const_cast<LogicHost*>(&logicHost); }
            RenderHost* getRenderHost() const noexcept { return const_cast<RenderHost*>(&renderHost); }
            UIHost* getUIHost() const noexcept { return const_cast<UIHost*>(&uiHost); }
            WorkHost* getWorkHost() const noexcept { return const_cast<WorkHost*>(&workHost); }
            FCT::Runtime* getFctRuntime() const noexcept { return m_fctRuntime.get(); }

        public:
            Config config;
            PhysicsHost physicsHost;
            LogicHost logicHost;
            RenderHost renderHost;
            UIHost uiHost;
            WorkHost workHost;

        private:
            UniquePtr<FCT::Runtime> m_fctRuntime;
            bool m_initialized = false;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_CORE_HOST_H
