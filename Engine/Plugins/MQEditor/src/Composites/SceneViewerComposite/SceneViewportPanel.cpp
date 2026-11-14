/**
 * @file SceneViewportPanel.cpp
 * @brief 3D 视口面板渲染、相机漫游输入控制、三维网格坐标轴、Gizmo 交互操纵、射线拾取与资产拖拽生成实现
 */
#include "SceneViewerComposite.h"

#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/domain/core/Domain.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <MQEngine/domain/physics/PhysicsSubsystem.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr float kViewportPi = 3.14159265358979323846f;
            constexpr float kViewportDegToRad = kViewportPi / 180.0f;
            constexpr float kViewportRadToDeg = 180.0f / kViewportPi;

            void drawAxisArrowhead(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color, float headLen = 11.0f, float headWidth = 5.0f)
            {
                const float dx = to.x - from.x;
                const float dy = to.y - from.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-3f) return;

                const float ux = dx / len;
                const float uy = dy / len;
                const float px = -uy;
                const float py = ux;

                const ImVec2 base(to.x - ux * headLen, to.y - uy * headLen);
                const ImVec2 left(base.x + px * headWidth, base.y + py * headWidth);
                const ImVec2 right(base.x - px * headWidth, base.y - py * headWidth);
                drawList->AddTriangleFilled(to, left, right, color);
            }

            FCT::Vec3<> directionFromEulerDeg(float pitchDeg, float yawDeg)
            {
                const float pitchRad = pitchDeg * kViewportDegToRad;
                const float yawRad = yawDeg * kViewportDegToRad;
                return FCT::Vec3<>(
                    std::cos(pitchRad) * std::sin(yawRad),
                    -std::sin(pitchRad),
                    std::cos(pitchRad) * std::cos(yawRad)).normalize();
            }

            void eulerDegFromDirection(const FCT::Vec3<>& rawDir, float& outPitchDeg, float& outYawDeg)
            {
                FCT::Vec3<> d = rawDir;
                if (d.lengthSquared() < 1e-6f)
                {
                    outPitchDeg = 90.0f;
                    outYawDeg = 0.0f;
                    return;
                }
                d = d.normalize();
                outPitchDeg = std::asin(std::clamp(-d.y, -1.0f, 1.0f)) * kViewportRadToDeg;
                outYawDeg = std::atan2(d.x, d.z) * kViewportRadToDeg;
            }

            void buildOrthonormalBasis(const FCT::Vec3<>& normalDir, FCT::Vec3<>& outTangentU, FCT::Vec3<>& outTangentV)
            {
                const FCT::Vec3<> refUp = (std::abs(normalDir.y) < 0.92f)
                    ? FCT::Vec3<>(0.0f, 1.0f, 0.0f)
                    : FCT::Vec3<>(1.0f, 0.0f, 0.0f);
                outTangentU = refUp.cross(normalDir).normalize();
                outTangentV = normalDir.cross(outTangentU).normalize();
            }

            void drawLabelPill(ImDrawList* drawList, const ImVec2& pos, const char* text, ImU32 textCol, ImU32 bgCol = IM_COL32(14, 16, 22, 185))
            {
                const ImVec2 textSize = ImGui::CalcTextSize(text);
                const ImVec2 pad(5.0f, 2.5f);
                drawList->AddRectFilled(
                    ImVec2(pos.x - pad.x, pos.y - pad.y),
                    ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y),
                    bgCol,
                    4.0f);
                drawList->AddText(pos, textCol, text);
            }
        } // namespace

        void SceneViewerComposite::renderViewportToolbar()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.5f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.5f, 2.0f));

            if (ImGui::RadioButton(L("移动", "Move"), m_gizmoMode == 0)) m_gizmoMode = 0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("W");
            ImGui::SameLine();
            if (ImGui::RadioButton(L("旋转", "Rot"), m_gizmoMode == 1)) m_gizmoMode = 1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("E");
            ImGui::SameLine();
            if (ImGui::RadioButton(L("缩放", "Scale"), m_gizmoMode == 2)) m_gizmoMode = 2;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("R");
            ImGui::SameLine();
            if (m_editorUiComposite)
            {
                if (ImGui::Button(L("撤销", "Undo")))
                {
                    m_editorUiComposite->getCommandStack().undo();
                }
                if (ImGui::IsItemHovered() && m_editorUiComposite->getCommandStack().canUndo())
                {
                    ImGui::SetTooltip("%s: %s (Ctrl+Z)", L("撤销", "Undo"), m_editorUiComposite->getCommandStack().getUndoName().c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(L("重做", "Redo")))
                {
                    m_editorUiComposite->getCommandStack().redo();
                }
                if (ImGui::IsItemHovered() && m_editorUiComposite->getCommandStack().canRedo())
                {
                    ImGui::SetTooltip("%s: %s (Ctrl+Y)", L("重做", "Redo"), m_editorUiComposite->getCommandStack().getRedoName().c_str());
                }
                ImGui::SameLine();
            }
            ImGui::Checkbox(L("线框", "Wire"), &m_wireframe);
            ImGui::SameLine();
            ImGui::Checkbox(L("网格", "Grid"), &m_show3dGrid);
            ImGui::SameLine();
            ImGui::Checkbox(L("包围盒", "Bounds"), &m_showBoundingBoxes);
            ImGui::SameLine();
            ImGui::Checkbox(L("光源", "Lights"), &m_showLightGizmos);
            ImGui::SameLine();
            if (ImGui::Checkbox(L("抗锯齿", "AA"), &m_enableAntiAliasing))
            {
                if (auto* dom = Domain::Domain::InstancePtr(); dom && dom->getRenderer())
                {
                    dom->getRenderer()->setAntiAliasingEnabled(m_enableAntiAliasing);
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(52.0f);
            ImGui::SliderFloat("##ViewportCamSpeed", &m_cameraSpeed, 1.0f, 30.0f, L("速:%.0f", "Spd:%.0f"));
            ImGui::SameLine();

            if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F8, false))
            {
                toggleEditorAndGameCamera();
            }

            std::string camBtnLabel;
            if (m_cameraSource == ViewportCameraSource::EditorFree)
            {
                camBtnLabel = L("相机: 编辑器自由", "Cam: Editor Free");
            }
            else if (m_cameraSource == ViewportCameraSource::ActiveGameMain)
            {
                camBtnLabel = L("相机: 游戏主视角", "Cam: Game Main");
            }
            else
            {
                std::string specName = "Camera";
                for (const auto& ent : m_entities)
                {
                    if (ent.ecsEntity == m_specificCameraEcsEntity && ent.registryName == m_specificCameraRegistry)
                    {
                        specName = ent.name;
                        break;
                    }
                }
                camBtnLabel = std::string(L("相机: ", "Cam: ")) + specName;
            }

            if (ImGui::Button((camBtnLabel + "###ViewportCameraToggleBtn").c_str()))
            {
                toggleEditorAndGameCamera();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", L(
                    "随时在“编辑器自由相机”与“游戏主视角 (MainWindow)”之间切换 (F8)，编辑态与 PIE 运行态均可用",
                    "Toggle between Editor Free Camera and Game Main Camera at any time (F8)"));
            }
            ImGui::SameLine(0.0f, 1.0f);
            if (ImGui::ArrowButton("##ViewportCameraSelectDropdown", ImGuiDir_Down))
            {
                ImGui::OpenPopup("ViewportCameraSelectPopup");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", L("选择视口相机源（自由相机 / 游戏主视角 / 场景任意相机实体）", "Select Viewport Camera Source"));
            }

            if (ImGui::BeginPopup("ViewportCameraSelectPopup"))
            {
                if (ImGui::MenuItem(
                        L("编辑器自由相机 (Editor Free Camera)", "Editor Free Camera"),
                        "F8",
                        m_cameraSource == ViewportCameraSource::EditorFree))
                {
                    setViewportCameraSource(ViewportCameraSource::EditorFree);
                }
                if (ImGui::MenuItem(
                        L("游戏主视角 (MainWindow 激活相机)", "Game Main Camera (MainWindow)"),
                        "F8",
                        m_cameraSource == ViewportCameraSource::ActiveGameMain))
                {
                    setViewportCameraSource(ViewportCameraSource::ActiveGameMain);
                }

                ImGui::Separator();
                ImGui::TextDisabled("%s", L("场景相机实体预览 (Scene Cameras):", "Scene Camera Entities:"));
                bool anySceneCam = false;
                for (const auto& ent : m_entities)
                {
                    if (!ent.hasCamera || ent.ecsEntity == FCT::ECS::null)
                    {
                        continue;
                    }
                    anySceneCam = true;
                    const std::string itemLabel =
                        ent.name + " [" + ent.registryName + " -> " +
                        (ent.cameraTargetName.empty() ? "MainWindow" : ent.cameraTargetName) + "]";
                    const bool isSelectedCam =
                        (m_cameraSource == ViewportCameraSource::SpecificEntity &&
                         m_specificCameraEcsEntity == ent.ecsEntity &&
                         m_specificCameraRegistry == ent.registryName);
                    if (ImGui::MenuItem(itemLabel.c_str(), nullptr, isSelectedCam))
                    {
                        setSpecificCameraEntity(ent.registryName, ent.ecsEntity);
                    }
                }
                if (!anySceneCam)
                {
                    ImGui::TextDisabled("  %s", L("(当前场景无 CameraComponent 实体)", "(No CameraComponent entities)"));
                }

                ImGui::Separator();
                if (ImGui::MenuItem(
                        L("将自由相机对齐到当前视角并接管", "Align Free Camera to Current View")))
                {
                    alignEditorCameraToCurrentView();
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();

            // PIE 拆分运行控制与快捷键（默认从主场景开始，支持下拉切换从当前场景开始）
            bool pendingStart = false;
            PieStartMode pendingMode = m_pieStartMode;
            bool pendingStop = false;

            const bool piePlaying = isPiePlaying();
            if (!ImGui::GetIO().WantTextInput)
            {
                if (!piePlaying)
                {
                    if (!ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F5, false))
                    {
                        m_pieStartMode = PieStartMode::FromMainScene;
                        pendingStart = true;
                        pendingMode = PieStartMode::FromMainScene;
                    }
                    else if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
                    {
                        m_pieStartMode = PieStartMode::FromCurrentScene;
                        pendingStart = true;
                        pendingMode = PieStartMode::FromCurrentScene;
                    }
                }
                else if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F5, false))
                {
                    pendingStop = true;
                }
            }

            if (!piePlaying)
            {
                const char* primaryLabel = (m_pieStartMode == PieStartMode::FromMainScene)
                    ? L("▶ 开始运行 (主场景)", "▶ Play (Main)")
                    : L("▶ 开始运行 (当前场景)", "▶ Play (Current)");
                if (ImGui::Button(primaryLabel))
                {
                    pendingStart = true;
                    pendingMode = m_pieStartMode;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", (m_pieStartMode == PieStartMode::FromMainScene)
                        ? L("从主场景开始游戏 (F5)", "Play From Main Scene (F5)")
                        : L("从当前场景开始游戏 (F6)", "Play From Current Scene (F6)"));
                }
                ImGui::SameLine(0.0f, 1.0f);
                if (ImGui::ArrowButton("##PieStartModeDropdown", ImGuiDir_Down))
                {
                    ImGui::OpenPopup("PieStartModePopup");
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", L("选择运行启动方式（主场景 / 当前场景）", "Select PIE Start Mode (Main / Current)"));
                }

                if (ImGui::BeginPopup("PieStartModePopup"))
                {
                    std::string startupHint = getStartupSceneDisplayHint();
                    if (startupHint.empty())
                    {
                        startupHint = L("未配置 (将回退当前场景)", "Not configured (fallback to current)");
                    }

                    if (ImGui::MenuItem(
                            L("▶ 从主场景开始游戏 (默认, F5)", "▶ Play From Main Scene (Default, F5)"),
                            nullptr,
                            m_pieStartMode == PieStartMode::FromMainScene))
                    {
                        m_pieStartMode = PieStartMode::FromMainScene;
                        pendingStart = true;
                        pendingMode = PieStartMode::FromMainScene;
                    }
                    ImGui::TextDisabled("   %s: %s", L("主场景", "startup_scene"), startupHint.c_str());

                    if (ImGui::MenuItem(
                            L("▷ 从当前场景开始游戏 (F6)", "▷ Play From Current Scene (F6)"),
                            nullptr,
                            m_pieStartMode == PieStartMode::FromCurrentScene))
                    {
                        m_pieStartMode = PieStartMode::FromCurrentScene;
                        pendingStart = true;
                        pendingMode = PieStartMode::FromCurrentScene;
                    }
                    ImGui::EndPopup();
                }
            }
            else
            {
                if (ImGui::Button(L("■ 停止", "■ Stop")))
                {
                    pendingStop = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", L("停止游戏逻辑并完整回滚至进入 PIE 前的编辑态场景 (Shift+F5)",
                                              "Stop gameplay and restore edit-mode scene (Shift+F5)"));
                }
            }

            if (pendingStop)
            {
                stopPieSession();
            }
            else if (pendingStart)
            {
                startPieSession(pendingMode);
            }

            ImGui::PopStyleVar(2);
        }

        void SceneViewerComposite::handleViewportCameraInput(bool isHovered, float deltaTime)
        {
            ZoneScoped();
            if (!isHovered) return;

            ImGuiIO& io = ImGui::GetIO();

            // W/E/R 快捷键切换 Gizmo 模式与 F 聚焦实体（自由相机激活时在编辑态与 PIE 下均可用）
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && isEditorFreeCameraActive())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoMode = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoMode = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoMode = 2;
                if (ImGui::IsKeyPressed(ImGuiKey_F))
                {
                    if (const auto* sel = getSelectedEntity())
                    {
                        m_cameraTarget[0] = sel->position[0];
                        m_cameraTarget[1] = sel->position[1];
                        m_cameraTarget[2] = sel->position[2];
                        m_cameraPos[0] = sel->position[0];
                        m_cameraPos[1] = sel->position[1] + 3.0f;
                        m_cameraPos[2] = sel->position[2] - 6.0f;
                        syncCameraToRenderer();
                    }
                }
            }

            if (!isEditorFreeCameraActive())
            {
                return;
            }

            FCT::Vec3<> worldUp(0.0f, 1.0f, 0.0f);
            FCT::Vec3<> eye(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
            FCT::Vec3<> target(m_cameraTarget[0], m_cameraTarget[1], m_cameraTarget[2]);
            FCT::Vec3<> forward = target - eye;
            if (forward.lengthSquared() < 1e-6f)
            {
                forward = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
            }
            else
            {
                forward = forward.normalize();
            }
            if (std::abs(forward.dot(worldUp)) > 0.99f)
            {
                worldUp = FCT::Vec3<>(0.0f, 0.0f, 1.0f);
            }
            FCT::Vec3<> right = worldUp.cross(forward).normalize();
            FCT::Vec3<> up = forward.cross(right).normalize();

            bool cameraChanged = false;

            // 1. 鼠标右键按住：WASDQE 漫游飞行 + 视角旋转
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                const float moveStep = m_cameraSpeed * deltaTime;
                FCT::Vec3<> deltaMove(0.0f, 0.0f, 0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W)) deltaMove = deltaMove + forward * moveStep;
                if (ImGui::IsKeyDown(ImGuiKey_S)) deltaMove = deltaMove - forward * moveStep;
                if (ImGui::IsKeyDown(ImGuiKey_D)) deltaMove = deltaMove + right * moveStep;
                if (ImGui::IsKeyDown(ImGuiKey_A)) deltaMove = deltaMove - right * moveStep;
                if (ImGui::IsKeyDown(ImGuiKey_E)) deltaMove = deltaMove + FCT::Vec3<>(0.0f, moveStep, 0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_Q)) deltaMove = deltaMove - FCT::Vec3<>(0.0f, moveStep, 0.0f);

                eye = eye + deltaMove;
                target = target + deltaMove;

                if (std::abs(io.MouseDelta.x) > 0.01f || std::abs(io.MouseDelta.y) > 0.01f)
                {
                    constexpr float kRadToDeg = 180.0f / 3.14159265f;
                    constexpr float kDegToRad = 3.14159265f / 180.0f;
                    m_cameraRot[0] = std::asin(std::clamp(-forward.y, -1.0f, 1.0f)) * kRadToDeg;
                    m_cameraRot[1] = std::atan2(forward.x, forward.z) * kRadToDeg;

                    m_cameraRot[1] += io.MouseDelta.x * 0.25f;
                    m_cameraRot[0] = std::clamp(m_cameraRot[0] + io.MouseDelta.y * 0.25f, -85.0f, 85.0f);
                    const float pitchRad = m_cameraRot[0] * kDegToRad;
                    const float yawRad = m_cameraRot[1] * kDegToRad;
                    const float dist = std::max((target - eye).length(), 1.0f);
                    FCT::Vec3<> newForward(
                        std::cos(pitchRad) * std::sin(yawRad),
                        -std::sin(pitchRad),
                        std::cos(pitchRad) * std::cos(yawRad));
                    target = eye + newForward.normalize() * dist;
                }
                cameraChanged = true;
            }

            // 2. 鼠标中键拖拽：视口平面平移 (Pan)
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const float panSpeed = m_editorCameraOrthographic
                    ? ((2.0f * std::max(m_editorCameraOrthoSize, 0.1f)) / std::max(static_cast<float>(m_lastViewportHeight), 1.0f))
                    : 0.02f;
                FCT::Vec3<> panOffset = right * (-io.MouseDelta.x * panSpeed) + up * (io.MouseDelta.y * panSpeed);
                eye = eye + panOffset;
                target = target + panOffset;
                cameraChanged = true;
            }

            // 3. 鼠标滚轮推拉缩放（正交模式调整正交半高，透视模式沿前向推进）
            if (std::abs(io.MouseWheel) > 0.01f)
            {
                if (m_editorCameraOrthographic)
                {
                    const float zoomFactor = std::clamp(1.0f - io.MouseWheel * 0.12f, 0.5f, 1.5f);
                    m_editorCameraOrthoSize = std::clamp(m_editorCameraOrthoSize * zoomFactor, 0.5f, 500.0f);
                }
                else
                {
                    FCT::Vec3<> zoomOffset = forward * (io.MouseWheel * 1.2f);
                    eye = eye + zoomOffset;
                }
                cameraChanged = true;
            }

            if (cameraChanged)
            {
                m_cameraPos[0] = eye.x; m_cameraPos[1] = eye.y; m_cameraPos[2] = eye.z;
                m_cameraTarget[0] = target.x; m_cameraTarget[1] = target.y; m_cameraTarget[2] = target.z;
                syncCameraToRenderer();
            }
        }

        void SceneViewerComposite::render3dGridAndAxes(
            ImDrawList* drawList,
            const FCT::Mat4& viewProj,
            const ImVec2& canvasP0,
            const ImVec2& avail)
        {
            if (m_show3dGrid)
            {
                constexpr int kGridHalfExtent = 8;
                for (int i = -kGridHalfExtent; i <= kGridHalfExtent; ++i)
                {
                    const float coord = static_cast<float>(i);
                    const ImU32 zLineCol = (i == 0) ? IM_COL32(75, 145, 250, 210) : IM_COL32(55, 62, 76, 110);
                    const ImU32 xLineCol = (i == 0) ? IM_COL32(240, 75, 75, 210) : IM_COL32(55, 62, 76, 110);
                    const float zLineThick = (i == 0) ? 1.8f : 1.0f;
                    const float xLineThick = (i == 0) ? 1.8f : 1.0f;

                    ImVec2 p0, p1;
                    if (projectWorldToScreen(FCT::Vec3<>(coord, 0.0f, -static_cast<float>(kGridHalfExtent)), viewProj, canvasP0, avail, p0) &&
                        projectWorldToScreen(FCT::Vec3<>(coord, 0.0f,  static_cast<float>(kGridHalfExtent)), viewProj, canvasP0, avail, p1))
                    {
                        drawList->AddLine(p0, p1, zLineCol, zLineThick);
                    }
                    if (projectWorldToScreen(FCT::Vec3<>(-static_cast<float>(kGridHalfExtent), 0.0f, coord), viewProj, canvasP0, avail, p0) &&
                        projectWorldToScreen(FCT::Vec3<>( static_cast<float>(kGridHalfExtent), 0.0f, coord), viewProj, canvasP0, avail, p1))
                    {
                        drawList->AddLine(p0, p1, xLineCol, xLineThick);
                    }
                }
            }

            // 视口右上角三维相机方向坐标轴指示器 (Viewport Corner Orientation Gizmo)
            if (avail.x > 160.0f && avail.y > 160.0f)
            {
                const ImVec2 gizmoCenter(canvasP0.x + avail.x - 56.0f, canvasP0.y + 56.0f);
                const float gizmoRadius = 38.0f;
                drawList->AddCircleFilled(gizmoCenter, gizmoRadius + 8.0f, IM_COL32(18, 20, 26, 165), 32);
                drawList->AddCircle(gizmoCenter, gizmoRadius + 8.0f, IM_COL32(80, 90, 110, 140), 32, 1.2f);

            const FCT::Mat4 view = computeViewMatrix();
            FCT::Vec3<> camRight(view.m[0], view.m[1], view.m[2]);
            FCT::Vec3<> camUp(-view.m[4], -view.m[5], -view.m[6]);
            if (camRight.lengthSquared() > 1e-6f)
            {
                camRight.normalize();
            }
            else
            {
                camRight = FCT::Vec3<>(1.0f, 0.0f, 0.0f);
            }
            if (camUp.lengthSquared() > 1e-6f)
            {
                camUp.normalize();
            }
            else
            {
                camUp = FCT::Vec3<>(0.0f, 1.0f, 0.0f);
            }

                struct CornerAxis
                {
                    FCT::Vec3<> worldDir;
                    ImU32 color;
                    const char* label;
                };
                const CornerAxis axes[3] = {
                    {FCT::Vec3<>(1.0f, 0.0f, 0.0f), IM_COL32(245, 75, 75, 255), "X"},
                    {FCT::Vec3<>(0.0f, 1.0f, 0.0f), IM_COL32(75, 240, 75, 255), "Y"},
                    {FCT::Vec3<>(0.0f, 0.0f, 1.0f), IM_COL32(75, 145, 255, 255), "Z"}
                };

                for (const auto& ax : axes)
                {
                    const float sx = ax.worldDir.dot(camRight);
                    const float sy = -ax.worldDir.dot(camUp);
                    const ImVec2 tip(gizmoCenter.x + sx * gizmoRadius, gizmoCenter.y + sy * gizmoRadius);
                    drawList->AddLine(gizmoCenter, tip, ax.color, 2.4f);
                    drawAxisArrowhead(drawList, gizmoCenter, tip, ax.color, 8.5f, 4.0f);
                    drawList->AddCircleFilled(tip, 4.0f, ax.color);
                    drawList->AddText(ImVec2(tip.x + (sx >= 0.0f ? 5.0f : -12.0f), tip.y + (sy >= 0.0f ? 2.0f : -14.0f)), ax.color, ax.label);
                }

                // 在右上角方位球内同步指示当前主方向光入射矢量
                for (const auto& ent : m_entities)
                {
                    if (!ent.active || !ent.hasDirectionalLight)
                    {
                        continue;
                    }
                    FCT::Vec3<> lightDir(ent.lightDirection[0], ent.lightDirection[1], ent.lightDirection[2]);
                    if (lightDir.lengthSquared() < 1e-6f)
                    {
                        break;
                    }
                    lightDir = lightDir.normalize();
                    const FCT::Vec3<> toSun = lightDir * -1.0f;
                    const float sunX = toSun.dot(camRight);
                    const float sunY = -toSun.dot(camUp);
                    const ImVec2 sunOrb(gizmoCenter.x + sunX * (gizmoRadius * 0.88f), gizmoCenter.y + sunY * (gizmoRadius * 0.88f));
                    const ImVec2 rayInEnd(gizmoCenter.x + sunX * (gizmoRadius * 0.24f), gizmoCenter.y + sunY * (gizmoRadius * 0.24f));
                    const ImU32 sunCol = IM_COL32(255, 220, 80, 245);
                    drawList->AddLine(sunOrb, rayInEnd, sunCol, 2.0f);
                    drawAxisArrowhead(drawList, sunOrb, rayInEnd, sunCol, 7.5f, 3.6f);
                    drawList->AddCircleFilled(sunOrb, 5.2f, IM_COL32(255, 215, 70, 90));
                    drawList->AddCircleFilled(sunOrb, 3.5f, IM_COL32(255, 245, 175, 255));
                    drawList->AddText(ImVec2(sunOrb.x + (sunX >= 0.0f ? 5.0f : -12.0f), sunOrb.y - 7.0f), sunCol, "L");
                    break;
                }

                drawList->AddCircleFilled(gizmoCenter, 3.5f, IM_COL32(230, 235, 245, 255));
            }
        }

        void SceneViewerComposite::render3dEntities(
            ImDrawList* drawList,
            const FCT::Mat4& viewProj,
            const ImVec2& canvasP0,
            const ImVec2& avail)
        {
            if (!m_showBoundingBoxes) return;

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;

            static constexpr int kBoxEdges[12][2] = {
                {0, 1}, {1, 3}, {3, 2}, {2, 0},
                {4, 5}, {5, 7}, {7, 6}, {6, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}
            };

            for (const auto& entity : m_entities)
            {
                if (!entity.active || !entity.hasMeshRenderer)
                {
                    continue;
                }
                if (m_syncFilterToViewport && !isEntityPassingFilter(entity))
                {
                    continue;
                }
                if (scene && !scene->isRegistryVisible(entity.registryName))
                {
                    continue;
                }

                const bool isSelected = (entity.id == m_selectedEntityId);
                if (!m_wireframe && !isSelected)
                {
                    continue;
                }

                const FCT::Mat4 rotMat = FCT::Mat4::RotateZ(entity.rotation[2]) *
                                         FCT::Mat4::RotateY(entity.rotation[1]) *
                                         FCT::Mat4::RotateX(entity.rotation[0]);
                FCT::Mat4 worldMat = FCT::Mat4::Translate(entity.position[0], entity.position[1], entity.position[2]) *
                                     rotMat *
                                     FCT::Mat4::Scale(entity.scale[0], entity.scale[1], entity.scale[2]);

                FCT::ECS::registry* entReg = resolveEntityRegistry(entity);
                if (entReg && entity.ecsEntity != FCT::ECS::null && entReg->valid(entity.ecsEntity))
                {
                    if (const auto* worldTf = entReg->try_get<Domain::Scene::WorldTransform>(entity.ecsEntity))
                    {
                        worldMat = worldTf->worldMatrix;
                    }
                }

                float localMinY = -0.5f;
                float localMaxY =  0.5f;
                float localHalfXZ = 0.5f;
                if (entity.primitiveType == 1)
                {
                    localMinY = -0.01f;
                    localMaxY =  0.01f;
                }

                const FCT::Vec3<> localCorners[8] = {
                    {-localHalfXZ, localMinY, -localHalfXZ},
                    { localHalfXZ, localMinY, -localHalfXZ},
                    {-localHalfXZ, localMaxY, -localHalfXZ},
                    { localHalfXZ, localMaxY, -localHalfXZ},
                    {-localHalfXZ, localMinY,  localHalfXZ},
                    { localHalfXZ, localMinY,  localHalfXZ},
                    {-localHalfXZ, localMaxY,  localHalfXZ},
                    { localHalfXZ, localMaxY,  localHalfXZ}
                };

                ImVec2 screenCorners[8];
                bool validCorner[8] = {false};
                float worldMaxY = entity.position[1];
                for (int c = 0; c < 8; ++c)
                {
                    const FCT::Vec4 wc4 = worldMat * FCT::Vec4(localCorners[c].x, localCorners[c].y, localCorners[c].z, 1.0f);
                    const FCT::Vec3<> wc(wc4.x, wc4.y, wc4.z);
                    worldMaxY = std::max(worldMaxY, wc.y);
                    validCorner[c] = projectWorldToScreen(wc, viewProj, canvasP0, avail, screenCorners[c]);
                }

                const ImU32 boxCol = isSelected ? IM_COL32(255, 200, 50, 255) : IM_COL32(110, 175, 235, 170);
                const float thickness = isSelected ? 2.2f : 1.2f;

                for (int e = 0; e < 12; ++e)
                {
                    const int i0 = kBoxEdges[e][0];
                    const int i1 = kBoxEdges[e][1];
                    if (validCorner[i0] && validCorner[i1])
                    {
                        drawList->AddLine(screenCorners[i0], screenCorners[i1], boxCol, thickness);
                    }
                }

                ImVec2 labelPos;
                FCT::Vec3<> centerPos(entity.position[0], worldMaxY + 0.25f, entity.position[2]);
                if (projectWorldToScreen(centerPos, viewProj, canvasP0, avail, labelPos))
                {
                    drawLabelPill(
                        drawList,
                        labelPos,
                        entity.name.c_str(),
                        isSelected ? IM_COL32(255, 235, 120, 255) : IM_COL32(215, 225, 235, 230));
                }
            }
        }

        void SceneViewerComposite::renderLightGizmos(
            ImDrawList* drawList,
            const FCT::Mat4& viewProj,
            const ImVec2& canvasP0,
            const ImVec2& avail,
            bool isHovered)
        {
            if (!m_showLightGizmos)
            {
                m_draggingLightDir = false;
                return;
            }

            auto* domain = Domain::Domain::InstancePtr();
            auto* scene = (domain && domain->getScene() && domain->getScene()->isInitialized())
                              ? domain->getScene()
                              : nullptr;

            auto draw3dCircle = [&](const FCT::Vec3<>& center, const FCT::Vec3<>& uAxis, const FCT::Vec3<>& vAxis, float radius, int segments, ImU32 color, float thickness) {
                ImVec2 prevPt;
                bool prevValid = false;
                for (int s = 0; s <= segments; ++s)
                {
                    const float angle = (static_cast<float>(s) / static_cast<float>(segments)) * 2.0f * kViewportPi;
                    const FCT::Vec3<> worldPt = center + uAxis * (std::cos(angle) * radius) + vAxis * (std::sin(angle) * radius);
                    ImVec2 curPt;
                    const bool curValid = projectWorldToScreen(worldPt, viewProj, canvasP0, avail, curPt);
                    if (prevValid && curValid)
                    {
                        drawList->AddLine(prevPt, curPt, color, thickness);
                    }
                    prevPt = curPt;
                    prevValid = curValid;
                }
            };

            ImGuiIO& io = ImGui::GetIO();
            constexpr float kPrimaryRayLength = 2.15f;
            constexpr float kSideRayLength = 1.45f;
            constexpr float kEmitterRingRadius = 0.42f;

            for (auto& entity : m_entities)
            {
                if (!entity.active || (!entity.hasDirectionalLight && !entity.hasPointLight))
                {
                    continue;
                }
                if (m_syncFilterToViewport && !isEntityPassingFilter(entity))
                {
                    continue;
                }
                if (scene && !scene->isRegistryVisible(entity.registryName))
                {
                    continue;
                }

                const bool isSelected = (entity.id == m_selectedEntityId);
                const FCT::Vec3<> lightPos(entity.position[0], entity.position[1], entity.position[2]);

                if (entity.hasDirectionalLight)
                {
                    FCT::Vec3<> lightDir(entity.lightDirection[0], entity.lightDirection[1], entity.lightDirection[2]);
                    if (lightDir.lengthSquared() < 1e-6f)
                    {
                        lightDir = FCT::Vec3<>(0.0f, -1.0f, 0.0f);
                    }
                    else
                    {
                        lightDir = lightDir.normalize();
                    }

                    FCT::Vec3<> tangentU, tangentV;
                    buildOrthonormalBasis(lightDir, tangentU, tangentV);

                    // 1. 地面落点追踪虚线与落点靶环（直观显示平行光在地面上的入射方位）
                    if (lightDir.y < -0.05f && lightPos.y > 0.05f)
                    {
                        const float hitT = -lightPos.y / lightDir.y;
                        if (hitT > 0.1f && hitT < 35.0f)
                        {
                            const FCT::Vec3<> groundHit = lightPos + lightDir * hitT;
                            const FCT::Vec3<> traceStart = lightPos + lightDir * std::min(kPrimaryRayLength, hitT);
                            constexpr int kDashCount = 7;
                            for (int d = 0; d < kDashCount; ++d)
                            {
                                const float t0 = static_cast<float>(d * 2) / static_cast<float>(kDashCount * 2);
                                const float t1 = static_cast<float>(d * 2 + 1) / static_cast<float>(kDashCount * 2);
                                const FCT::Vec3<> p0World = traceStart + (groundHit - traceStart) * t0;
                                const FCT::Vec3<> p1World = traceStart + (groundHit - traceStart) * t1;
                                ImVec2 s0, s1;
                                if (projectWorldToScreen(p0World, viewProj, canvasP0, avail, s0) &&
                                    projectWorldToScreen(p1World, viewProj, canvasP0, avail, s1))
                                {
                                    drawList->AddLine(s0, s1, IM_COL32(255, 220, 95, 115), 1.4f);
                                }
                            }
                            draw3dCircle(
                                FCT::Vec3<>(groundHit.x, 0.02f, groundHit.z),
                                FCT::Vec3<>(1.0f, 0.0f, 0.0f),
                                FCT::Vec3<>(0.0f, 0.0f, 1.0f),
                                0.32f,
                                20,
                                IM_COL32(255, 220, 95, 155),
                                1.6f);
                        }
                    }

                    // 2. 平行光发射圆盘环与 4 条外围平行光束箭头
                    const ImU32 ringCol = isSelected ? IM_COL32(255, 235, 115, 235) : IM_COL32(255, 215, 95, 185);
                    draw3dCircle(lightPos, tangentU, tangentV, kEmitterRingRadius, 24, ringCol, isSelected ? 2.0f : 1.5f);

                    for (int r = 0; r < 4; ++r)
                    {
                        const float angle = static_cast<float>(r) * (0.5f * kViewportPi) + 0.25f * kViewportPi;
                        const FCT::Vec3<> radialOffset =
                            (tangentU * std::cos(angle) + tangentV * std::sin(angle)) * kEmitterRingRadius;
                        const FCT::Vec3<> sideStart = lightPos + radialOffset;
                        const FCT::Vec3<> sideEnd = sideStart + lightDir * kSideRayLength;
                        ImVec2 sideStartScr, sideEndScr;
                        if (projectWorldToScreen(sideStart, viewProj, canvasP0, avail, sideStartScr) &&
                            projectWorldToScreen(sideEnd, viewProj, canvasP0, avail, sideEndScr))
                        {
                            drawList->AddLine(sideStartScr, sideEndScr, IM_COL32(18, 16, 10, 115), 3.4f);
                            drawList->AddLine(sideStartScr, sideEndScr, IM_COL32(255, 218, 92, 205), 1.8f);
                            drawAxisArrowhead(drawList, sideStartScr, sideEndScr, IM_COL32(255, 222, 100, 220), 9.5f, 4.3f);
                        }
                    }

                    // 3. 中心主光线方向箭杆 + 可拖拽方向控制手柄
                    const FCT::Vec3<> tipWorld = lightPos + lightDir * kPrimaryRayLength;
                    ImVec2 sunScreen, tipScreen;
                    const bool sunVisible = projectWorldToScreen(lightPos, viewProj, canvasP0, avail, sunScreen);
                    const bool tipVisible = projectWorldToScreen(tipWorld, viewProj, canvasP0, avail, tipScreen);

                    bool tipHovered = false;
                    if (tipVisible && isHovered && m_activeGizmoAxis < 0)
                    {
                        const float dx = io.MousePos.x - tipScreen.x;
                        const float dy = io.MousePos.y - tipScreen.y;
                        tipHovered = (dx * dx + dy * dy <= 15.0f * 15.0f);
                        if (tipHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                        {
                            m_draggingLightDir = true;
                            m_draggingLightEntityId = entity.id;
                            m_selectedEntityId = entity.id;
                            m_lightDragStartDir[0] = lightDir.x;
                            m_lightDragStartDir[1] = lightDir.y;
                            m_lightDragStartDir[2] = lightDir.z;
                            FCT::Vec3<> camEye(m_cameraPos[0], m_cameraPos[1], m_cameraPos[2]);
                            if (auto* domain = Domain::Domain::InstancePtr();
                                domain && domain->getRenderer() && domain->getRenderer()->isInitialized())
                            {
                                if (const auto activeCam = domain->getRenderer()->findActiveCameraForTarget(getActiveViewportTargetName()))
                                {
                                    camEye = activeCam->worldPosition;
                                }
                            }
                            m_lightDragPlaneDepth = std::max((tipWorld - camEye).length(), 0.5f);
                        }
                    }

                    const bool isDraggingThis = m_draggingLightDir && (m_draggingLightEntityId == entity.id);
                    if (sunVisible && tipVisible)
                    {
                        const ImU32 mainRayCol = (isDraggingThis || tipHovered)
                            ? IM_COL32(120, 245, 255, 255)
                            : IM_COL32(255, 232, 95, 255);
                        drawList->AddLine(sunScreen, tipScreen, IM_COL32(16, 14, 10, 165), 5.2f);
                        drawList->AddLine(sunScreen, tipScreen, mainRayCol, 3.1f);
                        drawAxisArrowhead(drawList, sunScreen, tipScreen, mainRayCol, 14.5f, 6.8f);
                        drawList->AddCircleFilled(tipScreen, (isDraggingThis || tipHovered) ? 6.5f : 5.0f, mainRayCol);
                        drawList->AddCircle(tipScreen, 8.0f, IM_COL32(255, 250, 210, 190), 16, 1.4f);
                    }

                    // 4. 太阳发光圆盘与 8 芒日冕徽章
                    if (sunVisible)
                    {
                        drawList->AddCircleFilled(sunScreen, 16.0f, IM_COL32(255, 205, 65, 60), 24);
                        for (int f = 0; f < 8; ++f)
                        {
                            const float ang = static_cast<float>(f) * (0.25f * kViewportPi);
                            const float rInner = 11.0f;
                            const float rOuter = (f % 2 == 0) ? 17.5f : 14.5f;
                            const ImVec2 f0(sunScreen.x + std::cos(ang) * rInner, sunScreen.y + std::sin(ang) * rInner);
                            const ImVec2 f1(sunScreen.x + std::cos(ang) * rOuter, sunScreen.y + std::sin(ang) * rOuter);
                            drawList->AddLine(f0, f1, IM_COL32(255, 228, 105, 235), 2.0f);
                        }
                        drawList->AddCircleFilled(sunScreen, 8.5f, IM_COL32(255, 215, 75, 245), 24);
                        drawList->AddCircleFilled(sunScreen, 5.2f, IM_COL32(255, 250, 210, 255), 20);
                        drawList->AddCircle(
                            sunScreen,
                            9.5f,
                            isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 190, 55, 230),
                            24,
                            1.8f);

                        const std::string sunLabel = entity.name + " [Sun]";
                        const ImVec2 sunTextSz = ImGui::CalcTextSize(sunLabel.c_str());
                        drawLabelPill(
                            drawList,
                            ImVec2(sunScreen.x - sunTextSz.x * 0.5f, sunScreen.y - sunTextSz.y - 22.0f),
                            sunLabel.c_str(),
                            isSelected ? IM_COL32(255, 242, 145, 255) : IM_COL32(255, 232, 165, 240));
                    }
                }
                else if (entity.hasPointLight)
                {
                    // 点光源：全向辐射星芒 + 地面垂线高度定位 + 选中时显示三正交衰减球面环
                    if (lightPos.y > 0.05f)
                    {
                        ImVec2 topScr, groundScr;
                        if (projectWorldToScreen(lightPos, viewProj, canvasP0, avail, topScr) &&
                            projectWorldToScreen(FCT::Vec3<>(lightPos.x, 0.0f, lightPos.z), viewProj, canvasP0, avail, groundScr))
                        {
                            drawList->AddLine(topScr, groundScr, IM_COL32(255, 185, 85, 95), 1.2f);
                            drawList->AddCircleFilled(groundScr, 3.2f, IM_COL32(255, 185, 85, 150));
                        }
                    }

                    if (isSelected)
                    {
                        const float rangeR = std::max(0.5f, entity.pointLightRange);
                        const ImU32 rangeCol = IM_COL32(255, 185, 75, 95);
                        draw3dCircle(lightPos, FCT::Vec3<>(1.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), rangeR, 40, rangeCol, 1.3f);
                        draw3dCircle(lightPos, FCT::Vec3<>(1.0f, 0.0f, 0.0f), FCT::Vec3<>(0.0f, 1.0f, 0.0f), rangeR, 40, rangeCol, 1.3f);
                        draw3dCircle(lightPos, FCT::Vec3<>(0.0f, 1.0f, 0.0f), FCT::Vec3<>(0.0f, 0.0f, 1.0f), rangeR, 40, rangeCol, 1.3f);
                    }

                    const FCT::Vec3<> omniDirs[6] = {
                        { 0.34f, 0.0f, 0.0f}, {-0.34f, 0.0f, 0.0f},
                        { 0.0f, 0.34f, 0.0f}, { 0.0f,-0.34f, 0.0f},
                        { 0.0f, 0.0f, 0.34f}, { 0.0f, 0.0f,-0.34f}
                    };
                    ImVec2 bulbScr;
                    if (projectWorldToScreen(lightPos, viewProj, canvasP0, avail, bulbScr))
                    {
                        for (const auto& od : omniDirs)
                        {
                            ImVec2 tipScr;
                            if (projectWorldToScreen(lightPos + od, viewProj, canvasP0, avail, tipScr))
                            {
                                drawList->AddLine(bulbScr, tipScr, IM_COL32(255, 190, 85, 200), 1.8f);
                            }
                        }
                        drawList->AddCircleFilled(bulbScr, 13.5f, IM_COL32(255, 165, 55, 65), 20);
                        drawList->AddCircleFilled(bulbScr, 7.0f, IM_COL32(255, 185, 70, 245), 20);
                        drawList->AddCircleFilled(bulbScr, 4.0f, IM_COL32(255, 248, 215, 255), 16);
                        drawList->AddCircle(
                            bulbScr,
                            8.2f,
                            isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 165, 55, 225),
                            20,
                            1.6f);

                        const ImVec2 ptTextSz = ImGui::CalcTextSize(entity.name.c_str());
                        drawLabelPill(
                            drawList,
                            ImVec2(bulbScr.x - ptTextSz.x * 0.5f, bulbScr.y - ptTextSz.y - 18.0f),
                            entity.name.c_str(),
                            isSelected ? IM_COL32(255, 232, 145, 255) : IM_COL32(255, 212, 145, 235));
                    }
                }
            }

            // 处理主方向光箭尖手柄的实时三维拖拽交互
            if (m_draggingLightDir)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    if (auto* dragEnt = findEntityById(m_draggingLightEntityId); dragEnt && dragEnt->hasDirectionalLight)
                    {
                        FCT::Vec3<> rayOrigin, rayDir;
                        buildCameraRay(io.MousePos, canvasP0, avail, rayOrigin, rayDir);
                        const FCT::Vec3<> targetWorld = rayOrigin + rayDir * m_lightDragPlaneDepth;
                        const FCT::Vec3<> sunPos(dragEnt->position[0], dragEnt->position[1], dragEnt->position[2]);
                        FCT::Vec3<> newLightDir = targetWorld - sunPos;
                        if (newLightDir.lengthSquared() > 1e-4f)
                        {
                            newLightDir = newLightDir.normalize();
                            dragEnt->lightDirection[0] = newLightDir.x;
                            dragEnt->lightDirection[1] = newLightDir.y;
                            dragEnt->lightDirection[2] = newLightDir.z;
                            eulerDegFromDirection(newLightDir, dragEnt->rotation[0], dragEnt->rotation[1]);
                            syncEntityToDomain(*dragEnt);
                        }
                    }
                }
                else
                {
                    m_draggingLightDir = false;
                    m_draggingLightEntityId = 0;
                }
            }
        }

        void SceneViewerComposite::renderAndHandle3dGizmo(
            ImDrawList* drawList,
            const FCT::Mat4& viewProj,
            const ImVec2& canvasP0,
            const ImVec2& avail,
            bool isHovered)
        {
            auto* selected = getSelectedEntity();
            if (!selected || !selected->hasTransform) return;

            FCT::Vec3<> center(selected->position[0], selected->position[1], selected->position[2]);
            ImVec2 centerScreen;
            if (!projectWorldToScreen(center, viewProj, canvasP0, avail, centerScreen))
            {
                return;
            }

            const float axisLength = 1.6f;
            const FCT::Vec3<> axisEnds[3] = {
                center + FCT::Vec3<>(axisLength, 0.0f, 0.0f),
                center + FCT::Vec3<>(0.0f, axisLength, 0.0f),
                center + FCT::Vec3<>(0.0f, 0.0f, axisLength)
            };
            const ImU32 axisColors[3] = {
                IM_COL32(240, 65, 65, 255),
                IM_COL32(65, 235, 65, 255),
                IM_COL32(65, 135, 245, 255)
            };
            const char* axisNames[3] = {"X", "Y", "Z"};

            ImVec2 endScreens[3];
            bool axisValid[3] = {false};

            for (int a = 0; a < 3; ++a)
            {
                axisValid[a] = projectWorldToScreen(axisEnds[a], viewProj, canvasP0, avail, endScreens[a]);
                if (axisValid[a])
                {
                    const bool isActive = (m_activeGizmoAxis == a);
                    const ImU32 col = isActive ? IM_COL32(255, 245, 90, 255) : axisColors[a];
                    drawList->AddLine(centerScreen, endScreens[a], col, isActive ? 4.0f : 3.0f);
                    drawAxisArrowhead(drawList, centerScreen, endScreens[a], col, 13.0f, 6.0f);
                    drawList->AddCircleFilled(endScreens[a], 4.5f, col);
                    drawList->AddText(ImVec2(endScreens[a].x + 7.0f, endScreens[a].y - 8.0f), col, axisNames[a]);
                }
            }
            drawList->AddCircleFilled(centerScreen, 5.0f, IM_COL32(245, 245, 250, 255));

            ImGuiIO& io = ImGui::GetIO();
            if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                for (int a = 0; a < 3; ++a)
                {
                    if (!axisValid[a]) continue;
                    const float dx = io.MousePos.x - endScreens[a].x;
                    const float dy = io.MousePos.y - endScreens[a].y;
                    if (dx * dx + dy * dy <= 16.0f * 16.0f)
                    {
                        m_activeGizmoAxis = a;
                        m_gizmoDragStartMouse = io.MousePos;
                        if (m_gizmoMode == 0) std::memcpy(m_gizmoDragStartVal, selected->position, sizeof(m_gizmoDragStartVal));
                        else if (m_gizmoMode == 1) std::memcpy(m_gizmoDragStartVal, selected->rotation, sizeof(m_gizmoDragStartVal));
                        else std::memcpy(m_gizmoDragStartVal, selected->scale, sizeof(m_gizmoDragStartVal));
                        break;
                    }
                }
            }

            if (m_activeGizmoAxis >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const int axis = m_activeGizmoAxis;
                ImVec2 axisDir(endScreens[axis].x - centerScreen.x, endScreens[axis].y - centerScreen.y);
                const float len = std::sqrt(axisDir.x * axisDir.x + axisDir.y * axisDir.y);
                if (len > 1.0f)
                {
                    axisDir.x /= len;
                    axisDir.y /= len;
                }
                const float mouseProj = (io.MousePos.x - m_gizmoDragStartMouse.x) * axisDir.x +
                                        (io.MousePos.y - m_gizmoDragStartMouse.y) * axisDir.y;

                if (m_gizmoMode == 0)
                {
                    selected->position[axis] = m_gizmoDragStartVal[axis] + mouseProj * 0.03f;
                }
                else if (m_gizmoMode == 1)
                {
                    selected->rotation[axis] = m_gizmoDragStartVal[axis] + mouseProj * 0.5f;
                    if (selected->hasDirectionalLight)
                    {
                        const FCT::Vec3<> newDir = directionFromEulerDeg(selected->rotation[0], selected->rotation[1]);
                        selected->lightDirection[0] = newDir.x;
                        selected->lightDirection[1] = newDir.y;
                        selected->lightDirection[2] = newDir.z;
                    }
                }
                else
                {
                    selected->scale[axis] = std::max(0.05f, m_gizmoDragStartVal[axis] + mouseProj * 0.02f);
                }
                syncEntityToDomain(*selected);
            }
            else if (m_activeGizmoAxis >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const uint32_t entityId = selected->id;
                const int mode = m_gizmoMode;
                float oldVal[3];
                float newVal[3];
                std::memcpy(oldVal, m_gizmoDragStartVal, sizeof(oldVal));
                if (mode == 0) std::memcpy(newVal, selected->position, sizeof(newVal));
                else if (mode == 1) std::memcpy(newVal, selected->rotation, sizeof(newVal));
                else std::memcpy(newVal, selected->scale, sizeof(newVal));

                FCT::ECS::registry* regPtr = resolveEntityRegistry(*selected);

                Domain::Scene::LocalTransform oldLocal{};
                Domain::Scene::LocalTransform newLocal{};
                oldLocal.position = FCT::Vec3<>(selected->position[0], selected->position[1], selected->position[2]);
                oldLocal.rotation = FCT::Vec3<>(selected->rotation[0], selected->rotation[1], selected->rotation[2]);
                oldLocal.scale = FCT::Vec3<>(selected->scale[0], selected->scale[1], selected->scale[2]);
                newLocal = oldLocal;

                if (mode == 0)
                {
                    oldLocal.position = FCT::Vec3<>(oldVal[0], oldVal[1], oldVal[2]);
                    newLocal.position = FCT::Vec3<>(newVal[0], newVal[1], newVal[2]);
                }
                else if (mode == 1)
                {
                    oldLocal.rotation = FCT::Vec3<>(oldVal[0], oldVal[1], oldVal[2]);
                    newLocal.rotation = FCT::Vec3<>(newVal[0], newVal[1], newVal[2]);
                }
                else
                {
                    oldLocal.scale = FCT::Vec3<>(oldVal[0], oldVal[1], oldVal[2]);
                    newLocal.scale = FCT::Vec3<>(newVal[0], newVal[1], newVal[2]);
                }
                oldLocal.isDirty = true;
                newLocal.isDirty = true;

                if (m_editorUiComposite)
                {
                    const char* fieldDesc = (mode == 0) ? "position" : (mode == 1 ? "rotation" : "scale");
                    m_editorUiComposite->getCommandStack().executeCommand(
                        makeUnique<EditorBase::ComponentModifyCommand<Domain::Scene::LocalTransform>>(
                            regPtr,
                            selected->ecsEntity,
                            oldLocal,
                            newLocal,
                            fieldDesc,
                            [this, entityId](const Domain::Scene::LocalTransform& applied) {
                                if (auto* ent = findEntityById(entityId))
                                {
                                    ent->position[0] = applied.position.x;
                                    ent->position[1] = applied.position.y;
                                    ent->position[2] = applied.position.z;
                                    ent->rotation[0] = applied.rotation.x;
                                    ent->rotation[1] = applied.rotation.y;
                                    ent->rotation[2] = applied.rotation.z;
                                    if (ent->hasDirectionalLight)
                                    {
                                        const FCT::Vec3<> syncedDir = directionFromEulerDeg(ent->rotation[0], ent->rotation[1]);
                                        ent->lightDirection[0] = syncedDir.x;
                                        ent->lightDirection[1] = syncedDir.y;
                                        ent->lightDirection[2] = syncedDir.z;
                                    }
                                    ent->scale[0] = applied.scale.x;
                                    ent->scale[1] = applied.scale.y;
                                    ent->scale[2] = applied.scale.z;
                                    syncEntityToDomain(*ent);
                                }
                            }));
                }
                m_activeGizmoAxis = -1;
            }
        }

        void SceneViewerComposite::handleViewportPicking(
            const ImVec2& canvasP0,
            const ImVec2& avail,
            const FCT::Mat4& viewProj,
            bool isHovered)
        {
            ZoneScoped();
            if (!isHovered || m_activeGizmoAxis >= 0 || m_draggingLightDir || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                return;
            }

            ImGuiIO& io = ImGui::GetIO();
            FCT::Vec3<> rayOrigin, rayDir;
            buildCameraRay(io.MousePos, canvasP0, avail, rayOrigin, rayDir);

            // 1. 优先通过 Domain::Scene::SpatialGraph 进行真实 3D 射线求交
            auto* domain = Domain::Domain::InstancePtr();
            if (domain && domain->getScene() && domain->getScene()->isInitialized())
            {
                auto hits = domain->getScene()->getSpatialGraph().raycast(rayOrigin, rayDir, m_cameraFar);
                for (auto hitEntity : hits)
                {
                    for (const auto& ent : m_entities)
                    {
                        if (!ent.active || (m_syncFilterToViewport && !isEntityPassingFilter(ent)))
                        {
                            continue;
                        }
                        if (ent.registryName == "Main" && ent.ecsEntity == hitEntity && ent.name != "GroundPlane") // 优先选中非地面物体
                        {
                            m_selectedEntityId = ent.id;
                            if (m_editorUiComposite)
                            {
                                EditorBase::SelectEntitySignal sig;
                                sig.entityId = ent.id;
                                sig.entityName = ent.name;
                                m_editorUiComposite->getUiContext().emit(std::move(sig));
                            }
                            return;
                        }
                    }
                }
            }

            // 2. 屏幕空间投影最近点兜底拾取
            float bestDistSq = 28.0f * 28.0f;
            uint32_t bestId = 0;
            for (const auto& ent : m_entities)
            {
                if (!ent.active || !ent.hasTransform || (m_syncFilterToViewport && !isEntityPassingFilter(ent)))
                {
                    continue;
                }
                ImVec2 screenPt;
                if (projectWorldToScreen(FCT::Vec3<>(ent.position[0], ent.position[1], ent.position[2]), viewProj, canvasP0, avail, screenPt))
                {
                    const float dx = io.MousePos.x - screenPt.x;
                    const float dy = io.MousePos.y - screenPt.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestDistSq)
                    {
                        bestDistSq = d2;
                        bestId = ent.id;
                    }
                }
            }

            if (bestId != 0)
            {
                m_selectedEntityId = bestId;
                if (auto* ent = findEntityById(bestId); ent && m_editorUiComposite)
                {
                    EditorBase::SelectEntitySignal sig;
                    sig.entityId = ent->id;
                    sig.entityName = ent->name;
                    m_editorUiComposite->getUiContext().emit(std::move(sig));
                }
            }
        }

        void SceneViewerComposite::renderViewport()
        {
            ZoneScoped();
            const std::string vpTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle("3D 视口 (Scene Viewport)")
                : "3D 视口###3D 视口 (Scene Viewport)";
            ImGui::Begin(vpTitle.c_str());

            renderViewportToolbar();
            ImGui::Separator();

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x < 50.0f) avail.x = 50.0f;
            if (avail.y < 50.0f) avail.y = 50.0f;
            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 canvasP1 = ImVec2(canvasP0.x + avail.x, canvasP0.y + avail.y);
            const ImVec2 mainVpPos = ImGui::GetMainViewport() ? ImGui::GetMainViewport()->Pos : ImVec2(0.0f, 0.0f);
            m_lastViewportWidth = static_cast<uint32_t>(avail.x);
            m_lastViewportHeight = static_cast<uint32_t>(avail.y);
            m_lastViewportOffsetX = static_cast<int32_t>(std::lround(canvasP0.x - mainVpPos.x));
            m_lastViewportOffsetY = static_cast<int32_t>(std::lround(canvasP0.y - mainVpPos.y));
            m_lastViewportAspect = avail.x / avail.y;

            if (auto* domain = Domain::Domain::InstancePtr(); domain && domain->getRenderer())
            {
                domain->getRenderer()->setViewportSize(m_lastViewportWidth, m_lastViewportHeight);
            }
            syncCameraToRenderer();

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // 绑定并绘制 RenderGraph 产出的真实离屏 SceneColorTarget 纹理
            ImTextureID sceneTexId = static_cast<ImTextureID>(0);
            if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
            {
                if (auto* fctImgui = m_editorUiComposite->getImGuiComposite()->getImguiContext())
                {
                    sceneTexId = fctImgui->getTexture("SceneColorTarget");
                }
            }

            if (sceneTexId != static_cast<ImTextureID>(0))
            {
                ImGui::Image(sceneTexId, avail);
                ImGui::SetCursorScreenPos(canvasP0);
            }
            else
            {
                drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(24, 26, 32, 255));
            }

            ImGui::InvisibleButton(
                "ViewportInteractiveCanvas",
                avail,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            const bool isHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
            handleHierarchyAndViewportShortcuts(isHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));

            // 接收从资产浏览器拖拽过来的网格或材质/纹理资产
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MQ_ASSET_PAYLOAD"))
                {
                    const std::string droppedPath = static_cast<const char*>(payload->Data);
                    const std::string ext = Resource::TextureDecoderRegistry::extractExtension(droppedPath);
                    if (Resource::TextureDecoderRegistry::InstancePtr() &&
                        Resource::TextureDecoderRegistry::InstancePtr()->isExtensionSupported(ext))
                    {
                        if (auto* sel = getSelectedEntity())
                        {
                            sel->materialName = droppedPath;
                        }
                        if (auto* uploader = Asset::AsyncTextureUploader::InstancePtr())
                        {
                            Asset::TextureUploadDesc uploadDesc;
                            uploadDesc.filePath = droppedPath;
                            uploadDesc.debugName = droppedPath;
                            (void)uploader->uploadTextureAsync(uploadDesc, [this, droppedPath](StatusOr<Asset::TextureUploadResult> resOr) {
                                if (resOr.ok() && m_editorUiComposite)
                                {
                                    m_editorUiComposite->addLog(
                                        EditorBase::LogEntry::Level::Info,
                                        "视口拖拽应用纹理就绪: " + droppedPath + " (" +
                                            std::to_string(resOr->width) + "x" + std::to_string(resOr->height) + ")",
                                        "ViewportDrop");
                                }
                            });
                        }
                    }
                    else
                    {
                        // 1. 计算鼠标释放点对应的相机射线与地面/场景交点 (BVH / Physics / 地面平面 Y=0)
                        FCT::Vec3<> rayOrigin, rayDir;
                        buildCameraRay(ImGui::GetMousePos(), canvasP0, avail, rayOrigin, rayDir);

                        FCT::Vec3<> dropPos(0.0f, 0.0f, 0.0f);
                        bool hitFound = false;

                        auto* domain = Domain::Domain::InstancePtr();
                        if (domain && domain->getPhysics() && domain->getPhysics()->isInitialized())
                        {
                            Domain::Physics::RaycastHit hit;
                            if (domain->getPhysics()->raycast(rayOrigin, rayDir, 2000.0f, hit))
                            {
                                dropPos = hit.point;
                                hitFound = true;
                            }
                        }

                        if (!hitFound)
                        {
                            if (std::abs(rayDir.y) > 1e-4f)
                            {
                                const float t = -rayOrigin.y / rayDir.y;
                                if (t > 0.0f)
                                {
                                    dropPos = rayOrigin + rayDir * t;
                                    hitFound = true;
                                }
                            }
                            if (!hitFound)
                            {
                                dropPos = rayOrigin + rayDir * 10.0f;
                                dropPos.y = 0.0f;
                            }
                        }

                        // 2. 检查是否为 3D 模型资产 (.obj, .gltf, .glb, .fbx)
                        std::string lowerExt = ext;
                        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), [](unsigned char c) {
                            return static_cast<char>(std::tolower(c));
                        });
                        const bool isModel = (lowerExt == "obj" || lowerExt == "gltf" || lowerExt == "glb" || lowerExt == "fbx");

                        std::string baseName = std::filesystem::path(droppedPath).stem().string();
                        if (baseName.empty()) baseName = "Model";
                        std::string entityName = baseName + "_" + std::to_string(m_nextEntityId);

                        createNewEntity(entityName);
                        if (auto* sel = getSelectedEntity())
                        {
                            sel->position[0] = dropPos.x;
                            sel->position[1] = dropPos.y;
                            sel->position[2] = dropPos.z;
                            sel->hasMeshRenderer = true;
                            sel->meshName = droppedPath;

                            if (isModel)
                            {
                                Domain::Renderer::LoadedModelMeshInfo modelInfo{};
                                FCT::StaticMesh<uint32_t>* gpuMesh = nullptr;
                                if (auto* renderer = Domain::Renderer::RendererSubsystem::InstancePtr())
                                {
                                    gpuMesh = renderer->loadOrGetModelMesh(droppedPath, &modelInfo);
                                }
                                sel->primitiveType = 3; // CustomMesh
                                sel->customMesh = gpuMesh;
                            }
                            syncEntityToDomain(*sel);
                        }

                        if (m_editorUiComposite)
                        {
                            m_editorUiComposite->addLog(
                                EditorBase::LogEntry::Level::Info,
                                "视口生成实体: " + entityName + " 坐标: (" +
                                    std::to_string(dropPos.x) + ", " +
                                    std::to_string(dropPos.y) + ", " +
                                    std::to_string(dropPos.z) + ") 资产: " + droppedPath,
                                "ViewportDrop");
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            handleViewportCameraInput(isHovered, m_lastDeltaTime);

            const FCT::Mat4 viewProj = computeProjMatrix(m_lastViewportAspect) * computeViewMatrix();
            drawList->PushClipRect(canvasP0, canvasP1, true);
            const bool showEditorSceneOverlays =
                !isPiePlaying() || (m_cameraSource != ViewportCameraSource::ActiveGameMain);
            if (showEditorSceneOverlays)
            {
                render3dGridAndAxes(drawList, viewProj, canvasP0, avail);
                render3dEntities(drawList, viewProj, canvasP0, avail);
                renderLightGizmos(drawList, viewProj, canvasP0, avail, isHovered);
                renderAndHandle3dGizmo(drawList, viewProj, canvasP0, avail, isHovered);
                handleViewportPicking(canvasP0, avail, viewProj, isHovered);
            }

            if (m_cameraSource == ViewportCameraSource::ActiveGameMain)
            {
                bool hasMainCamera = false;
                if (auto* domain = Domain::Domain::InstancePtr();
                    domain && domain->getRenderer() && domain->getRenderer()->isInitialized())
                {
                    hasMainCamera = domain->getRenderer()->findActiveCameraForTarget("MainWindow").has_value();
                }
                if (!hasMainCamera)
                {
                    const char* warnText = "当前没有激活相机渲染到 MainWindow (No active camera rendering to MainWindow)";
                    const ImVec2 textSize = ImGui::CalcTextSize(warnText);
                    const ImVec2 pad(16.0f, 10.0f);
                    const ImVec2 center(canvasP0.x + avail.x * 0.5f, canvasP0.y + avail.y * 0.5f);
                    const ImVec2 boxMin(center.x - textSize.x * 0.5f - pad.x, center.y - textSize.y * 0.5f - pad.y);
                    const ImVec2 boxMax(center.x + textSize.x * 0.5f + pad.x, center.y + textSize.y * 0.5f + pad.y);
                    ImDrawList* overlayDrawList = ImGui::GetWindowDrawList();
                    overlayDrawList->AddRectFilled(boxMin, boxMax, IM_COL32(28, 22, 16, 220), 6.0f);
                    overlayDrawList->AddRect(boxMin, boxMax, IM_COL32(235, 165, 55, 220), 6.0f, 0, 1.5f);
                    overlayDrawList->AddText(
                        ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                        IM_COL32(255, 220, 150, 255),
                        warnText);
                }
            }

            const char* titleLine = sceneTexId
                ? (m_enableAntiAliasing
                       ? "MQEngine 3D Viewport [Vulkan RDG: HDR PBR + PCF Shadow + FXAA]"
                       : "MQEngine 3D Viewport [Vulkan RDG: HDR PBR + PCF Shadow]")
                : "MQEngine 3D Realtime Viewport";
            const char* helpLine = m_editorUiComposite
                ? m_editorUiComposite->tr(
                      "右键+WASDQE漫游 | 中键平移 | 滚轮缩放 | F聚焦 | 左键拖拽Gizmo轴/光源方向手柄",
                      "RMB+WASDQE Fly | MMB Pan | Wheel Zoom | F Focus | LMB Gizmo/Light Ray Handle")
                : "右键+WASDQE漫游 | 中键平移 | 滚轮缩放 | F聚焦 | 左键拖拽Gizmo轴/光源方向手柄";
            const float hudWidth = std::max(ImGui::CalcTextSize(titleLine).x, ImGui::CalcTextSize(helpLine).x) + 18.0f;
            drawList->AddRectFilled(
                ImVec2(canvasP0.x + 6.0f, canvasP0.y + 6.0f),
                ImVec2(canvasP0.x + 6.0f + hudWidth, canvasP0.y + 48.0f),
                IM_COL32(12, 15, 22, 170),
                6.0f);
            drawList->AddText(
                ImVec2(canvasP0.x + 14.0f, canvasP0.y + 10.0f),
                IM_COL32(230, 238, 250, 255),
                titleLine);
            drawList->AddText(
                ImVec2(canvasP0.x + 14.0f, canvasP0.y + 28.0f),
                IM_COL32(180, 192, 210, 255),
                helpLine);
            drawList->PopClipRect();

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine
