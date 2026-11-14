# NtcBakerComposite 组件说明

## 1. 单一职责定位 (Single Responsibility)

`NtcBakerComposite`（编译产物 `NtcBakerComposite.dll`，激活标签 `["Editor", "ImGui"]`）是 `MQEditor` 插件包中唯一负责神经纹理压缩（Neural Texture Compression, NTC）离线烘焙、MLP 潜特征网格编码与 PSNR 质量对比预览的组件：

1. **多通道 PBR 贴图读取与合成**：支持 Albedo、Normal、ORM 等多通道贴图输入与分辨率重采样，配置 MLP 神经网络层数、隐层维度、训练轮数与目标 PSNR 阈值。
2. **NTC 神经潜特征网格编码与 BC7 块压缩对比 (`NtcBakerCodec`)**：基于分块潜特征网格（Latent Grid）与多层感知机（LeakyReLU + Sigmoid）执行 Mini-batch 反向传播与 Adam 动量权重优化，并提供 BC7 块量化预览对比。
3. **分屏对比与残差热力图检视**：实时计算 MSE Loss、PSNR（dB）、SSIM 与显存压缩比，提供原图与 NTC 解压缩画面的交互式分屏滑块与 5x 残差热力图。
4. **`.ntc` 二进制材质包导出与子系统注册**：将训练收敛后的 `NtcHeader`、潜特征网格与 MLP 权重固化为 `.ntc` 二进制文件，并自动注册至 `Domain::Effect::NtcSubsystem`。

## 2. 停靠窗口 (Dock Windows)

- `"神经纹理烘焙 (NTC Baker)"`（默认槽位：`Right`，`defaultOpen = false`）

## 3. 明确非目标 (Non-Goals & Forbidden Overlaps)

- **禁止管理通用资产目录浏览**：由 `AssetBrowserComposite` 负责。
- **禁止管理着色器节点图或 RenderGraph 渲染图拓扑**：分别由 `ShaderEditorComposite` 与 `RenderGraphEditorComposite` 负责。

## 4. 模块化源码文件结构

- `NtcBakerTypes.h`：`NtcBakeMode`、`NtcBakeQuality`、`NtcChannelItem`、`NtcBakeConfig`、`NtcBakeMetrics` 与 `NtcFileHeader` 定义。
- `NtcBakerComposite.h` / `NtcBakerComposite.cpp`：组件生命周期、烘焙面板 UI、超参配置页与 PSNR/残差热力图分屏对比预览器。
- `NtcBakerCodec.cpp`：源纹理采样合成、MLP 潜特征网格编码、Adam 优化器步进、BC7 块量化对比与 `.ntc` 二进制写出。
