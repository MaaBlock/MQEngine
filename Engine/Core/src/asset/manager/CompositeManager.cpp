/**
 * @file CompositeManager.cpp
 * @brief Composite 资产与插件拓扑调度管理器实现
 */
#include "CompositeManager.h"

#include <MQEngine/runtime/core/Engine.h>
#include <MQEngine/runtime/interface/Application.h>
#include <MQEngine/resource/core/resource.h>
#include <MQEngine/resource/core/ResourceLoader.h>
#include <MQEngine/resource/core/DiskResourceLoader.h>
#include <MQEngine/resource/core/GpuMemoryManager.h>
#include <MQEngine/host/logic/LogicHost.h>
#include <MQEngine/host/render/RenderHost.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace MQEngine
{
    namespace Asset
    {
        namespace
        {
            inline constexpr const char* kNativePluginName = "Native";
            inline constexpr const char* kCreateNativeSymbolName = "CreateNativeGameModule";
            inline constexpr const char* kGetNativeNameSymbolName = "GetNativeGameModuleName";
            inline constexpr const char* kGetNativeDepsSymbolName = "GetNativeGameModuleDependencies";
            inline constexpr const char* kGetNativeTagsSymbolName = "GetNativeGameModuleRequiredTags";

            std::string normalizeNativePathKey(const std::string& path)
            {
                if (path.empty())
                {
                    return "";
                }
                std::error_code ec;
                std::string normalized = std::filesystem::absolute(path, ec).lexically_normal().generic_string();
                while (normalized.size() > 3 && normalized.back() == '/')
                {
                    normalized.pop_back();
                }
                return normalized;
            }

            bool isNativeModuleBinaryPath(const ResourceFile* entry)
            {
                if (!entry || !SAI::FileSystem::isFile(entry->path()))
                {
                    return false;
                }
                std::string ext = entry->extension();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
#ifdef _WIN32
                return ext == ".dll";
#else
                return ext == ".so";
#endif
            }

            std::vector<std::string> parseCommaSeparatedTokens(const char* rawList)
            {
                std::vector<std::string> tokens;
                if (!rawList || rawList[0] == '\0')
                {
                    return tokens;
                }
                std::unordered_set<std::string> seen;
                const std::string input(rawList);
                size_t start = 0;
                while (start <= input.size())
                {
                    size_t comma = input.find(',', start);
                    if (comma == std::string::npos)
                    {
                        comma = input.size();
                    }
                    size_t left = start;
                    size_t right = comma;
                    while (left < right && std::isspace(static_cast<unsigned char>(input[left])))
                    {
                        ++left;
                    }
                    while (right > left && std::isspace(static_cast<unsigned char>(input[right - 1])))
                    {
                        --right;
                    }
                    if (right > left)
                    {
                        std::string token = input.substr(left, right - left);
                        if (seen.insert(token).second)
                        {
                            tokens.push_back(std::move(token));
                        }
                    }
                    if (comma == input.size())
                    {
                        break;
                    }
                    start = comma + 1;
                }
                return tokens;
            }

            std::string resolveCompositeDllPath(
                ResourceLoader* loader,
                const std::string& pluginSubDir,
                const std::string& /*pluginName*/,
                const std::string& compositeName)
            {
                const std::string dllFileName = compositeName + ".dll";

                // 严格遵循规范路径：仅在 <PluginDir>/<CompositeName>.dll 与 <PluginDir>/Composites/<CompositeName>/<CompositeName>.dll 寻址，杜绝盲猜探测循环
                std::vector<std::string> candidates = {
                    pluginSubDir + "/" + dllFileName,
                    pluginSubDir + "/Composites/" + compositeName + "/" + dllFileName,
                };

                for (const auto& cand : candidates)
                {
                    if (loader && loader->exists(cand))
                    {
                        return std::filesystem::absolute(cand).generic_string();
                    }
                    std::error_code ec;
                    if (std::filesystem::exists(cand, ec))
                    {
                        return std::filesystem::absolute(cand, ec).generic_string();
                    }
                }
                return "";
            }

            bool matchManifestTags(const std::vector<std::string>& requiredTags, const std::vector<std::string>& activeTags)
            {
                if (requiredTags.empty())
                {
                    return true;
                }
                std::unordered_set<std::string> activeSet(activeTags.begin(), activeTags.end());
                for (const auto& tag : requiredTags)
                {
                    if (!activeSet.contains(tag))
                    {
                        return false;
                    }
                }
                return true;
            }
            ResourceLoader* resolveActiveResourceLoader() noexcept
            {
                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (auto* loader = res->getResourceLoader())
                    {
                        return loader;
                    }
                }
                static ::MQEngine::DiskResourceLoader s_fallbackLoader;
                return &s_fallbackLoader;
            }

            std::vector<CompositeManifest> sortManifestsTopologically(const std::vector<CompositeManifest>& manifests)
            {
                if (manifests.size() <= 1)
                {
                    return manifests;
                }

                std::unordered_map<std::string, size_t> indexMap;
                indexMap.reserve(manifests.size());
                for (size_t i = 0; i < manifests.size(); ++i)
                {
                    indexMap.emplace(manifests[i].name, i);
                }

                TokenGraph<std::string, size_t> graph;
                for (size_t i = 0; i < manifests.size(); ++i)
                {
                    std::vector<std::string> preds;
                    for (const auto& dep : manifests[i].dependencies)
                    {
                        if (indexMap.contains(dep))
                        {
                            preds.push_back(dep);
                        }
                    }
                    graph.addNode(manifests[i].name, i, preds);
                }

                Status sortStatus = graph.update();
                if (sortStatus.ok() && !graph.hasCycle())
                {
                    const auto order = graph.order();
                    if (order.size() == manifests.size())
                    {
                        std::vector<CompositeManifest> sorted;
                        sorted.reserve(manifests.size());
                        for (size_t idx : order)
                        {
                            sorted.push_back(manifests[idx]);
                        }
                        return sorted;
                    }
                }

                spdlog::warn("[CompositeManager] Composite DLL 清单依赖排序回退为发现顺序（可能存在环或缺失外部依赖）");
                return manifests;
            }

#ifdef _WIN32
            std::string formatWin32Error(DWORD errorCode)
            {
                wchar_t* buffer = nullptr;
                const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS;
                const DWORD len = FormatMessageW(
                    flags, nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
                std::string result;
                if (len > 0 && buffer != nullptr)
                {
                    int utf8Len = WideCharToMultiByte(
                        CP_UTF8, 0, buffer, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0)
                    {
                        result.resize(static_cast<size_t>(utf8Len));
                        WideCharToMultiByte(
                            CP_UTF8, 0, buffer, static_cast<int>(len), result.data(), utf8Len, nullptr, nullptr);
                        while (!result.empty() &&
                               (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
                        {
                            result.pop_back();
                        }
                    }
                }
                if (buffer != nullptr)
                {
                    LocalFree(buffer);
                }
                return result;
            }

            class ScopedDllSearchDirectories
            {
            public:
                ~ScopedDllSearchDirectories()
                {
                    for (auto it = m_cookies.rbegin(); it != m_cookies.rend(); ++it)
                    {
                        if (*it != nullptr)
                        {
                            RemoveDllDirectory(*it);
                        }
                    }
                }

                void addDirectory(const std::filesystem::path& dir)
                {
                    if (dir.empty())
                    {
                        return;
                    }
                    std::error_code ec;
                    const std::string key = std::filesystem::absolute(dir, ec).lexically_normal().generic_string();
                    if (!m_registered.insert(key).second)
                    {
                        return;
                    }
                    if (!std::filesystem::is_directory(dir, ec))
                    {
                        return;
                    }
                    const std::wstring wideDir = std::filesystem::absolute(dir, ec).lexically_normal().wstring();
                    if (DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(wideDir.c_str()); cookie != nullptr)
                    {
                        m_cookies.push_back(cookie);
                    }
                }

            private:
                std::unordered_set<std::string> m_registered;
                std::vector<DLL_DIRECTORY_COOKIE> m_cookies;
            };
#endif

            std::string resolveManifestDllPath(ResourceLoader* loader, const CompositeManifest& cm)
            {
                if (!cm.dllPath.empty())
                {
                    if ((loader && loader->exists(cm.dllPath)) || SAI::FileSystem::isFile(cm.dllPath))
                    {
                        return normalizeNativePathKey(cm.dllPath);
                    }
                    return "";
                }
                return resolveCompositeDllPath(loader, cm.pluginPath, cm.pluginName, cm.name);
            }

            void* openCompositeLibrary(const std::string& dllPath, std::string& outError)
            {
                outError.clear();
#ifdef _WIN32
                const std::wstring wideDllPath = std::filesystem::path(dllPath).wstring();
                constexpr DWORD kSearchFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                               LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                               LOAD_LIBRARY_SEARCH_USER_DIRS |
                                               LOAD_LIBRARY_SEARCH_SYSTEM32;
                HMODULE hMod = LoadLibraryExW(wideDllPath.c_str(), nullptr, kSearchFlags);
                if (!hMod)
                {
                    hMod = LoadLibraryW(wideDllPath.c_str());
                }
                if (!hMod)
                {
                    const DWORD err = GetLastError();
                    outError = "Win32 " + std::to_string(err) + ": " + formatWin32Error(err);
                    return nullptr;
                }
                return static_cast<void*>(hMod);
#elif !defined(__EMSCRIPTEN__)
                void* handle = dlopen(dllPath.c_str(), RTLD_NOW | RTLD_LOCAL);
                if (!handle)
                {
                    const char* err = dlerror();
                    outError = err ? err : "dlopen failed";
                }
                return handle;
#else
                (void)dllPath;
                outError = "当前平台不支持动态库加载";
                return nullptr;
#endif
            }

            void* resolveLibrarySymbol(void* handle, const char* symbolName)
            {
                if (!handle || !symbolName)
                {
                    return nullptr;
                }
#ifdef _WIN32
                return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName));
#elif !defined(__EMSCRIPTEN__)
                return dlsym(handle, symbolName);
#else
                return nullptr;
#endif
            }

            void closeCompositeLibrary(void* handle) noexcept
            {
                if (!handle)
                {
                    return;
                }
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(handle));
#elif !defined(__EMSCRIPTEN__)
                dlclose(handle);
#endif
            }

            void addDefaultEngineDllSearchDirectories(
#ifdef _WIN32
                ScopedDllSearchDirectories& dllSearchDirs
#endif
            )
            {
#ifdef _WIN32
                if (auto* res = Resource::Resource::InstancePtr())
                {
                    if (auto* cfg = res->getConfig())
                    {
                        dllSearchDirs.addDirectory(cfg->getEngineDir());
                        dllSearchDirs.addDirectory(cfg->getEnginePluginsDir());
                        dllSearchDirs.addDirectory(cfg->getProjectDir());
                        dllSearchDirs.addDirectory(cfg->getProjectPluginsDir());
                        dllSearchDirs.addDirectory(cfg->getProjectNativeDir());
                    }
                }
#endif
            }

            FCT::Context* pauseRenderSubmitAndWaitGpu(Runtime::Engine* engine) noexcept
            {
                if (!engine)
                {
                    return nullptr;
                }
                auto* renderHost = engine->host.getRenderHost();
                if (!renderHost)
                {
                    return nullptr;
                }
                auto* ctx = renderHost->getContext();
                if (!ctx)
                {
                    return nullptr;
                }
                ctx->_consumeLogicTask();
                ctx->_waitCurrentFlush();
                ctx->waitGpu();
                return ctx;
            }

            void flushGpuDeferredReleasesAndWait(FCT::Context* renderCtx) noexcept
            {
                if (auto* gpuMem = Resource::GpuMemoryManager::InstancePtr())
                {
                    gpuMem->flushAllDeferredReleases();
                }
                if (renderCtx)
                {
                    renderCtx->flushAllDeferredReleases();
                    renderCtx->waitGpu();
                }
            }
        } // namespace
        CompositeManager::CompositeManager() = default;

        CompositeManager::~CompositeManager()
        {
            term();
            if (!m_deferredUnloadHandles.empty())
            {
                closeLibraryHandles(m_deferredUnloadHandles);
                m_deferredUnloadHandles.clear();
            }
        }

        void CompositeManager::deferLibraryHandle(void* handle) noexcept
        {
            if (handle != nullptr)
            {
                m_deferredUnloadHandles.push_back(handle);
            }
        }

        std::vector<void*> CompositeManager::takeDeferredLibraryHandles() noexcept
        {
            std::vector<void*> handles = std::move(m_deferredUnloadHandles);
            m_deferredUnloadHandles.clear();
            return handles;
        }

        void CompositeManager::closeLibraryHandles(const std::vector<void*>& handles) noexcept
        {
            for (auto it = handles.rbegin(); it != handles.rend(); ++it)
            {
                closeCompositeLibrary(*it);
            }
        }

        void CompositeManager::registerInfo()
        {
        }

        Status CompositeManager::discoverDefaultPlugins()
        {
            if (auto* res = Resource::Resource::InstancePtr())
            {
                if (auto* cfg = res->getConfig())
                {
                    const std::string enginePlugins = cfg->getEnginePluginsDir();
                    const std::string projectPlugins = cfg->getProjectPluginsDir();
                    if (!enginePlugins.empty())
                    {
                        (void)discoverPlugins(enginePlugins);
                    }
                    if (!projectPlugins.empty() && projectPlugins != enginePlugins)
                    {
                        (void)discoverPlugins(projectPlugins);
                    }
                    const std::string projectNative = cfg->getProjectNativeDir();
                    if (!projectNative.empty())
                    {
                        CHECK_STATUS(discoverProjectNativeModule(projectNative));
                    }
                }
            }
            return OkStatus();
        }

#ifdef _WIN32
        namespace
        {
            /**
             * @brief 以只读镜像映射方式检查 PE DLL 是否导出指定符号（不执行 DllMain、不解析依赖 DLL，避免误加载第三方库并触发 Loader Lock 死锁）
             */
            bool peBinaryHasExportedSymbol(const std::string& utf8Path, const char* symbolName) noexcept
            {
                if (utf8Path.empty() || !symbolName || symbolName[0] == '\0')
                {
                    return false;
                }

                const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
                if (wideLen <= 1)
                {
                    return false;
                }
                std::wstring widePath(static_cast<size_t>(wideLen - 1), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, widePath.data(), wideLen);

                HMODULE imageMod = LoadLibraryExW(
                    widePath.c_str(),
                    nullptr,
                    LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
                if (!imageMod)
                {
                    return false;
                }

                struct ScopeGuard
                {
                    HMODULE mod = nullptr;
                    ~ScopeGuard()
                    {
                        if (mod)
                        {
                            FreeLibrary(mod);
                        }
                    }
                } guard{imageMod};

                const auto rawAddr = reinterpret_cast<uintptr_t>(imageMod);
                if ((rawAddr & static_cast<uintptr_t>(0x2)) == 0)
                {
                    // 未按 SEC_IMAGE 对齐映射（无 LOAD_LIBRARY_AS_IMAGE_RESOURCE 视图）时跳过 RVA 解析
                    return false;
                }
                const auto* base = reinterpret_cast<const uint8_t*>(rawAddr & ~static_cast<uintptr_t>(0x3));
                const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
                {
                    return false;
                }

                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
                if (nt->Signature != IMAGE_NT_SIGNATURE ||
                    nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
                    nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
                {
                    return false;
                }

                const uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
                const IMAGE_DATA_DIRECTORY exportDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
                if (exportDir.VirtualAddress == 0 ||
                    exportDir.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
                    exportDir.VirtualAddress + sizeof(IMAGE_EXPORT_DIRECTORY) > imageSize)
                {
                    return false;
                }

                const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportDir.VirtualAddress);
                if (exports->NumberOfNames == 0 ||
                    exports->AddressOfNames == 0 ||
                    exports->AddressOfNames + exports->NumberOfNames * sizeof(uint32_t) > imageSize)
                {
                    return false;
                }

                const auto* nameRvas = reinterpret_cast<const uint32_t*>(base + exports->AddressOfNames);
                for (uint32_t i = 0; i < exports->NumberOfNames; ++i)
                {
                    const uint32_t nameRva = nameRvas[i];
                    if (nameRva == 0 || nameRva >= imageSize)
                    {
                        continue;
                    }
                    const char* exportedName = reinterpret_cast<const char*>(base + nameRva);
                    if (std::strcmp(exportedName, symbolName) == 0)
                    {
                        return true;
                    }
                }
                return false;
            }
        } // namespace
#endif

        Status CompositeManager::discoverProjectNativeModule(const std::string& nativeDir)
        {
            ZoneScoped();
            if (nativeDir.empty())
            {
                return OkStatus();
            }

            auto* loader = resolveActiveResourceLoader();
            if (!loader || !loader->accessDir(nativeDir).ok())
            {
                return OkStatus();
            }

            registerNativeDllSearchDirectory(nativeDir);
#ifdef _WIN32
            ScopedDllSearchDirectories probeSearchDirs;
            addDefaultEngineDllSearchDirectories(probeSearchDirs);
            probeSearchDirs.addDirectory(nativeDir);
#endif

            auto dirResult = loader->readDir(nativeDir);
            if (!dirResult.ok())
            {
                return dirResult.status();
            }

            const std::string normalizedNativeDir = normalizeNativePathKey(nativeDir);
            for (const auto& entry : dirResult.value())
            {
                if (!isNativeModuleBinaryPath(entry.get()))
                {
                    continue;
                }

                const std::string absDllPath = normalizeNativePathKey(entry->path());
#ifdef _WIN32
                if (!peBinaryHasExportedSymbol(absDllPath, kCreateNativeSymbolName))
                {
                    continue;
                }
#endif
                std::string loadError;
                void* handle = openCompositeLibrary(absDllPath, loadError);
                if (!handle)
                {
                    spdlog::warn("[CompositeManager] 探测 Native 模块 DLL 失败: '{}' ({})", absDllPath, loadError);
                    continue;
                }

                auto* createSym = resolveLibrarySymbol(handle, kCreateNativeSymbolName);
                if (!createSym)
                {
                    closeCompositeLibrary(handle);
                    continue;
                }

                std::string moduleName = entry->stem();
                using GetNativeNameFn = const char* (*)();
                if (auto nameFn = reinterpret_cast<GetNativeNameFn>(
                        resolveLibrarySymbol(handle, kGetNativeNameSymbolName)))
                {
                    if (const char* customName = nameFn(); customName && customName[0] != '\0')
                    {
                        moduleName = customName;
                    }
                }

                std::vector<std::string> deps;
                using GetNativeDepsFn = const char* (*)();
                if (auto depsFn = reinterpret_cast<GetNativeDepsFn>(
                        resolveLibrarySymbol(handle, kGetNativeDepsSymbolName)))
                {
                    deps = parseCommaSeparatedTokens(depsFn());
                }

                std::vector<std::string> requiredTags = {"Runtime", "Game"};
                using GetNativeTagsFn = const char* (*)();
                if (auto tagsFn = reinterpret_cast<GetNativeTagsFn>(
                        resolveLibrarySymbol(handle, kGetNativeTagsSymbolName)))
                {
                    std::vector<std::string> parsedTags = parseCommaSeparatedTokens(tagsFn());
                    if (!parsedTags.empty())
                    {
                        requiredTags = std::move(parsedTags);
                    }
                }

                // 拷贝完模块名、依赖与 Tags 字符串后立刻卸载探测句柄，避免无匹配时锁死文件与泄漏句柄
                closeCompositeLibrary(handle);

                CompositeManifest manifest;
                manifest.name = moduleName;
                manifest.pluginName = kNativePluginName;
                manifest.pluginPath = normalizedNativeDir;
                manifest.dllPath = absDllPath;
                manifest.entrySymbolName = kCreateNativeSymbolName;
                manifest.tags = std::move(requiredTags);
                manifest.dependencies = std::move(deps);
                manifest.description = "工程 Native 主游戏模块";
                m_compositeManifests.insert_or_assign(manifest.name, std::move(manifest));
            }

            return OkStatus();
        }

        Status CompositeManager::activateApplicationComposites()
        {
            auto* engine = Runtime::Engine::getInstance();
            std::vector<std::string> activeTags{"Runtime"};
            if (engine)
            {
                if (!engine->getActiveApplicationTags().empty())
                {
                    activeTags = engine->getActiveApplicationTags();
                }
                else if (engine->application && !engine->application->applicationRegisterInfo.tags.empty())
                {
                    activeTags = engine->application->applicationRegisterInfo.tags;
                }
            }
            return reconcileActiveTags(activeTags);
        }

        void CompositeManager::registerLogicTicker()
        {
            if (auto* logicHost = Host::LogicHost::InstancePtr())
            {
                logicHost->tickers().addNode(
                    "Asset_Composite_Tick",
                    [this, lastTick = std::chrono::steady_clock::now(), firstTick = true]() mutable {
                        const auto now = std::chrono::steady_clock::now();
                        float realDt = firstTick
                            ? 0.016667f
                            : std::chrono::duration<float>(now - lastTick).count();
                        firstTick = false;
                        lastTick = now;
                        realDt = std::clamp(realDt, 0.0001f, 0.25f);

                        tick(realDt);
                        if (auto* eng = Runtime::Engine::getInstance(); eng && eng->application)
                        {
                            eng->application->logicTick();
                        }
                    },
                    {"Domain_Scene_Update"},
                    {});
            }
        }

        void CompositeManager::unregisterLogicTicker() noexcept
        {
            if (auto* logicHost = Host::LogicHost::InstancePtr())
            {
                logicHost->tickers().removeNode("Asset_Composite_Tick");
            }
        }

        Status CompositeManager::init()
        {
            CHECK_STATUS(discoverDefaultPlugins());
            CHECK_STATUS(activateApplicationComposites());
            registerLogicTicker();
            m_initialized = true;
            return OkStatus();
        }

        void CompositeManager::term() noexcept
        {
            if (m_initialized)
            {
                unregisterLogicTicker();
                unloadComposites();
                std::vector<std::string> dynamicNames;
                dynamicNames.reserve(m_allComposites.size());
                for (const auto& [name, comp] : m_allComposites)
                {
                    if (comp && comp->getModuleHandle())
                    {
                        dynamicNames.push_back(name);
                    }
                }
                releaseDynamicCompositesAndLibraries(dynamicNames, m_engine);
                m_allComposites.clear();
                m_compositeManifests.clear();
                m_plugins.clear();
                clearNativeDllSearchDirectories();
                m_initialized = false;
            }
        }

        Status CompositeManager::registerComposite(UniquePtr<Composite> composite)
        {
            assert(composite != nullptr && "不能注册空 Composite 指针");
            const std::string name = composite->getName();
            assert(!name.empty() && "Composite 名称不可为空");

            if (m_allComposites.contains(name))
            {
                return AlreadyExistsError(StrCat("Composite 已经注册: ", name));
            }

            m_allComposites.emplace(name, std::move(composite));
            return OkStatus();
        }

        Status CompositeManager::registerPluginPackage(PluginPackage plugin)
        {
            const std::string name = plugin.getName();
            assert(!name.empty() && "PluginPackage 名称不可为空");

            // 提取插件包下声明的 Composite 实例并注册
            auto composites = plugin.createComposites();
            for (auto& comp : composites)
            {
                CHECK_STATUS(registerComposite(std::move(comp)));
            }

            m_plugins.insert_or_assign(name, std::move(plugin));
            return OkStatus();
        }

        Status CompositeManager::discoverPlugins(const std::string& pluginsDir)
        {
            ZoneScoped();
            auto* loader = resolveActiveResourceLoader();
            if (!loader || !loader->exists(pluginsDir))
            {
                return OkStatus();
            }

            auto dirResult = loader->readDir(pluginsDir);
            if (!dirResult.ok())
            {
                return dirResult.status();
            }

            for (const auto& entry : dirResult.value())
            {
                std::string subDirPath = entry->path();
                std::string metaPath = subDirPath + "/plugin.mq";
                if (!loader->exists(metaPath))
                {
                    metaPath = subDirPath + "/plugin.json";
                }

                if (loader->exists(metaPath))
                {
                    auto contentResult = loader->loadFile(metaPath);
                    if (contentResult.ok())
                    {
                        auto metaResult = PluginPackage::parseMetadata(contentResult.value());
                        if (metaResult.ok())
                        {
                            auto metadata = metaResult.value();
                            metadata.path = subDirPath;
                            for (auto& cm : metadata.compositeManifests)
                            {
                                cm.pluginPath = subDirPath;
                                if (!m_compositeManifests.contains(cm.name))
                                {
                                    m_compositeManifests.emplace(cm.name, cm);
                                }
                            }
                            if (!m_plugins.contains(metadata.name))
                            {
                                PluginPackage pkg(metadata);
                                (void)registerPluginPackage(std::move(pkg));
                            }
                        }
                        else
                        {
                            spdlog::warn("[CompositeManager] 解析插件清单失败 [{}]: {}",
                                         metaPath, std::string(metaResult.status().message()));
                        }
                    }
                    else
                    {
                        spdlog::warn("[CompositeManager] 读取插件清单失败 [{}]: {}",
                                     metaPath, std::string(contentResult.status().message()));
                    }
                }
            }

            return OkStatus();
        }

        bool CompositeManager::matchesCompositeTags(const std::string& name,
                                                    const Composite* comp,
                                                    const std::vector<std::string>& activeTags) const
        {
            if (!comp || !comp->matchTags(activeTags))
            {
                return false;
            }
            if (auto it = m_compositeManifests.find(name); it != m_compositeManifests.end())
            {
                return matchManifestTags(it->second.tags, activeTags);
            }
            return true;
        }

        std::vector<std::string> CompositeManager::collectCompositeDependencies(const std::string& name,
                                                                                const Composite* comp) const
        {
            std::vector<std::string> deps;
            std::unordered_set<std::string> seen;
            if (comp)
            {
                for (const auto& dep : comp->getDependencies())
                {
                    if (!dep.empty() && seen.insert(dep).second)
                    {
                        deps.push_back(dep);
                    }
                }
            }
            if (auto it = m_compositeManifests.find(name); it != m_compositeManifests.end())
            {
                for (const auto& dep : it->second.dependencies)
                {
                    if (!dep.empty() && seen.insert(dep).second)
                    {
                        deps.push_back(dep);
                    }
                }
            }
            return deps;
        }

        Status CompositeManager::loadMatchedCompositeDlls(const std::vector<std::string>& activeTags,
                                                          std::vector<std::string>* outNewlyLoadedNames)
        {
            ZoneScoped();
            if (m_compositeManifests.empty())
            {
                return OkStatus();
            }

            auto* loader = resolveActiveResourceLoader();
            if (!loader)
            {
                return OkStatus();
            }

            // 1. 仅筛选匹配当前 activeTags 的未装载 Composite 清单（物理级按需加载 DLL）
            std::vector<CompositeManifest> matchedManifests;
            for (const auto& [compName, cm] : m_compositeManifests)
            {
                if (m_allComposites.contains(compName))
                {
                    continue;
                }
                if (matchManifestTags(cm.tags, activeTags))
                {
                    matchedManifests.push_back(cm);
                }
            }

            if (matchedManifests.empty())
            {
                return OkStatus();
            }

            // 2. 按依赖关系执行确定性拓扑排序，确保被依赖的上游 Composite DLL 先于下游 DLL 加载
            matchedManifests = sortManifestsTopologically(matchedManifests);

#ifdef _WIN32
            ScopedDllSearchDirectories dllSearchDirs;
            addDefaultEngineDllSearchDirectories(dllSearchDirs);
#endif

            std::vector<std::pair<CompositeManifest, std::string>> resolvedEntries;
            resolvedEntries.reserve(matchedManifests.size());
            for (const auto& cm : matchedManifests)
            {
                std::string dllPath = resolveManifestDllPath(loader, cm);
                if (dllPath.empty())
                {
                    if (cm.pluginName == kNativePluginName)
                    {
                        return NotFoundError(StrCat("未找到 Native 模块 DLL [", cm.name, "]: ", cm.dllPath));
                    }
                    spdlog::warn("[CompositeManager] 未找到匹配 Composite [{}] 的 DLL（插件目录: {}）",
                                 cm.name, cm.pluginPath);
                    continue;
                }
#ifdef _WIN32
                dllSearchDirs.addDirectory(std::filesystem::path(dllPath).parent_path());
                if (!cm.pluginPath.empty())
                {
                    dllSearchDirs.addDirectory(cm.pluginPath);
                }
#endif
                resolvedEntries.emplace_back(cm, std::move(dllPath));
            }

            // 3. 逐个加载匹配的动态库并实例化 Composite
            for (const auto& [cm, dllPath] : resolvedEntries)
            {
                std::string loadError;
                void* handle = openCompositeLibrary(dllPath, loadError);
                if (!handle)
                {
                    if (cm.pluginName == kNativePluginName)
                    {
                        return InternalError(StrCat("加载 Native 模块 DLL 失败 [", cm.name, "]: ", loadError));
                    }
                    spdlog::error("[CompositeManager] 加载插件 DLL 失败 [{}] -> '{}' ({})",
                                  cm.name, dllPath, loadError);
                    continue;
                }

                const char* symbolName = cm.entrySymbolName.empty()
                    ? kCreateCompositeSymbolName
                    : cm.entrySymbolName.c_str();
                auto createFn = reinterpret_cast<CreateCompositeFn>(
                    resolveLibrarySymbol(handle, symbolName));
                if (!createFn)
                {
                    closeCompositeLibrary(handle);
                    if (cm.pluginName == kNativePluginName)
                    {
                        return NotFoundError(StrCat("Native 模块 DLL 缺少导出符号 '", symbolName, "': ", dllPath));
                    }
                    spdlog::error("[CompositeManager] 插件 DLL 缺少导出符号 '{}' [{}]: '{}'",
                                  symbolName, cm.name, dllPath);
                    continue;
                }

                Composite* rawComp = createFn();
                if (!rawComp)
                {
                    closeCompositeLibrary(handle);
                    if (cm.pluginName == kNativePluginName)
                    {
                        return InternalError(StrCat("Native 模块工厂返回空指针: ", cm.name));
                    }
                    continue;
                }
                rawComp->setModuleHandle(handle);

                const std::string actualName = rawComp->getName();
                if (!actualName.empty() && actualName != cm.name && !m_compositeManifests.contains(actualName))
                {
                    m_compositeManifests.emplace(actualName, cm);
                }

                Status regSt = registerComposite(UniquePtr<Composite>(rawComp));
                if (!regSt.ok())
                {
                    closeCompositeLibrary(handle);
                    if (cm.pluginName == kNativePluginName)
                    {
                        return regSt;
                    }
                    spdlog::error("[CompositeManager] 注册 Composite [{}] 失败: {}",
                                  cm.name, std::string(regSt.message()));
                    continue;
                }

                if (outNewlyLoadedNames && !actualName.empty())
                {
                    outNewlyLoadedNames->push_back(actualName);
                }
            }
            return OkStatus();
        }

        Status CompositeManager::buildTopologicalSchedule(const std::vector<std::string>& activeTags)
        {
            ZoneScoped();
            m_cachedActiveTags = activeTags;
            clearTopologicalOrder();

            // 1. 过滤符合当前 activeTags 的 Composite
            std::unordered_map<std::string, Composite*> candidates;
            for (const auto& [name, comp] : m_allComposites)
            {
                if (matchesCompositeTags(name, comp.get(), activeTags))
                {
                    candidates[name] = comp.get();
                }
            }

            if (candidates.empty())
            {
                return OkStatus();
            }

            // 2. 检查依赖完整性并向 TokenGraph 填入节点
            for (const auto& [name, comp] : candidates)
            {
                const std::vector<std::string> deps = collectCompositeDependencies(name, comp);
                TokenGraph<std::string, Composite*>::NodeInfo nodeInfo;
                nodeInfo.value = comp;

                for (const auto& dep : deps)
                {
                    if (!candidates.contains(dep))
                    {
                        clearTopologicalOrder();
                        return NotFoundError(StrCat(
                            "Composite '", name, "' 依赖的前置项 '", dep,
                            "' 在当前 Application tags 激活集合中不存在或未注册！"));
                    }
                    nodeInfo.predecessors.push_back(dep);
                }

                m_tokenGraph.addNode(name, comp, nodeInfo.predecessors);
            }

            // 3. 执行拓扑排序与环检测
            Status updateStatus = m_tokenGraph.update();
            if (!updateStatus.ok() || m_tokenGraph.hasCycle())
            {
                clearTopologicalOrder();
                return FailedPreconditionError("Composite 拓扑图中存在循环依赖 (Cyclic Dependency)！");
            }

            // 4. 获取 Kahn 拓扑排序结果并保存
            auto sortedVector = m_tokenGraph.order();
            m_executionOrder.reserve(sortedVector.size());
            for (auto* comp : sortedVector)
            {
                if (comp != nullptr)
                {
                    m_executionOrder.push_back(comp);
                }
            }

            return OkStatus();
        }

        Status CompositeManager::loadComposites(Runtime::Engine* engine, const std::vector<std::string>& activeTags)
        {
            ZoneScoped();
            m_engine = engine;

            CHECK_STATUS(loadMatchedCompositeDlls(activeTags));
            CHECK_STATUS(buildTopologicalSchedule(activeTags));

            // 正序调用 onLoad
            for (Composite* comp : m_executionOrder)
            {
                assert(comp != nullptr);
                if (!comp->isLoaded())
                {
                    CHECK_STATUS(comp->onLoad(m_engine));
                }
            }

            return OkStatus();
        }

        Status CompositeManager::initComposites(Runtime::Engine* engine)
        {
            ZoneScoped();
            m_engine = engine;

            // 正序调用 onInit
            for (Composite* comp : m_executionOrder)
            {
                assert(comp != nullptr);
                if (!comp->isInitialized())
                {
                    CHECK_STATUS(comp->onInit(m_engine));
                }
            }

            return OkStatus();
        }

        void CompositeManager::tick(float deltaTime)
        {
            ZoneScoped();
            // 快照拓扑名称列表，允许单个 Composite 的 onTick 内安全触发 Application Tag 动态增删
            m_tickNameScratch.clear();
            m_tickNameScratch.reserve(m_executionOrder.size());
            for (const Composite* comp : m_executionOrder)
            {
                if (comp)
                {
                    m_tickNameScratch.push_back(comp->getName());
                }
            }

            const size_t count = m_tickNameScratch.size();
            for (size_t i = 0; i < count; ++i)
            {
                Composite* comp = getComposite(m_tickNameScratch[i]);
                if (comp && comp->isInitialized())
                {
                    comp->onTick(deltaTime);
                }
            }
        }

        void CompositeManager::unloadComposites()
        {
            // 拷贝快照防止某 Composite 的 onUnload 触发二次变更导致迭代器失效
            const std::vector<Composite*> orderSnapshot = m_executionOrder;
            for (auto it = orderSnapshot.rbegin(); it != orderSnapshot.rend(); ++it)
            {
                Composite* comp = *it;
                if (comp && comp->isLoaded())
                {
                    comp->onUnload();
                }
            }
            clearTopologicalOrder();
        }

        void CompositeManager::releaseDynamicCompositesAndLibraries(const std::vector<std::string>& names,
                                                                     Runtime::Engine* engine) noexcept
        {
            std::vector<void*> handlesToDefer;
            for (const std::string& name : names)
            {
                auto it = m_allComposites.find(name);
                if (it == m_allComposites.end())
                {
                    continue;
                }
                Composite* comp = it->second.get();
                if (comp)
                {
                    std::erase(m_executionOrder, comp);
                    if (void* handle = comp->getModuleHandle())
                    {
                        handlesToDefer.push_back(handle);
                    }
                }
                m_tokenGraph.removeNode(name);
                m_allComposites.erase(it);
            }

            if (handlesToDefer.empty())
            {
                return;
            }

            FCT::Context* renderCtx = pauseRenderSubmitAndWaitGpu(engine);
            flushGpuDeferredReleasesAndWait(renderCtx);
            for (void* handle : handlesToDefer)
            {
                deferLibraryHandle(handle);
            }
            if (renderCtx)
            {
                renderCtx->_currentFlush();
            }
        }

        void CompositeManager::deactivateStaleComposites(const std::vector<std::string>& activeTags,
                                                         Runtime::Engine* engine) noexcept
        {
            std::vector<Composite*> staleReverse;
            for (auto it = m_executionOrder.rbegin(); it != m_executionOrder.rend(); ++it)
            {
                Composite* comp = *it;
                if (comp && !matchesCompositeTags(comp->getName(), comp, activeTags))
                {
                    staleReverse.push_back(comp);
                }
            }

            if (staleReverse.empty())
            {
                return;
            }

            FCT::Context* renderCtx = pauseRenderSubmitAndWaitGpu(engine);
            std::vector<std::string> dynamicNamesToDestroy;
            std::vector<void*> handlesToDefer;

            for (Composite* comp : staleReverse)
            {
                const std::string compName = comp->getName();
                void* handle = comp->getModuleHandle();
                std::erase(m_executionOrder, comp);
                if (comp->isLoaded())
                {
                    comp->onUnload();
                }
                if (handle != nullptr)
                {
                    dynamicNamesToDestroy.push_back(compName);
                    handlesToDefer.push_back(handle);
                }
            }

            // 必须先析构已卸载 DLL 内的 Composite 对象并排空 GPU 延迟释放队列；
            // 原生句柄保留至 Runtime::~Engine 完全销毁后再 FreeLibrary，防止 PIE 停止后 Scene/Host 仍持有虚表指针
            for (const std::string& name : dynamicNamesToDestroy)
            {
                m_tokenGraph.removeNode(name);
                m_allComposites.erase(name);
            }

            flushGpuDeferredReleasesAndWait(renderCtx);
            for (void* handle : handlesToDefer)
            {
                deferLibraryHandle(handle);
            }

            // 恢复 m_currentFlush 标志位使 LogicHost 结束后紧随的 RenderHost::sync() 不会死锁等待上一帧完成
            if (renderCtx)
            {
                renderCtx->_currentFlush();
            }
        }

        Status CompositeManager::reconcileActiveTags(const std::vector<std::string>& activeTags)
        {
            ZoneScoped();
            Runtime::Engine* engine = m_engine ? m_engine : Runtime::Engine::getInstance();
            m_engine = engine;
            const std::vector<std::string> previousTags = m_cachedActiveTags;

            // 1. 停用不再匹配当前 activeTags 的组件（严格按拓扑逆序卸载并卸载对应 DLL）
            deactivateStaleComposites(activeTags, engine);

            // 记录此前已活跃的组件，增量启用阶段绝不对其重复触发 onLoad/onInit
            std::unordered_set<std::string> alreadyActive;
            for (const Composite* comp : m_executionOrder)
            {
                if (comp && comp->isLoaded() && comp->isInitialized())
                {
                    alreadyActive.insert(comp->getName());
                }
            }

            // 2. 按需装载新增匹配的动态库 Composite
            std::vector<std::string> newlyLoadedNames;
            Status dllSt = loadMatchedCompositeDlls(activeTags, &newlyLoadedNames);
            if (!dllSt.ok())
            {
                std::vector<std::string> reverseLoaded(newlyLoadedNames.rbegin(), newlyLoadedNames.rend());
                releaseDynamicCompositesAndLibraries(reverseLoaded, engine);
                (void)buildTopologicalSchedule(activeTags);
                return dllSt;
            }

            // 3. 基于全量匹配集合重建 TokenGraph 拓扑执行序
            Status schedSt = buildTopologicalSchedule(activeTags);
            if (!schedSt.ok())
            {
                std::vector<std::string> reverseLoaded(newlyLoadedNames.rbegin(), newlyLoadedNames.rend());
                releaseDynamicCompositesAndLibraries(reverseLoaded, engine);
                return schedSt;
            }

            // 4. 仅对本次新激活（尚未加载或未初始化）的组件按拓扑正序执行 onLoad -> onInit
            std::vector<Composite*> newlyActivated;
            for (Composite* comp : m_executionOrder)
            {
                if (!comp)
                {
                    continue;
                }
                if (alreadyActive.contains(comp->getName()))
                {
                    continue;
                }
                newlyActivated.push_back(comp);
            }

            auto rollbackNewlyActivated = [this, &newlyActivated, &newlyLoadedNames, &previousTags, engine]() {
                for (auto it = newlyActivated.rbegin(); it != newlyActivated.rend(); ++it)
                {
                    if (*it && (*it)->isLoaded())
                    {
                        (*it)->onUnload();
                    }
                }
                std::vector<std::string> reverseLoaded(newlyLoadedNames.rbegin(), newlyLoadedNames.rend());
                releaseDynamicCompositesAndLibraries(reverseLoaded, engine);
                if (!previousTags.empty())
                {
                    (void)buildTopologicalSchedule(previousTags);
                }
            };

            for (Composite* comp : newlyActivated)
            {
                if (!comp->isLoaded())
                {
                    Status loadSt = comp->onLoad(engine);
                    if (!loadSt.ok())
                    {
                        rollbackNewlyActivated();
                        return loadSt;
                    }
                }
            }

            for (Composite* comp : newlyActivated)
            {
                if (!comp->isInitialized())
                {
                    Status initSt = comp->onInit(engine);
                    if (!initSt.ok())
                    {
                        rollbackNewlyActivated();
                        return initSt;
                    }
                }
            }

            return OkStatus();
        }

        void CompositeManager::registerNativeDllSearchDirectory(const std::string& dir)
        {
#ifdef _WIN32
            const std::string key = normalizeNativePathKey(dir);
            if (key.empty())
            {
                return;
            }
            if (std::find(m_registeredNativeDllDirKeys.begin(), m_registeredNativeDllDirKeys.end(), key) !=
                m_registeredNativeDllDirKeys.end())
            {
                return;
            }
            std::error_code ec;
            if (!std::filesystem::is_directory(key, ec))
            {
                return;
            }
            const std::wstring wideDir = std::filesystem::path(key).wstring();
            if (DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(wideDir.c_str()); cookie != nullptr)
            {
                m_registeredNativeDllDirKeys.push_back(key);
                m_nativeDllDirCookies.push_back(static_cast<void*>(cookie));
            }
#else
            (void)dir;
#endif
        }

        void CompositeManager::clearNativeDllSearchDirectories() noexcept
        {
#ifdef _WIN32
            for (auto it = m_nativeDllDirCookies.rbegin(); it != m_nativeDllDirCookies.rend(); ++it)
            {
                if (*it != nullptr)
                {
                    RemoveDllDirectory(static_cast<DLL_DIRECTORY_COOKIE>(*it));
                }
            }
#endif
            m_nativeDllDirCookies.clear();
            m_registeredNativeDllDirKeys.clear();
        }

        Status CompositeManager::reloadComposite(const std::string& name)
        {
            Composite* comp = getComposite(name);
            if (!comp)
            {
                return NotFoundError(StrCat("未找到待重载的 Composite: ", name));
            }

            if (comp->isLoaded())
            {
                comp->onUnload();
            }

            if (m_engine)
            {
                CHECK_STATUS(comp->onLoad(m_engine));
                CHECK_STATUS(comp->onInit(m_engine));
            }

            return OkStatus();
        }

        Composite* CompositeManager::getComposite(const std::string& name) const
        {
            auto it = m_allComposites.find(name);
            if (it != m_allComposites.end())
            {
                return it->second.get();
            }
            return nullptr;
        }

        void CompositeManager::clearTopologicalOrder()
        {
            m_executionOrder.clear();
            for (const auto& [name, comp] : m_allComposites)
            {
                m_tokenGraph.removeNode(name);
            }
        }

    } // namespace Asset
} // namespace MQEngine
