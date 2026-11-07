#ifndef EDITOR_CAMERA_CONTROL_UI_MODULE_H
#define EDITOR_CAMERA_CONTROL_UI_MODULE_H

#include "IUiModule.h"

namespace MQEngine {

    /**
     * @brief 相机控制面板UI模块，支持编辑器/场景相机切换、移动速度/灵敏度调节及视角同步
     */
    class CameraControlUiModule : public IUiModule {
    public:
        CameraControlUiModule() = default;
        ~CameraControlUiModule() override = default;

        std::string_view getModuleName() const override {
            return "CameraControlUiModule";
        }

        void onRender(UiContext& ctx) override;

    private:
        void renderEditorCameraOption(UiContext& ctx, const CameraControlViewState& state);
        void renderEditorCameraInspector(UiContext& ctx, const CameraControlViewState& state);
        void renderQuickActionButtons(UiContext& ctx, const CameraControlViewState& state);
        void renderSceneCameraList(UiContext& ctx, const CameraControlViewState& state);
        void renderActiveCameraSummary(const CameraControlViewState& state);
    };

} // namespace MQEngine

#endif // EDITOR_CAMERA_CONTROL_UI_MODULE_H
