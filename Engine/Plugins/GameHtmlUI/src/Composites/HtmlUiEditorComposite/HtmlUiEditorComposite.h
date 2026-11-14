/**
 * @file HtmlUiEditorComposite.h
 * @brief GameHtmlUI 专属所见即所得可视化编辑器 Composite（tags: [Editor, ImGui]，独立编译为 HtmlUiEditorComposite.dll）
 */
#ifndef MQENGINE_GAME_HTML_UI_EDITOR_COMPOSITE_H
#define MQENGINE_GAME_HTML_UI_EDITOR_COMPOSITE_H

#include "../../GameHtmlUiApi.h"
#include "HtmlUiTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
        class IUndoableCommand;
    } // namespace EditorBase

    namespace Blueprint
    {
        class BlueprintEditorComposite;
    } // namespace Blueprint

    namespace GameHtmlUI
    {
        /**
         * @brief GameHtmlUI 专属所见即所得 UI 编辑器 Composite：
         *        支持 1280x720 设计视口自适应缩放预览、鼠标直接点选拖拽位移、八向缩放手柄、
         *        智能对齐参考线、控件库拖拽创建、DOM 树拖拽重排、盒模型编辑、撤销重做与保存实时热重载。
         */
        class HTML_UI_EDITOR_COMPOSITE_API HtmlUiEditorComposite : public Asset::Composite
        {
        public:
            static constexpr float kDesignCanvasWidth = 1280.0f;
            static constexpr float kDesignCanvasHeight = 720.0f;

            HtmlUiEditorComposite();
            ~HtmlUiEditorComposite() override;

            HtmlUiEditorComposite(const HtmlUiEditorComposite&) = delete;
            HtmlUiEditorComposite& operator=(const HtmlUiEditorComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "HtmlUiEditorComposite"; }
            std::string getPluginName() const override { return "GameHtmlUI"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            void regenerateRmlAndRcssFromElements();
            void parseRmlAndRcssToElements();
            void saveUiFilesToDisk();

            void applyElementsState(const std::vector<HtmlUiElement>& state);
            void pushCommand(const std::string& name, std::vector<HtmlUiElement> beforeState);
            void reparentElement(uint32_t childId, uint32_t newParentId);
            void bindElementEventToBlueprint(const HtmlUiElement& elem, const std::string& eventType = "click");

        private:
            std::string resolveUiDiskPath(const std::string& virtualOrDiskPath) const;
            std::string toProjectRelativeUiPath(const std::string& diskOrVirtualPath) const;
            void refreshAvailableUiFiles();
            bool loadUiDocumentPair(const std::string& rmlOrRcssPath);
            void ensureProjectUiDocumentLoaded();
            void notifyRuntimeHotReload();

            HtmlUiElement* findElementById(uint32_t id);
            const HtmlUiElement* findElementById(uint32_t id) const;
            void translateElementSubtree(uint32_t rootId, float dx, float dy);
            void deleteElementSubtree(uint32_t targetId);

            void renderHtmlUiEditorWindow();
            void renderEditorTopToolbar();
            void renderControlPalettePane();
            void renderWysiwygCanvas();
            void renderDomHierarchyPane();
            void renderDomNodeRecursive(HtmlUiElement& elem);
            void renderElementInspectorPane();
            void renderBoxModelVisualizer(HtmlUiElement& elem);
            void renderAlignmentGuides(ImDrawList* drawList, const ImVec2& viewOrigin, float viewScale);

            ResizeHandle hitTestResizeHandles(
                const HtmlUiElement& elem,
                const ImVec2& mousePos,
                const ImVec2& viewOrigin,
                float viewScale);
            void applyResizeFromSnapshot(
                HtmlUiElement& elem,
                const HtmlUiElement& startElem,
                ResizeHandle handle,
                const ImVec2& totalDesignDelta);
            void computeAlignmentGuides(const HtmlUiElement& elem, float canvasW, float canvasH);

            void insertElementFromPalette(const std::string& tag, float dropDesignX, float dropDesignY);
            void parseRcssAndApplyStyles();
            void applyDeclarationsToElement(
                HtmlUiElement& elem,
                const std::vector<std::pair<std::string, std::string>>& declarations);
            void layoutElementsHierarchically();
            void layoutContainerChildren(HtmlUiElement& parent);

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            Blueprint::BlueprintEditorComposite* m_blueprintEditorComposite = nullptr;

            std::string m_boundProjectDir;
            std::vector<std::string> m_availableUiFiles;
            std::string m_activeFilePath = "Assets/UI/CityHUD.rml";
            std::string m_documentTitle = "Game HUD";
            std::string m_rmlSource;
            std::string m_rcssSource;
            std::string m_baseRcssSource;
            bool m_isDirty = false;

            std::vector<HtmlUiElement> m_elements;
            int32_t m_selectedElementId = -1;
            uint32_t m_elementIdCounter = 100;

            bool m_isDraggingElement = false;
            ResizeHandle m_activeResizeHandle = ResizeHandle::None;
            ImVec2 m_dragStartMousePos{0.0f, 0.0f};
            std::vector<AlignmentGuide> m_activeGuides;
            bool m_snapToGrid = true;
            float m_gridSize = 8.0f;
            float m_canvasZoom = 1.0f;

            std::vector<HtmlUiElement> m_dragStartSnapshot;

            float m_previewHpRatio = 0.85f;
            int m_previewAmmo = 30;
        };

    } // namespace GameHtmlUI
} // namespace MQEngine

#endif // MQENGINE_GAME_HTML_UI_EDITOR_COMPOSITE_H
