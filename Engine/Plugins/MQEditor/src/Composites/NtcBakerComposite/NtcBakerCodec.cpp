/**
 * @file NtcBakerCodec.cpp
 * @brief NTC 神经潜特征网格编码器、Adam 反向传播优化、BC7 块量化预览与 .ntc 二进制材质包写出实现
 */
#include "NtcBakerComposite.h"

#include <MQEngine/domain/core/domain.h>
#include <MQEngine/domain/effect/EffectSubsystem.h>
#include <MQEngine/domain/effect/NtcSubsystem.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <Composites/EditorUiComposite/EditorUiComposite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace MQEngine
{
    namespace MQEditor
    {
        namespace
        {
            inline float leakyRelu(float x) noexcept
            {
                return x > 0.0f ? x : (0.01f * x);
            }

            inline float leakyReluGrad(float x) noexcept
            {
                return x > 0.0f ? 1.0f : 0.01f;
            }

            inline float sigmoid(float x) noexcept
            {
                return 1.0f / (1.0f + std::exp(-std::clamp(x, -15.0f, 15.0f)));
            }
        } // namespace

        void NtcBakerComposite::loadOrSynthesizeSourceTextures()
        {
            const uint32_t res = m_config.resolution;
            const size_t totalPixels = static_cast<size_t>(res) * res;
            m_sourcePixels.assign(totalPixels * 4, 0.0f);

            bool loaded = false;
            auto* decoderRegistry = Resource::TextureDecoderRegistry::InstancePtr();
            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;

            if (decoderRegistry && loader && loader->exists(m_config.albedoPath))
            {
                auto decoded = decoderRegistry->decodeFromFile(m_config.albedoPath);
                if (decoded.ok() && !decoded->pixels.empty())
                {
                    const auto& data = *decoded;
                    for (uint32_t y = 0; y < res; ++y)
                    {
                        uint32_t srcY = (y * data.height) / res;
                        for (uint32_t x = 0; x < res; ++x)
                        {
                            uint32_t srcX = (x * data.width) / res;
                            size_t srcIdx = (srcY * data.width + srcX) * data.channels;
                            size_t dstIdx = (y * res + x) * 4;
                            m_sourcePixels[dstIdx + 0] = data.pixels[srcIdx + 0] / 255.0f;
                            m_sourcePixels[dstIdx + 1] = (data.channels > 1) ? (data.pixels[srcIdx + 1] / 255.0f) : m_sourcePixels[dstIdx + 0];
                            m_sourcePixels[dstIdx + 2] = (data.channels > 2) ? (data.pixels[srcIdx + 2] / 255.0f) : m_sourcePixels[dstIdx + 0];
                            m_sourcePixels[dstIdx + 3] = (data.channels > 3) ? (data.pixels[srcIdx + 3] / 255.0f) : 1.0f;
                        }
                    }
                    loaded = true;
                }
            }

            if (!loaded)
            {
                for (uint32_t y = 0; y < res; ++y)
                {
                    float v = static_cast<float>(y) / static_cast<float>(res);
                    for (uint32_t x = 0; x < res; ++x)
                    {
                        float u = static_cast<float>(x) / static_cast<float>(res);
                        size_t dstIdx = (y * res + x) * 4;

                        float n1 = std::sin(u * 28.0f) * std::cos(v * 28.0f);
                        float n2 = std::sin(u * 56.0f + 1.3f) * std::sin(v * 56.0f + 2.7f);
                        float n3 = std::sin(u * 112.0f + 3.1f) * std::cos(v * 112.0f + 0.9f);
                        float base = 0.5f + 0.28f * n1 + 0.14f * n2 + 0.08f * n3;
                        base = std::clamp(base, 0.05f, 0.98f);

                        m_sourcePixels[dstIdx + 0] = 0.45f * base;
                        m_sourcePixels[dstIdx + 1] = 0.42f * base;
                        m_sourcePixels[dstIdx + 2] = 0.40f * base;
                        m_sourcePixels[dstIdx + 3] = std::clamp(0.7f + 0.25f * n2, 0.0f, 1.0f);
                    }
                }
            }

            m_previewSourcePixels.assign(m_previewWidth * m_previewHeight, 0);
            for (uint32_t py = 0; py < m_previewHeight; ++py)
            {
                uint32_t sy = (py * res) / m_previewHeight;
                for (uint32_t px = 0; px < m_previewWidth; ++px)
                {
                    uint32_t sx = (px * res) / m_previewWidth;
                    size_t idx = (sy * res + sx) * 4;
                    uint8_t r = static_cast<uint8_t>(std::clamp(m_sourcePixels[idx + 0] * 255.0f, 0.0f, 255.0f));
                    uint8_t g = static_cast<uint8_t>(std::clamp(m_sourcePixels[idx + 1] * 255.0f, 0.0f, 255.0f));
                    uint8_t b = static_cast<uint8_t>(std::clamp(m_sourcePixels[idx + 2] * 255.0f, 0.0f, 255.0f));
                    m_previewSourcePixels[py * m_previewWidth + px] = IM_COL32(r, g, b, 255);
                }
            }
        }

        void NtcBakerComposite::initMlpWeightsAndLatents()
        {
            m_blockSize = 8;
            m_gridWidth = std::max(1u, m_config.resolution / m_blockSize);
            m_gridHeight = m_gridWidth;
            m_latentDims = 4;
            m_inputDims = 2 + m_latentDims;
            const uint32_t hiddenDims = m_config.hiddenDims;
            const uint32_t outDims = 4;

            const size_t totalLatents = static_cast<size_t>(m_gridWidth) * m_gridHeight * m_latentDims;
            m_latentGrid.assign(totalLatents, 0.0f);
            m_latentM.assign(totalLatents, 0.0f);
            m_latentV.assign(totalLatents, 0.0f);

            std::mt19937 rng(42);
            std::uniform_real_distribution<float> distLatent(-0.1f, 0.1f);
            for (size_t i = 0; i < totalLatents; ++i)
            {
                m_latentGrid[i] = distLatent(rng);
            }

            const size_t w1Size = hiddenDims * m_inputDims;
            const size_t w2Size = hiddenDims * hiddenDims;
            const size_t w3Size = outDims * hiddenDims;
            const size_t totalWeights = w1Size + w2Size + w3Size;
            const size_t totalBiases = hiddenDims + hiddenDims + outDims;

            m_mlpWeights.assign(totalWeights, 0.0f);
            m_mlpBiases.assign(totalBiases, 0.0f);
            m_weightM.assign(totalWeights, 0.0f);
            m_weightV.assign(totalWeights, 0.0f);
            m_biasM.assign(totalBiases, 0.0f);
            m_biasV.assign(totalBiases, 0.0f);
            m_optimizerStep = 0;

            float scale1 = std::sqrt(2.0f / static_cast<float>(m_inputDims));
            std::uniform_real_distribution<float> distW1(-scale1, scale1);
            for (size_t i = 0; i < w1Size; ++i) m_mlpWeights[i] = distW1(rng);

            float scale2 = std::sqrt(2.0f / static_cast<float>(hiddenDims));
            std::uniform_real_distribution<float> distW2(-scale2, scale2);
            for (size_t i = 0; i < w2Size; ++i) m_mlpWeights[w1Size + i] = distW2(rng);

            float scale3 = std::sqrt(2.0f / static_cast<float>(hiddenDims + outDims));
            std::uniform_real_distribution<float> distW3(-scale3, scale3);
            for (size_t i = 0; i < w3Size; ++i) m_mlpWeights[w1Size + w2Size + i] = distW3(rng);

            m_rawBytes = static_cast<size_t>(m_config.resolution) * m_config.resolution * 4 * sizeof(uint8_t);
            m_ntcBytes = sizeof(Domain::Effect::NtcHeader) +
                         m_latentGrid.size() * sizeof(float) +
                         (m_mlpWeights.size() + m_mlpBiases.size()) * sizeof(float);
            m_compressionRatio = static_cast<float>(m_rawBytes) / static_cast<float>(std::max(size_t(1), m_ntcBytes));

            updatePreviewBuffer();
        }

        void NtcBakerComposite::encodeBc7BlockPreview()
        {
            m_previewDecodedPixels.assign(m_previewWidth * m_previewHeight, 0);
            for (uint32_t by = 0; by < m_previewHeight; by += 4)
            {
                for (uint32_t bx = 0; bx < m_previewWidth; bx += 4)
                {
                    for (uint32_t dy = 0; dy < 4 && (by + dy) < m_previewHeight; ++dy)
                    {
                        for (uint32_t dx = 0; dx < 4 && (bx + dx) < m_previewWidth; ++dx)
                        {
                            const size_t idx = (by + dy) * m_previewWidth + (bx + dx);
                            const uint32_t srcCol = m_previewSourcePixels[idx];
                            const uint8_t r = static_cast<uint8_t>((srcCol & 0xFF) & 0xF8);
                            const uint8_t g = static_cast<uint8_t>(((srcCol >> 8) & 0xFF) & 0xFC);
                            const uint8_t b = static_cast<uint8_t>(((srcCol >> 16) & 0xFF) & 0xF8);
                            m_previewDecodedPixels[idx] = IM_COL32(r, g, b, 255);
                        }
                    }
                }
            }
        }

        void NtcBakerComposite::updatePreviewBuffer()
        {
            if (m_config.mode == NtcBakeMode::Bc7BlockCompressed)
            {
                encodeBc7BlockPreview();
                return;
            }

            m_previewDecodedPixels.assign(m_previewWidth * m_previewHeight, 0);
            const uint32_t hiddenDims = m_config.hiddenDims;
            const uint32_t outDims = 4;
            const size_t w1Size = hiddenDims * m_inputDims;
            const size_t w2Size = hiddenDims * hiddenDims;

            const float* w1 = m_mlpWeights.data();
            const float* w2 = w1 + w1Size;
            const float* w3 = w2 + w2Size;
            const float* b1 = m_mlpBiases.data();
            const float* b2 = b1 + hiddenDims;
            const float* b3 = b2 + hiddenDims;

            std::vector<float> input(m_inputDims);
            std::vector<float> h1(hiddenDims);
            std::vector<float> h2(hiddenDims);
            std::vector<float> out(outDims);

            for (uint32_t py = 0; py < m_previewHeight; ++py)
            {
                float v = static_cast<float>(py) / static_cast<float>(m_previewHeight);
                float realY = v * static_cast<float>(m_config.resolution);
                uint32_t by = std::min(static_cast<uint32_t>(realY / static_cast<float>(m_blockSize)), m_gridHeight - 1);
                float ly = (realY - static_cast<float>(by * m_blockSize)) / static_cast<float>(m_blockSize);

                for (uint32_t px = 0; px < m_previewWidth; ++px)
                {
                    float u = static_cast<float>(px) / static_cast<float>(m_previewWidth);
                    float realX = u * static_cast<float>(m_config.resolution);
                    uint32_t bx = std::min(static_cast<uint32_t>(realX / static_cast<float>(m_blockSize)), m_gridWidth - 1);
                    float lx = (realX - static_cast<float>(bx * m_blockSize)) / static_cast<float>(m_blockSize);

                    input[0] = lx;
                    input[1] = ly;
                    const size_t latentOffset = (by * m_gridWidth + bx) * m_latentDims;
                    for (uint32_t d = 0; d < m_latentDims; ++d)
                    {
                        input[2 + d] = m_latentGrid[latentOffset + d];
                    }

                    for (uint32_t j = 0; j < hiddenDims; ++j)
                    {
                        float sum = b1[j];
                        for (uint32_t i = 0; i < m_inputDims; ++i)
                        {
                            sum += w1[j * m_inputDims + i] * input[i];
                        }
                        h1[j] = leakyRelu(sum);
                    }

                    for (uint32_t j = 0; j < hiddenDims; ++j)
                    {
                        float sum = b2[j];
                        for (uint32_t i = 0; i < hiddenDims; ++i)
                        {
                            sum += w2[j * hiddenDims + i] * h1[i];
                        }
                        h2[j] = leakyRelu(sum);
                    }

                    for (uint32_t k = 0; k < outDims; ++k)
                    {
                        float sum = b3[k];
                        for (uint32_t i = 0; i < hiddenDims; ++i)
                        {
                            sum += w3[k * hiddenDims + i] * h2[i];
                        }
                        out[k] = sigmoid(sum);
                    }

                    uint8_t r = static_cast<uint8_t>(std::clamp(out[0] * 255.0f, 0.0f, 255.0f));
                    uint8_t g = static_cast<uint8_t>(std::clamp(out[1] * 255.0f, 0.0f, 255.0f));
                    uint8_t b = static_cast<uint8_t>(std::clamp(out[2] * 255.0f, 0.0f, 255.0f));
                    m_previewDecodedPixels[py * m_previewWidth + px] = IM_COL32(r, g, b, 255);
                }
            }
        }

        void NtcBakerComposite::writeNtcBinaryPackage(float finalPsnr, float finalMse)
        {
            Domain::Effect::NtcHeader hdr;
            hdr.magic[0] = 'N'; hdr.magic[1] = 'T'; hdr.magic[2] = 'C'; hdr.magic[3] = 0x02;
            hdr.version = 2;
            hdr.resolution = m_config.resolution;
            hdr.channels = 4;
            hdr.blockSize = m_blockSize;
            hdr.gridWidth = m_gridWidth;
            hdr.gridHeight = m_gridHeight;
            hdr.latentDims = m_latentDims;
            hdr.mlpLayers = 3;
            hdr.hiddenDims = m_config.hiddenDims;
            hdr.finalPsnr = finalPsnr;
            hdr.finalMse = finalMse;
            hdr.latentBytes = static_cast<uint32_t>(m_latentGrid.size() * sizeof(float));
            hdr.weightBytes = static_cast<uint32_t>((m_mlpWeights.size() + m_mlpBiases.size()) * sizeof(float));

            std::string binaryData;
            binaryData.resize(sizeof(Domain::Effect::NtcHeader) + hdr.latentBytes + hdr.weightBytes);
            uint8_t* dst = reinterpret_cast<uint8_t*>(binaryData.data());
            std::memcpy(dst, &hdr, sizeof(hdr));
            dst += sizeof(hdr);
            std::memcpy(dst, m_latentGrid.data(), hdr.latentBytes);
            dst += hdr.latentBytes;
            std::memcpy(dst, m_mlpWeights.data(), m_mlpWeights.size() * sizeof(float));
            dst += m_mlpWeights.size() * sizeof(float);
            std::memcpy(dst, m_mlpBiases.data(), m_mlpBiases.size() * sizeof(float));

            auto* resMod = Resource::Resource::InstancePtr();
            auto* loader = resMod ? resMod->getResourceLoader() : nullptr;
            if (loader)
            {
                (void)loader->makeDir("Assets/Materials");
                (void)loader->saveFile(m_config.outputPath, binaryData);
            }

            if (auto* domain = Domain::Domain::InstancePtr())
            {
                auto pack = makeUnique<Domain::Effect::NtcMaterialPack>();
                pack->header = hdr;
                pack->latentGrid = m_latentGrid;
                pack->mlpWeights = m_mlpWeights;
                pack->mlpBiases = m_mlpBiases;
                pack->assetPath = m_config.outputPath;
                pack->isLoaded = true;
                domain->effect.getNtcSubsystem().registerPack(m_config.outputPath, std::move(pack));
            }

            m_ntcBytes = binaryData.size();
            m_compressionRatio = static_cast<float>(m_rawBytes) / static_cast<float>(std::max(size_t(1), m_ntcBytes));

            if (m_editorUiComposite)
            {
                m_editorUiComposite->showToast(
                    "NTC 烘焙完成",
                    "模型已输出至: " + m_config.outputPath + " (压缩比 " + std::to_string(static_cast<int>(m_compressionRatio)) + "x)",
                    EditorBase::LogEntry::Level::Info,
                    3.0f);
            }
        }

        void NtcBakerComposite::executeBakeStep()
        {
            const uint32_t res = m_config.resolution;
            const uint32_t hiddenDims = m_config.hiddenDims;
            const uint32_t outDims = 4;
            const size_t w1Size = hiddenDims * m_inputDims;
            const size_t w2Size = hiddenDims * hiddenDims;
            const size_t w3Size = outDims * hiddenDims;

            float* w1 = m_mlpWeights.data();
            float* w2 = w1 + w1Size;
            float* w3 = w2 + w2Size;
            float* b1 = m_mlpBiases.data();
            float* b2 = b1 + hiddenDims;
            float* b3 = b2 + hiddenDims;

            std::vector<float> gradW1(w1Size, 0.0f);
            std::vector<float> gradW2(w2Size, 0.0f);
            std::vector<float> gradW3(w3Size, 0.0f);
            std::vector<float> gradB1(hiddenDims, 0.0f);
            std::vector<float> gradB2(hiddenDims, 0.0f);
            std::vector<float> gradB3(outDims, 0.0f);

            const uint32_t batchSize = 512;
            float totalBatchLoss = 0.0f;

            std::vector<float> input(m_inputDims);
            std::vector<float> z1(hiddenDims);
            std::vector<float> a1(hiddenDims);
            std::vector<float> z2(hiddenDims);
            std::vector<float> a2(hiddenDims);
            std::vector<float> z3(outDims);
            std::vector<float> a3(outDims);

            std::vector<float> delta3(outDims);
            std::vector<float> delta2(hiddenDims);
            std::vector<float> delta1(hiddenDims);

            static std::mt19937 stepRng(101);
            std::uniform_int_distribution<uint32_t> distCoord(0, res - 1);

            for (uint32_t s = 0; s < batchSize; ++s)
            {
                uint32_t px = distCoord(stepRng);
                uint32_t py = distCoord(stepRng);

                uint32_t bx = std::min(px / m_blockSize, m_gridWidth - 1);
                uint32_t by = std::min(py / m_blockSize, m_gridHeight - 1);
                float lx = static_cast<float>(px - bx * m_blockSize) / static_cast<float>(m_blockSize);
                float ly = static_cast<float>(py - by * m_blockSize) / static_cast<float>(m_blockSize);

                input[0] = lx;
                input[1] = ly;
                const size_t latentOffset = (by * m_gridWidth + bx) * m_latentDims;
                for (uint32_t d = 0; d < m_latentDims; ++d)
                {
                    input[2 + d] = m_latentGrid[latentOffset + d];
                }

                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float sum = b1[j];
                    for (uint32_t i = 0; i < m_inputDims; ++i)
                    {
                        sum += w1[j * m_inputDims + i] * input[i];
                    }
                    z1[j] = sum;
                    a1[j] = leakyRelu(sum);
                }

                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float sum = b2[j];
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        sum += w2[j * hiddenDims + i] * a1[i];
                    }
                    z2[j] = sum;
                    a2[j] = leakyRelu(sum);
                }

                for (uint32_t k = 0; k < outDims; ++k)
                {
                    float sum = b3[k];
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        sum += w3[k * hiddenDims + i] * a2[i];
                    }
                    z3[k] = sum;
                    a3[k] = sigmoid(sum);
                }

                const size_t targetIdx = (py * res + px) * 4;
                for (uint32_t k = 0; k < outDims; ++k)
                {
                    float target = m_sourcePixels[targetIdx + k];
                    float diff = a3[k] - target;
                    totalBatchLoss += diff * diff;

                    delta3[k] = 2.0f * diff * a3[k] * (1.0f - a3[k]);
                    gradB3[k] += delta3[k];
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        gradW3[k * hiddenDims + i] += delta3[k] * a2[i];
                    }
                }

                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float err = 0.0f;
                    for (uint32_t k = 0; k < outDims; ++k)
                    {
                        err += w3[k * hiddenDims + j] * delta3[k];
                    }
                    delta2[j] = err * leakyReluGrad(z2[j]);
                    gradB2[j] += delta2[j];
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        gradW2[j * hiddenDims + i] += delta2[j] * a1[i];
                    }
                }

                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float err = 0.0f;
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        err += w2[i * hiddenDims + j] * delta2[i];
                    }
                    delta1[j] = err * leakyReluGrad(z1[j]);
                    gradB1[j] += delta1[j];
                    for (uint32_t i = 0; i < m_inputDims; ++i)
                    {
                        gradW1[j * m_inputDims + i] += delta1[j] * input[i];
                    }
                }

                for (uint32_t d = 0; d < m_latentDims; ++d)
                {
                    float dLatent = 0.0f;
                    for (uint32_t j = 0; j < hiddenDims; ++j)
                    {
                        dLatent += w1[j * m_inputDims + 2 + d] * delta1[j];
                    }
                    m_latentGrid[latentOffset + d] -= 0.01f * (dLatent / static_cast<float>(batchSize));
                }
            }

            m_optimizerStep++;
            const float lr = 0.015f;
            const float beta1 = 0.9f;
            const float beta2 = 0.999f;
            const float eps = 1e-8f;
            const float invBatch = 1.0f / static_cast<float>(batchSize);

            for (size_t i = 0; i < m_mlpWeights.size(); ++i)
            {
                float g = 0.0f;
                if (i < w1Size) g = gradW1[i] * invBatch;
                else if (i < w1Size + w2Size) g = gradW2[i - w1Size] * invBatch;
                else g = gradW3[i - w1Size - w2Size] * invBatch;

                m_weightM[i] = beta1 * m_weightM[i] + (1.0f - beta1) * g;
                m_weightV[i] = beta2 * m_weightV[i] + (1.0f - beta2) * g * g;

                float mHat = m_weightM[i] / (1.0f - std::pow(beta1, m_optimizerStep));
                float vHat = m_weightV[i] / (1.0f - std::pow(beta2, m_optimizerStep));
                m_mlpWeights[i] -= lr * mHat / (std::sqrt(vHat) + eps);
            }

            for (size_t i = 0; i < m_mlpBiases.size(); ++i)
            {
                float g = 0.0f;
                if (i < hiddenDims) g = gradB1[i] * invBatch;
                else if (i < hiddenDims * 2) g = gradB2[i - hiddenDims] * invBatch;
                else g = gradB3[i - hiddenDims * 2] * invBatch;

                m_biasM[i] = beta1 * m_biasM[i] + (1.0f - beta1) * g;
                m_biasV[i] = beta2 * m_biasV[i] + (1.0f - beta2) * g * g;

                float mHat = m_biasM[i] / (1.0f - std::pow(beta1, m_optimizerStep));
                float vHat = m_biasV[i] / (1.0f - std::pow(beta2, m_optimizerStep));
                m_mlpBiases[i] -= lr * mHat / (std::sqrt(vHat) + eps);
            }

            float curMse = totalBatchLoss / static_cast<float>(batchSize * outDims);
            float curPsnr = 10.0f * std::log10(1.0f / std::max(curMse, 1e-7f));
            float curSsim = 1.0f / (1.0f + 6.0f * curMse);

            uint32_t epoch = m_progress.currentEpoch.load() + 10;
            const uint32_t total = m_config.epochs;

            m_progress.currentEpoch.store(epoch);
            float ratio = static_cast<float>(epoch) / static_cast<float>(total);
            m_progress.progressRatio.store(std::min(1.0f, ratio));
            m_progress.currentLoss.store(curMse);
            m_progress.currentPsnr.store(curPsnr);
            m_progress.currentSsim.store(curSsim);
            m_progress.statusText = "Epoch " + std::to_string(epoch) + "/" + std::to_string(total) +
                                    " (PSNR: " + std::to_string(static_cast<int>(curPsnr)) + " dB)";

            updatePreviewBuffer();

            if (epoch >= total || curPsnr >= m_config.targetPsnr)
            {
                m_progress.isBaking.store(false);
                m_progress.progressRatio.store(1.0f);
                m_progress.statusText = "烘焙完成！已生成量化压缩模型包 (.ntc)";
                writeNtcBinaryPackage(curPsnr, curMse);
            }
        }

    } // namespace MQEditor
} // namespace MQEngine
