/**
 * @file BlueprintRuntimeComposite.h
 * @brief 可视化蓝图运行时执行图与脚本桥接 Composite（tags: [Runtime, Game]，独立编译为 BlueprintRuntimeComposite.dll）
 */
#ifndef MQENGINE_BLUEPRINT_RUNTIME_COMPOSITE_H
#define MQENGINE_BLUEPRINT_RUNTIME_COMPOSITE_H

#include "BlueprintRuntimeTypes.h"
#include <MQEngine/asset/interface/Composite.h>

#include <functional>

namespace MQEngine
{
    namespace Blueprint
    {
        /**
         * @brief 蓝图运行时 Composite：
         *        负责蓝图执行图注册管理、事件分发、DAG 节点拓扑求值、变量表管理、TypeScript 类型声明 (.d.ts) 导出与 PIE 探针调试
         */
        class BLUEPRINT_RUNTIME_COMPOSITE_API BlueprintRuntimeComposite : public Asset::Composite
        {
        public:
            BlueprintRuntimeComposite();
            ~BlueprintRuntimeComposite() override;

            BlueprintRuntimeComposite(const BlueprintRuntimeComposite&) = delete;
            BlueprintRuntimeComposite& operator=(const BlueprintRuntimeComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "BlueprintRuntimeComposite"; }
            std::string getPluginName() const override { return "Blueprint"; }
            std::vector<std::string> getDependencies() const override { return {}; }
            std::vector<std::string> getRequiredTags() const override { return {"Runtime"}; }

            // ==================== 图管理与事件分发 ====================

            void registerGraph(RuntimeGraph graph);
            void unregisterGraph(const std::string& name);
            const RuntimeGraph* getGraph(const std::string& name) const noexcept;
            RuntimeGraph* getMutableGraph(const std::string& name) noexcept;

            Status executeEvent(const std::string& graphName, const std::string& eventName);
            Status executeUiEvent(const std::string& elementId, const std::string& eventType, float value = 0.0f);
            void executeFlow(RuntimeGraph& graph, uint32_t startNodeId);

            // ==================== 变量表访问 ====================

            void setGlobalVariable(const std::string& name, PinValue value);
            PinValue getGlobalVariable(const std::string& name) const;
            bool hasGlobalVariable(const std::string& name) const noexcept;

            // ==================== PIE 调试与探针 ====================

            const std::vector<ExecutionPulseProbe>& getRecentProbes() const noexcept { return m_recentProbes; }
            void clearProbes() noexcept { m_recentProbes.clear(); }
            void addProbe(uint32_t nodeId, uint32_t pinId, const std::string& msg);

            // ==================== TypeScript 绑定与 .d.ts 导出 ====================

            std::string generateTypeScriptDeclarations() const;
            Status exportTypeScriptDeclarations(const std::string& diskPath);

        private:
            void evaluateNode(RuntimeGraph& graph, const RuntimeNode& node, std::unordered_map<uint32_t, PinValue>& pinOutputs);
            PinValue evaluatePinInput(RuntimeGraph& graph, uint32_t pinId, std::unordered_map<uint32_t, PinValue>& pinOutputs);

        private:
            std::unordered_map<std::string, RuntimeGraph> m_graphs;
            std::unordered_map<std::string, PinValue> m_globals;
            std::vector<ExecutionPulseProbe> m_recentProbes;
            float m_accumulatedTime = 0.0f;
            uint32_t m_nextLinkId = 1000;
        };

    } // namespace Blueprint
} // namespace MQEngine

#endif // MQENGINE_BLUEPRINT_RUNTIME_COMPOSITE_H
