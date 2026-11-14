#ifndef MQENGINE_HOST_MANAGER_PHYSICSHOST_H
#define MQENGINE_HOST_MANAGER_PHYSICSHOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/host/classdef.h>
#include <functional>
#include <string>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief 物理宿主（PhysicsHost）：内部通过 FCT::TokenGraph 驱动同步，具体物理仿真逻辑由上层模块注册实现
         */
        class ENGINE_API PhysicsHost : public CoreEngineManager<PhysicsHost>
        {
        public:
            using PhysicsTicker = std::function<void()>;
            using TickerGraph = FCT::TokenGraph<std::string, PhysicsTicker>;

            PhysicsHost() = default;
            ~PhysicsHost() = default;

            void registerInfo();
            Status init();
            void term();

            void sync();

            TickerGraph& tickers() noexcept { return m_tickers; }
            const TickerGraph& tickers() const noexcept { return m_tickers; }

        private:
            TickerGraph m_tickers;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_PHYSICSHOST_H
