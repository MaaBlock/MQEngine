//
// Created by MaaBlock on 2025/10/25.
//
#include "ResourceActiveSystem.h"
#include "../data/DataManager.h"
#include "../data/Component.h"

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
        CacheResource* resource = nullptr;
        while (m_activeQueue.try_dequeue(resource))
        {
            if (resource)
            {
                resource->visible = true;
            }
        }
    }

    void ResourceActiveSystem::requestActive(CacheResource* resource)
    {
        if (resource)
        {
            m_activeQueue.enqueue(resource);
        }
    }
} // MQEngine