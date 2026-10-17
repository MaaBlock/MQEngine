#include "EditorApplication.h"
using namespace FCT;
namespace MQEngine {

    void EditorApplication::init()
    {
        g_editorGlobal.wnd = global.wnd;
        g_editorGlobal.ctx = global.ctx;
        g_editorGlobal.dataManager = global.dataManager;
        g_editorGlobal.rt = global.runtime;
        m_registry = g_engineGlobal.registriesManager->createRegistry();
        g_editorGlobal.editorRegistry = m_registry;
        g_editorGlobal.dataManager->appendRegistry(m_registry);
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
