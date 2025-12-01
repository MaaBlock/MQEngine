//
// Created by Administrator on 2025/8/18.
//

#ifndef GLOBAL_H
#define GLOBAL_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    class EditorCameraManager;
    class Inspector;
    class ContentBrowser;
}

namespace MQEngine{
    struct SelectedEntityState
    {
        Scene* scene = nullptr;
        std::string trunkName;
        entt::entity entity = entt::null;
        bool isGlobal = false;
    };
    
    struct EditorGlobal
    {
        FCT::Window* wnd;
        FCT::Context* ctx;
        FCT::ImguiModule* imguiModule;
        DataManager* dataManager;
        EditorCameraManager* cameraManager;
        FCT::Runtime* rt;
        entt::registry* editorRegistry;
        FCT::ImguiContext* imguiContext;
        SelectedEntityState selectedEntity;
        entt::id_type componentToDelete = 0;
        Inspector* inspector = nullptr;
        ContentBrowser* contentBrowser = nullptr;
    };
    extern EditorGlobal g_editorGlobal;
}
#endif //GLOBAL_H
