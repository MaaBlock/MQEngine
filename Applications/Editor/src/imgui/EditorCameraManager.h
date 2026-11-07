#ifndef EDITORCAMERAMANAGER_H
#define EDITORCAMERAMANAGER_H
#include "../thirdparty/thirdparty.h"

namespace MQEngine {
    /**
     * @brief 编辑器视口相机管理器，负责编辑器独立相机的创建、场景对齐、焦点追踪与交互式飞行/平移/环绕控制
     */
    class EditorCameraManager {
    public:
        EditorCameraManager();
        void createEditorCamera();
        entt::entity getEditorCameraEntity() const { return m_editorCameraEntity; }
        entt::registry* getEditorRegistry() const { return m_editorRegistry; }
        void hookCamera();

        void syncFromSceneCamera();
        void syncToSceneCamera(entt::registry* targetReg = nullptr, entt::entity targetEntity = entt::null);
        void resetCamera();
        void focusOnPosition(const FCT::Vec3<>& targetPos, float distance = 15.0f);
        void updateViewportInput(bool isViewportHovered, bool isViewportFocused, float deltaTime);

        float getMoveSpeed() const { return m_moveSpeed; }
        void setMoveSpeed(float speed) { m_moveSpeed = std::clamp(speed, 0.5f, 500000.0f); }
        float getMouseSensitivity() const { return m_mouseSensitivity; }
        void setMouseSensitivity(float sens) { m_mouseSensitivity = std::clamp(sens, 0.01f, 2.0f); }
        float getFastMultiplier() const { return m_fastMultiplier; }
        void setFastMultiplier(float mult) { m_fastMultiplier = std::clamp(mult, 1.1f, 20.0f); }
        const FCT::Vec3<>& getOrbitPivot() const { return m_orbitPivot; }

        bool resolveControlledCamera(entt::registry*& outReg, entt::entity& outEntity) const;
        static FCT::Vec3<> queryEntityWorldPosition(entt::registry* reg, entt::entity entity);

    private:
        static void computeBasisFromEuler(const FCT::Vec3<>& eulerDeg, FCT::Vec3<>& outForward, FCT::Vec3<>& outRight, FCT::Vec3<>& outUp);
        void handleFlyLookAndMove(entt::registry& reg, entt::entity camEntity, float deltaTime);
        void handleOrbitCamera(entt::registry& reg, entt::entity camEntity);
        void handleMiddleButtonPan(entt::registry& reg, entt::entity camEntity);
        void handleWheelZoom(entt::registry& reg, entt::entity camEntity, float wheelDelta);

    private:
        entt::registry* m_editorRegistry = nullptr;
        entt::entity m_editorCameraEntity = entt::null;
        float m_moveSpeed = 25.0f;
        float m_fastMultiplier = 4.0f;
        float m_mouseSensitivity = 0.18f;
        FCT::Vec3<> m_orbitPivot{ 0.0f, 0.0f, 0.0f };
        float m_orbitDistance = 25.0f;
        bool m_isNavigating = false;
    };

}

#endif //EDITORCAMERAMANAGER_H
