#ifndef MQENGINE_CESIUM_PREPARE_RENDERER_RESOURCES_H
#define MQENGINE_CESIUM_PREPARE_RENDERER_RESOURCES_H

#include "thirdparty/thirdparty.h"
#include "CesiumTileRenderResources.h"

namespace MQEngine {

/**
 * @brief 后台线程解析出的单个瓦片图元顶点与贴图数据包
 */
struct LoadThreadPrimitiveData {
    std::vector<FCT::Vec3<>> positions;
    std::vector<FCT::Vec3<>> normals;
    std::vector<FCT::Vec2> uvs;
    std::vector<uint32_t> indices;
    glm::dmat4 nodeTransform{1.0};
    FCT::Vec4 baseColorFactor = FCT::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool hasNormals = false;
    bool isUnlit = false;

    bool hasTexture = false;
    int32_t texWidth = 0;
    int32_t texHeight = 0;
    std::vector<uint8_t> texPixels;
};

/**
 * @brief 后台线程解析出的单个瓦片所有图元集合
 */
struct LoadThreadTileResult {
    glm::dmat4 tileTransform{1.0};
    std::vector<LoadThreadPrimitiveData> primitives;
};

/**
 * @brief Cesium 渲染资源准备器，负责后台线程解析 glTF 并在同步屏障创建/延迟销毁 FCT 网格与贴图实体
 */
class TILES3D_PLUGIN_API CesiumPrepareRendererResources : public Cesium3DTilesSelection::IPrepareRendererResources {
public:
    CesiumPrepareRendererResources(
        FCT::Context* rhiContext,
        DataManager* dataManager,
        entt::entity tilesetEntity = entt::null,
        entt::registry* targetRegistry = nullptr);
    ~CesiumPrepareRendererResources() override;

    void setEcefToLocalTransform(const glm::dmat4& ecefToLocalYUp) {
        std::lock_guard<std::mutex> lock(m_transformMutex);
        m_ecefToLocalYUp = ecefToLocalYUp;
    }

    glm::dmat4 getEcefToLocalTransform() const {
        std::lock_guard<std::mutex> lock(m_transformMutex);
        return m_ecefToLocalYUp;
    }

    uint64_t getCreatedMeshCount() const {
        return m_createdMeshCount;
    }

    bool hasPendingDeletions() const {
        return !m_deferredDeletions.empty();
    }

    void setTilesetEntity(entt::entity tilesetEntity) {
        m_tilesetEntity = tilesetEntity;
    }

    void setTargetRegistry(entt::registry* targetRegistry) {
        m_targetRegistry = targetRegistry;
    }

    /**
     * @brief 在帧同步屏障中推进延迟销毁队列，确保在途 CommandBuffer 不再引用已卸载瓦片的顶点缓冲
     */
    void pumpDeferredDeletions();

    /**
     * @brief 立即清空所有待销毁的 GPU 资源（仅在系统关闭且渲染线程已停止时调用）
     */
    void flushAllDeferredDeletions();

    CesiumAsync::Future<Cesium3DTilesSelection::TileLoadResultAndRenderResources> prepareInLoadThread(
        const CesiumAsync::AsyncSystem& asyncSystem,
        Cesium3DTilesSelection::TileLoadResult&& tileLoadResult,
        const glm::dmat4& transform,
        const std::any& rendererOptions) override;

    void* prepareInMainThread(
        Cesium3DTilesSelection::Tile& tile,
        void* pLoadThreadResult) override;

    void free(
        Cesium3DTilesSelection::Tile& tile,
        void* pLoadThreadResult,
        void* pMainThreadResult) noexcept override;

    void* prepareRasterInLoadThread(
        CesiumGltf::ImageCesium& image,
        const std::any& rendererOptions) override {
        return nullptr;
    }

    void* prepareRasterInMainThread(
        CesiumRasterOverlays::RasterOverlayTile& rasterTile,
        void* pLoadThreadResult) override {
        return nullptr;
    }

    void freeRaster(
        const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
        void* pLoadThreadResult,
        void* pMainThreadResult) noexcept override {}

    void attachRasterInMainThread(
        const Cesium3DTilesSelection::Tile& tile,
        int32_t overlayTextureCoordinateID,
        const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
        void* pMainThreadRendererResources,
        const glm::dvec2& translation,
        const glm::dvec2& scale) override {}

    void detachRasterInMainThread(
        const Cesium3DTilesSelection::Tile& tile,
        int32_t overlayTextureCoordinateID,
        const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
        void* pMainThreadRendererResources) noexcept override {}

private:
    struct DeferredGpuGarbage {
        FCT::StaticMesh<uint32_t>* mesh = nullptr;
        FCT::SingleBufferImage* texture = nullptr;
        uint32_t framesRemaining = 0;
    };

    static constexpr uint32_t kDeferredFreeFrames = 6;

    entt::registry* resolveTargetRegistry() const;

    static glm::dmat4 computeNodeLocalMatrix(const CesiumGltf::Node& node);
    static bool extractPositions(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        LoadThreadPrimitiveData& outPrim);
    static void extractUVs(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        size_t vertexCount,
        int32_t texCoordIndex,
        LoadThreadPrimitiveData& outPrim);
    static bool extractNormals(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        size_t vertexCount,
        LoadThreadPrimitiveData& outPrim);
    static void extractIndices(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        size_t vertexCount,
        LoadThreadPrimitiveData& outPrim);
    static bool convertTopologyToTriangleList(
        int32_t mode,
        size_t vertexCount,
        std::vector<uint32_t>& indices);
    static void computeSmoothNormals(LoadThreadPrimitiveData& prim);
    static void extractMaterialAndTexture(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        int32_t& outTexCoordIndex,
        LoadThreadPrimitiveData& outPrim);
    static void buildPrimitiveOnLoadThread(
        const CesiumGltf::Model& model,
        const CesiumGltf::MeshPrimitive& primitive,
        const glm::dmat4& nodeTransform,
        LoadThreadTileResult& outResult);

    void processNodeRecursive(
        const CesiumGltf::Model& model,
        int32_t nodeIdx,
        const glm::dmat4& parentTransform,
        LoadThreadTileResult& outResult) const;

    FCT::StaticMesh<uint32_t>* createGpuMesh(
        const LoadThreadPrimitiveData& primData,
        const glm::dmat4& tileTransform) const;
    FCT::SingleBufferImage* createGpuTexture(const LoadThreadPrimitiveData& primData) const;
    entt::entity createPrimitiveEntity(
        entt::registry& registry,
        const LoadThreadPrimitiveData& primData,
        FCT::SingleBufferImage* gpuTexture) const;

private:
    FCT::Context* m_rhiContext = nullptr;
    DataManager* m_dataManager = nullptr;
    entt::entity m_tilesetEntity = entt::null;
    entt::registry* m_targetRegistry = nullptr;
    mutable std::mutex m_transformMutex;
    glm::dmat4 m_ecefToLocalYUp{1.0};
    uint64_t m_createdMeshCount = 0;
    std::vector<DeferredGpuGarbage> m_deferredDeletions;
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_PREPARE_RENDERER_RESOURCES_H
