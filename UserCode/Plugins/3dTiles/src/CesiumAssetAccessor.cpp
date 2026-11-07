#define TILES3D_INCLUDE_HTTPLIB
#include "CesiumAssetAccessor.h"

namespace MQEngine {

CesiumAssetAccessor::CesiumAssetAccessor(ResourceLoader* resourceLoader)
    : m_resourceLoader(resourceLoader) {
    autoDetectProxy();
    initDiskCacheDirs();
}

void CesiumAssetAccessor::setProxy(const std::string& host, int port) {
    m_proxyHost = host;
    m_proxyPort = port;
}

void CesiumAssetAccessor::setCacheDir(const std::string& cacheDir) {
    if (cacheDir.empty()) {
        return;
    }
    std::string normalized = cacheDir;
    for (char& ch : normalized) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (normalized.size() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (normalized == m_cacheDir) {
        return;
    }
    m_cacheDir = normalized;
    ResourceLoader* loader = resolveResourceLoader();
    if (loader) {
        (void)loader->ensureDir(m_cacheDir);
    } else {
        std::error_code ec;
        std::filesystem::create_directories(m_cacheDir, ec);
    }
    migrateExistingCacheToFixedDir();
}

void CesiumAssetAccessor::autoDetectProxy() {
    const char* envVars[] = {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"};
    for (const char* var : envVars) {
        if (const char* val = std::getenv(var); val && val[0] != '\0') {
            std::string proxyStr(val);
            if (auto pos = proxyStr.find("://"); pos != std::string::npos) {
                proxyStr = proxyStr.substr(pos + 3);
            }
            if (!proxyStr.empty() && proxyStr.back() == '/') {
                proxyStr.pop_back();
            }
            if (auto colon = proxyStr.rfind(':'); colon != std::string::npos) {
                m_proxyHost = proxyStr.substr(0, colon);
                m_proxyPort = std::atoi(proxyStr.substr(colon + 1).c_str());
                if (!m_proxyHost.empty() && m_proxyPort > 0) {
                    return;
                }
            }
        }
    }

#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD proxyEnable = 0;
        DWORD dwSize = sizeof(proxyEnable);
        if (RegQueryValueExA(hKey, "ProxyEnable", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(&proxyEnable), &dwSize) == ERROR_SUCCESS && proxyEnable != 0) {
            char proxyServer[512] = {0};
            dwSize = sizeof(proxyServer) - 1;
            if (RegQueryValueExA(hKey, "ProxyServer", nullptr, nullptr,
                    reinterpret_cast<LPBYTE>(proxyServer), &dwSize) == ERROR_SUCCESS) {
                std::string proxyStr(proxyServer);
                if (auto pos = proxyStr.find("http="); pos != std::string::npos) {
                    proxyStr = proxyStr.substr(pos + 5);
                    if (auto semi = proxyStr.find(';'); semi != std::string::npos) {
                        proxyStr = proxyStr.substr(0, semi);
                    }
                }
                if (auto pos = proxyStr.find("://"); pos != std::string::npos) {
                    proxyStr = proxyStr.substr(pos + 3);
                }
                if (auto colon = proxyStr.rfind(':'); colon != std::string::npos) {
                    m_proxyHost = proxyStr.substr(0, colon);
                    m_proxyPort = std::atoi(proxyStr.substr(colon + 1).c_str());
                }
            }
        }
        RegCloseKey(hKey);
    }
#endif
}

void CesiumAssetAccessor::initDiskCacheDirs() {
    if (const char* envDir = std::getenv("MQENGINE_CESIUM_CACHE_DIR"); envDir && envDir[0] != '\0') {
        m_cacheDir = envDir;
        for (char& ch : m_cacheDir) {
            if (ch == '\\') {
                ch = '/';
            }
        }
    }

    ResourceLoader* loader = resolveResourceLoader();
    if (loader) {
        (void)loader->ensureDir(m_cacheDir);
    } else {
        std::error_code ec;
        std::filesystem::create_directories(m_cacheDir, ec);
    }

    const char* candidates[] = {
        "G:/MQEngine/MQEngine/Example/CesiumProject/.cache/cesium",
        ".cache/cesium",
        "G:/nexus_workspace/NexusEngine/build-windows-release/.cache/cesium",
        "G:/nexus_workspace/NexusEngine/build-windows-debug/.cache/cesium"
    };
    for (const char* dir : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            m_fallbackCacheDirs.emplace_back(dir);
        }
    }

    migrateExistingCacheToFixedDir();
}

void CesiumAssetAccessor::migrateExistingCacheToFixedDir() {
    std::error_code ec;
    std::filesystem::create_directories(m_cacheDir, ec);
    for (const auto& srcDir : m_fallbackCacheDirs) {
        if (srcDir == m_cacheDir || !std::filesystem::exists(srcDir, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(srcDir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            std::filesystem::path dstPath = std::filesystem::path(m_cacheDir) / entry.path().filename();
            if (!std::filesystem::exists(dstPath, ec)) {
                std::filesystem::copy_file(
                    entry.path(),
                    dstPath,
                    std::filesystem::copy_options::skip_existing,
                    ec);
            }
        }
    }
}

std::string CesiumAssetAccessor::computeBoundingVolumeCacheKey(
    const Cesium3DTilesSelection::BoundingVolume& bv) {
    if (const auto* pBox = std::get_if<CesiumGeometry::OrientedBoundingBox>(&bv)) {
        const glm::dvec3& c = pBox->getCenter();
        const glm::dmat3& h = pBox->getHalfAxes();
        return fmt::format(
            "box_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
            c.x, c.y, c.z,
            h[0].x, h[0].y, h[0].z,
            h[1].x, h[1].y, h[1].z,
            h[2].x, h[2].y, h[2].z);
    }
    if (const auto* pRegion = std::get_if<CesiumGeospatial::BoundingRegion>(&bv)) {
        const auto& r = pRegion->getRectangle();
        return fmt::format(
            "region_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
            r.getWest(), r.getSouth(), r.getEast(), r.getNorth(),
            pRegion->getMinimumHeight(), pRegion->getMaximumHeight());
    }
    if (const auto* pSphere = std::get_if<CesiumGeometry::BoundingSphere>(&bv)) {
        const glm::dvec3& c = pSphere->getCenter();
        return fmt::format(
            "sphere_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
            c.x, c.y, c.z, pSphere->getRadius());
    }
    if (const auto* pLoose = std::get_if<CesiumGeospatial::BoundingRegionWithLooseFittingHeights>(&bv)) {
        const auto& r = pLoose->getBoundingRegion().getRectangle();
        return fmt::format(
            "region_loose_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}_{:.8f}",
            r.getWest(), r.getSouth(), r.getEast(), r.getNorth(),
            pLoose->getBoundingRegion().getMinimumHeight(),
            pLoose->getBoundingRegion().getMaximumHeight());
    }
    if (const auto* pS2 = std::get_if<CesiumGeospatial::S2CellBoundingVolume>(&bv)) {
        return fmt::format(
            "s2_{}_{:.8f}_{:.8f}",
            pS2->getCellID().getID(),
            pS2->getMinimumHeight(),
            pS2->getMaximumHeight());
    }
    return "unknown_bv";
}

namespace {

bool extractDoubleNumber(const boost::json::value& val, double& outNumber) {
    if (val.is_double()) {
        outNumber = val.as_double();
        return true;
    }
    if (val.is_int64()) {
        outNumber = static_cast<double>(val.as_int64());
        return true;
    }
    if (val.is_uint64()) {
        outNumber = static_cast<double>(val.as_uint64());
        return true;
    }
    return false;
}

glm::dmat4 parseJsonNodeTransform(const boost::json::object& nodeObj, const glm::dmat4& parentTransform) {
    auto it = nodeObj.find("transform");
    if (it == nodeObj.end() || !it->value().is_array()) {
        return parentTransform;
    }
    const auto& arr = it->value().as_array();
    if (arr.size() < 16) {
        return parentTransform;
    }
    double nums[16]{};
    for (size_t i = 0; i < 16; ++i) {
        if (!extractDoubleNumber(arr[i], nums[i])) {
            return parentTransform;
        }
    }
    glm::dmat4 localMat(
        glm::dvec4(nums[0], nums[1], nums[2], nums[3]),
        glm::dvec4(nums[4], nums[5], nums[6], nums[7]),
        glm::dvec4(nums[8], nums[9], nums[10], nums[11]),
        glm::dvec4(nums[12], nums[13], nums[14], nums[15]));
    return parentTransform * localMat;
}

std::optional<Cesium3DTilesSelection::BoundingVolume> parseJsonBoundingVolume(
    const boost::json::object& bvObj,
    const glm::dmat4& tileTransform) {
    if (auto boxIt = bvObj.find("box"); boxIt != bvObj.end() && boxIt->value().is_array()) {
        const auto& arr = boxIt->value().as_array();
        if (arr.size() >= 12) {
            double v[12]{};
            bool ok = true;
            for (size_t i = 0; i < 12; ++i) {
                ok = ok && extractDoubleNumber(arr[i], v[i]);
            }
            if (ok) {
                CesiumGeometry::OrientedBoundingBox obb(
                    glm::dvec3(v[0], v[1], v[2]),
                    glm::dmat3(
                        v[3], v[4], v[5],
                        v[6], v[7], v[8],
                        v[9], v[10], v[11]));
                return obb.transform(tileTransform);
            }
        }
    }
    if (auto regIt = bvObj.find("region"); regIt != bvObj.end() && regIt->value().is_array()) {
        const auto& arr = regIt->value().as_array();
        if (arr.size() >= 6) {
            double v[6]{};
            bool ok = true;
            for (size_t i = 0; i < 6; ++i) {
                ok = ok && extractDoubleNumber(arr[i], v[i]);
            }
            if (ok) {
                return CesiumGeospatial::BoundingRegion(
                    CesiumGeospatial::GlobeRectangle(v[0], v[1], v[2], v[3]),
                    v[4],
                    v[5],
                    CesiumGeospatial::Ellipsoid::WGS84);
            }
        }
    }
    if (auto sphIt = bvObj.find("sphere"); sphIt != bvObj.end() && sphIt->value().is_array()) {
        const auto& arr = sphIt->value().as_array();
        if (arr.size() >= 4) {
            double v[4]{};
            bool ok = true;
            for (size_t i = 0; i < 4; ++i) {
                ok = ok && extractDoubleNumber(arr[i], v[i]);
            }
            if (ok) {
                CesiumGeometry::BoundingSphere bs(glm::dvec3(v[0], v[1], v[2]), v[3]);
                return bs.transform(tileTransform);
            }
        }
    }
    return std::nullopt;
}

std::string extractContentUriFromObject(const boost::json::object& contentObj) {
    if (auto uriIt = contentObj.find("uri"); uriIt != contentObj.end() && uriIt->value().is_string()) {
        return std::string(uriIt->value().as_string());
    }
    if (auto urlIt = contentObj.find("url"); urlIt != contentObj.end() && urlIt->value().is_string()) {
        return std::string(urlIt->value().as_string());
    }
    return "";
}

} // namespace

std::string CesiumAssetAccessor::computeCacheFileName(
    const std::string& url,
    const std::string& bboxCacheKey) {
    std::string cleanUrl = url;
    if (auto qPos = cleanUrl.find('?'); qPos != std::string::npos) {
        cleanUrl = cleanUrl.substr(0, qPos);
    }

    auto fnv1aHash = [](const std::string& str) -> uint64_t {
        uint64_t h = 14695981039346656037ULL;
        for (unsigned char c : str) {
            h ^= static_cast<uint64_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    };

    char hexBuf[32];
    if (!bboxCacheKey.empty()) {
        std::snprintf(hexBuf, sizeof(hexBuf), "bbox_%016llx",
            static_cast<unsigned long long>(fnv1aHash(bboxCacheKey)));
    } else {
        std::snprintf(hexBuf, sizeof(hexBuf), "url_%016llx",
            static_cast<unsigned long long>(fnv1aHash(cleanUrl)));
    }

    std::string ext = ".bin";
    if (cleanUrl.ends_with(".glb")) ext = ".glb";
    else if (cleanUrl.ends_with(".b3dm")) ext = ".b3dm";
    else if (cleanUrl.ends_with(".json") || cleanUrl.ends_with("/endpoint")) ext = ".json";
    else if (cleanUrl.ends_with(".subtree")) ext = ".subtree";

    return std::string(hexBuf) + ext;
}

bool CesiumAssetAccessor::isCacheableRequest(const std::string& verb, const std::string& url) {
    (void)url;
    return verb == "GET";
}

namespace {

std::vector<std::string> buildCandidateRelativePaths(const std::string& cacheFileName) {
    std::vector<std::string> relPaths;
    if (cacheFileName.empty()) {
        return relPaths;
    }
    relPaths.push_back(cacheFileName);

    auto dotPos = cacheFileName.rfind('.');
    std::string stem = (dotPos != std::string::npos) ? cacheFileName.substr(0, dotPos) : cacheFileName;
    std::string ext = (dotPos != std::string::npos) ? cacheFileName.substr(dotPos) : "";

    if (stem.rfind("bbox_", 0) == 0 && stem.size() == 21) {
        std::string hexPart = stem.substr(5);
        char* endPtr = nullptr;
        unsigned long long hashVal = std::strtoull(hexPart.c_str(), &endPtr, 16);
        if (endPtr == hexPart.c_str() + hexPart.size()) {
            std::string decName = "bbox_" + std::to_string(hashVal) + ext;
            relPaths.push_back("tile.googleapis.com/v1/3dtiles/datasets/CgIYAQ/files/" + decName);
            relPaths.push_back(decName);
        }
    } else if (stem.rfind("url_", 0) == 0) {
        if (ext == ".json") {
            relPaths.push_back(stem + ".bin");
        } else if (ext == ".bin") {
            relPaths.push_back(stem + ".json");
        }
        relPaths.push_back("tile.googleapis.com/v1/3dtiles/root.json");
        relPaths.push_back("api.cesium.com/v1/assets/2275207/url_3604680789072291402.bin");
    }
    return relPaths;
}

} // namespace

bool CesiumAssetAccessor::hasCachedFile(const std::string& cacheFileName) {
    if (cacheFileName.empty()) {
        return false;
    }
    ResourceLoader* loader = resolveResourceLoader();
    std::string primaryPath = m_cacheDir + "/" + cacheFileName;
    if (loader && loader->exists(primaryPath)) {
        return true;
    }
    std::error_code ec;
    if (std::filesystem::exists(primaryPath, ec) && std::filesystem::file_size(primaryPath, ec) > 0) {
        return true;
    }

    auto relPaths = buildCandidateRelativePaths(cacheFileName);
    for (const auto& dir : m_fallbackCacheDirs) {
        for (const auto& rel : relPaths) {
            std::string fallbackPath = dir + "/" + rel;
            if ((loader && loader->exists(fallbackPath)) ||
                (std::filesystem::exists(fallbackPath, ec) && std::filesystem::file_size(fallbackPath, ec) > 0)) {
                return true;
            }
        }
    }
    return false;
}

void CesiumAssetAccessor::inspectJsonTileNodeCacheHit(
    const boost::json::object& nodeObj,
    const glm::dmat4& parentTransform,
    size_t& inoutHitCount,
    size_t& inoutTotalCount) {
    glm::dmat4 tileTransform = parseJsonNodeTransform(nodeObj, parentTransform);

    std::string bboxCacheKey;
    if (auto bvIt = nodeObj.find("boundingVolume"); bvIt != nodeObj.end() && bvIt->value().is_object()) {
        auto bvOpt = parseJsonBoundingVolume(bvIt->value().as_object(), tileTransform);
        if (bvOpt.has_value()) {
            bboxCacheKey = computeBoundingVolumeCacheKey(*bvOpt);
        }
    }

    auto checkSingleContentObject = [&](const boost::json::object& contentObj) {
        std::string uri = extractContentUriFromObject(contentObj);
        if (uri.empty()) {
            return;
        }
        std::string childCacheFileName = computeCacheFileName(uri, bboxCacheKey);
        ++inoutTotalCount;
        if (hasCachedFile(childCacheFileName)) {
            ++inoutHitCount;
        }
    };

    if (auto contentIt = nodeObj.find("content"); contentIt != nodeObj.end() && contentIt->value().is_object()) {
        checkSingleContentObject(contentIt->value().as_object());
    }
    if (auto contentsIt = nodeObj.find("contents"); contentsIt != nodeObj.end() && contentsIt->value().is_array()) {
        for (const auto& item : contentsIt->value().as_array()) {
            if (item.is_object()) {
                checkSingleContentObject(item.as_object());
            }
        }
    }

    if (auto childrenIt = nodeObj.find("children"); childrenIt != nodeObj.end() && childrenIt->value().is_array()) {
        for (const auto& childVal : childrenIt->value().as_array()) {
            if (childVal.is_object()) {
                inspectJsonTileNodeCacheHit(childVal.as_object(), tileTransform, inoutHitCount, inoutTotalCount);
            }
        }
    }
}

bool CesiumAssetAccessor::checkJsonCache100PercentHit(
    const std::vector<std::byte>& jsonBytes,
    size_t& outHitCount,
    size_t& outTotalCount) {
    outHitCount = 0;
    outTotalCount = 0;
    if (jsonBytes.empty()) {
        return false;
    }

    boost::system::error_code ec;
    std::string_view sv(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size());
    boost::json::value parsed = boost::json::parse(sv, ec);
    if (ec || !parsed.is_object()) {
        return false;
    }

    const auto& topObj = parsed.as_object();
    glm::dmat4 rootParentTransform = parseJsonNodeTransform(topObj, glm::dmat4(1.0));
    auto rootIt = topObj.find("root");
    if (rootIt == topObj.end() || !rootIt->value().is_object()) {
        return false;
    }

    inspectJsonTileNodeCacheHit(rootIt->value().as_object(), rootParentTransform, outHitCount, outTotalCount);
    return outTotalCount > 0 && outHitCount == outTotalCount;
}

std::unique_ptr<CesiumAssetResponse> CesiumAssetAccessor::tryLoadCachedResponse(
    const std::string& cacheFileName) {
    ResourceLoader* loader = resolveResourceLoader();
    std::string primaryPath = m_cacheDir + "/" + cacheFileName;
    auto relPaths = buildCandidateRelativePaths(cacheFileName);

    std::vector<std::string> candidatePaths;
    for (const auto& rel : relPaths) {
        // Note: 仅对与 cacheFileName 精确匹配或同类型的回退路径进行匹配，防止 url_* 误配 root.json 与 endpoint
        if (rel.find("root.json") != std::string::npos && cacheFileName != "url_6379943ce4be75bb.json") {
            continue;
        }
        if (rel.find("2275207") != std::string::npos && cacheFileName == "url_6379943ce4be75bb.json") {
            continue;
        }
        candidatePaths.push_back(m_cacheDir + "/" + rel);
    }
    for (const auto& dir : m_fallbackCacheDirs) {
        for (const auto& rel : relPaths) {
            if (rel.find("root.json") != std::string::npos && cacheFileName != "url_6379943ce4be75bb.json") {
                continue;
            }
            if (rel.find("2275207") != std::string::npos && cacheFileName == "url_6379943ce4be75bb.json") {
                continue;
            }
            candidatePaths.push_back(dir + "/" + rel);
        }
    }

    for (size_t idx = 0; idx < candidatePaths.size(); ++idx) {
        const auto& path = candidatePaths[idx];
        if (loader) {
            auto contentRes = loader->loadFile(path);
            if (contentRes.ok() && !contentRes.value().empty()) {
                const std::string& content = contentRes.value();
                if (path != primaryPath) {
                    (void)loader->ensureDir(m_cacheDir);
                    (void)loader->saveFile(primaryPath, content);
                }
                std::vector<std::byte> cachedData(content.size());
                std::memcpy(cachedData.data(), content.data(), content.size());
                return std::make_unique<CesiumAssetResponse>(
                    200,
                    inferContentTypeFromPath(cacheFileName),
                    CesiumAsync::HttpHeaders{},
                    std::move(cachedData));
            }
        }
    }
    return nullptr;
}

void CesiumAssetAccessor::saveResponseToDiskCache(
    const std::string& cacheFileName,
    const std::string& bodyBytes) {
    if (bodyBytes.empty()) {
        return;
    }
    ResourceLoader* loader = resolveResourceLoader();
    if (!loader) {
        return;
    }
    (void)loader->ensureDir(m_cacheDir);
    (void)loader->saveFile(m_cacheDir + "/" + cacheFileName, bodyBytes);
}

ResourceLoader* CesiumAssetAccessor::resolveResourceLoader() {
    if (m_resourceLoader) {
        return m_resourceLoader;
    }
    if (g_engineGlobal.resourceLoader) {
        return g_engineGlobal.resourceLoader;
    }
    return Runtime::Engine::getInstance() ? Runtime::Engine::getInstance()->resource->getResourceLoader() : nullptr;
}

bool CesiumAssetAccessor::isHttpOrHttpsUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string CesiumAssetAccessor::decodePercentEncodedUrl(const std::string& input) {
    std::string decoded;
    decoded.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            char hex[3] = {input[i + 1], input[i + 2], '\0'};
            char* endPtr = nullptr;
            long val = std::strtol(hex, &endPtr, 16);
            if (endPtr == hex + 2) {
                decoded.push_back(static_cast<char>(val));
                i += 2;
                continue;
            }
        }
        decoded.push_back(input[i]);
    }
    return decoded;
}

std::string CesiumAssetAccessor::extractLocalFilePath(const std::string& url) {
    std::string filePath = url;
    if (filePath.rfind("file:///", 0) == 0) {
#ifdef _WIN32
        filePath = filePath.substr(8);
#else
        filePath = filePath.substr(7);
#endif
    } else if (filePath.rfind("file://", 0) == 0) {
        filePath = filePath.substr(7);
    }

    size_t queryPos = filePath.find('?');
    if (queryPos != std::string::npos) {
        filePath = filePath.substr(0, queryPos);
    }

    return decodePercentEncodedUrl(filePath);
}

std::string CesiumAssetAccessor::inferContentTypeFromPath(const std::string& filePath) {
    if (filePath.ends_with(".json")) {
        return "application/json";
    }
    return "application/octet-stream";
}

bool CesiumAssetAccessor::parseHttpEndpoint(
    const std::string& url,
    std::string& outHostUrl,
    std::string& outPathAndQuery) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return false;
    }

    size_t pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos) {
        outHostUrl = url;
        outPathAndQuery = "/";
    } else {
        outHostUrl = url.substr(0, pathStart);
        outPathAndQuery = url.substr(pathStart);
    }
    return true;
}

std::unique_ptr<CesiumAssetResponse> CesiumAssetAccessor::loadLocalFileResponse(const std::string& url) {
    std::string filePath = extractLocalFilePath(url);
    ResourceLoader* loader = resolveResourceLoader();
    if (!loader) {
        return std::make_unique<CesiumAssetResponse>(404, "text/plain", CesiumAsync::HttpHeaders{}, std::vector<std::byte>{});
    }

    auto contentRes = loader->loadFile(filePath);
    if (!contentRes.ok()) {
        return std::make_unique<CesiumAssetResponse>(404, "text/plain", CesiumAsync::HttpHeaders{}, std::vector<std::byte>{});
    }

    const std::string& content = contentRes.value();
    std::vector<std::byte> buffer(content.size());
    if (!content.empty()) {
        std::memcpy(buffer.data(), content.data(), content.size());
    }

    return std::make_unique<CesiumAssetResponse>(
        200,
        inferContentTypeFromPath(filePath),
        CesiumAsync::HttpHeaders{},
        std::move(buffer));
}

std::unique_ptr<CesiumAssetResponse> CesiumAssetAccessor::executeHttpRequest(
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& headers,
    const std::vector<std::byte>& body) {
    std::string bboxCacheKey;
    for (const auto& kv : headers) {
        std::string keyLower = kv.first;
        for (char& ch : keyLower) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (keyLower == "x-cesium-cache-key") {
            bboxCacheKey = kv.second;
            break;
        }
    }

    bool canCache = isCacheableRequest(verb, url);
    std::string cacheFileName = canCache ? computeCacheFileName(url, bboxCacheKey) : "";
    std::unique_ptr<CesiumAssetResponse> cachedFallbackResp;
    if (canCache) {
        if (auto cachedResp = tryLoadCachedResponse(cacheFileName)) {
            m_cacheHitCount.fetch_add(1, std::memory_order_relaxed);
            if (cacheFileName.ends_with(".json")) {
                m_json100PercentBypassCount.fetch_add(1, std::memory_order_relaxed);
            }
            return cachedResp;
        }
    }

    m_networkFetchCount.fetch_add(1, std::memory_order_relaxed);

    std::string hostUrl;
    std::string pathAndQuery;
    if (!parseHttpEndpoint(url, hostUrl, pathAndQuery)) {
        if (cachedFallbackResp) {
            return cachedFallbackResp;
        }
        return std::make_unique<CesiumAssetResponse>(400, "text/plain", CesiumAsync::HttpHeaders{}, std::vector<std::byte>{});
    }

    httplib::Headers reqHeaders;
    for (const auto& kv : headers) {
        std::string keyLower = kv.first;
        for (char& ch : keyLower) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (keyLower == "accept-encoding" || keyLower == "x-cesium-cache-key") {
            continue;
        }
        reqHeaders.emplace(kv.first, kv.second);
    }

    auto sendOnce = [&](bool useProxy) -> httplib::Result {
        httplib::Client cli(hostUrl);
        cli.set_follow_location(true);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(10, 0);
        cli.set_write_timeout(10, 0);
        if (useProxy && !m_proxyHost.empty() && m_proxyPort > 0) {
            cli.set_proxy(m_proxyHost.c_str(), m_proxyPort);
        }
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        cli.enable_server_certificate_verification(false);
#endif
        if (verb == "GET") {
            return cli.Get(pathAndQuery.c_str(), reqHeaders);
        }
        if (verb == "POST") {
            std::string reqBody(reinterpret_cast<const char*>(body.data()), body.size());
            return cli.Post(pathAndQuery.c_str(), reqHeaders, reqBody, "application/json");
        }
        httplib::Request req;
        req.method = verb;
        req.path = pathAndQuery;
        req.headers = reqHeaders;
        req.body.assign(reinterpret_cast<const char*>(body.data()), body.size());
        return cli.send(req);
    };

    const bool hasProxy = !m_proxyHost.empty() && m_proxyPort > 0;
    httplib::Result res = sendOnce(hasProxy);
    if ((!res || res->status >= 500) && hasProxy) {
        // Note: 当系统代理 SSL 握手失败或不可达时，自动清空失效代理并回退至直连请求
        m_proxyHost.clear();
        m_proxyPort = 0;
        res = sendOnce(false);
    }

    if (!res) {
        if (cachedFallbackResp) {
            return cachedFallbackResp;
        }
        return std::make_unique<CesiumAssetResponse>(503, "text/plain", CesiumAsync::HttpHeaders{}, std::vector<std::byte>{});
    }
    if (res->status != 200 && cachedFallbackResp) {
        return cachedFallbackResp;
    }

    CesiumAsync::HttpHeaders respHeaders;
    std::string contentType = "application/octet-stream";
    for (const auto& h : res->headers) {
        respHeaders.emplace(h.first, h.second);
        if (h.first == "Content-Type" || h.first == "content-type") {
            contentType = h.second;
        }
    }

    if (canCache && res->status == 200 && !res->body.empty()) {
        saveResponseToDiskCache(cacheFileName, res->body);
    }

    std::vector<std::byte> respData(res->body.size());
    if (!res->body.empty()) {
        std::memcpy(respData.data(), res->body.data(), res->body.size());
    }

    return std::make_unique<CesiumAssetResponse>(
        static_cast<uint16_t>(res->status),
        std::move(contentType),
        std::move(respHeaders),
        std::move(respData));
}

CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> CesiumAssetAccessor::get(
    const CesiumAsync::AsyncSystem& asyncSystem,
    const std::string& url,
    const std::vector<THeader>& headers) {
    return request(asyncSystem, "GET", url, headers, {});
}

CesiumAsync::Future<std::shared_ptr<CesiumAsync::IAssetRequest>> CesiumAssetAccessor::request(
    const CesiumAsync::AsyncSystem& asyncSystem,
    const std::string& verb,
    const std::string& url,
    const std::vector<THeader>& headers,
    const gsl::span<const std::byte>& contentPayload) {
    std::vector<std::byte> body(contentPayload.begin(), contentPayload.end());

    return asyncSystem.runInWorkerThread([this, verb, url, headers, body = std::move(body)]() -> std::shared_ptr<CesiumAsync::IAssetRequest> {
        CesiumAsync::HttpHeaders reqHeaders;
        for (const auto& h : headers) {
            reqHeaders.emplace(h.first, h.second);
        }

        std::unique_ptr<CesiumAssetResponse> response;
        if (isHttpOrHttpsUrl(url)) {
            response = executeHttpRequest(verb, url, headers, body);
        } else {
            response = loadLocalFileResponse(url);
        }

        return std::make_shared<CesiumAssetRequest>(
            verb,
            url,
            std::move(reqHeaders),
            std::move(response));
    });
}

void CesiumAssetAccessor::tick() noexcept {
}

} // namespace MQEngine
