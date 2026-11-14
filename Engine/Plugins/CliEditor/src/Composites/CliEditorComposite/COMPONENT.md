# CliEditorComposite 组件描述与职责边界契约

## 1. 基本元信息
- **组件名称**：`CliEditorComposite`
- **所属插件包**：`CliEditor` (`Engine/Plugins/CliEditor`)
- **宿主可执行程序**：`CliEditor.exe` (`Applications/CliEditor`)
- **激活标签 (`tags`)**：`["Runtime", "CliEditor"]`
- **依赖组件 (`dependencies`)**：`[]`（独立无头运行，不依赖 `ImGuiComposite` 或 `EditorUiComposite`）

## 2. 单一职责 (Single Responsibility)
提供 **4 级渐进式披露（Progressive Disclosure）引擎命令行与 AI 自动化编辑器协议**，确保图形化编辑器（`Editor.exe`）与引擎核心子系统能完成的一切操作，AI Agent 与命令行脚本均可通过 `CliEditor.exe` 100% 对等执行，并能通过 `render.capture` 抓取 Vulkan 渲染结果图片形成 **“执行操作 → 抓帧看画面 → 继续微调”** 的视觉闭环。

### 四级渐进式披露协议
1. **Level 0（能力域发现）**：`CliEditor.exe --headless --help`（列出 13 个顶层能力域与所属模块，不倾倒冗长细节）
2. **Level 1（域内动作列表）**：`CliEditor.exe --headless --domain=<domain>`（展开指定能力域下的所有 `<domain>.<action>` 命令）
3. **Level 2（参数 Schema 与示例）**：`CliEditor.exe --headless --schema=<domain.action>`（展示参数类型、默认值、必填项与调用示例）
4. **Level 3（单命令 / 分号流水线 / 脚本执行）**：`CliEditor.exe --headless --exec="<cmd1>; <cmd2>" [--json] [--out=<path>]`

## 3. 明确非目标与禁止重叠边界 (Non-Goals)
- **禁止包含任何 ImGui GUI 窗口代码**：不注册任何 Dock Window，与 `EditorBase` / `MQEditor` 的可视化面板完全解耦。
- **禁止将业务逻辑下沉到 `Resource` 层**：严格作为顶层 `Composite` 插件运行在 `Resource -> Host -> Asset -> Domain -> Composite` 架构最上层。

## 4. 模块化多文件源码结构
| 文件名 | 职责描述 |
| :--- | :--- |
| `CliEditorTypes.h` | `CliParamSpec`、`CliCommandSpec`、`CliDomainSpec`、`CliExecutionResult` 数据结构与 JSON/向量解析工具 |
| `CliEditorComposite.h` / `CliEditorComposite.cpp` | Composite 生命周期、命令行参数解析、Level 0/1/2 渐进式披露格式化与 Level 3 流水线调度器 |
| `CliSceneAndCharacterCommands.cpp` | `scene.*`（ECS 实体/相机/材质操作）与 `character.*`（18 部位参数化 3D 人形假人/机甲生成器及 Tripo3D/本地 AI 桥接） |
| `CliRenderAndShaderCommands.cpp` | `render.*`（Vulkan GPU 抓帧截图 `.png`、遥测统计、`.pgraph` 导出）与 `shader.*`（HLSL 实时热编译与预设） |
| `CliAssetAndWorldCommands.cpp` | `plugin.*`（插件与职责边界审计）、`asset.*`、`terrain.*`、`blueprint.*`、`ui.*`、`replay.*`、`ntc.*`、`package.*`、`profile.*` |
