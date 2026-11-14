/**
 * @file TerrainTypes.h
 * @brief 大世界地形图层材质描述、同心 Clipmap 环、雕刻笔刷模式与几何常量定义
 */
#ifndef MQENGINE_TERRAIN_TYPES_H
#define MQENGINE_TERRAIN_TYPES_H

#include "../../TerrainApi.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <vector>
#include <string>
#include <array>
#include <cstdint>

namespace MQEngine
{
    namespace Terrain
    {
        /**
         * @brief Clipmap 网格默认规格常量
         */
        inline constexpr uint32_t kDefaultTerrainResolution = 129;
        inline constexpr float kDefaultTerrainGridSpacing = 1.0f;
        inline constexpr uint32_t kTerrainClipmapRingLevels = 4;
        inline constexpr uint32_t kTerrainClipmapRingGridN = 32;
        inline constexpr uint32_t kTerrainSplatLayerCount = 4;

        /**
         * @brief 地形材质图层描述符
         */
        struct TerrainLayerDesc
        {
            std::string name;
            float uvScale = 1.0f;
            float roughness = 0.8f;
            float metallic = 0.0f;
            FCT::Vec3<float> tintColor{1.0f, 1.0f, 1.0f};
        };

        /**
         * @brief 单级 GPU Clipmap 同心环状态描述符
         */
        struct TerrainClipmapLevel
        {
            uint32_t levelIndex = 0;
            float gridSpacing = 1.0f;
            float extentMeters = 64.0f;
            FCT::Vec3<float> center{0.0f, 0.0f, 0.0f};
            uint32_t vertexCount = 0;
        };

        /**
         * @brief 单级 Clipmap 环几何顶点与索引缓冲数据
         */
        struct ClipmapRingGeometry
        {
            uint32_t level = 0;
            std::vector<float> vertices; // x, y, z, nx, ny, nz, u, v
            std::vector<uint32_t> indices;
        };

        /**
         * @brief 地形编辑器笔刷工作模式枚举
         */
        enum class TerrainBrushMode : uint8_t
        {
            SculptRaise,
            SculptLower,
            SculptSmooth,
            SculptFlatten,
            PaintSplatmap,
            FoliageScatter
        };

    } // namespace Terrain
} // namespace MQEngine

#endif // MQENGINE_TERRAIN_TYPES_H
