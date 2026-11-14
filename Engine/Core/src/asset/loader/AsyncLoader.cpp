/**
 * @file AsyncLoader.cpp
 * @brief 基于 WorkHost 异步 I/O 线程组的资产流式加载器、.mqmesh 直通与缓存配额实现
 */
#include "AsyncLoader.h"

#include <MQEngine/host/work/WorkHost.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>

#include <algorithm>
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine
{
    namespace Asset
    {
        AsyncLoader::AsyncLoader() = default;

        AsyncLoader::~AsyncLoader()
        {
            term();
        }

        void AsyncLoader::registerInfo()
        {
        }

        Status AsyncLoader::init()
        {
            m_initialized = true;
            return OkStatus();
        }

        void AsyncLoader::term() noexcept
        {
            m_initialized = false;
        }

        StatusOr<std::vector<uint8_t>> AsyncLoader::readLocalBinaryViaEngine(const std::string& path)
        {
            if (auto* resModule = Resource::Resource::InstancePtr())
            {
                if (auto* dataLoader = resModule->getDataLoader(); dataLoader && dataLoader->fileExists(path))
                {
                    auto bytesOr = dataLoader->readBinaryFile(path);
                    if (bytesOr.ok())
                    {
                        return bytesOr;
                    }
                }
                if (auto* resLoader = resModule->getResourceLoader())
                {
                    auto strOr = resLoader->loadFile(path);
                    if (strOr.ok())
                    {
                        return std::vector<uint8_t>(strOr->begin(), strOr->end());
                    }
                }
            }

            if (FCT::SAI::FileSystem::exists(path))
            {
                return FCT::SAI::FileSystem::readBinaryFile(path);
            }

            return NotFoundError(StrCat("无法打开二进制文件: ", path));
        }

        Status AsyncLoader::submitAsyncTextRead(const std::string& path, TextReadCallback onComplete)
        {
            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                return FailedPreconditionError("WorkHost 实例不可用或未处于运行状态");
            }

            m_activeIoCount.fetch_add(1, std::memory_order_relaxed);

            return workHost->postIoTask([this, path, cb = std::move(onComplete)]() mutable {
                StatusOr<std::string> contentResult = NotFoundError(StrCat("无法打开文件: ", path));

                if (auto* resModule = Resource::Resource::InstancePtr(); resModule && resModule->getResourceLoader())
                {
                    contentResult = resModule->getResourceLoader()->loadFile(path);
                }
                else if (FCT::SAI::FileSystem::exists(path))
                {
                    auto bytesOr = FCT::SAI::FileSystem::readBinaryFile(path);
                    if (bytesOr.ok())
                    {
                        contentResult = std::string(reinterpret_cast<const char*>(bytesOr->data()), bytesOr->size());
                    }
                    else
                    {
                        contentResult = bytesOr.status();
                    }
                }

                if (contentResult.ok())
                {
                    m_totalBytesRead.fetch_add(contentResult.value().size(), std::memory_order_relaxed);
                }

                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);

                if (cb)
                {
                    cb(std::move(contentResult));
                }
            });
        }

        Status AsyncLoader::submitAsyncBinaryRead(const std::string& path, BinaryReadCallback onComplete)
        {
            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                return FailedPreconditionError("WorkHost 实例不可用或未处于运行状态");
            }

            m_activeIoCount.fetch_add(1, std::memory_order_relaxed);

            return workHost->postIoTask([this, path, cb = std::move(onComplete)]() mutable {
                std::string normalizedQuery = path;
                std::replace(normalizedQuery.begin(), normalizedQuery.end(), '\\', '/');
                auto resolveRes = AssetManifest::getInstance().resolveAsset(normalizedQuery);
                if (resolveRes.ok())
                {
                    const auto& res = resolveRes.value();
                    if (res.location == AssetResolveResult::Location::RemoteCdn)
                    {
                        bool isPrimaryDownloader = false;
                        {
                            std::lock_guard<std::mutex> lock(m_inflightMutex);
                            auto& waiters = m_inflightDownloads[res.downloadUrl];
                            isPrimaryDownloader = waiters.empty();
                            if (cb)
                            {
                                waiters.push_back(cb);
                            }
                        }

                        if (!isPrimaryDownloader)
                        {
                            m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                            return;
                        }

                        FCT::SAI::FileSystem::downloadFileToPathAsync(
                            res.downloadUrl,
                            res.cacheDestPath,
                            [this, res](Status downloadStatus) {
                                auto dispatchToWaiters = [this, url = res.downloadUrl](const StatusOr<std::vector<uint8_t>>& outcome) {
                                    std::vector<BinaryReadCallback> callbacks;
                                    {
                                        std::lock_guard<std::mutex> lock(m_inflightMutex);
                                        auto it = m_inflightDownloads.find(url);
                                        if (it != m_inflightDownloads.end())
                                        {
                                            callbacks = std::move(it->second);
                                            m_inflightDownloads.erase(it);
                                        }
                                    }
                                    for (auto& waiter : callbacks)
                                    {
                                        if (waiter)
                                        {
                                            waiter(outcome);
                                        }
                                    }
                                };

                                if (!downloadStatus.ok())
                                {
                                    m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                                    dispatchToWaiters(downloadStatus);
                                    return;
                                }

                                FCT::SAI::FileSystem::readBinaryFileAsync(
                                    res.cacheDestPath,
                                    [this, res, dispatchToWaiters](StatusOr<std::vector<uint8_t>> dataOr) {
                                        m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                                        if (dataOr.ok())
                                        {
                                            m_totalBytesRead.fetch_add(dataOr.value().size(), std::memory_order_relaxed);
                                            recordCacheAccess(res.cacheDestPath, dataOr.value().size());
                                            enforceCacheQuota();
                                        }
                                        dispatchToWaiters(dataOr);
                                    });
                            });
                        return;
                    }
                    else if (res.location == AssetResolveResult::Location::LocalCache ||
                             res.location == AssetResolveResult::Location::FirstPackagePkg)
                    {
                        FCT::SAI::FileSystem::readBinaryFileAsync(
                            res.resolvedPath,
                            [this, res, cb](StatusOr<std::vector<uint8_t>> dataOr) {
                                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                                if (dataOr.ok())
                                {
                                    m_totalBytesRead.fetch_add(dataOr.value().size(), std::memory_order_relaxed);
                                    if (res.location == AssetResolveResult::Location::LocalCache)
                                    {
                                        recordCacheAccess(res.resolvedPath, dataOr.value().size());
                                    }
                                }
                                if (cb)
                                {
                                    cb(std::move(dataOr));
                                }
                            });
                        return;
                    }
                }
                else if (resolveRes.status().code() == absl::StatusCode::kInvalidArgument)
                {
                    m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                    if (cb)
                    {
                        cb(resolveRes.status());
                    }
                    return;
                }

                auto binaryResult = readLocalBinaryViaEngine(path);
                if (binaryResult.ok())
                {
                    m_totalBytesRead.fetch_add(binaryResult->size(), std::memory_order_relaxed);
                }

                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);

                if (cb)
                {
                    cb(std::move(binaryResult));
                }
            });
        }

        Status AsyncLoader::submitAsyncFileWrite(const std::string& path, std::string content, FileWriteCallback onComplete)
        {
            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                return FailedPreconditionError("WorkHost 实例不可用或未处于运行状态");
            }

            m_activeIoCount.fetch_add(1, std::memory_order_relaxed);

            return workHost->postIoTask([this, path, payload = std::move(content), cb = std::move(onComplete)]() mutable {
                Status saveStatus = OkStatus();

                if (auto* resModule = Resource::Resource::InstancePtr(); resModule && resModule->getResourceLoader())
                {
                    saveStatus = resModule->getResourceLoader()->saveFile(path, payload);
                }
                else
                {
                    saveStatus = FCT::SAI::FileSystem::writeBinaryFile(path, payload.data(), payload.size());
                }

                if (saveStatus.ok())
                {
                    m_totalBytesWritten.fetch_add(payload.size(), std::memory_order_relaxed);
                }

                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);

                if (cb)
                {
                    cb(saveStatus);
                }
            });
        }

        uint32_t AsyncLoader::getInflightDownloadCount() const
        {
            std::lock_guard<std::mutex> lock(m_inflightMutex);
            return static_cast<uint32_t>(m_inflightDownloads.size());
        }

        Status AsyncLoader::submitAsyncNetworkFetch(const std::string& url, BinaryReadCallback onComplete)
        {
            if (url.empty())
            {
                return InvalidArgumentError("网络拉取 URL 不能为空");
            }

            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                return FailedPreconditionError("WorkHost 实例不可用或未处于运行状态");
            }

            {
                std::lock_guard<std::mutex> lock(m_inflightMutex);
                auto& waiters = m_inflightDownloads[url];
                const bool alreadyInflight = !waiters.empty();
                if (onComplete)
                {
                    waiters.push_back(std::move(onComplete));
                }
                if (alreadyInflight)
                {
                    return OkStatus();
                }
            }

            m_activeIoCount.fetch_add(1, std::memory_order_relaxed);

            return workHost->postIoTask([this, url]() mutable {
                StatusOr<std::vector<uint8_t>> result;

                if (url.starts_with("file://"))
                {
                    const std::string localPath = url.substr(7);
                    result = readLocalBinaryViaEngine(localPath);
                    if (result.ok())
                    {
                        m_totalBytesRead.fetch_add(result->size(), std::memory_order_relaxed);
                    }
                }
                else if (url.starts_with("http://") || url.starts_with("https://"))
                {
#ifdef _WIN32
                    HMODULE hWinInet = LoadLibraryA("wininet.dll");
                    if (!hWinInet)
                    {
                        result = UnavailableError(StrCat("无法加载 wininet.dll 进行网络拉取: ", url));
                    }
                    else
                    {
                        using InternetOpenAFn = void*(WINAPI*)(const char*, uint32_t, const char*, const char*, uint32_t);
                        using InternetOpenUrlAFn = void*(WINAPI*)(void*, const char*, const char*, uint32_t, uint32_t, uintptr_t);
                        using InternetReadFileFn = int(WINAPI*)(void*, void*, uint32_t, uint32_t*);
                        using InternetCloseHandleFn = int(WINAPI*)(void*);

                        auto pfnOpen = reinterpret_cast<InternetOpenAFn>(GetProcAddress(hWinInet, "InternetOpenA"));
                        auto pfnOpenUrl = reinterpret_cast<InternetOpenUrlAFn>(GetProcAddress(hWinInet, "InternetOpenUrlA"));
                        auto pfnRead = reinterpret_cast<InternetReadFileFn>(GetProcAddress(hWinInet, "InternetReadFile"));
                        auto pfnClose = reinterpret_cast<InternetCloseHandleFn>(GetProcAddress(hWinInet, "InternetCloseHandle"));

                        if (!pfnOpen || !pfnOpenUrl || !pfnRead || !pfnClose)
                        {
                            FreeLibrary(hWinInet);
                            result = UnavailableError(StrCat("WinINet API 符号导出缺失: ", url));
                        }
                        else
                        {
                            void* hSession = pfnOpen("MQEngine/1.0", 1u, nullptr, nullptr, 0);
                            if (!hSession)
                            {
                                FreeLibrary(hWinInet);
                                result = InternalError(StrCat("创建网络会话失败: ", url));
                            }
                            else
                            {
                                constexpr uint32_t flags = 0x80000000u | 0x04000000u;
                                void* hUrl = pfnOpenUrl(hSession, url.c_str(), nullptr, 0, flags, 0);
                                if (!hUrl)
                                {
                                    pfnClose(hSession);
                                    FreeLibrary(hWinInet);
                                    result = NotFoundError(StrCat("无法连接或拉取远端 URL: ", url));
                                }
                                else
                                {
                                    std::vector<uint8_t> downloadBuffer;
                                    uint8_t chunk[8192];
                                    uint32_t bytesRead = 0;
                                    while (pfnRead(hUrl, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0)
                                    {
                                        downloadBuffer.insert(downloadBuffer.end(), chunk, chunk + bytesRead);
                                    }
                                    pfnClose(hUrl);
                                    pfnClose(hSession);
                                    FreeLibrary(hWinInet);

                                    m_totalBytesRead.fetch_add(downloadBuffer.size(), std::memory_order_relaxed);
                                    result = std::move(downloadBuffer);
                                }
                            }
                        }
                    }
#else
                    result = UnavailableError(StrCat("当前平台请通过 FCT::SAI::FileSystem 异步拉取: ", url));
#endif
                }
                else
                {
                    result = readLocalBinaryViaEngine(url);
                    if (result.ok())
                    {
                        m_totalBytesRead.fetch_add(result->size(), std::memory_order_relaxed);
                    }
                }

                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);

                std::vector<BinaryReadCallback> callbacks;
                {
                    std::lock_guard<std::mutex> lock(m_inflightMutex);
                    auto it = m_inflightDownloads.find(url);
                    if (it != m_inflightDownloads.end())
                    {
                        callbacks = std::move(it->second);
                        m_inflightDownloads.erase(it);
                    }
                }

                for (auto& waiter : callbacks)
                {
                    if (waiter)
                    {
                        waiter(result);
                    }
                }
            });
        }

        Status AsyncLoader::submitAsyncChunkedStream(
            const std::string& path,
            uint64_t chunkSize,
            ChunkCallback onChunk,
            StreamFinishedCallback onFinished)
        {
            if (chunkSize == 0)
            {
                chunkSize = 64 * 1024;
            }

            auto* workHost = Host::WorkHost::InstancePtr();
            if (!workHost || !workHost->isRunning())
            {
                return FailedPreconditionError("WorkHost 实例不可用或未处于运行状态");
            }

            m_activeIoCount.fetch_add(1, std::memory_order_relaxed);

            return workHost->postIoTask([this, path, chunkSize, chunkCb = std::move(onChunk), finCb = std::move(onFinished)]() mutable {
                auto fullBytesOr = readLocalBinaryViaEngine(path);
                if (!fullBytesOr.ok())
                {
                    m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);
                    if (finCb)
                    {
                        finCb(fullBytesOr.status());
                    }
                    return;
                }

                const auto& fullBytes = *fullBytesOr;
                const uint64_t totalSize = fullBytes.size();
                uint64_t currentOffset = 0;
                bool userCancelled = false;

                while (currentOffset < totalSize && !userCancelled)
                {
                    const uint64_t toRead = std::min(chunkSize, totalSize - currentOffset);
                    StreamChunk chunk;
                    chunk.offset = currentOffset;
                    chunk.data.assign(
                        fullBytes.begin() + static_cast<std::ptrdiff_t>(currentOffset),
                        fullBytes.begin() + static_cast<std::ptrdiff_t>(currentOffset + toRead));
                    chunk.isLastChunk = (currentOffset + toRead >= totalSize);

                    m_totalBytesRead.fetch_add(toRead, std::memory_order_relaxed);
                    currentOffset += toRead;

                    if (chunkCb && !chunkCb(std::move(chunk)))
                    {
                        userCancelled = true;
                        break;
                    }
                }

                m_activeIoCount.fetch_sub(1, std::memory_order_relaxed);

                if (finCb)
                {
                    finCb(userCancelled ? CancelledError("用户提前终止分块流式传输") : OkStatus());
                }
            });
        }

        Status AsyncLoader::validateSubmeshRanges(
            const MqMeshHeader& header,
            std::span<const SubMeshRange> subMeshes)
        {
            for (const auto& sm : subMeshes)
            {
                const uint64_t endIdx = static_cast<uint64_t>(sm.indexOffset) + static_cast<uint64_t>(sm.indexCount);
                if (endIdx > static_cast<uint64_t>(header.indexCount))
                {
                    return OutOfRangeError("SubMesh 索引区间越过 MqMeshHeader::indexCount 边界");
                }
                if (sm.indexCount > 0 && sm.baseVertex >= header.vertexCount && header.vertexCount > 0)
                {
                    return OutOfRangeError("SubMesh baseVertex 超出 MqMeshHeader::vertexCount 范围");
                }
            }
            return OkStatus();
        }

        StatusOr<std::vector<uint8_t>> AsyncLoader::serializeMqMesh(
            const MqMeshHeader& header,
            std::span<const uint8_t> vertexBytes,
            std::span<const uint8_t> indexBytes,
            std::span<const SubMeshRange> subMeshes)
        {
            if (header.magic != 0x4D514D53u || header.version != 1u)
            {
                return InvalidArgumentError("serializeMqMesh: 非法的 MqMeshHeader 魔数或版本号");
            }

            CHECK_STATUS(validateSubmeshRanges(header, subMeshes));

            MqMeshHeader outHdr = header;
            if (!subMeshes.empty())
            {
                outHdr.subMeshCount = static_cast<uint32_t>(subMeshes.size());
            }

            const size_t subMeshBytes = subMeshes.size_bytes();
            std::vector<uint8_t> outBuffer;
            outBuffer.resize(sizeof(MqMeshHeader) + vertexBytes.size() + indexBytes.size() + subMeshBytes);

            size_t offset = 0;
            std::memcpy(outBuffer.data() + offset, &outHdr, sizeof(MqMeshHeader));
            offset += sizeof(MqMeshHeader);

            if (!vertexBytes.empty())
            {
                std::memcpy(outBuffer.data() + offset, vertexBytes.data(), vertexBytes.size());
                offset += vertexBytes.size();
            }
            if (!indexBytes.empty())
            {
                std::memcpy(outBuffer.data() + offset, indexBytes.data(), indexBytes.size());
                offset += indexBytes.size();
            }
            if (subMeshBytes > 0)
            {
                std::memcpy(outBuffer.data() + offset, subMeshes.data(), subMeshBytes);
            }

            return outBuffer;
        }

        StatusOr<ParsedMqMesh> AsyncLoader::parseMqMeshBuffer(std::span<const uint8_t> buffer)
        {
            if (buffer.size() < sizeof(MqMeshHeader))
            {
                return InvalidArgumentError("网格二进制文件尺寸小于 MqMeshHeader 头部大小");
            }

            ParsedMqMesh parsed;
            std::memcpy(&parsed.header, buffer.data(), sizeof(MqMeshHeader));

            if (parsed.header.magic != 0x4D514D53u)
            {
                return InvalidArgumentError("无效的 MqMesh 魔数");
            }
            if (parsed.header.version != 1u)
            {
                return InvalidArgumentError("不支持的 MqMesh 版本号");
            }

            const uint64_t stride = (parsed.header.vertexStride > 0)
                ? static_cast<uint64_t>(parsed.header.vertexStride)
                : static_cast<uint64_t>(sizeof(StandardMeshVertex));
            if (parsed.header.vertexCount > 0 &&
                stride > (static_cast<uint64_t>(buffer.size()) / static_cast<uint64_t>(parsed.header.vertexCount)))
            {
                return InvalidArgumentError("MqMesh 顶点字节长度溢出缓冲区边界");
            }

            const uint64_t vbSize64 = static_cast<uint64_t>(parsed.header.vertexCount) * stride;
            const uint64_t headerPlusVb = static_cast<uint64_t>(sizeof(MqMeshHeader)) + vbSize64;
            if (headerPlusVb > buffer.size())
            {
                return InvalidArgumentError("MqMesh 顶点载荷被截断");
            }

            const uint64_t remainingAfterVb = buffer.size() - headerPlusVb;
            const uint64_t ib32Size = static_cast<uint64_t>(parsed.header.indexCount) * sizeof(uint32_t);
            const uint64_t ib16Size = static_cast<uint64_t>(parsed.header.indexCount) * sizeof(uint16_t);

            uint64_t ibSize64 = 0;
            if (parsed.header.indexCount == 0)
            {
                parsed.uses32BitIndices = true;
                ibSize64 = 0;
            }
            else if (remainingAfterVb >= ib32Size)
            {
                parsed.uses32BitIndices = true;
                ibSize64 = ib32Size;
            }
            else if (remainingAfterVb >= ib16Size)
            {
                parsed.uses32BitIndices = false;
                ibSize64 = ib16Size;
            }
            else
            {
                return InvalidArgumentError("MqMesh 索引缓冲区字节长度不足");
            }

            const size_t vbOffset = sizeof(MqMeshHeader);
            const size_t vbSize = static_cast<size_t>(vbSize64);
            const size_t ibOffset = vbOffset + vbSize;
            const size_t ibSize = static_cast<size_t>(ibSize64);

            parsed.vertexBytes.assign(buffer.data() + vbOffset, buffer.data() + vbOffset + vbSize);
            parsed.indexBytes.assign(buffer.data() + ibOffset, buffer.data() + ibOffset + ibSize);

            const size_t subMeshOffset = ibOffset + ibSize;
            const size_t trailingBytes = buffer.size() - subMeshOffset;
            if (trailingBytes >= sizeof(SubMeshRange))
            {
                const size_t countFromTrailing = std::min<size_t>(
                    parsed.header.subMeshCount,
                    trailingBytes / sizeof(SubMeshRange));
                parsed.subMeshes.resize(countFromTrailing);
                std::memcpy(parsed.subMeshes.data(), buffer.data() + subMeshOffset, countFromTrailing * sizeof(SubMeshRange));
                CHECK_STATUS(validateSubmeshRanges(parsed.header, parsed.subMeshes));
            }
            else if (parsed.header.indexCount > 0)
            {
                SubMeshRange defaultRange{};
                defaultRange.indexOffset = 0;
                defaultRange.indexCount = parsed.header.indexCount;
                defaultRange.baseVertex = 0;
                defaultRange.materialIndex = 0;
                parsed.subMeshes.push_back(defaultRange);
            }

            return parsed;
        }

        Status AsyncLoader::submitAsyncMqMeshRead(
            const std::string& path,
            std::function<void(StatusOr<MqMeshHeader>, std::vector<uint8_t>, std::vector<uint8_t>)> onComplete)
        {
            if (!onComplete)
            {
                return InvalidArgumentError("onComplete 回调不能为空");
            }

            return submitAsyncBinaryRead(path, [cb = std::move(onComplete)](StatusOr<std::vector<uint8_t>> binaryResult) {
                if (!binaryResult.ok())
                {
                    cb(binaryResult.status(), {}, {});
                    return;
                }

                auto parsedOr = parseMqMeshBuffer(*binaryResult);
                if (!parsedOr.ok())
                {
                    cb(parsedOr.status(), {}, {});
                    return;
                }

                cb(parsedOr->header, std::move(parsedOr->vertexBytes), std::move(parsedOr->indexBytes));
            });
        }

        void AsyncLoader::recordCacheAccess(std::string_view sha256Hex, uint64_t sizeBytes)
        {
            if (sha256Hex.empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            const std::string key(sha256Hex);
            const uint64_t seq = m_accessSequence.fetch_add(1, std::memory_order_relaxed);
            auto it = m_cacheLedger.find(key);

            if (it != m_cacheLedger.end())
            {
                it->second.lastAccessTime = seq;
                if (sizeBytes > 0 && it->second.sizeBytes != sizeBytes)
                {
                    m_totalCacheUsageBytes.fetch_sub(it->second.sizeBytes, std::memory_order_relaxed);
                    it->second.sizeBytes = sizeBytes;
                    m_totalCacheUsageBytes.fetch_add(sizeBytes, std::memory_order_relaxed);
                }
            }
            else
            {
                CacheLedgerEntry entry;
                entry.sha256Hex = key;
                entry.sizeBytes = sizeBytes;
                entry.lastAccessTime = seq;
                entry.pinned = false;
                m_cacheLedger.emplace(key, std::move(entry));
                m_totalCacheUsageBytes.fetch_add(sizeBytes, std::memory_order_relaxed);
            }
        }

        void AsyncLoader::updateCacheAccessTime(std::string_view sha256Hex)
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            auto it = m_cacheLedger.find(std::string(sha256Hex));
            if (it != m_cacheLedger.end())
            {
                it->second.lastAccessTime = m_accessSequence.fetch_add(1, std::memory_order_relaxed);
            }
        }

        void AsyncLoader::pinCacheEntry(std::string_view sha256Hex)
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            auto it = m_cacheLedger.find(std::string(sha256Hex));
            if (it != m_cacheLedger.end())
            {
                it->second.pinned = true;
            }
        }

        void AsyncLoader::unpinCacheEntry(std::string_view sha256Hex)
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            auto it = m_cacheLedger.find(std::string(sha256Hex));
            if (it != m_cacheLedger.end())
            {
                it->second.pinned = false;
            }
        }

        bool AsyncLoader::hasCacheEntry(std::string_view sha256Hex) const
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            return m_cacheLedger.find(std::string(sha256Hex)) != m_cacheLedger.end();
        }

        void AsyncLoader::clearCacheLedger()
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            m_cacheLedger.clear();
            m_totalCacheUsageBytes.store(0, std::memory_order_relaxed);
        }

        void AsyncLoader::enforceCacheQuota(uint64_t highWatermarkBytes, uint64_t lowWatermarkBytes)
        {
            std::lock_guard<std::mutex> lock(m_ledgerMutex);
            if (m_totalCacheUsageBytes.load(std::memory_order_relaxed) <= highWatermarkBytes)
            {
                return;
            }

            const uint64_t targetBytes = std::min(highWatermarkBytes, lowWatermarkBytes);

            std::vector<CacheLedgerEntry> evictableEntries;
            evictableEntries.reserve(m_cacheLedger.size());
            for (const auto& [_, entry] : m_cacheLedger)
            {
                if (!entry.pinned)
                {
                    evictableEntries.push_back(entry);
                }
            }

            std::sort(evictableEntries.begin(), evictableEntries.end(),
                [](const CacheLedgerEntry& a, const CacheLedgerEntry& b) {
                    return a.lastAccessTime < b.lastAccessTime;
                });

            for (const auto& entry : evictableEntries)
            {
                if (m_totalCacheUsageBytes.load(std::memory_order_relaxed) <= targetBytes)
                {
                    break;
                }

                std::string filePath = entry.sha256Hex;
                if (!filePath.starts_with("/cache/"))
                {
                    filePath = StrCat("/cache/", entry.sha256Hex, ".bin");
                }
                if (FCT::SAI::FileSystem::exists(filePath))
                {
                    (void)FCT::SAI::FileSystem::removeFile(filePath);
                }

                const uint64_t currentTotal = m_totalCacheUsageBytes.load(std::memory_order_relaxed);
                const uint64_t nextTotal = (currentTotal >= entry.sizeBytes) ? (currentTotal - entry.sizeBytes) : 0ULL;
                m_totalCacheUsageBytes.store(nextTotal, std::memory_order_relaxed);
                m_cacheLedger.erase(entry.sha256Hex);
            }
        }

        void AsyncLoader::onMemoryWarning(int level)
        {
            if (level < 0)
            {
                return;
            }
            if (level >= 2)
            {
                enforceCacheQuota(EmergencyCacheWatermarkBytes, 80ULL * 1024ULL * 1024ULL);
            }
            else
            {
                enforceCacheQuota(DefaultCacheLowWatermarkBytes, EmergencyCacheWatermarkBytes);
            }
        }

    } // namespace Asset
} // namespace MQEngine
