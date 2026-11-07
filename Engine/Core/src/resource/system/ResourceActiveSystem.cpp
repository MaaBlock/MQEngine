//
// Created by MaaBlock on 2025/10/25.
//
#include "ResourceActiveSystem.h"
#include "../../scene/manager/DataManager.h"
#include "../../scene/data/Component.h"

namespace MQEngine {
    ResourceActiveSystem::ResourceActiveSystem(DataManager* dataManager)
        : m_dataManager(dataManager)
    {
    }

    void ResourceActiveSystem::updateLogic()
    {

    }

    void ResourceActiveSystem::updateRender()
    {
        ZoneScoped();
        bool* visibleFlag = nullptr;
        while (m_activeQueue.try_dequeue(visibleFlag))
        {
            if (visibleFlag)
            {
                *visibleFlag = true;
            }
        }
    }

    void ResourceActiveSystem::requestActive(CacheResource* resource)
    {
        if (resource)
        {
            requestActive(&resource->visible);
        }
    }

    void ResourceActiveSystem::requestActive(bool* visibleFlag)
    {
        if (visibleFlag)
        {
            m_activeQueue.enqueue(visibleFlag);
        }
    }
} // MQEngine