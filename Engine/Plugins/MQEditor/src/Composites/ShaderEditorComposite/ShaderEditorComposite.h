/**
 * @file ShaderEditorComposite.h
 * @brief 着色器节点图 (ShaderGraph)、HLSL 源码实时编译与 PBR 材质球预览 Composite
 */
#ifndef MQENGINE_MQ_EDITOR_SHADER_EDITOR_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_SHADER_EDITOR_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "ShaderEditorTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>
#include <imnodes.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 着色器与材质编辑器 Composite：
         *        提供可视化节点式着色器图 (ShaderGraph)、DAG 拓扑转译 HLSL、Shaderc 实时 SPIR-V 编译诊断，
         *        以及交互式 PBR 材质球预览与 .mqshader/.sgedt 资产序列化。
         *        Note: RenderGraph 拓扑图编辑职责由独立的 RenderGraphEditorComposite 唯一承载。
         */
        class SHADER_EDITOR_COMPOSITE_API ShaderEditorComposite : public Asset::Composite
        {
        public:
            ShaderEditorComposite();
            ~ShaderEditorComposite() override;

            ShaderEditorComposite(const ShaderEditorComposite&) = delete;
            ShaderEditorComposite& operator=(const ShaderEditorComposite&) = delete;

            // ==================== Composite 接口实现 ====================

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "ShaderEditorComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor"}; }

        private:
            // 主面板、工具栏、预设与节点属性检视器 (ShaderEditorComposite.cpp)
            void renderShaderEditor();
            void renderToolbarAndPresets();
            void renderNodePropertyInspector();
            void renderHlslSourceEditor();
            void openActiveShaderInCodeEditor();
            void reloadActiveShaderFromDisk();
            std::string resolveActiveShaderDiskPath() const;
            void initDefaultShaderNodes();
            void applyTemplatePreset(int presetIndex);
            void clearShaderGraph();
            void ensureImNodesInitialized();

            // ImNodes 节点图画布、引脚连线与右键菜单 (ShaderNodeGraphCanvas.cpp)
            void renderShaderGraphCanvas();
            void renderShaderNodeItems();
            void handleNodeGraphInteractions(bool isEditorHovered);
            void renderNodeGraphContextMenus();

            // DAG -> HLSL 编译、SPIR-V 热更新、PBR 材质球预览与序列化 (ShaderCodeGenAndPreview.cpp)
            void compileShaderCode();
            void compileCustomHlslSource();
            void renderPbrMaterialPreview();
            void saveShaderGraph(const std::string& name);
            void loadShaderGraph(const std::string& name);

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;
            ImNodesContext* m_imnodesCtx = nullptr;
            ImNodesEditorContext* m_shaderEditorCtx = nullptr;
            bool m_shaderNodesPositioned = false;

            std::vector<ShaderGraphNode> m_nodes;
            std::vector<ShaderGraphLink> m_links;
            PbrMaterialPreviewParams m_previewParams;

            int m_selectedTab = 0; // 0: ShaderGraph, 1: HLSL Source, 2: PBR Material Preview
            int m_selectedNodeId = -1;
            int m_presetIndex = 0;
            std::string m_activeShaderSourcePath = "Assets/Shaders/PBR_Standard.hlsl";
            std::string m_hlslSource;
            std::string m_compileOutput;
            bool m_compileSuccess = true;
            int m_nextNodeId = 10;
            int m_nextPinId = 100;
            int m_nextLinkId = 1000;

            char m_shaderGraphName[64] = "DefaultPBR";
            char m_nodeSearchFilter[128] = "";
            int m_hoveredNodeId = -1;
            int m_hoveredLinkId = -1;
            float m_contextMenuPos[2] = {100.0f, 100.0f};
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_SHADER_EDITOR_COMPOSITE_H
