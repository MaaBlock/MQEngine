/**
 * @file Domain.h
 * @brief Domain 层顶层门面类：继承自 CoreEngineModule<Domain>，
 *        聚合与统管 10 大高级业务子域
 */
#ifndef MQENGINE_DOMAIN_CORE_DOMAIN_H
#define MQENGINE_DOMAIN_CORE_DOMAIN_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/script/js/JSSubsystem.h>
#include <MQEngine/domain/script/py/PySubsystem.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/domain/hid/HIDSubsystem.h>
#include <MQEngine/domain/debug/DebugSubsystem.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/animation/AnimationSubsystem.h>
#include <MQEngine/domain/effect/EffectSubsystem.h>
#include <MQEngine/domain/gameplay/GamePlaySubsystem.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <string>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        /**
         * @brief Domain 核心门面类
         */
        class ENGINE_API Domain : public CoreEngineModule<Domain>
        {
        public:
            Domain();
            ~Domain();

            Domain(const Domain&) = delete;
            Domain& operator=(const Domain&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            /**
             * @brief 根据当前 Application Tags 声明式同步场景/游戏逻辑与物理步进 Ticker
             */
            Status reconcileApplicationTags(const std::vector<std::string>& activeTags);

            // 子域访问器
            Script::JSSubsystem* getJS() noexcept { return &js; }
            const Script::JSSubsystem* getJS() const noexcept { return &js; }

            Script::PySubsystem* getPy() noexcept { return &py; }
            const Script::PySubsystem* getPy() const noexcept { return &py; }

            Physics::PhysicsSubsystem* getPhysics() noexcept { return &physics; }
            const Physics::PhysicsSubsystem* getPhysics() const noexcept { return &physics; }

            HID::HIDSubsystem* getHID() noexcept { return &hid; }
            const HID::HIDSubsystem* getHID() const noexcept { return &hid; }

            Debug::DebugSubsystem* getDebug() noexcept { return &debug; }
            const Debug::DebugSubsystem* getDebug() const noexcept { return &debug; }

            Renderer::RendererSubsystem* getRenderer() noexcept { return &renderer; }
            const Renderer::RendererSubsystem* getRenderer() const noexcept { return &renderer; }

            Scene::SceneSubsystem* getScene() noexcept { return &scene; }
            const Scene::SceneSubsystem* getScene() const noexcept { return &scene; }

            Animation::AnimationSubsystem* getAnimation() noexcept { return &animation; }
            const Animation::AnimationSubsystem* getAnimation() const noexcept { return &animation; }

            Effect::EffectSubsystem* getEffect() noexcept { return &effect; }
            const Effect::EffectSubsystem* getEffect() const noexcept { return &effect; }

            GamePlay::GamePlaySubsystem* getGamePlay() noexcept { return &gameplay; }
            const GamePlay::GamePlaySubsystem* getGamePlay() const noexcept { return &gameplay; }

            bool isInitialized() const noexcept { return m_initialized; }

        public:
            Script::JSSubsystem js;
            Script::PySubsystem py;
            Physics::PhysicsSubsystem physics;
            HID::HIDSubsystem hid;
            Debug::DebugSubsystem debug;
            Renderer::RendererSubsystem renderer;
            Scene::SceneSubsystem scene;
            Animation::AnimationSubsystem animation;
            Effect::EffectSubsystem effect;
            GamePlay::GamePlaySubsystem gameplay;

        private:
            void activateSceneTicker();
            void deactivateSceneTicker() noexcept;
            void activateGameplayTicker();
            void deactivateGameplayTicker() noexcept;
            void unregisterTickersFromLogicHost() noexcept;

        private:
            bool m_initialized = false;
        };

    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_CORE_DOMAIN_H
