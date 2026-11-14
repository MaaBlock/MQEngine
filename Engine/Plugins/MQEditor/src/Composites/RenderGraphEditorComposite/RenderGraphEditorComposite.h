/**
 * @file RenderGraphEditorComposite.h
 * @brief 管线编辑组件：承载 RenderGraph 节点编辑与管线对象 (PSO) 编辑双页签，以及 GraphViewInsight 中间纹理检视
 */
#ifndef MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "RenderGraphEditorTypes.h"
#include "GraphViewInsight.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>
#include <MQEngine/domain/renderer/RendererSubsystem.h>
#include <imnodes.h>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace FCT
{
    struct PassDesc;
    class Context;
    class Window;
    class RenderGraph;
    class Image;
    class ImguiContext;
} // namespace FCT

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 管线编辑 Composite：
         *        提供 [RenderGraph | 管线对象] 双页签工作流、.pgraph/.pso 序列化、C++ 代码生成与中间纹理实时预览
         */
        class RENDER_GRAPH_EDITOR_COMPOSITE_API RenderGraphEditorComposite : public Asset::Composite
        {
        public:
            RenderGraphEditorComposite();
            ~RenderGraphEditorComposite() override;

            RenderGraphEditorComposite(const RenderGraphEditorComposite&) = delete;
            RenderGraphEditorComposite& operator=(const RenderGraphEditorComposite&) = delete;

            // ==================== Composite 生命周期接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "RenderGraphEditorComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 图表节点与连线管理 API (RenderGraphEditorNodes.cpp) ====================

            int newPassNode(const std::string& name = "Pass");
            int newImageNode(const std::string& name = "Image");
            void addTextureLink(int imageId, int passId);
            void addTargetLink(int passId, int index, int imageId);
            void addDepthStencilLink(int passId, int imageId);
            void addLink(int startHash, int endHash);
            void newTexturePin(PassNode& pass);
            void removePassPin(int pinHash);
            void removeImagePin(int pinHash);
            void deleteNode(int contextMenuNodeId);
            void deletePass(int contextMenuNodeId);
            void deleteImage(int contextMenuNodeId);

            int findImageNode(const std::string& name) const;
            int findPassNode(const std::string& name) const;

            void createGraphFromPassDescs(const std::vector<FCT::PassDesc>& passDescs);
            std::vector<FCT::PassDesc> convertCurrentGraphToPassDescs();

            void autoLayoutGraph();
            void clearGraph();

            // ==================== 序列化与代码生成 API (RenderGraphEditorSerializer.cpp) ====================

            void saveToFile(const std::string& filename);
            void loadFromFile(const std::string& filename);

            std::string generatorCode();
            std::string generatePassCode(const PassNode& pass);

            void restoreEditorTopology();

        private:
            // ==================== 主窗口与 RenderGraph 页签 (RenderGraphEditorComposite.cpp) ====================

            void renderEditorPanel();
            void renderRenderGraphTab();
            void renderRenderGraphToolbar();
            void renderInspectorPanel();
            void renderGeneratedCodePanel();

            // ==================== 管线对象编辑页签 (PipelineObjectEditor.cpp) ====================

            void renderPipelineObjectsTab();
            void syncPipelineObjectsFromRenderer(bool forceReload = false);
            void renderPipelineObjectsToolbar();
            void renderPipelineObjectListPane(float paneWidth);
            void renderPipelineObjectDetailPane();
            bool renderPsoHeaderAndShaderSection(Domain::Renderer::PipelineObjectDesc& pso);
            bool renderPsoRasterizerSection(Domain::Renderer::PipelineObjectDesc& pso);
            bool renderPsoDepthStencilSection(Domain::Renderer::PipelineObjectDesc& pso);
            bool renderPsoBlendSection(Domain::Renderer::PipelineObjectDesc& pso);
            void applyPipelineObjectToRenderer(const Domain::Renderer::PipelineObjectDesc& pso, bool showFeedback);
            void savePipelineObjectsToFile(const std::string& filename);
            void loadPipelineObjectsFromFile(const std::string& filename);

            // ==================== ImNodes 画布与节点绘制 (RenderGraphEditorCanvas.cpp) ====================

            void renderNodeGraphCanvas();
            void renderPassNode(PassNode& pass);
            void renderImageNode(ImageNode& image);
            void ensureImNodesInitialized();

            // ==================== 中间纹理实时预览 (GraphViewInsight.cpp) ====================

            void renderTextureInsightWindows();

            int generatePinId(int nodeId, const std::string& pinType, int index = 0);
            int getNextNodeId() { return ++m_nextNodeId; }
            int getNextLinkId() { return ++m_linkId; }

            const char* tr(const char* zh, const char* en) const;

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            ImNodesContext* m_imnodesCtx = nullptr;
            ImNodesEditorContext* m_context = nullptr;

            std::map<int, LinkInfo> m_passOutputlinks;
            std::map<int, LinkInfo> m_passInputLinks;
            std::map<int, PassNode> m_passes;
            std::map<int, ImageNode> m_images;
            std::unordered_map<int, PinInfo> m_pinInfoMap;

            int m_nextNodeId = 0;
            int m_linkId = 0;
            int m_contextMenuNodeId = -1;

            bool m_nodesPositioned = false;
            std::string m_generatedCode;

            // F11 纯游戏模式与拓扑还原快照
            std::vector<FCT::PassDesc> m_editorTopologySnapshot;
            bool m_hasSnapshot = false;
            bool m_isDirectGameMode = false;

            // 中间纹理实时预览窗口状态 (GraphViewInsight)
            bool m_showTextureInsight = false;
            std::unordered_map<std::string, TextureInsightSettings> m_textureInsightSettings;

            int m_selectedNodeId = -1;
            char m_searchFilter[64] = {0};

            // 管线对象 (PSO) 页签编辑状态
            std::vector<Domain::Renderer::PipelineObjectDesc> m_pipelineObjectDrafts;
            bool m_pipelineObjectsSynced = false;
            int m_selectedPipelineObjectIdx = 0;
            bool m_autoApplyPipelineObject = true;
            char m_psoSearchFilter[64] = {0};
            int m_requestedMainTab = -1;
            uint32_t m_customPsoCounter = 1;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_RENDER_GRAPH_EDITOR_COMPOSITE_H
