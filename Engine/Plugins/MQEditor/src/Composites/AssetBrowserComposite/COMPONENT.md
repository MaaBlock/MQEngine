# AssetBrowserComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`AssetBrowserComposite`（编译产物 `AssetBrowserComposite.dll`，激活标签 `["Editor", "ImGui"]`）是 `MQEditor` 插件包中唯一负责项目内容资产浏览、外部文件拖拽导入与 `.mqmeta` 伴生元数据管理的组件：

1. **目录树与卡片网格资产浏览**：提供双栏式文件夹快捷导航树、前进/后退历史栈、实时关键词过滤，以及可调尺寸的卡片网格/列表双视图切换。
2. **外部 OS 资源管理器无锁拖拽导入管道**：基于 `FCT::EventHandler::onFileDrop` 与 `LockFreeSpscQueue` 无锁环形队列接收外部拖入文件；针对 3D 网格自动调用 `FCT::ModelLoader`（Assimp）解析顶点/三角面/骨骼并生成 `model.mqmeta` 伴生侧车，针对纹理图像调用 `AsyncTextureUploader` 异步解码上传，并接入 `CommandStack` 撤销/重做栈。
3. **跨面板 Drag-and-Drop 资产分发**：发出 `"MQ_ASSET_PAYLOAD"` 拖拽载荷与 `SelectAssetSignal` 信号（支持拖入 3D 视口实例化或双击打开对应编辑器），并接收 `"MQ_ENTITY_PAYLOAD"` 将场景实体逆向导出为 `.mqscene` 预制体。
4. **GPU 资源表与活跃 Composite 拓扑监控**：实时展示 `GpuResourceTable` 活跃句柄数与显存占用，以及当前激活的 Composite 执行拓扑。

## 2. 停靠窗口 (Dock Windows)

- `"内容资产浏览器 (Content Browser)"`（默认槽位：`Bottom`，`defaultOpen = true`）

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止内嵌 3D 场景视口、实体大纲树或 ECS 组件反射检视器**：由 `SceneViewerComposite` 独立负责。
- **禁止内嵌着色器节点编辑、渲染图拓扑编辑或 NTC 离线烘焙逻辑**：分别由 `ShaderEditorComposite`、`RenderGraphEditorComposite` 与 `NtcBakerComposite` 负责。

## 4. 模块化源码文件结构

- `AssetBrowserTypes.h`：`AssetCategory`、`AssetMetadataSidecar`、`DirectoryTreeNode`、`AssetEntry` 与 `LockFreeSpscQueue` 定义。
- `AssetBrowserComposite.h` / `AssetBrowserComposite.cpp`：组件生命周期、目录扫描、OS 文件拖拽监听、Assimp 模型/异步纹理导入管线与 `.mqmeta` 侧车生成。
- `AssetBrowserPanels.cpp`：顶部导航栏、左侧目录树与 GPU 资源统计、右侧卡片网格/列表视图、跨面板拖拽源与导入/重命名/删除模态弹窗。
