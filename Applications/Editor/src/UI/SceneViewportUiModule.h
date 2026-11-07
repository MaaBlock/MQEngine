#ifndef EDITOR_SCENE_VIEWPORT_UI_MODULE_H
#define EDITOR_SCENE_VIEWPORT_UI_MODULE_H

#include "IUiModule.h"
#include "../viewport/ViewportInteractionManager.h"

namespace MQEngine {

    /**
     * @brief 场景视口UI模块，负责展示离屏渲染画面、处理视口相机交互控制、3D Gizmo 平移与资源拖拽实例化
     */
    class SceneViewportUiModule : public IUiModule {
    public:
        SceneViewportUiModule() = default;
        ~SceneViewportUiModule() override = default;

        std::string_view getModuleName() const override {
            return "SceneViewportUiModule";
        }

        void onRender(UiContext& ctx) override;

    private:
        void renderViewportToolbar(UiContext& ctx, const ViewportViewState& viewState);
        void renderViewportImage(UiContext& ctx, const ViewportViewState& viewState, bool isWindowFocused);
        void handleViewportDragDrop(UiContext& ctx, const ViewportViewState& viewState);
        void renderMissingTextureHint();

    private:
        ViewportInteractionManager m_interactionManager;
    };

} // namespace MQEngine

#endif // EDITOR_SCENE_VIEWPORT_UI_MODULE_H
