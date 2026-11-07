#include "Platform.h"
#include "../../runtime/core/Engine.h"
#include "../../runtime/core/EngineGlobal.h"
#include "../../runtime/interface/Application.h"
#include <cassert>

namespace MQEngine
{
    namespace Platform
    {
        Platform::Platform() = default;

        Platform::~Platform()
        {
            term();
        }

        void Platform::registerInfo()
        {
            g_engineGlobal.platform = this;
            g_engineGlobal.rt = &m_fctRuntime;
        }

        Status Platform::init()
        {
            if (m_initialized) {
                return OkStatus();
            }

            auto* engine = MQEngine::Runtime::Engine::getInstance();
            assert(engine != nullptr && engine->application != nullptr && "Engine 与 Application 不能为空");

            const auto& platformRequire = engine->application->applicationRegisterInfo.platformRequire;
            const char* title = (platformRequire.windowTitle && platformRequire.windowTitle[0] != '\0')
                ? platformRequire.windowTitle
                : "MQEngine";
            const bool headless = engine->headLess;
            constexpr int width = 800;
            constexpr int height = 600;

            g_engineGlobal.platform = this;
            g_engineGlobal.rt = &m_fctRuntime;

            if (headless) {
                FCT::SetHeadlessDialogMode(true);
#ifdef _WIN32
                SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
            }

            m_wnd = m_fctRuntime.createWindow(
                width,
                height,
                title,
                FCT::WindowBackend::Win32);
            assert(m_wnd && "Platform 创建 FCT::Window 失败");

            if (headless) {
                m_wnd->setVisible(false);
            }

            m_ctx = m_fctRuntime.createContext(FCT::ContextBackend::Vulkan);
            assert(m_ctx && "Platform 创建 FCT::Context 失败");
            m_ctx->create();

            m_wnd->bind(m_ctx);
            m_autoViewport = m_wnd->getModule<FCT::WindowModule::AutoViewport>();

            g_engineGlobal.ctx = m_ctx;
            m_initialized = true;
            return OkStatus();
        }

        void Platform::term()
        {
            if (!m_initialized) {
                return;
            }
            m_initialized = false;
            m_autoViewport = nullptr;

            if (m_ctx) {
                m_ctx->requestExit();
            }
            if (m_wnd) {
                m_wnd->release();
                m_wnd = nullptr;
            }
            if (m_ctx) {
                m_ctx->release();
                m_ctx = nullptr;
            }
            if (g_engineGlobal.ctx) {
                g_engineGlobal.ctx = nullptr;
            }
            if (g_engineGlobal.platform == this) {
                g_engineGlobal.platform = nullptr;
            }
        }

        bool Platform::isWindowRunning() const noexcept
        {
            return m_wnd && m_wnd->isRunning();
        }

        void Platform::flush()
        {
            assert(m_ctx != nullptr && "RHI Context 不能为空");
            m_ctx->flush();
        }

    } // namespace Platform
} // namespace MQEngine
