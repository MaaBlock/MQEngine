# TerrainRuntimeComposite 组件说明

## 1. 核心定位与单一职责 (Single Responsibility)

`TerrainRuntimeComposite`（隶属 `Terrain` 插件，标签 `["Runtime"]`，独立编译为 `TerrainRuntimeComposite.dll`）是 MQEngine 的**大世界地形运行时几何、物理碰撞与渲染管线扩展层**：

1. **高度场、Splatmap 与 GPU Clipmap 环管理**：维护规则高度场网格（默认 129×129）、4 层 RGBA Splatmap 材质权重（草地/岩石/泥土/雪地）以及 4 级同心 GPU Clipmap 环几何缓冲（`ClipmapRingGeometry`），每帧根据主相机位置执行网格整数倍吸附（Grid Snapping）消除顶点抖动。
2. **高度/法线采样与射线检测**：提供双线性插值高度采样（`getHeightAt`）、中心差分法线计算（`getNormalAt`）以及步进式地形射线求交（`raycast`）。
3. **RenderGraph 与物理子系统集成**：向 `RendererSubsystem` 注册 `TerrainClipmapPipeline`（挂载 `SyncTerrainClipmap` 同步节点与 `"ObjectPass"` 渲染订阅），并在高度场更新时向 `PhysicsSubsystem` 同步高度场碰撞体（`setHeightfieldCollider`）。
4. **笔刷算子与持久化**：提供抬升/下压（`modifyHeight`）、3×3 邻域平滑（`smoothHeight`）、目标高度压平（`flattenHeight`）、Splatmap 归一化涂抹（`paintSplatmap`）以及地形资产文件（`TERRAIN_V1`）读写。

---

## 2. 边界与明确非目标 (Non-Goals / Forbidden Overlaps)

- **严禁依赖 ImGui**：`TerrainRuntimeComposite` 是纯游戏运行时组件，在独立游戏打包（Game Standalone）时加载，**绝不包含**任何编辑器面板或笔刷 UI。
- **不负责编辑器笔刷交互面板与植被散布工具 UI**：编辑器侧的笔刷参数调节、等高线缩略预览与程序化植被 ECS 实体生成面板完全由 `TerrainEditorComposite` 负责。

---

## 3. 模块化文件拆分结构

| 文件名 | 职责说明 |
| :--- | :--- |
| `TerrainTypes.h` | `TerrainLayerDesc`、`TerrainClipmapLevel`、`ClipmapRingGeometry`、`TerrainBrushMode` 与 Clipmap 几何常量定义 |
| `TerrainRuntimeComposite.h` | `TerrainRuntimeComposite` 类声明与高度采样、笔刷算子、持久化接口 |
| `TerrainRuntimeComposite.cpp` | Composite 生命周期、默认地形初始化、双线性高度/法线采样、Clipmap 环缓冲生成与 `ObjectPass` 管线注册 |
| `TerrainBrushAndFoliage.cpp` | `modifyHeight`、`smoothHeight`、`flattenHeight`、`paintSplatmap` 与 `saveTerrainToDisk`/`loadTerrainFromDisk` 实现 |
