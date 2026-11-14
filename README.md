# MQEngine

[简体中文 (数据驱动 · 数据配置流程 · 微内核插件制)](README_zh.md) | [FCT_RHI](https://github.com/MaaBlock/FCT_RHI)

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Architecture](https://img.shields.io/badge/Architecture-Data--Driven%20%7C%20Microkernel%20Plugins-6f42c1)](https://github.com/MaaBlock/MQEngine)
[![RHI](https://img.shields.io/badge/Graphics-Vulkan%20%7C%20DirectX%2012%20%7C%20Headless-AC162C)](https://github.com/MaaBlock/FCT_RHI)
[![Scripting](https://img.shields.io/badge/Scripting-TypeScript%20%7C%20Blueprint%20%7C%20Python-3178C6)](https://github.com/MaaBlock/MQEngine)

**MQEngine** is a cross-platform C++20 3D game and graphics engine built around **data-driven scenes and rendering**, an end-to-end **declarative data configuration workflow**, and a **microkernel plugin architecture**.

## Features

### Data-Driven Engine
- **Relational Scene & Asset Data**: Manages entities, components, meshes, materials, and textures through unified data tables to drive simulation and rendering without object-graph coupling.
- **Modern Real-Time Rendering & Physics**: Supports GPU-Driven indirect rendering, Bindless textures, PBR materials, hardware ray tracing, DLSS, post-processing, terrain, and multi-solver physics simulation.

### Data Configuration Workflow
- **Declarative TOML Configuration**: Projects (`project.mq`), plugin packages (`plugin.mq`), and render graphs (`.pgraph`) are configured through clean TOML manifests.
- **Zero-Boilerplate Inspector & Script Types**: Component definitions automatically expose Editor Inspector properties and generate TypeScript `.d.ts` type declarations.
- **Two-Way Visual & Code Authoring**:
  - **HTML/CSS Game UI**: Drag-and-drop WYSIWYG canvas editing with live two-way sync to `.rml` / `.rcss` stylesheets and declarative data/event bindings.
  - **Blueprint <-> TypeScript**: Visual Blueprint graphs and strongly typed TypeScript (`.ts`) source files translate seamlessly in both directions.
- **Drag-and-Drop Import & One-Click Packaging**: Import `.fbx`, `.gltf`, `.glb`, and `.obj` assets directly into the 3D viewport, and cook standalone game packages in one step.

### Microkernel Plugin Architecture
- **Minimal Kernel & Pure-Shell Hosts**: `Editor`, `Game`, and `CLI` executables act purely as lightweight hosts that declare active feature tags.
- **On-Demand Modular Plugins & Hot-Reload**: Editor tools (3D viewport, asset browser, shader editor, render graph editor, built-in profiler, HTML UI designer, Blueprint editor) and runtime features load on demand as independent plugin units—supporting live hot-reload during development while keeping shipped standalone games completely free of editor dependencies.
- **Multi-Platform & Headless Support**: Runs on Windows, Linux, and Android over Vulkan and DirectX 12 (powered by [FCT_RHI](https://github.com/MaaBlock/FCT_RHI)), including zero-window Headless GPU execution for CI and automated frame verification.

## Quick Start

### Requirements
- Windows 10/11 x64, Linux, or Android
- Visual Studio 2022 17.10+ (C++20), CMake 3.22.1+, Ninja, [vcpkg](https://github.com/microsoft/vcpkg), Vulkan SDK 1.2+

### Build & Run
```bash
git clone --recursive https://github.com/MaaBlock/MQEngine.git
cd MQEngine
cmake --preset windows-debug
cmake --build Output/build-windows-debug --target Engine Editor Game
```

```bash
# Launch Editor or standalone Game
./Output/build-windows-debug/Applications/Editor/Editor.exe
./Output/build-windows-debug/Applications/Game/Game.exe

# Headless GPU verification
./Output/build-windows-debug/Applications/Game/Game.exe --headless --test-frames 30
```

## Related Repositories
- **[FCT_RHI](https://github.com/MaaBlock/FCT_RHI)** — Cross-platform RHI & RenderGraph library powering MQEngine.
- **[FCT](https://github.com/MaaBlock/FCT)** — Modular C++ runtime suite (`FCT_Base`, `FCT_RHI`, `FCT_IMGUI`, `FCT_Node`, `FCT_Phys`, `FCT_Compute`).
