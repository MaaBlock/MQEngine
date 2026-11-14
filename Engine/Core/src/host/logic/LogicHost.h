#ifndef MQENGINE_HOST_MANAGER_LOGICHOST_H
#define MQENGINE_HOST_MANAGER_LOGICHOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/host/classdef.h>
#include <cstdint>
#include <functional>
#include <string>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief 逻辑宿主（LogicHost）：负责通过 FCT::TokenGraph 拓扑驱动主逻辑帧节点执行与主循环退出控制
         */
        class ENGINE_API LogicHost : public CoreEngineManager<LogicHost>
        {
        public:
            using LogicTicker = std::function<void()>;
            using TickerGraph = FCT::TokenGraph<std::string, LogicTicker>;

            LogicHost() = default;
            ~LogicHost() = default;

            void registerInfo();
            Status init();
            void term();

            void sync();

            TickerGraph& tickers() noexcept { return m_tickers; }
            const TickerGraph& tickers() const noexcept { return m_tickers; }

            void requestExit() noexcept { m_requestExit = true; }
            void resetExitRequest() noexcept
            {
                m_requestExit = false;
                m_loopRunning = true;
            }
            bool isExitRequested() const noexcept { return m_requestExit; }
            bool loopRunning() const noexcept { return m_loopRunning; }

            uint64_t getFrameCount() const noexcept { return m_frameCount; }

        private:
            bool m_loopRunning = true;
            bool m_requestExit = false;
            uint64_t m_frameCount = 0;
            TickerGraph m_tickers;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_LOGICHOST_H
