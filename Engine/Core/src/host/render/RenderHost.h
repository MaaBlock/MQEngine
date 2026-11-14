#ifndef MQENGINE_HOST_MANAGER_RENDERHOST_H
#define MQENGINE_HOST_MANAGER_RENDERHOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/core/Config.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/host/classdef.h>
#include <atomic>
#include <cstdint>

namespace MQEngine
{
    namespace Host
    {
        class Host;

        /**
         * @brief 渲染宿主（RenderHost）：
         * 负责渲染提交线程（m_submitThread）调度与 FCT RHI 对接，
         * 通过 _consumeLogicTask, _waitCurrentFlush, _nextFrame 实现逻辑-渲染双缓冲乒乓同步。
         */
        class ENGINE_API RenderHost : public CoreEngineManager<RenderHost>
        {
        public:
            RenderHost();
            ~RenderHost();

            RenderHost(const RenderHost&) = delete;
            RenderHost& operator=(const RenderHost&) = delete;

            void registerInfo();
            Status init(FCT::Runtime* fctRuntime, FCT::Window* window, const Config& config);
            Status init(const Config& config, FCT::Window* window);
            Status init();
            void term() noexcept;

            void sync();
            void flushLastFrame();
            void requestExit() noexcept;

            FCT::Context* getContext() const noexcept { return m_ctx; }
            uint64_t getRenderFrameCount() const noexcept { return m_renderFrameCount; }
            bool isHeadless() const noexcept { return m_headless; }

        private:
            FCT::Runtime* m_fctRuntime = nullptr;
            FCT::Window* m_window = nullptr;
            FCT::Context* m_ctx = nullptr;

            bool m_headless = false;
            std::atomic<bool> m_initialized{false};
            uint64_t m_renderFrameCount = 0;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_RENDERHOST_H
