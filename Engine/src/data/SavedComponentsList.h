//
// Created by Administrator on 2025/9/12.
//

#ifndef SAVEDCOMPONENTSLIST_H
#define SAVEDCOMPONENTSLIST_H
namespace MQEngine {
    template<typename SnapshotType, typename WrapperType>
    void SerializeComponents(SnapshotType&& snapshot, WrapperType& wrapper) {
        snapshot
        .template get<entt::entity>(wrapper)
        .template get<NameTag>(wrapper)
        .template get<StaticMeshInstance>(wrapper)
        .template get<DirectionalLightComponent>(wrapper)
        .template get<DiffuseTextureComponent>(wrapper)
        .template get<NormalMapComponent>(wrapper)
        .template get<PositionComponent>(wrapper)
        .template get<RotationComponent>(wrapper)
        .template get<ScaleComponent>(wrapper)
        .template get<CameraComponent>(wrapper);
    }
}
#endif //SAVEDCOMPONENTSLIST_H
