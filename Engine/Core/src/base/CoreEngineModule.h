#ifndef MQENGINE_BASE_INPLACE_MODULE_H
#define MQENGINE_BASE_INPLACE_MODULE_H

#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    /**
     * @brief CRTP 零开销值内嵌引擎顶层模块基类（专用于 Engine 持有的 12 个顶层模块）：
     *        1. 提供 `operator->()` 与 `operator Derived*()`，使按值内嵌在 Engine 中的顶层模块支持 `engine->renderer->xxx` 语法，
     *           编译期直接退化为常量结构体偏移加法（0 次指针内存读取，开销与 `engine->xxx` 完全等价）；
     *        2. 统一约束 12 个顶层模块实现 `registerInfo()`、`init()` 与 `term()` 标准生命周期。
     */
    template <typename Derived>
    class CoreEngineModule
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
            static_cast<Derived*>(this)->registerInfo();
        }

        Status init()
        {
            return static_cast<Derived*>(this)->init();
        }

        void term()
        {
            static_cast<Derived*>(this)->term();
        }
    };
} // namespace MQEngine

#endif // MQENGINE_BASE_INPLACE_MODULE_H
