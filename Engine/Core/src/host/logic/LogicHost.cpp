#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/host/ui/UIHost.h>
#include <MQEngine/host/core/Host.h>
#include <algorithm>

namespace MQEngine
{
    namespace Host
    {
        void LogicHost::registerInfo()
        {
            m_loopRunning = true;
            m_requestExit = false;
            m_frameCount = 0;
        }

        Status LogicHost::init()
        {
            m_loopRunning = true;
            m_requestExit = false;
            m_frameCount = 0;
            return OkStatus();
        }

        void LogicHost::term()
        {
            m_loopRunning = false;
            m_tickers.clear();
        }

        void LogicHost::sync()
        {
            ZoneScoped();
            if (auto* uiHost = UIHost::InstancePtr()) {
                if (uiHost->getWindow() != nullptr && !uiHost->isWindowRunning()) {
                    m_requestExit = true;
                }
            }

            (void)m_tickers.update();
            for (const auto& ticker : m_tickers.order()) {
                if (ticker) {
                    ticker();
                }
            }
            ++m_frameCount;

            if (auto* host = Host::InstancePtr()) {
                const int32_t maxFrames = host->config.getMaxFrames();
                if (host->config.hasMaxFrames() && m_frameCount >= static_cast<uint64_t>(std::max(0, maxFrames))) {
                    m_requestExit = true;
                }
            }

            if (m_requestExit) {
                m_loopRunning = false;
            }
        }

    } // namespace Host
} // namespace MQEngine
