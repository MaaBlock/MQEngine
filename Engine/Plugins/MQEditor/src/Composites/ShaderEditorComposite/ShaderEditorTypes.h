/**
 * @file ShaderEditorTypes.h
 * @brief 着色器节点图与 PBR 材质编辑器核心数据结构定义
 */
#ifndef MQENGINE_MQ_EDITOR_SHADER_EDITOR_TYPES_H
#define MQENGINE_MQ_EDITOR_SHADER_EDITOR_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        /**
         * @brief 着色器节点功能分类枚举
         */
        enum class ShaderNodeCategory : uint8_t
        {
            Texture = 0,
            Constant,
            Math,
            Vector,
            Pbr,
            SystemSlot,
            Output
        };

        /**
         * @brief 着色器引脚数据类型枚举
         */
        enum class ShaderPinType : uint8_t
        {
            Float = 0,
            Float2,
            Float3,
            Float4,
            Matrix4x4,
            Texture2D,
            SamplerState
        };

        /**
         * @brief 着色器节点引脚描述
         */
        struct ShaderPin
        {
            int id = 0;
            std::string name;
            bool isInput = true;
            ShaderPinType type = ShaderPinType::Float4;
        };

        /**
         * @brief 着色器可视化节点图单个节点描述
         */
        struct ShaderGraphNode
        {
            int id = 0;
            std::string title;
            ShaderNodeCategory category = ShaderNodeCategory::Math;
            float pos[2] = {100.0f, 100.0f};
            std::vector<ShaderPin> pins;
            float value = 1.0f;
            float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        };

        /**
         * @brief 着色器节点图有向连线描述
         */
        struct ShaderGraphLink
        {
            int id = 0;
            int startPin = 0;
            int endPin = 0;
        };

        /**
         * @brief PBR 材质球实时预览参数描述
         */
        struct PbrMaterialPreviewParams
        {
            float baseColor[4] = {0.82f, 0.85f, 0.92f, 1.0f};
            float metallic = 0.20f;
            float roughness = 0.35f;
            float ao = 1.0f;
            float emissive[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float lightAzimuthDeg = 42.0f;
            float lightElevationDeg = 48.0f;
            float lightIntensity = 2.4f;
            bool autoRotateSphere = false;
            float sphereYawRad = 0.0f;
        };

        using ShaderNodePin = ShaderPin;
        using ShaderNodeDesc = ShaderGraphNode;
        using ShaderNodeLink = ShaderGraphLink;

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_SHADER_EDITOR_TYPES_H
