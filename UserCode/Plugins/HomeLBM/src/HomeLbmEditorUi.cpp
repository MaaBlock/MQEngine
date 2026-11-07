#include "HomeLbmEditorUi.h"
#include <cstdio>

#define LBM_TEXT(str) reinterpret_cast<const char*>(u8##str)

namespace MQEngine {

namespace {

bool drawBoundaryCombo(const char* label, LbmBoundaryType& inoutType) {
    static const char* const kItems[] = {
        LBM_TEXT("无滑移固壁 (No-Slip)"),
        LBM_TEXT("自由滑移壁 (Free-Slip)"),
        LBM_TEXT("速度入口 (Velocity Inlet)"),
        LBM_TEXT("定压出口 (Pressure Outflow)")
    };
    int cur = std::clamp(static_cast<int>(inoutType), 0, 3);
    if (ImGui::Combo(label, &cur, kItems, 4)) {
        inoutType = static_cast<LbmBoundaryType>(cur);
        return true;
    }
    return false;
}

ImU32 faceAxisColor(LbmDomainFace face, uint8_t alpha) noexcept {
    switch (face) {
        case LbmDomainFace::NegX:
        case LbmDomainFace::PosX:
            return IM_COL32(242, 88, 82, alpha);
        case LbmDomainFace::NegY:
        case LbmDomainFace::PosY:
            return IM_COL32(78, 214, 118, alpha);
        case LbmDomainFace::NegZ:
        case LbmDomainFace::PosZ:
            return IM_COL32(84, 162, 250, alpha);
    }
    return IM_COL32(220, 220, 220, alpha);
}

const char* faceShortLabel(LbmDomainFace face) noexcept {
    switch (face) {
        case LbmDomainFace::NegX: return "-X";
        case LbmDomainFace::PosX: return "+X";
        case LbmDomainFace::NegY: return "-Y";
        case LbmDomainFace::PosY: return "+Y";
        case LbmDomainFace::NegZ: return "-Z";
        case LbmDomainFace::PosZ: return "+Z";
    }
    return "";
}

} // namespace

HomeLbmEditorUi::HomeLbmEditorUi(HomeLbmSystem* system) noexcept
    : m_system(system) {}

void HomeLbmEditorUi::render() {
    if (!m_system || !ImGui::GetCurrentContext()) {
        return;
    }

    const HomeLbmUiSnapshot& snap = m_system->currentUiSnapshot();
    LbmDomainComponent workingDomain = snap.domain;
    bool domainModified = false;
    HomeLbmUiPendingCommand cmd{};

    renderWorkbenchWindow(snap, workingDomain, domainModified, cmd);
    renderSceneViewportOverlay(snap, workingDomain, domainModified, cmd);

    if (domainModified && snap.hasDomain) {
        cmd.hasDomainPatch = true;
        cmd.patchedDomain = workingDomain;
    }
    if (cmd.hasAnyAction()) {
        m_system->submitUiCommand(std::move(cmd));
    }
}

void HomeLbmEditorUi::renderWorkbenchWindow(
    const HomeLbmUiSnapshot& snap,
    LbmDomainComponent& domain,
    bool& outDomainModified,
    HomeLbmUiPendingCommand& inoutCmd) {
    if (!ImGui::Begin(LBM_TEXT("HOME-LBM 自适应流体工作台"))) {
        ImGui::End();
        return;
    }

    renderTransportAndCreationHeader(snap, domain, outDomainModified, inoutCmd);
    ImGui::Separator();

    if (!snap.hasDomain) {
        ImGui::Spacing();
        ImGui::TextWrapped(LBM_TEXT(
            "当前场景尚未放置流体模拟域。可点击上方【UE5 拉框创建流体域】在场景视口中拖拽拉出任意三维空间盒子，或点击【一键生成默认模拟域】。"));
        if (ImGui::Button(LBM_TEXT("一键生成默认模拟域 (风洞卡门涡街)"), ImVec2(-1.0f, 32.0f))) {
            inoutCmd.createDefaultDomain = true;
        }
        ImGui::End();
        return;
    }

    renderPresetAndBackendSection(snap, domain, inoutCmd);
    renderSpatialGeometrySection(domain, outDomainModified);
    renderPhysicsAndBoundarySection(domain, outDomainModified);
    renderTelemetrySection(snap, domain);

    ImGui::End();
}

void HomeLbmEditorUi::renderTransportAndCreationHeader(
    const HomeLbmUiSnapshot& snap,
    LbmDomainComponent& domain,
    bool& outDomainModified,
    HomeLbmUiPendingCommand& inoutCmd) {
    auto& tool = m_system->cubeTool();
    const bool creating = tool.isCreationActive();

    if (creating) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.56f, 0.78f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.88f, 1.0f));
    }
    const char* createBtnText = creating
        ? LBM_TEXT("取消 UE5 空间拉框建盒 (ESC / 右键)")
        : LBM_TEXT("UE5 拉框创建 / 重塑空间流体域");
    if (ImGui::Button(createBtnText, ImVec2(-1.0f, 30.0f))) {
        tool.setCreationModeEnabled(!creating, 0.0f);
    }
    if (creating) {
        ImGui::PopStyleColor(2);
        switch (tool.state()) {
            case HomeLbmCubeTool::State::WaitingBaseStart:
                ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
                    LBM_TEXT("步骤 1/2: 在【场景视口】内按住左键拖拽底面矩形"));
                break;
            case HomeLbmCubeTool::State::DrawingBaseRect:
                ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
                    LBM_TEXT("步骤 1/2: 拖动鼠标调整底面长宽，松开左键进入高度拉伸"));
                break;
            case HomeLbmCubeTool::State::ExtrudingHeight:
                ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.62f, 1.0f),
                    LBM_TEXT("步骤 2/2: 上下移动鼠标拉伸空间高度，点击左键完成建模"));
                break;
            default:
                break;
        }
    }

    if (snap.hasDomain) {
        if (ImGui::Button(domain.paused ? LBM_TEXT("继续模拟") : LBM_TEXT("暂停模拟"))) {
            domain.paused = !domain.paused;
            outDomainModified = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(LBM_TEXT("单步推进"))) {
            domain.paused = true;
            domain.stepOnce = true;
            outDomainModified = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(LBM_TEXT("重置流场"))) {
            domain.resetRequested = true;
            outDomainModified = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(LBM_TEXT("守恒诊断"))) {
            inoutCmd.requestDiagnostics = true;
        }
    }

    ImGui::TextDisabled("%s", snap.statusMessage.c_str());
}

void HomeLbmEditorUi::renderPresetAndBackendSection(
    const HomeLbmUiSnapshot& snap,
    LbmDomainComponent& domain,
    HomeLbmUiPendingCommand& inoutCmd) {
    if (ImGui::CollapsingHeader(LBM_TEXT("实验场景预设 & LuisaCompute 计算后端"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(LBM_TEXT("风洞卡门涡街"))) {
            inoutCmd.hasPresetRequest = true;
            inoutCmd.requestedPreset = LbmPresetScenario::WindTunnelKarmanVortex;
        }
        ImGui::SameLine();
        if (ImGui::Button(LBM_TEXT("三维溃坝自由液面"))) {
            inoutCmd.hasPresetRequest = true;
            inoutCmd.requestedPreset = LbmPresetScenario::FreeSurfaceDamBreak3D;
        }
        ImGui::SameLine();
        if (ImGui::Button(LBM_TEXT("热浮力烟羽"))) {
            inoutCmd.hasPresetRequest = true;
            inoutCmd.requestedPreset = LbmPresetScenario::ThermalBuoyantPlume;
        }

        ImGui::Text(LBM_TEXT("硬件计算后端 (实时热迁移保留流场):"));
        auto backendRadio = [&](const char* label, LbmComputeBackendMode mode, bool avail) {
            if (!avail) {
                ImGui::BeginDisabled();
            }
            const bool selected = (domain.backendMode == mode);
            if (ImGui::RadioButton(label, selected) && avail && !selected) {
                inoutCmd.hasBackendSwitchRequest = true;
                inoutCmd.requestedBackendMode = mode;
            }
            if (!avail) {
                ImGui::EndDisabled();
            }
        };
        backendRadio(LBM_TEXT("DX12 (RHI 零拷贝)"), LbmComputeBackendMode::MatchRHI_DX12, snap.dxAvailable);
        ImGui::SameLine();
        backendRadio("CUDA", LbmComputeBackendMode::CUDA, snap.cudaAvailable);
        ImGui::SameLine();
        backendRadio("Vulkan", LbmComputeBackendMode::Vulkan, snap.vkAvailable);

        if (snap.solverInitialized && !snap.activeBackendName.empty()) {
            ImGui::TextColored(
                snap.sharingRhiDx12
                    ? ImVec4(0.38f, 0.94f, 0.56f, 1.0f)
                    : ImVec4(0.95f, 0.78f, 0.32f, 1.0f),
                LBM_TEXT("活跃设备: %s"),
                snap.activeBackendName.c_str());
        }
    }
}

void HomeLbmEditorUi::renderSpatialGeometrySection(
    LbmDomainComponent& domain,
    bool& outDomainModified) {
    if (ImGui::CollapsingHeader(LBM_TEXT("三维空间模拟盒子 & 块结构 AMR 网格"), ImGuiTreeNodeFlags_DefaultOpen)) {
        outDomainModified |= ImGui::Checkbox(LBM_TEXT("启用流体模拟"), &domain.enabled);
        ImGui::SameLine();
        outDomainModified |= ImGui::Checkbox(LBM_TEXT("显示域线框与六面手柄"), &domain.showDomainWireframe);
        ImGui::SameLine();
        outDomainModified |= ImGui::Checkbox(LBM_TEXT("显示 8^3 AMR 宏块"), &domain.showAmrBlocks);

        outDomainModified |= ImGui::Checkbox(LBM_TEXT("自动体素化盒内场景碰撞体 (Box/Sphere/Capsule)"), &domain.autoVoxelizeScene);

        bool boundsEdited = false;
        boundsEdited |= ImGui::DragFloat3(LBM_TEXT("空间下界 Min (m)"), &domain.minBounds.x, 0.05f);
        boundsEdited |= ImGui::DragFloat3(LBM_TEXT("空间上界 Max (m)"), &domain.maxBounds.x, 0.05f);
        if (boundsEdited) {
            domain.maxBounds.x = std::max(domain.minBounds.x + 0.25f, domain.maxBounds.x);
            domain.maxBounds.y = std::max(domain.minBounds.y + 0.25f, domain.maxBounds.y);
            domain.maxBounds.z = std::max(domain.minBounds.z + 0.25f, domain.maxBounds.z);
            domain.resetRequested = true;
            outDomainModified = true;
        }

        const FCT::Vec3<> span = domain.maxBounds - domain.minBounds;
        ImGui::TextDisabled(LBM_TEXT("当前空间尺寸: %.2f x %.2f x %.2f m (可在视口拖拽 6 面手柄实时推拉)"),
            span.x, span.y, span.z);

        int blocks[3] = {domain.baseBlocksX, domain.baseBlocksY, domain.baseBlocksZ};
        if (ImGui::SliderInt3(LBM_TEXT("L0 基础 8^3 宏块数"), blocks, 2, 16)) {
            domain.baseBlocksX = blocks[0];
            domain.baseBlocksY = blocks[1];
            domain.baseBlocksZ = blocks[2];
            domain.resetRequested = true;
            outDomainModified = true;
        }
        if (ImGui::SliderInt(LBM_TEXT("最大自适应层级 (L0..L2)"), &domain.maxAmrLevels, 1, 3)) {
            domain.resetRequested = true;
            outDomainModified = true;
        }
        outDomainModified |= ImGui::SliderInt(LBM_TEXT("动态重网格化间隔步数"), &domain.amrRemeshInterval, 2, 32);
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("涡量加密阈值 |w|_refine"), &domain.vorticityRefineThreshold, 0.002f, 0.12f, "%.4f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("涡量粗化阈值 |w|_coarsen"), &domain.vorticityCoarsenThreshold, 0.001f, 0.06f, "%.4f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("障碍物加密保护半径 (格)"), &domain.obstacleRefineDistance, 0.5f, 4.0f, "%.2f");
    }
}

void HomeLbmEditorUi::renderPhysicsAndBoundarySection(
    LbmDomainComponent& domain,
    bool& outDomainModified) {
    if (ImGui::CollapsingHeader(LBM_TEXT("流体介质参数、边界条件 & 体积渲染"), ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* const kSimModes[] = {
            LBM_TEXT("气烟湍流风洞 (HOME-LBM D3Q27)"),
            LBM_TEXT("自由液面流体 (HOME-FREE VOF)")
        };
        int curSim = static_cast<int>(domain.simMode);
        if (ImGui::Combo(LBM_TEXT("物理求解模式"), &curSim, kSimModes, 2)) {
            domain.simMode = static_cast<LbmSimulationMode>(curSim);
            domain.visMode = (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid)
                ? LbmVisualizationMode::FreeSurfaceWater
                : LbmVisualizationMode::VorticityMagnitude;
            domain.resetRequested = true;
            outDomainModified = true;
        }

        static const char* const kVisModes[] = {
            LBM_TEXT("被动烟雾浓度 (Smoke)"),
            LBM_TEXT("涡量幅值 |curl u|"),
            LBM_TEXT("Q 判据相干涡结构"),
            LBM_TEXT("速度幅值 |u|"),
            LBM_TEXT("自由液面水体等值面 (phi=0.5)")
        };
        int curVis = std::clamp(static_cast<int>(domain.visMode), 0, 4);
        if (ImGui::Combo(LBM_TEXT("三维渲染通道"), &curVis, kVisModes, 5)) {
            domain.visMode = static_cast<LbmVisualizationMode>(curVis);
            outDomainModified = true;
        }

        outDomainModified |= ImGui::SliderInt(LBM_TEXT("每帧宏时间步数"), &domain.substepsPerFrame, 1, 8);
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("格子运动粘度 nu"), &domain.kinematicViscosity, 0.0005f, 0.025f, "%.5f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("Smagorinsky 亚格子系数 Cs"), &domain.smagorinskyCs, 0.0f, 0.22f, "%.3f");
        outDomainModified |= ImGui::DragFloat3(LBM_TEXT("入口格子速度 u_in"), &domain.inflowVelocity.x, 0.002f, -0.20f, 0.20f, "%.4f");
        outDomainModified |= ImGui::DragFloat3(LBM_TEXT("体积重力加速度 g"), &domain.gravity.x, 0.0001f, -0.01f, 0.01f, "%.5f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("热浮力系数 alpha"), &domain.buoyancyAlpha, 0.0f, 0.01f, "%.4f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("体积光学密度倍率"), &domain.densityScale, 0.5f, 8.0f, "%.2f");
        outDomainModified |= ImGui::SliderFloat(LBM_TEXT("光线步进分辨率比例"), &domain.renderResolutionScale, 0.25f, 1.0f, "%.2f");

        if (ImGui::TreeNode(LBM_TEXT("六面独立边界条件 (-X/+X/-Y/+Y/-Z/+Z)"))) {
            outDomainModified |= drawBoundaryCombo("-X (左面)", domain.negXBoundary);
            outDomainModified |= drawBoundaryCombo("+X (右面)", domain.posXBoundary);
            outDomainModified |= drawBoundaryCombo("-Y (底面)", domain.negYBoundary);
            outDomainModified |= drawBoundaryCombo("+Y (顶面)", domain.posYBoundary);
            outDomainModified |= drawBoundaryCombo("-Z (前面)", domain.negZBoundary);
            outDomainModified |= drawBoundaryCombo("+Z (后面)", domain.posZBoundary);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode(LBM_TEXT("内置标定障碍物与初始液柱配置"))) {
            outDomainModified |= ImGui::Checkbox(LBM_TEXT("启用内置测试障碍物"), &domain.enableBuiltinObstacle);
            static const char* const kShapes[] = {
                LBM_TEXT("球体 (Sphere)"),
                LBM_TEXT("方柱 (Box)"),
                LBM_TEXT("立式胶囊柱 (CapsuleY)")
            };
            int curShape = std::clamp(static_cast<int>(domain.builtinObstacleShape), 0, 2);
            if (ImGui::Combo(LBM_TEXT("内置障碍物几何"), &curShape, kShapes, 3)) {
                domain.builtinObstacleShape = static_cast<LbmPrimitiveShape>(curShape);
                outDomainModified = true;
            }
            outDomainModified |= ImGui::SliderFloat3(LBM_TEXT("障碍物归一化坐标"), &domain.builtinObstacleNormalizedPos.x, 0.05f, 0.95f);
            outDomainModified |= ImGui::SliderFloat(LBM_TEXT("障碍物相对半径比"), &domain.builtinObstacleRadiusRatio, 0.04f, 0.28f);

            if (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
                bool liqChanged = false;
                liqChanged |= ImGui::SliderFloat3(LBM_TEXT("初始液柱下界比例"), &domain.initialLiquidMinRatio.x, 0.0f, 0.95f);
                liqChanged |= ImGui::SliderFloat3(LBM_TEXT("初始液柱上界比例"), &domain.initialLiquidMaxRatio.x, 0.05f, 1.0f);
                if (liqChanged) {
                    domain.resetRequested = true;
                    outDomainModified = true;
                }
            }
            ImGui::TreePop();
        }
    }
}

void HomeLbmEditorUi::renderTelemetrySection(
    const HomeLbmUiSnapshot& snap,
    const LbmDomainComponent& domain) {
    if (ImGui::CollapsingHeader(LBM_TEXT("实时 AMR 统计与物理守恒遥测"), ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& t = snap.telemetry;
        ImGui::Text(LBM_TEXT("累计宏时间步: %d | 活跃 8^3 宏块: %d (L0:%d, L1:%d, L2:%d)"),
            t.stepCount, t.totalActiveBlocks, t.activeBlocksL0, t.activeBlocksL1, t.activeBlocksL2);
        ImGui::Text(LBM_TEXT("活跃格子总数: %d | 等效最细分辨率: %d x %d x %d"),
            t.totalActiveCells, t.finestResolutionX, t.finestResolutionY, t.finestResolutionZ);
        ImGui::Text(LBM_TEXT("最大格子速度 |u|_max: %.4f | 峰值涡量 |w|_max: %.4f | 数值有限: %s"),
            t.maxSpeed, t.maxVorticity, t.allFinite ? "PASS" : "NaN/Inf!");

        if (domain.simMode == LbmSimulationMode::FreeSurfaceLiquid) {
            ImGui::Text(LBM_TEXT("自由液面总质量: %.3f (初始: %.3f, 漂移: %.3f%%)"),
                t.totalLiquidMass, t.initialLiquidMass, t.liquidMassDriftPercent);
            ImGui::Text(LBM_TEXT("液相质心坐标: (%.3f, %.3f, %.3f)"),
                t.liquidCenterOfMass.x, t.liquidCenterOfMass.y, t.liquidCenterOfMass.z);
        }
    }
}

void HomeLbmEditorUi::renderSceneViewportOverlay(
    const HomeLbmUiSnapshot& snap,
    LbmDomainComponent& domain,
    bool& outDomainModified,
    HomeLbmUiPendingCommand& inoutCmd) {
    ImGuiWindow* vpWin = ImGui::FindWindowByName(LBM_TEXT("场景视口"));
    if (!vpWin || vpWin->Hidden || vpWin->Collapsed || !vpWin->WasActive) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float toolbarHeight = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f + 1.0f;
    const ImVec2 contentMin = vpWin->ContentRegionRect.Min;
    const ImVec2 contentMax = vpWin->ContentRegionRect.Max;
    const float regionTopY = contentMin.y + toolbarHeight;
    const float availW = std::max(64.0f, contentMax.x - contentMin.x);
    const float availH = std::max(64.0f, (contentMax.y - regionTopY) - 24.0f);

    float targetAspect = 16.0f / 9.0f;
    if (Runtime::Engine::getInstance() && Runtime::Engine::getInstance()->platform && Runtime::Engine::getInstance()->platform->getWindow()) {
        const float ww = static_cast<float>(std::max(1, Runtime::Engine::getInstance()->platform->getWindow()->getWidth()));
        const float wh = static_cast<float>(std::max(1, Runtime::Engine::getInstance()->platform->getWindow()->getHeight()));
        targetAspect = ww / wh;
    }

    float imgW = availW;
    float imgH = imgW / targetAspect;
    if (imgH > availH) {
        imgH = availH;
        imgW = imgH * targetAspect;
    }

    const FCT::Vec2 vpMin(
        contentMin.x + (availW - imgW) * 0.5f,
        regionTopY + (availH - imgH) * 0.5f);
    const FCT::Vec2 vpSize(imgW, imgH);

    HomeLbmCameraView cam = snap.cameraView;
    cam.aspectRatio = targetAspect;

    if (!ImGui::Begin(LBM_TEXT("场景视口"))) {
        ImGui::End();
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(
        ImVec2(vpMin.x, vpMin.y),
        ImVec2(vpMin.x + vpSize.x, vpMin.y + vpSize.y),
        true);

    const ImVec2 mouseIm = ImGui::GetIO().MousePos;
    const bool mouseInside =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        mouseIm.x >= vpMin.x && mouseIm.x <= (vpMin.x + vpSize.x) &&
        mouseIm.y >= vpMin.y && mouseIm.y <= (vpMin.y + vpSize.y);

    if (snap.hasDomain) {
        if (domain.showAmrBlocks && snap.solverInitialized) {
            for (const auto& box : snap.amrWireframeBoxes) {
                ImU32 col = IM_COL32(70, 175, 220, 42);
                float thick = 1.0f;
                if (box.level == 1) {
                    col = IM_COL32(245, 190, 66, 85);
                    thick = 1.15f;
                } else if (box.level >= 2) {
                    col = IM_COL32(250, 96, 168, 130);
                    thick = 1.35f;
                }
                drawProjectedBoxWireframe(
                    drawList, box.minWorld, box.maxWorld, vpMin, vpSize, cam, col, thick);
            }
        }

        if (domain.showDomainWireframe) {
            drawProjectedBoxWireframe(
                drawList,
                domain.minBounds,
                domain.maxBounds,
                vpMin,
                vpSize,
                cam,
                IM_COL32(64, 224, 255, 225),
                2.2f);

            const FCT::Vec3<> topCenter(
                0.5f * (domain.minBounds.x + domain.maxBounds.x),
                domain.maxBounds.y,
                0.5f * (domain.minBounds.z + domain.maxBounds.z));
            FCT::Vec2 labelScreen{};
            if (HomeLbmCubeTool::projectWorldToScreen(
                    topCenter, vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees, labelScreen)) {
                const FCT::Vec3<> span = domain.maxBounds - domain.minBounds;
                char dimBuf[96];
                std::snprintf(dimBuf, sizeof(dimBuf), "HOME-LBM [%.2f x %.2f x %.2f m]", span.x, span.y, span.z);
                drawList->AddText(
                    ImVec2(labelScreen.x - 75.0f, labelScreen.y - 22.0f),
                    IM_COL32(150, 240, 255, 245),
                    dimBuf);
            }

            if (!m_system->cubeTool().isCreationActive()) {
                drawDomainFaceHandlesAndInteract(
                    drawList, domain, outDomainModified, inoutCmd, vpMin, vpSize, cam, mouseInside);
            }
        }
    }

    if (m_system->cubeTool().isCreationActive()) {
        handleTwoStageBoxCreation(drawList, inoutCmd, vpMin, vpSize, cam, mouseInside);
    }

    drawList->PopClipRect();
    ImGui::End();
}

void HomeLbmEditorUi::drawProjectedBoxWireframe(
    ImDrawList* drawList,
    const FCT::Vec3<>& boxMin,
    const FCT::Vec3<>& boxMax,
    const FCT::Vec2& vpMin,
    const FCT::Vec2& vpSize,
    const HomeLbmCameraView& cam,
    ImU32 color,
    float thickness) const {
    const std::array<FCT::Vec3<>, 8> corners = {{
        {boxMin.x, boxMin.y, boxMin.z},
        {boxMax.x, boxMin.y, boxMin.z},
        {boxMax.x, boxMax.y, boxMin.z},
        {boxMin.x, boxMax.y, boxMin.z},
        {boxMin.x, boxMin.y, boxMax.z},
        {boxMax.x, boxMin.y, boxMax.z},
        {boxMax.x, boxMax.y, boxMax.z},
        {boxMin.x, boxMax.y, boxMax.z}
    }};

    std::array<FCT::Vec2, 8> pts{};
    std::array<bool, 8> vis{};
    for (size_t i = 0; i < 8; ++i) {
        vis[i] = HomeLbmCubeTool::projectWorldToScreen(
            corners[i], vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees, pts[i]);
    }

    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (const auto& edge : kEdges) {
        const int a = edge[0];
        const int b = edge[1];
        if (vis[a] && vis[b]) {
            drawList->AddLine(
                ImVec2(pts[a].x, pts[a].y),
                ImVec2(pts[b].x, pts[b].y),
                color,
                thickness);
        }
    }
}

void HomeLbmEditorUi::drawDomainFaceHandlesAndInteract(
    ImDrawList* drawList,
    LbmDomainComponent& domain,
    bool& outDomainModified,
    HomeLbmUiPendingCommand& inoutCmd,
    const FCT::Vec2& vpMin,
    const FCT::Vec2& vpSize,
    const HomeLbmCameraView& cam,
    bool mouseInsideViewport) {
    auto& tool = m_system->cubeTool();
    const ImGuiIO& io = ImGui::GetIO();
    const FCT::Vec2 mousePos(io.MousePos.x, io.MousePos.y);
    const LbmRay3D ray = HomeLbmCubeTool::buildCameraRay(
        mousePos, vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees);

    if (tool.isDraggingFace()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            FCT::Vec3<> newMin = domain.minBounds;
            FCT::Vec3<> newMax = domain.maxBounds;
            if (tool.updateFaceDrag(ray, newMin, newMax)) {
                domain.minBounds = newMin;
                domain.maxBounds = newMax;
                outDomainModified = true;
            }
        } else {
            tool.endFaceDrag();
            domain.resetRequested = true;
            outDomainModified = true;
            inoutCmd.hasStatusMessage = true;
            inoutCmd.statusMessage = "已完成六面推拉调整，流场网格已同步重构";
        }
    } else if (mouseInsideViewport && !io.KeyAlt) {
        m_hoveredFace = tool.pickFaceHandle(
            mousePos, 15.0f, domain.minBounds, domain.maxBounds,
            vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees);
        if (m_hoveredFace.has_value() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            tool.beginFaceDrag(*m_hoveredFace, ray, domain.minBounds, domain.maxBounds);
        }
    } else {
        m_hoveredFace.reset();
    }

    const std::optional<LbmDomainFace> highlightFace =
        tool.isDraggingFace() ? tool.activeFace() : m_hoveredFace;

    if (highlightFace.has_value()) {
        std::array<FCT::Vec3<>, 4> quadWorld{};
        HomeLbmCubeTool::faceQuadCorners(domain.minBounds, domain.maxBounds, *highlightFace, quadWorld);
        std::array<ImVec2, 4> quadScreen{};
        bool allVis = true;
        for (size_t i = 0; i < 4; ++i) {
            FCT::Vec2 sp{};
            if (!HomeLbmCubeTool::projectWorldToScreen(
                    quadWorld[i], vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees, sp)) {
                allVis = false;
                break;
            }
            quadScreen[i] = ImVec2(sp.x, sp.y);
        }
        if (allVis) {
            drawList->AddQuadFilled(
                quadScreen[0], quadScreen[1], quadScreen[2], quadScreen[3],
                faceAxisColor(*highlightFace, 48));
            drawList->AddQuad(
                quadScreen[0], quadScreen[1], quadScreen[2], quadScreen[3],
                faceAxisColor(*highlightFace, 220), 2.5f);
        }
    }

    static constexpr std::array<LbmDomainFace, 6> kAllFaces = {
        LbmDomainFace::NegX, LbmDomainFace::PosX,
        LbmDomainFace::NegY, LbmDomainFace::PosY,
        LbmDomainFace::NegZ, LbmDomainFace::PosZ
    };
    const float arrowLen = std::max(0.25f, (domain.maxBounds - domain.minBounds).length() * 0.06f);
    for (LbmDomainFace f : kAllFaces) {
        const FCT::Vec3<> center = HomeLbmCubeTool::faceCenter(domain.minBounds, domain.maxBounds, f);
        const FCT::Vec3<> tip = center + HomeLbmCubeTool::faceNormal(f) * arrowLen;
        FCT::Vec2 spCenter{}, spTip{};
        if (!HomeLbmCubeTool::projectWorldToScreen(
                center, vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees, spCenter)) {
            continue;
        }
        const bool isActive = (highlightFace.has_value() && *highlightFace == f);
        const float radius = isActive ? 9.0f : 6.5f;
        if (HomeLbmCubeTool::projectWorldToScreen(
                tip, vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees, spTip)) {
            drawList->AddLine(
                ImVec2(spCenter.x, spCenter.y),
                ImVec2(spTip.x, spTip.y),
                faceAxisColor(f, isActive ? 255 : 185),
                isActive ? 2.8f : 1.8f);
        }
        drawList->AddCircleFilled(
            ImVec2(spCenter.x, spCenter.y),
            radius,
            faceAxisColor(f, isActive ? 255 : 215));
        drawList->AddCircle(
            ImVec2(spCenter.x, spCenter.y),
            radius + 1.5f,
            IM_COL32(255, 255, 255, isActive ? 245 : 130),
            16,
            1.5f);
        drawList->AddText(
            ImVec2(spCenter.x + 10.0f, spCenter.y - 8.0f),
            IM_COL32(240, 245, 250, 225),
            faceShortLabel(f));
    }
}

void HomeLbmEditorUi::handleTwoStageBoxCreation(
    ImDrawList* drawList,
    HomeLbmUiPendingCommand& inoutCmd,
    const FCT::Vec2& vpMin,
    const FCT::Vec2& vpSize,
    const HomeLbmCameraView& cam,
    bool mouseInsideViewport) {
    auto& tool = m_system->cubeTool();
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (mouseInsideViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        tool.cancelInteraction();
        inoutCmd.hasStatusMessage = true;
        inoutCmd.statusMessage = "已取消 UE5 空间拉框建盒";
        return;
    }

    const FCT::Vec2 mousePos(io.MousePos.x, io.MousePos.y);
    const LbmRay3D ray = HomeLbmCubeTool::buildCameraRay(
        mousePos, vpMin, vpSize, cam.position, cam.forward, cam.up, cam.fovDegrees);

    if (tool.state() == HomeLbmCubeTool::State::WaitingBaseStart) {
        if (mouseInsideViewport && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            FCT::Vec3<> hitPt{};
            const bool hasHit = m_system->raycastSnapshotColliders(ray, hitPt);
            tool.beginBaseDrag(ray, hasHit ? &hitPt : nullptr);
        }
        return;
    }

    if (tool.state() == HomeLbmCubeTool::State::DrawingBaseRect) {
        tool.updateCreationPointer(ray);
        FCT::Vec3<> pMin{}, pMax{};
        tool.currentPreviewBounds(pMin, pMax);
        pMax.y = pMin.y + 0.03f;
        drawProjectedBoxWireframe(
            drawList, pMin, pMax, vpMin, vpSize, cam, IM_COL32(72, 245, 255, 245), 2.5f);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            tool.finishBaseDragAndBeginExtrude(ray);
        }
        return;
    }

    if (tool.state() == HomeLbmCubeTool::State::ExtrudingHeight) {
        tool.updateCreationPointer(ray);
        FCT::Vec3<> pMin{}, pMax{};
        tool.currentPreviewBounds(pMin, pMax);
        drawProjectedBoxWireframe(
            drawList, pMin, pMax, vpMin, vpSize, cam, IM_COL32(95, 255, 158, 245), 2.6f);

        if (mouseInsideViewport && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            FCT::Vec3<> finalMin{}, finalMax{};
            if (tool.commitExtrusion(ray, finalMin, finalMax)) {
                inoutCmd.hasBoxCommit = true;
                inoutCmd.committedMinBounds = finalMin;
                inoutCmd.committedMaxBounds = finalMax;
            }
        }
    }
}

} // namespace MQEngine
