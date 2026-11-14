/**
 * @file SceneViewerTypes.h
 * @brief 场景视口、实体描述、相机漫游状态与 3D Gizmo 操纵相关数据结构定义
 */
#ifndef MQENGINE_MQ_EDITOR_SCENE_VIEWER_TYPES_H
#define MQENGINE_MQ_EDITOR_SCENE_VIEWER_TYPES_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/domain/scene/SceneComponents.h>

#include <cstdint>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace MQEditor
    {
        using Domain::Scene::EditorNameComponent;
        using Domain::Scene::UuidComponent;
        using Domain::Scene::DisabledTag;
        using Domain::Scene::CameraComponent;
        using Domain::Scene::DirectionalLightComponent;
        using Domain::Scene::PointLightComponent;
        using Domain::Scene::StaticMeshComponent;
        using Domain::Scene::PbrMaterialComponent;
        using Domain::Scene::RigidBodyComponent;
        using Domain::Scene::NativeScriptComponent;
        using Domain::Scene::NativeScriptSlot;
        using Domain::Scene::ScriptPropertyValue;

        /**
         * @brief 编辑器 3D 视口活跃相机源模式（编辑态与 PIE 运行态均支持无锁自由切换）
         */
        enum class ViewportCameraSource : uint8_t
        {
            EditorFree = 0,
            ActiveGameMain = 1,
            SpecificEntity = 2
        };

        /**
         * @brief 3D 视口变换操纵器 (Gizmo) 操作模式枚举
         */
        enum class EGizmoMode : int
        {
            Translate = 0,
            Rotate = 1,
            Scale = 2
        };

        /**
         * @brief 3D 视口变换操纵器选中轴向枚举
         */
        enum class EGizmoAxis : int
        {
            None = -1,
            X = 0,
            Y = 1,
            Z = 2
        };

        /**
         * @brief 编辑器内置场景实体创建预设分类枚举
         */
        enum class EntityPresetKind : uint8_t
        {
            EmptyNode = 0,
            LogicEntity,
            NativeScriptEntity,
            Cube,
            Plane,
            Sphere,
            StaticMesh,
            DirectionalLight,
            PointLight,
            PerspectiveCamera,
            OrthographicCamera,
            StaticBoxCollider,
            DynamicBox,
            DynamicSphere,
            TriggerVolume
        };

        /**
         * @brief 编辑器 3D 视口相机参数与漫游状态
         */
        struct ViewportCameraState
        {
            float position[3] = {0.0f, 4.5f, -9.5f};
            float target[3] = {0.0f, 1.0f, 0.0f};
            float rotation[3] = {18.0f, 0.0f, 0.0f};
            float fov = 60.0f;
            float nearPlane = 0.1f;
            float farPlane = 1000.0f;
            float flySpeed = 8.0f;
            float lastAspect = 16.0f / 9.0f;
        };

        /**
         * @brief 视口实体拾取与 Gizmo 拖拽状态
         */
        struct GizmoInteractionState
        {
            int mode = 0; // 0: Translate, 1: Rotate, 2: Scale
            int activeAxis = -1; // -1: None, 0: X, 1: Y, 2: Z
            float dragStartVal[3] = {0.0f, 0.0f, 0.0f};
            ImVec2 dragStartMouse = ImVec2(0.0f, 0.0f);
        };

        /**
         * @brief 编辑器场景实体描述与属性缓存结构
         */
        struct SceneEntityDesc
        {
            uint32_t id = 0;
            FCT::ECS::entity ecsEntity = FCT::ECS::null;
            std::string name;
            std::string uuid;
            std::string registryName = "Main";
            bool active = true;
            uint32_t parentId = 0;
            bool isExpanded = true;
            bool isSelected = false;

            // Transform 变换属性（纯逻辑实体设为 false 时不挂接空间变换组件）
            bool hasTransform = true;
            float position[3] = {0.0f, 0.0f, 0.0f};
            float rotation[3] = {0.0f, 0.0f, 0.0f};
            float scale[3] = {1.0f, 1.0f, 1.0f};

            // 组件标记与参数
            bool hasCamera = false;
            bool cameraActive = true;
            int cameraPriority = 0;
            std::string cameraTargetName = "MainWindow";
            float cameraFov = 60.0f;
            float cameraNear = 0.1f;
            float cameraFar = 1000.0f;
            bool cameraOrthographic = false;
            float cameraOrthoSize = 24.0f;
            bool hasMeshRenderer = false;
            bool hasDirectionalLight = false;
            bool hasPointLight = false;
            bool hasScript = false;
            bool castShadows = true;
            int primitiveType = 0;
            float baseColor[3] = {0.8f, 0.8f, 0.85f};
            float metallic = 0.1f;
            float roughness = 0.45f;
            float lightDirection[3] = {-0.5511f, -0.6820f, 0.4808f};
            float lightColor[3] = {1.0f, 0.95f, 0.86f};
            float lightIntensity = 4.2f;
            float pointLightColor[3] = {1.0f, 0.80f, 0.48f};
            float pointLightIntensity = 11.0f;
            float pointLightRange = 14.0f;
            std::string meshName = "Cube";
            std::string materialName = "DefaultPBR";
            std::string scriptPath;
            FCT::StaticMesh<uint32_t>* customMesh = nullptr;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_SCENE_VIEWER_TYPES_H
