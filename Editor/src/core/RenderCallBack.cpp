#include "EditorApplication.h"
using namespace FCT;
namespace MQEngine {

    void EditorApplication::init()
    {
        g_global.wnd = global.wnd;
        g_global.ctx = global.ctx;
        g_global.dataManager = global.dataManager;
        g_global.rt = global.runtime;
        g_global.editorRegistry = &registry;
        g_global.dataManager->appendRegistry(&registry);
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
