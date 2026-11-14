/**
 * @file BlueprintEditorComposite.h
 * @brief 可视化节点蓝图与 TypeScript (TS) 源码双向无损翻译编辑器 Composite（tags: [Editor, ImGui]，独立编译为 BlueprintEditorComposite.dll）
 */
#ifndef MQENGINE_BLUEPRINT_EDITOR_COMPOSITE_H
#define MQENGINE_BLUEPRINT_EDITOR_COMPOSITE_H

#include "BlueprintEditorTypes.h"
#include "../BlueprintRuntimeComposite/BlueprintRuntimeComposite.h"

#include <MQEngine/asset/interface/Composite.h>

#include <unordered_map>
#include <functional>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
        class IUndoableCommand;
    } // namespace EditorBase

    namespace Blueprint
    {
        /**
         * @brief 蓝图可视化编辑器 Composite：
         *        提供 ImNodes 风格可视化画布，支持节点连线、参数编辑、
         *        以及蓝图节点图 <-> TypeScript 源码的双向无损 AST 互转翻译。
         */
        class BLUEPRINT_EDITOR_COMPOSITE_API BlueprintEditorComposite : public Asset::Composite
        {
        public:
            BlueprintEditorComposite();
            ~BlueprintEditorComposite() override;

            BlueprintEditorComposite(const BlueprintEditorComposite&) = delete;
            BlueprintEditorComposite& operator=(const BlueprintEditorComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "BlueprintEditorComposite"; }
            std::string getPluginName() const override { return "Blueprint"; }
            std::vector<std::string> getDependencies() const override { return {"BlueprintRuntimeComposite", "EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 双向翻译核心 API ====================

            std::string translateGraphToTypeScript() const;
            Status parseTypeScriptToGraph(const std::string& tsCode);
            void autoLayoutGraph();

            // ==================== 状态快照与 Undo/Redo 恢复 ====================

            GraphSnapshot captureGraphSnapshot();
            void restoreGraphSnapshot(const GraphSnapshot& snapshot);

            // ==================== 节点创建工厂与外部事件绑定 ====================

            EditorNode createNodeByOpCode(const std::string& opCode, FCT::Vec2 pos, const std::string& extraParam = "");
            void bindHtmlUiEventNode(const std::string& domId, const std::string& tag, const std::string& eventType);

        private:
            void ensureImNodesInitialized();
            void renderBlueprintEditorWindow();
            void renderVisualCanvas();
            void renderNodeContextMenu();
            void syncWithRuntime();
            void saveBlueprintToDisk();

            void createDefaultSampleGraph();
            uint32_t allocatePinId() noexcept { return ++m_pinIdCounter; }
            uint32_t allocateNodeId() noexcept { return ++m_nodeIdCounter; }
            uint32_t allocateLinkId() noexcept { return ++m_linkIdCounter; }

            bool isPinLinked(uint32_t pinId) const noexcept;
            const RuntimePin* findPin(uint32_t pinId, const EditorNode** outOwnerNode = nullptr) const;
            RuntimePin* findPinMutable(uint32_t pinId, EditorNode** outOwnerNode = nullptr);

            void handleLinkCreated(int startAttr, int endAttr);
            void handleLinkDestroyed(int linkId);
            void handleDeleteSelection();
            void handleDomElementDrop(const char* domId);
            void handleAssetDrop(const char* assetPath);

            void pushCommand(std::string name, GraphSnapshot beforeState);

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            BlueprintRuntimeComposite* m_runtimeComposite = nullptr;

            ImNodesContext* m_imnodesCtx = nullptr;
            ImNodesEditorContext* m_editorCtx = nullptr;
            bool m_nodesPositioned = false;

            std::vector<EditorNode> m_nodes;
            std::vector<EditorLink> m_links;

            std::string m_activeFilePath = "Assets/Scripts/GameplayController.ts";
            std::string m_typeScriptSource;

            uint32_t m_nodeIdCounter = 10;
            uint32_t m_pinIdCounter = 100;
            uint32_t m_linkIdCounter = 1000;

            bool m_openContextMenu = false;
            FCT::Vec2 m_contextMenuPos{120.0f, 120.0f};
            char m_nodeSearchFilter[128] = {0};
        };

    } // namespace Blueprint
} // namespace MQEngine

#endif // MQENGINE_BLUEPRINT_EDITOR_COMPOSITE_H
