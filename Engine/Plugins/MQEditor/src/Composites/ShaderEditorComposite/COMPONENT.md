# ShaderEditorComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`ShaderEditorComposite`（编译产物 `ShaderEditorComposite.dll`，激活标签 `["Editor", "ImGui"]`）是 `MQEditor` 插件包中唯一负责着色器可视化节点图、HLSL 代码生成/实时编译与 PBR 材质球预览的组件：

1. **ImNodes 可视化着色器节点图编辑 (`ShaderGraph`)**：支持纹理采样、颜色常量、数学运算、向量操作、系统 Uniform 插槽绑定与 `PBR Master Output` 节点连线编辑、预设材质模板切换及节点属性检视。
2. **Kahn 拓扑排序与 HLSL 代码生成**：执行有向无环图 (DAG) 环路检测与拓扑序求值，自动转译为 HLSL 6.0 像素着色器源码。
3. **Shaderc SPIR-V 实时编译与热重载**：对接 `FCT::Context` 的 `ShaderGenerator` 与 `ShaderCompiler` 执行 HLSL 到 SPIR-V 字节码实时编译，并将编译产物热更新至 `RendererSubsystem`。
4. **PBR 材质球预览与资产持久化**：提供基于 Cook-Torrance GGX BRDF 的交互式 PBR 材质球预览，以及 `.mqshader` / `.sgedt` TOML 节点图资产序列化。

## 2. 停靠窗口 (Dock Windows)

- `"着色器与材质编辑器 (Shader & Material Editor)"`（默认槽位：`Center`，`defaultOpen = false`）

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止包含 RenderGraph 渲染图拓扑编辑或离屏 RT 中间纹理检视 (`GraphViewInsight`)**：`RenderGraph` 拓扑构建、Pass/Image 节点连线、自动排版、`.pgraph` 序列化、F11 纯游戏直出切换与中间渲染目标预览由 `RenderGraphEditorComposite` 唯一承载。
- **禁止管理文件浏览器或通用场景实体检视**：资产浏览由 `AssetBrowserComposite` 负责，ECS 场景实体检视由 `SceneViewerComposite` 负责。

## 4. 模块化源码文件结构

- `ShaderEditorTypes.h`：`ShaderNodeCategory`、`ShaderPinType`、`ShaderPin`、`ShaderGraphNode`、`ShaderGraphLink` 与 `PbrMaterialPreviewParams` 数据结构定义。
- `ShaderEditorComposite.h` / `ShaderEditorComposite.cpp`：组件生命周期、`着色器与材质编辑器 (Shader & Material Editor)` 停靠窗口注册、预设模板工具栏、节点属性检视器与 HLSL 源码编辑页。
- `ShaderNodeGraphCanvas.cpp`：ImNodes 节点图画布渲染、引脚连线增删与右键上下文菜单（含系统 Uniform 插槽与 Snippet 搜索）。
- `ShaderCodeGenAndPreview.cpp`：Kahn 拓扑排序 DAG 转译 HLSL、Shaderc SPIR-V 实时编译、Cook-Torrance GGX 材质球预览及 `.mqshader` / `.sgedt` 序列化。
