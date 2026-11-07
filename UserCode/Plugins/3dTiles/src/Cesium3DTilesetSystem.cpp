#include "Cesium3DTilesetSystem.h"

namespace MQEngine {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultOriginLon = 116.3912757;
constexpr double kDefaultOriginLat = 39.906217;
constexpr double kDefaultOriginHeight = 50.0;
constexpr const char* kCesiumSyncTickerName = "Cesium3DTilesSyncTicker";

double degToRad(double deg) {
    return deg * kPi / 180.0;
}

double radToDeg(double rad) {
    return rad * 180.0 / kPi;
}

bool isIdentityMat4(const FCT::Mat4& mat) {
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    for (int i = 0; i < 16; ++i) {
        if (std::abs(mat.m[i] - kIdentity[i]) > 1e-5f) {
            return false;
        }
    }
    return true;
}

FCT::Vec3d transformPointDouble(const FCT::Mat4& mat, const FCT::Vec3d& pt) {
    if (isIdentityMat4(mat)) {
        return pt;
    }
    return FCT::Vec3d(
        static_cast<double>(mat.m[0]) * pt.x + static_cast<double>(mat.m[1]) * pt.y +
            static_cast<double>(mat.m[2]) * pt.z + static_cast<double>(mat.m[3]),
        static_cast<double>(mat.m[4]) * pt.x + static_cast<double>(mat.m[5]) * pt.y +
            static_cast<double>(mat.m[6]) * pt.z + static_cast<double>(mat.m[7]),
        static_cast<double>(mat.m[8]) * pt.x + static_cast<double>(mat.m[9]) * pt.y +
            static_cast<double>(mat.m[10]) * pt.z + static_cast<double>(mat.m[11]));
}

CesiumGeospatial::LocalHorizontalCoordinateSystem makeEusCoordinateSystem(
    double lonDeg,
    double latDeg,
    double heightMeters) {
    CesiumGeospatial::Cartographic origin(
        degToRad(lonDeg),
        degToRad(latDeg),
        heightMeters);
    return CesiumGeospatial::LocalHorizontalCoordinateSystem(
        origin,
        CesiumGeospatial::LocalDirection::East,
        CesiumGeospatial::LocalDirection::Up,
        CesiumGeospatial::LocalDirection::South,
        1.0,
        CesiumGeospatial::Ellipsoid::WGS84);
}
} // namespace

Cesium3DTilesetSystem::Cesium3DTilesetSystem(
    FCT::Context* rhiContext,
    DataManager* dataManager,
    ResourceLoader* resourceLoader,
    FCT::Window* window)
    : m_rhiContext(rhiContext)
    , m_dataManager(dataManager)
    , m_resourceLoader(resourceLoader)
    , m_window(window) {}

Cesium3DTilesetSystem::~Cesium3DTilesetSystem() {
    shutdown();
}

void Cesium3DTilesetSystem::registerSyncTicker() {
    if (m_syncTickerRegistered || !m_rhiContext) {
        return;
    }
    auto& syncGraph = m_rhiContext->syncTickers();
    syncGraph[kCesiumSyncTickerName] = {
        [this]() {
            executeSyncTick();
        },
        {"ManagerSyncTicker"},
        {FCT::InnerSync::AdvanceFrameIndex}
    };
    (void)syncGraph.update();
    m_syncTickerRegistered = true;
}

void Cesium3DTilesetSystem::unregisterSyncTicker() {
    if (!m_syncTickerRegistered || !m_rhiContext) {
        return;
    }
    auto& syncGraph = m_rhiContext->syncTickers();
    syncGraph.removeNode(kCesiumSyncTickerName);
    (void)syncGraph.update();
    m_syncTickerRegistered = false;
}

void Cesium3DTilesetSystem::onActivate() {
    if (m_initialized) {
        return;
    }

    static bool s_contentTypesRegistered = false;
    if (!s_contentTypesRegistered) {
        Cesium3DTilesContent::registerAllTileContentTypes();
        s_contentTypesRegistered = true;
    }

    size_t numThreads = std::max<size_t>(2u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 2);
    m_taskProcessor = std::make_shared<CesiumTaskProcessor>(numThreads);
    m_asyncSystem = std::make_unique<CesiumAsync::AsyncSystem>(m_taskProcessor);
    m_assetAccessor = std::make_shared<CesiumAssetAccessor>(m_resourceLoader);
    m_creditSystem = std::make_shared<CesiumUtility::CreditSystem>();

    recomputeGeoreferenceMatrices(kDefaultOriginLon, kDefaultOriginLat, kDefaultOriginHeight);
    registerSyncTicker();
    m_initialized = true;
}

void Cesium3DTilesetSystem::onDeactivate() {
    shutdown();
}

void Cesium3DTilesetSystem::shutdown() {
    if (!m_initialized) {
        return;
    }

    unregisterSyncTicker();

    entt::registry* registry = resolveTargetRegistry();
    for (auto& kv : m_instances) {
        releaseTilesetInstance(registry, kv.first, kv.second, true);
    }
    m_instances.clear();

    if (m_taskProcessor) {
        m_taskProcessor->shutdown();
    }
    if (m_asyncSystem) {
        m_asyncSystem->dispatchMainThreadTasks();
    }

    for (auto& retired : m_retiredPrepareResources) {
        if (retired) {
            retired->flushAllDeferredDeletions();
        }
    }
    m_retiredPrepareResources.clear();

    m_creditSystem.reset();
    m_assetAccessor.reset();
    m_asyncSystem.reset();
    m_taskProcessor.reset();
    m_initialized = false;
}

void Cesium3DTilesetSystem::pumpRetiredPrepareResources() {
    for (auto it = m_retiredPrepareResources.begin(); it != m_retiredPrepareResources.end();) {
        if (!*it) {
            it = m_retiredPrepareResources.erase(it);
            continue;
        }
        (*it)->pumpDeferredDeletions();
        if (!(*it)->hasPendingDeletions()) {
            it = m_retiredPrepareResources.erase(it);
        } else {
            ++it;
        }
    }
}

entt::registry* Cesium3DTilesetSystem::resolveTargetRegistry() const {
    if (!m_dataManager) {
        return nullptr;
    }
    auto registries = m_dataManager->currentRegistries();
    if (Scene::Scene* currentScene = m_dataManager->getCurrentScene()) {
        for (const auto& trunkName : currentScene->getTrunkList()) {
            if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName)) {
                if (entt::registry* trunkReg = trunk->getRegistry()) {
                    if (std::find(registries.begin(), registries.end(), trunkReg) == registries.end()) {
                        registries.push_back(trunkReg);
                    }
                }
            }
        }
        if (entt::registry* mainReg = currentScene->getRegistry()) {
            if (std::find(registries.begin(), registries.end(), mainReg) == registries.end()) {
                registries.push_back(mainReg);
            }
        }
    }
    for (auto* reg : registries) {
        if (!reg) {
            continue;
        }
        const auto* tilesetPool = std::as_const(*reg).storage<Cesium3DTileset>();
        const auto* geoPool = std::as_const(*reg).storage<CesiumGeoreference>();
        if ((tilesetPool && !tilesetPool->empty()) || (geoPool && !geoPool->empty())) {
            return reg;
        }
    }
    if (Scene::Scene* currentScene = m_dataManager->getCurrentScene()) {
        for (const auto& trunkName : currentScene->getTrunkList()) {
            if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName)) {
                if (entt::registry* trunkReg = trunk->getRegistry()) {
                    return trunkReg;
                }
            }
        }
        if (entt::registry* mainReg = currentScene->getRegistry()) {
            return mainReg;
        }
    }
    return nullptr;
}

bool Cesium3DTilesetSystem::resolveGeoreferenceEntity(
    const std::string& trunkName,
    entt::registry*& outReg,
    entt::entity& outEntity) const {
    outReg = nullptr;
    outEntity = entt::null;
    if (!m_dataManager) {
        return false;
    }

    auto findInReg = [](entt::registry* reg, entt::entity& foundEntity) -> bool {
        if (!reg) {
            return false;
        }
        auto view = reg->view<CesiumGeoreference>();
        for (auto entity : view) {
            if (reg->valid(entity)) {
                foundEntity = entity;
                return true;
            }
        }
        return false;
    };

    entt::registry* primaryReg = reinterpret_cast<entt::registry*>(m_dataManager->getTrunkRegistryAddress(trunkName));
    if (findInReg(primaryReg, outEntity)) {
        outReg = primaryReg;
        return true;
    }

    if (trunkName.empty() || trunkName == "default") {
        entt::registry* defaultReg = reinterpret_cast<entt::registry*>(m_dataManager->getTrunkRegistryAddress("default"));
        if (defaultReg != primaryReg && findInReg(defaultReg, outEntity)) {
            outReg = defaultReg;
            return true;
        }
    }

    for (auto* reg : m_dataManager->currentRegistries()) {
        if (reg != primaryReg && findInReg(reg, outEntity)) {
            outReg = reg;
            return true;
        }
    }
    return false;
}

void Cesium3DTilesetSystem::recomputeGeoreferenceMatrices(
    double lonDeg,
    double latDeg,
    double heightMeters) {
    m_lastLon = lonDeg;
    m_lastLat = latDeg;
    m_lastHeight = heightMeters;
    ++m_georeferenceVersion;

    CesiumGeospatial::LocalHorizontalCoordinateSystem lhcs =
        makeEusCoordinateSystem(lonDeg, latDeg, heightMeters);

    m_ecefToLocalYUp = lhcs.getEcefToLocalTransformation();
    m_localYUpToEcef = lhcs.getLocalToEcefTransformation();

    for (auto& kv : m_instances) {
        if (kv.second.prepareResources) {
            kv.second.prepareResources->setEcefToLocalTransform(m_ecefToLocalYUp);
            if (kv.second.prepareResources->getCreatedMeshCount() == 0) {
                kv.second.georeferenceVersion = m_georeferenceVersion;
            }
        }
    }
}

void Cesium3DTilesetSystem::syncGeoreferenceFromScene(entt::registry& registry) {
    double lon = kDefaultOriginLon;
    double lat = kDefaultOriginLat;
    double height = kDefaultOriginHeight;

    auto geoView = registry.view<CesiumGeoreference>();
    for (auto entity : geoView) {
        const auto& geo = geoView.get<CesiumGeoreference>(entity);
        lon = geo.longitude;
        lat = geo.latitude;
        height = geo.height;
        break;
    }

    if (std::abs(lon - m_lastLon) > 1e-9 ||
        std::abs(lat - m_lastLat) > 1e-9 ||
        std::abs(height - m_lastHeight) > 1e-5) {
        recomputeGeoreferenceMatrices(lon, lat, height);
    }
}

Cesium3DTilesSelection::ViewState Cesium3DTilesetSystem::buildCameraViewState(
    entt::registry& registry,
    entt::entity tilesetEntity) const {
    glm::dvec3 camLocalPos(0.0, 20.0, 20.0);
    glm::dvec3 camLocalDir(0.0, -0.5, -0.8660254);
    glm::dvec3 camLocalUp(0.0, 1.0, 0.0);
    double fovYDeg = 60.0;

    bool foundActiveCamera = false;
    if (g_engineGlobal.cameraSystem) {
        FCT::Vec3<> activePos;
        FCT::Vec3<> activeFwd;
        FCT::Vec3<> activeUp;
        CameraComponent activeCam{};
        if (g_engineGlobal.cameraSystem->getActiveCameraWorldTransform(activePos, activeFwd, activeUp, activeCam)) {
            camLocalPos = glm::dvec3(activePos.x, activePos.y, activePos.z);
            camLocalDir = glm::normalize(glm::dvec3(activeFwd.x, activeFwd.y, activeFwd.z));
            camLocalUp = glm::normalize(glm::dvec3(activeUp.x, activeUp.y, activeUp.z));
            fovYDeg = std::clamp(static_cast<double>(activeCam.fov), 10.0, 160.0);
            foundActiveCamera = true;
        }
    }

    auto camView = registry.view<PositionComponent, RotationComponent, CameraComponent>();
    for (auto entity : camView) {
        if (foundActiveCamera) {
            break;
        }
        const auto& pos = camView.get<PositionComponent>(entity);
        const auto& rot = camView.get<RotationComponent>(entity);
        const auto& cam = camView.get<CameraComponent>(entity);
        if (!cam.active) {
            continue;
        }

        camLocalPos = glm::dvec3(pos.position.x, pos.position.y, pos.position.z);
        double pitch = degToRad(static_cast<double>(rot.rotation.x));
        double yaw = degToRad(static_cast<double>(rot.rotation.y));

        glm::dvec3 forward(
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch));
        if (glm::length(forward) > 1e-6) {
            camLocalDir = glm::normalize(forward);
        }

        glm::dvec3 worldUp(0.0, 1.0, 0.0);
        glm::dvec3 right = glm::cross(camLocalDir, worldUp);
        if (glm::length(right) > 1e-6) {
            right = glm::normalize(right);
            camLocalUp = glm::normalize(glm::cross(right, camLocalDir));
        }

        fovYDeg = std::clamp(static_cast<double>(cam.fov), 10.0, 160.0);
        break;
    }

    // Engine Space -> EUS Space (通过 Tileset / Georeference 节点的场景图世界逆矩阵)
    if (g_engineGlobal.matrixCacheSystem) {
        entt::entity anchorEntity = tilesetEntity;
        if (anchorEntity == entt::null || !registry.valid(anchorEntity)) {
            auto geoView = registry.view<CesiumGeoreference>();
            for (auto geoEntity : geoView) {
                anchorEntity = geoEntity;
                break;
            }
        }
        if (anchorEntity != entt::null && registry.valid(anchorEntity)) {
            FCT::Mat4 eusToEngineMat = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(&registry, anchorEntity);
            FCT::Mat4 engineToEusMat = eusToEngineMat.inverse();
            FCT::Vec4 eusPos4 = engineToEusMat * FCT::Vec4(
                static_cast<float>(camLocalPos.x),
                static_cast<float>(camLocalPos.y),
                static_cast<float>(camLocalPos.z),
                1.0f);
            FCT::Vec4 eusDir4 = engineToEusMat * FCT::Vec4(
                static_cast<float>(camLocalDir.x),
                static_cast<float>(camLocalDir.y),
                static_cast<float>(camLocalDir.z),
                0.0f);
            FCT::Vec4 eusUp4 = engineToEusMat * FCT::Vec4(
                static_cast<float>(camLocalUp.x),
                static_cast<float>(camLocalUp.y),
                static_cast<float>(camLocalUp.z),
                0.0f);

            camLocalPos = glm::dvec3(eusPos4.x, eusPos4.y, eusPos4.z);
            glm::dvec3 dirCandidate(eusDir4.x, eusDir4.y, eusDir4.z);
            if (glm::length(dirCandidate) > 1e-8) {
                camLocalDir = glm::normalize(dirCandidate);
            }
            glm::dvec3 upCandidate(eusUp4.x, eusUp4.y, eusUp4.z);
            if (glm::length(upCandidate) > 1e-8) {
                camLocalUp = glm::normalize(upCandidate);
            }
        }
    }

    glm::dvec3 camEcefPos = glm::dvec3(m_localYUpToEcef * glm::dvec4(camLocalPos, 1.0));
    glm::dvec3 camEcefDir = glm::normalize(glm::dmat3(m_localYUpToEcef) * camLocalDir);
    glm::dvec3 camEcefUp = glm::normalize(glm::dmat3(m_localYUpToEcef) * camLocalUp);

    double viewportWidth = 1920.0;
    double viewportHeight = 1080.0;
    if (m_window && m_window->getWidth() > 0 && m_window->getHeight() > 0) {
        viewportWidth = static_cast<double>(m_window->getWidth());
        viewportHeight = static_cast<double>(m_window->getHeight());
    }

    double aspect = viewportWidth / std::max(1.0, viewportHeight);
    double fovYRad = degToRad(fovYDeg);
    double fovXRad = 2.0 * std::atan(aspect * std::tan(fovYRad * 0.5));

    return Cesium3DTilesSelection::ViewState::create(
        camEcefPos,
        camEcefDir,
        camEcefUp,
        glm::dvec2(viewportWidth, viewportHeight),
        fovXRad,
        fovYRad);
}

std::string Cesium3DTilesetSystem::normalizeTilesetUrl(const std::string& rawUrl) {
    if (rawUrl.empty()) {
        return "";
    }
    if (rawUrl.rfind("http://", 0) == 0 ||
        rawUrl.rfind("https://", 0) == 0 ||
        rawUrl.rfind("file://", 0) == 0) {
        return rawUrl;
    }

    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::absolute(std::filesystem::u8path(rawUrl), ec);
    std::string genericStr = ec
        ? rawUrl
        : std::string(reinterpret_cast<const char*>(absPath.generic_u8string().c_str()));
    for (char& c : genericStr) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (!genericStr.empty() && genericStr.front() == '/') {
        return "file://" + genericStr;
    }
    return "file:///" + genericStr;
}

Cesium3DTilesSelection::TilesetOptions Cesium3DTilesetSystem::buildTilesetOptions(const Cesium3DTileset& comp) {
    Cesium3DTilesSelection::TilesetOptions options;
    options.maximumScreenSpaceError = comp.maximumScreenSpaceError;
    options.preloadAncestors = comp.preloadAncestors;
    options.preloadSiblings = comp.preloadSiblings;
    options.forbidHoles = comp.forbidHoles;
    options.enableFrustumCulling = comp.enableFrustumCulling;
    options.enableFogCulling = comp.enableFogCulling;
    options.maximumSimultaneousTileLoads = static_cast<uint32_t>(std::max(1, comp.maximumSimultaneousTileLoads));
    options.maximumCachedBytes = static_cast<int64_t>(std::max(16 * 1024 * 1024, comp.maximumCachedBytes));
    // 关闭 Cesium 按瓦片平滑法线重算，防止相邻独立 LOD 瓦片在接缝处因法线跳变产生亮暗分界线
    options.contentOptions.generateMissingNormalsSmooth = false;
    return options;
}

void Cesium3DTilesetSystem::cleanupRemovedTilesets(entt::registry& registry) {
    for (auto it = m_instances.begin(); it != m_instances.end();) {
        entt::entity e = it->first;
        if (!registry.valid(e) || !registry.all_of<Cesium3DTileset>(e)) {
            releaseTilesetInstance(&registry, e, it->second, false);
            it = m_instances.erase(it);
        } else {
            ++it;
        }
    }
}

void Cesium3DTilesetSystem::releaseTilesetInstance(
    entt::registry* registry,
    entt::entity tilesetEntity,
    TilesetRuntimeInstance& instance,
    bool immediateFlush) {
    instance.visibleTileResources.clear();
    instance.tileset.reset();

    if (registry && std::as_const(*registry).storage<CesiumTilePrimitiveComponent>() != nullptr) {
        std::vector<entt::entity> leftoverPrimitives;
        auto primView = registry->view<CesiumTilePrimitiveComponent>();
        for (auto primEntity : primView) {
            const auto& primComp = primView.get<CesiumTilePrimitiveComponent>(primEntity);
            if (tilesetEntity == entt::null || primComp.tilesetEntity == tilesetEntity) {
                leftoverPrimitives.push_back(primEntity);
            }
        }
        for (auto primEntity : leftoverPrimitives) {
            if (!registry->valid(primEntity)) {
                continue;
            }
            if (registry->all_of<StaticMeshInstance>(primEntity)) {
                registry->get<StaticMeshInstance>(primEntity).mesh = nullptr;
            }
            if (registry->all_of<DiffuseTextureComponent>(primEntity)) {
                registry->get<DiffuseTextureComponent>(primEntity).visible = false;
            }
            registry->destroy(primEntity);
        }
    }

    if (instance.prepareResources) {
        if (immediateFlush) {
            instance.prepareResources->flushAllDeferredDeletions();
        } else if (instance.prepareResources->hasPendingDeletions()) {
            m_retiredPrepareResources.push_back(instance.prepareResources);
        }
        instance.prepareResources.reset();
    }
}

void Cesium3DTilesetSystem::recreateTilesetInstance(
    entt::registry& registry,
    entt::entity entity,
    const Cesium3DTileset& comp,
    const std::string& resolvedUrl,
    TilesetRuntimeInstance& instance) {
    releaseTilesetInstance(&registry, entity, instance, false);

    instance.prepareResources = std::make_shared<CesiumPrepareRendererResources>(
        m_rhiContext,
        m_dataManager,
        entity,
        &registry);
    instance.prepareResources->setEcefToLocalTransform(m_ecefToLocalYUp);

    Cesium3DTilesSelection::TilesetExternals externals{
        m_assetAccessor,
        instance.prepareResources,
        *m_asyncSystem,
        m_creditSystem};

    Cesium3DTilesSelection::TilesetOptions options = buildTilesetOptions(comp);
    if (!resolvedUrl.empty()) {
        instance.tileset = std::make_unique<Cesium3DTilesSelection::Tileset>(externals, resolvedUrl, options);
    } else if (comp.ionAssetId > 0 && !comp.ionAccessToken.empty()) {
        std::string ionEndpoint = comp.ionAssetEndpointUrl.empty() ? "https://api.cesium.com/" : comp.ionAssetEndpointUrl;
        instance.tileset = std::make_unique<Cesium3DTilesSelection::Tileset>(
            externals,
            static_cast<int64_t>(comp.ionAssetId),
            comp.ionAccessToken,
            options,
            ionEndpoint);
    }

    instance.loadedUrl = resolvedUrl;
    instance.loadedIonAssetId = comp.ionAssetId;
    instance.loadedIonToken = comp.ionAccessToken;
    instance.rootAutoCentered = false;
    instance.georeferenceVersion = m_georeferenceVersion;
}

void Cesium3DTilesetSystem::tryAutoCenterGeoreferenceToRoot(
    entt::registry& registry,
    TilesetRuntimeInstance& instance) {
    if (instance.rootAutoCentered || !instance.tileset) {
        return;
    }

    const Cesium3DTilesSelection::Tile* rootTile = instance.tileset->getRootTile();
    if (!rootTile) {
        return;
    }

    glm::dvec3 centerEcef = Cesium3DTilesSelection::getBoundingVolumeCenter(rootTile->getBoundingVolume());
    if (glm::length(centerEcef) <= 1000.0) {
        instance.rootAutoCentered = true;
        return;
    }

    auto cartoOpt = CesiumGeospatial::Ellipsoid::WGS84.cartesianToCartographic(centerEcef);
    if (!cartoOpt.has_value()) {
        instance.rootAutoCentered = true;
        return;
    }

    double newLon = radToDeg(cartoOpt->longitude);
    double newLat = radToDeg(cartoOpt->latitude);
    double newHeight = cartoOpt->height;

    auto geoView = registry.view<CesiumGeoreference>();
    for (auto geoEntity : geoView) {
        auto& geo = geoView.get<CesiumGeoreference>(geoEntity);
        if (std::abs(geo.longitude - kDefaultOriginLon) < 1e-4 &&
            std::abs(geo.latitude - kDefaultOriginLat) < 1e-4) {
            geo.longitude = newLon;
            geo.latitude = newLat;
            geo.height = newHeight;
            recomputeGeoreferenceMatrices(newLon, newLat, newHeight);
        }
        break;
    }

    instance.rootAutoCentered = true;
}

void Cesium3DTilesetSystem::syncVisibleTileEntities(
    entt::registry& registry,
    TilesetRuntimeInstance& instance,
    const Cesium3DTilesSelection::ViewUpdateResult& updateResult) {
    std::unordered_set<CesiumTileRenderResources*> currentFrameVisible;
    currentFrameVisible.reserve(updateResult.tilesToRenderThisFrame.size());

    for (const auto& tilePtr : updateResult.tilesToRenderThisFrame) {
        if (!tilePtr) {
            continue;
        }
        const auto& content = tilePtr->getContent();
        const auto* renderContent = content.getRenderContent();
        if (!renderContent) {
            continue;
        }
        auto* tileRes = static_cast<CesiumTileRenderResources*>(renderContent->getRenderResources());
        if (!tileRes) {
            continue;
        }

        currentFrameVisible.insert(tileRes);
        tileRes->visible = true;
        for (const auto& prim : tileRes->primitives) {
            if (prim.entity != entt::null && registry.valid(prim.entity) &&
                registry.all_of<StaticMeshInstance>(prim.entity)) {
                registry.get<StaticMeshInstance>(prim.entity).mesh = prim.mesh;
            }
        }
    }

    for (CesiumTileRenderResources* prevTileRes : instance.visibleTileResources) {
        if (currentFrameVisible.find(prevTileRes) != currentFrameVisible.end()) {
            continue;
        }
        prevTileRes->visible = false;
        for (const auto& prim : prevTileRes->primitives) {
            if (prim.entity != entt::null && registry.valid(prim.entity) &&
                registry.all_of<StaticMeshInstance>(prim.entity)) {
                registry.get<StaticMeshInstance>(prim.entity).mesh = nullptr;
            }
        }
    }

    instance.visibleTileResources = std::move(currentFrameVisible);
}

void Cesium3DTilesetSystem::updateSingleTileset(
    entt::registry& registry,
    entt::entity entity,
    Cesium3DTileset& comp,
    const Cesium3DTilesSelection::ViewState& viewState) {
    if (m_assetAccessor && !comp.cacheDirectory.empty()) {
        m_assetAccessor->setCacheDir(comp.cacheDirectory);
    }

    std::string resolvedUrl = normalizeTilesetUrl(comp.url);
    auto& instance = m_instances[entity];

    bool needRecreate = false;
    if (!resolvedUrl.empty()) {
        if (instance.loadedUrl != resolvedUrl || !instance.tileset) {
            needRecreate = true;
        }
    } else if (comp.ionAssetId > 0 && !comp.ionAccessToken.empty()) {
        if (instance.loadedIonAssetId != comp.ionAssetId ||
            instance.loadedIonToken != comp.ionAccessToken ||
            !instance.tileset) {
            needRecreate = true;
        }
    } else if (instance.tileset) {
        releaseTilesetInstance(&registry, entity, instance, false);
        return;
    }

    if (!needRecreate && instance.tileset && instance.georeferenceVersion != m_georeferenceVersion) {
        if (instance.prepareResources && instance.prepareResources->getCreatedMeshCount() > 0) {
            bool prevAutoCentered = instance.rootAutoCentered;
            recreateTilesetInstance(registry, entity, comp, resolvedUrl, instance);
            instance.rootAutoCentered = prevAutoCentered;
        } else {
            instance.georeferenceVersion = m_georeferenceVersion;
        }
    } else if (needRecreate) {
        recreateTilesetInstance(registry, entity, comp, resolvedUrl, instance);
    }

    if (!instance.tileset) {
        return;
    }

    if (instance.prepareResources) {
        instance.prepareResources->setTargetRegistry(&registry);
        instance.prepareResources->pumpDeferredDeletions();
    }

    auto& options = instance.tileset->getOptions();
    options.maximumScreenSpaceError = comp.maximumScreenSpaceError;
    options.preloadAncestors = comp.preloadAncestors;
    options.preloadSiblings = comp.preloadSiblings;
    options.forbidHoles = comp.forbidHoles;
    options.enableFrustumCulling = comp.enableFrustumCulling;
    options.enableFogCulling = comp.enableFogCulling;
    options.maximumSimultaneousTileLoads = static_cast<uint32_t>(std::max(1, comp.maximumSimultaneousTileLoads));
    options.maximumCachedBytes = static_cast<int64_t>(std::max(16 * 1024 * 1024, comp.maximumCachedBytes));

    if (comp.suspendUpdate) {
        return;
    }

    tryAutoCenterGeoreferenceToRoot(registry, instance);
    if (instance.georeferenceVersion != m_georeferenceVersion &&
        instance.prepareResources && instance.prepareResources->getCreatedMeshCount() > 0) {
        bool prevAutoCentered = instance.rootAutoCentered;
        recreateTilesetInstance(registry, entity, comp, resolvedUrl, instance);
        instance.rootAutoCentered = prevAutoCentered;
        if (!instance.tileset) {
            return;
        }
    }

    const auto& updateResult = instance.tileset->updateView({viewState});
    syncVisibleTileEntities(registry, instance, updateResult);

    size_t currentCreatedMeshes = instance.prepareResources ? instance.prepareResources->getCreatedMeshCount() : 0;
    size_t currentVisibleTiles = instance.visibleTileResources.size();
    static size_t s_lastLoggedMeshes = 999999;
    static size_t s_lastLoggedVisible = 999999;
    if (currentCreatedMeshes != s_lastLoggedMeshes || currentVisibleTiles != s_lastLoggedVisible) {
        s_lastLoggedMeshes = currentCreatedMeshes;
        s_lastLoggedVisible = currentVisibleTiles;
        uint32_t gpuDispatched = g_engineGlobal.gpuDrivenSystem ? g_engineGlobal.gpuDrivenSystem->getLastDispatchedObjectCount() : 0;
        uint32_t gpuVisible = g_engineGlobal.gpuDrivenSystem ? g_engineGlobal.gpuDrivenSystem->getLastVisibleObjectCount() : 0;
        spdlog::info(
            "[Cesium3DTilesetSystem] 瓦片状态更新: visibleTiles={}, createdMeshes={}, workerLoading={}, mainLoading={}, gpuDispatched={}, gpuVisible={}",
            currentVisibleTiles,
            currentCreatedMeshes,
            updateResult.workerThreadTileLoadQueueLength,
            updateResult.mainThreadTileLoadQueueLength,
            gpuDispatched,
            gpuVisible);
    }
}

void Cesium3DTilesetSystem::executeSyncTick() {
    if (!m_initialized) {
        return;
    }

    pumpRetiredPrepareResources();

    entt::registry* registry = resolveTargetRegistry();
    if (registry) {
        syncGeoreferenceFromScene(*registry);
    }

    if (m_asyncSystem) {
        m_asyncSystem->dispatchMainThreadTasks();
    }

    if (!registry) {
        return;
    }

    cleanupRemovedTilesets(*registry);

    auto tilesetView = registry->view<Cesium3DTileset>();
    for (auto entity : tilesetView) {
        auto& comp = tilesetView.get<Cesium3DTileset>(entity);
        Cesium3DTilesSelection::ViewState viewState = buildCameraViewState(*registry, entity);
        updateSingleTileset(*registry, entity, comp, viewState);
    }

    if (m_creditSystem) {
        m_creditSystem->startNextFrame();
    }
}

void Cesium3DTilesetSystem::updateLogic() {
    if (!m_initialized) {
        onActivate();
    }

    if (m_assetAccessor) {
        m_assetAccessor->tick();
    }

    if (!m_syncTickerRegistered) {
        executeSyncTick();
    }
}

void Cesium3DTilesetSystem::updateRender() {
}

FCT::Vec3d Cesium3DTilesetSystem::getGeoreferenceOrigin(const std::string& trunkName) const {
    entt::registry* reg = nullptr;
    entt::entity geoEntity = entt::null;
    if (resolveGeoreferenceEntity(trunkName, reg, geoEntity) && reg && reg->all_of<CesiumGeoreference>(geoEntity)) {
        const auto& geo = reg->get<CesiumGeoreference>(geoEntity);
        return FCT::Vec3d(geo.longitude, geo.latitude, geo.height);
    }
    return FCT::Vec3d(kDefaultOriginLon, kDefaultOriginLat, kDefaultOriginHeight);
}

bool Cesium3DTilesetSystem::setGeoreferenceOrigin(
    const std::string& trunkName,
    double longitude,
    double latitude,
    double height) {
    entt::registry* reg = nullptr;
    entt::entity geoEntity = entt::null;
    if (!resolveGeoreferenceEntity(trunkName, reg, geoEntity) || !reg || !reg->all_of<CesiumGeoreference>(geoEntity)) {
        return false;
    }
    auto& geo = reg->get<CesiumGeoreference>(geoEntity);
    geo.longitude = longitude;
    geo.latitude = latitude;
    geo.height = height;
    recomputeGeoreferenceMatrices(longitude, latitude, height);
    return true;
}

FCT::Vec3d Cesium3DTilesetSystem::cartographicToEcef(double longitude, double latitude, double height) const {
    CesiumGeospatial::Cartographic carto(degToRad(longitude), degToRad(latitude), height);
    glm::dvec3 ecef = CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(carto);
    return FCT::Vec3d(ecef.x, ecef.y, ecef.z);
}

FCT::Vec3d Cesium3DTilesetSystem::ecefToCartographic(FCT::Vec3d ecef) const {
    auto cartoOpt = CesiumGeospatial::Ellipsoid::WGS84.cartesianToCartographic(glm::dvec3(ecef.x, ecef.y, ecef.z));
    if (!cartoOpt.has_value()) {
        return FCT::Vec3d(0.0, 0.0, 0.0);
    }
    return FCT::Vec3d(radToDeg(cartoOpt->longitude), radToDeg(cartoOpt->latitude), cartoOpt->height);
}

FCT::Vec3d Cesium3DTilesetSystem::ecefToEus(const std::string& trunkName, FCT::Vec3d ecef) const {
    FCT::Vec3d origin = getGeoreferenceOrigin(trunkName);
    CesiumGeospatial::LocalHorizontalCoordinateSystem lhcs =
        makeEusCoordinateSystem(origin.x, origin.y, origin.z);
    glm::dvec3 eus = lhcs.ecefPositionToLocal(glm::dvec3(ecef.x, ecef.y, ecef.z));
    return FCT::Vec3d(eus.x, eus.y, eus.z);
}

FCT::Vec3d Cesium3DTilesetSystem::eusToEcef(const std::string& trunkName, FCT::Vec3d eus) const {
    FCT::Vec3d origin = getGeoreferenceOrigin(trunkName);
    CesiumGeospatial::LocalHorizontalCoordinateSystem lhcs =
        makeEusCoordinateSystem(origin.x, origin.y, origin.z);
    glm::dvec3 ecef = lhcs.localPositionToEcef(glm::dvec3(eus.x, eus.y, eus.z));
    return FCT::Vec3d(ecef.x, ecef.y, ecef.z);
}

FCT::Vec3d Cesium3DTilesetSystem::eusToEngine(const std::string& trunkName, FCT::Vec3d eus) const {
    entt::registry* reg = nullptr;
    entt::entity geoEntity = entt::null;
    if (g_engineGlobal.matrixCacheSystem && resolveGeoreferenceEntity(trunkName, reg, geoEntity)) {
        FCT::Mat4 eusToEngineMat = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(reg, geoEntity);
        return transformPointDouble(eusToEngineMat, eus);
    }
    return eus;
}

FCT::Vec3d Cesium3DTilesetSystem::engineToEus(const std::string& trunkName, FCT::Vec3d enginePos) const {
    entt::registry* reg = nullptr;
    entt::entity geoEntity = entt::null;
    if (g_engineGlobal.matrixCacheSystem && resolveGeoreferenceEntity(trunkName, reg, geoEntity)) {
        FCT::Mat4 eusToEngineMat = g_engineGlobal.matrixCacheSystem->computeEntityWorldMatrix(reg, geoEntity);
        if (!isIdentityMat4(eusToEngineMat)) {
            return transformPointDouble(eusToEngineMat.inverse(), enginePos);
        }
    }
    return enginePos;
}

FCT::Vec3d Cesium3DTilesetSystem::ecefToEngine(const std::string& trunkName, FCT::Vec3d ecef) const {
    return eusToEngine(trunkName, ecefToEus(trunkName, ecef));
}

FCT::Vec3d Cesium3DTilesetSystem::engineToEcef(const std::string& trunkName, FCT::Vec3d enginePos) const {
    return eusToEcef(trunkName, engineToEus(trunkName, enginePos));
}

FCT::Vec3d Cesium3DTilesetSystem::cartographicToEus(
    const std::string& trunkName,
    double longitude,
    double latitude,
    double height) const {
    return ecefToEus(trunkName, cartographicToEcef(longitude, latitude, height));
}

FCT::Vec3d Cesium3DTilesetSystem::eusToCartographic(const std::string& trunkName, FCT::Vec3d eus) const {
    return ecefToCartographic(eusToEcef(trunkName, eus));
}

FCT::Vec3d Cesium3DTilesetSystem::cartographicToEngine(
    const std::string& trunkName,
    double longitude,
    double latitude,
    double height) const {
    return eusToEngine(trunkName, cartographicToEus(trunkName, longitude, latitude, height));
}

FCT::Vec3d Cesium3DTilesetSystem::engineToCartographic(const std::string& trunkName, FCT::Vec3d enginePos) const {
    return eusToCartographic(trunkName, engineToEus(trunkName, enginePos));
}

} // namespace MQEngine
