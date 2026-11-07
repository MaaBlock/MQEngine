#ifndef MQENGINE_CESIUM_ASSET_ACCESSOR_H
#define MQENGINE_CESIUM_ASSET_ACCESSOR_H

#include "thirdparty/thirdparty.h"

namespace MQEngine {

/**
 * @brief Cesium 资源响应数据封装，保存状态码、响应头以及二进制响应载荷
 */
class TILES3D_PLUGIN_API CesiumAssetResponse : public CesiumAsync::IAssetResponse {
public:
    CesiumAssetResponse(
        uint16_t statusCode,
        std::string contentType,
        CesiumAsync::HttpHeaders headers,
        std::vector<std::byte> data)
        : m_statusCode(statusCode)
        , m_contentType(std::move(contentType))
        , m_headers(std::move(headers))
        , m_data(std::move(data)) {}

    uint16_t statusCode() const override { return m_statusCode; }
    std::string contentType() const override { return m_contentType; }
    const CesiumAsync::HttpHeaders& headers() const override { return m_headers; }
    gsl::span<const std::byte> data() const override {
        return gsl::span<const std::byte>(m_data.data(), m_data.size());
    }
    const std::vector<std::byte>& rawBytes() const { return m_data; }

private:
    uint16_t m_statusCode;
    std::string m_contentType;
    CesiumAsync::HttpHeaders m_headers;
    std::vector<std::byte> m_data;
};

/**
 * @brief Cesium 单次资源请求上下文，持有请求方法、URL、请求头与响应结果
 */
class TILES3D_PLUGIN_API CesiumAssetRequest : public CesiumAsync::IAssetRequest {
public:
    CesiumAssetRequest(
        std::string method,
        std::string url,
        CesiumAsync::HttpHeaders headers,
        std::unique_ptr<CesiumAssetResponse> response)
        : m_method(std::move(method))
        , m_url(std::move(url))
        , m_headers(std::move(headers))
        , m_response(std::move(response)) {}

    const std::string& method() const override { return m_method; }
    const std::string& url() const override { return m_url; }
    const CesiumAsync::HttpHeaders& headers() const override { return m_headers; }
    const CesiumAsync::IAssetResponse* response() const override { return m_response.get(); }

private:
    std::string m_method;
    std::string m_url;
    CesiumAsync::HttpHeaders m_headers;
    std::unique_ptr<CesiumAssetResponse> m_response;
};

/**
 * @brief Cesium 资源访问器，通过引擎 ResourceLoader 读取本地瓦片并支持 HTTP/HTTPS 远程流式请求
 */
class TILES3D_PLUGIN_API CesiumAssetAccessor : public CesiumAsync::IAssetAccessor {
public:
    explicit CesiumAssetAccessor(ResourceLoader* resourceLoader = nullptr);
    ~CesiumAssetAccessor() override = default;

    void setResourceLoader(ResourceLoader* resourceLoader) { m_resourceLoader = resourceLoader; }
    void setProxy(const std::string& host, int port);
    void setCacheDir(const std::string& cacheDir);
    const std::string& getCacheDir() const { return m_cacheDir; }

    uint64_t getCacheHitCount() const { return m_cacheHitCount.load(std::memory_order_relaxed); }
    uint64_t getJson100PercentBypassCount() const { return m_json100PercentBypassCount.load(std::memory_order_relaxed); }
    uint64_t getNetworkFetchCount() const { return m_networkFetchCount.load(std::memory_order_relaxed); }

    bool checkJsonCache100PercentHit(
        const std::vector<std::byte>& jsonBytes,
        size_t& outHitCount,
        size_t& outTotalCount);

    static std::string computeCacheFileName(const std::string& url, const std::string& bboxCacheKey);
    static std::string computeBoundingVolumeCacheKey(const Cesium3DTilesSelection::BoundingVolume& bv);

    CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> get(
        const CesiumAsync::AsyncSystem& asyncSystem,
        const std::string& url,
        const std::vector<THeader>& headers = {}) override;

    CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> request(
        const CesiumAsync::AsyncSystem& asyncSystem,
        const std::string& verb,
        const std::string& url,
        const std::vector<THeader>& headers = std::vector<THeader>(),
        const gsl::span<const std::byte>& contentPayload = {}) override;

    void tick() noexcept override;

private:
    ResourceLoader* resolveResourceLoader();
    void autoDetectProxy();
    void initDiskCacheDirs();
    void migrateExistingCacheToFixedDir();
    bool hasCachedFile(const std::string& cacheFileName);
    void inspectJsonTileNodeCacheHit(
        const boost::json::object& nodeObj,
        const glm::dmat4& parentTransform,
        size_t& inoutHitCount,
        size_t& inoutTotalCount);
    static bool isHttpOrHttpsUrl(const std::string& url);
    static std::string decodePercentEncodedUrl(const std::string& input);
    static std::string extractLocalFilePath(const std::string& url);
    static std::string inferContentTypeFromPath(const std::string& filePath);
    static bool parseHttpEndpoint(const std::string& url, std::string& outHostUrl, std::string& outPathAndQuery);
    static bool isCacheableRequest(const std::string& verb, const std::string& url);

    std::unique_ptr<CesiumAssetResponse> tryLoadCachedResponse(const std::string& cacheFileName);
    void saveResponseToDiskCache(const std::string& cacheFileName, const std::string& bodyBytes);
    std::unique_ptr<CesiumAssetResponse> loadLocalFileResponse(const std::string& url);
    std::unique_ptr<CesiumAssetResponse> executeHttpRequest(
        const std::string& verb,
        const std::string& url,
        const std::vector<THeader>& headers,
        const std::vector<std::byte>& body);

private:
    ResourceLoader* m_resourceLoader = nullptr;
    std::string m_proxyHost;
    int m_proxyPort = 0;
    std::string m_cacheDir = "G:/MQEngine/MQEngine/CesiumCache";
    std::vector<std::string> m_fallbackCacheDirs;
    std::atomic<uint64_t> m_cacheHitCount{0};
    std::atomic<uint64_t> m_json100PercentBypassCount{0};
    std::atomic<uint64_t> m_networkFetchCount{0};
};

} // namespace MQEngine

#endif // MQENGINE_CESIUM_ASSET_ACCESSOR_H
