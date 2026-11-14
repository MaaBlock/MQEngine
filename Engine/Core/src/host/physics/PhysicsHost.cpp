#include <MQEngine/host/physics/PhysicsHost.h>

namespace MQEngine
{
    namespace Host
    {
        void PhysicsHost::registerInfo()
        {
        }

        Status PhysicsHost::init()
        {
            return OkStatus();
        }

        void PhysicsHost::term()
        {
            m_tickers.clear();
        }

        void PhysicsHost::sync()
        {
            ZoneScoped();
            (void)m_tickers.update();
            for (const auto& ticker : m_tickers.order()) {
                if (ticker) {
                    ticker();
                }
            }
        }

    } // namespace Host
} // namespace MQEngine
