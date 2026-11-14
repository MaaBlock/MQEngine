#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/runtime/interface/Application.h>
#include <algorithm>

namespace MQEngine
{
    namespace Host
    {
        class UIHost::UIEventHandlerImpl : public FCT::EventHandler
        {
        public:
            explicit UIEventHandlerImpl(UIHost* host) : m_host(host) {}

            void onResize(FCT::Window* /*wnd*/, int width, int height) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::WindowResize;
                    ev.mouseX = width;
                    ev.mouseY = height;
                    m_host->pushRawEvent(ev);
                }
            }

            void onMouseMove(FCT::Window* /*wnd*/, int xpos, int ypos) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseMove;
                    ev.mouseX = xpos;
                    ev.mouseY = ypos;
                    m_host->pushRawEvent(ev);
                }
            }

            void onMouseWheel(FCT::Window* /*wnd*/, int delta) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseWheel;
                    ev.delta = delta;
                    m_host->pushRawEvent(ev);
                }
            }

            void onLButtonDown(FCT::Window* /*wnd*/, int xpos, int ypos) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseDown;
                    ev.mouseButton = 0;
                    ev.mouseX = xpos;
                    ev.mouseY = ypos;
                    m_host->pushRawEvent(ev);
                }
            }

            void onLButtonUp(FCT::Window* /*wnd*/, int xpos, int ypos) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseUp;
                    ev.mouseButton = 0;
                    ev.mouseX = xpos;
                    ev.mouseY = ypos;
                    m_host->pushRawEvent(ev);
                }
            }

            void onRButtonDown(FCT::Window* /*wnd*/, int xpos, int ypos) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseDown;
                    ev.mouseButton = 1;
                    ev.mouseX = xpos;
                    ev.mouseY = ypos;
                    m_host->pushRawEvent(ev);
                }
            }

            void onRButtonUp(FCT::Window* /*wnd*/, int xpos, int ypos) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::MouseUp;
                    ev.mouseButton = 1;
                    ev.mouseX = xpos;
                    ev.mouseY = ypos;
                    m_host->pushRawEvent(ev);
                }
            }

            void onKeyDown(FCT::Window* /*wnd*/, int key) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::KeyDown;
                    ev.keyCode = static_cast<uint32_t>(key);
                    m_host->pushRawEvent(ev);
                }
            }

            void onKeyUp(FCT::Window* /*wnd*/, int key) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::KeyUp;
                    ev.keyCode = static_cast<uint32_t>(key);
                    m_host->pushRawEvent(ev);
                }
            }

            void onFileDrop(FCT::Window* /*wnd*/, const std::vector<std::string>& /*files*/) override
            {
                if (m_host) {
                    RawInputEvent ev;
                    ev.type = RawInputEventType::FileDrop;
                    m_host->pushRawEvent(ev);
                }
            }

        private:
            UIHost* m_host = nullptr;
        };

        UIHost::UIHost() = default;

        UIHost::~UIHost()
        {
            term();
        }

        void UIHost::registerInfo()
        {
        }

        Status UIHost::init(FCT::Runtime* fctRuntime, const Config& config)
        {
            if (m_initialized.load(std::memory_order_acquire)) {
                return OkStatus();
            }

            m_fctRuntime = fctRuntime;
            m_headless = config.isHeadless();
            m_fallbackRunning.store(true, std::memory_order_release);

            if (!m_headless && m_fctRuntime) {
                const char* title = "MQEngine";
                auto* engine = Runtime::Engine::getInstance();
                if (engine && engine->application) {
                    const auto& req = engine->application->applicationRegisterInfo.platformRequire;
                    if (req.windowTitle && req.windowTitle[0] != '\0') {
                        title = req.windowTitle;
                    }
                }
#ifdef _WIN32
                m_window = m_fctRuntime->createWindow(1280, 720, title, FCT::WindowBackend::Win32);
#else
                m_window = m_fctRuntime->createWindow(1280, 720, title);
#endif
                assert(m_window != nullptr && "UIHost 创建 FCT::Window 失败");

                m_eventHandler = FCT_NEW(UIEventHandlerImpl, this);
                m_window->registerHandler(m_eventHandler);
            }

            m_initialized.store(true, std::memory_order_release);
            return OkStatus();
        }

        Status UIHost::init(const Config& config)
        {
            return init(nullptr, config);
        }

        Status UIHost::init()
        {
            Config defaultConfig;
            return init(nullptr, defaultConfig);
        }

        void UIHost::term() noexcept
        {
            if (!m_initialized.load(std::memory_order_acquire)) {
                return;
            }

            m_fallbackRunning.store(false, std::memory_order_release);

            if (m_window) {
                if (m_eventHandler) {
                    m_window->unregisterHandler(m_eventHandler);
                    m_eventHandler->release();
                    m_eventHandler = nullptr;
                }
                m_window->destroy();
                m_window->release();
                m_window = nullptr;
            }

            m_eventQueue.reset();
            m_fctRuntime = nullptr;

            m_initialized.store(false, std::memory_order_release);
        }

        bool UIHost::isWindowRunning() const noexcept
        {
            if (m_window) {
                return m_window->isRunning();
            }
            return m_fallbackRunning.load(std::memory_order_acquire);
        }

        void UIHost::postUiTask(std::function<void()> task)
        {
            if (!task) return;
            auto* holder = new std::function<void()>(std::move(task));
            postUiTask([](void* p) {
                auto* fn = static_cast<std::function<void()>*>(p);
                if (fn && *fn) {
                    (*fn)();
                }
                delete fn;
            }, holder, false);
        }

        void UIHost::postUiTask(FCT::UITaskFunction task, void* param, bool waited)
        {
            if (m_fctRuntime) {
                m_fctRuntime->postUiTask(std::move(task), param, waited);
            } else if (task) {
                task(param);
            }
        }

        void UIHost::sendUiTask(FCT::UITaskFunction task, void* param, bool waited)
        {
            postUiTask(std::move(task), param, waited);
        }

        void UIHost::postTicker(std::function<void(float)> ticker)
        {
            if (!ticker) return;
            postTicker([t = std::move(ticker)](float dt) {
                t(dt);
                return true;
            });
        }

        uint32_t UIHost::postTicker(FCT::UiTicker ticker)
        {
#ifdef _WIN32
            if (m_window) {
                auto* win32Wnd = dynamic_cast<FCT::Win32_Window*>(m_window);
                if (win32Wnd) {
                    return win32Wnd->postTicker(std::move(ticker));
                }
            }
#endif
            return 0;
        }

        void UIHost::pushRawEvent(const RawInputEvent& event)
        {
            m_eventQueue.push(event);
        }

        std::vector<RawInputEvent> UIHost::drainEvents()
        {
            std::vector<RawInputEvent> drained;
            RawInputEvent ev;
            while (m_eventQueue.pop(ev)) {
                drained.push_back(ev);
            }
            return drained;
        }

        void UIHost::injectVirtualEvent(const RawInputEvent& event)
        {
            pushRawEvent(event);
        }

        void UIHost::injectKeyEvent(uint32_t keyCode, bool isDown)
        {
            RawInputEvent ev;
            ev.type = isDown ? RawInputEventType::KeyDown : RawInputEventType::KeyUp;
            ev.keyCode = keyCode;
            pushRawEvent(ev);
        }

        void UIHost::injectMouseMove(int32_t x, int32_t y)
        {
            RawInputEvent ev;
            ev.type = RawInputEventType::MouseMove;
            ev.mouseX = x;
            ev.mouseY = y;
            pushRawEvent(ev);
        }

        void UIHost::injectMouseButton(uint32_t button, bool isDown, int32_t x, int32_t y)
        {
            RawInputEvent ev;
            ev.type = isDown ? RawInputEventType::MouseDown : RawInputEventType::MouseUp;
            ev.mouseButton = button;
            ev.mouseX = x;
            ev.mouseY = y;
            pushRawEvent(ev);
        }

        void UIHost::injectMouseWheel(int32_t delta)
        {
            RawInputEvent ev;
            ev.type = RawInputEventType::MouseWheel;
            ev.delta = delta;
            pushRawEvent(ev);
        }

    } // namespace Host
} // namespace MQEngine
