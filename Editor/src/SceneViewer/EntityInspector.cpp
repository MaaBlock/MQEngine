//
// Created by Administrator on 2025/8/25.
//

#include "EntityInspector.h"

namespace MQEngine {
    EntityInspector::EntityInspector()
    {

    }

    void EntityInspector::inspectEntity(Scene* scene, const std::string& trunk, entt::entity entity)
    {
        m_currentEntityScene = scene;
        m_currentEntityTrunk = trunk;
        m_currentEntity = entity;
    }
    void EntityInspector::render()
    {

    }
} // MQEngine