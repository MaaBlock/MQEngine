/**
 * @file TerrainRuntimeComposite.h
 * @brief 大世界地形运行时渲染与几何/物理 Composite（tags: [Runtime, Game]，独立编译为 TerrainRuntimeComposite.dll）
 */
#ifndef MQENGINE_TERRAIN_RUNTIME_COMPOSITE_H
#define MQENGINE_TERRAIN_RUNTIME_COMPOSITE_H

#include "TerrainTypes.h"
#include <MQEngine/asset/interface/Composite.h>

namespace MQEngine
{
    namespace Terrain
    {
        /**
         * @brief 大世界地形运行时 Composite：
         *        管理多级 GPU Clipmap 环、高度场采样、法线解算、Splatmap 4 层材质权重
         *        并向 ObjectPass 挂载 TerrainClipmapPipeline 与向物理系统注册高度场碰撞
         */
        class TERRAIN_RUNTIME_COMPOSITE_API TerrainRuntimeComposite : public Asset::Composite
        {
        public:
            using ClipmapRingGeometry = Terrain::ClipmapRingGeometry;

            TerrainRuntimeComposite();
            ~TerrainRuntimeComposite() override;

            TerrainRuntimeComposite(const TerrainRuntimeComposite&) = delete;
            TerrainRuntimeComposite& operator=(const TerrainRuntimeComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "TerrainRuntimeComposite"; }
            std::string getPluginName() const override { return "Terrain"; }
            std::vector<std::string> getDependencies() const override { return {}; }
            std::vector<std::string> getRequiredTags() const override { return {"Runtime"}; }

            // ==================== 高度场与物理碰撞查询 ====================

            float getHeightAt(float worldX, float worldZ) const noexcept;
            FCT::Vec3<float> getNormalAt(float worldX, float worldZ) const noexcept;

            bool raycast(
                const FCT::Vec3<float>& rayOrigin,
                const FCT::Vec3<float>& rayDir,
                float maxDist,
                FCT::Vec3<float>& hitPos,
                FCT::Vec3<float>& hitNormal) const noexcept;

            // ==================== 运行时修改接口（供雕刻笔刷调用） ====================

            void modifyHeight(float worldX, float worldZ, float radius, float delta, float falloff);
            void smoothHeight(float worldX, float worldZ, float radius, float strength);
            void flattenHeight(float worldX, float worldZ, float radius, float targetHeight, float strength);
            void paintSplatmap(float worldX, float worldZ, float radius, uint8_t layerIndex, float weight);

            // ==================== 状态访问与持久化 ====================

            uint32_t getGridResolution() const noexcept { return m_resolution; }
            float getGridSpacing() const noexcept { return m_gridSpacing; }
            float getWorldSize() const noexcept { return static_cast<float>(m_resolution - 1) * m_gridSpacing; }

            const std::vector<float>& getHeightData() const noexcept { return m_heightfield; }
            const std::vector<std::array<float, 4>>& getSplatmapData() const noexcept { return m_splatmap; }
            const std::vector<TerrainClipmapLevel>& getClipmapLevels() const noexcept { return m_clipmapLevels; }
            const std::array<TerrainLayerDesc, 4>& getLayers() const noexcept { return m_layers; }
            const std::vector<ClipmapRingGeometry>& getClipmapGeometries() const noexcept { return m_clipmapGeometries; }

            Status saveTerrainToDisk(const std::string& filePath);
            Status loadTerrainFromDisk(const std::string& filePath);

        private:
            void initDefaultTerrain();
            void generateClipmapMeshBuffers();
            void updateClipmapLevels(const FCT::Vec3<float>& viewerPos);
            void syncPhysicsCollider();
            void registerPipelinesToRenderer();
            void unregisterPipelinesFromRenderer();

        private:
            uint32_t m_resolution = kDefaultTerrainResolution; // 129x129 规则高精网格
            float m_gridSpacing = kDefaultTerrainGridSpacing;   // 1米间距，覆盖 128m x 128m
            float m_originX = -64.0f;
            float m_originZ = -64.0f;

            std::vector<float> m_heightfield;
            std::vector<std::array<float, 4>> m_splatmap; // RGBA 4 层权重 (草地, 岩石, 泥土, 雪地)
            std::vector<TerrainClipmapLevel> m_clipmapLevels;
            std::vector<ClipmapRingGeometry> m_clipmapGeometries;
            std::array<TerrainLayerDesc, 4> m_layers;

            bool m_pipelinesRegistered = false;
        };

    } // namespace Terrain
} // namespace MQEngine

#endif // MQENGINE_TERRAIN_RUNTIME_COMPOSITE_H
