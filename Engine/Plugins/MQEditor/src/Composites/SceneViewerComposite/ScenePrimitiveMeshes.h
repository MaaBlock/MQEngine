/**
 * @file ScenePrimitiveMeshes.h
 * @brief 程序化几何基元网格生成器（立方体/平面/球体/天空盒）、默认场景预设实体构建与基础 PBR 着色器源码
 */
#ifndef MQENGINE_MQ_EDITOR_SCENE_PRIMITIVE_MESHES_H
#define MQENGINE_MQ_EDITOR_SCENE_PRIMITIVE_MESHES_H

#include "../../MQEditorApi.h"
#include "SceneViewerTypes.h"

#include <cstdint>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 程序化几何体顶点数据结构
         */
        struct PrimitiveVertex
        {
            float position[3] = {0.0f, 0.0f, 0.0f};
            float normal[3] = {0.0f, 1.0f, 0.0f};
            float uv[2] = {0.0f, 0.0f};
        };

        /**
         * @brief 程序化几何体网格缓冲数据（顶点与索引列表）
         */
        struct PrimitiveMeshData
        {
            std::vector<PrimitiveVertex> vertices;
            std::vector<uint32_t> indices;
        };

        /**
         * @brief 编辑器内置程序化基元网格生成器与基础着色器源码工具类
         */
        class SCENE_VIEWER_COMPOSITE_API ScenePrimitiveMeshes
        {
        public:
            /**
             * @brief 生成标准立方体网格数据
             */
            static PrimitiveMeshData generateCubeMesh(float halfExtent = 0.5f);

            /**
             * @brief 生成细分水平面网格数据
             */
            static PrimitiveMeshData generatePlaneMesh(float halfExtent = 5.0f, uint32_t subdivisions = 10);

            /**
             * @brief 生成高精度平滑法线 UV 经纬球体网格数据
             */
            static PrimitiveMeshData generateSphereMesh(float radius = 0.5f, uint32_t slices = 64, uint32_t stacks = 64);

            /**
             * @brief 生成天空盒内向立方体网格数据
             */
            static PrimitiveMeshData generateSkyboxMesh(float halfExtent = 500.0f);

            /**
             * @brief 获取编辑器视口默认 PBR 顶点着色器 HLSL 源码
             */
            static const char* getDefaultViewportVertexShaderHlsl();

            /**
             * @brief 获取编辑器视口默认 PBR 像素着色器 HLSL 源码
             */
            static const char* getDefaultViewportPixelShaderHlsl();
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_SCENE_PRIMITIVE_MESHES_H
