#include "CameraControlUiModule.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    void CameraControlUiModule::onRender(UiContext& ctx) {
        ZoneScoped();
        if (ImGui::Begin(TEXT("相机控制"))) {
            const auto& state = ctx.state<CameraControlViewState>();
            renderEditorCameraOption(ctx, state);
            ImGui::Separator();
            renderEditorCameraInspector(ctx, state);
            ImGui::Separator();
            renderQuickActionButtons(ctx, state);
            ImGui::Separator();
            renderSceneCameraList(ctx, state);
            ImGui::Separator();
            renderActiveCameraSummary(state);
        }
        ImGui::End();
    }

    void CameraControlUiModule::renderEditorCameraOption(UiContext& ctx, const CameraControlViewState& state) {
        if (ImGui::RadioButton(TEXT("编辑器自由相机"), state.usingEditorCamera)) {
            if (!state.usingEditorCamera && state.hasEditorCamera) {
                ctx.emit(ActivateEditorCameraSignal{});
            }
        }
    }

    void CameraControlUiModule::renderEditorCameraInspector(UiContext& ctx, const CameraControlViewState& state) {
        if (!state.hasEditorCamera) {
            return;
        }

        ImGui::Text(TEXT("编辑器相机参数:"));
        float pos[3] = { state.editorCameraPos.x, state.editorCameraPos.y, state.editorCameraPos.z };
        float rot[3] = { state.editorCameraRot.x, state.editorCameraRot.y, state.editorCameraRot.z };
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3(TEXT("位置 (Pos)"), pos, 0.5f);
        transformChanged |= ImGui::DragFloat3(TEXT("旋转 (Pitch/Yaw/Roll)"), rot, 0.5f);
        if (transformChanged) {
            ctx.emit(UpdateEditorCameraTransformSignal{
                .position = FCT::Vec3(pos[0], pos[1], pos[2]),
                .rotation = FCT::Vec3(rot[0], rot[1], rot[2])
            });
        }

        UpdateEditorCameraSettingsSignal settingsSig{
            .fov = state.editorFov,
            .nearPlane = state.editorNearPlane,
            .farPlane = state.editorFarPlane,
            .reverseZ = state.editorReverseZ,
            .moveSpeed = state.moveSpeed,
            .mouseSensitivity = state.mouseSensitivity,
            .fastMultiplier = state.fastMultiplier
        };
        bool settingsChanged = false;
        settingsChanged |= ImGui::SliderFloat(TEXT("视野 (FOV)"), &settingsSig.fov, 15.0f, 120.0f, "%.1f");
        settingsChanged |= ImGui::DragFloat(TEXT("飞行速度"), &settingsSig.moveSpeed, 5.0f, 0.5f, 500000.0f, "%.1f");
        settingsChanged |= ImGui::SliderFloat(TEXT("Shift加速倍率"), &settingsSig.fastMultiplier, 1.5f, 20.0f, "%.1fx");
        settingsChanged |= ImGui::SliderFloat(TEXT("鼠标灵敏度"), &settingsSig.mouseSensitivity, 0.02f, 1.0f, "%.2f");
        settingsChanged |= ImGui::DragFloat(TEXT("近裁剪面"), &settingsSig.nearPlane, 0.05f, 0.01f, 1000.0f, "%.2f");
        settingsChanged |= ImGui::DragFloat(TEXT("远裁剪面"), &settingsSig.farPlane, 1000.0f, 100.0f, 20000000.0f, "%.0f");
        settingsChanged |= ImGui::Checkbox(TEXT("Reverse-Z 深度缓冲"), &settingsSig.reverseZ);
        if (settingsChanged) {
            ctx.emit(settingsSig);
        }
    }

    void CameraControlUiModule::renderQuickActionButtons(UiContext& ctx, const CameraControlViewState& state) {
        if (ImGui::Button(TEXT("聚焦选中实体 (F)"), ImVec2(-1, 0))) {
            if (state.hasSelectedEntity) {
                ctx.emit(FocusEntitySignal{
                    .entity = state.selectedEntity,
                    .trunkName = state.selectedTrunkName,
                    .isGlobal = state.selectedIsGlobal
                });
            }
        }
        if (ImGui::Button(TEXT("对齐编辑器相机到场景相机"), ImVec2(-1, 0))) {
            ctx.emit(SyncEditorCameraFromSceneSignal{});
        }
        if (ImGui::Button(TEXT("将当前视角写入场景相机"), ImVec2(-1, 0))) {
            ctx.emit(SyncSceneCameraFromEditorSignal{});
        }
        if (ImGui::Button(TEXT("重置编辑器相机"), ImVec2(-1, 0))) {
            ctx.emit(ResetEditorCameraSignal{});
        }
    }

    void CameraControlUiModule::renderSceneCameraList(UiContext& ctx, const CameraControlViewState& state) {
        ImGui::Text(TEXT("场景相机列表:"));
        if (state.sceneCameras.empty()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), TEXT("  无场景相机"));
            return;
        }

        for (const auto& cameraInfo : state.sceneCameras) {
            ImGui::PushID(static_cast<int>(entt::to_integral(cameraInfo.entity)));
            bool isSelected = cameraInfo.active && !state.usingEditorCamera;
            if (ImGui::RadioButton(cameraInfo.name.c_str(), isSelected)) {
                if (!isSelected) {
                    ctx.emit(ActivateSceneCameraSignal{
                        .registry = cameraInfo.registry,
                        .entity = cameraInfo.entity
                    });
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(TEXT("写入当前视角"))) {
                ctx.emit(SyncSceneCameraFromEditorSignal{
                    .targetRegistry = cameraInfo.registry,
                    .targetEntity = cameraInfo.entity
                });
            }
            ImGui::PopID();
        }
    }

    void CameraControlUiModule::renderActiveCameraSummary(const CameraControlViewState& state) {
        if (state.usingEditorCamera) {
            ImGui::Text(TEXT("当前激活: 编辑器自由相机"));
        } else {
            ImGui::Text(TEXT("当前激活: %s"), state.activeCameraLabel.c_str());
        }
        ImGui::TextDisabled(TEXT("操作: 右键+WASDQE飞行 | 滚轮调速/缩放 | 中键平移 | Alt+左键环绕 | F聚焦"));
    }

} // namespace MQEngine
