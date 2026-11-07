//
// Created by MaaBlock on 2026/11/6.
//
#include "../core/Engine.h"
#include "Application.h"
namespace MQEngine
{
    namespace Runtime
    {
        void Application::registerInfoImpl()
        {
            m_engine = Engine::getInstance();
        }
    }
}