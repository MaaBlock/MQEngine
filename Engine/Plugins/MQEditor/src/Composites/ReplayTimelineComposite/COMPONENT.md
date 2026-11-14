# ReplayTimelineComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`ReplayTimelineComposite`（编译产物 `ReplayTimelineComposite.dll`，激活标签 `["Editor", "ImGui"]`）是 `MQEditor` 插件包中**唯一权威（Single Authoritative Owner）**负责 ECS 内存数据库时间旅行回放与 PIE 时间轴（`"ECS 回放时间轴 (ECS Replay Timeline)"`）的组件：

1. **环形缓冲区 ECS 快照录制**：按帧捕获 `Domain::Scene::SceneSubsystem` 中所有实体的 `LocalTransform` 与 `WorldTransform` 状态，维护最长 3600 帧（60 FPS 下 1 分钟）的历史快照序列与第 0 帧初始基底。
2. **时间旅行穿梭与一键回滚**：支持录制（Record）、播放（Play）、暂停（Pause）、停止并回滚至第 0 帧（Stop & Rewind）、逐帧进退（Step Forward / Backward）、倍速播放与任意帧滑块拖拽穿梭（Scrubbing）。
3. **关键帧书签与实体状态差异检视 (`Entity State Diff`)**：支持在时间轴标尺上打点标记关键帧书签（`ReplayBookmark`），并实时对比当前帧与第 0 帧各实体的位置增量。
4. **`.mqreplay` 回放序列持久化**：通过 `ResourceLoader` 将录制的帧快照与书签导出或加载为 `.mqreplay` 文件。

## 2. 停靠窗口 (Dock Windows)

- `"ECS 回放时间轴 (ECS Replay Timeline)"`（默认槽位：`Bottom`，`defaultOpen = false`）

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止在 `SceneViewerComposite` 或其他组件中重复注册 ECS 回放时间轴面板**：`ReplayTimelineComposite` 是 `"ECS 回放时间轴 (ECS Replay Timeline)"` 的唯一权威所有者。
- **禁止承担 CPU/GPU 帧采样性能剖析职责**：硬件级与函数级性能时间轴由 `TracyProfilerComposite` 独立负责。

## 4. 模块化源码文件结构

- `ReplayTimelineTypes.h`：`EntityTransformSnapshot`、`ReplayFrameRecord`、`ReplayBookmark` 与 `ReplayPlaybackState` 定义。
- `ReplayTimelineComposite.h` / `ReplayTimelineComposite.cpp`：组件生命周期、环形缓冲区录制、快照场景还原与 `.mqreplay` 文件序列化。
- `ReplayTimelinePanel.cpp`：播放控制栏、时间轴滑块标尺、关键帧菱形书签绘制与实体状态差异检视表。
