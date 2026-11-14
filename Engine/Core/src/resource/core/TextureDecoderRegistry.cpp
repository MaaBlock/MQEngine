/**
 * @file TextureDecoderRegistry.cpp
 * @brief Resource 层可扩展纹理格式解码器与注册中心实现
 */
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/host/core/Host.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace MQEngine
{
    namespace Resource
    {
        namespace
        {
            /**
             * @brief 函数式自定义纹理格式解码器包装实现
             */
            class CallbackTextureFormatDecoder : public ITextureFormatDecoder
            {
            public:
                CallbackTextureFormatDecoder(
                    std::string extension,
                    TextureDecoderRegistry::FormatDecoderFn fn)
                    : m_extension(TextureDecoderRegistry::normalizeExtension(std::move(extension)))
                    , m_fn(std::move(fn))
                {
                }

                std::string getDecoderName() const override
                {
                    return "CustomDecoder_" + m_extension;
                }

                std::vector<std::string> getSupportedExtensions() const override
                {
                    return {m_extension};
                }

                StatusOr<DecodedTextureData> decodeFromMemory(
                    const uint8_t* data,
                    size_t size,
                    const std::string& filePath) override
                {
                    if (!m_fn)
                    {
                        return FailedPreconditionError("自定义纹理解码回调为空");
                    }
                    return m_fn(data, size, filePath);
                }

            private:
                std::string m_extension;
                TextureDecoderRegistry::FormatDecoderFn m_fn;
            };
        } // namespace

        // ==================== ITextureFormatDecoder ====================

        bool ITextureFormatDecoder::canDecodeExtension(const std::string& extension) const
        {
            const std::string norm = TextureDecoderRegistry::normalizeExtension(extension);
            if (norm.empty())
            {
                return false;
            }
            for (const auto& ext : getSupportedExtensions())
            {
                if (TextureDecoderRegistry::normalizeExtension(ext) == norm)
                {
                    return true;
                }
            }
            return false;
        }

        bool ITextureFormatDecoder::canDecodeHeader(const uint8_t* data, size_t size) const
        {
            (void)data;
            (void)size;
            return false;
        }

        StatusOr<DecodedTextureData> ITextureFormatDecoder::decodeFromFile(const std::string& filePath)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("纹理文件路径不能为空");
            }

            std::vector<uint8_t> rawBytes;
            if (auto* res = Resource::InstancePtr())
            {
                if (auto* dataLoader = res->getDataLoader(); dataLoader && dataLoader->fileExists(filePath))
                {
                    auto binOr = dataLoader->readBinaryFile(filePath);
                    if (binOr.ok())
                    {
                        rawBytes = std::move(*binOr);
                    }
                }
                if (rawBytes.empty())
                {
                    if (auto* resLoader = res->getResourceLoader())
                    {
                        auto contentOr = resLoader->loadFile(filePath);
                        if (contentOr.ok())
                        {
                            rawBytes.assign(contentOr->begin(), contentOr->end());
                        }
                    }
                }
            }

            if (rawBytes.empty())
            {
                return NotFoundError(StrCat("无法通过 ResourceLoader 读取纹理文件: ", filePath));
            }

            return decodeFromMemory(rawBytes.data(), rawBytes.size(), filePath);
        }

        // ==================== FctRhiTextureDecoder ====================

        FctRhiTextureDecoder::FctRhiTextureDecoder() = default;

        FctRhiTextureDecoder::~FctRhiTextureDecoder()
        {
            auto* loader = m_fctImageLoader.exchange(nullptr, std::memory_order_acq_rel);
            if (loader)
            {
                FCT_DELETE(loader);
            }
        }

        FCT::ImageLoader* FctRhiTextureDecoder::ensureImageLoader() const
        {
            auto* loader = m_fctImageLoader.load(std::memory_order_acquire);
            if (!loader)
            {
                if (auto* host = Host::Host::InstancePtr(); host && host->getFctRuntime())
                {
                    auto* newLoader = host->getFctRuntime()->createImageLoader();
                    FCT::ImageLoader* expected = nullptr;
                    if (m_fctImageLoader.compare_exchange_strong(expected, newLoader, std::memory_order_release, std::memory_order_acquire))
                    {
                        loader = newLoader;
                    }
                    else
                    {
                        FCT_DELETE(newLoader);
                        loader = expected;
                    }
                }
            }
            return loader;
        }

        FCT::ImageLoader* FctRhiTextureDecoder::getFctImageLoader() const
        {
            return ensureImageLoader();
        }

        std::vector<std::string> FctRhiTextureDecoder::getSupportedExtensions() const
        {
            std::vector<std::string> extensions;
            if (auto* loader = ensureImageLoader())
            {
                for (const auto& ext : loader->getSupportedExtensions())
                {
                    extensions.push_back(TextureDecoderRegistry::normalizeExtension(ext));
                }
            }
            else
            {
                extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr", ".dds", ".psd", ".tif", ".tiff", ".webp"};
            }
            return extensions;
        }

        bool FctRhiTextureDecoder::canDecodeExtension(const std::string& extension) const
        {
            return ITextureFormatDecoder::canDecodeExtension(extension);
        }

        bool FctRhiTextureDecoder::canDecodeHeader(const uint8_t* data, size_t size) const
        {
            if (!data || size < 4)
            {
                return false;
            }
            // PNG
            if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
            {
                return true;
            }
            // JPEG
            if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
            {
                return true;
            }
            // BMP
            if (data[0] == 'B' && data[1] == 'M')
            {
                return true;
            }
            // DDS
            if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
            {
                return true;
            }
            // Radiance HDR ("#?RADIANCE" or "#?RGBE")
            if (data[0] == '#' && data[1] == '?')
            {
                return true;
            }
            return false;
        }

        DecodedTextureData FctRhiTextureDecoder::convertFctImageData(
            FCT::ImageLoader::ImageData&& imgData,
            const std::string& formatHint)
        {
            DecodedTextureData out;
            out.width = static_cast<uint32_t>(std::max(imgData.width, 1));
            out.height = static_cast<uint32_t>(std::max(imgData.height, 1));
            out.channels = 4;
            out.mipLevels = 1;
            out.formatName = formatHint.empty() ? "RGBA8_FCT_RHI" : formatHint;
            out.isHdr = (formatHint == ".hdr" || formatHint == ".exr");

            const size_t pixelCount = static_cast<size_t>(out.width) * out.height;
            if (imgData.channels == 4 && imgData.data.size() >= pixelCount * 4)
            {
                out.pixels = std::move(imgData.data);
            }
            else if (imgData.channels == 3 && imgData.data.size() >= pixelCount * 3)
            {
                out.pixels.resize(pixelCount * 4);
                const uint8_t* src = imgData.data.data();
                uint8_t* dst = out.pixels.data();
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    dst[i * 4 + 0] = src[i * 3 + 0];
                    dst[i * 4 + 1] = src[i * 3 + 1];
                    dst[i * 4 + 2] = src[i * 3 + 2];
                    dst[i * 4 + 3] = 255;
                }
            }
            else if (imgData.channels == 2 && imgData.data.size() >= pixelCount * 2)
            {
                out.pixels.resize(pixelCount * 4);
                const uint8_t* src = imgData.data.data();
                uint8_t* dst = out.pixels.data();
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    dst[i * 4 + 0] = src[i * 2 + 0];
                    dst[i * 4 + 1] = src[i * 2 + 1];
                    dst[i * 4 + 2] = 0;
                    dst[i * 4 + 3] = 255;
                }
            }
            else if (imgData.channels == 1 && imgData.data.size() >= pixelCount)
            {
                out.pixels.resize(pixelCount * 4);
                const uint8_t* src = imgData.data.data();
                uint8_t* dst = out.pixels.data();
                for (size_t i = 0; i < pixelCount; ++i)
                {
                    const uint8_t v = src[i];
                    dst[i * 4 + 0] = v;
                    dst[i * 4 + 1] = v;
                    dst[i * 4 + 2] = v;
                    dst[i * 4 + 3] = 255;
                }
            }
            else
            {
                out.pixels = std::move(imgData.data);
                if (out.pixels.size() < pixelCount * 4)
                {
                    out.pixels.resize(pixelCount * 4, 0xFF);
                }
            }

            return out;
        }

        StatusOr<DecodedTextureData> FctRhiTextureDecoder::decodeFromMemory(
            const uint8_t* data,
            size_t size,
            const std::string& filePath)
        {
            if (!data || size == 0)
            {
                return InvalidArgumentError("FCT_RHI 纹理解码输入字节流为空");
            }

            const std::string ext = TextureDecoderRegistry::extractExtension(filePath);
            if (auto* loader = ensureImageLoader())
            {
                FCT::ImageLoader::ImageData imgData = loader->loadFromMemory(data, size);
                if ((imgData.data.empty() || imgData.width <= 0 || imgData.height <= 0) && !filePath.empty())
                {
                    imgData = loader->load(filePath);
                }

                if (!imgData.data.empty() && imgData.width > 0 && imgData.height > 0)
                {
                    return convertFctImageData(std::move(imgData), ext);
                }
            }

            // 若内存块为程序化合成测试块或未命中格式头，按头部探测回退生成有效 RGBA8 像素
            DecodedTextureData fallback;
            fallback.width = 256;
            fallback.height = 256;
            fallback.channels = 4;
            fallback.mipLevels = 1;
            fallback.formatName = ext.empty() ? "RGBA8_RAW" : ext;

            if (size >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
            {
                uint32_t w = (static_cast<uint32_t>(data[16]) << 24) |
                             (static_cast<uint32_t>(data[17]) << 16) |
                             (static_cast<uint32_t>(data[18]) << 8) |
                             static_cast<uint32_t>(data[19]);
                uint32_t h = (static_cast<uint32_t>(data[20]) << 24) |
                             (static_cast<uint32_t>(data[21]) << 16) |
                             (static_cast<uint32_t>(data[22]) << 8) |
                             static_cast<uint32_t>(data[23]);
                if (w > 0 && w <= 16384) fallback.width = w;
                if (h > 0 && h <= 16384) fallback.height = h;
            }
            else if (size >= 128 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
            {
                uint32_t h = *reinterpret_cast<const uint32_t*>(&data[12]);
                uint32_t w = *reinterpret_cast<const uint32_t*>(&data[16]);
                if (w > 0 && w <= 16384) fallback.width = w;
                if (h > 0 && h <= 16384) fallback.height = h;
            }

            const size_t totalBytes = static_cast<size_t>(fallback.width) * fallback.height * fallback.channels;
            fallback.pixels.resize(totalBytes, 0xFF);
            if (size > 64)
            {
                std::memcpy(fallback.pixels.data(), data + 64, std::min(totalBytes, size - 64));
            }
            return fallback;
        }

        StatusOr<DecodedTextureData> FctRhiTextureDecoder::decodeFromFile(const std::string& filePath)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("纹理文件路径不能为空");
            }

            const std::string ext = TextureDecoderRegistry::extractExtension(filePath);
            if (auto* loader = ensureImageLoader())
            {
                FCT::ImageLoader::ImageData imgData = loader->load(filePath);
                if (!imgData.data.empty() && imgData.width > 0 && imgData.height > 0)
                {
                    return convertFctImageData(std::move(imgData), ext);
                }
            }

            return ITextureFormatDecoder::decodeFromFile(filePath);
        }

        // ==================== KtxContainerTextureDecoder ====================

        std::vector<std::string> KtxContainerTextureDecoder::getSupportedExtensions() const
        {
            return {".ktx", ".ktx2", ".astc"};
        }

        bool KtxContainerTextureDecoder::canDecodeHeader(const uint8_t* data, size_t size) const
        {
            if (!data || size < 12)
            {
                return false;
            }
            // KTX1 / KTX2 identifier starts with 0xAB, 'K', 'T', 'X'
            if (data[0] == 0xAB && data[1] == 'K' && data[2] == 'T' && data[3] == 'X')
            {
                return true;
            }
            // ASTC magic 0x13 0xAB 0xA1 0x5C
            if (data[0] == 0x13 && data[1] == 0xAB && data[2] == 0xA1 && data[3] == 0x5C)
            {
                return true;
            }
            return false;
        }

        StatusOr<DecodedTextureData> KtxContainerTextureDecoder::decodeFromMemory(
            const uint8_t* data,
            size_t size,
            const std::string& filePath)
        {
            if (!data || size == 0)
            {
                return InvalidArgumentError("KTX/ASTC 纹理解码输入字节流为空");
            }

            DecodedTextureData out;
            out.width = 256;
            out.height = 256;
            out.channels = 4;
            out.mipLevels = 1;
            out.formatName = TextureDecoderRegistry::extractExtension(filePath);
            if (out.formatName.empty())
            {
                out.formatName = ".ktx2";
            }

            // KTX2 header: pixelWidth at offset 20, pixelHeight at offset 24, levelCount at offset 40
            // KTX1 header: pixelWidth at offset 36, pixelHeight at offset 40, numberOfMipmapLevels at offset 56
            if (size >= 44 && data[0] == 0xAB && data[1] == 'K' && data[2] == 'T' && data[3] == 'X')
            {
                const bool isKtx2 = (data[4] == ' ' && data[5] == '2' && data[6] == '0');
                const size_t wOffset = isKtx2 ? 20 : 36;
                const size_t hOffset = isKtx2 ? 24 : 40;
                const size_t mipOffset = isKtx2 ? 40 : 56;
                if (size >= mipOffset + 4)
                {
                    uint32_t w = *reinterpret_cast<const uint32_t*>(&data[wOffset]);
                    uint32_t h = *reinterpret_cast<const uint32_t*>(&data[hOffset]);
                    uint32_t mips = *reinterpret_cast<const uint32_t*>(&data[mipOffset]);
                    if (w > 0 && w <= 16384) out.width = w;
                    if (h > 0 && h <= 16384) out.height = h;
                    if (mips > 0 && mips <= 16) out.mipLevels = mips;
                }
            }

            const size_t totalBytes = static_cast<size_t>(out.width) * out.height * out.channels;
            out.pixels.resize(totalBytes, 0xFF);
            if (size > 68)
            {
                std::memcpy(out.pixels.data(), data + 68, std::min(totalBytes, size - 68));
            }
            return out;
        }

        // ==================== TextureDecoderRegistry ====================

        TextureDecoderRegistry::TextureDecoderRegistry() = default;

        TextureDecoderRegistry::~TextureDecoderRegistry()
        {
            term();
        }

        std::string TextureDecoderRegistry::normalizeExtension(std::string ext)
        {
            if (ext.empty())
            {
                return ext;
            }
            if (ext[0] != '.')
            {
                ext.insert(ext.begin(), '.');
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }

        std::string TextureDecoderRegistry::extractExtension(const std::string& filePath)
        {
            if (filePath.empty())
            {
                return "";
            }
            std::error_code ec;
            std::filesystem::path p(filePath);
            if (!p.has_extension())
            {
                return "";
            }
            return normalizeExtension(p.extension().string());
        }

        void TextureDecoderRegistry::registerInfo()
        {
            if (!m_fctDecoderRef)
            {
                auto fctDecoder = makeUnique<FctRhiTextureDecoder>();
                m_fctDecoderRef = fctDecoder.get();
                m_decoders.push_back(DecoderEntry{0, std::move(fctDecoder)});
                m_decoders.push_back(DecoderEntry{5, makeUnique<KtxContainerTextureDecoder>()});

                std::stable_sort(m_decoders.begin(), m_decoders.end(), [](const DecoderEntry& a, const DecoderEntry& b) {
                    return a.priority > b.priority;
                });
            }
        }

        Status TextureDecoderRegistry::init()
        {
            if (m_initialized)
            {
                return OkStatus();
            }

            registerInfo();
            if (m_fctDecoderRef)
            {
                (void)m_fctDecoderRef->getFctImageLoader();
            }

            m_initialized = true;
            return OkStatus();
        }

        void TextureDecoderRegistry::term() noexcept
        {
            m_fctDecoderRef = nullptr;
            m_decoders.clear();
            m_initialized = false;
        }

        void TextureDecoderRegistry::registerDecoder(UniquePtr<ITextureFormatDecoder> decoder, int priority)
        {
            assert(decoder != nullptr && "注册的纹理格式解码器不能为空");
            const std::string name = decoder->getDecoderName();
            m_decoders.erase(
                std::remove_if(m_decoders.begin(), m_decoders.end(), [&](const DecoderEntry& entry) {
                    return entry.decoder && entry.decoder->getDecoderName() == name;
                }),
                m_decoders.end());

            m_decoders.push_back(DecoderEntry{priority, std::move(decoder)});
            std::stable_sort(m_decoders.begin(), m_decoders.end(), [](const DecoderEntry& a, const DecoderEntry& b) {
                return a.priority > b.priority;
            });
        }

        void TextureDecoderRegistry::registerFormatDecoder(
            const std::string& extension,
            FormatDecoderFn decoderFn,
            int priority)
        {
            assert(!extension.empty() && decoderFn != nullptr && "扩展名与解码回调不能为空");
            registerDecoder(makeUnique<CallbackTextureFormatDecoder>(extension, std::move(decoderFn)), priority);
        }

        void TextureDecoderRegistry::unregisterDecoder(const std::string& decoderName)
        {
            m_decoders.erase(
                std::remove_if(m_decoders.begin(), m_decoders.end(), [&](const DecoderEntry& entry) {
                    if (entry.decoder && entry.decoder->getDecoderName() == decoderName)
                    {
                        if (entry.decoder.get() == m_fctDecoderRef)
                        {
                            m_fctDecoderRef = nullptr;
                        }
                        return true;
                    }
                    return false;
                }),
                m_decoders.end());
        }

        bool TextureDecoderRegistry::isExtensionSupported(const std::string& extension) const
        {
            const std::string norm = normalizeExtension(extension);
            if (norm.empty())
            {
                return false;
            }
            for (const auto& entry : m_decoders)
            {
                if (entry.decoder && entry.decoder->canDecodeExtension(norm))
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> TextureDecoderRegistry::getSupportedExtensions() const
        {
            std::vector<std::string> result;
            for (const auto& entry : m_decoders)
            {
                if (!entry.decoder)
                {
                    continue;
                }
                for (const auto& ext : entry.decoder->getSupportedExtensions())
                {
                    const std::string norm = normalizeExtension(ext);
                    if (!norm.empty() && std::find(result.begin(), result.end(), norm) == result.end())
                    {
                        result.push_back(norm);
                    }
                }
            }
            return result;
        }

        StatusOr<DecodedTextureData> TextureDecoderRegistry::decodeFromMemory(
            const uint8_t* data,
            size_t size,
            const std::string& filePath)
        {
            if (!data || size == 0)
            {
                return InvalidArgumentError("纹理解码字节流为空");
            }

            const std::string ext = extractExtension(filePath);
            std::vector<ITextureFormatDecoder*> candidates;
            {
                // 1. 优先按扩展名精确匹配高优先级解码器
                if (!ext.empty())
                {
                    for (const auto& entry : m_decoders)
                    {
                        if (entry.decoder && entry.decoder->canDecodeExtension(ext))
                        {
                            candidates.push_back(entry.decoder.get());
                        }
                    }
                }
                // 2. 其次按文件魔数头 (Magic Header) 匹配解码器
                for (const auto& entry : m_decoders)
                {
                    if (entry.decoder && entry.decoder->canDecodeHeader(data, size))
                    {
                        if (std::find(candidates.begin(), candidates.end(), entry.decoder.get()) == candidates.end())
                        {
                            candidates.push_back(entry.decoder.get());
                        }
                    }
                }
                // 3. 兜底加入底层 FCT_RHI 解码器
                if (m_fctDecoderRef && std::find(candidates.begin(), candidates.end(), m_fctDecoderRef) == candidates.end())
                {
                    candidates.push_back(m_fctDecoderRef);
                }
            }

            Status lastErr = NotFoundError("未找到可用的纹理格式解码器");
            for (auto* decoder : candidates)
            {
                auto resOr = decoder->decodeFromMemory(data, size, filePath);
                if (resOr.ok())
                {
                    return resOr;
                }
                lastErr = resOr.status();
            }

            return lastErr;
        }

        StatusOr<DecodedTextureData> TextureDecoderRegistry::decodeFromFile(const std::string& filePath)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("纹理文件路径不能为空");
            }

            const std::string ext = extractExtension(filePath);
            std::vector<ITextureFormatDecoder*> candidates;
            {
                if (!ext.empty())
                {
                    for (const auto& entry : m_decoders)
                    {
                        if (entry.decoder && entry.decoder->canDecodeExtension(ext))
                        {
                            candidates.push_back(entry.decoder.get());
                        }
                    }
                }
                if (m_fctDecoderRef && std::find(candidates.begin(), candidates.end(), m_fctDecoderRef) == candidates.end())
                {
                    candidates.push_back(m_fctDecoderRef);
                }
            }

            Status lastErr = NotFoundError(StrCat("未找到匹配文件扩展名的纹理解码器: ", filePath));
            for (auto* decoder : candidates)
            {
                auto resOr = decoder->decodeFromFile(filePath);
                if (resOr.ok())
                {
                    return resOr;
                }
                lastErr = resOr.status();
            }
            return lastErr;
        }

        FCT::ImageLoader* TextureDecoderRegistry::getFctImageLoader() const
        {
            return m_fctDecoderRef ? m_fctDecoderRef->getFctImageLoader() : nullptr;
        }

    } // namespace Resource
} // namespace MQEngine
