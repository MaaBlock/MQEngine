/**
 * @file BlueprintVmEvaluator.cpp
 * @brief 蓝图运行时 DAG 拓扑执行引擎与数学/逻辑/ECS/UI 节点求值实现
 */
#include "BlueprintRuntimeComposite.h"

#include <MQEngine/resource/core/resource.h>
#include <MQEngine/domain/core/domain.h>
#include <MQEngine/domain/scene/SceneSubsystem.h>
#include <MQEngine/domain/scene/SceneComponents.h>
#include <MQEngine/asset/core/Asset.h>
#include <Composites/RmlUiComposite/RmlUiComposite.h>

namespace MQEngine
{
    namespace Blueprint
    {
        namespace
        {
            GameHtmlUI::RmlUiComposite* resolveLoadedRmlUiComposite() noexcept
            {
                auto* assetMod = Asset::Asset::InstancePtr();
                if (!assetMod)
                {
                    return nullptr;
                }
                // 先按名称判空确认 RmlUiComposite.dll 已在宿主内存激活，避免无意义触发延迟加载
                auto* comp = assetMod->compositeManager.getComposite("RmlUiComposite");
                return comp ? static_cast<GameHtmlUI::RmlUiComposite*>(comp) : nullptr;
            }
        } // namespace
        void BlueprintRuntimeComposite::evaluateNode(
            RuntimeGraph& graph,
            const RuntimeNode& node,
            std::unordered_map<uint32_t, PinValue>& pinOutputs)
        {
            addProbe(node.id, 0, "Executing: " + node.opCode);

            // ==================== 1. 执行具体节点的核心逻辑 ====================

            if (node.opCode == "PrintLog")
            {
                std::string msg = "Log";
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "Message")
                    {
                        msg = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                spdlog::info("[Blueprint] {}", msg);
            }
            else if (node.opCode == "GetStartupFilePath")
            {
                std::string startupPath;
                auto* res = Resource::Resource::InstancePtr();
                if (res && res->getConfig())
                {
                    startupPath = res->getConfig()->getStartupFilePath();
                }
                for (const auto& outPin : node.outputs)
                {
                    if (outPin.name == "FilePath")
                    {
                        pinOutputs[outPin.id] = PinValue{.type = PinDataType::String, .data = startupPath};
                    }
                }
            }
            else if (node.opCode == "OpenModel")
            {
                std::string modelPath;
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "Path")
                    {
                        modelPath = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                if (modelPath.empty())
                {
                    auto* res = Resource::Resource::InstancePtr();
                    if (res && res->getConfig())
                    {
                        modelPath = res->getConfig()->getStartupFilePath();
                    }
                }
                if (modelPath.empty())
                {
                    return;
                }

                auto* domain = Domain::Domain::InstancePtr();
                if (domain)
                {
                    auto& reg = domain->scene.getRegistry();
                    FCT::ECS::entity anchor = FCT::ECS::null;
                    auto view = reg.view<Domain::Scene::ModelAnchorTag>();
                    for (auto e : view)
                    {
                        anchor = e;
                        break;
                    }
                    if (anchor == FCT::ECS::null)
                    {
                        anchor = domain->scene.createEntity();
                        reg.emplace_or_replace<Domain::Scene::ModelAnchorTag>(anchor);
                        if (domain->scene.shouldAttachEditorMetadata())
                        {
                            reg.emplace_or_replace<Domain::Scene::EditorNameComponent>(
                                anchor, Domain::Scene::EditorNameComponent{"ModelAnchorEntity"});
                        }
                    }
                    auto& mesh = reg.emplace_or_replace<Domain::Scene::StaticMeshComponent>(anchor);
                    mesh.meshAssetPath = modelPath;
                    mesh.primitiveType = -1;
                    mesh.visible = true;
                    mesh.castShadows = true;
                    spdlog::info("[Blueprint] OpenModel: 真实加载场景模型实体: {}", modelPath);
                }
            }
            else if (node.opCode == "SetUIInnerText")
            {
                std::string elemId;
                std::string text;
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "ElementId")
                    {
                        elemId = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                    else if (inPin.name == "Text")
                    {
                        text = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                if (elemId.empty() && !node.extraProperty.empty())
                {
                    elemId = node.extraProperty;
                }

                if (auto* rmlUi = resolveLoadedRmlUiComposite())
                {
                    (void)rmlUi->setElementInnerText(elemId, text);
                }
            }
            else if (node.opCode == "SetStyleProperty")
            {
                std::string elemId;
                std::string prop = "color";
                std::string val = "#ffffff";
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "ElementId")
                    {
                        elemId = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                    else if (inPin.name == "Property")
                    {
                        prop = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                    else if (inPin.name == "Value")
                    {
                        val = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                if (elemId.empty() && !node.extraProperty.empty())
                {
                    elemId = node.extraProperty;
                }

                if (auto* rmlUi = resolveLoadedRmlUiComposite())
                {
                    (void)rmlUi->setElementStyleProperty(elemId, prop, val);
                }
            }
            else if (node.opCode == "ToggleClass")
            {
                std::string elemId;
                std::string clsName = "active";
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "ElementId")
                    {
                        elemId = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                    else if (inPin.name == "ClassName")
                    {
                        clsName = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                if (elemId.empty() && !node.extraProperty.empty())
                {
                    elemId = node.extraProperty;
                }

                if (auto* rmlUi = resolveLoadedRmlUiComposite())
                {
                    (void)rmlUi->toggleElementClass(elemId, clsName);
                }
            }
            else if (node.opCode == "GetComponent")
            {
                uint32_t entId = 0;
                std::string compType = "Transform";
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "Entity")
                    {
                        entId = static_cast<uint32_t>(evaluatePinInput(graph, inPin.id, pinOutputs).asInt());
                    }
                    else if (inPin.name == "Type" || inPin.name == "ComponentType")
                    {
                        compType = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                }
                if (!node.extraProperty.empty())
                {
                    compType = node.extraProperty;
                }

                auto* domain = Domain::Domain::InstancePtr();
                if (domain)
                {
                    auto& reg = domain->scene.getRegistry();
                    FCT::ECS::entity targetEntity = static_cast<FCT::ECS::entity>(entId);
                    if (entId == 0)
                    {
                        auto view = reg.view<Domain::Scene::LocalTransform>();
                        if (view.begin() != view.end())
                        {
                            targetEntity = *view.begin();
                        }
                    }

                    if (reg.valid(targetEntity) && reg.all_of<Domain::Scene::LocalTransform>(targetEntity))
                    {
                        const auto& tr = reg.get<Domain::Scene::LocalTransform>(targetEntity);
                        for (const auto& outPin : node.outputs)
                        {
                            if (outPin.name == "Value" || outPin.name == "Transform")
                            {
                                pinOutputs[outPin.id] = PinValue{
                                    .type = PinDataType::Vector3,
                                    .data = tr.position
                                };
                            }
                        }
                    }
                }
            }
            else if (node.opCode == "SetComponent")
            {
                uint32_t entId = 0;
                std::string compType = "Transform";
                PinValue compVal;
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "Entity")
                    {
                        entId = static_cast<uint32_t>(evaluatePinInput(graph, inPin.id, pinOutputs).asInt());
                    }
                    else if (inPin.name == "Type" || inPin.name == "ComponentType")
                    {
                        compType = evaluatePinInput(graph, inPin.id, pinOutputs).asString();
                    }
                    else if (inPin.name == "Value")
                    {
                        compVal = evaluatePinInput(graph, inPin.id, pinOutputs);
                    }
                }
                if (!node.extraProperty.empty())
                {
                    compType = node.extraProperty;
                }

                auto* domain = Domain::Domain::InstancePtr();
                if (domain)
                {
                    auto& reg = domain->scene.getRegistry();
                    FCT::ECS::entity targetEntity = static_cast<FCT::ECS::entity>(entId);
                    if (entId == 0)
                    {
                        auto view = reg.view<Domain::Scene::LocalTransform>();
                        if (view.begin() != view.end())
                        {
                            targetEntity = *view.begin();
                        }
                    }

                    if (reg.valid(targetEntity))
                    {
                        auto& tr = reg.get_or_emplace<Domain::Scene::LocalTransform>(targetEntity);
                        tr.position = compVal.asVector3();
                    }
                }
            }

            // ==================== 2. 分支控制流与顺序连线递归推进 ====================

            if (node.opCode == "Branch")
            {
                bool cond = true;
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.name == "Condition")
                    {
                        cond = evaluatePinInput(graph, inPin.id, pinOutputs).asBool();
                    }
                }

                const std::string targetPinName = cond ? "True" : "False";
                for (const auto& outPin : node.outputs)
                {
                    if (outPin.name == targetPinName)
                    {
                        for (const auto& link : graph.links)
                        {
                            if (link.startPinId == outPin.id)
                            {
                                for (const auto& candidate : graph.nodes)
                                {
                                    for (const auto& inPin : candidate.inputs)
                                    {
                                        if (inPin.id == link.endPinId)
                                        {
                                            evaluateNode(graph, candidate, pinOutputs);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return;
            }

            // 常规 Execution 连线向下游推进
            for (const auto& outPin : node.outputs)
            {
                if (outPin.dataType != PinDataType::Execution)
                    continue;

                for (const auto& link : graph.links)
                {
                    if (link.startPinId == outPin.id)
                    {
                        for (const auto& candidate : graph.nodes)
                        {
                            for (const auto& inPin : candidate.inputs)
                            {
                                if (inPin.id == link.endPinId && inPin.dataType == PinDataType::Execution)
                                {
                                    evaluateNode(graph, candidate, pinOutputs);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        PinValue BlueprintRuntimeComposite::evaluatePinInput(
            RuntimeGraph& graph,
            uint32_t pinId,
            std::unordered_map<uint32_t, PinValue>& pinOutputs)
        {
            // 查找连接到该引脚的 Link
            for (const auto& link : graph.links)
            {
                if (link.endPinId == pinId)
                {
                    auto outIt = pinOutputs.find(link.startPinId);
                    if (outIt != pinOutputs.end())
                    {
                        return outIt->second;
                    }

                    // 检查产出端节点是否是 Math 或 Logic 算子节点
                    for (const auto& node : graph.nodes)
                    {
                        for (const auto& outPin : node.outputs)
                        {
                            if (outPin.id == link.startPinId)
                            {
                                if (node.opCode == "MathAdd")
                                {
                                    float a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asFloat() : 0.0f;
                                    float b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asFloat() : 0.0f;
                                    return PinValue{.type = PinDataType::Float, .data = a + b};
                                }
                                else if (node.opCode == "MathSub")
                                {
                                    float a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asFloat() : 0.0f;
                                    float b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asFloat() : 0.0f;
                                    return PinValue{.type = PinDataType::Float, .data = a - b};
                                }
                                else if (node.opCode == "MathMul")
                                {
                                    float a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asFloat() : 1.0f;
                                    float b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asFloat() : 1.0f;
                                    return PinValue{.type = PinDataType::Float, .data = a * b};
                                }
                                else if (node.opCode == "MathDiv")
                                {
                                    float a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asFloat() : 0.0f;
                                    float b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asFloat() : 1.0f;
                                    return PinValue{.type = PinDataType::Float, .data = (b != 0.0f ? a / b : 0.0f)};
                                }
                                else if (node.opCode == "Compare")
                                {
                                    float a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asFloat() : 0.0f;
                                    float b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asFloat() : 0.0f;
                                    std::string op = node.extraProperty.empty() ? ">" : node.extraProperty;
                                    bool res = false;
                                    if (op == "==") res = (a == b);
                                    else if (op == "!=") res = (a != b);
                                    else if (op == ">") res = (a > b);
                                    else if (op == "<") res = (a < b);
                                    else if (op == ">=") res = (a >= b);
                                    else if (op == "<=") res = (a <= b);
                                    return PinValue{.type = PinDataType::Bool, .data = res};
                                }
                                else if (node.opCode == "LogicAnd")
                                {
                                    bool a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asBool() : true;
                                    bool b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asBool() : true;
                                    return PinValue{.type = PinDataType::Bool, .data = a && b};
                                }
                                else if (node.opCode == "LogicOr")
                                {
                                    bool a = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asBool() : false;
                                    bool b = (node.inputs.size() > 1) ? evaluatePinInput(graph, node.inputs[1].id, pinOutputs).asBool() : false;
                                    return PinValue{.type = PinDataType::Bool, .data = a || b};
                                }
                                else if (node.opCode == "LogicNot")
                                {
                                    bool inVal = (node.inputs.size() > 0) ? evaluatePinInput(graph, node.inputs[0].id, pinOutputs).asBool() : false;
                                    return PinValue{.type = PinDataType::Bool, .data = !inVal};
                                }
                                return outPin.defaultValue;
                            }
                        }
                    }
                }
            }

            // 找不到连线则回退到该输入引脚自身的 defaultValue
            for (const auto& node : graph.nodes)
            {
                for (const auto& inPin : node.inputs)
                {
                    if (inPin.id == pinId)
                    {
                        return inPin.defaultValue;
                    }
                }
            }

            return PinValue{};
        }

    } // namespace Blueprint
} // namespace MQEngine
