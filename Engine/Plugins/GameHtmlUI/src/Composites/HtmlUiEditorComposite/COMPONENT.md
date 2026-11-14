# HtmlUiEditorComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`HtmlUiEditorComposite`（编译产物 `HtmlUiEditorComposite.dll`，激活标签 `["Editor", "ImGui"]`，依赖 `EditorUiComposite`）是 `GameHtmlUI` 插件包中的所见即所得（WYSIWYG）HTML/CSS 界面设计器组件：

1. **2D 所见即所得画布交互（`HtmlUiDesignerCanvas.cpp`）**：支持鼠标直接在画布上点选并拖动（Drag-to-Move）UI 控件位置、拖拽八向控制手柄（8-Way Resize Handles）实时缩放宽高、显示智能对齐吸附参考线（Smart Alignment Guides），并支持从左侧 UI 控件库或底部资产浏览器直接拖入控件/贴图。
2. **DOM 层级树与 CSS 盒模型检视器（`HtmlUiDomInspector.cpp`）**：提供支持拖拽修改父子嵌套关系（Reparenting）的 DOM 节点树、可视化 CSS 盒模型（Margin / Border / Padding / Content）及样式属性检视器。
3. **`.rml` / `.rcss` 双向解析与序列化（`HtmlUiParserSerializer.cpp`）**：将画布拖拽与检视器修改实时格式化回写至 `.rml` DOM 源码与 `.rcss` 样式表，同时支持直接编辑源码反向刷新画布，并完整接入 `EditorUiComposite::CommandStack` 撤销/重做栈与 `BlueprintEditorComposite` 事件绑定。

## 2. 停靠窗口 (Dock Windows)

- `HTML UI 设计器 (RmlUi Visual Editor)`（默认停靠 `EditorDockSlot::Center`，初始隐藏 `defaultOpen = false`，按需从菜单或双击 `.rml`/`.rcss` 资产打开）

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **物理级隔离于纯游戏运行时**：在 `GameApplication` 独立打包时物理级剥离，绝不被游戏运行时加载。
- **不负责运行时 HUD 渲染合成**：游戏内 RmlUi 上下文与 `"RmlUiPass"` 渲染图注入由 `RmlUiComposite` 独立负责。
- **不负责蓝图节点画布与 TS 翻译**：点击 `[+ 绑定蓝图]` 时仅通过跨模块桥接调用 `BlueprintEditorComposite::bindHtmlUiEventNode`，不在本组件内重复实现节点图画布。

## 4. 模块化源码文件结构

- `HtmlUiTypes.h`：DOM 节点结构（`HtmlUiElement`）、CSS 盒模型与弹性布局结构（`HtmlUiBoxModel` / `HtmlUiFlexGridLayout`）、八向缩放手柄枚举与对齐参考线定义。
- `HtmlUiEditorComposite.h` / `HtmlUiEditorComposite.cpp`：组件生命周期、主窗口与顶栏工具条、`HtmlUiElementCommand` 撤销/重做命令封装以及蓝图事件绑定桥接。
- `HtmlUiParserSerializer.cpp`：`.rml` XML 树与 `.rcss` 样式表双向解析器与格式化序列化器。
- `HtmlUiDesignerCanvas.cpp`：2D WYSIWYG 画布渲染、八向缩放手柄碰撞检测、智能对齐辅助线计算与控件库拖拽放置。
- `HtmlUiDomInspector.cpp`：DOM 层级树递归渲染、拖拽重排（Reparenting）、CSS 盒模型可视化器与属性检视面板。
