/**
 * @file AsyncTextureUploader.h
 * @brief 基于 WorkHost 异步调度、占位贴图热替换与每帧 1.5MB 限流上传的纹理管线 (PRD 03, FEAT-17, FEAT-19)
 */
#ifndef MQENGINE_ASSET_LOADER_ASYNCTEXTUREUPLOADER_H
#define MQENGINE_ASSET_LOADER_ASYNCTEXTUREUPLOADER_H

#include <MQEngine/EnginePCH.h>
#include <MQEngine/asset/classdef.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <MQEngine/thirdparty/thirdparty.h>

#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MQEngine
{
    namespace Asset
    {
        using DecodedImageBuffer = Resource::DecodedTextureData;

        /**
         * @brief 占位纹理逻辑句柄运行态记录
         */
        struct PlaceholderTextureEntry
        {
            uint32_t handle{0};
            uint32_t effectiveSlot{2};
            FCT::Vec4 uvScaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
            bool ready{false};
            bool failed{false};
            std::string path;
        };

        /**
         * @brief 异步纹理上传管线管理器：
         *        支持 1x1 占位纹理秒开与原子热替换 (Slot 2)，以及 <= 1.5 MB/frame 显存带宽分帧限流。
         */
        class ENGINE_API AsyncTextureUploader : public CoreEngineManager<AsyncTextureUploader>
        {
        public:
            using UploadCallback = std::function<void(StatusOr<TextureUploadResult>)>;
            using FormatDecoderFn = Resource::TextureDecoderRegistry::FormatDecoderFn;

            static constexpr uint32_t PlaceholderTextureSlot = 2u;
            static constexpr uint64_t DefaultMaxUploadBudgetPerFrame = 1572864ULL; // 1.5 MB/frame

            AsyncTextureUploader();
            ~AsyncTextureUploader();

            AsyncTextureUploader(const AsyncTextureUploader&) = delete;
            AsyncTextureUploader& operator=(const AsyncTextureUploader&) = delete;

            void registerInfo();
            Status init();
            void term() noexcept;

            void registerFormatDecoder(const std::string& extension, FormatDecoderFn decoder);
            bool isExtensionSupported(const std::string& extension) const;
            std::vector<std::string> getSupportedExtensions() const;

            Status uploadTextureAsync(TextureUploadDesc desc, UploadCallback onComplete);

            /**
             * @brief 纹理占位秒开与热替换机制：立即返回逻辑纹理句柄 (>=10)，未就绪期间解析到 Slot 2 占位贴图
             */
            uint32_t requestTextureWithPlaceholder(const std::string& path, UploadCallback onComplete = nullptr);

            [[nodiscard]] uint32_t getEffectiveTextureSlot(uint32_t handle) const;
            [[nodiscard]] bool isTextureReady(uint32_t handle) const;
            [[nodiscard]] FCT::Vec4 getTextureUvScaleOffset(uint32_t handle) const;

            void onDownloadComplete(
                uint32_t handle,
                uint32_t realAtlasSlot,
                const FCT::Vec4& uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f});
            void onDownloadFailed(uint32_t handle);

            /**
             * @brief 直接向 GPU 分帧限流队列入队指定字节数的待上传贴图任务
             */
            void enqueueStagedUpload(
                uint64_t byteSize,
                std::string debugName = {},
                UploadCallback onComplete = nullptr);

            void setUploadBudgetPerFrame(int64_t budgetBytes) noexcept;
            [[nodiscard]] uint64_t getUploadBudgetPerFrame() const noexcept { return m_maxUploadBudgetPerFrame; }
            [[nodiscard]] uint64_t getUploadedBytesThisFrame() const noexcept
            {
                return m_uploadedBytesThisFrame.load(std::memory_order_relaxed);
            }
            [[nodiscard]] size_t getQueuedGpuUploadCount() const;

            /**
             * @brief 显存上传带宽分帧限流驱动入口（默认预算 <= 1.5 MB/frame）
             */
            void tick(float dt = 0.016667f);

            void cancelAllPending();

            size_t getPendingCount() const;
            uint64_t getCompletedCount() const noexcept { return m_completedCount.load(std::memory_order_relaxed); }
            uint64_t getTotalUploadedBytes() const noexcept { return m_totalUploadedBytes.load(std::memory_order_relaxed); }

        private:
            struct PendingUpload
            {
                TextureUploadDesc desc;
                UploadCallback callback;
            };

            struct GpuUploadTask
            {
                TextureUploadResult decodedInfo;
                std::vector<uint8_t> pixelData;
                UploadCallback callback;
            };

            void processNextUpload();
            void performCpuDecode(TextureUploadDesc desc, std::vector<uint8_t> rawFileBytes, UploadCallback callback);
            void performGpuUpload(TextureUploadResult decodedInfo, std::vector<uint8_t> pixelData, UploadCallback callback);
            void performGpuUploadImmediate(TextureUploadResult decodedInfo, std::vector<uint8_t> pixelData, UploadCallback callback);

        private:
            boost::lockfree::queue<PendingUpload*> m_pendingQueue{1024};
            std::atomic<size_t> m_pendingCount{0};

            mutable std::mutex m_uploadQueueMutex;
            std::vector<GpuUploadTask> m_uploadQueue;
            uint64_t m_maxUploadBudgetPerFrame{DefaultMaxUploadBudgetPerFrame};
            std::atomic<uint64_t> m_uploadedBytesThisFrame{0};

            mutable std::mutex m_placeholderMutex;
            std::unordered_map<uint32_t, PlaceholderTextureEntry> m_placeholders;
            std::atomic<uint32_t> m_nextPlaceholderHandle{10};

            std::atomic<uint64_t> m_completedCount{0};
            std::atomic<uint64_t> m_totalUploadedBytes{0};
            std::atomic<uint32_t> m_activeUploadTasks{0};
            std::atomic<uintptr_t> m_nextHandleSeed{1000};

            bool m_initialized = false;
        };

    } // namespace Asset
} // namespace MQEngine

#endif // MQENGINE_ASSET_LOADER_ASYNCTEXTUREUPLOADER_H
