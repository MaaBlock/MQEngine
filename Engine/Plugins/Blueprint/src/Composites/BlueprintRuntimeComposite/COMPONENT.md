# BlueprintRuntimeComposite 组件说明

## 1. 核心定位与单一职责 (Single Responsibility)

`BlueprintRuntimeComposite`（隶属 `Blueprint` 插件，标签 `["Runtime"]`，独立编译为 `BlueprintRuntimeComposite.dll`）是 MQEngine 的**可视化蓝图运行时 DAG 求值引擎与脚本声明桥接层**：

1. **运行时执行图注册表与变量表管理**：管理所有已注册的 `RuntimeGraph` 实例、全局变量表（`m_globals`）与图局部变量表。
2. **生命周期与 UI 事件驱动**：在 `onInit` 阶段触发 `OnStart` 事件流，在 `onTick` 阶段注入 `dt` 并驱动 `OnUpdate` 事件流，并提供 `executeUiEvent` 接收来自 `RmlUiComposite` 的 DOM 交互事件（`OnUIClick` / `OnUIInput`）。
3. **DAG 拓扑节点求值器**：执行控制流分支（`Branch`）、数学/逻辑算子（`MathAdd`、`MathSub`、`MathMul`、`MathDiv`、`Compare`、`LogicAnd`、`LogicOr`、`LogicNot`）、ECS 实体组件读写（`GetComponent`、`SetComponent`）、HTML UI 属性变更（`SetUIInnerText`、`SetStyleProperty`、`ToggleClass`）以及场景模型加载（`OpenModel`、`GetStartupFilePath`）。
4. **TypeScript `.d.ts` 类型声明生成与 PIE 调试探针**：导出 `mqengine.d.ts` 强类型声明文件，并记录执行脉冲探针（`ExecutionPulseProbe`）供编辑器调试。

---

## 2. 边界与明确非目标 (Non-Goals / Forbidden Overlaps)

- **严禁依赖 ImGui 或 ImNodes**：`BlueprintRuntimeComposite` 是纯运行时组件，在独立游戏打包（Game Standalone）时加载，**绝不包含**任何编辑器节点画布渲染代码。
- **不负责蓝图编辑器画布与双向 AST 翻译 UI**：可视化节点连线与 TypeScript 源码互转界面完全由 `BlueprintEditorComposite` 负责。
- **不处理着色器图或 RenderGraph 节点**：材质着色器节点由 `ShaderEditorComposite` 负责，渲染管线 DAG 拓扑由 `RenderGraphEditorComposite` 负责。

---

## 3. 模块化文件拆分结构

| 文件名 | 职责说明 |
| :--- | :--- |
| `BlueprintRuntimeTypes.h` | `PinDataType`、`PinValue` 多态变体、`RuntimePin`、`RuntimeNode`、`RuntimeLink`、`RuntimeGraph` 与 `ExecutionPulseProbe` 定义 |
| `BlueprintRuntimeComposite.h` | `BlueprintRuntimeComposite` 类声明与公共求值/变量/声明导出接口 |
| `BlueprintRuntimeComposite.cpp` | Composite 生命周期、默认图构建、图注册表、事件派发、全局变量管理与 `mqengine.d.ts` 生成 |
| `BlueprintVmEvaluator.cpp` | `evaluateNode` 与 `evaluatePinInput`：DAG 控制流推进与数学/逻辑/ECS/UI 节点求值实现 |
