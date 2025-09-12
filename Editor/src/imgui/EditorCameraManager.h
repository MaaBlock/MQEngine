//
// Created by Administrator on 2025/8/24.
//

#ifndef EDITORCAMERAMANAGER_H
#define EDITORCAMERAMANAGER_H
#include "../Thirdparty/thirdparty.h"
namespace MQEngine {
    class EditorCameraManager {
    public:
        EditorCameraManager();
        void createEditorCamera();
        entt::entity getEditorCameraEntity() const { return m_editorCameraEntity; }
        entt::registry* getEditorRegistry() const { return m_editorRegistry; }
        void hookCamera();
    private:
        entt::registry* m_editorRegistry;
        entt::entity m_editorCameraEntity;
    };

}


#endif //EDITORCAMERAMANAGER_H
