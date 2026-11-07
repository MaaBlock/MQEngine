#ifndef EDITOR_SCENE_ENTITY_UI_MODULE_H
#define EDITOR_SCENE_ENTITY_UI_MODULE_H

#include "IUiModule.h"
#include "../imgui/GenericInputPopup.h"

namespace MQEngine {

    /**
     * @brief 场景实体查看器UI模块，支持层级树展示、搜索过滤、预设实体创建、父子拖拽编排与资源拖放绑定
     */
    class SceneEntityUiModule : public IUiModule {
    public:
        SceneEntityUiModule() = default;
        ~SceneEntityUiModule() override = default;

        std::string_view getModuleName() const override {
            return "SceneEntityUiModule";
        }

        void onRender(UiContext& ctx) override;

    private:
        void renderSceneHeader(UiContext& ctx, const SceneEntityViewState& state);
        void renderPresetCreateMenu(UiContext& ctx,
                                    const std::string& trunkName,
                                    bool isGlobal,
                                    entt::entity parentEntity = entt::null);
        void renderGlobalEntitySection(UiContext& ctx, const SceneEntityViewState& state);
        void renderTrunkEntitySection(UiContext& ctx, const TrunkEntityGroupView& trunkGroup);
        void renderEntityItemList(UiContext& ctx,
                                  const std::vector<EntityItemView>& entities,
                                  const std::string& trunkName,
                                  bool isGlobal);
        void handleSectionBackgroundDropTarget(UiContext& ctx,
                                               const std::string& trunkName,
                                               bool isGlobal);
        void handleEntityDragDropTarget(UiContext& ctx,
                                        entt::entity entity,
                                        const std::string& trunkName,
                                        bool isGlobal);
        void openScriptBindingModal(entt::entity entity,
                                    const std::string& scriptName,
                                    bool isGlobal,
                                    const std::string& trunkName,
                                    bool isClass);
        void openTextureBindingModal(entt::entity entity,
                                     const std::string& modelUuid,
                                     const std::string& texturePath,
                                     bool isGlobal,
                                     const std::string& trunkName);
        void renderScriptBindingModal(UiContext& ctx);
        void renderTextureBindingModal(UiContext& ctx);
        void emitScriptBindAndClose(UiContext& ctx, ScriptBindMode mode);
        void emitTextureBindAndClose(UiContext& ctx, TextureBindSlot slot);

    private:
        GenericInputPopup m_inputPopup;
        char m_searchFilter[128] = { 0 };

        bool m_showScriptTypePopup = false;
        std::string m_draggedScriptName;
        bool m_isDraggedScriptClass = false;
        entt::entity m_targetEntity = entt::null;
        std::string m_targetTrunkName;
        bool m_targetIsGlobal = false;

        bool m_showTextureTypePopup = false;
        std::string m_draggedTexturePath;
        std::string m_draggedModelUuid;
    };

} // namespace MQEngine

#endif // EDITOR_SCENE_ENTITY_UI_MODULE_H
