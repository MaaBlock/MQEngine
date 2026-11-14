/**
 * @file BlueprintRuntimeComposite.cpp
 * @brief 蓝图运行时 Composite 生命周期、图注册表、事件派发、全局变量与 .d.ts 类型声明生成实现
 */
#include "BlueprintRuntimeComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>

#include <sstream>

namespace MQEngine
{
    namespace Blueprint
    {
        BlueprintRuntimeComposite::BlueprintRuntimeComposite() = default;

        BlueprintRuntimeComposite::~BlueprintRuntimeComposite()
        {
            onUnload();
        }

        Status BlueprintRuntimeComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));
            return OkStatus();
        }

        Status BlueprintRuntimeComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));

            // 执行所有已注册图的 OnStart 事件
            for (auto& [name, graph] : m_graphs)
            {
                (void)executeEvent(name, "OnStart");
            }

            return OkStatus();
        }

        void BlueprintRuntimeComposite::onTick(float deltaTime)
        {
            m_accumulatedTime += deltaTime;

            // 驱动所有已注册图的 OnUpdate 事件，注入实时 dt
            for (auto& [name, graph] : m_graphs)
            {
                graph.variables["dt"] = PinValue{.type = PinDataType::Float, .data = deltaTime};
                (void)executeEvent(name, "OnUpdate");
            }
        }

        void BlueprintRuntimeComposite::onUnload()
        {
            m_graphs.clear();
            m_globals.clear();
            m_recentProbes.clear();
            Asset::Composite::onUnload();
        }

        void BlueprintRuntimeComposite::registerGraph(RuntimeGraph graph)
        {
            const std::string name = graph.name;
            m_graphs[name] = std::move(graph);
        }

        void BlueprintRuntimeComposite::unregisterGraph(const std::string& name)
        {
            m_graphs.erase(name);
        }

        const RuntimeGraph* BlueprintRuntimeComposite::getGraph(const std::string& name) const noexcept
        {
            auto it = m_graphs.find(name);
            return (it != m_graphs.end()) ? &it->second : nullptr;
        }

        RuntimeGraph* BlueprintRuntimeComposite::getMutableGraph(const std::string& name) noexcept
        {
            auto it = m_graphs.find(name);
            return (it != m_graphs.end()) ? &it->second : nullptr;
        }

        Status BlueprintRuntimeComposite::executeEvent(const std::string& graphName, const std::string& eventName)
        {
            auto* graph = getMutableGraph(graphName);
            if (!graph)
            {
                return NotFoundError("Graph not found: " + graphName);
            }

            for (const auto& node : graph->nodes)
            {
                if (node.opCode == eventName)
                {
                    addProbe(node.id, 0, "Triggered Event: " + eventName);
                    std::unordered_map<uint32_t, PinValue> pinOutputs;
                    if (eventName == "OnUpdate" && node.outputs.size() > 1)
                    {
                        auto dtIt = graph->variables.find("dt");
                        float dtVal = (dtIt != graph->variables.end()) ? dtIt->second.asFloat() : 0.016f;
                        pinOutputs[node.outputs[1].id] = PinValue{.type = PinDataType::Float, .data = dtVal};
                    }
                    evaluateNode(*graph, node, pinOutputs);
                }
            }

            return OkStatus();
        }

        Status BlueprintRuntimeComposite::executeUiEvent(const std::string& elementId, const std::string& eventType, float value)
        {
            const std::string targetOp = (eventType == "input") ? "OnUIInput" : "OnUIClick";
            bool triggered = false;

            for (auto& [name, graph] : m_graphs)
            {
                for (const auto& node : graph.nodes)
                {
                    if (node.opCode == targetOp)
                    {
                        bool match = false;
                        if (!node.extraProperty.empty() && node.extraProperty == elementId)
                        {
                            match = true;
                        }
                        else
                        {
                            for (const auto& pin : node.outputs)
                            {
                                if (pin.name == "ElementId" && pin.defaultValue.asString() == elementId)
                                {
                                    match = true;
                                    break;
                                }
                            }
                        }

                        if (match)
                        {
                            triggered = true;
                            addProbe(node.id, 0, "UI Event: " + targetOp + " #" + elementId);
                            std::unordered_map<uint32_t, PinValue> pinOutputs;
                            for (const auto& outPin : node.outputs)
                            {
                                if (outPin.name == "ElementId")
                                {
                                    pinOutputs[outPin.id] = PinValue{.type = PinDataType::String, .data = elementId};
                                }
                                else if (outPin.name == "Value")
                                {
                                    pinOutputs[outPin.id] = PinValue{.type = PinDataType::Float, .data = value};
                                }
                            }
                            evaluateNode(graph, node, pinOutputs);
                        }
                    }
                }
            }

            return triggered ? OkStatus() : NotFoundError("No matching UI event handler for #" + elementId);
        }

        void BlueprintRuntimeComposite::executeFlow(RuntimeGraph& graph, uint32_t startNodeId)
        {
            for (const auto& node : graph.nodes)
            {
                if (node.id == startNodeId)
                {
                    std::unordered_map<uint32_t, PinValue> pinOutputs;
                    evaluateNode(graph, node, pinOutputs);
                    break;
                }
            }
        }

        void BlueprintRuntimeComposite::setGlobalVariable(const std::string& name, PinValue value)
        {
            m_globals[name] = std::move(value);
        }

        PinValue BlueprintRuntimeComposite::getGlobalVariable(const std::string& name) const
        {
            auto it = m_globals.find(name);
            return (it != m_globals.end()) ? it->second : PinValue{};
        }

        bool BlueprintRuntimeComposite::hasGlobalVariable(const std::string& name) const noexcept
        {
            return m_globals.find(name) != m_globals.end();
        }

        void BlueprintRuntimeComposite::addProbe(uint32_t nodeId, uint32_t pinId, const std::string& msg)
        {
            m_recentProbes.push_back(ExecutionPulseProbe{
                .nodeId = nodeId,
                .pinId = pinId,
                .timestampSec = m_accumulatedTime,
                .debugMessage = msg
            });

            if (m_recentProbes.size() > 64)
            {
                m_recentProbes.erase(m_recentProbes.begin(), m_recentProbes.begin() + 16);
            }
        }

        std::string BlueprintRuntimeComposite::generateTypeScriptDeclarations() const
        {
            std::ostringstream oss;
            oss << "/**\n";
            oss << " * @file mqengine.d.ts\n";
            oss << " * @brief MQEngine 核心 ECS 与蓝图 TypeScript 强类型定义声明\n";
            oss << " */\n\n";

            oss << "declare namespace MQEngine {\n";
            oss << "  export interface Vector3 {\n";
            oss << "    x: number;\n";
            oss << "    y: number;\n";
            oss << "    z: number;\n";
            oss << "  }\n\n";

            oss << "  export interface LocalTransform {\n";
            oss << "    position: Vector3;\n";
            oss << "    rotation: Vector3;\n";
            oss << "    scale: Vector3;\n";
            oss << "  }\n\n";

            oss << "  export interface Entity {\n";
            oss << "    id: number;\n";
            oss << "    getTransform(): LocalTransform;\n";
            oss << "    setTransform(transform: LocalTransform): void;\n";
            oss << "  }\n\n";

            oss << "  export class App {\n";
            oss << "    static getStartupFilePath(): string;\n";
            oss << "    static openModel(filePath: string): void;\n";
            oss << "    static log(message: string): void;\n";
            oss << "  }\n\n";

            oss << "  export class UI {\n";
            oss << "    static setInnerText(elementId: string, text: string): void;\n";
            oss << "    static setStyleProperty(elementId: string, prop: string, value: string): void;\n";
            oss << "    static toggleClass(elementId: string, className: string): void;\n";
            oss << "    static addEventListener(elementId: string, event: string, handler: (e: any) => void): void;\n";
            oss << "  }\n\n";

            oss << "  export class ECS {\n";
            oss << "    static getComponent(entity: number, componentType: string): any;\n";
            oss << "    static setComponent(entity: number, componentType: string, value: any): void;\n";
            oss << "  }\n";
            oss << "}\n";

            return oss.str();
        }

        Status BlueprintRuntimeComposite::exportTypeScriptDeclarations(const std::string& diskPath)
        {
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (!loader)
            {
                return FailedPreconditionError("ResourceLoader unavailable");
            }

            const std::string content = generateTypeScriptDeclarations();
            return loader->saveFile(diskPath, content);
        }

    } // namespace Blueprint
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::Blueprint::BlueprintRuntimeComposite)
