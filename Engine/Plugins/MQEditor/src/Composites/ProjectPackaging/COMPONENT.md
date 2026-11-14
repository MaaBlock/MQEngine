# ProjectPackaging 组件说明

## 1. 单一职责定位 (Single Responsibility)

`ProjectPackaging`（编译产物 `ProjectPackaging.dll`，激活标签 `["Editor", "ImGui"]`）是 `MQEditor` 插件包中唯一负责独立游戏工程 4 阶段无损打包导出与发布目录校验的组件：

1. **阶段 1 — 工程依赖遍历与配置扫描**：校验 `project.mq` 主配置、初始场景与启用插件，定位运行时构建二进制源目录。
2. **阶段 2 — 基于标签的动态库物理级裁剪**：自动剥离所有带 `"Editor"` / `"Test"` / `"Diagnostic"` 属性的编辑器 DLL，仅将 `Game.exe`（重命名为 `<ProjectName>.exe`）、`Engine.dll` 与纯运行时插件（`RmlUiComposite.dll`、`BlueprintRuntimeComposite.dll`、`TerrainRuntimeComposite.dll` 等）注入 `Dist/<ProjectName>/`。
3. **阶段 3 — 资产烘焙与 `.mqpak` 归档**：递归复制并固化 `Assets/` 与 `Scenes/` 目录，可选生成 `Content.mqpak` 虚拟文件系统包。
4. **阶段 4 — 独立发布清单与完整性自检**：生成独立 `project.mq` 与 `manifest.toml`（配置零 ImGui 开销的纯游戏直出 RenderGraph），并对关键运行产物执行完整性校验。

## 2. 停靠窗口与菜单挂载 (Dock Windows & Menu Actions)

- 停靠窗口：`"工程独立发布与打包 (Package Project)"`（默认槽位：`Center`，`defaultOpen = false`）
- 菜单挂载：`"文件"` -> `"打包工程 (Package Project...)"`

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止管理日常资产浏览或拖拽导入**：由 `AssetBrowserComposite` 负责。
- **禁止管理场景编辑、着色器节点图或渲染图拓扑编辑**：分别由 `SceneViewerComposite`、`ShaderEditorComposite` 与 `RenderGraphEditorComposite` 负责。

## 4. 模块化源码文件结构

- `ProjectPackagingTypes.h`：`PackageTargetPlatform`、`PackageStage`、`PakEntryHeader`、`PackageBuildConfig` 与 `PackageJobState` 定义。
- `ProjectPackaging.h` / `ProjectPackaging.cpp`：组件生命周期、菜单动作与停靠窗口注册、打包配置面板 UI 与实时构建日志查看器。
- `ProjectPackagingPipeline.cpp`：4 阶段打包流水线执行逻辑（依赖扫描、Editor DLL 剥离、资产/场景固化、`.mqpak` 归档与独立发行包完整性校验）。
