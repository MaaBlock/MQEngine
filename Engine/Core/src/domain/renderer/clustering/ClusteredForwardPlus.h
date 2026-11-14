/**
 * @file ClusteredForwardPlus.h
 * @brief 3D Clustered 前向光照剔除与分桶管理器 (2048 Froxels, CPU SIMD128, FEAT-09)
 */
#ifndef MQENGINE_DOMAIN_RENDERER_CLUSTERING_CLUSTEREDFORWARDPLUS_H
#define MQENGINE_DOMAIN_RENDERER_CLUSTERING_CLUSTEREDFORWARDPLUS_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <array>
#include <cstdint>
#include <vector>

namespace MQEngine::Domain::Renderer
{
    /**
     * @brief 对齐 std430 严格 16 字节对齐的光源显存结构
     */
    struct alignas(16) ClusterLightRecord
    {
        float position[3]{0.0f, 0.0f, 0.0f};
        float radius{1.0f};
        float color[3]{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        uint32_t lightType{0}; // 0: Point, 1: Spot
        uint32_t shadowChannel{0xFFFFFFFF};
        float pad[2]{0.0f, 0.0f};
    };
    static_assert(sizeof(ClusterLightRecord) % 16 == 0, "ClusterLightRecord 必须严格 16 字节对齐");

    struct alignas(16) FroxelAabb
    {
        float minPt[3]{0.0f, 0.0f, 0.0f};
        float pad0{0.0f};
        float maxPt[3]{0.0f, 0.0f, 0.0f};
        float pad1{0.0f};
    };

    struct FroxelGridConfig
    {
        uint32_t dimX{16};
        uint32_t dimY{8};
        uint32_t dimZ{16};
        [[nodiscard]] uint32_t totalFroxels() const noexcept { return dimX * dimY * dimZ; }
    };

    /**
     * @brief 3D Clustered 前向光照剔除与分桶管理器
     *        支持 16x8x16 = 2048 Froxels Log-Z 视锥划分、4 路 SIMD128 球体-AABB 相交测试，
     *        以及局部光源 <= 4 盏时的单簇自适应退化。
     */
    class ENGINE_API ClusteredForwardPlus
    {
    public:
        static constexpr uint32_t MaxLightsPerFroxel = 32;
        static constexpr uint32_t SingleClusterDegradeThreshold = 4;

        ClusteredForwardPlus();
        ~ClusteredForwardPlus() = default;

        void updateFrustumGrid(
            const FCT::Mat4& projMatrix,
            float nearPlane,
            float farPlane,
            uint32_t viewportWidth,
            uint32_t viewportHeight);

        void cullLights(
            const std::vector<ClusterLightRecord>& sceneLights,
            const FCT::Mat4& viewMatrix);

        [[nodiscard]] bool isDegradedToSingleCluster() const noexcept { return m_isDegraded; }
        [[nodiscard]] bool isDegraded() const noexcept { return m_isDegraded; }
        [[nodiscard]] uint32_t getActiveLightCount() const noexcept { return m_activeLightCount; }
        [[nodiscard]] const FroxelGridConfig& getGridConfig() const noexcept { return m_grid; }
        [[nodiscard]] const std::vector<ClusterLightRecord>& getActiveLightTable() const noexcept { return m_activeLightTable; }
        [[nodiscard]] const std::vector<uint32_t>& getFroxelLightGridBuffer() const noexcept { return m_froxelGridBuffer; }
        [[nodiscard]] const std::vector<uint32_t>& getLightIndexListBuffer() const noexcept { return m_lightIndexList; }

        [[nodiscard]] uint32_t getFroxelLightCount(uint32_t froxelIndex) const noexcept;
        [[nodiscard]] const FroxelAabb* getFroxelAabb(uint32_t froxelIndex) const noexcept;

        static uint32_t computeSliceIndex(
            float viewZ,
            float nearPlane = 0.1f,
            float farPlane = 100.0f,
            uint32_t sliceCount = 16) noexcept;

        static bool TestFroxelSphereIntersection(
            const FroxelAabb& box,
            float cx,
            float cy,
            float cz,
            float radius) noexcept;

    private:
        FroxelGridConfig m_grid{16, 8, 16};
        bool m_isDegraded{false};
        uint32_t m_activeLightCount{0};

        std::vector<FroxelAabb> m_froxelAabbs;
        std::vector<ClusterLightRecord> m_activeLightTable;
        std::vector<uint32_t> m_froxelGridBuffer; // 2048 * 2 (offset, count)
        std::vector<uint32_t> m_lightIndexList;
    };
}

#endif // MQENGINE_DOMAIN_RENDERER_CLUSTERING_CLUSTEREDFORWARDPLUS_H
