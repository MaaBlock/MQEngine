//
// Created by Administrator on 2025/8/18.
//

#ifndef GLOBAL_H
#define GLOBAL_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine
{
    class EditorCameraManager;
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
    };
    extern EditorGlobal g_editorGlobal;
}
#endif //GLOBAL_H
