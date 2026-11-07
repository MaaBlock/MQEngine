//
// Created by MaaBlock on 2026/11/7.
//

#ifndef MQENGINE_LOGICTICKMANAGER_H
#define MQENGINE_LOGICTICKMANAGER_H

#include "../../EnginePCH.h"
#include "../../base/CoreEngineManager.h"
#include "../classdef.h"

namespace MQEngine
{
    namespace Runtime
    {
        /**
         * @brief 逻辑帧调度与退出控制管理器（内嵌于 Host 模块）
         */
        class ENGINE_API LogicTickManager : public CoreEngineManager<LogicTickManager>
        {
        public:
            LogicTickManager() = default;
            ~LogicTickManager() = default;

            void registerInfo();
            void syncTicker();

            void requestExit() noexcept;
            void resetExitRequest() noexcept;
            bool isExitRequested() const noexcept;

        private:
            bool m_requestExit = false;
        };

    } // namespace Runtime
} // namespace MQEngine

#endif // MQENGINE_LOGICTICKMANAGER_H
