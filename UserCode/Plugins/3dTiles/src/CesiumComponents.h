#ifndef MQENGINE_CESIUM_COMPONENTS_H
#define MQENGINE_CESIUM_COMPONENTS_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief 地理参考系组件，将地球表面经纬高原点映射为引擎局部 Y-Up 笛卡尔坐标系原点
 */
struct CesiumGeoreference {
    double longitude = 116.3912757;
    double latitude = 39.906217;
    double height = 50.0;

    FCT::Vec3d getOrigin() const {
        return FCT::Vec3d(longitude, latitude, height);
    }

    void setOrigin(const FCT::Vec3d& origin) {
        longitude = origin.x;
        latitude = origin.y;
        height = origin.z;
    }
};

/**
 * @brief 3D Tiles 瓦片集组件，配置 tileset.json 路径或 Cesium Ion 资产及流式加载参数
 */
struct Cesium3DTileset {
    std::string url;
    int ionAssetId = 0;
    std::string ionAccessToken;
    std::string ionAssetEndpointUrl = "https://api.cesium.com/";
    double maximumScreenSpaceError = 16.0;
    bool preloadAncestors = true;
    bool preloadSiblings = true;
    int maximumSimultaneousTileLoads = 20;
    int maximumCachedBytes = 512 * 1024 * 1024;
    bool forbidHoles = false;
    bool enableFrustumCulling = true;
    bool enableFogCulling = true;
    bool showCreditsOnScreen = false;
    bool suspendUpdate = false;
    std::string cacheDirectory = "G:/MQEngine/MQEngine/CesiumCache";
};

/**
 * @brief 标识由 Cesium3DTilesetSystem 动态生成的瓦片图元子实体组件
 */
struct CesiumTilePrimitiveComponent {
    entt::entity tilesetEntity = entt::null;
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_COMPONENTS_H
