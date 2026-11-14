# RmlUiComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`RmlUiComposite`（编译产物 `RmlUiComposite.dll`，激活标签 `["Runtime", "Game"]`）是 `GameHtmlUI` 插件包中的运行时 HTML/CSS 界面渲染与桥接组件：

1. **RmlUi 6.0 后端接口封装**：实现 `MQRmlSystemInterface`（计时、剪贴板、spdlog 日志转发）、`MQRmlFileInterface`（对接引擎 `ResourceLoader`）与 `MQRmlRenderInterface`（2D 几何编译、Scissor 裁剪与绘制指令收集）。
2. **运行时文档与输入生命周期**：初始化 `Rml::Context`，支持从磁盘或内存加载/热重载 `.rml` 与 `.rcss` 文档，向 `Host::LogicHost` 注册逻辑更新 Ticker 并转发 `Domain::HID` 鼠标与键盘事件。
3. **RenderGraph `"RmlUiPass"` 挂载与 DOM 动态操作**：向 `RendererSubsystem` 注册 `"RmlUiPass"` 通道，并导出 `setElementInnerText`、`setElementStyleProperty`、`toggleElementClass` 等公开 DOM 操作接口。

## 2. 停靠窗口 (Dock Windows)

- 无（纯游戏运行时 UI 渲染组件，不注册任何 ImGui Dock 窗口）。

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **零编辑器与 ImGui 依赖**：在 `GameApplication` 独立打包发行时单独加载，绝不链接或依赖 `EditorBase` / `ImGui`。
- **不负责 WYSIWYG 拖拽设计器界面**：可视化拖拽 UI 控件、CSS 盒模型检视与 DOM 树重排由同插件包下的 `HtmlUiEditorComposite` 独立负责。

## 4. 模块化源码文件结构

- `RmlUiBackendInterfaces.h` / `RmlUiBackendInterfaces.cpp`：`MQRmlSystemInterface`、`MQRmlFileInterface` 与 `MQRmlRenderInterface` 实现。
- `RmlUiComposite.h` / `RmlUiComposite.cpp`：组件生命周期、DOM 动态操作、文档管理、HID 事件路由与 `"RmlUiPass"` RenderGraph 集成。
