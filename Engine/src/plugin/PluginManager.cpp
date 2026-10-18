#include "PluginManager.h"
#include "../core/EngineGlobal.h"
#include "../data/DiskResourceLoader.h"
#include "../data/ResourceLoader.h"
#include "../system/ScriptSystem.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine {

using absl::AlreadyExistsError;
using absl::FailedPreconditionError;

using CreatePluginFunc = IPlugin* (*)();

namespace {

struct ScriptPluginManifestInfo {
    std::string name;
    std::string version = "1.0.0";
    std::string mainScript;
    std::string pluginDirectory;
    std::string resolvedEntryPath;
};

std::string NormalizeScriptFsPath(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    const std::filesystem::path& target = ec ? p : canonical;
    return std::string(reinterpret_cast<const char*>(target.generic_u8string().c_str()));
}

std::string ReadJsonStringField(const boost::json::object& obj, const char* key, const std::string& defaultVal = "") {
    auto it = obj.find(key);
    if (it != obj.end() && it->value().is_string()) {
        return std::string(it->value().as_string().c_str());
    }
    return defaultVal;
}

StatusOr<ScriptPluginManifestInfo> ResolveScriptPluginManifest(const std::string& pluginPath, ResourceLoader* resourceLoader) {
    namespace fs = std::filesystem;
    std::unique_ptr<DiskResourceLoader> fallbackLoader;
    if (!resourceLoader) {
        fallbackLoader = std::make_unique<DiskResourceLoader>();
        resourceLoader = fallbackLoader.get();
    }

    std::error_code ec;
    fs::path p = fs::u8path(pluginPath);
    ScriptPluginManifestInfo manifest;

    if (fs::is_directory(p, ec)) {
        manifest.pluginDirectory = NormalizeScriptFsPath(p);
        manifest.name = std::string(reinterpret_cast<const char*>(p.filename().generic_u8string().c_str()));

        fs::path manifestJsonPath = p / "plugin.json";
        if (fs::exists(manifestJsonPath, ec)) {
            std::string jsonPathStr = NormalizeScriptFsPath(manifestJsonPath);
            auto jsonContentRes = resourceLoader->loadFile(jsonPathStr);
            CHECK_STATUS(jsonContentRes);

            boost::system::error_code jec;
            auto jv = boost::json::parse(jsonContentRes.value(), jec);
            if (!jec && jv.is_object()) {
                const auto& obj = jv.as_object();
                std::string parsedName = ReadJsonStringField(obj, "name");
                if (!parsedName.empty()) {
                    manifest.name = parsedName;
                }
                manifest.version = ReadJsonStringField(obj, "version", "1.0.0");
                manifest.mainScript = ReadJsonStringField(obj, "main");
            }
        }

        if (manifest.mainScript.empty()) {
            if (fs::exists(p / "index.ts", ec)) {
                manifest.mainScript = "index.ts";
            } else if (fs::exists(p / "index.js", ec)) {
                manifest.mainScript = "index.js";
            } else {
                return NotFoundError(StrCat("脚本插件目录缺少入口文件 (index.ts / index.js / plugin.json): ", pluginPath));
            }
        }

        manifest.resolvedEntryPath = NormalizeScriptFsPath(p / fs::u8path(manifest.mainScript));
        return manifest;
    }

    if (fs::exists(p, ec) && p.filename() == "plugin.json") {
        return ResolveScriptPluginManifest(NormalizeScriptFsPath(p.parent_path()), resourceLoader);
    }

    if (!fs::exists(p, ec)) {
        return NotFoundError(StrCat("未找到脚本插件文件: ", pluginPath));
    }

    manifest.resolvedEntryPath = NormalizeScriptFsPath(p);
    manifest.pluginDirectory = NormalizeScriptFsPath(p.parent_path());
    manifest.mainScript = std::string(reinterpret_cast<const char*>(p.filename().generic_u8string().c_str()));
    manifest.name = std::string(reinterpret_cast<const char*>(p.stem().generic_u8string().c_str()));
    return manifest;
}

/**
 * @brief 内部脚本插件适配器：将使用 TS / JS 编写的插件通过引擎核心 ScriptSystem 接入 IPlugin 生命周期
 */
class ScriptPluginAdapter : public IPlugin {
public:
    explicit ScriptPluginAdapter(const ScriptPluginManifestInfo& manifest)
        : m_manifest(manifest) {}

    ~ScriptPluginAdapter() override {
        if (m_isLoaded) {
            onUnload();
        }
    }

    std::string getName() const override { return m_manifest.name; }
    std::string getVersion() const override { return m_manifest.version; }

    Status onLoad(PluginContext& ctx) override {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("加载脚本插件 [", m_manifest.name, "] 失败: 引擎核心 ScriptSystem 未初始化"));
        }

        CHECK_STATUS(ss->executePluginScript(
            m_manifest.name,
            m_manifest.version,
            m_manifest.resolvedEntryPath,
            m_manifest.pluginDirectory,
            ctx.resourceLoader));

        m_isLoaded = true;
        return ss->invokePluginLifecycle(m_manifest.name, "onLoad");
    }

    Status onInit(PluginContext& /*ctx*/) override {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("初始化脚本插件 [", m_manifest.name, "] 失败: ScriptSystem 未就绪"));
        }
        return ss->invokePluginLifecycle(m_manifest.name, "onInit");
    }

    void onLogicTick(float dt) override {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        std::string dtJson = "{\"dt\":" + std::to_string(dt) + "}";
        (void)ss->invokePluginLifecycle(m_manifest.name, "onLogicTick", dtJson);
        if (ss->getNodeEnvironment()) {
            try {
                ss->getNodeEnvironment()->tick();
            } catch (...) {
            }
        }
    }

    void onRenderTick() override {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        (void)ss->invokePluginLifecycle(m_manifest.name, "onRenderTick");
    }

    void onUnload() override {
        if (!m_isLoaded) {
            return;
        }
        if (g_engineGlobal.scriptSystem) {
            g_engineGlobal.scriptSystem->unloadPluginScript(m_manifest.name);
        }
        m_isLoaded = false;
    }

private:
    ScriptPluginManifestInfo m_manifest;
    bool m_isLoaded = false;
};

} // namespace

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    unloadAll();
}

Status PluginManager::registerPlugin(std::shared_ptr<IPlugin> plugin) {
    if (!plugin) {
        return InvalidArgumentError("registerPlugin: 插件指针为空");
    }

    std::string name = plugin->getName();
    if (name.empty()) {
        return InvalidArgumentError("registerPlugin: 插件名称为空");
    }

    if (m_plugins.find(name) != m_plugins.end()) {
        return AlreadyExistsError(StrCat("插件已存在: ", name));
    }

    PluginEntry entry;
    entry.name = name;
    entry.plugin = plugin;
    entry.state = PluginState::Unloaded;
    m_plugins.emplace(name, std::move(entry));

    return OkStatus();
}

Status PluginManager::registerPluginFactory(const std::string& name, PluginFactory factory) {
    if (name.empty()) {
        return InvalidArgumentError("registerPluginFactory: 插件名称为空");
    }
    if (!factory) {
        return InvalidArgumentError("registerPluginFactory: 工厂函数为空");
    }

    m_factories[name] = std::move(factory);
    return OkStatus();
}

Status PluginManager::ensurePluginInstance(const std::string& name) {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end() && it->second.plugin) {
        return OkStatus();
    }

    auto factoryIt = m_factories.find(name);
    if (factoryIt != m_factories.end()) {
        auto instance = factoryIt->second();
        if (!instance) {
            return InternalError(StrCat("插件工厂创建插件失败: ", name));
        }
        PluginEntry entry;
        entry.name = name;
        entry.plugin = instance;
        entry.state = PluginState::Unloaded;
        m_plugins[name] = std::move(entry);
        return OkStatus();
    }

    return NotFoundError(StrCat("未找到已注册插件或插件工厂: ", name));
}

Status PluginManager::loadPlugin(const std::string& name, PluginContext& ctx) {
    auto ensureRes = ensurePluginInstance(name);
    if (!ensureRes.ok()) {
        return ensureRes;
    }

    auto& entry = m_plugins[name];
    if (entry.state == PluginState::Loaded || entry.state == PluginState::Initialized) {
        return OkStatus();
    }

    Status status = entry.plugin->onLoad(ctx);
    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onLoad 失败: {}", name, status.message());
        return status;
    }

    entry.state = PluginState::Loaded;
    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), name) == m_loadOrder.end()) {
        m_loadOrder.push_back(name);
    }

    spdlog::info("插件 [{}] 成功加载 (版本: {})", name, entry.plugin->getVersion());
    return OkStatus();
}

Status PluginManager::initPlugin(const std::string& name, PluginContext& ctx) {
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }

    auto& entry = it->second;
    if (entry.state == PluginState::Unloaded) {
        Status loadRes = loadPlugin(name, ctx);
        if (!loadRes.ok()) {
            return loadRes;
        }
    }

    if (entry.state == PluginState::Initialized) {
        return OkStatus();
    }

    if (entry.state != PluginState::Loaded) {
        return FailedPreconditionError(StrCat("插件 [", name, "] 处于非就绪状态: ", static_cast<int>(entry.state)));
    }

    Status status = entry.plugin->onInit(ctx);
    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onInit 失败: {}", name, status.message());
        return status;
    }

    entry.state = PluginState::Initialized;
    spdlog::info("插件 [{}] 成功初始化", name);
    return OkStatus();
}

Status PluginManager::loadAndInitPlugin(const std::string& name, PluginContext& ctx) {
    Status loadRes = loadPlugin(name, ctx);
    if (!loadRes.ok()) {
        return loadRes;
    }
    return initPlugin(name, ctx);
}

Status PluginManager::loadAll(PluginContext& ctx) {
    std::vector<std::string> namesToLoad;
    for (const auto& pair : m_plugins) {
        namesToLoad.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(namesToLoad.begin(), namesToLoad.end(), pair.first) == namesToLoad.end()) {
            namesToLoad.push_back(pair.first);
        }
    }

    for (const auto& name : namesToLoad) {
        Status status = loadPlugin(name, ctx);
        if (!status.ok()) {
            return status;
        }
    }
    return OkStatus();
}

Status PluginManager::initAll(PluginContext& ctx) {
    for (const auto& name : m_loadOrder) {
        Status status = initPlugin(name, ctx);
        if (!status.ok()) {
            return status;
        }
    }
    return OkStatus();
}

void PluginManager::logicTick(float dt) {
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onLogicTick(dt);
        }
    }
}

void PluginManager::renderTick() {
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onRenderTick();
        }
    }
}

Status PluginManager::unloadPlugin(const std::string& name) {
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }

    auto& entry = it->second;
    if (entry.plugin && (entry.state == PluginState::Initialized || entry.state == PluginState::Loaded)) {
        entry.plugin->onUnload();
        spdlog::info("插件 [{}] 已卸载", name);
    }

    entry.state = PluginState::Unloaded;
    freeDynamicLibrary(entry);

    auto orderIt = std::find(m_loadOrder.begin(), m_loadOrder.end(), name);
    if (orderIt != m_loadOrder.end()) {
        m_loadOrder.erase(orderIt);
    }

    return OkStatus();
}

void PluginManager::unloadAll() {
    std::vector<std::string> reverseOrder = m_loadOrder;
    std::reverse(reverseOrder.begin(), reverseOrder.end());

    for (const auto& name : reverseOrder) {
        (void)unloadPlugin(name);
    }
    m_loadOrder.clear();
}

std::string PluginManager::resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir) const {
    namespace fs = std::filesystem;
    std::error_code ec;

    auto normalize = [&](const fs::path& p) -> std::string {
        fs::path resolved = fs::weakly_canonical(p, ec);
        const fs::path& target = ec ? p : resolved;
        return std::string(reinterpret_cast<const char*>(target.generic_u8string().c_str()));
    };

    fs::path rawPath = fs::u8path(inputPath);
    if (fs::exists(rawPath, ec) && !fs::is_directory(rawPath, ec)) {
        return normalize(rawPath);
    }

    std::string dllFileName = std::string(reinterpret_cast<const char*>(rawPath.filename().generic_u8string().c_str()));
    std::string stemName = std::string(reinterpret_cast<const char*>(rawPath.stem().generic_u8string().c_str()));
    if (!dllFileName.ends_with(".dll") && !dllFileName.ends_with(".so")) {
        stemName = dllFileName;
        dllFileName += ".dll";
    }

    std::vector<fs::path> candidates;
    if (!baseDir.empty()) {
        fs::path base = fs::u8path(baseDir);
        candidates.push_back(base / rawPath);
        candidates.push_back(base / dllFileName);
        candidates.push_back(base / "Plugins" / stemName / dllFileName);
    }

#ifdef _WIN32
    wchar_t moduleBuf[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, moduleBuf, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(moduleBuf).parent_path();
        candidates.push_back(exeDir / dllFileName);
        candidates.push_back(exeDir / "Plugins" / stemName / dllFileName);
        if (exeDir.has_parent_path() && exeDir.parent_path().has_parent_path()) {
            fs::path buildRoot = exeDir.parent_path().parent_path();
            candidates.push_back(buildRoot / "Plugins" / stemName / dllFileName);
        }
    }
#endif

    fs::path probe = fs::current_path(ec);
    for (int depth = 0; depth < 6; ++depth) {
        candidates.push_back(probe / rawPath);
        candidates.push_back(probe / dllFileName);
        candidates.push_back(probe / "Plugins" / stemName / dllFileName);
        candidates.push_back(probe / "Output" / "build-windows-debug" / "Plugins" / stemName / dllFileName);
        candidates.push_back(probe / "Output" / "build-windows-release" / "Plugins" / stemName / dllFileName);
        if (!probe.has_parent_path() || probe == probe.parent_path()) {
            break;
        }
        probe = probe.parent_path();
    }

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
            return normalize(candidate);
        }
    }
    return inputPath;
}

std::string PluginManager::resolveScriptPluginPath(const std::string& inputPath, const std::string& baseDir) const {
    namespace fs = std::filesystem;
    std::error_code ec;

    auto normalize = [&](const fs::path& p) -> std::string {
        fs::path resolved = fs::weakly_canonical(p, ec);
        const fs::path& target = ec ? p : resolved;
        return std::string(reinterpret_cast<const char*>(target.generic_u8string().c_str()));
    };

    auto isValidScriptTarget = [&](const fs::path& candidate) -> bool {
        if (!fs::exists(candidate, ec)) {
            return false;
        }
        if (fs::is_directory(candidate, ec)) {
            return fs::exists(candidate / "plugin.json", ec) ||
                   fs::exists(candidate / "index.ts", ec) ||
                   fs::exists(candidate / "index.js", ec);
        }
        std::string s = std::string(reinterpret_cast<const char*>(candidate.generic_u8string().c_str()));
        return s.ends_with(".ts") || s.ends_with(".js") || s.ends_with("plugin.json");
    };

    fs::path rawPath = fs::u8path(inputPath);
    if (isValidScriptTarget(rawPath)) {
        return normalize(rawPath);
    }

    std::vector<fs::path> candidates;
    if (!baseDir.empty()) {
        fs::path base = fs::u8path(baseDir);
        candidates.push_back(base / rawPath);
        candidates.push_back(base / "Plugins" / rawPath);
        candidates.push_back(base / "Assets" / "plugins" / rawPath);
    }

#ifdef _WIN32
    wchar_t moduleBuf[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, moduleBuf, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(moduleBuf).parent_path();
        candidates.push_back(exeDir / rawPath);
        candidates.push_back(exeDir / "Plugins" / rawPath);
    }
#endif

    fs::path probe = fs::current_path(ec);
    for (int depth = 0; depth < 6; ++depth) {
        candidates.push_back(probe / rawPath);
        candidates.push_back(probe / "Plugins" / rawPath);
        candidates.push_back(probe / "Assets" / "plugins" / rawPath);
        if (!probe.has_parent_path() || probe == probe.parent_path()) {
            break;
        }
        probe = probe.parent_path();
    }

    for (const auto& candidate : candidates) {
        if (isValidScriptTarget(candidate)) {
            return normalize(candidate);
        }
    }
    return "";
}

Status PluginManager::loadDynamicPlugin(const std::string& libraryPath, PluginContext& ctx) {
#ifdef _WIN32
    std::string resolvedPath = resolveDynamicLibraryPath(libraryPath);
    HMODULE handle = LoadLibraryA(resolvedPath.c_str());
    if (!handle) {
        DWORD err = GetLastError();
        return NotFoundError(StrCat("加载动态链接库失败: ", resolvedPath, ", Windows错误码: ", err));
    }

    auto createFn = reinterpret_cast<CreatePluginFunc>(GetProcAddress(handle, "CreatePlugin"));
    if (!createFn) {
        FreeLibrary(handle);
        return InvalidArgumentError(StrCat("动态库未导出 CreatePlugin 函数: ", resolvedPath));
    }

    IPlugin* rawPlugin = createFn();
    if (!rawPlugin) {
        FreeLibrary(handle);
        return InternalError(StrCat("CreatePlugin 返回空指针: ", resolvedPath));
    }

    std::string pluginName = rawPlugin->getName();
    if (hasPlugin(pluginName)) {
        delete rawPlugin;
        FreeLibrary(handle);
        return loadAndInitPlugin(pluginName, ctx);
    }

    auto pluginPtr = std::shared_ptr<IPlugin>(rawPlugin);

    PluginEntry entry;
    entry.name = pluginName;
    entry.plugin = pluginPtr;
    entry.state = PluginState::Unloaded;
    entry.isDynamic = true;
    entry.dynamicLibraryPath = resolvedPath;
    entry.libraryHandle = handle;

    m_plugins[pluginName] = std::move(entry);

    CHECK_STATUS(loadPlugin(pluginName, ctx));
    return initPlugin(pluginName, ctx);
#else
    return UnimplementedError("非Windows平台的动态插件加载尚未配置");
#endif
}

Status PluginManager::loadScriptPlugin(const std::string& scriptOrDirPath, PluginContext& ctx) {
    std::string resolvedPath = resolveScriptPluginPath(scriptOrDirPath);
    if (resolvedPath.empty()) {
        resolvedPath = scriptOrDirPath;
    }

    auto manifestRes = ResolveScriptPluginManifest(resolvedPath, ctx.resourceLoader);
    CHECK_STATUS(manifestRes);

    const ScriptPluginManifestInfo& manifest = manifestRes.value();
    if (hasPlugin(manifest.name)) {
        return loadAndInitPlugin(manifest.name, ctx);
    }

    auto scriptPlugin = std::make_shared<ScriptPluginAdapter>(manifest);
    PluginEntry entry;
    entry.name = manifest.name;
    entry.plugin = scriptPlugin;
    entry.state = PluginState::Unloaded;
    entry.isScript = true;
    entry.dynamicLibraryPath = manifest.resolvedEntryPath;

    m_plugins[manifest.name] = std::move(entry);

    CHECK_STATUS(loadPlugin(manifest.name, ctx));
    return initPlugin(manifest.name, ctx);
}

Status PluginManager::resolveAndLoadPlugin(const std::string& pluginIdent,
                                           const std::string& baseDir,
                                           PluginContext& ctx) {
    if (hasPlugin(pluginIdent)) {
        return loadAndInitPlugin(pluginIdent, ctx);
    }

    std::string scriptCandidate = resolveScriptPluginPath(pluginIdent, baseDir);
    if (!scriptCandidate.empty()) {
        return loadScriptPlugin(scriptCandidate, ctx);
    }

    if (pluginIdent.ends_with(".ts") || pluginIdent.ends_with(".js") || pluginIdent.ends_with("plugin.json")) {
        return loadScriptPlugin(pluginIdent, ctx);
    }

    std::string dllCandidate = resolveDynamicLibraryPath(pluginIdent, baseDir);
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::u8path(dllCandidate), ec) ||
        pluginIdent.ends_with(".dll") ||
        pluginIdent.ends_with(".so")) {
        return loadDynamicPlugin(dllCandidate, ctx);
    }

    return NotFoundError(StrCat("无法解析或找到插件: ", pluginIdent));
}

void PluginManager::freeDynamicLibrary(PluginEntry& entry) {
#ifdef _WIN32
    if (entry.isDynamic && entry.libraryHandle) {
        entry.plugin.reset();
        FreeLibrary(static_cast<HMODULE>(entry.libraryHandle));
        entry.libraryHandle = nullptr;
    }
#endif
}

bool PluginManager::hasPlugin(const std::string& name) const {
    return m_plugins.find(name) != m_plugins.end() || m_factories.find(name) != m_factories.end();
}

std::shared_ptr<IPlugin> PluginManager::getPlugin(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.plugin;
    }
    return nullptr;
}

PluginState PluginManager::getPluginState(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.state;
    }
    return PluginState::Unloaded;
}

std::vector<std::string> PluginManager::getAllPluginNames() const {
    std::vector<std::string> names;
    names.reserve(m_plugins.size() + m_factories.size());
    for (const auto& pair : m_plugins) {
        names.push_back(pair.first);
    }
    for (const auto& pair : m_factories) {
        if (std::find(names.begin(), names.end(), pair.first) == names.end()) {
            names.push_back(pair.first);
        }
    }
    return names;
}

std::vector<std::string> PluginManager::getPluginsInState(PluginState state) const {
    std::vector<std::string> names;
    for (const auto& pair : m_plugins) {
        if (pair.second.state == state) {
            names.push_back(pair.first);
        }
    }
    return names;
}

const PluginEntry* PluginManager::getPluginEntry(const std::string& name) const {
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace MQEngine
