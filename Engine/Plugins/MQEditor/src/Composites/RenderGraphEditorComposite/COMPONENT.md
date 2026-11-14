# RenderGraphEditorComposite 组件架构说明

## 1. 单一职责 (Single Responsibility)

`RenderGraphEditorComposite` 是 `MQEditor` 插件中负责 **渲染图拓扑编辑（RenderGraph Topology Editor）、`.pgraph` 资产序列化、C++ 管线代码生成与 `GraphViewInsight` 中间纹理实时透视** 的唯一所有者（Exclusive Owner），核心职责涵盖：

1. **交互式 RenderGraph 节点拓扑图编辑 (`PassNode` & `ImageNode`)**：
   - 基于 `ImNodes` 提供可视化 Pass 节点与 Image/ResourceGroup 节点画布。
   - 支持 `PassNode` 输入纹理引脚的自动动态增长（连接一条 `Texture` 输入后自动追加新空闲插槽）、9 路颜色附件 `Target 0..8` 配置（自定义格式、自定义分辨率、直连 OS 窗口 `isWindow`）、`DepthStencil` 深度模板配置与 `Clear`（Color / Depth / Stencil）参数内联编辑。
   - 支持带环路防护的拓扑层级自动布局（`autoLayoutGraph` / `calculateLevel`）。
2. **运行时 `FCT::RenderGraph` 双向同步与热重编译**：
   - 支持从 `RendererSubsystem` 当前活跃 `FCT::RenderGraph` 读取 `FCT::PassDesc` 还原节点图（`Pull From Live RG`）。
   - 支持将当前画布拓扑编译为 `std::vector<FCT::PassDesc>` 并实时注入 `FCT::RenderGraph::recompile()`（`Compile To Live RG`），同时支持 `F11` 一键从窗口直出纯游戏模式还原编辑器离屏渲染拓扑。
3. **`.pgraph` TOML 序列化与 C++ 代码生成**：
   - 通过 `Resource::ResourceLoader` 与 `toml++` 读写 `.pgraph` 渲染图拓扑文件。
   - 一键生成可直接用于 C++ 引擎代码的 `graph->addPass(...)` 声明式管线构建代码片段。
4. **`GraphViewInsight` 实时中间纹理查看器**：
   - 实时枚举并预览当前 `RenderGraph` 各 Pass 输出的中间渲染目标纹理（如 `ShadowMapDepth`、`SceneHDRColor`、`SceneColorTarget`），支持 `RGBA / R / G / B / A` 单通道过滤与曝光度调节。

---

## 2. 注册的停靠窗口与悬浮面板 (Dock Windows)

| 窗口 ID | 默认停靠槽位 (`EditorDockSlot`) | 职责概述 |
| :--- | :--- | :--- |
| `"管线编辑 (Pipeline Editor)"` | `Center` | 顶层双页签：`RenderGraph`（Pass/Image 画布、节点属性与代码生成）与 `管线对象`（PSO 列表、光栅化/深度模板/颜色混合实时编辑与 `.pso` 序列化） |
| `"贴图预览: <TextureName>###TexInsight_<TextureName>"` | 独立可停靠/悬浮窗口 | `GraphViewInsight` 中间渲染目标纹理实时预览（支持通道隔离与曝光滑块） |

---

## 3. 职责边界与非目标 (Non-Goals)

- **严禁包含材质或着色器节点图编辑**：
  - PBR 材质连线图、HLSL/GLSL 着色器源码编辑、Shader 变体反射与热重载 **完全由 `ShaderEditorComposite` 独占负责**。
  - `RenderGraphEditorComposite` 仅操作 Pass 级渲染图拓扑（`PassNode` 与 `ImageNode`），不处理着色器指令级节点。

---

## 4. 模块化文件拆分说明 (File Breakdown)

| 文件名 | 行数规模 | 核心内容 |
| :--- | :--- | :--- |
| `RenderGraphEditorTypes.h` | ~200 行 | `NodeType`、`PinKind`、`PassTargetDesc`、`DepthStencilDesc`、`PassNode`、`ImageNode`、`PinInfo`、`LinkInfo`、`PassResourceStat`、`BarrierTransitionView`、`TextureInsightSettings` 定义 |
| `GraphViewInsight.h` / `GraphViewInsight.cpp` | ~40 / ~115 行 | `GraphViewInsight` 实时中间纹理悬浮预览窗口渲染、RGBA 通道过滤与曝光度滑块控制 |
| `RenderGraphEditorComposite.h` / `RenderGraphEditorComposite.cpp` | ~185 / ~420 行 | Composite 生命周期（`onLoad` / `onInit` / `onTick` / `onUnload`）、`管线编辑` Dock 窗口注册、`RenderGraph` 页签工具栏、F11 还原与节点属性检视 |
| `PipelineObjectEditor.cpp` | ~470 行 | `管线对象` 页签：PSO 列表管理、光栅化/深度模板/颜色混合状态编辑、与 `RendererSubsystem` 实时状态同步及 `.pso` TOML 读写 |
| `RenderGraphEditorNodes.cpp` | ~515 行 | `PassNode` / `ImageNode` 创建与删除、动态自动增长纹理引脚、拓扑层级自动布局（`autoLayoutGraph`）及与 `FCT::PassDesc` 的双向转换 |
| `RenderGraphEditorCanvas.cpp` | ~385 行 | `ImNodes` 上下文管理、画布绘制、连线创建/拖拽断开事件处理、`PassNode` 与 `ImageNode` 节点内联控件渲染 |
| `RenderGraphEditorSerializer.cpp` | ~425 行 | `.pgraph` TOML 文件读写（基于 `ResourceLoader`）与 `graph->addPass(...)` C++ 代码生成器 |
