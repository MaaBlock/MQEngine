#ifndef MQENGINE_HOMELBM_EDITOR_UI_H
#define MQENGINE_HOMELBM_EDITOR_UI_H

#include "thirdparty/thirdparty.h"
#include "HomeLbmSystem.h"

namespace MQEngine {

/**
 * @brief 自适应 HOME-LBM 编辑器工作台与场景视口三维交互叠加层（UE5 两段式拉框建盒 + 六面推拉手柄 + AMR 网格可视化）
 */
class HOME_LBM_API HomeLbmEditorUi {
public:
    explicit HomeLbmEditorUi(HomeLbmSystem* system) noexcept;
    ~HomeLbmEditorUi() = default;

    void render();

    static const char* workbenchWindowTitle() noexcept {
        return reinterpret_cast<const char*>(u8"HOME-LBM 自适应流体工作台");
    }

private:
    void renderWorkbenchWindow(
        const HomeLbmUiSnapshot& snap,
        LbmDomainComponent& domain,
        bool& outDomainModified,
        HomeLbmUiPendingCommand& inoutCmd);
    void renderTransportAndCreationHeader(
        const HomeLbmUiSnapshot& snap,
        LbmDomainComponent& domain,
        bool& outDomainModified,
        HomeLbmUiPendingCommand& inoutCmd);
    void renderPresetAndBackendSection(
        const HomeLbmUiSnapshot& snap,
        LbmDomainComponent& domain,
        HomeLbmUiPendingCommand& inoutCmd);
    void renderSpatialGeometrySection(
        LbmDomainComponent& domain,
        bool& outDomainModified);
    void renderPhysicsAndBoundarySection(
        LbmDomainComponent& domain,
        bool& outDomainModified);
    void renderTelemetrySection(
        const HomeLbmUiSnapshot& snap,
        const LbmDomainComponent& domain);

    void renderSceneViewportOverlay(
        const HomeLbmUiSnapshot& snap,
        LbmDomainComponent& domain,
        bool& outDomainModified,
        HomeLbmUiPendingCommand& inoutCmd);
    void drawProjectedBoxWireframe(
        ImDrawList* drawList,
        const FCT::Vec3<>& boxMin,
        const FCT::Vec3<>& boxMax,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        const HomeLbmCameraView& cam,
        ImU32 color,
        float thickness) const;
    void drawDomainFaceHandlesAndInteract(
        ImDrawList* drawList,
        LbmDomainComponent& domain,
        bool& outDomainModified,
        HomeLbmUiPendingCommand& inoutCmd,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        const HomeLbmCameraView& cam,
        bool mouseInsideViewport);
    void handleTwoStageBoxCreation(
        ImDrawList* drawList,
        HomeLbmUiPendingCommand& inoutCmd,
        const FCT::Vec2& vpMin,
        const FCT::Vec2& vpSize,
        const HomeLbmCameraView& cam,
        bool mouseInsideViewport);

private:
    HomeLbmSystem* m_system = nullptr;
    std::optional<LbmDomainFace> m_hoveredFace;
};

} // namespace MQEngine

#endif // MQENGINE_HOMELBM_EDITOR_UI_H
