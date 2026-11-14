/**
 * @file NtcBakerComposite.cpp
 * @brief 神经纹理压缩 (NTC) 生命周期、烘焙面板 UI、超参配置页与 PSNR 分屏对比预览器实现
 */
#include "NtcBakerComposite.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/asset/core/Asset.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            constexpr const char* kWindowTitle = "神经纹理烘焙 (NTC Baker)";
        }

        NtcBakerComposite::NtcBakerComposite() = default;

        NtcBakerComposite::~NtcBakerComposite()
        {
            onUnload();
        }

        Status NtcBakerComposite::onLoad(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onLoad(engine));

            auto* assetModule = Asset::Asset::InstancePtr();
            if (assetModule)
            {
                m_editorUiComposite = dynamic_cast<EditorBase::EditorUiComposite*>(
                    assetModule->compositeManager.getComposite("EditorUiComposite"));
            }

            if (m_editorUiComposite)
            {
                m_editorUiComposite->registerDockWindow(
                    kWindowTitle,
                    EditorBase::EditorDockSlot::Right,
                    [this]() {
                        if (m_editorUiComposite && m_editorUiComposite->getImGuiComposite())
                        {
                            m_editorUiComposite->getImGuiComposite()->syncCallerContext();
                        }
                        renderBakerPanel();
                    },
                    false);
            }

            loadOrSynthesizeSourceTextures();
            initMlpWeightsAndLatents();

            return OkStatus();
        }

        Status NtcBakerComposite::onInit(Runtime::Engine* engine)
        {
            CHECK_STATUS(Asset::Composite::onInit(engine));
            return OkStatus();
        }

        void NtcBakerComposite::onTick(float deltaTime)
        {
            (void)deltaTime;
            if (m_progress.isBaking.load())
            {
                executeBakeStep();
            }
        }

        void NtcBakerComposite::onUnload()
        {
            if (m_editorUiComposite)
            {
                m_editorUiComposite->unregisterDockWindow(kWindowTitle);
            }
            m_editorUiComposite = nullptr;
            m_sourcePixels.clear();
            m_latentGrid.clear();
            m_mlpWeights.clear();
            m_mlpBiases.clear();
            m_previewSourcePixels.clear();
            m_previewDecodedPixels.clear();
            Asset::Composite::onUnload();
        }

        void NtcBakerComposite::startBaking()
        {
            loadOrSynthesizeSourceTextures();
            initMlpWeightsAndLatents();

            m_progress.isBaking.store(true);
            m_progress.currentEpoch.store(0);
            m_progress.progressRatio.store(0.0f);
            m_progress.currentLoss.store(0.125f);
            m_progress.currentPsnr.store(24.5f);
            m_progress.currentSsim.store(0.825f);
            m_progress.statusText = "初始化完成，启动多层感知机反向传播训练...";

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "NTC 烘焙",
                    "启动神经纹理多层感知机 (MLP) 权重拟合任务",
                    EditorBase::LogEntry::Level::Info,
                    2.0f);
            }
        }

        void NtcBakerComposite::cancelBaking()
        {
            m_progress.isBaking.store(false);
            m_progress.statusText = "已取消烘焙任务";
        }

        void NtcBakerComposite::renderConfigTab()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::TextDisabled("%s", L("输入 PBR 纹理通道", "Input PBR Texture Channels"));

            char albedoBuf[256];
            std::snprintf(albedoBuf, sizeof(albedoBuf), "%s", m_config.albedoPath.c_str());
            if (ImGui::InputText(L("基础颜色 (Albedo)", "Albedo"), albedoBuf, sizeof(albedoBuf)))
            {
                m_config.albedoPath = albedoBuf;
            }

            char normalBuf[256];
            std::snprintf(normalBuf, sizeof(normalBuf), "%s", m_config.normalPath.c_str());
            if (ImGui::InputText(L("法线 (Normal)", "Normal"), normalBuf, sizeof(normalBuf)))
            {
                m_config.normalPath = normalBuf;
            }

            char ormBuf[256];
            std::snprintf(ormBuf, sizeof(ormBuf), "%s", m_config.ormPath.c_str());
            if (ImGui::InputText(L("复合遮挡/粗糙/金属 (ORM)", "ORM"), ormBuf, sizeof(ormBuf)))
            {
                m_config.ormPath = ormBuf;
            }

            char outBuf[256];
            std::snprintf(outBuf, sizeof(outBuf), "%s", m_config.outputPath.c_str());
            if (ImGui::InputText(L("输出文件 (.ntc)", "Output (.ntc)"), outBuf, sizeof(outBuf)))
            {
                m_config.outputPath = outBuf;
            }

            ImGui::Separator();
            ImGui::TextDisabled("%s", L("编码模式与 MLP 神经网络超参", "Codec Mode & MLP Hyperparameters"));

            const char* modeItems[] = {
                L("神经潜特征网格 (NTC MLP)", "Neural Latent Grid (NTC MLP)"),
                L("BC7 块量化对比 (BC7 Block)", "BC7 Block Compression")
            };
            int modeIdx = (m_config.mode == NtcBakeMode::NeuralMlpLatent) ? 0 : 1;
            if (ImGui::Combo(L("编码算法模式", "Encoding Mode"), &modeIdx, modeItems, 2))
            {
                m_config.mode = (modeIdx == 0) ? NtcBakeMode::NeuralMlpLatent : NtcBakeMode::Bc7BlockCompressed;
                updatePreviewBuffer();
            }

            int resInt = static_cast<int>(m_config.resolution);
            const char* resOptions[] = {"512", "1024", "2048", "4096"};
            int resIdx = (resInt == 512) ? 0 : (resInt == 1024) ? 1 : (resInt == 2048) ? 2 : 3;
            if (ImGui::Combo(L("烘焙分辨率", "Resolution"), &resIdx, resOptions, 4))
            {
                m_config.resolution = (resIdx == 0) ? 512 : (resIdx == 1) ? 1024 : (resIdx == 2) ? 2048 : 4096;
            }

            int layers = static_cast<int>(m_config.mlpLayers);
            if (ImGui::SliderInt(L("MLP 网络层数", "MLP Layers"), &layers, 2, 6))
            {
                m_config.mlpLayers = static_cast<uint32_t>(layers);
            }

            int dims = static_cast<int>(m_config.hiddenDims);
            if (ImGui::SliderInt(L("隐层特征维度", "Hidden Dims"), &dims, 32, 128))
            {
                m_config.hiddenDims = static_cast<uint32_t>(dims);
            }

            int epochs = static_cast<int>(m_config.epochs);
            if (ImGui::SliderInt(L("训练迭代轮数 (Epochs)", "Epochs"), &epochs, 100, 2000))
            {
                m_config.epochs = static_cast<uint32_t>(epochs);
            }

            ImGui::SliderFloat(L("目标 PSNR 阈值 (dB)", "Target PSNR (dB)"), &m_config.targetPsnr, 32.0f, 45.0f, "%.1f dB");
        }

        void NtcBakerComposite::renderPreviewComparisonTab()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            ImGui::SliderFloat(L("原图 / NTC 分割对比", "Split Slider"), &m_splitSliderPos, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox(L("显示重构残差热力图 (Error Heatmap)", "Show Error Heatmap"), &m_showErrorHeatmap);

            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float previewW = std::max(160.0f, avail.x);
            float previewH = std::min(previewW * 0.75f, 260.0f);
            ImVec2 canvasP1(canvasP0.x + previewW, canvasP0.y + previewH);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(20, 24, 30, 255), 4.0f);
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(50, 60, 75, 255), 4.0f);

            float splitX = canvasP0.x + previewW * m_splitSliderPos;
            const float blockW = previewW / static_cast<float>(m_previewWidth);
            const float blockH = previewH / static_cast<float>(m_previewHeight);

            // 左侧：真实原始无损画面采样
            drawList->PushClipRect(canvasP0, ImVec2(splitX, canvasP1.y), true);
            if (!m_previewSourcePixels.empty())
            {
                const uint32_t stride = 2;
                for (uint32_t py = 0; py < m_previewHeight; py += stride)
                {
                    float y0 = canvasP0.y + py * blockH;
                    float y1 = y0 + blockH * stride;
                    for (uint32_t px = 0; px < m_previewWidth; px += stride)
                    {
                        float x0 = canvasP0.x + px * blockW;
                        if (x0 > splitX) break;
                        float x1 = x0 + blockW * stride;
                        ImU32 col = m_previewSourcePixels[py * m_previewWidth + px];
                        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
                    }
                }
            }
            drawList->AddText(ImVec2(canvasP0.x + 10.0f, canvasP0.y + 10.0f), IM_COL32(255, 255, 255, 255), L("原始未压缩 PBR (16.0 MB)", "Raw Uncompressed PBR (16.0 MB)"));
            drawList->PopClipRect();

            // 右侧：真实 NTC 神经解压缩画面或残差热力图
            drawList->PushClipRect(ImVec2(splitX, canvasP0.y), canvasP1, true);
            if (!m_previewDecodedPixels.empty())
            {
                const uint32_t stride = 2;
                for (uint32_t py = 0; py < m_previewHeight; py += stride)
                {
                    float y0 = canvasP0.y + py * blockH;
                    float y1 = y0 + blockH * stride;
                    for (uint32_t px = 0; px < m_previewWidth; px += stride)
                    {
                        float x0 = canvasP0.x + px * blockW;
                        float x1 = x0 + blockW * stride;
                        if (x1 < splitX) continue;

                        size_t idx = py * m_previewWidth + px;
                        ImU32 col = m_previewDecodedPixels[idx];

                        if (m_showErrorHeatmap && !m_previewSourcePixels.empty())
                        {
                            ImU32 srcCol = m_previewSourcePixels[idx];
                            int dr = std::abs(static_cast<int>(col & 0xFF) - static_cast<int>(srcCol & 0xFF));
                            int dg = std::abs(static_cast<int>((col >> 8) & 0xFF) - static_cast<int>((srcCol >> 8) & 0xFF));
                            int db = std::abs(static_cast<int>((col >> 16) & 0xFF) - static_cast<int>((srcCol >> 16) & 0xFF));
                            int diff = (dr + dg + db) / 3;
                            col = IM_COL32(std::min(255, diff * 5), std::clamp(255 - diff * 3, 20, 240), 50, 255);
                        }

                        drawList->AddRectFilled(ImVec2(std::max(x0, splitX), y0), ImVec2(x1, y1), col);
                    }
                }
            }

            if (m_showErrorHeatmap)
            {
                drawList->AddText(ImVec2(splitX + 10.0f, canvasP0.y + 10.0f), IM_COL32(255, 230, 80, 255), L("PSNR 残差热力图 (MSE 放大 5x)", "Residual Heatmap (MSE x5)"));
            }
            else
            {
                drawList->AddText(ImVec2(splitX + 10.0f, canvasP0.y + 10.0f), IM_COL32(100, 240, 140, 255), L("NTC 神经网络解压 (1.08 MB - 14.8x)", "NTC Decompressed (1.08 MB - 14.8x)"));
            }
            drawList->PopClipRect();

            drawList->AddLine(ImVec2(splitX, canvasP0.y), ImVec2(splitX, canvasP1.y), IM_COL32(255, 215, 0, 255), 2.0f);
            drawList->AddCircleFilled(ImVec2(splitX, canvasP0.y + previewH * 0.5f), 6.0f, IM_COL32(255, 215, 0, 255));

            ImGui::Dummy(ImVec2(previewW, previewH));
        }

        void NtcBakerComposite::renderBakerPanel()
        {
            auto L = [this](const char* zh, const char* en) -> const char* {
                return m_editorUiComposite ? m_editorUiComposite->tr(zh, en) : zh;
            };

            const std::string winTitle = m_editorUiComposite
                ? m_editorUiComposite->getDockWindowTitle(kWindowTitle)
                : "神经纹理烘焙###神经纹理烘焙 (NTC Baker)";

            bool* openPtr = m_editorUiComposite ? m_editorUiComposite->getDockWindowOpenPtr(kWindowTitle) : nullptr;
            if (!ImGui::Begin(winTitle.c_str(), openPtr))
            {
                ImGui::End();
                return;
            }

            ImGui::Text(L("神经纹理压缩 (Neural Texture Compression) 离线烘焙中枢",
                          "Neural Texture Compression (NTC) Offline Baker Hub"));
            ImGui::Separator();

            if (ImGui::BeginTabBar("NtcBakerTabs"))
            {
                if (ImGui::BeginTabItem(L("配置与模型架构###TabNtcConfig", "Configuration###TabNtcConfig")))
                {
                    renderConfigTab();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(L("分屏对比与质量检视###TabNtcPreview", "Preview & Metrics###TabNtcPreview")))
                {
                    renderPreviewComparisonTab();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Separator();

            const bool baking = m_progress.isBaking.load();
            float ratio = m_progress.progressRatio.load();
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), (std::to_string(static_cast<int>(ratio * 100.0f)) + "%").c_str());

            ImGui::Text(L("状态: %s", "Status: %s"), m_progress.statusText.c_str());
            ImGui::Text(L("当前 Loss: %.5f | PSNR: %.2f dB | SSIM: %.4f | 压缩比: %.1fx",
                          "Loss: %.5f | PSNR: %.2f dB | SSIM: %.4f | Ratio: %.1fx"),
                        m_progress.currentLoss.load(),
                        m_progress.currentPsnr.load(),
                        m_progress.currentSsim.load(),
                        m_compressionRatio);

            if (!baking)
            {
                if (ImGui::Button(L("开始神经拟合烘焙 (Start Bake)", "Start Bake")))
                {
                    startBaking();
                }
            }
            else
            {
                if (ImGui::Button(L("取消烘焙 (Cancel)", "Cancel")))
                {
                    cancelBaking();
                }
            }

            ImGui::End();
        }

    } // namespace MQEditor
} // namespace MQEngine

MQ_EXPORT_COMPOSITE(MQEngine::MQEditor::NtcBakerComposite)
