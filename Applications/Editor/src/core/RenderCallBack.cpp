#include "EditorApplication.h"
#include "../command/UndoManager.h"
using namespace FCT;
namespace MQEngine {

    void EditorApplication::init()
    {
        ZoneScoped();
        g_editorGlobal.engine = m_engine;
        g_editorGlobal.wnd = m_engine->platform->getWindow();
        g_editorGlobal.ctx = m_engine->platform->getContext();
        g_editorGlobal.dataManager = m_engine->scene->getDataManager();
        g_editorGlobal.rt = &m_engine->platform->getFctRuntime();
        m_registry = m_engine->scene->getRegistriesManager()->createRegistry();
        g_editorGlobal.editorRegistry = m_registry;
        g_editorGlobal.dataManager->appendRegistry(m_registry);
        m_undoManager = makeUnique<UndoManager>();
        g_editorGlobal.undoManager = m_undoManager.get();
        uiManager.init();
        settingRenderCallBack();
    }

    void EditorApplication::settingRenderCallBack()
    {
        uiManager.registerRenderCallback(renderCallBackDispatcher);
    }

    void EditorApplication::logicTick()
    {
        ZoneScoped();
        uiManager.logicTick();
        imguiLogicTick();
    }

    void EditorApplication::imguiLogicTick()
    {

    }
}
