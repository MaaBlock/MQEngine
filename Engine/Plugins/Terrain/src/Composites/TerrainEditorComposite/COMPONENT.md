# TerrainEditorComposite 组件说明

## 1. 核心定位与单一职责 (Single Responsibility)

`TerrainEditorComposite`（隶属 `Terrain` 插件，标签 `["Editor", "ImGui"]`，独立编译为 `TerrainEditorComposite.dll`）是 MQEngine 的**大世界地形雕刻、材质涂抹与程序化植被散布工坊面板**：

1. **大世界地形工坊面板 (`Terrain Studio`)**：管理 `大世界地形工坊 (Terrain Studio)` 停靠窗口，提供雕刻模式（抬升/下压/平滑/压平）、Splatmap 4 层材质权重涂抹模式与程序化植被散布模式切换。
2. **高度场俯视交互预览与笔刷投射**：实时绘制地形高度场等高线缩略画布与笔刷指示圈，将鼠标交互坐标转换为世界坐标并调用 `TerrainRuntimeComposite` 笔刷算子。
3. **程序化植被 ECS 实体散布**：根据海拔区间（`Min/Max Elevation`）与最大坡度角（`Max Slope`）约束，在地形表面随机采样并实例化真实 ECS 植被实体（`Conifer`/`Shrub`/`Flora`），支持一键全地形散布与一键清空。

---

## 2. 边界与明确非目标 (Non-Goals / Forbidden Overlaps)

- **不直接维护底层 GPU Clipmap 缓冲或物理碰撞体**：底层高度场数据、Clipmap 环顶点缓冲与物理碰撞同步完全委托给 `TerrainRuntimeComposite`。
- **不替代通用 3D 场景视口**：场景相机漫游、通用实体大纲树与变换 Gizmo 由 `SceneViewerComposite` 负责。

---

## 3. 模块化文件拆分结构

| 文件名 | 职责说明 |
| :--- | :--- |
| `TerrainEditorComposite.h` | `TerrainEditorComposite` 类声明与笔刷/植被状态字段 |
| `TerrainEditorComposite.cpp` | Composite 生命周期、`applyBrushAt` 笔刷分发与 `scatterFoliage`/`scatterAllTerrain`/`clearAllFoliage` 程序化植被 ECS 散布实现 |
| `TerrainEditorPanel.cpp` | `renderTerrainEditorPanel`、雕刻/Splatmap/植被参数控件、高度场 Min/Max 俯视交互画布与资产保存 UI |
