#ifndef MQENGINE_CESIUM_3DTILESET_SYSTEM_H
#define MQENGINE_CESIUM_3DTILESET_SYSTEM_H

#include "thirdparty/thirdparty.h"
#include "CesiumComponents.h"
#include "CesiumTaskProcessor.h"
#include "CesiumAssetAccessor.h"
#include "CesiumPrepareRendererResources.h"

namespace MQEngine {

/**
 * @brief 3D Tiles 流式加载与视锥体 LOD 调度系统
 */
class TILES3D_PLUGIN_API Cesium3DTilesetSystem : public ISystem {
public:
    Cesium3DTilesetSystem(
        FCT::Context* rhiContext,
        DataManager* dataManager,
        ResourceLoader* resourceLoader,
        FCT::Window* window = nullptr);
    ~Cesium3DTilesetSystem() override;

    void onActivate() override;
    void onDeactivate() override;
    void updateLogic() override;
    void updateRender() override;

    void shutdown();

    const glm::dmat4& getEcefToLocalTransform() const { return m_ecefToLocalYUp; }
    const glm::dmat4& getLocalToEcefTransform() const { return m_localYUpToEcef; }

    /**
     * @brief 获取/设置场景中 CesiumGeoreference 地理原点 (经度 deg, 纬度 deg, 椭球高 m)
     */
    FCT::Vec3d getGeoreferenceOrigin(const std::string& trunkName) const;
    bool setGeoreferenceOrigin(const std::string& trunkName, double longitude, double latitude, double height);

    /**
     * @brief ECEF / EUS (East-Up-South) / Engine 世界空间 / WGS84 经纬高坐标双向转换
     */
    FCT::Vec3d cartographicToEcef(double longitude, double latitude, double height) const;
    FCT::Vec3d ecefToCartographic(FCT::Vec3d ecef) const;
    FCT::Vec3d ecefToEus(const std::string& trunkName, FCT::Vec3d ecef) const;
    FCT::Vec3d eusToEcef(const std::string& trunkName, FCT::Vec3d eus) const;
    FCT::Vec3d eusToEngine(const std::string& trunkName, FCT::Vec3d eus) const;
    FCT::Vec3d engineToEus(const std::string& trunkName, FCT::Vec3d enginePos) const;
    FCT::Vec3d ecefToEngine(const std::string& trunkName, FCT::Vec3d ecef) const;
    FCT::Vec3d engineToEcef(const std::string& trunkName, FCT::Vec3d enginePos) const;
    FCT::Vec3d cartographicToEus(const std::string& trunkName, double longitude, double latitude, double height) const;
    FCT::Vec3d eusToCartographic(const std::string& trunkName, FCT::Vec3d eus) const;
    FCT::Vec3d cartographicToEngine(const std::string& trunkName, double longitude, double latitude, double height) const;
    FCT::Vec3d engineToCartographic(const std::string& trunkName, FCT::Vec3d enginePos) const;

    BOOST_DESCRIBE_CLASS(Cesium3DTilesetSystem, (),
        (getGeoreferenceOrigin,
         setGeoreferenceOrigin,
         cartographicToEcef,
         ecefToCartographic,
         ecefToEus,
         eusToEcef,
         eusToEngine,
         engineToEus,
         ecefToEngine,
         engineToEcef,
         cartographicToEus,
         eusToCartographic,
         cartographicToEngine,
         engineToCartographic),
        (), ())

private:
    struct TilesetRuntimeInstance {
        std::unique_ptr<Cesium3DTilesSelection::Tileset> tileset;
        std::shared_ptr<CesiumPrepareRendererResources> prepareResources;
        std::string loadedUrl;
        int loadedIonAssetId = 0;
        std::string loadedIonToken;
        bool rootAutoCentered = false;
        uint64_t georeferenceVersion = 0;
        std::unordered_set<CesiumTileRenderResources*> visibleTileResources;
    };

    void registerSyncTicker();
    void unregisterSyncTicker();
    void executeSyncTick();
    void pumpRetiredPrepareResources();

    entt::registry* resolveTargetRegistry() const;
    bool resolveGeoreferenceEntity(const std::string& trunkName, entt::registry*& outReg, entt::entity& outEntity) const;
    void recomputeGeoreferenceMatrices(double lonDeg, double latDeg, double heightMeters);
    void syncGeoreferenceFromScene(entt::registry& registry);
    Cesium3DTilesSelection::ViewState buildCameraViewState(
        entt::registry& registry,
        entt::entity tilesetEntity = entt::null) const;
    void cleanupRemovedTilesets(entt::registry& registry);
    void updateSingleTileset(
        entt::registry& registry,
        entt::entity entity,
        Cesium3DTileset& comp,
        const Cesium3DTilesSelection::ViewState& viewState);
    void recreateTilesetInstance(
        entt::registry& registry,
        entt::entity entity,
        const Cesium3DTileset& comp,
        const std::string& resolvedUrl,
        TilesetRuntimeInstance& instance);
    void tryAutoCenterGeoreferenceToRoot(
        entt::registry& registry,
        TilesetRuntimeInstance& instance);
    void syncVisibleTileEntities(
        entt::registry& registry,
        TilesetRuntimeInstance& instance,
        const Cesium3DTilesSelection::ViewUpdateResult& updateResult);
    void releaseTilesetInstance(
        entt::registry* registry,
        entt::entity tilesetEntity,
        TilesetRuntimeInstance& instance,
        bool immediateFlush = false);

    static std::string normalizeTilesetUrl(const std::string& rawUrl);
    static Cesium3DTilesSelection::TilesetOptions buildTilesetOptions(const Cesium3DTileset& comp);

private:
    FCT::Context* m_rhiContext = nullptr;
    DataManager* m_dataManager = nullptr;
    ResourceLoader* m_resourceLoader = nullptr;
    FCT::Window* m_window = nullptr;

    std::shared_ptr<CesiumTaskProcessor> m_taskProcessor;
    std::unique_ptr<CesiumAsync::AsyncSystem> m_asyncSystem;
    std::shared_ptr<CesiumAssetAccessor> m_assetAccessor;
    std::shared_ptr<CesiumUtility::CreditSystem> m_creditSystem;

    std::unordered_map<entt::entity, TilesetRuntimeInstance> m_instances;
    std::vector<std::shared_ptr<CesiumPrepareRendererResources>> m_retiredPrepareResources;

    glm::dmat4 m_ecefToLocalYUp{1.0};
    glm::dmat4 m_localYUpToEcef{1.0};
    double m_lastLon = -9999.0;
    double m_lastLat = -9999.0;
    double m_lastHeight = -9999.0;
    uint64_t m_georeferenceVersion = 0;
    bool m_initialized = false;
    bool m_syncTickerRegistered = false;
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_3DTILESET_SYSTEM_H
