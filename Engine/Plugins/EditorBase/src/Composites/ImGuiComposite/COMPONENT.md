# ImGuiComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`ImGuiComposite`（编译产物 `ImGuiComposite.dll`，激活标签 `["Runtime", "ImGui"]`）是 `EditorBase` 插件包中的底层 ImGui 渲染与上下文桥接组件：

1. **Vulkan ImGui RenderGraph Pass 集成**：对接 `Host::UIHost` 窗口与 `Host::RenderHost` RHI 上下文，向 `Domain::Renderer::RendererSubsystem` 注册 `"ImguiPass"` 渲染图通道与 `"syncImgui"` 同步节点，并在帧编译后触发保活图像回调（`KeepImageCallback`）。
2. **多视口与跨 DLL 上下文同步**：统一管理 `FCT::ImguiContext` 与 `ImNodesContext` 生命周期，提供 `syncCallerContext()` 模板方法以跨 DLL 边界同步调用方模块的静态 `ImGuiContext`、内存分配器与 `ImNodesContext`。
3. **字体、主题与 DPI 缩放桥接（`ImGuiVulkanBridge`）**：负责中文字体集启用、高 DPI 缩放系数计算、现代深色主题配色（`applyDarkTheme`）以及 RHI 纹理描述符缓存管理。

## 2. 停靠窗口 (Dock Windows)

- 无（纯底层渲染与上下文桥接组件，不注册任何 Dock 窗口）。

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **零业务面板**：绝不包含任何编辑器业务 UI 面板（如控制台日志、场景视口、资产浏览器、性能剖析器等）。
- **不负责 DockSpace 与菜单栏排版**：主窗口 `DockSpace` 划分、菜单栏注入、工作区预设及 `Undo/Redo` 命令栈由下游 `EditorUiComposite` 负责。

## 4. 模块化源码文件结构

- `ImGuiComposite.h` / `ImGuiComposite.cpp`：组件生命周期（`onLoad` / `onInit` / `onTick` / `onUnload`）、跨 DLL 上下文同步与 RenderGraph `"ImguiPass"` 挂载。
- `ImGuiVulkanBridge.h` / `ImGuiVulkanBridge.cpp`：中文字体构建、现代深色主题样式配置、DPI 缩放适配与纹理描述符缓存管理。
