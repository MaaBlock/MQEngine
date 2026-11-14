/**
 * @file NtcBakerComposite.h
 * @brief 神经纹理压缩 (Neural Texture Compression, NTC) 离线烘焙与预览 Composite（tags: [Editor, ImGui]，独立编译为 NtcBakerComposite.dll）
 */
#ifndef MQENGINE_MQ_EDITOR_NTC_BAKER_COMPOSITE_H
#define MQENGINE_MQ_EDITOR_NTC_BAKER_COMPOSITE_H

#include "../../MQEditorApi.h"
#include "NtcBakerTypes.h"

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/asset/interface/Composite.h>

#include <string>
#include <vector>

namespace MQEngine
{
    namespace EditorBase
    {
        class EditorUiComposite;
    } // namespace EditorBase

    namespace MQEditor
    {
        /**
         * @brief 神经纹理压缩 (NTC) 离线烘焙工具 Composite：
         *        提供多通道贴图打包、MLP 网络结构超参配置、后台帧步进烘焙进度监控、
         *        原图与神经解压缩画面的分屏对比及残差热力图预览
         */
        class NTC_BAKER_COMPOSITE_API NtcBakerComposite : public Asset::Composite
        {
        public:
            NtcBakerComposite();
            ~NtcBakerComposite() override;

            NtcBakerComposite(const NtcBakerComposite&) = delete;
            NtcBakerComposite& operator=(const NtcBakerComposite&) = delete;

            Status onLoad(Runtime::Engine* engine) override;
            Status onInit(Runtime::Engine* engine) override;
            void onTick(float deltaTime) override;
            void onUnload() override;

            std::string getName() const override { return "NtcBakerComposite"; }
            std::string getPluginName() const override { return "MQEditor"; }
            std::vector<std::string> getDependencies() const override { return {"EditorUiComposite"}; }
            std::vector<std::string> getRequiredTags() const override { return {"Editor", "ImGui"}; }

            // ==================== 烘焙控制 ====================

            void startBaking();
            void cancelBaking();
            bool isBaking() const noexcept { return m_progress.isBaking.load(); }

        private:
            // UI 面板、超参配置页与 PSNR 分屏对比预览器 (NtcBakerComposite.cpp)
            void renderBakerPanel();
            void renderConfigTab();
            void renderPreviewComparisonTab();

            // MLP 潜特征网格编码器、BC7 块压缩器与 .ntc/.dds 二进制写出 (NtcBakerCodec.cpp)
            void loadOrSynthesizeSourceTextures();
            void initMlpWeightsAndLatents();
            void updatePreviewBuffer();
            void encodeBc7BlockPreview();
            void executeBakeStep();
            void writeNtcBinaryPackage(float finalPsnr, float finalMse);

        private:
            EditorBase::EditorUiComposite* m_editorUiComposite = nullptr;

            NtcBakeConfig m_config;
            NtcBakeMetrics m_progress;

            float m_splitSliderPos = 0.5f;
            bool m_showErrorHeatmap = false;
            float m_compressionRatio = 14.8f;
            size_t m_rawBytes = 16 * 1024 * 1024;
            size_t m_ntcBytes = 1108 * 1024;

            // 原始输入与预览缓存 (RGBA 浮点与 RGBA32 格式)
            std::vector<float> m_sourcePixels;
            std::vector<uint32_t> m_previewSourcePixels;
            std::vector<uint32_t> m_previewDecodedPixels;
            uint32_t m_previewWidth = 128;
            uint32_t m_previewHeight = 128;

            // 潜特征分块网格与 MLP 权重张量
            uint32_t m_blockSize = 8;
            uint32_t m_gridWidth = 128;
            uint32_t m_gridHeight = 128;
            uint32_t m_latentDims = 4;
            uint32_t m_inputDims = 6;
            std::vector<float> m_latentGrid;
            std::vector<float> m_mlpWeights;
            std::vector<float> m_mlpBiases;

            // Adam 优化器一阶与二阶动量状态
            std::vector<float> m_weightM;
            std::vector<float> m_weightV;
            std::vector<float> m_biasM;
            std::vector<float> m_biasV;
            std::vector<float> m_latentM;
            std::vector<float> m_latentV;
            uint32_t m_optimizerStep = 0;
        };

    } // namespace MQEditor
} // namespace MQEngine

#endif // MQENGINE_MQ_EDITOR_NTC_BAKER_COMPOSITE_H
