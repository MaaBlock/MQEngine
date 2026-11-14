# MQEngine

[English](README.md) | [FCT_RHI](https://github.com/MaaBlock/FCT_RHI)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Architecture](https://img.shields.io/badge/Architecture-Data--Driven%20%7C%20Microkernel%20Plugins-6f42c1)](https://github.com/MaaBlock/MQEngine)
[![RHI](https://img.shields.io/badge/Graphics-Vulkan%20%7C%20DirectX%2012%20%7C%20Headless-AC162C)](https://github.com/MaaBlock/FCT_RHI)
[![Scripting](https://img.shields.io/badge/Scripting-TypeScript%20%7C%20Blueprint%20%7C%20Python-3178C6)](https://github.com/MaaBlock/MQEngine)

`MQEngine` 是跨平台 C++20 三维游戏与图形引擎，围绕**数据驱动**、**声明式数据配置流程**与**微内核插件制**组织场景模拟、实时渲染、可视化编辑器与独立游戏打包流程。

## 功能特性

### 数据驱动引擎
- 以统一的关系型数据表管理场景实体、组件、网格、材质与纹理资源，直接驱动逻辑更新与渲染提交，避免复杂的对象引用耦合。
- 支持 GPU-Driven 间接渲染、Bindless 无绑定纹理、PBR 物理材质、硬件光线追踪、DLSS 超分、后处理管线、大世界地形与多求解器物理模拟。

### 数据配置流程
- 工程设置（`project.mq`）、插件包清单（`plugin.mq`）与渲染图拓扑（`.pgraph`）统一使用 TOML 配置文件描述。
- 组件数据结构定义完成后，自动生成编辑器属性检视面板并导出 TypeScript `.d.ts` 类型声明，无需手写样板绑定代码。
- HTML/CSS 游戏界面可在画布中直接拖拽排版与缩放，实时双向写回 `.rml` / `.rcss` 样式文件，并支持声明式数据与事件绑定。
- 可视化节点蓝图与强类型 TypeScript（`.ts`）源码支持双向无损互转，兼顾连线编排与代码协作。
- 支持将 `.fbx`、`.gltf`、`.glb`、`.obj` 模型与贴图直接拖入三维视口导入场景，并提供一键资产烘焙与独立游戏打包。

### 微内核插件制
- 引擎内核只保留基础调度底座，`Editor`、`Game` 与 `CLI` 可执行程序仅作为轻量启动宿主按标签激活所需功能。
- 编辑器面板（三维视口、资产浏览器、着色器编辑器、渲染图编辑器、内置性能分析器、HTML 界面设计器、蓝图编辑器）与游戏运行时功能均拆分为独立插件单元按需加载：开发期支持单模块热重载，打包独立游戏时自动剔除全部编辑器依赖。
- 基于 [FCT_RHI](https://github.com/MaaBlock/FCT_RHI) 支持 Windows、Linux 与 Android 平台（Vulkan / DirectX 12），并提供无窗口 Headless GPU 模式用于自动化测试与离屏渲染。

## 快速上手

### 环境要求
- Windows 10/11 x64、Linux 或 Android
- Visual Studio 2022 17.10+（C++20）、CMake 3.22.1+、Ninja、[vcpkg](https://github.com/microsoft/vcpkg)、Vulkan SDK 1.2+

### 编译与运行
```bash
git clone --recursive https://github.com/MaaBlock/MQEngine.git
cd MQEngine
cmake --preset windows-debug
cmake --build Output/build-windows-debug --target Engine Editor Game
```

```bash
# 启动编辑器或独立游戏宿主
./Output/build-windows-debug/Applications/Editor/Editor.exe
./Output/build-windows-debug/Applications/Game/Game.exe

# 无窗口 Headless GPU 离屏运行验证
./Output/build-windows-debug/Applications/Game/Game.exe --headless --test-frames 30
```

## 关联仓库
- [FCT_RHI](https://github.com/MaaBlock/FCT_RHI)：支撑 MQEngine 的跨平台图形硬件接口与渲染图库。
- [FCT](https://github.com/MaaBlock/FCT)：模块化 C++ 基础运行库集合。