# BlueprintEditorComposite 组件说明

## 1. 核心定位与单一职责 (Single Responsibility)

`BlueprintEditorComposite`（隶属 `Blueprint` 插件，标签 `["Editor", "ImGui"]`，独立编译为 `BlueprintEditorComposite.dll`）是 MQEngine 的**可视化逻辑蓝图与 TypeScript 源码双向无损 AST 翻译编辑器**：

1. **ImNodes 可视化逻辑画布**：管理 `蓝图可视化编辑器 (Blueprint Editor)` Dock 窗口，提供节点连线、内联引脚默认值编辑、右下角小地图（MiniMap）以及带分类搜索过滤的右键添加节点上下文菜单。
2. **TypeScript 双向无损 AST 互转**：将蓝图节点与连线导出为包含 `@bp-node`/`@bp-pin`/`@bp-link` 结构化元数据的可执行 TypeScript 源码（`translateGraphToTypeScript`），并支持从带元数据或手写 TS 源码反向解析还原节点图（`parseTypeScriptToGraph`）。
3. **跨插件拖拽与事件绑定**：接收来自 `HtmlUiEditorComposite` 的 `HTML_UI_DOM_ELEMENT` 载荷与 `bindHtmlUiEventNode` 绑定调用，以及来自 `AssetBrowserComposite` 的资产拖拽载荷，自动生成事件或资源节点。
4. **Undo/Redo 快照与运行时热同步**：通过 `BlueprintGraphCommand` 对接 `EditorUiComposite` 全局命令栈，并支持一键将当前节点图同步至 `BlueprintRuntimeComposite`。

---

## 2. 边界与明确非目标 (Non-Goals / Forbidden Overlaps)

- **不负责运行时每帧事件求值**：实际运行时的 `OnStart`/`OnUpdate`/`OnUIClick` 拓扑执行由 `BlueprintRuntimeComposite` 负责。
- **不编辑材质着色器或 RenderGraph 节点**：材质 Slang 代码生成由 `ShaderEditorComposite` 负责，渲染管线 Pass 拓扑由 `RenderGraphEditorComposite` 负责。
- **不直接渲染 HTML/CSS 盒模型画布**：HTML/CSS 可视化设计由 `HtmlUiEditorComposite` 负责。

---

## 3. 模块化文件拆分结构

| 文件名 | 职责说明 |
| :--- | :--- |
| `BlueprintEditorTypes.h` | `EditorNode`、`EditorLink` 与 `GraphSnapshot` 数据结构定义 |
| `BlueprintEditorComposite.h` | `BlueprintEditorComposite` 类声明、双向翻译与节点工厂接口 |
| `BlueprintEditorComposite.cpp` | Composite 生命周期、顶层工具栏、快照 Undo/Redo 命令、节点创建工厂、拖拽接收与运行时同步 |
| `BlueprintEditorCanvas.cpp` | ImNodes 上下文管理、节点画布与内联引脚渲染、连线增删交互、小地图与右键搜索菜单 |
| `BlueprintTsAstTranslator.cpp` | `translateGraphToTypeScript`、`parseTypeScriptToGraph` 与 `autoLayoutGraph` 实现 |
