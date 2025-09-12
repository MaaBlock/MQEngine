//
// Created by Administrator on 2025/9/12.
//

#ifndef SAVEDCOMPONENTSLIST_H
#define SAVEDCOMPONENTSLIST_H
namespace MQEngine {
    template<typename SnapshotType, typename WrapperType>
    void SerializeComponents(SnapshotType&& snapshot, WrapperType& wrapper) {
        snapshot
        .get<entt::entity>(wrapper)
        .get<NameTag>(wrapper)
        .get<StaticMeshInstance>(wrapper)
        .get<DirectionalLightComponent>(wrapper)
        .get<DiffuseTextureComponent>(wrapper)
        .get<PositionComponent>(wrapper)
        .get<RotationComponent>(wrapper)
        .get<ScaleComponent>(wrapper)
        .get<CameraComponent>(wrapper);
    }
}
#endif //SAVEDCOMPONENTSLIST_H
