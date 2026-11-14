# EditorUiComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`EditorUiComposite`（编译产物 `EditorUiComposite.dll`，激活标签 `["Editor", "ImGui"]`，依赖 `ImGuiComposite`）是 `EditorBase` 插件包中的编辑器排版框架与基础服务总线：

1. **主窗口 DockSpace 与菜单栏调度**：构建全屏 `MQEngine_Master_DockSpace`，按 `Center` / `Left` / `Right` / `Bottom` / `Top` 五大槽位切分初始停靠布局，支持多套工作区预设（`applyWorkspacePreset`）以及扩展菜单项动态注册（`registerMenuAction`）。
2. **强类型 UI 状态/信号总线（`UiContext`）与命令栈（`CommandStack`）**：提供基于 C++20 Concept 的视图状态同步与信号分发中心，以及支持泛型与类型擦除 ECS 组件修改的全局 `Undo / Redo` 命令历史栈。
3. **多语言本地化（i18n）与基础诊断 UI**：维护中英文双语词典（`tr` / `trKey` / `localizeLabel`），并内置统一的**控制台日志窗口**、**偏好设置弹窗**、**右下角浮动 Toast 通知卡片**及基础场景/工程设置持久化入口。

## 2. 停靠窗口 (Dock Windows)

- `控制台日志 (Console Log)`（默认停靠 `EditorDockSlot::Bottom`）
- 模态/浮动窗口：`偏好设置###EditorPreferencesWindow`

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止实现帧性能剖析器面板（Profiler）**：CPU/GPU 时间轴与性能剖析由 `MQEditor::TracyProfilerComposite` 独立负责，避免重复注册 Profiler 面板。
- **禁止实现 3D 场景视口、层级树或实体属性检视器**：均由 `MQEditor::SceneViewerComposite` 独立负责。
- **禁止实现资产浏览器、着色器编辑器、渲染图编辑器、回放时间轴或打包工具**：分别由 `MQEditor` 下对应的专属 Composite 独立负责。

## 4. 模块化源码文件结构

- `EditorUiTypes.h`：停靠槽位枚举（`EditorDockSlot`）、工作区预设枚举（`EditorWorkspacePreset`）、停靠窗口与菜单项结构体定义。
- `UiSignals.h` / `UiContext.h` / `CommandStack.h` / `ToastNotification.h`：强类型信号契约、视图状态容器、通用撤销/重做命令栈与通知数据结构。
- `EditorUiComposite.h` / `EditorUiComposite.cpp`：组件生命周期（`onLoad` / `onInit` / `onTick` / `onUnload`）、窗口与菜单注册、无锁日志队列刷新及中英文多语言字典管理。
- `EditorUiLayout.cpp`：`renderDockSpace`、`buildInitialDockLayout`、`applyWorkspacePreset`、`renderMainMenuBar` 与 `renderRegisteredPanels`。
- `EditorUiPanels.cpp`：`renderLogWindow`（含报错行源码跳转）、`renderPreferencesWindow`（含外部代码编辑器配置分区）、`renderToastNotifications` 以及基础持久化入口。
- `ExternalCodeEditorService.h` / `ExternalCodeEditorService.cpp`：Windows 代码编辑器自动探测（CLion / Rider / VS Code / Visual Studio / Custom）、偏好读写与 `CreateProcessW` 异步跳转打开。
- `ProjectIdeConfigGenerator.h` / `ProjectIdeConfigGenerator.cpp`：自动生成与同步外部工程 `CMakePresets.json`、`.idea/cmake.xml`、`.run/*.run.xml` 与 `.gitignore`。
- `ProjectLauncherService.h` / `ProjectLauncherService.cpp` 与 `EditorUiProjectModals.cpp`：工程目录选择、最近工程持久化、`Templates/` 模板工程实例化、`Editor.exe --project="..."` 进程拉起及文件/工程弹窗。
