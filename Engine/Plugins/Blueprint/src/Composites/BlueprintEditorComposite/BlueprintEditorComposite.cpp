/**
 * @file BlueprintEditorComposite.cpp
 * @brief 蓝图可视化编辑器生命周期、顶层工具栏、图表管理、节点工厂与外部拖拽接收实现
 */
#include "BlueprintEditorComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>
#include <Composites/EditorUiComposite/CommandStack.h>

#include <cctype>

namespace MQEngine
{
    namespace Blueprint
    {
        namespace
        {
            constexpr const char* kWindowTitle = "蓝图可视化编辑器 (Blueprint Editor)";

            std::string toLowerAscii(std::string_view sv)
            {
                std::string res;
                res.reserve(sv.size());
                for (char c : sv)
                {
                    res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                return res;
            }
        } // namespace

        /**
         * @brief 蓝图图表撤销/重做命令
         */
        class BlueprintGraphCommand : public EditorBase::IUndoableCommand
        {
        public:
            BlueprintGraphCommand(
                std::string name,
                BlueprintEditorComposite* editor,
                GraphSnapshot beforeState,
                GraphSnapshot afterState)
                : m_name(std::move(name))
                , m_editor(editor)
                , m_before(std::move(beforeState))
                , m_after(std::move(afterState))
            {
            }

            void execute() override
            {
                if (m_editor)
                {
                    m_editor->restoreGraphSnapshot(m_after);
                }
            }

            void undo() override
            {
                if (m_editor)
                {
                    m_editor->restoreGraphSnapshot(m_before);
                }
            }

            std::string getName() const override
            {
                return m_name;
            }

        private:
            std::string m_name;
            BlueprintEditorComposite* m_editor = nullptr;
            GraphSnapshot m_before;
            GraphSnapshot m_after;
        };

        BlueprintEditorComposite::BlueprintEditorComposite() = default;

        BlueprintEditorComposite::~BlueprintEditorComposite()
        {
            onUnload();
        }

        Status BlueprintEditorComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
                m_runtimeComposite = dynamic_cast<BlueprintRuntimeComposite*>(
                    assetModule->compositeManager.getComposite("BlueprintRuntimeComposite"));
            }

            createDefaultSampleGraph();
            m_typeScriptSource = translateGraphToTypeScript();
            syncWithRuntime();

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Center,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderBlueprintEditorWindow();
                    },
                    false);

                // 资产浏览器双击 .ts 文件信号监听
                m_editorUiComposite->getUiContext().registerSignalHandler<EditorBase::SelectAssetSignal>(
                    [this](const EditorBase::SelectAssetSignal& sig) {
                        if (sig.assetExtension == ".ts")
                        {
                            if (m_editorUiComposite)
                            {
                                m_editorUiComposite->setDockWindowOpen(kWindowTitle, true);
                            }
                            m_activeFilePath = sig.assetPath;
                            auto* resMod = Resource::Resource::InstancePtr();
                            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
                            if (loader && loader->exists(sig.assetPath))
                            {
                                auto res = loader->loadFile(sig.assetPath);
                                if (res.ok())
                                {
                                    m_typeScriptSource = *res;
                                    (void)parseTypeScriptToGraph(m_typeScriptSource);
                                }
                            }
                        }
                    });
            }

            return OkStatus();
        }

        Status BlueprintEditorComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void BlueprintEditorComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
        }

        void BlueprintEditorComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
            }
            if (m_editorCtx)
            {
                ImNodes::EditorContextFree(m_editorCtx);
                m_editorCtx = nullptr;
            }
            if (m_imnodesCtx)
            {
                ImNodes::DestroyContext(m_imnodesCtx);
                m_imnodesCtx = nullptr;
            }

            m_nodes.clear();
            m_links.clear();
            m_editorUiComposite = nullptr;
            m_runtimeComposite = nullptr;
            Asset::Composite::onUnload();
        }

        GraphSnapshot BlueprintEditorComposite::captureGraphSnapshot()
        {
            if (m_editorCtx)
            {
                for (auto& node : m_nodes)
                {
                    ImVec2 p = ImNodes::GetNodeEditorSpacePos(static_cast<int>(node.id));
                    if (p.x != 0.0f || p.y != 0.0f)
                    {
                        node.pos = FCT::Vec2(p.x, p.y);
                    }
                }
            }
            return GraphSnapshot{m_nodes, m_links};
        }

        void BlueprintEditorComposite::restoreGraphSnapshot(const GraphSnapshot& snapshot)
        {
            m_nodes = snapshot.nodes;
            m_links = snapshot.links;
            m_nodesPositioned = false;
            m_typeScriptSource = translateGraphToTypeScript();
            syncWithRuntime();
        }

        void BlueprintEditorComposite::pushCommand(std::string name, GraphSnapshot beforeState)
        {
            GraphSnapshot afterState = captureGraphSnapshot();
            if (m_editorUiComposite)
            {
                m_editorUiComposite->getCommandStack().executeCommand(
                    makeUnique<BlueprintGraphCommand>(std::move(name), this, std::move(beforeState), std::move(afterState)));
            }
            else
            {
                restoreGraphSnapshot(afterState);
            }
        }

        EditorNode BlueprintEditorComposite::createNodeByOpCode(const std::string& opCode, FCT::Vec2 pos, const std::string& extraParam)
        {
            EditorNode node;
            node.id = allocateNodeId();
            node.opCode = opCode;
            node.pos = pos;
            node.size = FCT::Vec2(200.0f, 90.0f);

            if (opCode == "OnStart")
            {
                node.title = "OnStart 事件";
                node.outputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, true});
            }
            else if (opCode == "OnUpdate")
            {
                node.title = "OnUpdate 每帧回调";
                node.outputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, true});
                node.outputs.push_back(RuntimePin{allocatePinId(), "dt", PinDataType::Float, true, PinValue{PinDataType::Float, 0.016f}});
            }
            else if (opCode == "OnUIClick")
            {
                node.title = "OnUIClick 元素点击";
                node.outputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, true});
                node.outputs.push_back(RuntimePin{
                    allocatePinId(), "ElementId", PinDataType::String, true,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("btnSubmit") : extraParam}});
            }
            else if (opCode == "OnUIInput")
            {
                node.title = "OnUIInput 元素输入";
                node.outputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, true});
                node.outputs.push_back(RuntimePin{
                    allocatePinId(), "ElementId", PinDataType::String, true,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("txtInput") : extraParam}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Value", PinDataType::String, true, PinValue{PinDataType::String, std::string("")}});
            }
            else if (opCode == "GetStartupFilePath")
            {
                node.title = "获取启动文件路径";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
                node.outputs.push_back(RuntimePin{allocatePinId(), "FilePath", PinDataType::String, true, PinValue{PinDataType::String, std::string("")}});
            }
            else if (opCode == "OpenModel")
            {
                node.title = "打开并聚焦模型";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Path", PinDataType::String, false,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("Assets/Models/DamagedHelmet.gltf") : extraParam}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Done", PinDataType::Execution, true});
            }
            else if (opCode == "PrintLog")
            {
                node.title = "打印控制台日志";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Message", PinDataType::String, false,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("Hello MQEngine Blueprint") : extraParam}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }
            else if (opCode == "SetUIInnerText")
            {
                node.title = "设置 UI 内部文本";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ElementId", PinDataType::String, false,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("lblTitle") : extraParam}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Text", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("Hello World")}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }
            else if (opCode == "SetStyleProperty")
            {
                node.title = "设置 UI 样式属性";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ElementId", PinDataType::String, false,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("mainPanel") : extraParam}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Property", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("display")}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Value", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("block")}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }
            else if (opCode == "ToggleClass")
            {
                node.title = "切换 UI 类名样式";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ElementId", PinDataType::String, false,
                    PinValue{PinDataType::String, extraParam.empty() ? std::string("navButton") : extraParam}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ClassName", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("active")}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }
            else if (opCode == "GetComponent")
            {
                node.title = "获取实体组件";
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Entity", PinDataType::Entity, false,
                    PinValue{PinDataType::Entity, static_cast<uint32_t>(1)}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ComponentType", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("LocalTransform")}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Value", PinDataType::String, true, PinValue{PinDataType::String, std::string("")}});
            }
            else if (opCode == "SetComponent")
            {
                node.title = "设置实体组件";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Entity", PinDataType::Entity, false,
                    PinValue{PinDataType::Entity, static_cast<uint32_t>(1)}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "ComponentType", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("LocalTransform")}});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Value", PinDataType::String, false,
                    PinValue{PinDataType::String, std::string("0.0, 0.0, 0.0")}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }
            else if (opCode == "Branch")
            {
                node.title = "条件分支 (Branch)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.inputs.push_back(RuntimePin{
                    allocatePinId(), "Condition", PinDataType::Bool, false,
                    PinValue{PinDataType::Bool, true}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "True", PinDataType::Execution, true});
                node.outputs.push_back(RuntimePin{allocatePinId(), "False", PinDataType::Execution, true});
            }
            else if (opCode == "Add" || opCode == "MathAdd")
            {
                node.title = "数值加法 (Add)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Float, true, PinValue{PinDataType::Float, 0.0f}});
            }
            else if (opCode == "Sub" || opCode == "MathSub")
            {
                node.title = "数值减法 (Sub)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Float, true, PinValue{PinDataType::Float, 0.0f}});
            }
            else if (opCode == "Mul" || opCode == "MathMul")
            {
                node.title = "数值乘法 (Mul)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Float, false, PinValue{PinDataType::Float, 1.0f}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Float, false, PinValue{PinDataType::Float, 1.0f}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Float, true, PinValue{PinDataType::Float, 1.0f}});
            }
            else if (opCode == "Div" || opCode == "MathDiv")
            {
                node.title = "数值除法 (Div)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Float, false, PinValue{PinDataType::Float, 1.0f}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Float, false, PinValue{PinDataType::Float, 1.0f}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Float, true, PinValue{PinDataType::Float, 1.0f}});
            }
            else if (opCode == "Compare")
            {
                node.title = "数值比较 (Compare)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Float, false, PinValue{PinDataType::Float, 0.0f}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Equal", PinDataType::Bool, true, PinValue{PinDataType::Bool, true}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Greater", PinDataType::Bool, true, PinValue{PinDataType::Bool, false}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Less", PinDataType::Bool, true, PinValue{PinDataType::Bool, false}});
            }
            else if (opCode == "LogicAnd" || opCode == "And")
            {
                node.title = "逻辑与 (And)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Bool, false, PinValue{PinDataType::Bool, true}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Bool, false, PinValue{PinDataType::Bool, true}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Bool, true, PinValue{PinDataType::Bool, true}});
            }
            else if (opCode == "LogicOr" || opCode == "Or")
            {
                node.title = "逻辑或 (Or)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "A", PinDataType::Bool, false, PinValue{PinDataType::Bool, false}});
                node.inputs.push_back(RuntimePin{allocatePinId(), "B", PinDataType::Bool, false, PinValue{PinDataType::Bool, false}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Result", PinDataType::Bool, true, PinValue{PinDataType::Bool, false}});
            }
            else if (opCode == "LogicNot" || opCode == "Not")
            {
                node.title = "逻辑非 (Not)";
                node.inputs.push_back(RuntimePin{allocatePinId(), "In", PinDataType::Bool, false, PinValue{PinDataType::Bool, false}});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Out", PinDataType::Bool, true, PinValue{PinDataType::Bool, true}});
            }
            else
            {
                node.title = opCode;
                node.inputs.push_back(RuntimePin{allocatePinId(), "Exec", PinDataType::Execution, false});
                node.outputs.push_back(RuntimePin{allocatePinId(), "Then", PinDataType::Execution, true});
            }

            return node;
        }

        void BlueprintEditorComposite::createDefaultSampleGraph()
        {
            m_nodes.clear();
            m_links.clear();

            EditorNode nStart = createNodeByOpCode("OnStart", FCT::Vec2(80.0f, 100.0f));
            m_nodes.push_back(std::move(nStart));

            EditorNode nPath = createNodeByOpCode("GetStartupFilePath", FCT::Vec2(320.0f, 100.0f));
            m_nodes.push_back(std::move(nPath));

            EditorNode nOpen = createNodeByOpCode("OpenModel", FCT::Vec2(600.0f, 100.0f), "Assets/Models/DamagedHelmet.gltf");
            m_nodes.push_back(std::move(nOpen));

            m_links.push_back(EditorLink{
                .id = allocateLinkId(),
                .startPinId = m_nodes[0].outputs[0].id,
                .endPinId = m_nodes[1].inputs[0].id
            });
            m_links.push_back(EditorLink{
                .id = allocateLinkId(),
                .startPinId = m_nodes[1].outputs[0].id,
                .endPinId = m_nodes[2].inputs[0].id
            });
            m_links.push_back(EditorLink{
                .id = allocateLinkId(),
                .startPinId = m_nodes[1].outputs[1].id,
                .endPinId = m_nodes[2].inputs[1].id
            });

            m_nodesPositioned = false;
        }

        void BlueprintEditorComposite::handleDomElementDrop(const char* domId)
        {
            if (!domId || domId[0] == '\0')
            {
                return;
            }

            GraphSnapshot beforeState = captureGraphSnapshot();
            std::string idStr = domId;
            std::string lowerId = toLowerAscii(idStr);

            std::string opCode = "OnUIClick";
            if (lowerId.find("input") != std::string::npos ||
                lowerId.find("edit") != std::string::npos ||
                lowerId.find("search") != std::string::npos ||
                lowerId.find("field") != std::string::npos)
            {
                opCode = "OnUIInput";
            }

            EditorNode node = createNodeByOpCode(opCode, m_contextMenuPos, idStr);
            m_nodes.push_back(std::move(node));
            m_nodesPositioned = false;

            pushCommand("拖入 UI 元素事件节点 (" + idStr + ")", std::move(beforeState));
        }

        void BlueprintEditorComposite::bindHtmlUiEventNode(const std::string& domId, const std::string& tag, const std::string& eventType)
        {
            GraphSnapshot beforeState = captureGraphSnapshot();
            std::string opCode = "OnUIClick";
            if (eventType == "input" || eventType == "change" || tag == "input" || tag == "select")
            {
                opCode = "OnUIInput";
            }
            else if (eventType == "mouseenter" || eventType == "hover")
            {
                opCode = "OnUIHover";
            }
            else if (eventType == "click")
            {
                opCode = "OnUIClick";
            }

            FCT::Vec2 spawnPos = m_contextMenuPos;
            if (!m_nodes.empty())
            {
                spawnPos.x = m_nodes.back().pos.x + 220.0f;
                spawnPos.y = m_nodes.back().pos.y;
            }

            EditorNode node = createNodeByOpCode(opCode, spawnPos, domId);
            m_nodes.push_back(std::move(node));
            m_nodesPositioned = false;

            syncWithRuntime();

            pushCommand("绑定 HTML UI 事件节点 (" + domId + "." + eventType + ")", std::move(beforeState));
        }

        void BlueprintEditorComposite::handleAssetDrop(const char* assetPath)
        {
            if (!assetPath || assetPath[0] == '\0')
            {
                return;
            }

            GraphSnapshot beforeState = captureGraphSnapshot();
            std::string pathStr = assetPath;
            std::string lowerPath = toLowerAscii(pathStr);

            EditorNode node;
            if (lowerPath.ends_with(".mesh") || lowerPath.ends_with(".gltf") ||
                lowerPath.ends_with(".glb") || lowerPath.ends_with(".obj") || lowerPath.ends_with(".fbx"))
            {
                node = createNodeByOpCode("OpenModel", m_contextMenuPos, pathStr);
            }
            else
            {
                node = createNodeByOpCode("PrintLog", m_contextMenuPos, "Asset: " + pathStr);
            }

            m_nodes.push_back(std::move(node));
            m_nodesPositioned = false;

            pushCommand("拖入资产节点 (" + pathStr + ")", std::move(beforeState));
        }

        void BlueprintEditorComposite::syncWithRuntime()
        {
            if (!m_runtimeComposite)
            {
                return;
            }

            RuntimeGraph rg;
            rg.name = "ActiveEditorGraph";
            for (const auto& en : m_nodes)
            {
                RuntimeNode rn;
                rn.id = en.id;
                rn.title = en.title;
                rn.opCode = en.opCode;
                rn.posX = en.pos.x;
                rn.posY = en.pos.y;
                rn.inputs = en.inputs;
                rn.outputs = en.outputs;
                rg.nodes.push_back(std::move(rn));
            }
            for (const auto& el : m_links)
            {
                rg.links.push_back(RuntimeLink{
                    .id = el.id,
                    .startPinId = el.startPinId,
                    .endPinId = el.endPinId
                });
            }

            m_runtimeComposite->registerGraph(std::move(rg));

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "蓝图同步",
                    "已将当前节点图同步至 Blueprint 运行时求值引擎",
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
        }

        void BlueprintEditorComposite::saveBlueprintToDisk()
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return;
            }

            (void)loader->makeDir("Assets");
            (void)loader->makeDir("Assets/Scripts");

            m_typeScriptSource = translateGraphToTypeScript();
            (void)loader->saveFile(m_activeFilePath, m_typeScriptSource);

            if (m_editorUiComposite)
            {
                m_editorUiComposite->addLog(
                    EditorBase::LogEntry::Level::Info,
                    "已保存蓝图 TypeScript 源码至 " + m_activeFilePath,
                    "BlueprintEditor");
            }
        }

        void BlueprintEditorComposite::renderBlueprintEditorWindow()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kWindowTitle)
                : "蓝图可视化编辑器###蓝图可视化编辑器 (Blueprint Editor)";

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            ImGui::Text(L("TypeScript 双向无损翻译蓝图 — %s (%zu 节点, %zu 连线)",
                          "TypeScript Bi-directional AST Blueprint — %s (%zu Nodes, %zu Links)"),
                        m_activeFilePath.c_str(), m_nodes.size(), m_links.size());
            ImGui::SameLine();
            if (ImGui::SmallButton(L("整理图表", "Auto-Layout")))
            {
                GraphSnapshot beforeState = captureGraphSnapshot();
                autoLayoutGraph();
                pushCommand("整理图表布局", std::move(beforeState));
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(L("同步到运行时", "Sync to Runtime")))
            {
                syncWithRuntime();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(L("保存至磁盘", "Save to Disk")))
            {
                saveBlueprintToDisk();
            }

            // Undo / Redo 工具按钮
            if (m_editorUiComposite)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                const bool canUndo = m_editorUiComposite->getCommandStack().canUndo();
                if (!canUndo) ImGui::BeginDisabled();
                if (ImGui::SmallButton(L("撤销 (Ctrl+Z)", "Undo (Ctrl+Z)")))
                {
                    m_editorUiComposite->getCommandStack().undo();
                }
                if (!canUndo) ImGui::EndDisabled();

                ImGui::SameLine();
                const bool canRedo = m_editorUiComposite->getCommandStack().canRedo();
                if (!canRedo) ImGui::BeginDisabled();
                if (ImGui::SmallButton(L("重做 (Ctrl+Y)", "Redo (Ctrl+Y)")))
                {
                    m_editorUiComposite->getCommandStack().redo();
                }
                if (!canRedo) ImGui::EndDisabled();
            }

            ImGui::Separator();

            if (ImGui::BeginTabBar("BlueprintEditorTabs"))
            {
                if (ImGui::BeginTabItem(L("可视化蓝图画布###TabBpCanvas", "Visual Canvas###TabBpCanvas")))
                {
                    renderVisualCanvas();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(L("TypeScript 源码###TabBpTsCode", "TypeScript Source###TabBpTsCode")))
                {
                    std::vector<char> buf(m_typeScriptSource.begin(), m_typeScriptSource.end());
                    buf.resize(65536, 0);
                    if (ImGui::InputTextMultiline("##BpTsEditor", buf.data(), buf.size(), ImVec2(-1.0f, -40.0f)))
                    {
                        m_typeScriptSource = buf.data();
                    }
                    if (ImGui::Button(L("从代码反向更新蓝图节点图", "Reverse Parse TS to Nodes")))
                    {
                        (void)parseTypeScriptToGraph(m_typeScriptSource);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(L("从节点图重新生成代码", "Regenerate TS from Nodes")))
                    {
                        m_typeScriptSource = translateGraphToTypeScript();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::End();
        }

    } // namespace Blueprint
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::Blueprint::BlueprintEditorComposite)
