/**
 * @file NtcSubsystem.cpp
 * @brief 神经纹理压缩 (NTC) 运行时子系统与 MLP 推理实现
 */
#include "NtcSubsystem.h"
#include <algorithm>
#include <cmath>

namespace MQEngine
{
    namespace Domain
    {
        namespace Effect
        {
            namespace
            {
                inline float leakyRelu(float x) noexcept
                {
                    return x > 0.0f ? x : (0.01f * x);
                }

                inline float sigmoid(float x) noexcept
                {
                    return 1.0f / (1.0f + std::exp(-std::clamp(x, -15.0f, 15.0f)));
                }
            } // namespace

            NtcSubsystem::NtcSubsystem() = default;
            NtcSubsystem::~NtcSubsystem() = default;

            void NtcSubsystem::registerPack(const std::string& path, UniquePtr<NtcMaterialPack> pack)
            {
                if (pack)
                {
                    pack->assetPath = path;
                    pack->isLoaded = true;
                    m_packs[path] = std::move(pack);
                }
            }

            void NtcSubsystem::unregisterPack(const std::string& path)
            {
                m_packs.erase(path);
            }

            const NtcMaterialPack* NtcSubsystem::getPack(const std::string& path) const noexcept
            {
                auto it = m_packs.find(path);
                if (it != m_packs.end())
                {
                    return it->second.get();
                }
                return nullptr;
            }

            bool NtcSubsystem::hasPack(const std::string& path) const noexcept
            {
                return m_packs.find(path) != m_packs.end();
            }

            void NtcSubsystem::decodePixel(const NtcMaterialPack& pack, float u, float v, float* outChannels) const
            {
                if (!outChannels) return;
                const auto& hdr = pack.header;
                if (hdr.gridWidth == 0 || hdr.gridHeight == 0 || hdr.blockSize == 0)
                {
                    outChannels[0] = outChannels[1] = outChannels[2] = outChannels[3] = 0.5f;
                    return;
                }

                float clampedU = std::clamp(u, 0.0f, 1.0f);
                float clampedV = std::clamp(v, 0.0f, 1.0f);
                float px = clampedU * static_cast<float>(hdr.resolution);
                float py = clampedV * static_cast<float>(hdr.resolution);

                uint32_t bx = std::min(static_cast<uint32_t>(px / static_cast<float>(hdr.blockSize)), hdr.gridWidth - 1);
                uint32_t by = std::min(static_cast<uint32_t>(py / static_cast<float>(hdr.blockSize)), hdr.gridHeight - 1);

                float lx = (px - static_cast<float>(bx * hdr.blockSize)) / static_cast<float>(hdr.blockSize);
                float ly = (py - static_cast<float>(by * hdr.blockSize)) / static_cast<float>(hdr.blockSize);

                const uint32_t latentOffset = (by * hdr.gridWidth + bx) * hdr.latentDims;
                const uint32_t inputDims = 2 + hdr.latentDims;
                const uint32_t hiddenDims = hdr.hiddenDims;
                const uint32_t outDims = hdr.channels;

                // 构建输入向量: [lx, ly, latent...]
                std::vector<float> inputVec(inputDims);
                inputVec[0] = lx;
                inputVec[1] = ly;
                for (uint32_t d = 0; d < hdr.latentDims; ++d)
                {
                    if (latentOffset + d < pack.latentGrid.size())
                    {
                        inputVec[2 + d] = pack.latentGrid[latentOffset + d];
                    }
                    else
                    {
                        inputVec[2 + d] = 0.0f;
                    }
                }

                if (pack.mlpWeights.empty() || pack.mlpBiases.empty())
                {
                    // 若无权重，直接通过潜特征构造占位颜色
                    outChannels[0] = std::clamp(0.5f + inputVec[2] * 0.5f, 0.0f, 1.0f);
                    outChannels[1] = std::clamp(0.5f + (hdr.latentDims > 1 ? inputVec[3] : 0.0f) * 0.5f, 0.0f, 1.0f);
                    outChannels[2] = 0.7f;
                    outChannels[3] = 1.0f;
                    return;
                }

                // 前向传播 Layer 1: inputDims -> hiddenDims
                std::vector<float> h1(hiddenDims, 0.0f);
                size_t wOffset = 0;
                size_t bOffset = 0;

                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float sum = (bOffset + j < pack.mlpBiases.size()) ? pack.mlpBiases[bOffset + j] : 0.0f;
                    for (uint32_t i = 0; i < inputDims; ++i)
                    {
                        size_t wIdx = wOffset + j * inputDims + i;
                        if (wIdx < pack.mlpWeights.size())
                        {
                            sum += pack.mlpWeights[wIdx] * inputVec[i];
                        }
                    }
                    h1[j] = leakyRelu(sum);
                }
                wOffset += hiddenDims * inputDims;
                bOffset += hiddenDims;

                // 前向传播 Layer 2: hiddenDims -> hiddenDims
                std::vector<float> h2(hiddenDims, 0.0f);
                for (uint32_t j = 0; j < hiddenDims; ++j)
                {
                    float sum = (bOffset + j < pack.mlpBiases.size()) ? pack.mlpBiases[bOffset + j] : 0.0f;
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        size_t wIdx = wOffset + j * hiddenDims + i;
                        if (wIdx < pack.mlpWeights.size())
                        {
                            sum += pack.mlpWeights[wIdx] * h1[i];
                        }
                    }
                    h2[j] = leakyRelu(sum);
                }
                wOffset += hiddenDims * hiddenDims;
                bOffset += hiddenDims;

                // 输出层: hiddenDims -> outDims
                for (uint32_t k = 0; k < outDims; ++k)
                {
                    float sum = (bOffset + k < pack.mlpBiases.size()) ? pack.mlpBiases[bOffset + k] : 0.0f;
                    for (uint32_t i = 0; i < hiddenDims; ++i)
                    {
                        size_t wIdx = wOffset + k * hiddenDims + i;
                        if (wIdx < pack.mlpWeights.size())
                        {
                            sum += pack.mlpWeights[wIdx] * h2[i];
                        }
                    }
                    outChannels[k] = sigmoid(sum);
                }
            }

            void NtcSubsystem::decodeBlock(const NtcMaterialPack& pack, uint32_t blockX, uint32_t blockY, float* outBlockPixels) const
            {
                if (!outBlockPixels) return;
                const auto& hdr = pack.header;
                const uint32_t bs = hdr.blockSize;
                const float invRes = 1.0f / static_cast<float>(hdr.resolution);

                for (uint32_t ly = 0; ly < bs; ++ly)
                {
                    for (uint32_t lx = 0; lx < bs; ++lx)
                    {
                        float u = static_cast<float>(blockX * bs + lx) * invRes;
                        float v = static_cast<float>(blockY * bs + ly) * invRes;
                        float* pixelPtr = outBlockPixels + (ly * bs + lx) * hdr.channels;
                        decodePixel(pack, u, v, pixelPtr);
                    }
                }
            }

        } // namespace Effect
    } // namespace Domain
} // namespace MQEngine
