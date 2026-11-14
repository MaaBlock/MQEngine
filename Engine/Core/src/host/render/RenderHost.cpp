#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>

namespace MQEngine
{
    namespace Host
    {
        RenderHost::RenderHost() = default;

        RenderHost::~RenderHost()
        {
            term();
        }

        void RenderHost::registerInfo()
        {
            m_renderFrameCount = 0;
        }

        Status RenderHost::init(FCT::Runtime* fctRuntime, FCT::Window* window, const Config& config)
        {
            if (m_initialized.load(std::memory_order_acquire)) {
                return OkStatus();
            }

            m_fctRuntime = fctRuntime;
            m_window = window;
            m_headless = config.isHeadless();
            m_renderFrameCount = 0;

            if (m_fctRuntime) {
                m_ctx = m_fctRuntime->createContext(FCT::ContextBackend::Vulkan);
                assert(m_ctx != nullptr && "RenderHost 创建 FCT::Context 失败");
                m_ctx->create();
                if (!m_headless && m_window) {
                    m_window->bind(m_ctx);
                }
            }

            m_initialized.store(true, std::memory_order_release);
            return OkStatus();
        }

        Status RenderHost::init(const Config& config, FCT::Window* window)
        {
            return init(nullptr, window, config);
        }

        Status RenderHost::init()
        {
            Config defaultConfig;
            return init(nullptr, nullptr, defaultConfig);
        }

        void RenderHost::term() noexcept
        {
            if (!m_initialized.load(std::memory_order_acquire)) {
                return;
            }

            if (m_ctx) {
                m_ctx->waitGpu();
                if (auto* gpuMem = Resource::GpuMemoryManager::InstancePtr()) {
                    gpuMem->flushAllDeferredReleases();
                }
                m_ctx->requestExit();
                m_ctx->release();
                m_ctx = nullptr;
            }

            m_window = nullptr;
            m_fctRuntime = nullptr;

            m_initialized.store(false, std::memory_order_release);
        }

        void RenderHost::sync()
        {
            ZoneScoped();
            if (m_ctx) {
                m_ctx->_consumeLogicTask();
                m_ctx->_waitCurrentFlush();
                if (m_ctx->getFlowControl()) {
                    m_ctx->getFlowControl()->executeSyncTickers();
                }
                m_ctx->_nextFrame();
            }
            if (auto* gpuMem = Resource::GpuMemoryManager::InstancePtr()) {
                gpuMem->processFrameDeletions(m_renderFrameCount);
            }
            ++m_renderFrameCount;
        }

        void RenderHost::flushLastFrame()
        {
            ZoneScoped();
            if (m_ctx) {
                m_ctx->_consumeLogicTask();
                m_ctx->_waitCurrentFlush();
            }
            ++m_renderFrameCount;
        }

        void RenderHost::requestExit() noexcept
        {
            if (m_ctx) {
                m_ctx->requestExit();
            }
        }

    } // namespace Host
} // namespace MQEngine
