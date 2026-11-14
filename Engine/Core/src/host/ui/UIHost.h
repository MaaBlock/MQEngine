#ifndef MQENGINE_HOST_MANAGER_UIHOST_H
#define MQENGINE_HOST_MANAGER_UIHOST_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/host/core/Config.h>
#include <MQEngine/host/ui/RawInputEvent.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <atomic>
#include <functional>
#include <vector>
#include <boost/lockfree/spsc_queue.hpp>

namespace MQEngine
{
    namespace Host
    {
        /**
         * @brief 界面宿主（UIHost）：
         * 负责对接 FCT::Window 与 FCT UI 线程，管理任务派发（postUiTask / postTicker），
         * 遵循 19:28 最新架构规范：sync() 保持为空函数 {}，UI 线程自管消息循环。
         */
        class ENGINE_API UIHost : public CoreEngineManager<UIHost>
        {
        public:
            UIHost();
            ~UIHost();

            UIHost(const UIHost&) = delete;
            UIHost& operator=(const UIHost&) = delete;

            void registerInfo();
            Status init(FCT::Runtime* fctRuntime, const Config& config);
            Status init(const Config& config);
            Status init();
            void term() noexcept;

            // Runtime::loop() 统一契约接口：UI 线程自管消息循环，sync 必须为空实现
            void sync() noexcept {}
            void syncTicker() noexcept {}

            // 窗口生命周期与状态查询
            FCT::Window* getWindow() const noexcept { return m_window; }
            FCT::Window* getMainWindow() const noexcept { return m_window; }
            bool isWindowRunning() const noexcept;

            // UI 线程任务派发与调度接口
            void postUiTask(std::function<void()> task);
            void postUiTask(FCT::UITaskFunction task, void* param = nullptr, bool waited = false);
            void sendUiTask(FCT::UITaskFunction task, void* param = nullptr, bool waited = true);
            void postTicker(std::function<void(float)> ticker);
            uint32_t postTicker(FCT::UiTicker ticker);

            // 虚拟输入注入接口（用于自动化回归与无头测试）
            void injectVirtualEvent(const RawInputEvent& event);
            void injectKeyEvent(uint32_t keyCode, bool isDown);
            void injectMouseMove(int32_t x, int32_t y);
            void injectMouseButton(uint32_t button, bool isDown, int32_t x = 0, int32_t y = 0);
            void injectMouseWheel(int32_t delta);

            // 供内部与 EventHandler 使用的事件入队接口
            void pushRawEvent(const RawInputEvent& event);

            // 抽取并清空当前已排队的输入事件列表（由注册在 LogicHost 的 Ticker 调用）
            std::vector<RawInputEvent> drainEvents();

        private:
            class UIEventHandlerImpl;

            FCT::Runtime* m_fctRuntime = nullptr;
            FCT::Window* m_window = nullptr;
            UIEventHandlerImpl* m_eventHandler = nullptr;

            bool m_headless = false;
            std::atomic<bool> m_initialized{false};
            std::atomic<bool> m_fallbackRunning{true};

            boost::lockfree::spsc_queue<RawInputEvent, boost::lockfree::capacity<4096>> m_eventQueue;
        };

    } // namespace Host
} // namespace MQEngine

#endif // MQENGINE_HOST_MANAGER_UIHOST_H
