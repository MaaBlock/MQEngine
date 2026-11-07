#include "UiManager.h"

namespace MQEngine {

    void UiManager::init() {
        ZoneScoped();
        m_uiLayer.init();
        m_behaviorController.bindToLayer(m_uiLayer);
    }

    void UiManager::term() {
        m_uiLayer.term();
    }

    void UiManager::registerRenderCallback(
        FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& callback) {
        m_uiLayer.registerRenderCallback(callback);
    }

    void UiManager::logicTick() {
        ZoneScoped();
        m_uiLayer.logicTick();
    }

} // namespace MQEngine
