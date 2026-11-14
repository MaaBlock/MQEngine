#ifndef MQENGINE_RESOURCE_TABLE_ENTITYKEY_H
#define MQENGINE_RESOURCE_TABLE_ENTITYKEY_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/resource/classdef.h>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine
{
    namespace Resource
    {
        // Note: EntityKey 全面统一为天然主键 FCT::ECS::entity (entt::entity)
        using EntityKey = FCT::ECS::entity;
    }
}

#endif // MQENGINE_RESOURCE_TABLE_ENTITYKEY_H
