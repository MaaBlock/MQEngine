/**
 * @file NativeScript.h
 * @brief 实体挂载式原生 C++ 游戏脚本基类、C++20 零宏属性反射与动态库自注册模板
 */
#ifndef MQENGINE_DOMAIN_GAMEPLAY_NATIVESCRIPT_H
#define MQENGINE_DOMAIN_GAMEPLAY_NATIVESCRIPT_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/resource/table/TypeMetaTable.h>
#include <MQEngine/runtime/core/CreateApplication.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <boost/pfr.hpp>
#include <boost/pfr/core_name.hpp>
#include <cassert>
#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace MQEngine
{
    namespace Runtime
    {
        class Engine;
    } // namespace Runtime

    namespace Domain
    {
        namespace Scene
        {
            class SceneSubsystem;
        } // namespace Scene

        namespace HID
        {
            class HIDSubsystem;
        } // namespace HID

        namespace Renderer
        {
            class RendererSubsystem;
        } // namespace Renderer

        namespace GamePlay
        {
            class GamePlaySubsystem;

            /**
             * @brief 原生脚本反射属性元数据描述
             */
            struct NativeScriptPropertyMeta
            {
                std::string name;
                Resource::EFieldPrimitiveKind kind = Resource::EFieldPrimitiveKind::Unknown;
                Scene::ScriptPropertyValue defaultValue{};
            };

            /**
             * @brief 挂载在场景实体上的原生 C++ 游戏脚本基类：
             *        生命周期由 GamePlaySubsystem 在 "Game" 标签激活期间驱动，与编辑器态严格隔离
             */
            class ENGINE_API NativeScript
            {
            public:
                NativeScript() = default;
                virtual ~NativeScript() = default;

                NativeScript(const NativeScript&) = delete;
                NativeScript& operator=(const NativeScript&) = delete;

                virtual void onCreate() {}
                virtual void onStart() {}
                virtual void onUpdate(float /*deltaTime*/) {}
                virtual void onFixedUpdate(float /*fixedDeltaTime*/) {}
                virtual void onDestroy() {}

                [[nodiscard]] FCT::ECS::entity getEntity() const noexcept { return m_entity; }
                [[nodiscard]] FCT::ECS::registry* tryGetRegistry() const noexcept { return m_registry; }
                [[nodiscard]] FCT::ECS::registry& getRegistry() const noexcept
                {
                    assert(m_registry != nullptr && "NativeScript 尚未绑定有效的 ECS Registry");
                    return *m_registry;
                }

                [[nodiscard]] Runtime::Engine* getEngine() const noexcept { return m_engine; }
                [[nodiscard]] GamePlaySubsystem* tryGetGameplay() const noexcept { return m_gameplay; }
                [[nodiscard]] Scene::SceneSubsystem* tryGetScene() const noexcept;
                [[nodiscard]] HID::HIDSubsystem* tryGetHid() const noexcept;
                [[nodiscard]] Renderer::RendererSubsystem* tryGetRenderer() const noexcept;

                template <typename T>
                [[nodiscard]] bool hasComponent() const noexcept
                {
                    return m_registry != nullptr &&
                           m_entity != FCT::ECS::null &&
                           m_registry->valid(m_entity) &&
                           m_registry->all_of<T>(m_entity);
                }

                template <typename T>
                [[nodiscard]] T& getComponent()
                {
                    assert(hasComponent<T>() && "宿主实体未挂载目标 ECS 组件");
                    return m_registry->get<T>(m_entity);
                }

                template <typename T>
                [[nodiscard]] const T& getComponent() const
                {
                    assert(hasComponent<T>() && "宿主实体未挂载目标 ECS 组件");
                    return m_registry->get<T>(m_entity);
                }

                template <typename T>
                [[nodiscard]] T* tryGetComponent() noexcept
                {
                    if (!m_registry || m_entity == FCT::ECS::null || !m_registry->valid(m_entity)) {
                        return nullptr;
                    }
                    return m_registry->try_get<T>(m_entity);
                }

                template <typename T>
                [[nodiscard]] const T* tryGetComponent() const noexcept
                {
                    if (!m_registry || m_entity == FCT::ECS::null || !m_registry->valid(m_entity)) {
                        return nullptr;
                    }
                    return m_registry->try_get<T>(m_entity);
                }

                template <typename T, typename... Args>
                T& addOrReplaceComponent(Args&&... args)
                {
                    assert(m_registry != nullptr && m_entity != FCT::ECS::null && m_registry->valid(m_entity) &&
                           "NativeScript 宿主实体无效，无法挂载组件");
                    return m_registry->emplace_or_replace<T>(m_entity, std::forward<Args>(args)...);
                }

                template <typename TScript>
                [[nodiscard]] TScript* findFirstScript() const noexcept
                {
                    return static_cast<TScript*>(findLiveScriptByTypeHashImpl(typeid(TScript).hash_code()));
                }

                template <typename TScript>
                [[nodiscard]] TScript* getScriptOnEntity(FCT::ECS::entity targetEntity) const noexcept
                {
                    return static_cast<TScript*>(
                        findLiveScriptOnEntityByTypeHashImpl(targetEntity, typeid(TScript).hash_code()));
                }

            private:
                friend class GamePlaySubsystem;

                void bindRuntimeContext(
                    FCT::ECS::entity entity,
                    FCT::ECS::registry* registry,
                    Runtime::Engine* engine,
                    GamePlaySubsystem* gameplay) noexcept
                {
                    m_entity = entity;
                    m_registry = registry;
                    m_engine = engine;
                    m_gameplay = gameplay;
                }

                [[nodiscard]] NativeScript* findLiveScriptByTypeHashImpl(size_t typeHash) const noexcept;
                [[nodiscard]] NativeScript* findLiveScriptOnEntityByTypeHashImpl(
                    FCT::ECS::entity targetEntity,
                    size_t typeHash) const noexcept;

                FCT::ECS::entity m_entity = FCT::ECS::null;
                FCT::ECS::registry* m_registry = nullptr;
                Runtime::Engine* m_engine = nullptr;
                GamePlaySubsystem* m_gameplay = nullptr;
            };

            /**
             * @brief 已注册原生脚本类描述符（含工厂与编译期零宏生成的属性序列化桥接器）
             */
            struct NativeScriptClassDescriptor
            {
                std::string className;
                std::string moduleOwner;
                std::string sourceFileHint;
                size_t typeHash = 0;
                std::function<UniquePtr<NativeScript>()> factory;
                std::vector<NativeScriptPropertyMeta> properties;
                std::function<void(NativeScript*, const std::vector<std::pair<std::string, Scene::ScriptPropertyValue>>&)> applyProperties;
                std::function<void(const NativeScript*, std::vector<std::pair<std::string, Scene::ScriptPropertyValue>>&)> extractProperties;
            };

            /**
             * @brief C++20 编译期探测脚本是否内嵌聚合体 Properties props 公开配置字段
             */
            template <typename T>
            concept HasScriptProperties =
                requires(T instance) {
                    typename T::Properties;
                    { instance.props } -> std::same_as<typename T::Properties&>;
                } && std::is_aggregate_v<typename T::Properties>;

            namespace Detail
            {
                template <typename FieldT>
                Scene::ScriptPropertyValue ReadFieldToPropertyValue(const FieldT& field)
                {
                    using CleanT = std::decay_t<FieldT>;
                    if constexpr (std::is_same_v<CleanT, bool>) {
                        return Scene::ScriptPropertyValue::fromBool(field);
                    } else if constexpr (std::is_same_v<CleanT, int32_t>) {
                        return Scene::ScriptPropertyValue::fromInt32(field);
                    } else if constexpr (std::is_same_v<CleanT, uint32_t>) {
                        return Scene::ScriptPropertyValue::fromUInt32(field);
                    } else if constexpr (std::is_same_v<CleanT, float>) {
                        return Scene::ScriptPropertyValue::fromFloat(field);
                    } else if constexpr (std::is_same_v<CleanT, double>) {
                        return Scene::ScriptPropertyValue::fromDouble(field);
                    } else if constexpr (std::is_same_v<CleanT, std::string>) {
                        return Scene::ScriptPropertyValue::fromString(field);
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec2>) {
                        return Scene::ScriptPropertyValue::fromVec2(field);
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec3<>>) {
                        return Scene::ScriptPropertyValue::fromVec3(field);
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec4>) {
                        return Scene::ScriptPropertyValue::fromVec4(field);
                    } else {
                        return Scene::ScriptPropertyValue{};
                    }
                }

                template <typename FieldT>
                void WritePropertyValueToField(FieldT& field, const Scene::ScriptPropertyValue& val)
                {
                    using CleanT = std::decay_t<FieldT>;
                    if constexpr (std::is_same_v<CleanT, bool>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Bool) {
                            field = val.boolVal;
                        }
                    } else if constexpr (std::is_same_v<CleanT, int32_t>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Int32) {
                            field = val.intVal;
                        } else if (val.kind == Resource::EFieldPrimitiveKind::UInt32) {
                            field = static_cast<int32_t>(val.uintVal);
                        } else if (val.kind == Resource::EFieldPrimitiveKind::Float ||
                                   val.kind == Resource::EFieldPrimitiveKind::Double) {
                            field = static_cast<int32_t>(val.floatVal);
                        }
                    } else if constexpr (std::is_same_v<CleanT, uint32_t>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::UInt32) {
                            field = val.uintVal;
                        } else if (val.kind == Resource::EFieldPrimitiveKind::Int32 && val.intVal >= 0) {
                            field = static_cast<uint32_t>(val.intVal);
                        } else if ((val.kind == Resource::EFieldPrimitiveKind::Float ||
                                    val.kind == Resource::EFieldPrimitiveKind::Double) && val.floatVal >= 0.0) {
                            field = static_cast<uint32_t>(val.floatVal);
                        }
                    } else if constexpr (std::is_same_v<CleanT, float>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Float ||
                            val.kind == Resource::EFieldPrimitiveKind::Double) {
                            field = static_cast<float>(val.floatVal);
                        } else if (val.kind == Resource::EFieldPrimitiveKind::Int32) {
                            field = static_cast<float>(val.intVal);
                        } else if (val.kind == Resource::EFieldPrimitiveKind::UInt32) {
                            field = static_cast<float>(val.uintVal);
                        }
                    } else if constexpr (std::is_same_v<CleanT, double>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Float ||
                            val.kind == Resource::EFieldPrimitiveKind::Double) {
                            field = val.floatVal;
                        } else if (val.kind == Resource::EFieldPrimitiveKind::Int32) {
                            field = static_cast<double>(val.intVal);
                        } else if (val.kind == Resource::EFieldPrimitiveKind::UInt32) {
                            field = static_cast<double>(val.uintVal);
                        }
                    } else if constexpr (std::is_same_v<CleanT, std::string>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::String) {
                            field = val.stringVal;
                        }
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec2>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Vec2 ||
                            val.kind == Resource::EFieldPrimitiveKind::Vec3 ||
                            val.kind == Resource::EFieldPrimitiveKind::Vec4) {
                            field = FCT::Vec2(val.vecVal.x, val.vecVal.y);
                        }
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec3<>>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Vec3 ||
                            val.kind == Resource::EFieldPrimitiveKind::Vec4) {
                            field = FCT::Vec3<>(val.vecVal.x, val.vecVal.y, val.vecVal.z);
                        }
                    } else if constexpr (std::is_same_v<CleanT, FCT::Vec4>) {
                        if (val.kind == Resource::EFieldPrimitiveKind::Vec4) {
                            field = val.vecVal;
                        }
                    }
                }

                using DllLocalScriptRegisterCallback = void (*)(GamePlaySubsystem& gameplay, std::string_view moduleOwner);

                inline std::vector<DllLocalScriptRegisterCallback>& GetDllLocalScriptRegistrars()
                {
                    static std::vector<DllLocalScriptRegisterCallback> s_registrars;
                    return s_registrars;
                }
            } // namespace Detail

            /**
             * @brief 通过 Boost.PFR 编译期零宏构建原生脚本类描述符
             */
            template <typename TScript>
                requires std::derived_from<TScript, NativeScript>
            NativeScriptClassDescriptor BuildNativeScriptClassDescriptor(
                std::string className,
                std::string moduleOwner = {},
                std::string sourceFileHint = {})
            {
                if (className.empty()) {
                    className = Resource::ExtractShortTypeName<TScript>();
                }

                NativeScriptClassDescriptor desc{};
                desc.className = std::move(className);
                desc.moduleOwner = std::move(moduleOwner);
                desc.sourceFileHint = std::move(sourceFileHint);
                desc.typeHash = typeid(TScript).hash_code();
                desc.factory = []() -> UniquePtr<NativeScript> {
                    return UniquePtr<NativeScript>(FCT_NEW(TScript));
                };

                if constexpr (HasScriptProperties<TScript>) {
                    using PropsT = typename TScript::Properties;
                    PropsT defaultProps{};
                    constexpr auto fieldNames = boost::pfr::names_as_array<PropsT>();
                    desc.properties.reserve(fieldNames.size());

                    boost::pfr::for_each_field(defaultProps, [&](const auto& field, size_t idx) {
                        using FieldType = std::decay_t<decltype(field)>;
                        constexpr auto kind = Resource::DeduceFieldPrimitiveKind<FieldType>();
                        if constexpr (kind != Resource::EFieldPrimitiveKind::Unknown) {
                            NativeScriptPropertyMeta meta{};
                            meta.name = (idx < fieldNames.size())
                                ? std::string(fieldNames[idx])
                                : ("prop_" + std::to_string(idx));
                            meta.kind = kind;
                            meta.defaultValue = Detail::ReadFieldToPropertyValue(field);
                            desc.properties.push_back(std::move(meta));
                        }
                    });

                    desc.applyProperties = [](
                        NativeScript* rawScript,
                        const std::vector<std::pair<std::string, Scene::ScriptPropertyValue>>& values) {
                        if (!rawScript || values.empty()) {
                            return;
                        }
                        auto* typedScript = static_cast<TScript*>(rawScript);
                        constexpr auto names = boost::pfr::names_as_array<PropsT>();
                        boost::pfr::for_each_field(typedScript->props, [&](auto& field, size_t idx) {
                            const std::string_view fname = (idx < names.size()) ? names[idx] : std::string_view{};
                            for (const auto& kv : values) {
                                if (kv.first == fname) {
                                    Detail::WritePropertyValueToField(field, kv.second);
                                    break;
                                }
                            }
                        });
                    };

                    desc.extractProperties = [](
                        const NativeScript* rawScript,
                        std::vector<std::pair<std::string, Scene::ScriptPropertyValue>>& outValues) {
                        outValues.clear();
                        if (!rawScript) {
                            return;
                        }
                        const auto* typedScript = static_cast<const TScript*>(rawScript);
                        constexpr auto names = boost::pfr::names_as_array<PropsT>();
                        outValues.reserve(names.size());
                        boost::pfr::for_each_field(typedScript->props, [&](const auto& field, size_t idx) {
                            using FieldType = std::decay_t<decltype(field)>;
                            constexpr auto kind = Resource::DeduceFieldPrimitiveKind<FieldType>();
                            if constexpr (kind != Resource::EFieldPrimitiveKind::Unknown) {
                                std::string fname = (idx < names.size())
                                    ? std::string(names[idx])
                                    : ("prop_" + std::to_string(idx));
                                outValues.emplace_back(std::move(fname), Detail::ReadFieldToPropertyValue(field));
                            }
                        });
                    };
                }

                return desc;
            }

            ENGINE_API void RegisterNativeScriptDescriptorInSubsystem(
                GamePlaySubsystem& gameplay,
                NativeScriptClassDescriptor descriptor);

            /**
             * @brief 动态库内零宏脚本类静态自注册器：
             *        每个脚本编译单元只需声明静态实例，无需集中手写模块路由表
             */
            template <typename TScript>
                requires std::derived_from<TScript, NativeScript>
            class NativeScriptRegistrar
            {
            public:
                explicit NativeScriptRegistrar(
                    const char* explicitClassName = "",
                    const char* sourceFileHint = "") noexcept
                {
                    s_className = explicitClassName ? explicitClassName : "";
                    s_sourceFileHint = sourceFileHint ? sourceFileHint : "";
                    Detail::GetDllLocalScriptRegistrars().push_back(&registerIntoSubsystem);
                }

            private:
                static void registerIntoSubsystem(GamePlaySubsystem& gameplay, std::string_view moduleOwner)
                {
                    RegisterNativeScriptDescriptorInSubsystem(
                        gameplay,
                        BuildNativeScriptClassDescriptor<TScript>(
                            s_className,
                            std::string(moduleOwner),
                            s_sourceFileHint));
                }

                inline static std::string s_className;
                inline static std::string s_sourceFileHint;
            };

            /**
             * @brief 供工程 Native 脚本动态库入口函数调用，将当前 DLL 内所有 NativeScriptRegistrar 一次性提交至引擎
             */
            inline void FlushDllLocalNativeScripts(GamePlaySubsystem* gameplay, const char* moduleOwner)
            {
                ::MQEngine::BindModuleSharedMemoryTracker();
                if (!gameplay) {
                    return;
                }
                const std::string_view ownerView = (moduleOwner != nullptr) ? moduleOwner : std::string_view{};
                for (auto fn : Detail::GetDllLocalScriptRegistrars()) {
                    if (fn) {
                        fn(*gameplay, ownerView);
                    }
                }
            }

        } // namespace GamePlay
    } // namespace Domain

    using NativeScript = Domain::GamePlay::NativeScript;

    template <typename TScript>
        requires std::derived_from<TScript, Domain::GamePlay::NativeScript>
    using NativeScriptRegistrar = Domain::GamePlay::NativeScriptRegistrar<TScript>;
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_GAMEPLAY_NATIVESCRIPT_H
