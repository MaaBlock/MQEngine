#ifndef EDITOR_DECLARATIVE_UI_BINDER_H
#define EDITOR_DECLARATIVE_UI_BINDER_H

#include "../UI/UiContext.h"

namespace MQEngine {

    template <typename Rule, typename Controller>
    concept UiBindingRuleConcept = requires(UiContext& ctx, Controller& controller) {
        { Rule::bind(ctx, controller) } -> std::same_as<void>;
    };

    /**
     * @brief 声明式视图状态同步规则：在编译期将 ViewState 类型静态映射到控制器的同步成员函数
     */
    template <UiViewStateConcept StateType, auto SyncMemberPtr>
    struct SyncState {
        template <typename Controller>
        static void bind(UiContext& ctx, Controller& controller) {
            static_assert(
                std::is_invocable_v<decltype(SyncMemberPtr), Controller*, StateType&>,
                "SyncState 绑定的成员函数签名必须兼容 void(Controller::*)(StateType&)");

            ctx.registerStateSynchronizer<StateType>([&controller](StateType& state) {
                std::invoke(SyncMemberPtr, &controller, state);
            });
        }
    };

    /**
     * @brief 声明式UI信号响应规则：在编译期将强类型 Signal 静态映射到控制器的行为成员函数
     */
    template <UiSignalConcept SignalType, auto HandlerMemberPtr>
    struct OnSignal {
        template <typename Controller>
        static void bind(UiContext& ctx, Controller& controller) {
            static_assert(
                std::is_invocable_v<decltype(HandlerMemberPtr), Controller*, const SignalType&>,
                "OnSignal 绑定的成员函数签名必须兼容 void/Status(Controller::*)(const SignalType&)");

            ctx.registerSignalHandler<SignalType>([&controller](const SignalType& signal) {
                using ReturnType = std::invoke_result_t<decltype(HandlerMemberPtr), Controller*, const SignalType&>;
                if constexpr (std::is_same_v<ReturnType, Status>) {
                    Status st = std::invoke(HandlerMemberPtr, &controller, signal);
                    if (!st.ok()) {
                        spdlog::error("UI 行为执行失败: {}", st.message());
                    }
                } else {
                    std::invoke(HandlerMemberPtr, &controller, signal);
                }
            });
        }
    };

    /**
     * @brief 声明式组件添加元编程绑定规则：展开组件类型元组，批量绑定 AddTypedComponentSignal<T>
     */
    template <typename ComponentTuple>
    struct BindComponentAddTuple;

    template <typename... ComponentTypes>
    struct BindComponentAddTuple<std::tuple<ComponentTypes...>> {
        template <typename Controller>
        static void bind(UiContext& ctx, Controller& controller) {
            (bindSingle<ComponentTypes>(ctx, controller), ...);
        }

    private:
        template <typename ComponentType, typename Controller>
        static void bindSingle(UiContext& ctx, Controller& controller) {
            using SignalType = AddTypedComponentSignal<ComponentType>;
            ctx.registerSignalHandler<SignalType>([&controller](const SignalType& signal) {
                controller.template onAddTypedComponent<ComponentType>(signal);
            });
        }
    };

    /**
     * @brief 声明式组件更新元编程绑定规则：展开组件类型元组，批量绑定 UpdateComponentSignal<T>
     */
    template <typename ComponentTuple>
    struct BindComponentUpdateTuple;

    template <typename... ComponentTypes>
    struct BindComponentUpdateTuple<std::tuple<ComponentTypes...>> {
        template <typename Controller>
        static void bind(UiContext& ctx, Controller& controller) {
            (bindSingle<ComponentTypes>(ctx, controller), ...);
        }

    private:
        template <typename ComponentType, typename Controller>
        static void bindSingle(UiContext& ctx, Controller& controller) {
            using SignalType = UpdateComponentSignal<ComponentType>;
            ctx.registerSignalHandler<SignalType>([&controller](const SignalType& signal) {
                controller.template onUpdateTypedComponent<ComponentType>(signal);
            });
        }
    };

    /**
     * @brief 编译期声明式行为绑定表：通过可变参数模板折叠表达式零开销挂载全部状态同步与信号行为规则
     */
    template <typename Controller, UiBindingRuleConcept<Controller>... Rules>
    struct UiBehaviorBindingTable {
        using RuleTuple = std::tuple<Rules...>;
        static constexpr size_t kRuleCount = sizeof...(Rules);

        static void apply(UiContext& ctx, Controller& controller) {
            (Rules::bind(ctx, controller), ...);
        }
    };

} // namespace MQEngine

#endif // EDITOR_DECLARATIVE_UI_BINDER_H
