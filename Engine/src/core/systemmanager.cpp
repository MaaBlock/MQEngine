/**
* @file systemmanager.cpp
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
