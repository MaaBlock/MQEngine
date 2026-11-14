# SceneViewerComposite 组件架构说明

## 1. 单一职责 (Single Responsibility)

`SceneViewerComposite` 是 `MQEditor` 插件中负责 **3D 场景视口交互、场景层级大纲树与实体组件属性检视** 的唯一所有者（Exclusive Owner），核心职责涵盖：

1. **3D 视口渲染与交互 (`3D 视口 (Scene Viewport)`)**：
   - 采样并显示 `RenderGraph` 输出的离屏渲染目标纹理 `SceneColorTarget`。
   - 提供鼠标右键 + `WASDQE` 自由飞行漫游、鼠标中键平移、滚轮推拉缩放、`F` 键聚焦选中实体。
   - 绘制三维空间地面网格、世界原点坐标轴、视口右上角相机朝向指示器（Corner Orientation Gizmo）及实体三维 AABB 包围盒。
   - 实现 `W / E / R` 三维位移/旋转/缩放 Gizmo 轴向拖拽操纵，并在释放鼠标时自动将 `ComponentModifyCommand<LocalTransform>` 推入 `EditorCommandStack` 支持 `Ctrl+Z / Ctrl+Y` 撤销重做。
   - 支持基于 `Domain::Scene::SpatialGraph` BVH 射线求交与屏幕投影兜底的 3D 视口点击拾取，以及从 `AssetBrowserComposite` 拖拽纹理/3D 模型资产（`.obj` / `.gltf` / `.glb` / `.fbx`）至视口内落点自动生成实体。
2. **场景层级大纲 (`层级大纲 (Scene Hierarchy)`)**：
   - 渲染场景实体父子层级树（Outliner），支持关键字过滤、右键创建/复制/删除实体、拖拽重设父子关系（`HierarchyRelation`）以及 `SceneTrunk` 子场景分块流式挂载/卸载。
3. **实体检视器 (`实体检视器 (Inspector)`)**：
   - 检视并编辑选中实体的基础属性（Transform、Camera、Mesh & PBR Material、DirectionalLight）。
   - 基于 **C++20 `Boost.PFR` 零宏聚合体编译期反射** 与 **`Resource::SchemaTable` 运行时类型擦除描述符**，自动为所有已注册的引擎内置组件及外部插件动态注册组件生成 ImGui 属性编辑控件，并自动接入 `DynamicComponentModifyCommand` / `DynamicComponentAddRemoveCommand` 实现无侵入式 Undo/Redo。
4. **相机控制 (`相机控制 (Camera Control)`)**：
   - 精确调节编辑器主视口相机的空间坐标、观察焦点、姿态角、FOV 视场角、近/远裁剪面及漫游移速。

---

## 2. 注册的停靠窗口 (Dock Windows)

| 窗口 ID | 默认停靠槽位 (`EditorDockSlot`) | 职责概述 |
| :--- | :--- | :--- |
| `"3D 视口 (Scene Viewport)"` | `Center` | 离屏场景渲染视口、3D Gizmo 操纵、射线拾取与资产拖拽落点实例化 |
| `"层级大纲 (Scene Hierarchy)"` | `Left` | 场景实体树状大纲、父子层级拖拽绑定与 `SceneTrunk` 分块流式加载 |
| `"实体检视器 (Inspector)"` | `Right` | 实体属性编辑与基于 `Boost.PFR` / `SchemaTable` 的零宏通用 ECS 组件反射检视 |
| `"相机控制 (Camera Control)"` | `Right` | 编辑器漫游相机参数面板与视角重置 |

---

## 3. 职责边界与非目标 (Non-Goals)

- **严禁包含 ECS 时间轴与回放面板 (`时间轴与回放 (ECS Replay)`)**：
  - ECS 关键帧录像、增量快照回放、时间旅行拖拽（Time-Travel Scrubbing）与前后帧状态 Diff 对比 **完全由 `ReplayTimelineComposite` 独占负责**。
  - `SceneViewerComposite` 严禁注册 `"时间轴与回放 (ECS Replay)"` 停靠窗口，仅在 3D 视口顶部工具栏提供快捷的 PIE 运行/停止触发按钮。
- **不负责材质/着色器节点图或渲染图拓扑编辑**：分别由 `ShaderEditorComposite` 与 `RenderGraphEditorComposite` 独占负责。

---

## 4. 模块化文件拆分说明 (File Breakdown)

| 文件名 | 行数规模 | 核心内容 |
| :--- | :--- | :--- |
| `SceneViewerTypes.h` | ~110 行 | `EGizmoMode`、`EGizmoAxis`、`ViewportCameraState`、`GizmoInteractionState` 与 `SceneEntityDesc` 数据结构定义 |
| `ScenePrimitiveMeshes.h` / `ScenePrimitiveMeshes.cpp` | ~90 / ~305 行 | 程序化几何基元网格生成器（Cube / Plane / UV Sphere / Skybox）、默认场景预设实体构建与基础视口 HLSL 着色器源码 |
| `SceneViewerComposite.h` / `SceneViewerComposite.cpp` | ~140 / ~380 行 | Composite 生命周期（`onLoad` / `onInit` / `onTick` / `onUnload`）、4 个 Dock 窗口注册、相机矩阵/射线计算与 `Domain::Scene` ECS 实体同步 |
| `SceneViewportPanel.cpp` | ~590 行 | `renderViewport()`、`renderViewportToolbar()`、相机漫游输入控制、三维网格与方向指示器、3D Gizmo 拖拽与 Undo 命令提交、BVH 射线拾取及资产拖拽实例化 |
| `SceneHierarchyPanel.cpp` | ~220 行 | `renderHierarchy()`、`renderHierarchyNode()` 递归树节点、父子拖拽重绑定与 `renderTrunkStreamingSection()` 子场景分块流式管理 |
| `SceneInspectorPanel.cpp` | ~680 行 | `renderInspector()`、`renderCameraControl()`、`Boost.PFR` 编译期反射与 `SchemaTable` 动态类型擦除通用 ECS 组件检视器 |
