/**
* @file systemmanager.cpp
* @brief class source for start and stop system
*/
#include "../engineapi.h"

namespace MQEngine
{
    void SystemManager::init()
    {
        NodeCommon::Init();
    }

    void SystemManager::term()
    {
        NodeCommon::Term();
    }
}
