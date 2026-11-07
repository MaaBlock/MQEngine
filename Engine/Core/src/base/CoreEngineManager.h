#ifndef MQENGINE_BASE_CORE_ENGINE_MANAGER_H
#define MQENGINE_BASE_CORE_ENGINE_MANAGER_H

namespace MQEngine
{
    /**
     * @brief CRTP 零开销值内嵌管理器基类（区别于 12 个顶层引擎模块基类 CoreEngineModule）：
     *        1. 提供 `operator->()`、`get()` 与 `operator Derived*()`，支持按值内嵌于模块中的 Manager 零开销访问；
     *        2. 统一约束派生 Manager 的 `registerInfo()` 与帧同步汇聚接口 `syncTicker()`。
     */
    template <typename Derived>
    class CoreEngineManager
    {
    public:
        constexpr Derived* get() noexcept
        {
            return static_cast<Derived*>(this);
        }

        constexpr const Derived* get() const noexcept
        {
            return static_cast<const Derived*>(this);
        }

        constexpr Derived* operator->() noexcept
        {
            return static_cast<Derived*>(this);
        }

        constexpr const Derived* operator->() const noexcept
        {
            return static_cast<const Derived*>(this);
        }

        constexpr operator Derived*() noexcept
        {
            return static_cast<Derived*>(this);
        }

        constexpr operator const Derived*() const noexcept
        {
            return static_cast<const Derived*>(this);
        }

        void registerInfo()
        {
        }

        void syncTicker()
        {
        }
    };
} // namespace MQEngine

#endif // MQENGINE_BASE_CORE_ENGINE_MANAGER_H
