#ifndef UIMANAGER_H
#define UIMANAGER_H

#include "../UI/UiLayer.h"
#include "../UIBehavior/EditorUiBehaviorController.h"

namespace MQEngine {

    /**
     * @brief 编辑器 UI 门面协调器，组合顶层 UiLayer 与声明式行为控制器 EditorUiBehaviorController
     */
    class UiManager {
    public:
        void init();
        void term();
        void registerRenderCallback(FCT::EventDispatcher<FCT::EventSystemConfig::TriggerOnly>& renderCallBack);
        void logicTick();

        UiLayer& getUiLayer() { return m_uiLayer; }
        EditorUiBehaviorController& getBehaviorController() { return m_behaviorController; }

    private:
        UiLayer m_uiLayer;
        EditorUiBehaviorController m_behaviorController;
    };

} // namespace MQEngine

#endif // UIMANAGER_H
