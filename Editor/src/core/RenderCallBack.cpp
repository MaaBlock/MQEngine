#include "EditorApplication.h"
using namespace FCT;
namespace MQEngine {

    void EditorApplication::init()
    {
        g_global.wnd = global.wnd;
        g_global.ctx = global.ctx;
        g_global.dataManager = global.dataManager;
        uiManager.init();
        settingRenderCallBack();
    }

    void EditorApplication::settingRenderCallBack()
    {
        uiManager.registerRenderCallback(renderCallBackDispatcher);
    }

    void EditorApplication::logicTick()
    {
        uiManager.logicTick();
        imguiLogicTick();
    }

    void EditorApplication::imguiLogicTick()
    {

    }
}
