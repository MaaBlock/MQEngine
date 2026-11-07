#ifndef MQENGINE_CESIUM_TILE_RENDER_RESOURCES_H
#define MQENGINE_CESIUM_TILE_RENDER_RESOURCES_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief 单个瓦片图元在引擎中的渲染资源与对应实体句柄
 */
struct CesiumPrimitiveRenderData {
    entt::entity entity = entt::null;
    FCT::StaticMesh<uint32_t>* mesh = nullptr;
    FCT::SingleBufferImage* texture = nullptr;
    bool ownsTexture = false;
};

/**
 * @brief 单个 3D Tile 瓦片在主线程持有的所有图元渲染资源集合
 */
struct CesiumTileRenderResources {
    std::vector<CesiumPrimitiveRenderData> primitives;
    bool visible = false;
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_TILE_RENDER_RESOURCES_H
