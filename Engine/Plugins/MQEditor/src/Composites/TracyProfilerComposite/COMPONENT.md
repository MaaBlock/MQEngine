# TracyProfilerComposite 组件架构说明

## 1. 单一职责 (Single Responsibility)

`TracyProfilerComposite` 是 `MQEditor` 插件中负责 **CPU / GPU / 内存 / 锁争用与帧遥测性能剖析（Performance Profiling & Frame Telemetry）** 的唯一所有者（Exclusive Owner），核心职责涵盖：

1. **内嵌上游原生 `tracy::View` 与 `tracy::Worker`**：
   - 直接链接并嵌入 `FCT/FCT_Base/ThirdParty/tracy` 上游原生 Profiler 服务端（`MQTracyServer`）与视图引擎（`tracy::View`）。
   - 通过 `TRACY_NO_ROOT_WINDOW` 编译宏将上游 `tracy::View::Draw()` 无缝嵌入编辑器底部的 `"Profiler"` Dock 面板中运行。
2. **实时本地回环遥测连接与重连管理**：
   - 默认连接本机 `127.0.0.1:8086` 采集引擎主循环、渲染线程、TaskGraph 工作线程的 `ZoneScoped`、帧标记（`FrameMark`）与内存分配遥测数据。
   - 自动响应 `tracy::View::ReconnectRequested()` 处理会话重连。
3. **离线 `.tracy` 快照文件加载**：
   - 支持通过 `tracy::FileRead::Open` 加载外部 `.tracy` 捕获文件并在内嵌视图中完整回放时间轴与统计信息。
4. **无锁主线程任务桥接与 ImGui 字体共享**：
   - 使用 `moodycamel::ConcurrentQueue` 实现无锁主线程回调队列（`TracyBridge::runOnMainThread` / `drainMainThreadTasks`），严禁使用 `std::mutex` 或 `SpinLock`。

---

## 2. 注册的停靠窗口 (Dock Windows)

| 窗口 ID | 默认停靠槽位 (`EditorDockSlot`) | 职责概述 |
| :--- | :--- | :--- |
| `"Profiler"` | `Bottom` | 承载上游原生 `tracy::View` 全功能性能剖析界面（帧时间轴、CPU/GPU Zone 火焰图、调用栈统计、内存与锁分析） |

---

## 3. 职责边界与非目标 (Non-Goals)

- **严禁手写自定义 ImGui 火焰图或伪造性能统计面板**：
  - 所有性能剖析可视化必须 100% 由上游原生 `tracy::View::Draw()` 驱动，禁止任何其他 Composite 自行绘制山寨火焰图或帧耗时柱状图。
- **不负责 ECS 逻辑状态时间旅行回放**：
  - ECS 实体组件状态录制与逐帧回放由 `ReplayTimelineComposite` 独占负责。

---

## 4. 模块化文件拆分说明 (File Breakdown)

| 文件名 | 行数规模 | 核心内容 |
| :--- | :--- | :--- |
| `TracyProfilerComposite.h` | ~80 行 | `TracyProfilerComposite` 生命周期与遥测控制接口（`startCapture` / `pauseCapture` / `stopCapture` / `loadTraceFile`）声明 |
| `TracyProfilerCallbacks.h` | ~85 行 | `TracyBridge` 无锁主线程任务队列（`moodycamel::ConcurrentQueue`）、ImGui 全局字体槽位绑定与宿主窗口空操作回调 |
| `TracyProfilerComposite.cpp` | ~235 行 | `"Profiler"` Dock 窗口注册、`tracy::View` 实例生命周期管理（`FCT_NEW` / `FCT_DELETE`）、`.tracy` 快照加载与 `m_tracyView->Draw()` 调用 |
| `shims/TracyViewShims.cpp` | ~185 行 | 上游 `tracy::View` 剥离独立 GLFW 宿主窗口、Capstone 反汇编、LLM 与内置文件选择器所需的精简符号桥接实现 |
