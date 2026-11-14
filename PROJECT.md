# Project: MQEngine Docs 01-07 Full Architecture Real Implementation

## Architecture Overview
MQEngine 采用严格的五层静态拓扑微内核架构（Docs 01~07）：
```
Layer 5: Runtime   (Runtime::Engine 值内嵌四大微内核模块, ModuleStaticGraph 确定性生命周期驱动)
    ↓
Layer 4: Domain    (10 大业务子域: Scene, Renderer, Effect, Physics, HID, Debug, Animation, GamePlay, JS, Py)
    ↓
Layer 3: Asset     (异步文件/网络 I/O, 异步纹理上传, 资产解码流转, CompositeManager 插件编排)
    ↓
Layer 2: Host      (五大 Host: WorkHost 双轨调度, UIHost, PhysicsHost, LogicHost, RenderHost 对接 FCT)
    ↓
Layer 1: Resource  (DataBase 统一内存数据库, 独立资源表, Config 双根自省, Arena/BlockPool 内存追踪)
    ↓
Platform: FCT      (多线程、Vulkan RHI 硬件抽象、智能指针 UniquePtr/Ptr、无锁并发基建)
```

### 核心设计原则与非妥协红线
1. **严格无锁硬红线 (STRICT LOCK-FREE)**：全引擎核心与插件目录（`Engine/Core/src/` 与 `Plugins/`）严禁使用任何互斥锁（`std::mutex`, `std::recursive_mutex`, `std::shared_mutex`, `std::lock_guard`, `std::unique_lock`, `std::scoped_lock`）。跨 Host 通信全部使用 SPSC/MPMC 无锁队列与 `bcpp::work_contract`；帧数据交换使用 `SingleFrameArena` / `DoubleBufferedFrameArena` CAS 原子索引翻转与 `Host::sync()` 内存屏障。
2. **零占位符与消除全部 Mock (ZERO PLACEHOLDERS)**：彻底杜绝任何占位假逻辑、桩函数或降级 Mock，全部功能提供真实完整生产级实现。
3. **路径解析职责单一性**：`Resource` 层（`Resource::init()` / `Resource::Config`）统一自省确定 `EngineDir`（可执行文件所在引擎目录）与 `ProjectDir`（项目根目录），对外提供标准路径获取接口；`CompositeManager` 与上层代码严禁相对路径盲猜探测（彻底删除 `./Plugins`, `../Plugins`, `../../Engine/Plugins`）。
4. **模块静态单例与契约不变量**：依据 `ModuleStaticGraph`（`Resource -> Host -> Asset -> Domain`）正向确定性初始化拓扑，已保证下层模块在先就绪，严禁在调用方编写防御性 `if (!modulePtr) return Error`，统一直接调用或 `assert`。
5. **智能指针与错误处理规范**：独占所有权必须使用引擎专有 `UniquePtr<T>` 与 `makeUnique<T>`；引用计数使用 `Ptr<T>`；严禁 C++ 异常，统一采用 `Status`/`StatusOr` + `CHECK_STATUS` 处理运行期可恢复错误，开发期违规使用 `assert` 快速失败。
6. **着色器标准线性代数乘法**：HLSL 统一显式声明 `row_major float4x4`，顶点着色器运算严格使用 `*` 运算符（`projectionMatrix * viewMatrix * modelMatrix * localPos`），彻底杜绝 `mul(vector, matrix)`。

---

## Feature Inventory

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Resource Canonical Path Resolution | `Resource::Config` 与 `Resource::Resource` 负责自省确定 `EngineDir` 与 `ProjectDir`，对外暴露 `getEnginePluginsDir()`, `getProjectPluginsDir()`, `getProjectAssetsDir()`, `getProjectScenesDir()` 标准接口 | M1_TrackA | survey_explorer_1 |
| 2 | CompositeManager Canonical Dual-Root Loading | `CompositeManager` 彻底删除全部相对路径探测与候选列表盲猜，直接从 `Resource::Resource::Instance()` 读取双根插件目录；移除对 Resource 的防御性空检查 | M1_TrackA | survey_explorer_2 |
| 3 | Resource DataBase & Dedicated Resource Tables | 将 `ComponentDataBase` 重构为 `resource/table/DataBase.h`（封装 `entt::registry`）；拆分出 `TextureTable`, `MeshTable`, `MaterialTable`, `PipelineTable`, `ShaderTable` 独立表（原生 GPU 资源裸指针为主键）；`SceneSubsystem` 对接 `Resource::DataBase` | M1_TrackA | survey_explorer_1 |
| 4 | Core & Host Strict Lock-Free Elimination | 彻底移除 `WorkHost` (4 处)、`UIHost` (1 处)、`Arena`、`BlockPool`、`AsyncTextureUploader`、`GpuResourceTable`、`TableRegistry` 中的 `std::mutex`，重构为 `boost::lockfree` / CAS 原子链表 / 无锁环形缓冲区 | M1_TrackA | survey_explorer_1 |
| 5 | Viewport ModelUniform & MultiDraw Alignment | 重构 `RendererSubsystem::updateRenderUniformsOnSubmitThread` 与 `recordObjectPass`，修复循环外仅绑定第 0 项矩阵的严重错位坍缩缺陷，实现每实体 ModelUniform 正确绑定，网格与 ImGui 包围盒 / Gizmo 100% 对齐 | M1_TrackB | survey_explorer_3 |
| 6 | Real 3D Model Loader Implementation | 在 `RendererSubsystem.cpp` 中完整实现 `loadOrGetModelMesh`, `getLoadedModelMeshInfo`, `getAllLoadedModelMeshes`, `ensureSampleModelAssetsOnDisk`，对接 `FCT::Assimp_ModelLoader` 实现真实顶点/索引上传与拖拽模型渲染 | M1_TrackB | survey_explorer_3 |
| 7 | Shaders & Matrix Multiplication Refactoring | HLSL 统一显式声明 `row_major float4x4`，重写顶点着色器为标准线性代数运算符乘法 `projectionMatrix * viewMatrix * modelMatrix * localPos`，消除一切 `mul(...)` | M1_TrackB | survey_explorer_3 |
| 8 | RenderGraph ResourceGroup & LifeCycle Upgrade | 升级 RenderGraph 节点为 `PassNode <-> ResourceGroup <-> PassNode`，接入 `ResourceManager::require` 动态分配工厂，解耦 `ConstBuffer` 内部反向依赖，落实延迟释放队列与退出状态机 | M1_TrackB | survey_explorer_3 |
| 9 | Loose BVH & Domain Real Subsystems Implementation | 重构 `SpatialGraph` 为真实松散 BVH 层次加速结构；重构 `EffectSubsystem`（接入 DLSS, 硬件光追 RTReflection/RTGI/RTShadow, 后处理链与粒子）；重构 `PhysicsSubsystem`（接入多后端 Jolt/PhysX 求解器与双缓冲位姿同步）；完善 5 种 PBR 材质模型与延迟着色 | M1_TrackB | survey_explorer_3 |
| 10 | Domain & Rendering Strict Lock-Free Elimination | 消除 `RendererSubsystem` (5 处)、`BindlessSet0Manager` (2 处)、`EffectSubsystem`、`PhysicsSubsystem`、`SpatialGraph`、`TransformGraph` 的所有 `std::mutex`，全部改用无锁数据流与单 Host 线程所有权 | M1_TrackB | survey_explorer_3 |
| 11 | Editor Core Composites Full Real Implementation | `EditorUiComposite` 维持“视图 (View)”与“窗口 (Window)”菜单分类；`ShaderEditorComposite` 与 `HtmlUiEditorComposite` 居中且 `defaultOpen = false`；完善浮动通知 Toast、撤销重做栈与纯中文/纯英文无杂合本地化；移除 `AssetBrowserComposite` 与 `SceneViewerComposite` 中的互斥锁 | M1_TrackC | survey_explorer_2 |
| 12 | Real 3D Viewport Model Drag-and-Drop & Gizmos | `SceneViewerComposite` 完善视口外部模型文件（`.obj`, `.gltf`, `.fbx`）拖拽放置、BVH 射线拾取、3D 坐标轴箭头 Gizmo 与相机坐标轴指示器 | M1_TrackC | survey_explorer_2 |
| 13 | Extended Composites & Profiler Enrichment | 真实落地 `TracyProfilerComposite`（全管线 Tracy 区域注入）、`RenderGraphEditorComposite`、`RmlUiComposite`、`BlueprintRuntime/Editor`、`TerrainRuntime/Editor` 与 `ModelViewer` 示例工程 | M1_TrackD | user_priority_update |
| 14 | Unified MSVC 14.44 Build & Zero-Error Link | Visual Studio 2022 MSVC 14.44 全工程编译链接（`Engine`, `Editor`, `Game` 及各独立 Composite DLL），0 编译与链接错误 | M2 | user_priority_update |
| 15 | Headless 30-Frame Runtime Verification | `Editor.exe --headless --test-frames 30` 与 `Game.exe --headless --test-frames 30` 运行 30 帧稳定退出（退出码 0，无 Vulkan Validation 错误） | M3 | user_priority_update |
| 16 | GUI Window 30-Frame Runtime Verification | `Editor.exe --test-frames 30` 与 `Game.exe --test-frames 30` 在 Vulkan 窗口模式下运行 30 帧稳定退出（退出码 0，无校验层错误与内存崩溃） | M3 | user_priority_update |
| 17 | Forensic Integrity Audit & Quality Gate | 静态扫描全库验证 0 互斥锁、0 `EngineGlobal`、0 `std::unique_ptr`、0 C++ 异常、0 Mock 桩代码，通过法医审计员审计 | M4 | orchestrator_plan |

---

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1_TrackA | Track A: Resource, Host, Table & CompositeManager Architecture | Feature 1, 2, 3, 4: 路径自省、DataBase 与独立资源表、无锁 Host 与内存池、CompositeManager 清除盲猜 | none | DONE |
| M1_TrackB | Track B: Rendering, RenderGraph, Shaders, Viewport & Culling | Feature 5, 6, 7, 8, 9, 10: 视口 ModelUniform 修复、Assimp 3D 模型加载、着色器 `*` 乘法、松散 BVH、真实 Effect 与 Physics、无锁渲染与领域域 | none | DONE |
| M1_TrackC | Track C: Editor & Core Composites Real Implementation | Feature 11, 12: EditorUi 菜单与布局、视口拖拽模型放置、无锁 Editor 合成器、纯语言本地化 | none | DONE |
| M1_TrackD | Track D: Extended Composites, Profiling & Tooling | Feature 13: Tracy 性能分析合成器、RDG 编辑器合成器、RmlUi、蓝图与地形、ModelViewer 工程 | none | DONE |
| M2 | Milestone 2: Unified Build & Static Sanity Gate | Feature 14: MSVC 14.44 Ninja 全量构建，生成所有可执行文件与 DLL；静态扫描验证 0 互斥锁与 0 桩代码 | M1_TrackA, M1_TrackB, M1_TrackC, M1_TrackD | DONE |
| M3 | Milestone 3: 30-Frame Headless & GUI Runtime Verification | Feature 15, 16: Editor.exe 与 Game.exe 在无头与窗口模式下完成 30 帧真实运行验证，校验退出码为 0 且无 Vulkan Validation 报错 | M2 | DONE |
| M4 | Milestone 4: Forensic Audit & Final Delivery | Feature 17: 运行 `teamwork_auditor` 验证代码完整性与架构依从性，向 Sentinel 交付完整成果报告 | M3 | DONE |

---

## Interface Contracts

### 1. `Resource::Config` & `Resource::Resource` (双根路径契约)
```cpp
namespace MQEngine::Resource {
class Config {
public:
    const std::string& getEngineDir() const noexcept;
    const std::string& getProjectDir() const noexcept;
    std::string getEnginePluginsDir() const;   // <EngineDir>/Plugins
    std::string getProjectPluginsDir() const;  // <ProjectDir>/Plugins
    std::string getProjectAssetsDir() const;   // <ProjectDir>/Assets
    std::string getProjectScenesDir() const;   // <ProjectDir>/Scenes
};
}
```

### 2. `CompositeManager` (双根加载与零盲猜契约)
```cpp
namespace MQEngine::Asset {
class CompositeManager {
public:
    // 仅接收明确路径或直接从 Resource 获取，严禁 relative probing
    Status discoverPluginsFromCanonicalRoots();
    Status loadCompositeDll(const std::string& pluginDir, const std::string& compositeName);
};
}
```

### 3. `Resource::DataBase` & 资源表体系 (纯裸指针主键契约)
```cpp
namespace MQEngine::Resource {
class DataBase {
public:
    FCT::ECS::registry& getRegistry() noexcept;
};

template <typename NativeGpuResourcePtr, typename Metadata>
class LockFreeResourceTable {
public:
    void registerResource(NativeGpuResourcePtr ptr, Metadata meta);
    Metadata* findResource(NativeGpuResourcePtr ptr);
    bool unregisterResource(NativeGpuResourcePtr ptr);
};
}
```

### 4. `Domain::Renderer` (ModelUniform 与 3D 视口对齐契约)
```cpp
// 顶点着色器统一规范：
// cbuffer ModelUniform : register(b1) { row_major float4x4 modelMatrix; row_major float4x4 modelInverseTransposeMatrix; };
// sOut.position = projectionMatrix * viewMatrix * modelMatrix * sIn.position;
// 绘制循环中每个 draw item 必须绑定对应的独立 ModelUniform 槽位或使用 GpuInstanceRow MultiDraw.
```

---

## Code Layout
```
G:\MQEngine\
├── MQEngine\
│   ├── CMakeLists.txt                       # 根构建脚本 (C++20, MSVC 14.44)
│   ├── Engine\
│   │   ├── Core\
│   │   │   ├── src\
│   │   │   │   ├── resource\                # Layer 1: DataBase, Table, Config (EngineDir/ProjectDir), Memory
│   │   │   │   ├── host\                    # Layer 2: WorkHost (无锁双轨), UIHost, PhysicsHost, LogicHost, RenderHost
│   │   │   │   ├── asset\                   # Layer 3: Asset, AsyncLoader, AsyncTextureUploader, CompositeManager
│   │   │   │   ├── domain\                  # Layer 4: Renderer, Scene, Effect, Physics, Script, HID, Debug, Animation
│   │   │   │   ├── runtime\                 # Layer 5: Engine, Runtime, Application
│   │   │   │   └── base\                    # 编译期模板工具 (CoreEngineModule, CoreEngineManager, StaticTokenGraph)
│   │   └── Plugins\
│   │       ├── EditorBase\                  # ImGuiComposite, EditorUiComposite (DockSpace, Menu, Toast, UndoRedo)
│   │       ├── MQEditor\                    # SceneViewerComposite (3D 视口/拖拽), AssetBrowserComposite, ShaderEditorComposite
│   │       └── GameHtmlUI\                  # RmlUiComposite, HtmlUiEditorComposite
│   └── Applications\
│       ├── Editor\                          # Editor 极简宿主
│       └── Game\                            # Game 独立游戏宿主
└── .agents\teamwork\                        # 编排与协同元数据
```
