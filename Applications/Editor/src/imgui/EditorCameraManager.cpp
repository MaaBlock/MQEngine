#include "EditorCameraManager.h"
#include "../core/Global.h"

using namespace FCT;

namespace MQEngine {

    namespace {
        constexpr float kDegToRad = 3.14159265359f / 180.0f;
        constexpr float kRadToDeg = 180.0f / 3.14159265359f;

        entt::entity resolveValidParent(entt::registry* reg, entt::entity entity) {
            if (!reg || entity == entt::null || !reg->valid(entity)) {
                return entt::null;
            }
            const auto* h = reg->try_get<HierarchyComponent>(entity);
            if (!h) {
                return entt::null;
            }
            entt::entity parent = static_cast<entt::entity>(h->parentEntity);
            if (parent != entt::null && parent != entity && reg->valid(parent)) {
                return parent;
            }
            return entt::null;
        }

        Vec3<> worldToEntityLocalPosition(entt::registry* reg, entt::entity entity, const Vec3<>& worldPos) {
            entt::entity parent = resolveValidParent(reg, entity);
            if (parent == entt::null) {
                return worldPos;
            }
            if (auto* engine = Runtime::Engine::getInstance(); engine && engine->renderer->getMatrixCacheSystem()) {
                Mat4 parentWorld = engine->renderer->getMatrixCacheSystem()->computeEntityWorldMatrix(reg, parent);
                Vec4 local4 = parentWorld.inverse() * Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
                return Vec3<>(local4.x, local4.y, local4.z);
            }
            return worldPos - EditorCameraManager::queryEntityWorldPosition(reg, parent);
        }

        Vec3<> directionToEulerDeg(const Vec3<>& dir) {
            Vec3<> normDir = dir.length() > 1e-5f ? normalize(dir) : Vec3<>(0.0f, 0.0f, -1.0f);
            float yaw = std::atan2(normDir.z, normDir.x) * kRadToDeg;
            float pitch = std::asin(std::clamp(normDir.y, -1.0f, 1.0f)) * kRadToDeg;
            return Vec3<>(pitch, yaw, 0.0f);
        }

        Vec3<> worldForwardToEntityLocalEuler(entt::registry* reg, entt::entity entity, const Vec3<>& worldFwd) {
            entt::entity parent = resolveValidParent(reg, entity);
            if (auto* engine = Runtime::Engine::getInstance(); parent != entt::null && engine && engine->renderer->getMatrixCacheSystem()) {
                Mat4 parentRot = engine->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(reg, parent);
                Vec4 localFwd4 = parentRot.inverse() * Vec4(worldFwd.x, worldFwd.y, worldFwd.z, 0.0f);
                return directionToEulerDeg(Vec3<>(localFwd4.x, localFwd4.y, localFwd4.z));
            }
            return directionToEulerDeg(worldFwd);
        }
    }

    EditorCameraManager::EditorCameraManager() {
        m_editorRegistry = g_editorGlobal.editorRegistry;
        createEditorCamera();
        g_engineGlobal.sceneEventPipe.subscribe<SceneEvent::EntryScene>(
            [this](const SceneEvent::EntryScene&) {
                syncFromSceneCamera();
                hookCamera();
            });
    }

    void EditorCameraManager::createEditorCamera() {
        m_editorCameraEntity = m_editorRegistry->create();

        auto& position = m_editorRegistry->emplace<PositionComponent>(m_editorCameraEntity);
        position.position = Vec3<>(0.0f, 35.0f, 75.0f);

        auto& rotation = m_editorRegistry->emplace<RotationComponent>(m_editorCameraEntity);
        Vec3<> cameraPos = position.position;
        Vec3<> target = Vec3<>(0.0f, 0.0f, 0.0f);
        Vec3<> direction = normalize(target - cameraPos);

        float yaw = std::atan2(direction.z, direction.x) * kRadToDeg;
        float pitch = std::asin(std::clamp(direction.y, -1.0f, 1.0f)) * kRadToDeg;
        rotation.rotation = Vec3<>(pitch, yaw, 0.0f);

        auto& camera = m_editorRegistry->emplace<CameraComponent>(m_editorCameraEntity);
        camera.active = true;
        camera.fov = 60.0f;
        camera.nearPlane = 0.5f;
        camera.farPlane = 20000000.0f;
        camera.reverseZ = true;

        m_orbitPivot = target;
        m_orbitDistance = (cameraPos - target).length();
    }

    void EditorCameraManager::hookCamera() {
        auto* engine = Runtime::Engine::getInstance();
        if (engine && engine->renderer->getCameraSystem() && m_editorRegistry) {
            auto editorView = m_editorRegistry->view<CameraComponent>();
            for (auto editorEntity : editorView) {
                engine->renderer->getCameraSystem()->setActiveCamera(m_editorRegistry, editorEntity);
                break;
            }
        }
    }

    Vec3<> EditorCameraManager::queryEntityWorldPosition(entt::registry* reg, entt::entity entity) {
        if (!reg || entity == entt::null || !reg->valid(entity)) {
            return Vec3<>(0.0f, 0.0f, 0.0f);
        }
        if (auto* engine = Runtime::Engine::getInstance(); engine && engine->renderer->getMatrixCacheSystem()) {
            return engine->renderer->getMatrixCacheSystem()->computeEntityWorldPosition(reg, entity);
        }
        Vec3<> worldPos(0.0f, 0.0f, 0.0f);
        entt::entity cur = entity;
        int depth = 0;
        while (cur != entt::null && reg->valid(cur) && depth < 32) {
            if (const auto* pos = reg->try_get<PositionComponent>(cur)) {
                worldPos = worldPos + pos->position;
            }
            if (const auto* h = reg->try_get<HierarchyComponent>(cur)) {
                entt::entity next = static_cast<entt::entity>(h->parentEntity);
                if (next == cur) {
                    break;
                }
                cur = next;
            } else {
                break;
            }
            ++depth;
        }
        return worldPos;
    }

    void EditorCameraManager::computeBasisFromEuler(
        const Vec3<>& eulerDeg,
        Vec3<>& outForward,
        Vec3<>& outRight,
        Vec3<>& outUp) {
        float pitchRad = eulerDeg.x * kDegToRad;
        float yawRad = eulerDeg.y * kDegToRad;

        Vec3<> fwd(
            std::cos(yawRad) * std::cos(pitchRad),
            std::sin(pitchRad),
            std::sin(yawRad) * std::cos(pitchRad));
        if (fwd.length() > 1e-5f) {
            outForward = normalize(fwd);
        } else {
            outForward = Vec3<>(0.0f, 0.0f, -1.0f);
        }

        Vec3<> worldUp(0.0f, 1.0f, 0.0f);
        Vec3<> right = cross(outForward, worldUp);
        if (right.length() > 1e-5f) {
            outRight = normalize(right);
        } else {
            outRight = Vec3<>(1.0f, 0.0f, 0.0f);
        }
        outUp = normalize(cross(outRight, outForward));
    }

    bool EditorCameraManager::resolveControlledCamera(
        entt::registry*& outReg,
        entt::entity& outEntity) const {
        if (m_editorRegistry && m_editorCameraEntity != entt::null &&
            m_editorRegistry->valid(m_editorCameraEntity) &&
            m_editorRegistry->all_of<CameraComponent>(m_editorCameraEntity)) {
            const auto& edCam = m_editorRegistry->get<CameraComponent>(m_editorCameraEntity);
            if (edCam.active) {
                outReg = m_editorRegistry;
                outEntity = m_editorCameraEntity;
                return true;
            }
        }

        if (g_editorGlobal.dataManager) {
            for (auto* reg : g_editorGlobal.dataManager->currentRegistries()) {
                if (!reg) {
                    continue;
                }
                auto view = reg->view<CameraComponent>();
                for (auto e : view) {
                    if (view.get<CameraComponent>(e).active) {
                        outReg = reg;
                        outEntity = e;
                        return true;
                    }
                }
            }
        }

        if (m_editorRegistry && m_editorCameraEntity != entt::null &&
            m_editorRegistry->valid(m_editorCameraEntity)) {
            outReg = m_editorRegistry;
            outEntity = m_editorCameraEntity;
            return true;
        }
        return false;
    }

    void EditorCameraManager::syncFromSceneCamera() {
        if (!m_editorRegistry || m_editorCameraEntity == entt::null ||
            !m_editorRegistry->valid(m_editorCameraEntity) || !g_editorGlobal.dataManager) {
            return;
        }

        auto registries = g_editorGlobal.dataManager->currentRegistries();
        if (Scene::Scene* currentScene = g_editorGlobal.dataManager->getCurrentScene()) {
            if (entt::registry* mainReg = currentScene->getRegistry()) {
                if (std::find(registries.begin(), registries.end(), mainReg) == registries.end()) {
                    registries.push_back(mainReg);
                }
            }
            for (const auto& trunkName : currentScene->getTrunkList()) {
                if (currentScene->isLoad(trunkName)) {
                    if (SceneTrunk* trunk = currentScene->getLoadedTrunk(trunkName)) {
                        if (entt::registry* trunkReg = trunk->getRegistry()) {
                            if (std::find(registries.begin(), registries.end(), trunkReg) == registries.end()) {
                                registries.push_back(trunkReg);
                            }
                        }
                    }
                }
            }
        }
        for (auto* reg : registries) {
            if (!reg || reg == m_editorRegistry) {
                continue;
            }
            auto view = reg->view<CameraComponent>();
            for (auto e : view) {
                Vec3<> worldPos = queryEntityWorldPosition(reg, e);
                Vec3<> localRotDeg(0.0f, -90.0f, 0.0f);
                if (const auto* rotComp = reg->try_get<RotationComponent>(e)) {
                    localRotDeg = rotComp->rotation;
                }
                Vec3<> fwd, right, up;
                computeBasisFromEuler(localRotDeg, fwd, right, up);
                entt::entity parent = resolveValidParent(reg, e);
                if (auto* engine = Runtime::Engine::getInstance(); parent != entt::null && engine && engine->renderer->getMatrixCacheSystem()) {
                    Mat4 parentRot = engine->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(reg, parent);
                    Vec4 worldFwd4 = parentRot * Vec4(fwd.x, fwd.y, fwd.z, 0.0f);
                    Vec3<> worldFwd(worldFwd4.x, worldFwd4.y, worldFwd4.z);
                    if (worldFwd.length() > 1e-5f) {
                        localRotDeg = directionToEulerDeg(worldFwd);
                        computeBasisFromEuler(localRotDeg, fwd, right, up);
                    }
                }
                const auto& sceneCam = view.get<CameraComponent>(e);

                m_editorRegistry->get_or_emplace<PositionComponent>(m_editorCameraEntity).position = worldPos;
                m_editorRegistry->get_or_emplace<RotationComponent>(m_editorCameraEntity).rotation = localRotDeg;

                auto& edCam = m_editorRegistry->get_or_emplace<CameraComponent>(m_editorCameraEntity);
                edCam.fov = sceneCam.fov;
                edCam.nearPlane = sceneCam.nearPlane;
                edCam.farPlane = std::max(sceneCam.farPlane, 20000000.0f);
                edCam.reverseZ = sceneCam.reverseZ;

                m_orbitDistance = std::clamp(worldPos.length() > 1.0f ? worldPos.length() * 0.5f : 25.0f, 5.0f, 50000.0f);
                m_orbitPivot = worldPos + fwd * m_orbitDistance;
                m_moveSpeed = std::clamp(std::max(15.0f, std::abs(worldPos.y) * 0.6f), 10.0f, 50000.0f);
                return;
            }
        }
    }

    void EditorCameraManager::syncToSceneCamera(entt::registry* targetReg, entt::entity targetEntity) {
        if (!m_editorRegistry || m_editorCameraEntity == entt::null ||
            !m_editorRegistry->valid(m_editorCameraEntity)) {
            return;
        }

        if (!targetReg || targetEntity == entt::null || !targetReg->valid(targetEntity)) {
            if (!g_editorGlobal.dataManager) {
                return;
            }
            for (auto* reg : g_editorGlobal.dataManager->currentRegistries()) {
                if (!reg || reg == m_editorRegistry) {
                    continue;
                }
                auto view = reg->view<CameraComponent>();
                for (auto e : view) {
                    targetReg = reg;
                    targetEntity = e;
                    break;
                }
                if (targetEntity != entt::null) {
                    break;
                }
            }
        }

        if (!targetReg || targetEntity == entt::null || !targetReg->valid(targetEntity)) {
            return;
        }

        const auto& edPos = m_editorRegistry->get<PositionComponent>(m_editorCameraEntity).position;
        const auto& edRot = m_editorRegistry->get<RotationComponent>(m_editorCameraEntity).rotation;
        const auto& edCam = m_editorRegistry->get<CameraComponent>(m_editorCameraEntity);

        Vec3<> edFwd, edRight, edUp;
        computeBasisFromEuler(edRot, edFwd, edRight, edUp);
        Vec3<> localPos = worldToEntityLocalPosition(targetReg, targetEntity, edPos);
        Vec3<> localRot = resolveValidParent(targetReg, targetEntity) != entt::null
            ? worldForwardToEntityLocalEuler(targetReg, targetEntity, edFwd)
            : edRot;

        targetReg->get_or_emplace<PositionComponent>(targetEntity).position = localPos;
        targetReg->get_or_emplace<RotationComponent>(targetEntity).rotation = localRot;
        auto& dstCam = targetReg->get_or_emplace<CameraComponent>(targetEntity);
        dstCam.fov = edCam.fov;
        dstCam.nearPlane = edCam.nearPlane;
        dstCam.farPlane = edCam.farPlane;
        dstCam.reverseZ = edCam.reverseZ;
    }

    void EditorCameraManager::resetCamera() {
        if (!m_editorRegistry || m_editorCameraEntity == entt::null ||
            !m_editorRegistry->valid(m_editorCameraEntity)) {
            return;
        }

        m_editorRegistry->get_or_emplace<PositionComponent>(m_editorCameraEntity).position = Vec3<>(0.0f, 35.0f, 75.0f);
        m_editorRegistry->get_or_emplace<RotationComponent>(m_editorCameraEntity).rotation = Vec3<>(-25.0f, -90.0f, 0.0f);
        auto& edCam = m_editorRegistry->get_or_emplace<CameraComponent>(m_editorCameraEntity);
        edCam.fov = 60.0f;
        edCam.nearPlane = 0.5f;
        edCam.farPlane = 20000000.0f;
        edCam.reverseZ = true;

        m_orbitPivot = Vec3<>(0.0f, 0.0f, 0.0f);
        m_orbitDistance = 82.0f;
        m_moveSpeed = 25.0f;
    }

    void EditorCameraManager::focusOnPosition(const Vec3<>& targetPos, float distance) {
        entt::registry* reg = nullptr;
        entt::entity camEntity = entt::null;
        if (!resolveControlledCamera(reg, camEntity)) {
            return;
        }

        auto& posComp = reg->get_or_emplace<PositionComponent>(camEntity);
        auto& rotComp = reg->get_or_emplace<RotationComponent>(camEntity);

        Vec3<> fwd, right, up;
        computeBasisFromEuler(rotComp.rotation, fwd, right, up);
        if (entt::entity parent = resolveValidParent(reg, camEntity);
            parent != entt::null && Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
            Mat4 parentRot = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(reg, parent);
            Vec4 wFwd = parentRot * Vec4(fwd.x, fwd.y, fwd.z, 0.0f);
            if (Vec3<>(wFwd.x, wFwd.y, wFwd.z).length() > 1e-5f) {
                fwd = normalize(Vec3<>(wFwd.x, wFwd.y, wFwd.z));
            }
        }

        float safeDist = std::clamp(distance, 2.0f, 5000.0f);
        Vec3<> newWorldPos = targetPos - fwd * safeDist;

        posComp.position = worldToEntityLocalPosition(reg, camEntity, newWorldPos);
        m_orbitPivot = targetPos;
        m_orbitDistance = safeDist;
    }

    void EditorCameraManager::handleFlyLookAndMove(
        entt::registry& reg,
        entt::entity camEntity,
        float deltaTime) {
        ImGuiIO& io = ImGui::GetIO();
        auto& posComp = reg.get_or_emplace<PositionComponent>(camEntity);
        auto& rotComp = reg.get_or_emplace<RotationComponent>(camEntity);

        if (std::abs(io.MouseDelta.x) > 0.001f || std::abs(io.MouseDelta.y) > 0.001f) {
            rotComp.rotation.y += io.MouseDelta.x * m_mouseSensitivity;
            rotComp.rotation.x -= io.MouseDelta.y * m_mouseSensitivity;
            rotComp.rotation.x = std::clamp(rotComp.rotation.x, -89.0f, 89.0f);
        }

        if (std::abs(io.MouseWheel) > 0.001f) {
            float factor = 1.0f + io.MouseWheel * 0.15f;
            m_moveSpeed = std::clamp(m_moveSpeed * std::max(0.2f, factor), 0.5f, 500000.0f);
        }

        Vec3<> fwd, right, up;
        computeBasisFromEuler(rotComp.rotation, fwd, right, up);
        if (entt::entity parent = resolveValidParent(&reg, camEntity);
            parent != entt::null && Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
            Mat4 parentRot = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(&reg, parent);
            Vec4 wFwd = parentRot * Vec4(fwd.x, fwd.y, fwd.z, 0.0f);
            Vec4 wRight = parentRot * Vec4(right.x, right.y, right.z, 0.0f);
            Vec4 wUp = parentRot * Vec4(up.x, up.y, up.z, 0.0f);
            if (Vec3<>(wFwd.x, wFwd.y, wFwd.z).length() > 1e-5f) {
                fwd = normalize(Vec3<>(wFwd.x, wFwd.y, wFwd.z));
            }
            if (Vec3<>(wRight.x, wRight.y, wRight.z).length() > 1e-5f) {
                right = normalize(Vec3<>(wRight.x, wRight.y, wRight.z));
            }
            if (Vec3<>(wUp.x, wUp.y, wUp.z).length() > 1e-5f) {
                up = normalize(Vec3<>(wUp.x, wUp.y, wUp.z));
            }
        }

        Vec3<> moveDir(0.0f, 0.0f, 0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            moveDir = moveDir + fwd;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            moveDir = moveDir - fwd;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            moveDir = moveDir + right;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            moveDir = moveDir - right;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) {
            moveDir = moveDir + Vec3<>(0.0f, 1.0f, 0.0f);
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_C)) {
            moveDir = moveDir - Vec3<>(0.0f, 1.0f, 0.0f);
        }

        Vec3<> currentWorldPos = queryEntityWorldPosition(&reg, camEntity);
        if (moveDir.length() > 1e-4f) {
            moveDir = normalize(moveDir);
            float speed = m_moveSpeed;
            if (io.KeyShift) {
                speed *= m_fastMultiplier;
            } else if (io.KeyCtrl) {
                speed *= 0.25f;
            }
            currentWorldPos = currentWorldPos + moveDir * (speed * deltaTime);
            posComp.position = worldToEntityLocalPosition(&reg, camEntity, currentWorldPos);
        }

        m_orbitPivot = currentWorldPos + fwd * m_orbitDistance;
    }

    void EditorCameraManager::handleOrbitCamera(entt::registry& reg, entt::entity camEntity) {
        ImGuiIO& io = ImGui::GetIO();
        if (std::abs(io.MouseDelta.x) < 0.001f && std::abs(io.MouseDelta.y) < 0.001f) {
            return;
        }

        auto& posComp = reg.get_or_emplace<PositionComponent>(camEntity);
        auto& rotComp = reg.get_or_emplace<RotationComponent>(camEntity);

        rotComp.rotation.y += io.MouseDelta.x * m_mouseSensitivity * 1.2f;
        rotComp.rotation.x -= io.MouseDelta.y * m_mouseSensitivity * 1.2f;
        rotComp.rotation.x = std::clamp(rotComp.rotation.x, -89.0f, 89.0f);

        Vec3<> fwd, right, up;
        computeBasisFromEuler(rotComp.rotation, fwd, right, up);
        if (entt::entity parent = resolveValidParent(&reg, camEntity);
            parent != entt::null && Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
            Mat4 parentRot = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(&reg, parent);
            Vec4 wFwd = parentRot * Vec4(fwd.x, fwd.y, fwd.z, 0.0f);
            if (Vec3<>(wFwd.x, wFwd.y, wFwd.z).length() > 1e-5f) {
                fwd = normalize(Vec3<>(wFwd.x, wFwd.y, wFwd.z));
            }
        }

        Vec3<> newWorldPos = m_orbitPivot - fwd * m_orbitDistance;
        posComp.position = worldToEntityLocalPosition(&reg, camEntity, newWorldPos);
    }

    void EditorCameraManager::handleMiddleButtonPan(entt::registry& reg, entt::entity camEntity) {
        ImGuiIO& io = ImGui::GetIO();
        if (std::abs(io.MouseDelta.x) < 0.001f && std::abs(io.MouseDelta.y) < 0.001f) {
            return;
        }

        auto& posComp = reg.get_or_emplace<PositionComponent>(camEntity);
        const auto& rotComp = reg.get_or_emplace<RotationComponent>(camEntity);

        Vec3<> fwd, right, up;
        computeBasisFromEuler(rotComp.rotation, fwd, right, up);
        if (entt::entity parent = resolveValidParent(&reg, camEntity);
            parent != entt::null && Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
            Mat4 parentRot = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(&reg, parent);
            Vec4 wRight = parentRot * Vec4(right.x, right.y, right.z, 0.0f);
            Vec4 wUp = parentRot * Vec4(up.x, up.y, up.z, 0.0f);
            if (Vec3<>(wRight.x, wRight.y, wRight.z).length() > 1e-5f) {
                right = normalize(Vec3<>(wRight.x, wRight.y, wRight.z));
            }
            if (Vec3<>(wUp.x, wUp.y, wUp.z).length() > 1e-5f) {
                up = normalize(Vec3<>(wUp.x, wUp.y, wUp.z));
            }
        }

        float panScale = std::max(0.02f, m_moveSpeed * 0.003f);
        Vec3<> delta = (right * (-io.MouseDelta.x) + up * io.MouseDelta.y) * panScale;
        Vec3<> newWorldPos = queryEntityWorldPosition(&reg, camEntity) + delta;
        posComp.position = worldToEntityLocalPosition(&reg, camEntity, newWorldPos);
        m_orbitPivot = m_orbitPivot + delta;
    }

    void EditorCameraManager::handleWheelZoom(
        entt::registry& reg,
        entt::entity camEntity,
        float wheelDelta) {
        auto& posComp = reg.get_or_emplace<PositionComponent>(camEntity);
        const auto& rotComp = reg.get_or_emplace<RotationComponent>(camEntity);

        Vec3<> fwd, right, up;
        computeBasisFromEuler(rotComp.rotation, fwd, right, up);
        if (entt::entity parent = resolveValidParent(&reg, camEntity);
            parent != entt::null && Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()) {
            Mat4 parentRot = Runtime::Engine::getInstance()->renderer->getMatrixCacheSystem()->computeEntityWorldRotationMatrix(&reg, parent);
            Vec4 wFwd = parentRot * Vec4(fwd.x, fwd.y, fwd.z, 0.0f);
            if (Vec3<>(wFwd.x, wFwd.y, wFwd.z).length() > 1e-5f) {
                fwd = normalize(Vec3<>(wFwd.x, wFwd.y, wFwd.z));
            }
        }

        float step = wheelDelta * std::max(1.0f, m_moveSpeed * 0.35f);
        Vec3<> newWorldPos = queryEntityWorldPosition(&reg, camEntity) + fwd * step;
        posComp.position = worldToEntityLocalPosition(&reg, camEntity, newWorldPos);
        m_orbitDistance = std::max(1.0f, m_orbitDistance - step);
    }

    void EditorCameraManager::updateViewportInput(
        bool isViewportHovered,
        bool isViewportFocused,
        float deltaTime) {
        entt::registry* reg = nullptr;
        entt::entity camEntity = entt::null;
        if (!resolveControlledCamera(reg, camEntity)) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        float dt = std::clamp(deltaTime > 0.0f ? deltaTime : io.DeltaTime, 0.0005f, 0.1f);

        bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        bool altLmbDown = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        if ((isViewportHovered && (rmbDown || mmbDown || altLmbDown)) ||
            (m_isNavigating && (rmbDown || mmbDown || altLmbDown))) {
            m_isNavigating = true;
        } else {
            m_isNavigating = false;
        }

        if (m_isNavigating) {
            if (rmbDown) {
                handleFlyLookAndMove(*reg, camEntity, dt);
            } else if (altLmbDown) {
                handleOrbitCamera(*reg, camEntity);
            } else if (mmbDown) {
                handleMiddleButtonPan(*reg, camEntity);
            }
        } else if (isViewportHovered && std::abs(io.MouseWheel) > 0.001f) {
            handleWheelZoom(*reg, camEntity, io.MouseWheel);
        }

        if ((isViewportHovered || isViewportFocused) && !io.WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            const auto& selected = g_editorGlobal.selectedEntity;
            if (selected.scene && selected.entity != entt::null) {
                entt::registry* selReg = selected.isGlobal
                    ? selected.scene->getRegistry()
                    : (selected.scene->getLoadedTrunk(selected.trunkName)
                        ? selected.scene->getLoadedTrunk(selected.trunkName)->getRegistry()
                        : nullptr);
                if (selReg && selReg->valid(selected.entity)) {
                    Vec3<> targetWorldPos = queryEntityWorldPosition(selReg, selected.entity);
                    focusOnPosition(targetWorldPos, std::clamp(m_moveSpeed * 1.2f, 8.0f, 250.0f));
                }
            }
        }
    }

} // namespace MQEngine
