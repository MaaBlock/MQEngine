#include "SceneViewportUiModule.h"
#include "../core/Global.h"
#include "../imgui/EditorCameraManager.h"

#define TEXT(str) (const char*)u8##str

namespace MQEngine {

    namespace {
        bool ParseViewportPipePayload(const ImGuiPayload* payload,
                                      std::string& outFirst,
                                      std::string& outSecond) {
            if (!payload || !payload->Data) {
                return false;
            }
            std::string dragData = static_cast<const char*>(payload->Data);
            size_t separatorPos = dragData.find('|');
            if (separatorPos == std::string::npos) {
                return false;
            }
            outFirst = dragData.substr(0, separatorPos);
            outSecond = dragData.substr(separatorPos + 1);
            return true;
        }
    }

    void SceneViewportUiModule::onRender(UiContext& ctx) {
        ZoneScoped();
        ImGui::Begin(TEXT("场景视口"));

        const auto& viewState = ctx.state<ViewportViewState>();
        bool isWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        renderViewportToolbar(ctx, viewState);
        ImGui::Separator();

        if (viewState.sceneTextureId) {
            renderViewportImage(ctx, viewState, isWindowFocused);
        } else {
            renderMissingTextureHint();
        }

        ImGui::End();
    }

    void SceneViewportUiModule::renderViewportToolbar(UiContext& ctx, const ViewportViewState& viewState) {
        if (!viewState.usingEditorCamera) {
            if (ImGui::SmallButton(TEXT("切换回编辑器相机"))) {
                ctx.emit(ActivateEditorCameraSignal{});
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(TEXT("对齐场景相机"))) {
            ctx.emit(SyncEditorCameraFromSceneSignal{});
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT("聚焦选中 (F)"))) {
            if (viewState.hasSelectedEntity) {
                ctx.emit(FocusEntitySignal{
                    .entity = viewState.selectedEntity,
                    .trunkName = viewState.selectedTrunkName,
                    .isGlobal = viewState.selectedIsGlobal
                });
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT("重置视角"))) {
            ctx.emit(ResetEditorCameraSignal{});
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        float speed = viewState.moveSpeed;
        if (ImGui::DragFloat(TEXT("速度##ViewportSpeed"), &speed, 1.0f, 0.5f, 5000.0f, "%.1f")) {
            if (g_editorGlobal.cameraManager) {
                g_editorGlobal.cameraManager->setMoveSpeed(speed);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Pos:(%.1f, %.1f, %.1f)",
            viewState.cameraPos.x, viewState.cameraPos.y, viewState.cameraPos.z);
    }

    void SceneViewportUiModule::renderViewportImage(
        UiContext& ctx,
        const ViewportViewState& viewState,
        bool isWindowFocused) {
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        float footerReserve = 24.0f;
        ImVec2 windowSize(availSize.x, std::max(64.0f, availSize.y - footerReserve));

        float aspectRatio = viewState.aspectRatio > 0.0f ? viewState.aspectRatio : (800.0f / 600.0f);
        float imageWidth = windowSize.x;
        float imageHeight = imageWidth / aspectRatio;

        if (imageHeight > windowSize.y) {
            imageHeight = windowSize.y;
            imageWidth = imageHeight * aspectRatio;
        }

        float offsetX = (windowSize.x - imageWidth) * 0.5f;
        float offsetY = (windowSize.y - imageHeight) * 0.5f;

        if (offsetX > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        }
        if (offsetY > 0.0f) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
        }

        ImGui::Image(ImTextureRef(viewState.sceneTextureId), ImVec2(imageWidth, imageHeight));

        bool isImageHovered = ImGui::IsItemHovered();
        ImVec2 vpMin = ImGui::GetItemRectMin();
        ImVec2 vpSize = ImGui::GetItemRectSize();

        if (isImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::SetWindowFocus();
        }

        if (g_editorGlobal.cameraManager) {
            g_editorGlobal.cameraManager->updateViewportInput(
                isImageHovered,
                isWindowFocused,
                ImGui::GetIO().DeltaTime);
        }

        // 视口物体拾取、3D 平移 Gizmo 交互渲染及四级输入优先级仲裁
        m_interactionManager.updateAndRender(
            ctx,
            viewState,
            FCT::Vec2(vpMin.x, vpMin.y),
            FCT::Vec2(vpSize.x, vpSize.y),
            isImageHovered,
            isWindowFocused);

        handleViewportDragDrop(ctx, viewState);

        ImGui::TextDisabled(
            TEXT("视口: %.0fx%.0f | 右键+WASDQE飞行(Shift加速/滚轮调速) | 中键平移 | Alt+左键环绕 | 支持直接拖入模型/网格/贴图/脚本"),
            imageWidth,
            imageHeight);
    }

    void SceneViewportUiModule::handleViewportDragDrop(UiContext& ctx, const ViewportViewState& viewState) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH_FROM_MODEL")) {
            std::string modelUuid;
            std::string meshName;
            if (ParseViewportPipePayload(payload, modelUuid, meshName)) {
                if (ImGui::GetIO().KeyCtrl && viewState.hasSelectedEntity) {
                    ctx.emit(BindMeshToEntitySignal{
                        .entity = viewState.selectedEntity,
                        .trunkName = viewState.selectedTrunkName,
                        .isGlobal = viewState.selectedIsGlobal,
                        .modelUuid = std::move(modelUuid),
                        .meshName = std::move(meshName)
                    });
                } else {
                    ctx.emit(SpawnModelInSceneSignal{
                        .modelUuid = std::move(modelUuid),
                        .meshName = std::move(meshName),
                        .modelName = "",
                        .trunkName = viewState.defaultTrunkName,
                        .isGlobal = viewState.defaultTrunkName.empty()
                    });
                }
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL")) {
            std::string modelUuid;
            std::string modelName;
            if (ParseViewportPipePayload(payload, modelUuid, modelName)) {
                ctx.emit(SpawnModelInSceneSignal{
                    .modelUuid = std::move(modelUuid),
                    .meshName = "",
                    .modelName = std::move(modelName),
                    .trunkName = viewState.defaultTrunkName,
                    .isGlobal = viewState.defaultTrunkName.empty()
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE_FROM_MODEL")) {
            std::string modelUuid;
            std::string texturePath;
            if (viewState.hasSelectedEntity && ParseViewportPipePayload(payload, modelUuid, texturePath)) {
                ctx.emit(BindTextureToEntitySignal{
                    .entity = viewState.selectedEntity,
                    .trunkName = viewState.selectedTrunkName,
                    .isGlobal = viewState.selectedIsGlobal,
                    .modelUuid = std::move(modelUuid),
                    .texturePath = std::move(texturePath),
                    .slot = TextureBindSlot::Diffuse
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_CLASS")) {
            if (viewState.hasSelectedEntity && payload->Data) {
                std::string className = static_cast<const char*>(payload->Data);
                ctx.emit(BindScriptToEntitySignal{
                    .entity = viewState.selectedEntity,
                    .trunkName = viewState.selectedTrunkName,
                    .isGlobal = viewState.selectedIsGlobal,
                    .scriptName = std::move(className),
                    .mode = ScriptBindMode::ClassBinding
                });
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_FUNCTION")) {
            if (viewState.hasSelectedEntity && payload->Data) {
                std::string funcName = static_cast<const char*>(payload->Data);
                ctx.emit(BindScriptToEntitySignal{
                    .entity = viewState.selectedEntity,
                    .trunkName = viewState.selectedTrunkName,
                    .isGlobal = viewState.selectedIsGlobal,
                    .scriptName = std::move(funcName),
                    .mode = ScriptBindMode::FunctionTable
                });
            }
        }

        ImGui::EndDragDropTarget();
    }

    void SceneViewportUiModule::renderMissingTextureHint() {
        ImGui::Text(TEXT("场景纹理未找到"));
        ImGui::Text(TEXT("请检查SceneColorTarget纹理是否正确创建"));
    }

} // namespace MQEngine
