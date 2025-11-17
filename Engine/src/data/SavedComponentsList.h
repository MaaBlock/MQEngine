#ifndef SAVEDCOMPONENTSLIST_H
#define SAVEDCOMPONENTSLIST_H
#include "Component.h"
namespace MQEngine {
    template<typename SnapshotType, typename WrapperType>
    void SerializeComponents(SnapshotType&& snapshot, WrapperType& wrapper) {
        snapshot
        .template get<entt::entity>(wrapper)
        .template get<NameTag>(wrapper)
        .template get<StaticMeshInstance>(wrapper)
        .template get<OrmTextureComponent>(wrapper)
        .template get<AlbedoTextureComponent>(wrapper)
        .template get<NormalTextureComponent>(wrapper)
        .template get<DirectionalLightComponent>(wrapper)
        .template get<DiffuseTextureComponent>(wrapper)
        .template get<PositionComponent>(wrapper)
        .template get<RotationComponent>(wrapper)
        .template get<ScaleComponent>(wrapper)
        .template get<CameraComponent>(wrapper);
    }
}
#endif //SAVEDCOMPONENTSLIST_H
