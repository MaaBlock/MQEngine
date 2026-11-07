#ifndef EDITOR_UI_CONTEXT_H
#define EDITOR_UI_CONTEXT_H

#include "UiSignals.h"

namespace MQEngine {

    /**
     * @brief UI层上下文，承载视图状态快照与编译期强类型信号队列，实现UI绘制与行为执行的彻底解耦
     */
    class UiContext {
    public:
        UiContext() = default;
        ~UiContext() = default;

        template <UiViewStateConcept State>
        State& mutableState() {
            constexpr entt::id_type stateId = entt::type_hash<std::decay_t<State>>::value();
            auto it = m_states.find(stateId);
            if (it == m_states.end()) {
                auto holder = std::make_unique<StateHolder<std::decay_t<State>>>();
                auto* rawPtr = &holder->value;
                m_states.emplace(stateId, std::move(holder));
                return *rawPtr;
            }
            return static_cast<StateHolder<std::decay_t<State>>*>(it->second.get())->value;
        }

        template <UiViewStateConcept State>
        const State& state() const {
            constexpr entt::id_type stateId = entt::type_hash<std::decay_t<State>>::value();
            auto it = m_states.find(stateId);
            if (it != m_states.end()) {
                return static_cast<const StateHolder<std::decay_t<State>>*>(it->second.get())->value;
            }
            static const std::decay_t<State> s_defaultState{};
            return s_defaultState;
        }

        template <UiViewStateConcept State, typename SyncFn>
        void registerStateSynchronizer(SyncFn&& syncFn) {
            using CleanState = std::decay_t<State>;
            constexpr entt::id_type stateId = entt::type_hash<CleanState>::value();
            m_stateSynchronizers[stateId] = [this, fn = std::forward<SyncFn>(syncFn)]() {
                fn(mutableState<CleanState>());
            };
        }

        template <UiSignalConcept Signal, typename HandlerFn>
        void registerSignalHandler(HandlerFn&& handlerFn) {
            using CleanSignal = std::decay_t<Signal>;
            constexpr entt::id_type signalId = entt::type_hash<CleanSignal>::value();
            m_signalHandlers[signalId].push_back(
                [fn = std::forward<HandlerFn>(handlerFn)](const void* rawSignal) {
                    fn(*static_cast<const CleanSignal*>(rawSignal));
                });
        }

        template <UiSignalConcept Signal>
        void emit(Signal&& signal) {
            using CleanSignal = std::decay_t<Signal>;
            constexpr entt::id_type signalId = entt::type_hash<CleanSignal>::value();
            CleanSignal capturedSignal(std::forward<Signal>(signal));
            m_pendingDispatches.push_back([this, signalId, payload = std::move(capturedSignal)]() {
                dispatchRawSignal(signalId, &payload);
            });
        }

        template <UiSignalConcept Signal>
        void emitImmediate(const Signal& signal) {
            using CleanSignal = std::decay_t<Signal>;
            constexpr entt::id_type signalId = entt::type_hash<CleanSignal>::value();
            dispatchRawSignal(signalId, &signal);
        }

        void syncAllStates() {
            for (auto& [stateId, syncFn] : m_stateSynchronizers) {
                if (syncFn) {
                    syncFn();
                }
            }
        }

        void dispatchPendingSignals() {
            if (m_pendingDispatches.empty()) {
                return;
            }
            std::vector<std::function<void()>> currentBatch;
            currentBatch.swap(m_pendingDispatches);
            for (auto& task : currentBatch) {
                if (task) {
                    task();
                }
            }
        }

        void clearBindings() {
            m_stateSynchronizers.clear();
            m_signalHandlers.clear();
            m_pendingDispatches.clear();
        }

    private:
        struct IStateHolder {
            virtual ~IStateHolder() = default;
        };

        template <typename State>
        struct StateHolder : IStateHolder {
            State value{};
        };

        using ErasedSignalHandler = std::function<void(const void*)>;

        void dispatchRawSignal(entt::id_type signalId, const void* rawSignal) {
            auto it = m_signalHandlers.find(signalId);
            if (it == m_signalHandlers.end()) {
                return;
            }
            for (const auto& handler : it->second) {
                if (handler) {
                    handler(rawSignal);
                }
            }
        }

        std::unordered_map<entt::id_type, std::unique_ptr<IStateHolder>> m_states;
        std::unordered_map<entt::id_type, std::function<void()>> m_stateSynchronizers;
        std::unordered_map<entt::id_type, std::vector<ErasedSignalHandler>> m_signalHandlers;
        std::vector<std::function<void()>> m_pendingDispatches;
    };

} // namespace MQEngine

#endif // EDITOR_UI_CONTEXT_H
