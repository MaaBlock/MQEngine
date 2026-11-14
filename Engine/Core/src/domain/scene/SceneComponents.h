/**
 * @file SceneComponents.h
 * @brief 场景与渲染/物理共享 ECS 聚合组件定义（支持 Boost.PFR 零宏编译期反射与 SchemaTable 注册）
 */
#ifndef MQENGINE_DOMAIN_SCENE_SCENECOMPONENTS_H
#define MQENGINE_DOMAIN_SCENE_SCENECOMPONENTS_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/resource/table/TypeMetaTable.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace Scene
        {
            /**
             * @brief 实体禁用稀疏标签（零字节；未挂载表示激活，挂载表示实体禁用）
             */
            struct DisabledTag {};

            /**
             * @brief 运行时瞬态程序化实体稀疏标签（零字节；挂载后在 .mqscene 场景保存时自动跳过）
             */
            struct TransientTag {};

            /**
             * @brief 模型锚点实体稀疏标签（零字节，供脚本与蓝图宿主 O(1) 复用锚点）
             */
            struct ModelAnchorTag {};

            /**
             * @brief 实体持久化全局唯一标识组件（仅需持久化或跨实体引用的实体按需挂载）
             */
            struct UuidComponent
            {
                std::string uuid = "00000000-0000-0000-0000-000000000000";
            };

            /**
             * @brief 编辑器态实体显示名称组件（仅在宿主含 Editor / CliEditor 标签时挂载）
             */
            struct EditorNameComponent
            {
                std::string name = "Entity";
            };

            /**
             * @brief 透视/正交相机组件
             */
            struct CameraComponent
            {
                float fov = 60.0f;
                float nearPlane = 0.1f;
                float farPlane = 1000.0f;
                bool active = true;
                bool reverseZ = false;
                FCT::Vec4 clearColor{0.12f, 0.14f, 0.18f, 1.0f};
                bool orthographic = false;
                float orthoSize = 24.0f;
                int priority = 0;
                std::string targetName = "MainWindow";
            };

            /**
             * @brief 主方向光组件
             */
            struct DirectionalLightComponent
            {
                FCT::Vec3<> direction{-0.4f, -0.8f, -0.5f};
                FCT::Vec3<> color{1.0f, 0.98f, 0.92f};
                float intensity = 2.5f;
                bool castShadows = true;
            };

            /**
             * @brief 点光源组件
             */
            struct PointLightComponent
            {
                FCT::Vec3<> color{0.3f, 0.7f, 1.0f};
                float intensity = 3.0f;
                float range = 15.0f;
            };

            /**
             * @brief 静态网格渲染组件（0=Cube, 1=Plane, 2=Sphere）
             */
            struct StaticMeshComponent
            {
                int primitiveType = 0;
                std::string meshAssetPath;
                bool castShadows = true;
                bool visible = true;
                FCT::StaticMesh<uint32_t>* customMesh = nullptr;
            };

            /**
             * @brief 物理基准 PBR 材质组件
             */
            struct PbrMaterialComponent
            {
                FCT::Vec3<> baseColor{0.8f, 0.8f, 0.85f};
                float metallic = 0.1f;
                float roughness = 0.45f;
                float ao = 1.0f;
                FCT::Vec3<> emissiveColor{0.0f, 0.0f, 0.0f};
                float emissiveIntensity = 0.0f;
                std::string shaderAssetPath = "Assets/Shaders/PBR_Standard.hlsl";
            };

            /**
             * @brief 刚体与碰撞体物理组件（bodyType: 0=Static, 1=Dynamic, 2=Kinematic）
             */
            struct RigidBodyComponent
            {
                int bodyType = 0;
                float mass = 1.0f;
                float linearDamping = 0.05f;
                float angularDamping = 0.05f;
                bool useGravity = true;
                int colliderType = 0;
                FCT::Vec3<> colliderHalfExtents{0.5f, 0.5f, 0.5f};
                FCT::Vec3<> linearVelocity{0.0f, 0.0f, 0.0f};
                FCT::Vec3<> angularVelocity{0.0f, 0.0f, 0.0f};
                float restitution = 0.3f;
                float friction = 0.5f;
                bool isTrigger = false;
            };

            /**
             * @brief 原生 C++ 脚本反射属性值容器（纯值语义，安全跨越动态库边界进行检视、撤销重做与 .mqscene 序列化）
             */
            struct ScriptPropertyValue
            {
                Resource::EFieldPrimitiveKind kind = Resource::EFieldPrimitiveKind::Unknown;
                bool boolVal = false;
                int32_t intVal = 0;
                uint32_t uintVal = 0;
                double floatVal = 0.0;
                std::string stringVal;
                FCT::Vec4 vecVal{0.0f, 0.0f, 0.0f, 0.0f};

                static ScriptPropertyValue fromBool(bool v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Bool;
                    out.boolVal = v;
                    return out;
                }

                static ScriptPropertyValue fromInt32(int32_t v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Int32;
                    out.intVal = v;
                    return out;
                }

                static ScriptPropertyValue fromUInt32(uint32_t v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::UInt32;
                    out.uintVal = v;
                    return out;
                }

                static ScriptPropertyValue fromFloat(float v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Float;
                    out.floatVal = static_cast<double>(v);
                    return out;
                }

                static ScriptPropertyValue fromDouble(double v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Double;
                    out.floatVal = v;
                    return out;
                }

                static ScriptPropertyValue fromString(std::string v)
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::String;
                    out.stringVal = std::move(v);
                    return out;
                }

                static ScriptPropertyValue fromVec2(const FCT::Vec2& v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Vec2;
                    out.vecVal = FCT::Vec4(v.x, v.y, 0.0f, 0.0f);
                    return out;
                }

                static ScriptPropertyValue fromVec3(const FCT::Vec3<>& v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Vec3;
                    out.vecVal = FCT::Vec4(v.x, v.y, v.z, 0.0f);
                    return out;
                }

                static ScriptPropertyValue fromVec4(const FCT::Vec4& v) noexcept
                {
                    ScriptPropertyValue out{};
                    out.kind = Resource::EFieldPrimitiveKind::Vec4;
                    out.vecVal = v;
                    return out;
                }
            };

            /**
             * @brief 单个挂载在实体上的原生 C++ 游戏脚本槽位
             */
            struct NativeScriptSlot
            {
                std::string className;
                bool enabled = true;
                std::vector<std::pair<std::string, ScriptPropertyValue>> properties;
            };

            /**
             * @brief 实体挂载的原生 C++ 游戏脚本组件（聚合体，存储池固化于 Engine.dll 内，运行时实例由 GamePlaySubsystem 驱动）
             */
            struct NativeScriptComponent
            {
                std::string scriptClassName;
                std::vector<NativeScriptSlot> scripts;
            };

            inline const ScriptPropertyValue* FindNativeScriptProperty(
                const NativeScriptSlot& slot,
                std::string_view propName) noexcept
            {
                for (const auto& kv : slot.properties) {
                    if (kv.first == propName) {
                        return &kv.second;
                    }
                }
                return nullptr;
            }

            inline void UpsertNativeScriptProperty(
                NativeScriptSlot& slot,
                std::string propName,
                ScriptPropertyValue val)
            {
                for (auto& kv : slot.properties) {
                    if (kv.first == propName) {
                        kv.second = std::move(val);
                        return;
                    }
                }
                slot.properties.emplace_back(std::move(propName), std::move(val));
            }

            inline void NormalizeNativeScriptSlots(NativeScriptComponent& comp)
            {
                if (comp.scripts.empty() && !comp.scriptClassName.empty()) {
                    NativeScriptSlot primary{};
                    primary.className = comp.scriptClassName;
                    primary.enabled = true;
                    comp.scripts.push_back(std::move(primary));
                } else if (!comp.scripts.empty()) {
                    comp.scriptClassName = comp.scripts.front().className;
                }
            }

            /**
             * @brief 编辑器检视面板与 SchemaTable 统一遍历的可反射业务组件 TypeList
             *        新增任何聚合组件只需加入此 TypeList，即可自动获得 Schema 反射、Inspector 检视面板与 ComponentModifyCommand 撤销重做支持
             */
            using InspectableSceneComponents = Resource::ComponentTypeList<
                UuidComponent,
                EditorNameComponent,
                CameraComponent,
                DirectionalLightComponent,
                PointLightComponent,
                StaticMeshComponent,
                PbrMaterialComponent,
                RigidBodyComponent,
                NativeScriptComponent>;

        } // namespace Scene
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_SCENE_SCENECOMPONENTS_H
