#ifndef EDITOR_ENTITY_INSPECTOR_UI_MODULE_H
#define EDITOR_ENTITY_INSPECTOR_UI_MODULE_H

#include "IUiModule.h"
#include "../imgui/GenericInputPopup.h"

namespace MQEngine {

    /**
     * @brief 实体检查器UI模块，利用编译期组件元组遍历绘制组件面板并发出增删改信号
     */
    class EntityInspectorUiModule : public IUiModule {
    public:
        EntityInspectorUiModule() = default;
        ~EntityInspectorUiModule() override = default;

        std::string_view getModuleName() const override {
            return "EntityInspectorUiModule";
        }

        void onRender(UiContext& ctx) override;

    private:
        void renderEntityDetails(UiContext& ctx, const EntityInspectorViewState& state);
        void renderAddComponentPopup(UiContext& ctx, const EntityInspectorViewState& state);
        void renderComponentList(UiContext& ctx, const EntityInspectorViewState& state);

        template <typename ComponentType>
        void renderAddComponentMenuItem(UiContext& ctx);

        template <typename ComponentType>
        bool tryRenderComponentWidget(UiContext& ctx,
                                      const entt::registry& registry,
                                      entt::entity entity);

        void renderReflectedAddMenuItems(UiContext& ctx, const EntityInspectorViewState& state);
        bool renderReflectedComponents(UiContext& ctx, const EntityInspectorViewState& state);
        static bool isBuiltInInspectorComponent(entt::id_type typeId);

    private:
        GenericInputPopup m_inputPopup;
    };

} // namespace MQEngine

#endif // EDITOR_ENTITY_INSPECTOR_UI_MODULE_H
