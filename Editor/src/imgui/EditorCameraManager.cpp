//
// Created by Administrator on 2025/8/24.
//

#include "EditorCameraManager.h"

#include "../core/Global.h"
using namespace FCT;
namespace MQEngine {

    EditorCameraManager::EditorCameraManager()
    {
        m_editorRegistry = g_editorGlobal.editorRegistry;
        createEditorCamera();
    }

    void EditorCameraManager::createEditorCamera()
    {
        m_editorCameraEntity = m_editorRegistry->create();

        auto& position = m_editorRegistry->emplace<PositionComponent>(m_editorCameraEntity);
        position.position = Vec3(40.0f, 40.0f, 40.0f);

        auto& rotation = m_editorRegistry->emplace<RotationComponent>(m_editorCameraEntity);

        Vec3 cameraPos = position.position;
        Vec3 target = Vec3(0.0f, 0.0f, 0.0f);
        Vec3 direction = target - cameraPos;
        direction = normalize(direction);

        float yaw = atan2(direction.z, direction.x) * 180.0f / 3.14159265359f;
        float pitch = asin(direction.y) * 180.0f / 3.14159265359f;

        rotation.rotation = Vec3(pitch, yaw, 0.0f);

        auto& camera = m_editorRegistry->emplace<CameraComponent>(m_editorCameraEntity);
        camera.active = true;
        camera.fov = 90;
        camera.nearPlane = 0.1f;
        camera.farPlane = 250.0f;
    }
}