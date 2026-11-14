#ifndef MQENGINE_BASE_CORE_ENGINE_MANAGER_H
#define MQENGINE_BASE_CORE_ENGINE_MANAGER_H

#include <cassert>

namespace MQEngine
{
    /**
     * @brief CRTP 零开销值内嵌管理器基类（区别于顶层引擎模块基类 CoreEngineModule）：
     *        1. 维护 static Derived* s_instance，支持 Derived::Instance() 与 CoreEngineManager<Derived>::Instance() 访问；
     *        2. 提供 `operator->()`、`get()` 与 `operator Derived*()`，支持按值内嵌于模块中的 Manager 零开销访问；
     *        3. 统一约束派生 Manager 的 `registerInfo()` 与帧同步汇聚接口 `syncTicker()`。
     */
    template <typename Derived>
    class CoreEngineManager
    {
    protected:
        static Derived* s_instance;

    public:
        CoreEngineManager() noexcept
        {
            if (s_instance == nullptr) {
                s_instance = static_cast<Derived*>(this);
            }
        }

        ~CoreEngineManager() noexcept
        {
            if (s_instance == static_cast<Derived*>(this)) {
                s_instance = nullptr;
            }
        }

        CoreEngineManager(const CoreEngineManager&) = delete;
        CoreEngineManager& operator=(const CoreEngineManager&) = delete;

        static constexpr Derived& Instance() noexcept
        {
            assert(s_instance != nullptr && "CoreEngineManager 派生管理器实例尚未创建或已销毁");
            return *s_instance;
        }

        static constexpr Derived* InstancePtr() noexcept
        {
            return s_instance;
        }

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

    template <typename Derived>
    Derived* CoreEngineManager<Derived>::s_instance = nullptr;
} // namespace MQEngine

#endif // MQENGINE_BASE_CORE_ENGINE_MANAGER_H
