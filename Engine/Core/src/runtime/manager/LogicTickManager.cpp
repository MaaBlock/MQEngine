//
// Created by MaaBlock on 2026/11/7.
//

#include "LogicTickManager.h"

namespace MQEngine
{
    namespace Runtime
    {
        void LogicTickManager::registerInfo()
        {
            m_requestExit = false;
        }

        void LogicTickManager::syncTicker()
        {
        }

        void LogicTickManager::requestExit() noexcept
        {
            m_requestExit = true;
        }

        void LogicTickManager::resetExitRequest() noexcept
        {
            m_requestExit = false;
        }

        bool LogicTickManager::isExitRequested() const noexcept
        {
            return m_requestExit;
        }

    } // namespace Runtime
} // namespace MQEngine
