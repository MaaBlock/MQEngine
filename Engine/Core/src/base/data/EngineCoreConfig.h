//
// Created by MaaBlock on 2026/11/7.
//

#ifndef MQENGINE_ENGINECONFIG_H
#define MQENGINE_ENGINECONFIG_H

#include <cctype>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include "../../EnginePCH.h"
#include "../../thirdparty/thirdparty.h"
#include "../ConfigDocument.h"
#include "../EngineCLIOption.h"
#include "../StaticTokenGraph.h"
#include "../classdef.h"

namespace MQEngine
{
    namespace Runtime { class Host; }
    namespace Platform { class Platform; }
    namespace Resource { class Repository; }
    namespace Scene { class World; }
    namespace Physics { class Simulator; }
    namespace Animation { class Animator; }
    namespace HID { class Input; }
    namespace Network { class Replication; }
    namespace Script { class Interpreter; }
    namespace Effect { class Effector; }
    namespace Render { class Renderer; }

    /**
     * @brief Base 模块层配置切片（无头模式、区域语言、自动化测试帧数限制等）
     */
    struct BaseModuleConfig
    {
        bool headless = false;
        std::string local;
        int32_t testFrames = 0;

        constexpr bool hasTestFramesLimit() const noexcept { return testFrames > 0; }

        void bindCLI(const EngineCLIOption& cli)
        {
            if (cli.hasOption("headless") || cli.hasOption("no-window")) {
                headless = cli.getBoolOption("headless", false) || cli.getBoolOption("no-window", false);
            }
            if (cli.hasOption("local")) {
                local = cli.getOption("local", local);
            }
            const int32_t frames = cli.getIntOption(
                "test-frames",
                cli.getIntOption("test_frames", cli.getIntOption("frames", testFrames)));
            if (frames > 0) {
                testFrames = frames;
            }
        }
    };

    /**
     * @brief Host 模块层配置切片
     */
    struct HostModuleConfig
    {
        bool autoHotReload = false;
    };

    struct PlatformModuleConfig {};
    struct ResourceModuleConfig {};
    struct SceneModuleConfig {};
    struct PhysicsModuleConfig {};
    struct AnimationModuleConfig {};
    struct HIDModuleConfig {};
    struct NetworkModuleConfig {};
    struct ScriptModuleConfig {};
    struct EffectModuleConfig {};

    /**
     * @brief Render 模块层配置切片（测试帧截图路径、触发帧与捕获目标）
     */
    struct RenderModuleConfig
    {
        std::string screenshotPath;
        int32_t captureFrame = 0;
        std::string captureTarget = "SceneColorTarget";

        bool hasScreenshotPath() const noexcept { return !screenshotPath.empty(); }

        void bindCLI(const EngineCLIOption& cli)
        {
            if (cli.hasOption("screenshot")) {
                screenshotPath = cli.getOption("screenshot", screenshotPath);
            } else if (cli.hasOption("capture_output")) {
                screenshotPath = cli.getOption("capture_output", screenshotPath);
            }
            const int32_t capFrame = cli.getIntOption(
                "capture-frame",
                cli.getIntOption("capture_frame", captureFrame));
            if (capFrame > 0) {
                captureFrame = capFrame;
            }
            if (cli.hasOption("capture-target")) {
                captureTarget = cli.getOption("capture-target", captureTarget);
            } else if (cli.hasOption("capture_target")) {
                captureTarget = cli.getOption("capture_target", captureTarget);
            }
        }
    };

    /**
     * @brief 编译期模块类型到模块配置切片类型的映射特征
     */
    template <typename T>
    struct ModuleConfigTrait
    {
        using Type = T;
    };

    template <> struct ModuleConfigTrait<Base::Foundation>      { using Type = BaseModuleConfig; };
    template <> struct ModuleConfigTrait<Runtime::Host>         { using Type = HostModuleConfig; };
    template <> struct ModuleConfigTrait<Platform::Platform>    { using Type = PlatformModuleConfig; };
    template <> struct ModuleConfigTrait<Resource::Repository>  { using Type = ResourceModuleConfig; };
    template <> struct ModuleConfigTrait<Scene::World>          { using Type = SceneModuleConfig; };
    template <> struct ModuleConfigTrait<Physics::Simulator>    { using Type = PhysicsModuleConfig; };
    template <> struct ModuleConfigTrait<Animation::Animator>   { using Type = AnimationModuleConfig; };
    template <> struct ModuleConfigTrait<HID::Input>            { using Type = HIDModuleConfig; };
    template <> struct ModuleConfigTrait<Network::Replication>  { using Type = NetworkModuleConfig; };
    template <> struct ModuleConfigTrait<Script::Interpreter>   { using Type = ScriptModuleConfig; };
    template <> struct ModuleConfigTrait<Effect::Effector>      { using Type = EffectModuleConfig; };
    template <> struct ModuleConfigTrait<Render::Renderer>      { using Type = RenderModuleConfig; };

    template <typename T>
    using ResolveModuleConfigT = typename ModuleConfigTrait<std::remove_cvref_t<T>>::Type;

    template <typename T>
    concept HasCustomCLIBinder = requires(T& cfg, const EngineCLIOption& cli) {
        cfg.bindCLI(cli);
    };

    namespace Detail
    {
        inline std::string camelToKebab(std::string_view name)
        {
            std::string out;
            out.reserve(name.size() + 4);
            for (size_t i = 0; i < name.size(); ++i) {
                const char c = name[i];
                if (std::isupper(static_cast<unsigned char>(c))) {
                    if (i > 0) {
                        out.push_back('-');
                    }
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        inline std::string camelToSnake(std::string_view name)
        {
            std::string out;
            out.reserve(name.size() + 4);
            for (size_t i = 0; i < name.size(); ++i) {
                const char c = name[i];
                if (std::isupper(static_cast<unsigned char>(c))) {
                    if (i > 0) {
                        out.push_back('_');
                    }
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        template <typename FieldType>
        void bindFieldFromCLI(std::string_view fieldName, FieldType& fieldRef, const EngineCLIOption& cli)
        {
            using Decayed = std::remove_cvref_t<FieldType>;
            const std::string rawKey(fieldName);
            const std::string kebabKey = camelToKebab(fieldName);
            const std::string snakeKey = camelToSnake(fieldName);

            const std::string* matchedKey = nullptr;
            if (cli.hasOption(rawKey)) {
                matchedKey = &rawKey;
            } else if (cli.hasOption(kebabKey)) {
                matchedKey = &kebabKey;
            } else if (cli.hasOption(snakeKey)) {
                matchedKey = &snakeKey;
            }
            if (!matchedKey) {
                return;
            }

            if constexpr (std::is_same_v<Decayed, bool>) {
                fieldRef = cli.getBoolOption(*matchedKey, fieldRef);
            } else if constexpr (std::is_integral_v<Decayed>) {
                fieldRef = static_cast<Decayed>(cli.getIntOption(*matchedKey, static_cast<int32_t>(fieldRef)));
            } else if constexpr (std::is_same_v<Decayed, std::string>) {
                fieldRef = cli.getOption(*matchedKey, fieldRef);
            }
        }

        template <typename Cfg>
        void parseModuleConfigFromCLI(Cfg& cfg, const EngineCLIOption& cli)
        {
            if constexpr (!std::is_empty_v<Cfg> && NB::is_reflectable_struct_v<Cfg>) {
                NB::forEachReflectedField(cfg, [&](std::string_view fieldName, auto& fieldRef) {
                    bindFieldFromCLI(fieldName, fieldRef, cli);
                });
            }
            if constexpr (HasCustomCLIBinder<Cfg>) {
                cfg.bindCLI(cli);
            }
        }
    } // namespace Detail

    namespace Base
    {
        /**
         * @brief 基于 C++20 变参继承、空基类优化（EBO）与 Boost.PFR 零宏编译期反射的引擎核心配置聚合模板
         */
        template <typename... ModuleOrConfigs>
        class EngineCoreConfig : public ResolveModuleConfigT<ModuleOrConfigs>...
        {
        public:
            using ConfigTypeList = TypeList<ResolveModuleConfigT<ModuleOrConfigs>...>;

            template <typename Target>
            constexpr ResolveModuleConfigT<Target>& get() noexcept
            {
                using Resolved = ResolveModuleConfigT<Target>;
                static_assert((std::is_same_v<Resolved, ResolveModuleConfigT<ModuleOrConfigs>> || ...),
                              "目标模块配置未注册在 EngineCoreConfig 中");
                return static_cast<Resolved&>(*this);
            }

            template <typename Target>
            constexpr const ResolveModuleConfigT<Target>& get() const noexcept
            {
                using Resolved = ResolveModuleConfigT<Target>;
                static_assert((std::is_same_v<Resolved, ResolveModuleConfigT<ModuleOrConfigs>> || ...),
                              "目标模块配置未注册在 EngineCoreConfig 中");
                return static_cast<const Resolved&>(*this);
            }

            template <typename Visitor>
            constexpr void forEachModuleConfig(Visitor&& visitor)
            {
                (visitor(static_cast<ResolveModuleConfigT<ModuleOrConfigs>&>(*this)), ...);
            }

            template <typename Visitor>
            constexpr void forEachModuleConfig(Visitor&& visitor) const
            {
                (visitor(static_cast<const ResolveModuleConfigT<ModuleOrConfigs>&>(*this)), ...);
            }

            void fillConfigFromCLI(const EngineCLIOption& cli)
            {
                (Detail::parseModuleConfigFromCLI(static_cast<ResolveModuleConfigT<ModuleOrConfigs>&>(*this), cli), ...);
            }

            void parseFromCLI(const EngineCLIOption& cli)
            {
                fillConfigFromCLI(cli);
            }
        };

        /**
         * @brief 覆盖引擎全部 12 个核心模块层的完整配置类型定义
         */
        using EngineConfig = EngineCoreConfig<
            Base::Foundation,
            Runtime::Host,
            Platform::Platform,
            Resource::Repository,
            Scene::World,
            Physics::Simulator,
            Animation::Animator,
            HID::Input,
            Network::Replication,
            Script::Interpreter,
            Effect::Effector,
            Render::Renderer
        >;

    } // namespace Base

    using Base::EngineCoreConfig;
    using Base::EngineConfig;

} // namespace MQEngine

#endif // MQENGINE_ENGINECONFIG_H
