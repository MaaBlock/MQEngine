/**
 * @file TextureDecoderRegistry.h
 * @brief Resource 层可扩展纹理格式解码器接口、FCT_RHI 底层图像解码器适配与全局解码器注册表
 */
#ifndef MQENGINE_RESOURCE_CORE_TEXTUREDECODERREGISTRY_H
#define MQENGINE_RESOURCE_CORE_TEXTUREDECODERREGISTRY_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/thirdparty/thirdparty.h>
#include <MQEngine/resource/classdef.h>

#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Resource
    {
        /**
         * @brief CPU 端解码完成的通用像素缓冲与元数据描述
         */
        struct DecodedTextureData
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t channels = 4;
            uint32_t mipLevels = 1;
            bool isHdr = false;
            std::string formatName;
            std::vector<uint8_t> pixels;
        };

        /**
         * @brief 纹理格式解码器抽象基类：
         *        支持按文件扩展名或二进制魔数头（Magic Header）匹配并执行图像解码，
         *        外部模块或插件可派生此类并注册至 Resource::TextureDecoderRegistry 以扩展任意纹理格式。
         */
        class ENGINE_API ITextureFormatDecoder
        {
        public:
            virtual ~ITextureFormatDecoder() = default;

            virtual std::string getDecoderName() const = 0;
            virtual std::vector<std::string> getSupportedExtensions() const = 0;

            virtual bool canDecodeExtension(const std::string& extension) const;
            virtual bool canDecodeHeader(const uint8_t* data, size_t size) const;

            virtual StatusOr<DecodedTextureData> decodeFromMemory(
                const uint8_t* data,
                size_t size,
                const std::string& filePath = "") = 0;

            virtual StatusOr<DecodedTextureData> decodeFromFile(const std::string& filePath);
        };

        /**
         * @brief 基于 FCT_RHI 底层 ImageLoader (FreeImage / Stb) 的内置标准纹理格式解码器
         */
        class ENGINE_API FctRhiTextureDecoder : public ITextureFormatDecoder
        {
        public:
            FctRhiTextureDecoder();
            ~FctRhiTextureDecoder() override;

            std::string getDecoderName() const override { return "FCT_RHI_ImageLoader"; }
            std::vector<std::string> getSupportedExtensions() const override;

            bool canDecodeExtension(const std::string& extension) const override;
            bool canDecodeHeader(const uint8_t* data, size_t size) const override;

            StatusOr<DecodedTextureData> decodeFromMemory(
                const uint8_t* data,
                size_t size,
                const std::string& filePath = "") override;

            StatusOr<DecodedTextureData> decodeFromFile(const std::string& filePath) override;

            FCT::ImageLoader* getFctImageLoader() const;

        private:
            FCT::ImageLoader* ensureImageLoader() const;
            static DecodedTextureData convertFctImageData(
                FCT::ImageLoader::ImageData&& imgData,
                const std::string& formatHint);

        private:
            mutable std::atomic<FCT::ImageLoader*> m_fctImageLoader{nullptr};
        };

        /**
         * @brief 内置 KTX / KTX2 / ASTC 容器扩展格式解码器
         */
        class ENGINE_API KtxContainerTextureDecoder : public ITextureFormatDecoder
        {
        public:
            std::string getDecoderName() const override { return "KtxContainerDecoder"; }
            std::vector<std::string> getSupportedExtensions() const override;
            bool canDecodeHeader(const uint8_t* data, size_t size) const override;

            StatusOr<DecodedTextureData> decodeFromMemory(
                const uint8_t* data,
                size_t size,
                const std::string& filePath = "") override;
        };

        /**
         * @brief Resource 层纹理格式解码器注册中心：
         *        统管内置 FCT_RHI 图像解码器与运行时动态扩展的自定义格式解码器，
         *        按优先级与扩展名/文件头自动路由分发纹理格式解析。
         */
        class ENGINE_API TextureDecoderRegistry : public CoreEngineManager<TextureDecoderRegistry>
        {
        public:
            using FormatDecoderFn = std::function<StatusOr<DecodedTextureData>(
                const uint8_t* data,
                size_t size,
                const std::string& filePath)>;

            TextureDecoderRegistry();
            ~TextureDecoderRegistry();

            TextureDecoderRegistry(const TextureDecoderRegistry&) = delete;
            TextureDecoderRegistry& operator=(const TextureDecoderRegistry&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            /**
             * @brief 注册派生自 ITextureFormatDecoder 的解码器实例
             * @param decoder 解码器独占指针
             * @param priority 匹配优先级（数值越大越优先匹配，自定义扩展默认高于内置通用解码器）
             */
            void registerDecoder(UniquePtr<ITextureFormatDecoder> decoder, int priority = 10);

            /**
             * @brief 按扩展名快速注册函数式纹理格式解码器（支持运行时扩展新纹理格式）
             * @param extension 扩展名（如 ".ktx2", ".dds", ".basis"，不区分大小写）
             * @param decoderFn 解码回调
             * @param priority 优先级
             */
            void registerFormatDecoder(
                const std::string& extension,
                FormatDecoderFn decoderFn,
                int priority = 20);

            /**
             * @brief 按名称注销解码器
             */
            void unregisterDecoder(const std::string& decoderName);

            /**
             * @brief 判断指定文件扩展名是否受已注册解码器支持
             */
            bool isExtensionSupported(const std::string& extension) const;

            /**
             * @brief 获取当前 Resource 层已注册支持的全部纹理格式扩展名
             */
            std::vector<std::string> getSupportedExtensions() const;

            /**
             * @brief 从内存二进制字节流自动匹配解码器并解析像素数据
             */
            StatusOr<DecodedTextureData> decodeFromMemory(
                const uint8_t* data,
                size_t size,
                const std::string& filePath = "");

            /**
             * @brief 从磁盘/虚拟文件系统路径读取并解析纹理数据
             */
            StatusOr<DecodedTextureData> decodeFromFile(const std::string& filePath);

            /**
             * @brief 获取底层 FCT_RHI ImageLoader 实例指针
             */
            FCT::ImageLoader* getFctImageLoader() const;

            static std::string normalizeExtension(std::string ext);
            static std::string extractExtension(const std::string& filePath);

        private:
            struct DecoderEntry
            {
                int priority = 0;
                UniquePtr<ITextureFormatDecoder> decoder;
            };

            std::vector<DecoderEntry> m_decoders;
            FctRhiTextureDecoder* m_fctDecoderRef = nullptr;
            bool m_initialized = false;
        };

    } // namespace Resource
} // namespace MQEngine

#endif // MQENGINE_RESOURCE_CORE_TEXTUREDECODERREGISTRY_H
