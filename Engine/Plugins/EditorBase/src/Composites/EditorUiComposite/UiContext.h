/**
 * @file UiContext.h
 * @brief 编辑器 UI 上下文，承载视图状态快照与强类型信号路由
 */
#ifndef MQENGINE_EDITOR_BASE_UI_CONTEXT_H
#define MQENGINE_EDITOR_BASE_UI_CONTEXT_H

#include "UiSignals.h"
#include <MQEngine/thirdparty/thirdparty.h>

#include <unordered_map>
#include <vector>
#include <functional>
#include <type_traits>

namespace MQEngine
{
    namespace EditorBase
    {
        /**
         * @brief 编辑器 UI 上下文：
         *        承载视图状态快照（ViewState）与基于 Concept 的强类型信号分发（Signal Dispatch），
         *        实现 UI 渲染与业务逻辑的彻底解耦。
         */
        class UiContext
        {
        public:
            UiContext() = default;
            ~UiContext() = default;

            UiContext(const UiContext&) = delete;
            UiContext& operator=(const UiContext&) = delete;

            template <UiViewStateConcept State>
            State& mutableState()
            {
                const uint32_t stateId = typeHash<std::decay_t<State>>();
                auto it = m_states.find(stateId);
                if (it == m_states.end())
                {
                    auto holder = makeUnique<StateHolder<std::decay_t<State>>>();
                    auto* rawPtr = &holder->value;
                    m_states.emplace(stateId, std::move(holder));
                    return *rawPtr;
                }
                return static_cast<StateHolder<std::decay_t<State>>*>(it->second.get())->value;
            }

            template <UiViewStateConcept State>
            const State& state() const
            {
                const uint32_t stateId = typeHash<std::decay_t<State>>();
                auto it = m_states.find(stateId);
                if (it != m_states.end())
                {
                    return static_cast<const StateHolder<std::decay_t<State>>*>(it->second.get())->value;
                }
                static const std::decay_t<State> s_defaultState{};
                return s_defaultState;
            }

            template <UiViewStateConcept State, typename SyncFn>
            void registerStateSynchronizer(SyncFn&& syncFn)
            {
                using CleanState = std::decay_t<State>;
                const uint32_t stateId = typeHash<CleanState>();
                m_stateSynchronizers[stateId] = [this, fn = std::forward<SyncFn>(syncFn)]() {
                    fn(mutableState<CleanState>());
                };
            }

            template <UiSignalConcept Signal, typename HandlerFn>
            void registerSignalHandler(HandlerFn&& handlerFn)
            {
                using CleanSignal = std::decay_t<Signal>;
                const uint32_t signalId = typeHash<CleanSignal>();
                m_signalHandlers[signalId].push_back(
                    [fn = std::forward<HandlerFn>(handlerFn)](const void* rawSignal) {
                        fn(*static_cast<const CleanSignal*>(rawSignal));
                    });
            }

            template <UiSignalConcept Signal>
            size_t getSignalHandlerCount() const noexcept
            {
                using CleanSignal = std::decay_t<Signal>;
                const uint32_t signalId = typeHash<CleanSignal>();
                auto it = m_signalHandlers.find(signalId);
                return it != m_signalHandlers.end() ? it->second.size() : 0;
            }

            template <UiSignalConcept Signal>
            void emit(Signal&& signal)
            {
                using CleanSignal = std::decay_t<Signal>;
                const uint32_t signalId = typeHash<CleanSignal>();
                CleanSignal capturedSignal(std::forward<Signal>(signal));
                m_pendingDispatches.push_back([this, signalId, payload = std::move(capturedSignal)]() {
                    dispatchRawSignal(signalId, &payload);
                });
            }

            template <UiSignalConcept Signal>
            void emitImmediate(const Signal& signal)
            {
                using CleanSignal = std::decay_t<Signal>;
                const uint32_t signalId = typeHash<CleanSignal>();
                dispatchRawSignal(signalId, &signal);
            }

            void syncAllStates()
            {
                for (auto& [stateId, syncFn] : m_stateSynchronizers)
                {
                    if (syncFn)
                    {
                        syncFn();
                    }
                }
            }

            void dispatchPendingSignals()
            {
                if (m_pendingDispatches.empty())
                {
                    return;
                }
                std::vector<std::function<void()>> currentBatch;
                currentBatch.swap(m_pendingDispatches);
                for (auto& task : currentBatch)
                {
                    if (task)
                    {
                        task();
                    }
                }
            }

            void clear()
            {
                m_states.clear();
                m_stateSynchronizers.clear();
                m_signalHandlers.clear();
                m_pendingDispatches.clear();
            }

        private:
            template <typename T>
            static inline uint32_t typeHash() noexcept
            {
                static const uint32_t s_id = []() {
                    const char* name = typeid(T).name();
                    uint32_t hash = 2166136261u;
                    while (*name)
                    {
                        hash ^= static_cast<uint8_t>(*name++);
                        hash *= 16777619u;
                    }
                    return hash;
                }();
                return s_id;
            }

            struct IStateHolder
            {
                virtual ~IStateHolder() = default;
            };

            template <typename State>
            struct StateHolder : IStateHolder
            {
                State value{};
            };

            using ErasedSignalHandler = std::function<void(const void*)>;

            void dispatchRawSignal(uint32_t signalId, const void* rawSignal)
            {
                auto it = m_signalHandlers.find(signalId);
                if (it == m_signalHandlers.end())
                {
                    return;
                }
                for (const auto& handler : it->second)
                {
                    if (handler)
                    {
                        handler(rawSignal);
                    }
                }
            }

        private:
            std::unordered_map<uint32_t, UniquePtr<IStateHolder>> m_states;
            std::unordered_map<uint32_t, std::function<void()>> m_stateSynchronizers;
            std::unordered_map<uint32_t, std::vector<ErasedSignalHandler>> m_signalHandlers;
            std::vector<std::function<void()>> m_pendingDispatches;
        };

    } // namespace EditorBase
} // namespace MQEngine

#endif // MQENGINE_EDITOR_BASE_UI_CONTEXT_H
