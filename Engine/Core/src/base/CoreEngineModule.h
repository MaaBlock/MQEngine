#ifndef MQENGINE_BASE_INPLACE_MODULE_H
#define MQENGINE_BASE_INPLACE_MODULE_H

#include <cassert>
#include <MQEngine/thirdparty/thirdparty.h>

namespace MQEngine
{
    /**
     * @brief CRTP 零开销值内嵌引擎顶层模块基类：
     *        1. 直接维护 `static Derived* s_instance`（派生类带 `ENGINE_API` 时，MSVC 自动将基类特化 `CoreEngineModule<Derived>` 隐式标记为 dllexport/dllimport，汇编级退化为单条 `mov` 指令直接读取指针，无任何分支或局部静态锁开销）；
     *        2. 提供 `operator->()` 与 `operator Derived*()`，使按值内嵌在 Engine 中的顶层模块支持 `engine->resource->xxx` 语法；
     *        3. 统一约束顶层模块实现 `registerInfo()`、`init()` 与 `term()` 标准生命周期。
     */
    template <typename Derived>
    class CoreEngineModule
    {
    protected:
        static Derived* s_instance;

    public:
        CoreEngineModule() noexcept
        {
            assert(s_instance == nullptr && "CoreEngineModule 派生模块实例重复创建");
            s_instance = static_cast<Derived*>(this);
        }

        ~CoreEngineModule() noexcept
        {
            if (s_instance == static_cast<Derived*>(this)) {
                s_instance = nullptr;
            }
        }

        CoreEngineModule(const CoreEngineModule&) = delete;
        CoreEngineModule& operator=(const CoreEngineModule&) = delete;

        static constexpr Derived& Instance() noexcept
        {
            assert(s_instance != nullptr && "CoreEngineModule 派生模块实例尚未创建或已销毁");
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

    template <typename Derived>
    Derived* CoreEngineModule<Derived>::s_instance = nullptr;
} // namespace MQEngine

#endif // MQENGINE_BASE_INPLACE_MODULE_H
