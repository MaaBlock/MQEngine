#ifndef MQENGINE_PLATFORM_CORE_PLATFORM_H
#define MQENGINE_PLATFORM_CORE_PLATFORM_H

#include <cstdint>
#include <string>
#include "../../EnginePCH.h"
#include "../../base/CoreEngineModule.h"
#include "../../thirdparty/thirdparty.h"
#include "../classdef.h"
#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    namespace Platform
    {
        /**
         * @brief 平台与底层 RHI 宿主模块，统一托管 FCT::Runtime、FCT::Window 与 FCT::Context 生命周期
         */
        class ENGINE_API Platform : public CoreEngineModule<Platform>
        {
        public:
            Platform();
            ~Platform();

            Platform(const Platform&) = delete;
            Platform& operator=(const Platform&) = delete;

            /**
             * @brief 注册阶段（registerModules）调用，注册平台元信息与全局句柄
             */
            void registerInfo();

            /**
             * @brief 初始化平台窗口与 RHI Context（直接从 Engine::getInstance()->application 获取 PlatformRequire 与 Engine::headLess）
             */
            Status init();

            /**
             * @brief 释放 RHI Context 与平台窗口资源
             */
            void term();

            /**
             * @brief 查询主窗口是否仍在运行
             */
            bool isWindowRunning() const noexcept;

            /**
             * @brief 触发 RHI Context 帧同步与提交唤醒
             */
            void flush();

            FCT::Runtime& getFctRuntime() noexcept { return m_fctRuntime; }
            const FCT::Runtime& getFctRuntime() const noexcept { return m_fctRuntime; }

            Window* getWindow() const noexcept { return m_wnd; }
            Context* getContext() const noexcept { return m_ctx; }
            FCT::AutoViewport* getAutoViewport() const noexcept { return m_autoViewport; }

        private:
            Runtime m_fctRuntime;
            Window* m_wnd = nullptr;
            Context* m_ctx = nullptr;
            FCT::AutoViewport* m_autoViewport = nullptr;
            bool m_initialized = false;
        };

    } // namespace Platform
} // namespace MQEngine

#endif // MQENGINE_PLATFORM_CORE_PLATFORM_H
