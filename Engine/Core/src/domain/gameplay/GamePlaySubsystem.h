/**
 * @file GamePlaySubsystem.h
 * @brief 游戏业务层 (GamePlay) 子域：
 *        管理游戏有限状态机 (GameStateMachine)、驱动实体原生行为脚本生命周期 (onStart/onUpdate/onFixedUpdate/onDestroy)、
 *        提供游戏定时器与业务逻辑帧调度（严格无锁单线程所有权）
 */
#ifndef MQENGINE_DOMAIN_GAMEPLAY_GAMEPLAYSUBSYSTEM_H
#define MQENGINE_DOMAIN_GAMEPLAY_GAMEPLAYSUBSYSTEM_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/domain/classdef.h>
#include <MQEngine/domain/gameplay/NativeScript.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Domain
    {
        namespace GamePlay
        {
            /**
              * @brief 游戏顶层全局状态枚举
              */
            enum class GameState : uint8_t
            {
                Boot = 0,
                MainMenu = 1,
                Playing = 2,
                Paused = 3,
                GameOver = 4
            };

            /**
             * @brief 实体原生行为脚本组件
             */
            struct ENGINE_API BehaviorComponent
            {
                std::string behaviorName;
                bool started = false;
                std::function<void(FCT::ECS::entity)> onStart;
                std::function<void(FCT::ECS::entity, float)> onUpdate;
                std::function<void(FCT::ECS::entity, float)> onFixedUpdate;
                std::function<void(FCT::ECS::entity)> onDestroy;
            };

            /**
             * @brief 游戏状态机
             */
            class ENGINE_API GameStateMachine
            {
            public:
                GameStateMachine() = default;
                ~GameStateMachine() = default;

                GameState getCurrentState() const noexcept { return m_currentState; }
                void transitionTo(GameState newState);

                using StateChangeCallback = std::function<void(GameState oldState, GameState newState)>;
                void onStateChanged(StateChangeCallback callback);

            private:
                GameState m_currentState = GameState::Boot;
                std::vector<StateChangeCallback> m_callbacks;
            };

            /**
             * @brief 游戏定时器结构
             */
            struct GameplayTimer
            {
                uint64_t id = 0;
                float duration = 0.0f;
                float elapsed = 0.0f;
                bool repeating = false;
                std::function<void()> callback;
            };

            /**
             * @brief 游戏业务子系统核心类（单宿主逻辑线程驱动，严格无锁）
             */
            class ENGINE_API GamePlaySubsystem
            {
            public:
                using GameplayTickCallback = std::function<void(float deltaTime)>;

                GamePlaySubsystem();
                ~GamePlaySubsystem();

                GamePlaySubsystem(const GamePlaySubsystem&) = delete;
                GamePlaySubsystem& operator=(const GamePlaySubsystem&) = delete;

                void registerInfo();
                Status init();
                void term() noexcept;

                /**
                 * @brief 驱动逻辑帧行为更新与定时器推进
                 * @param deltaTime 逻辑帧流逝时间 (秒)
                 * @param registry ECS 关系表
                 */
                void update(float deltaTime, FCT::ECS::registry& registry);

                /**
                 * @brief 驱动物理帧行为更新
                 */
                void fixedUpdate(float fixedDeltaTime, FCT::ECS::registry& registry);

                /**
                 * @brief 实体销毁通知，触发 BehaviorComponent 与 NativeScriptComponent 的 onDestroy
                 */
                void notifyEntityDestroyed(FCT::ECS::entity entity, FCT::ECS::registry& registry);

                /**
                 * @brief 注册实体挂载式原生 C++ 脚本类描述符
                 */
                void registerNativeScriptDescriptor(NativeScriptClassDescriptor descriptor);

                template <typename TScript>
                    requires std::derived_from<TScript, NativeScript>
                void registerNativeScript(
                    std::string className = {},
                    std::string moduleOwner = {},
                    std::string sourceFileHint = {})
                {
                    registerNativeScriptDescriptor(
                        BuildNativeScriptClassDescriptor<TScript>(
                            std::move(className),
                            std::move(moduleOwner),
                            std::move(sourceFileHint)));
                }

                void unregisterNativeScriptsByOwner(std::string_view moduleOwner);

                [[nodiscard]] std::vector<std::string> getRegisteredNativeScriptNames() const;
                [[nodiscard]] const NativeScriptClassDescriptor* getNativeScriptDescriptor(
                    std::string_view className) const noexcept;

                [[nodiscard]] NativeScript* getLiveNativeScript(
                    const FCT::ECS::registry& registry,
                    FCT::ECS::entity entity,
                    std::string_view className) const noexcept;

                [[nodiscard]] NativeScript* findLiveScriptByTypeHash(
                    const FCT::ECS::registry* preferredRegistry,
                    size_t typeHash) const noexcept;

                [[nodiscard]] NativeScript* findLiveScriptOnEntityByTypeHash(
                    const FCT::ECS::registry* registry,
                    FCT::ECS::entity entity,
                    size_t typeHash) const noexcept;

                bool syncLiveScriptPropertiesToSlot(
                    const FCT::ECS::registry& registry,
                    FCT::ECS::entity entity,
                    Scene::NativeScriptSlot& slot) const;

                bool applySlotPropertiesToLiveScript(
                    const FCT::ECS::registry& registry,
                    FCT::ECS::entity entity,
                    const Scene::NativeScriptSlot& slot);

                /**
                 * @brief 逆序销毁全部运行中的原生脚本实例（在退出 PIE、清空场景或卸载脚本动态库前确定性触发 onDestroy）
                 */
                void shutdownAllNativeScriptInstances(FCT::ECS::registry* targetRegistry = nullptr) noexcept;

                /**
                 * @brief 扫描并加载工程 Native 目录下的原生脚本动态库（导出 RegisterMQNativeScripts）
                 */
                Status loadProjectNativeScriptLibraries(const std::string& nativeDir);
                void unloadNativeScriptLibraries() noexcept;

                /**
                 * @brief 注册通用 Gameplay 逻辑帧回调
                 */
                void registerTickCallback(const std::string& name, GameplayTickCallback cb);
                void unregisterTickCallback(const std::string& name);

                /**
                 * @brief 注册延迟或循环定时器
                 */
                uint64_t setTimer(float durationSec, bool repeating, std::function<void()> cb);
                void clearTimer(uint64_t timerId);

                /**
                 * @brief 常用状态控制快捷接口
                 */
                void pauseGame();
                void resumeGame();
                void restartGame();

                /**
                 * @brief 状态机访问器
                 */
                GameStateMachine& getStateMachine() noexcept { return m_stateMachine; }
                const GameStateMachine& getStateMachine() const noexcept { return m_stateMachine; }

                bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }

            private:
                void updateTimers(float deltaTime);
                void updateNativeScripts(float deltaTime, FCT::ECS::registry& registry);
                void fixedUpdateNativeScripts(float fixedDeltaTime, FCT::ECS::registry& registry);
                void reconcileStaleNativeScriptInstances(FCT::ECS::registry& registry);

            private:
                std::atomic<bool> m_initialized{false};
                GameStateMachine m_stateMachine;

                struct NamedTickCallback
                {
                    std::string name;
                    GameplayTickCallback callback;
                };
                std::vector<NamedTickCallback> m_tickCallbacks;

                std::vector<GameplayTimer> m_timers;
                uint64_t m_nextTimerId = 1;

                struct LiveNativeScriptRecord
                {
                    FCT::ECS::registry* registry = nullptr;
                    FCT::ECS::entity entity = FCT::ECS::null;
                    std::string className;
                    std::string moduleOwner;
                    size_t typeHash = 0;
                    bool started = false;
                    UniquePtr<NativeScript> instance;
                };

                struct LoadedNativeScriptLibrary
                {
                    std::string normalizedPath;
                    std::string moduleName;
                    void* handle = nullptr;
                };

                std::unordered_map<std::string, NativeScriptClassDescriptor> m_scriptDescriptors;
                std::vector<LiveNativeScriptRecord> m_liveScripts;
                std::vector<LoadedNativeScriptLibrary> m_loadedScriptLibraries;
                std::string m_activeNativeScriptDir;
#ifdef _WIN32
                std::vector<void*> m_dllDirectoryCookies;
#endif
            };

        } // namespace GamePlay
    } // namespace Domain
} // namespace MQEngine

#endif // MQENGINE_DOMAIN_GAMEPLAY_GAMEPLAYSUBSYSTEM_H
