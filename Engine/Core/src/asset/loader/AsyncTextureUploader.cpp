/**
 * @file AsyncTextureUploader.cpp
 * @brief 异步纹理上传管线、占位热替换与 <=1.5MB/frame 分帧限流实现
 */
#include "AsyncTextureUploader.h"

#include <MQEngine/host/core/Host.h>
#include <MQEngine/host/render/RenderHost.h>
#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/TextureDecoderRegistry.h>
#include <MQEngine/resource/table/GpuResourceTable.h>

#include <algorithm>
#include <cstring>

namespace MQEngine
{
    namespace Asset
    {
        AsyncTextureUploader::AsyncTextureUploader() = default;

        AsyncTextureUploader::~AsyncTextureUploader()
        {
            term();
        }

        void AsyncTextureUploader::registerInfo()
        {
        }

        Status AsyncTextureUploader::init()
        {
            if (auto* decoderReg = Resource::TextureDecoderRegistry::InstancePtr())
            {
                CHECK_STATUS(decoderReg->init());
            }
            m_initialized = true;
            return OkStatus();
        }

        void AsyncTextureUploader::term() noexcept
        {
            cancelAllPending();
            {
                std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
                m_uploadQueue.clear();
            }
            m_initialized = false;
        }

        void AsyncTextureUploader::registerFormatDecoder(const std::string& extension, FormatDecoderFn decoder)
        {
            if (auto* decoderReg = Resource::TextureDecoderRegistry::InstancePtr())
            {
                decoderReg->registerFormatDecoder(extension, std::move(decoder));
            }
        }

        bool AsyncTextureUploader::isExtensionSupported(const std::string& extension) const
        {
            if (const auto* decoderReg = Resource::TextureDecoderRegistry::InstancePtr())
            {
                return decoderReg->isExtensionSupported(extension);
            }
            return false;
        }

        std::vector<std::string> AsyncTextureUploader::getSupportedExtensions() const
        {
            if (const auto* decoderReg = Resource::TextureDecoderRegistry::InstancePtr())
            {
                return decoderReg->getSupportedExtensions();
            }
            return {};
        }

        Status AsyncTextureUploader::uploadTextureAsync(TextureUploadDesc desc, UploadCallback onComplete)
        {
            if (!m_initialized)
            {
                return FailedPreconditionError("AsyncTextureUploader 尚未初始化");
            }

            if (desc.filePath.empty() && desc.memoryData.empty())
            {
                return InvalidArgumentError("纹理上传描述必须提供文件路径或原始内存数据");
            }

            auto* item = FCT_NEW(PendingUpload);
            item->desc = std::move(desc);
            item->callback = std::move(onComplete);
            m_pendingQueue.push(item);
            m_pendingCount.fetch_add(1, std::memory_order_relaxed);

            processNextUpload();
            return OkStatus();
        }

        void AsyncTextureUploader::cancelAllPending()
        {
            PendingUpload* item = nullptr;
            while (m_pendingQueue.pop(item))
            {
                m_pendingCount.fetch_sub(1, std::memory_order_relaxed);
                if (item)
                {
                    if (item->callback)
                    {
                        item->callback(CancelledError("纹理上传任务已取消"));
                    }
                    FCT_DELETE(item);
                }
            }
        }

        size_t AsyncTextureUploader::getPendingCount() const
        {
            return m_pendingCount.load(std::memory_order_relaxed);
        }

        void AsyncTextureUploader::processNextUpload()
        {
            PendingUpload* itemPtr = nullptr;
            if (!m_pendingQueue.pop(itemPtr))
            {
                return;
            }
            m_pendingCount.fetch_sub(1, std::memory_order_relaxed);

            if (!itemPtr)
            {
                return;
            }

            PendingUpload nextItem = std::move(*itemPtr);
            FCT_DELETE(itemPtr);

            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                if (nextItem.callback)
                {
                    nextItem.callback(FailedPreconditionError("WorkHost 未运行，无法调度异步纹理上传"));
                }
                return;
            }

            m_activeUploadTasks.fetch_add(1, std::memory_order_relaxed);

            if (!nextItem.desc.memoryData.empty())
            {
                std::vector<uint8_t> rawCopy = nextItem.desc.memoryData;
                performCpuDecode(std::move(nextItem.desc), std::move(rawCopy), std::move(nextItem.callback));
                return;
            }

            (void)workHost->postIoTask([this, desc = std::move(nextItem.desc), cb = std::move(nextItem.callback)]() mutable {
                std::vector<uint8_t> fileBytes;
                const std::string& path = desc.filePath;

                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (auto* dataLoader = res->getDataLoader(); dataLoader && dataLoader->fileExists(path))
                    {
                        auto bytesOr = dataLoader->readBinaryFile(path);
                        if (bytesOr.ok())
                        {
                            fileBytes = std::move(*bytesOr);
                        }
                    }

                    if (fileBytes.empty())
                    {
                        if (auto* resLoader = res->getResourceLoader())
                        {
                            auto contentOr = resLoader->loadFile(path);
                            if (contentOr.ok())
                            {
                                fileBytes.assign(contentOr->begin(), contentOr->end());
                            }
                        }
                    }
                }

                if (fileBytes.empty() && FCT::SAI::FileSystem::exists(path))
                {
                    if (auto saiBytes = FCT::SAI::FileSystem::readBinaryFile(path); saiBytes.ok())
                    {
                        fileBytes = std::move(*saiBytes);
                    }
                }

                if (fileBytes.empty())
                {
                    m_activeUploadTasks.fetch_sub(1, std::memory_order_relaxed);
                    if (cb)
                    {
                        cb(NotFoundError(StrCat("无法读取纹理文件: ", path)));
                    }
                    processNextUpload();
                    return;
                }

                performCpuDecode(std::move(desc), std::move(fileBytes), std::move(cb));
            });
        }

        void AsyncTextureUploader::performCpuDecode(
            TextureUploadDesc desc,
            std::vector<uint8_t> rawFileBytes,
            UploadCallback callback)
        {
            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                m_activeUploadTasks.fetch_sub(1, std::memory_order_relaxed);
                if (callback)
                {
                    callback(FailedPreconditionError("WorkHost 不可用"));
                }
                return;
            }

            workHost->postCpuTask([this, desc = std::move(desc), raw = std::move(rawFileBytes), cb = std::move(callback)]() mutable {
                auto* decoderReg = Resource::TextureDecoderRegistry::InstancePtr();
                if (!decoderReg)
                {
                    m_activeUploadTasks.fetch_sub(1, std::memory_order_relaxed);
                    if (cb)
                    {
                        cb(FailedPreconditionError("Resource::TextureDecoderRegistry 未就绪"));
                    }
                    processNextUpload();
                    return;
                }

                auto decodedOr = decoderReg->decodeFromMemory(raw.data(), raw.size(), desc.filePath);
                if (!decodedOr.ok() && !desc.filePath.empty())
                {
                    decodedOr = decoderReg->decodeFromFile(desc.filePath);
                }

                if (!decodedOr.ok())
                {
                    m_activeUploadTasks.fetch_sub(1, std::memory_order_relaxed);
                    if (cb)
                    {
                        cb(decodedOr.status());
                    }
                    processNextUpload();
                    return;
                }

                Resource::DecodedTextureData decoded = std::move(*decodedOr);

                TextureUploadResult result;
                result.debugName = desc.debugName.empty() ? desc.filePath : desc.debugName;
                result.width = decoded.width;
                result.height = decoded.height;
                result.channels = decoded.channels;
                result.mipLevels = desc.generateMips ? std::max(decoded.mipLevels, 4u) : std::max(decoded.mipLevels, 1u);
                result.totalBytes = decoded.pixels.size();

                performGpuUpload(std::move(result), std::move(decoded.pixels), std::move(cb));
            });
        }

        uint32_t AsyncTextureUploader::requestTextureWithPlaceholder(const std::string& path, UploadCallback onComplete)
        {
            if (path.empty())
            {
                return 0u;
            }

            const uint32_t handle = m_nextPlaceholderHandle.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(m_placeholderMutex);
                PlaceholderTextureEntry entry;
                entry.handle = handle;
                entry.effectiveSlot = PlaceholderTextureSlot;
                entry.uvScaleOffset = FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
                entry.ready = false;
                entry.failed = false;
                entry.path = path;
                m_placeholders[handle] = std::move(entry);
            }

            if (m_initialized)
            {
                TextureUploadDesc desc;
                desc.filePath = path;
                desc.debugName = path;

                (void)uploadTextureAsync(std::move(desc), [this, handle, cb = std::move(onComplete)](StatusOr<TextureUploadResult> res) {
                    if (res.ok())
                    {
                        onDownloadComplete(handle, handle, FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f});
                    }
                    else
                    {
                        onDownloadFailed(handle);
                    }
                    if (cb)
                    {
                        cb(std::move(res));
                    }
                });
            }

            return handle;
        }

        uint32_t AsyncTextureUploader::getEffectiveTextureSlot(uint32_t handle) const
        {
            std::lock_guard<std::mutex> lock(m_placeholderMutex);
            auto it = m_placeholders.find(handle);
            if (it == m_placeholders.end())
            {
                return 0u;
            }
            return it->second.effectiveSlot;
        }

        bool AsyncTextureUploader::isTextureReady(uint32_t handle) const
        {
            std::lock_guard<std::mutex> lock(m_placeholderMutex);
            auto it = m_placeholders.find(handle);
            if (it == m_placeholders.end())
            {
                return false;
            }
            return it->second.ready;
        }

        FCT::Vec4 AsyncTextureUploader::getTextureUvScaleOffset(uint32_t handle) const
        {
            std::lock_guard<std::mutex> lock(m_placeholderMutex);
            auto it = m_placeholders.find(handle);
            if (it == m_placeholders.end())
            {
                return FCT::Vec4{1.0f, 1.0f, 0.0f, 0.0f};
            }
            return it->second.uvScaleOffset;
        }

        void AsyncTextureUploader::onDownloadComplete(
            uint32_t handle,
            uint32_t realAtlasSlot,
            const FCT::Vec4& uvScaleOffset)
        {
            std::lock_guard<std::mutex> lock(m_placeholderMutex);
            auto it = m_placeholders.find(handle);
            if (it != m_placeholders.end())
            {
                it->second.effectiveSlot = realAtlasSlot;
                it->second.uvScaleOffset = uvScaleOffset;
                it->second.ready = true;
                it->second.failed = false;
            }
        }

        void AsyncTextureUploader::onDownloadFailed(uint32_t handle)
        {
            std::lock_guard<std::mutex> lock(m_placeholderMutex);
            auto it = m_placeholders.find(handle);
            if (it != m_placeholders.end())
            {
                it->second.effectiveSlot = PlaceholderTextureSlot;
                it->second.ready = false;
                it->second.failed = true;
            }
        }

        void AsyncTextureUploader::enqueueStagedUpload(
            uint64_t byteSize,
            std::string debugName,
            UploadCallback onComplete)
        {
            TextureUploadResult info{};
            info.debugName = std::move(debugName);
            info.width = 1;
            info.height = 1;
            info.channels = 4;
            info.mipLevels = 1;
            info.totalBytes = byteSize;

            std::vector<uint8_t> payload(static_cast<size_t>(byteSize), 0xFFu);
            m_activeUploadTasks.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
            m_uploadQueue.push_back(GpuUploadTask{std::move(info), std::move(payload), std::move(onComplete)});
        }

        void AsyncTextureUploader::setUploadBudgetPerFrame(int64_t budgetBytes) noexcept
        {
            m_maxUploadBudgetPerFrame = (budgetBytes > 0) ? static_cast<uint64_t>(budgetBytes) : 0ULL;
        }

        size_t AsyncTextureUploader::getQueuedGpuUploadCount() const
        {
            std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
            return m_uploadQueue.size();
        }

        void AsyncTextureUploader::tick(float dt)
        {
            (void)dt;
            std::vector<GpuUploadTask> tasksToRun;
            uint64_t frameUploadedBytes = 0;

            {
                std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
                auto it = m_uploadQueue.begin();
                while (it != m_uploadQueue.end())
                {
                    const uint64_t taskBytes = it->decodedInfo.totalBytes > 0
                        ? it->decodedInfo.totalBytes
                        : static_cast<uint64_t>(it->pixelData.size());

                    if (taskBytes == 0)
                    {
                        tasksToRun.push_back(std::move(*it));
                        it = m_uploadQueue.erase(it);
                        continue;
                    }

                    if (m_maxUploadBudgetPerFrame == 0)
                    {
                        break;
                    }

                    // 单张超大贴图 (>1.5MB) 在当前帧尚无已上传流量时允许单独放行，防止永久饥饿
                    if (frameUploadedBytes > 0 && (frameUploadedBytes + taskBytes > m_maxUploadBudgetPerFrame))
                    {
                        break;
                    }

                    frameUploadedBytes += taskBytes;
                    tasksToRun.push_back(std::move(*it));
                    it = m_uploadQueue.erase(it);

                    if (frameUploadedBytes >= m_maxUploadBudgetPerFrame)
                    {
                        break;
                    }
                }
            }

            m_uploadedBytesThisFrame.store(frameUploadedBytes, std::memory_order_relaxed);

            for (auto& task : tasksToRun)
            {
                performGpuUploadImmediate(std::move(task.decodedInfo), std::move(task.pixelData), std::move(task.callback));
            }
        }

        void AsyncTextureUploader::performGpuUpload(
            TextureUploadResult decodedInfo,
            std::vector<uint8_t> pixelData,
            UploadCallback callback)
        {
            {
                std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
                m_uploadQueue.push_back(GpuUploadTask{std::move(decodedInfo), std::move(pixelData), std::move(callback)});
            }
            tick(0.016667f);
        }

        void AsyncTextureUploader::performGpuUploadImmediate(
            TextureUploadResult decodedInfo,
            std::vector<uint8_t> pixelData,
            UploadCallback callback)
        {
            void* gpuHandle = nullptr;
            if (auto* renderHost = Host::RenderHost::InstancePtr(); renderHost && renderHost->getContext())
            {
                if (FCT::Context* ctx = renderHost->getContext(); !pixelData.empty() && decodedInfo.width > 0 && decodedInfo.height > 0)
                {
                    auto* gpuImage = FCT_NEW(FCT::SingleBufferImage, ctx);
                    gpuImage->width(decodedInfo.width);
                    gpuImage->height(decodedInfo.height);
                    gpuImage->format(FCT::Format::R8G8B8A8_UNORM);
                    gpuImage->as(FCT::ImageUsage::Texture);
                    gpuImage->initData(pixelData.data(), static_cast<int>(pixelData.size()));
                    gpuImage->create();
                    gpuHandle = static_cast<void*>(gpuImage);
                }
            }

            if (!gpuHandle)
            {
                const uintptr_t handleAddr = m_nextHandleSeed.fetch_add(1, std::memory_order_relaxed);
                gpuHandle = reinterpret_cast<void*>(handleAddr);
            }

            decodedInfo.textureHandle = gpuHandle;

            if (auto* res = Resource::Resource::InstancePtr(); res && res->getTableRegistry())
            {
                res->getTableRegistry()->gpuResourceTable().registerResource(
                    gpuHandle,
                    Resource::GpuResourceType::Texture,
                    decodedInfo.totalBytes,
                    decodedInfo.debugName,
                    Resource::GpuResourceState::Ready);
            }

            m_totalUploadedBytes.fetch_add(decodedInfo.totalBytes, std::memory_order_relaxed);
            m_completedCount.fetch_add(1, std::memory_order_relaxed);
            if (m_activeUploadTasks.load(std::memory_order_relaxed) > 0)
            {
                m_activeUploadTasks.fetch_sub(1, std::memory_order_relaxed);
            }

            if (callback)
            {
                callback(std::move(decodedInfo));
            }

            processNextUpload();
        }

    } // namespace Asset
} // namespace MQEngine
