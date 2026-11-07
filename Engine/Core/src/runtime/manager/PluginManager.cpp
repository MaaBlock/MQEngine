#include "PluginManager.h"
#include <algorithm>
#include <cassert>
#include "../core/EngineGlobal.h"
#include "../core/Engine.h"
#include "../core/Runtime.h"
#include "../interface/Application.h"
#include "UserCodeManager.h"
#include "../../base/ConfigDocument.h"
#include "../../scene/manager/DataManager.h"
#include "../../resource/core/DiskResourceLoader.h"
#include "../../resource/core/ResourceLoader.h"
#include "../../render/manager/PipelineRegistries.h"
#include "SystemManager.h"
#include "../../scene/system/ComponentReflection.h"
#include "../../script/system/ScriptSystem.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace MQEngine {
namespace Runtime {

using absl::AlreadyExistsError;
using absl::FailedPreconditionError;

using CreatePluginFunc = IPlugin* (*)();

namespace {

Engine* ResolveEngine(Engine* provided) {
    return provided ? provided : Engine::getInstance();
}

ResourceLoader* ResolveEngineResourceLoader(Engine* engine) {
    if (engine && engine->resource && engine->resource->getResourceLoader()) {
        return engine->resource->getResourceLoader();
    }
    return g_engineGlobal.resourceLoader;
}

struct ScriptPluginManifestInfo {
    std::string name;
    std::string version = "1.0.0";
    std::string mainScript;
    std::string pluginDirectory;
    std::string resolvedEntryPath;
    PluginScope scope = PluginScope::User;
    std::vector<std::string> requiredTags;
    std::vector<std::string> dependencies;
};

ResourceLoader* ResolveLoaderOrFallback(ResourceLoader* provided, UniquePtr<DiskResourceLoader>& fallbackHolder) {
    if (provided) {
        return provided;
    }
    if (g_engineGlobal.resourceLoader) {
        return g_engineGlobal.resourceLoader;
    }
    fallbackHolder = makeUnique<DiskResourceLoader>();
    return fallbackHolder.get();
}

template <typename T>
void MergeUniqueItems(std::vector<T>& dst, const std::vector<T>& src) {
    for (const auto& item : src) {
        if (std::find(dst.begin(), dst.end(), item) == dst.end()) {
            dst.push_back(item);
        }
    }
}

template <typename T>
void AppendDiffItems(std::vector<T>& dst, const std::vector<T>& before, const std::vector<T>& after) {
    for (const auto& item : after) {
        if (std::find(before.begin(), before.end(), item) == before.end() &&
            std::find(dst.begin(), dst.end(), item) == dst.end()) {
            dst.push_back(item);
        }
    }
}

bool IsEngineBuiltinPluginPath(const std::string& pathHint) {
    std::string normPath = SAI::Path::normalize(pathHint);
    return normPath.find("/Engine/Plugins/") != std::string::npos ||
           normPath.find("/Plugins/Engine/") != std::string::npos;
}

StatusOr<ScriptPluginManifestInfo> ResolveScriptPluginManifest(const std::string& pluginPath, ResourceLoader* resourceLoader) {
    UniquePtr<DiskResourceLoader> fallbackLoader;
    resourceLoader = ResolveLoaderOrFallback(resourceLoader, fallbackLoader);

    ScriptPluginManifestInfo manifest;

    if (SAI::FileSystem::isDirectory(pluginPath)) {
        manifest.pluginDirectory = SAI::Path::normalize(pluginPath);
        manifest.name = SAI::Path::fileName(pluginPath);

        std::string manifestMqPath = SAI::Path::join(pluginPath, "plugin.mq");
        std::string manifestJsonPath = SAI::Path::join(pluginPath, "plugin.json");
        std::string activeManifestPath = resourceLoader->exists(manifestMqPath) ? manifestMqPath : manifestJsonPath;
        if (resourceLoader->exists(activeManifestPath)) {
            auto docRes = ConfigDocument::loadFromFile(resourceLoader, SAI::Path::normalize(activeManifestPath));
            if (docRes.ok()) {
                const ConfigDocument& doc = docRes.value();
                std::string parsedName = doc.getStringOr({"plugin.name", "name"});
                if (!parsedName.empty()) {
                    manifest.name = parsedName;
                }
                manifest.version = doc.getStringOr({"plugin.version", "version"}, "1.0.0");
                manifest.mainScript = doc.getStringOr({"plugin.main", "main"});
                std::string scopeStr = doc.getStringOr({"plugin.scope", "scope"});
                if (scopeStr == "Engine" || scopeStr == "engine" || scopeStr == "builtin") {
                    manifest.scope = PluginScope::Engine;
                }
                manifest.requiredTags = doc.getStringArrayOr({"plugin.tags", "tags"});
                manifest.dependencies = doc.getStringArrayOr({"plugin.dependencies", "dependencies"});
            }
        }

        if (IsEngineBuiltinPluginPath(manifest.pluginDirectory)) {
            manifest.scope = PluginScope::Engine;
        }

        if (manifest.mainScript.empty()) {
            if (resourceLoader->exists(SAI::Path::join(pluginPath, "index.ts"))) {
                manifest.mainScript = "index.ts";
            } else if (resourceLoader->exists(SAI::Path::join(pluginPath, "index.js"))) {
                manifest.mainScript = "index.js";
            } else if (resourceLoader->exists(SAI::Path::join(pluginPath, "index.py"))) {
                manifest.mainScript = "index.py";
            } else {
                return NotFoundError(StrCat("脚本插件目录缺少入口文件 (index.ts / index.js / index.py / plugin.mq): ", pluginPath));
            }
        }

        manifest.resolvedEntryPath = SAI::Path::normalize(SAI::Path::join(pluginPath, manifest.mainScript));
        return manifest;
    }

    if (resourceLoader->exists(pluginPath) &&
        (SAI::Path::fileName(pluginPath) == "plugin.mq" || SAI::Path::fileName(pluginPath) == "plugin.json")) {
        return ResolveScriptPluginManifest(SAI::Path::normalize(SAI::Path::parentPath(pluginPath)), resourceLoader);
    }

    if (!resourceLoader->exists(pluginPath)) {
        return NotFoundError(StrCat("未找到脚本插件文件: ", pluginPath));
    }

    manifest.resolvedEntryPath = SAI::Path::normalize(pluginPath);
    manifest.pluginDirectory = SAI::Path::normalize(SAI::Path::parentPath(pluginPath));
    manifest.mainScript = SAI::Path::fileName(pluginPath);
    manifest.name = SAI::Path::stem(pluginPath);
    if (IsEngineBuiltinPluginPath(manifest.pluginDirectory)) {
        manifest.scope = PluginScope::Engine;
    }
    return manifest;
}

class ScriptPluginAdapter;

/**
 * @brief 脚本插件默认组合体，将脚本插件统一纳入 TokenGraph 拓扑排序与调度
 */
class ScriptPluginComposite : public Composite {
public:
    ScriptPluginComposite(ScriptPluginAdapter* owner, ScriptPluginManifestInfo manifest)
        : m_owner(owner), m_manifest(std::move(manifest)) {}

    void registerInfo() override {
        compositeRegisterInfo = CompositeRegisterInfo{
            .name = m_manifest.name,
            .requiredTags = m_manifest.requiredTags,
            .predecessors = m_manifest.dependencies,
        };
    }

    Status onLoad(Engine* engine) override;
    Status onInit(Engine* engine) override;
    void onLogicTick(float dt) override;
    void onRenderTick() override;
    void onUnload() override;

private:
    ScriptPluginAdapter* m_owner = nullptr;
    ScriptPluginManifestInfo m_manifest;
};

/**
 * @brief 内部脚本插件适配器：将使用 TS / JS / Python 编写的插件通过 Composite 单元接入 TokenGraph 生命周期
 */
class ScriptPluginAdapter : public IPlugin {
public:
    explicit ScriptPluginAdapter(const ScriptPluginManifestInfo& manifest)
        : m_manifest(manifest) {}

    ~ScriptPluginAdapter() override {
        if (m_isLoaded) {
            unloadScript();
        }
    }

    void registerInfo() override {
        if (getComposites().empty()) {
            emplaceComposite<ScriptPluginComposite>(this, m_manifest);
        }
    }

    std::string getName() const override { return m_manifest.name; }
    std::string getVersion() const override { return m_manifest.version; }
    PluginScope getScope() const override { return m_manifest.scope; }
    std::vector<std::string> getDependencies() const override { return m_manifest.dependencies; }

    Status loadScript(Engine* engine) {
        if (m_isLoaded) {
            return OkStatus();
        }
        ScriptSystem* ss = (engine && engine->script && engine->script->getScriptSystem())
            ? engine->script->getScriptSystem()
            : g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("加载脚本插件 [", m_manifest.name, "] 失败: 引擎核心 ScriptSystem 未初始化"));
        }

        CHECK_STATUS(ss->executePluginScript(
            m_manifest.name,
            m_manifest.version,
            m_manifest.resolvedEntryPath,
            m_manifest.pluginDirectory,
            ResolveEngineResourceLoader(engine)));

        m_isLoaded = true;
        return ss->invokePluginLifecycle(m_manifest.name, "onLoad");
    }

    Status initScript(Engine* engine) {
        ScriptSystem* ss = (engine && engine->script && engine->script->getScriptSystem())
            ? engine->script->getScriptSystem()
            : g_engineGlobal.scriptSystem;
        if (!ss) {
            return FailedPreconditionError(StrCat("初始化脚本插件 [", m_manifest.name, "] 失败: ScriptSystem 未就绪"));
        }
        return ss->invokePluginLifecycle(m_manifest.name, "onInit");
    }

    void tickScriptLogic(float dt) {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        std::string dtJson = "{\"dt\":" + std::to_string(dt) + "}";
        (void)ss->invokePluginLifecycle(m_manifest.name, "onLogicTick", dtJson);
        if (ss->getNodeEnvironment()) {
            ss->getNodeEnvironment()->tick();
        }
    }

    void tickScriptRender() {
        ScriptSystem* ss = g_engineGlobal.scriptSystem;
        if (!m_isLoaded || !ss) {
            return;
        }
        (void)ss->invokePluginLifecycle(m_manifest.name, "onRenderTick");
    }

    void unloadScript() {
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

Status ScriptPluginComposite::onLoad(Engine* engine) {
    return m_owner ? m_owner->loadScript(engine) : OkStatus();
}

Status ScriptPluginComposite::onInit(Engine* engine) {
    return m_owner ? m_owner->initScript(engine) : OkStatus();
}

void ScriptPluginComposite::onLogicTick(float dt) {
    if (m_owner) {
        m_owner->tickScriptLogic(dt);
    }
}

void ScriptPluginComposite::onRenderTick() {
    if (m_owner) {
        m_owner->tickScriptRender();
    }
}

void ScriptPluginComposite::onUnload() {
    if (m_owner) {
        m_owner->unloadScript();
    }
}

} // namespace

PluginManager::PluginManager() {
    m_activeTags.insert("Runtime");

    m_requestQueue.subscribe<Request::Load>([this](Request::Load& req) {
        Engine* engine = Engine::getInstance();
        std::string baseDir = req.baseDir;
        if (baseDir.empty() && engine && engine->foundation &&
            engine->foundation->isProjectOpen()) {
            baseDir = engine->foundation->getProjectDirectory();
        }
        Status st = resolveAndPreloadPlugin(req.pluginIdent, baseDir, engine);
        if (st.ok()) {
            m_pendingGraphRefresh = true;
        }
        return st;
    });

    m_requestQueue.subscribe<Request::Unload>([this](Request::Unload& req) {
        return unloadPlugin(req.pluginName, Engine::getInstance());
    });

    m_requestQueue.subscribe<Request::Reload>([this](Request::Reload& req) {
        return reloadPlugin(req.pluginName, Engine::getInstance());
    });

    m_requestQueue.subscribe<Request::SetTags>([this](Request::SetTags& req) {
        return setTagsAndRefresh(req.tags, Engine::getInstance());
    });
}

void PluginManager::registerInfo() {
    Engine* engine = Engine::getInstance();
    assert(engine != nullptr && engine->application != nullptr && "Engine 与 Application 必须已创建");
    const auto& tags = engine->application->applicationRegisterInfo.tags;
    if (!tags.empty()) {
        setTags(tags);
    }
}

void PluginManager::requestLoadPlugin(const std::string& pluginIdent, const std::string& baseDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Load{pluginIdent, baseDir});
}

void PluginManager::requestUnloadPlugin(const std::string& pluginName) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Unload{pluginName});
}

void PluginManager::requestReloadPlugin(const std::string& pluginName) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::Reload{pluginName});
}

void PluginManager::requestSetTags(const std::vector<std::string>& tags) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_requestQueue.enqueue(Request::SetTags{tags});
}

void PluginManager::syncTicker() {
    ZoneScoped();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    while (!m_requestQueue.empty()) {
        auto st = m_requestQueue.processNext();
        if (!st.ok()) {
            spdlog::error("PluginManager::syncTicker 处理请求失败: {}", st.message());
        }
    }
    if (m_pendingGraphRefresh) {
        m_pendingGraphRefresh = false;
        auto st = refreshComposites(Engine::getInstance());
        if (!st.ok()) {
            spdlog::error("PluginManager::syncTicker 拓扑排序激发 Composite 失败: {}", st.message());
        }
    }
}

void PluginManager::checkHotReload(Engine* engine) {
    if (!m_autoHotReload) {
        return;
    }
    (void)checkAndReloadModified(ResolveEngine(engine));
}

PluginManager::~PluginManager() {
    unloadAll();
}

void PluginManager::setTags(const std::vector<std::string>& tags) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_activeTags.clear();
    for (const auto& tag : tags) {
        if (!tag.empty()) {
            m_activeTags.insert(tag);
        }
    }
}

Status PluginManager::setTagsAndRefresh(const std::vector<std::string>& tags, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    setTags(tags);
    return refreshComposites(ResolveEngine(engine));
}

void PluginManager::addTag(const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!tag.empty()) {
        m_activeTags.insert(tag);
    }
}

Status PluginManager::addTagAndRefresh(const std::string& tag, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    addTag(tag);
    return refreshComposites(ResolveEngine(engine));
}

void PluginManager::removeTag(const std::string& tag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_activeTags.erase(tag);
}

Status PluginManager::removeTagAndRefresh(const std::string& tag, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    removeTag(tag);
    return refreshComposites(ResolveEngine(engine));
}

bool PluginManager::hasTag(const std::string& tag) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_activeTags.find(tag) != m_activeTags.end();
}

std::vector<std::string> PluginManager::getActiveTags() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> tags(m_activeTags.begin(), m_activeTags.end());
    std::sort(tags.begin(), tags.end());
    return tags;
}

bool PluginManager::isTagsMatched(const std::vector<std::string>& requiredTags) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& reqTag : requiredTags) {
        if (!reqTag.empty() && m_activeTags.find(reqTag) == m_activeTags.end()) {
            return false;
        }
    }
    return true;
}

void PluginManager::registerRawService(uint64_t serviceTypeId, void* instance) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (serviceTypeId != 0 && instance != nullptr) {
        m_services[serviceTypeId] = instance;
    }
}

void PluginManager::unregisterRawService(uint64_t serviceTypeId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_services.erase(serviceTypeId);
}

void* PluginManager::getRawService(uint64_t serviceTypeId) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_services.find(serviceTypeId);
    return it != m_services.end() ? it->second : nullptr;
}

std::vector<uint64_t> PluginManager::getRegisteredServiceIds() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<uint64_t> ids;
    ids.reserve(m_services.size());
    for (const auto& pair : m_services) {
        ids.push_back(pair.first);
    }
    return ids;
}

Status PluginManager::registerPlugin(std::shared_ptr<IPlugin> plugin) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

    plugin->registerInfo();

    PluginEntry entry;
    entry.name = name;
    entry.plugin = plugin;
    entry.scope = plugin->getScope();
    entry.state = PluginState::Unloaded;
    registerPluginComposites(entry);
    m_plugins.emplace(name, std::move(entry));

    return OkStatus();
}

Status PluginManager::registerPluginFactory(const std::string& name, PluginFactory factory) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
        instance->registerInfo();
        PluginEntry entry;
        entry.name = name;
        entry.plugin = instance;
        entry.scope = instance->getScope();
        entry.state = PluginState::Unloaded;
        registerPluginComposites(entry);
        m_plugins[name] = std::move(entry);
        return OkStatus();
    }

    return NotFoundError(StrCat("未找到已注册插件或插件工厂: ", name));
}

Status PluginManager::preloadPluginDependencies(const IPlugin& plugin, Engine* engine) {
    const auto deps = plugin.getDependencies();
    for (const auto& depName : deps) {
        if (depName.empty() || depName == plugin.getName()) {
            continue;
        }
        auto depIt = m_plugins.find(depName);
        if (depIt != m_plugins.end() && depIt->second.state != PluginState::Unloaded && depIt->second.state != PluginState::Failed) {
            continue;
        }
        Status depStatus = resolveAndPreloadPlugin(depName, "", engine);
        if (!depStatus.ok()) {
            spdlog::error("插件 [{}] 依赖的上游插件 [{}] 预加载失败: {}",
                          plugin.getName(), depName, depStatus.message());
            return depStatus;
        }
    }
    return OkStatus();
}

void PluginManager::registerPluginComposites(PluginEntry& entry) {
    if (!entry.plugin) {
        return;
    }
    for (const auto& compPtr : entry.plugin->getComposites()) {
        if (!compPtr) {
            continue;
        }
        compPtr->registerInfo();
        std::string compName = compPtr->getName();
        if (compName.empty()) {
            continue;
        }
        if (std::find(entry.compositeNames.begin(), entry.compositeNames.end(), compName) == entry.compositeNames.end()) {
            entry.compositeNames.push_back(compName);
        }
        auto existingIt = m_composites.find(compName);
        if (existingIt == m_composites.end()) {
            CompositeEntry compEntry;
            compEntry.name = compName;
            compEntry.ownerPluginName = entry.name;
            compEntry.composite = compPtr.get();
            compEntry.state = PluginState::Unloaded;
            m_composites.emplace(compName, std::move(compEntry));
        } else {
            existingIt->second.ownerPluginName = entry.name;
            existingIt->second.composite = compPtr.get();
        }
    }
}

Status PluginManager::preloadPlugin(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    CHECK_STATUS(ensurePluginInstance(name));

    auto& entry = m_plugins[name];
    if (entry.state == PluginState::Preloaded ||
        entry.state == PluginState::Loaded ||
        entry.state == PluginState::Initialized) {
        return OkStatus();
    }

    entry.plugin->registerInfo();
    CHECK_STATUS(preloadPluginDependencies(*entry.plugin, engine));
    registerPluginComposites(entry);
    entry.state = PluginState::Preloaded;

    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), name) == m_loadOrder.end()) {
        m_loadOrder.push_back(name);
    }

    spdlog::info("插件 [{}] 已完成预加载与 Composite 注册 (版本: {}, Composite 数量: {})",
                 name, entry.plugin->getVersion(), entry.compositeNames.size());
    return OkStatus();
}

Status PluginManager::ensureOwnerPluginLoaded(PluginEntry& entry, Engine* engine) {
    if (entry.state == PluginState::Loaded || entry.state == PluginState::Initialized) {
        return OkStatus();
    }
    if (!entry.plugin) {
        return FailedPreconditionError(StrCat("插件实例为空: ", entry.name));
    }

    for (const auto& depName : entry.plugin->getDependencies()) {
        auto depIt = m_plugins.find(depName);
        if (depIt != m_plugins.end()) {
            CHECK_STATUS(ensureOwnerPluginLoaded(depIt->second, engine));
        }
    }

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;

    std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
    std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
    std::vector<uint64_t> beforeServices = getRegisteredServiceIds();

    Status status = entry.plugin->onLoad(engine);

    if (compRefl) {
        AppendDiffItems(entry.registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
    }
    if (sysMgr) {
        AppendDiffItems(entry.registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
    }
    AppendDiffItems(entry.registeredServices, beforeServices, getRegisteredServiceIds());

    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onLoad 失败: {}", entry.name, status.message());
        return status;
    }

    entry.state = PluginState::Loaded;
    registerPluginComposites(entry);

    if (std::find(m_loadOrder.begin(), m_loadOrder.end(), entry.name) == m_loadOrder.end()) {
        m_loadOrder.push_back(entry.name);
    }
    return OkStatus();
}

Status PluginManager::ensureOwnerPluginInitialized(PluginEntry& entry, Engine* engine) {
    if (entry.state == PluginState::Initialized) {
        return OkStatus();
    }
    if (entry.state == PluginState::Unloaded || entry.state == PluginState::Preloaded) {
        CHECK_STATUS(ensureOwnerPluginLoaded(entry, engine));
    }
    if (entry.state != PluginState::Loaded) {
        return FailedPreconditionError(StrCat("插件 [", entry.name, "] 处于非就绪状态: ", static_cast<int>(entry.state)));
    }

    for (const auto& depName : entry.plugin->getDependencies()) {
        auto depIt = m_plugins.find(depName);
        if (depIt != m_plugins.end()) {
            CHECK_STATUS(ensureOwnerPluginInitialized(depIt->second, engine));
        }
    }

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;

    std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
    std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
    std::vector<uint64_t> beforeServices = getRegisteredServiceIds();

    Status status = entry.plugin->onInit(engine);

    if (compRefl) {
        AppendDiffItems(entry.registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
    }
    if (sysMgr) {
        AppendDiffItems(entry.registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
        sysMgr->flushRequests();
    }
    AppendDiffItems(entry.registeredServices, beforeServices, getRegisteredServiceIds());

    if (!status.ok()) {
        entry.state = PluginState::Failed;
        entry.errorMessage = std::string(status.message());
        spdlog::error("插件 [{}] onInit 失败: {}", entry.name, status.message());
        return status;
    }

    registerPluginComposites(entry);
    entry.state = PluginState::Initialized;
    return OkStatus();
}

Status PluginManager::refreshComposites(Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);

    ComponentReflection* compRefl = (engine && engine->foundation)
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    SystemManager* sysMgr = (engine && engine->host)
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;

    // 1. 先检查已入图但因 Tag 变更不再满足包含匹配条件的 Composite，按逆拓扑序卸载并移出 TokenGraph
    auto currentOrder = m_compositeGraph.order();
    for (auto it = currentOrder.rbegin(); it != currentOrder.rend(); ++it) {
        CompositeEntry* compEntry = *it;
        if (!compEntry || !compEntry->composite) {
            continue;
        }
        if (!isTagsMatched(compEntry->composite->getRequiredTags())) {
            unloadSingleComposite(*compEntry, engine);
            m_compositeGraph.removeNode(compEntry->name);
        }
    }

    // 2. 将所有归属插件已预加载/已加载且满足 Tag 包含匹配的 Composite 节点加入 TokenGraph
    for (auto& [compName, compEntry] : m_composites) {
        if (!compEntry.composite) {
            continue;
        }
        auto plugIt = m_plugins.find(compEntry.ownerPluginName);
        if (plugIt == m_plugins.end() ||
            (plugIt->second.state != PluginState::Preloaded &&
             plugIt->second.state != PluginState::Loaded &&
             plugIt->second.state != PluginState::Initialized)) {
            continue;
        }

        if (isTagsMatched(compEntry.composite->getRequiredTags())) {
            FCT::Vector<std::string> preds;
            for (const auto& p : compEntry.composite->getPredecessors()) {
                preds.push_back(p);
            }
            FCT::Vector<std::string> succs;
            for (const auto& s : compEntry.composite->getSuccessors()) {
                succs.push_back(s);
            }
            m_compositeGraph.addNode(compName, &compEntry, std::move(preds), std::move(succs));
        }
    }

    // 3. 在所有插件预加载注册完成后，统一对 TokenGraph 执行拓扑排序
    Status topoStatus = m_compositeGraph.update();
    if (!topoStatus.ok()) {
        spdlog::error("PluginManager: Composite TokenGraph 拓扑排序失败（检测到循环依赖）: {}", topoStatus.message());
        return topoStatus;
    }

    auto sortedComposites = m_compositeGraph.order();
    Status firstError = OkStatus();

    // 4. 按拓扑正序确保所属插件已 onLoad，并执行未加载 Composite 的 onLoad
    for (CompositeEntry* compEntry : sortedComposites) {
        if (!compEntry || !compEntry->composite || compEntry->state != PluginState::Unloaded) {
            continue;
        }
        auto plugIt = m_plugins.find(compEntry->ownerPluginName);
        if (plugIt != m_plugins.end()) {
            Status ownerLoadSt = ensureOwnerPluginLoaded(plugIt->second, engine);
            if (!ownerLoadSt.ok()) {
                if (firstError.ok()) {
                    firstError = ownerLoadSt;
                }
                continue;
            }
        }

        std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
        std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
        std::vector<uint64_t> beforeServices = getRegisteredServiceIds();

        Status loadSt = compEntry->composite->onLoad(engine);

        if (compRefl) {
            AppendDiffItems(compEntry->registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
        }
        if (sysMgr) {
            AppendDiffItems(compEntry->registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
        }
        AppendDiffItems(compEntry->registeredServices, beforeServices, getRegisteredServiceIds());

        if (!loadSt.ok()) {
            compEntry->state = PluginState::Failed;
            compEntry->errorMessage = std::string(loadSt.message());
            if (plugIt != m_plugins.end()) {
                plugIt->second.state = PluginState::Failed;
                plugIt->second.errorMessage = std::string(loadSt.message());
            }
            spdlog::error("Composite [{}] (所属插件: {}) onLoad 失败: {}",
                          compEntry->name, compEntry->ownerPluginName, loadSt.message());
            if (firstError.ok()) {
                firstError = loadSt;
            }
            continue;
        }
        compEntry->state = PluginState::Loaded;
    }

    // 5. 按拓扑正序确保所属插件已 onInit，并执行已加载 Composite 的 onInit
    for (CompositeEntry* compEntry : sortedComposites) {
        if (!compEntry || !compEntry->composite || compEntry->state != PluginState::Loaded) {
            continue;
        }
        auto plugIt = m_plugins.find(compEntry->ownerPluginName);
        if (plugIt != m_plugins.end()) {
            Status ownerInitSt = ensureOwnerPluginInitialized(plugIt->second, engine);
            if (!ownerInitSt.ok()) {
                if (firstError.ok()) {
                    firstError = ownerInitSt;
                }
                continue;
            }
        }

        std::vector<std::string> beforeComps = compRefl ? compRefl->getRegisteredComponentNames() : std::vector<std::string>{};
        std::vector<std::string> beforeSys = sysMgr ? sysMgr->getRegisteredSystemNames() : std::vector<std::string>{};
        std::vector<uint64_t> beforeServices = getRegisteredServiceIds();

        Status initSt = compEntry->composite->onInit(engine);

        if (compRefl) {
            AppendDiffItems(compEntry->registeredComponents, beforeComps, compRefl->getRegisteredComponentNames());
        }
        if (sysMgr) {
            AppendDiffItems(compEntry->registeredSystems, beforeSys, sysMgr->getRegisteredSystemNames());
            sysMgr->flushRequests();
        }
        AppendDiffItems(compEntry->registeredServices, beforeServices, getRegisteredServiceIds());

        if (!initSt.ok()) {
            compEntry->state = PluginState::Failed;
            compEntry->errorMessage = std::string(initSt.message());
            if (plugIt != m_plugins.end()) {
                plugIt->second.state = PluginState::Failed;
                plugIt->second.errorMessage = std::string(initSt.message());
            }
            spdlog::error("Composite [{}] (所属插件: {}) onInit 失败: {}",
                          compEntry->name, compEntry->ownerPluginName, initSt.message());
            if (firstError.ok()) {
                firstError = initSt;
            }
            continue;
        }
        compEntry->state = PluginState::Initialized;
        spdlog::info("Composite [{}] (插件: {}) 已按拓扑序激发初始化",
                     compEntry->name, compEntry->ownerPluginName);
    }

    // 6. 对于已预加载但当前环境无匹配 Composite（或插件本身仅挂载在 IPlugin 上）的插件，确保其完成基础生命周期
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() &&
            (it->second.state == PluginState::Preloaded || it->second.state == PluginState::Loaded)) {
            Status ownerSt = ensureOwnerPluginInitialized(it->second, engine);
            if (!ownerSt.ok() && firstError.ok()) {
                firstError = ownerSt;
            }
        }
    }

    // 7. 同步刷新 UserCodeManager 下的 Composite（使 Tag 动态切换同时作用于 UserCode Composite）
    if (engine && engine->host && engine->host->getUserCodeManager()) {
        Status ucSt = engine->host->getUserCodeManager()->refreshComposites(engine);
        if (!ucSt.ok() && firstError.ok()) {
            firstError = ucSt;
        }
    }

    return firstError;
}

std::vector<std::string> PluginManager::getActiveCompositeOrder() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (CompositeEntry* entry : m_compositeGraph.order()) {
        if (entry && entry->state == PluginState::Initialized) {
            result.push_back(entry->name);
        }
    }
    return result;
}

const CompositeEntry* PluginManager::getCompositeEntry(const std::string& compositeName) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_composites.find(compositeName);
    return it != m_composites.end() ? &it->second : nullptr;
}

Status PluginManager::loadPlugin(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadPlugin(name, engine));
    auto& entry = m_plugins[name];
    return ensureOwnerPluginLoaded(entry, engine);
}

Status PluginManager::initPlugin(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }
    CHECK_STATUS(ensureOwnerPluginInitialized(it->second, engine));
    return refreshComposites(engine);
}

Status PluginManager::loadAndInitPlugin(const std::string& name, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadPlugin(name, engine));
    return refreshComposites(engine);
}

Status PluginManager::loadAll(Engine* engine) {
    engine = ResolveEngine(engine);
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
        CHECK_STATUS(preloadPlugin(name, engine));
    }
    return OkStatus();
}

Status PluginManager::initAll(Engine* engine) {
    engine = ResolveEngine(engine);
    return refreshComposites(engine);
}

void PluginManager::logicTick(float dt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onLogicTick(dt);
        }
    }
    for (CompositeEntry* compEntry : m_compositeGraph.order()) {
        if (compEntry && compEntry->composite && compEntry->state == PluginState::Initialized) {
            compEntry->composite->onLogicTick(dt);
        }
    }
}

void PluginManager::renderTick() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.state == PluginState::Initialized) {
            it->second.plugin->onRenderTick();
        }
    }
    for (CompositeEntry* compEntry : m_compositeGraph.order()) {
        if (compEntry && compEntry->composite && compEntry->state == PluginState::Initialized) {
            compEntry->composite->onRenderTick();
        }
    }
}

void PluginManager::cleanupRegistrations(std::vector<std::string>& components,
                                         std::vector<std::string>& systems,
                                         std::vector<uint64_t>& services,
                                         Engine* engine) {
    engine = ResolveEngine(engine);
    SystemManager* sysMgr = (engine && engine->host && engine->host->getSystemManager())
        ? engine->host->getSystemManager()
        : g_engineGlobal.systemManager;
    DataManager* dataMgr = (engine && engine->scene && engine->scene->getDataManager())
        ? engine->scene->getDataManager()
        : g_engineGlobal.dataManager;
    ComponentReflection* compRefl = (engine && engine->foundation && engine->foundation->getComponentReflection())
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;

    if (sysMgr && !systems.empty()) {
        for (const auto& sysName : systems) {
            sysMgr->requestRemoveSystem(sysName);
        }
        sysMgr->flushRequests();
    }
    systems.clear();

    if (!components.empty()) {
        if (compRefl && dataMgr) {
            compRefl->purgeComponentPools(dataMgr->currentRegistries(), components);
        }
        for (const auto& compName : components) {
            ComponentRegistry::instance().unregisterComponent(compName);
            if (compRefl) {
                compRefl->unregisterComponent(compName);
            }
        }
        components.clear();
    }

    for (uint64_t serviceId : services) {
        m_services.erase(serviceId);
    }
    services.clear();
}

void PluginManager::cleanupPluginRegistrations(PluginEntry& entry, Engine* engine) {
    cleanupRegistrations(entry.registeredComponents, entry.registeredSystems, entry.registeredServices, engine);
}

void PluginManager::unloadSingleComposite(CompositeEntry& compEntry, Engine* engine) {
    if (compEntry.composite &&
        (compEntry.state == PluginState::Initialized || compEntry.state == PluginState::Loaded)) {
        compEntry.composite->onUnload();
    }
    cleanupRegistrations(
        compEntry.registeredComponents,
        compEntry.registeredSystems,
        compEntry.registeredServices,
        engine);
    compEntry.state = PluginState::Unloaded;
}

void PluginManager::unloadPluginComposites(PluginEntry& entry, Engine* engine) {
    if (entry.compositeNames.empty()) {
        return;
    }

    // 优先按当前 TokenGraph 拓扑逆序卸载该插件旗下的 Composite
    auto sortedOrder = m_compositeGraph.order();
    for (auto it = sortedOrder.rbegin(); it != sortedOrder.rend(); ++it) {
        CompositeEntry* compEntry = *it;
        if (compEntry && compEntry->ownerPluginName == entry.name) {
            unloadSingleComposite(*compEntry, engine);
            m_compositeGraph.removeNode(compEntry->name);
        }
    }

    for (const auto& compName : entry.compositeNames) {
        auto compIt = m_composites.find(compName);
        if (compIt != m_composites.end()) {
            unloadSingleComposite(compIt->second, engine);
            m_compositeGraph.removeNode(compName);
            m_composites.erase(compIt);
        }
    }
    entry.compositeNames.clear();
    (void)m_compositeGraph.update();
}

Status PluginManager::unloadPlugin(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到插件: ", name));
    }

    auto& entry = it->second;
    unloadPluginComposites(entry, engine);

    if (entry.plugin && (entry.state == PluginState::Initialized || entry.state == PluginState::Loaded)) {
        entry.plugin->onUnload();
        spdlog::info("插件 [{}] 已卸载", name);
    }

    cleanupPluginRegistrations(entry, engine);
    entry.state = PluginState::Unloaded;
    freeDynamicLibrary(entry, ResolveEngineResourceLoader(engine));

    auto orderIt = std::find(m_loadOrder.begin(), m_loadOrder.end(), name);
    if (orderIt != m_loadOrder.end()) {
        m_loadOrder.erase(orderIt);
    }

    return OkStatus();
}

void PluginManager::unloadAll() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> reverseOrder = m_loadOrder;
    std::reverse(reverseOrder.begin(), reverseOrder.end());

    for (const auto& name : reverseOrder) {
        (void)unloadPlugin(name, Engine::getInstance());
    }
    m_loadOrder.clear();
}

void PluginManager::unloadPluginsByScope(PluginScope scope) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> reverseOrder = m_loadOrder;
    std::reverse(reverseOrder.begin(), reverseOrder.end());

    for (const auto& name : reverseOrder) {
        auto it = m_plugins.find(name);
        if (it != m_plugins.end() && it->second.scope == scope) {
            (void)unloadPlugin(name, Engine::getInstance());
        }
    }
}

std::string PluginManager::createShadowCopy(const std::string& resolvedPath, ResourceLoader* resourceLoader) {
    UniquePtr<DiskResourceLoader> fallbackLoader;
    resourceLoader = ResolveLoaderOrFallback(resourceLoader, fallbackLoader);

    std::string parentDir = SAI::Path::parentPath(resolvedPath);
    std::string stem = SAI::Path::stem(resolvedPath);
    std::string ext = SAI::Path::extension(resolvedPath);
    if (ext.empty()) {
        ext = ".dll";
    }

    uint32_t pid = 0;
#ifdef _WIN32
    pid = static_cast<uint32_t>(GetCurrentProcessId());
#endif
    ++m_shadowCounter;

    std::string shadowFileName = StrCat(stem, ".__hotreload_", pid, "_", m_shadowCounter, ext);
    std::string shadowPath = SAI::Path::normalize(SAI::Path::join(parentDir, shadowFileName));
    if (resourceLoader->copyFile(resolvedPath, shadowPath, true).ok()) {
        return shadowPath;
    }
    return "";
}

std::string PluginManager::resolveDynamicLibraryPath(const std::string& inputPath, const std::string& baseDir) const {
    if (SAI::FileSystem::isFile(inputPath)) {
        return SAI::Path::normalize(inputPath);
    }

    std::string rawFileName = SAI::Path::fileName(inputPath);
    std::string stemName = SAI::Path::stem(inputPath);
    std::string dllFileName = SAI::Path::ensureExtension(rawFileName, ".dll", {".dll", ".so"});
    if (!SAI::Path::hasAnyExtension(rawFileName, {".dll", ".so"})) {
        stemName = rawFileName;
    }

    std::string pluginSubPath = SAI::Path::join("Plugins", stemName, dllFileName);
    std::string engineCorePluginSubPath = SAI::Path::join("Engine/Plugins", stemName, dllFileName);
    std::string userCodePluginSubPath = SAI::Path::join("UserCode/Plugins", stemName, dllFileName);
    std::string enginePluginSubPath = SAI::Path::join("Plugins/Engine", stemName, dllFileName);
    std::string userPluginSubPath = SAI::Path::join("Plugins/User", stemName, dllFileName);

    std::string debugBuildSubPath = SAI::Path::join("Output/build-windows-debug/Plugins", stemName, dllFileName);
    std::string debugEngineCoreBuildSubPath = SAI::Path::join("Output/build-windows-debug/Engine/Plugins", stemName, dllFileName);
    std::string debugUserCodeBuildSubPath = SAI::Path::join("Output/build-windows-debug/UserCode/Plugins", stemName, dllFileName);
    std::string debugEngineBuildSubPath = SAI::Path::join("Output/build-windows-debug/Plugins/Engine", stemName, dllFileName);
    std::string debugUserBuildSubPath = SAI::Path::join("Output/build-windows-debug/Plugins/User", stemName, dllFileName);

    std::string releaseBuildSubPath = SAI::Path::join("Output/build-windows-release/Plugins", stemName, dllFileName);
    std::string releaseEngineCoreBuildSubPath = SAI::Path::join("Output/build-windows-release/Engine/Plugins", stemName, dllFileName);
    std::string releaseUserCodeBuildSubPath = SAI::Path::join("Output/build-windows-release/UserCode/Plugins", stemName, dllFileName);
    std::string releaseEngineBuildSubPath = SAI::Path::join("Output/build-windows-release/Plugins/Engine", stemName, dllFileName);
    std::string releaseUserBuildSubPath = SAI::Path::join("Output/build-windows-release/Plugins/User", stemName, dllFileName);

    SAI::PathSearcher searcher;
    searcher.addCandidatesFromBase(baseDir, {
                inputPath, dllFileName, pluginSubPath, engineCorePluginSubPath, userCodePluginSubPath, enginePluginSubPath, userPluginSubPath
            })
            .addCandidatesFromExecutableDir({
                dllFileName, pluginSubPath, engineCorePluginSubPath, userCodePluginSubPath, enginePluginSubPath, userPluginSubPath
            })
            .addCandidatesFromExecutableAncestor(2, {
                pluginSubPath, engineCorePluginSubPath, userCodePluginSubPath, enginePluginSubPath, userPluginSubPath
            })
            .addCandidatesUpwardFromCurrentDir(6, {
                inputPath,
                dllFileName,
                pluginSubPath,
                engineCorePluginSubPath,
                userCodePluginSubPath,
                enginePluginSubPath,
                userPluginSubPath,
                debugBuildSubPath,
                debugEngineCoreBuildSubPath,
                debugUserCodeBuildSubPath,
                debugEngineBuildSubPath,
                debugUserBuildSubPath,
                releaseBuildSubPath,
                releaseEngineCoreBuildSubPath,
                releaseUserCodeBuildSubPath,
                releaseEngineBuildSubPath,
                releaseUserBuildSubPath
            });

    return searcher.resolveFirstFile(inputPath);
}

std::string PluginManager::resolveScriptPluginPath(const std::string& inputPath, const std::string& baseDir) const {
    auto isValidScriptTarget = [](const std::string& candidate) -> bool {
        if (SAI::FileSystem::isDirectory(candidate)) {
            return SAI::FileSystem::containsAny(candidate, {"plugin.mq", "plugin.json", "index.ts", "index.js", "index.py"});
        }
        if (!SAI::FileSystem::isFile(candidate)) {
            return false;
        }
        return SAI::Path::hasAnyExtension(candidate, {".ts", ".js", ".py"}) ||
               SAI::Path::fileName(candidate) == "plugin.mq" ||
               SAI::Path::fileName(candidate) == "plugin.json";
    };

    if (isValidScriptTarget(inputPath)) {
        return SAI::Path::normalize(inputPath);
    }

    std::string pluginsSubPath = SAI::Path::join("Plugins", inputPath);
    std::string engineCorePluginsSubPath = SAI::Path::join("Engine/Plugins", inputPath);
    std::string userCodePluginsSubPath = SAI::Path::join("UserCode/Plugins", inputPath);
    std::string enginePluginsSubPath = SAI::Path::join("Plugins/Engine", inputPath);
    std::string userPluginsSubPath = SAI::Path::join("Plugins/User", inputPath);
    std::string assetPluginsSubPath = SAI::Path::join("Assets/Plugins", inputPath);

    SAI::PathSearcher searcher;
    searcher.addCandidatesFromBase(baseDir, {
                inputPath, pluginsSubPath, engineCorePluginsSubPath, userCodePluginsSubPath, enginePluginsSubPath, userPluginsSubPath, assetPluginsSubPath
            })
            .addCandidatesFromExecutableDir({
                inputPath, pluginsSubPath, engineCorePluginsSubPath, userCodePluginsSubPath, enginePluginsSubPath, userPluginsSubPath
            })
            .addCandidatesUpwardFromCurrentDir(6, {
                inputPath, pluginsSubPath, engineCorePluginsSubPath, userCodePluginsSubPath, enginePluginsSubPath, userPluginsSubPath, assetPluginsSubPath
            });

    return searcher.resolveFirstMatching(isValidScriptTarget);
}

Status PluginManager::preloadDynamicPlugin(const std::string& libraryPath, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
#ifdef _WIN32
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::string resolvedPath = resolveDynamicLibraryPath(libraryPath);
    if (!loader->exists(resolvedPath)) {
        return NotFoundError(StrCat("未找到动态插件库文件: ", resolvedPath));
    }

    int64_t writeTime = loader->getLastWriteTimeSeconds(resolvedPath);
    std::string shadowPath = createShadowCopy(resolvedPath, loader);
    std::string loadTargetPath = shadowPath.empty() ? resolvedPath : shadowPath;

    HMODULE handle = LoadLibraryA(loadTargetPath.c_str());
    if (!handle) {
        DWORD err = GetLastError();
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return NotFoundError(StrCat("加载动态链接库失败: ", loadTargetPath, ", Windows错误码: ", err));
    }

    auto createFn = reinterpret_cast<CreatePluginFunc>(GetProcAddress(handle, "CreatePlugin"));
    if (!createFn) {
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return InvalidArgumentError(StrCat("动态库未导出 CreatePlugin 函数: ", resolvedPath));
    }

    IPlugin* rawPlugin = createFn();
    if (!rawPlugin) {
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return InternalError(StrCat("CreatePlugin 返回空指针: ", resolvedPath));
    }

    std::string pluginName = rawPlugin->getName();
    auto existingIt = m_plugins.find(pluginName);
    if (existingIt != m_plugins.end() &&
        (existingIt->second.state == PluginState::Preloaded ||
         existingIt->second.state == PluginState::Loaded ||
         existingIt->second.state == PluginState::Initialized)) {
        delete rawPlugin;
        FreeLibrary(handle);
        if (!shadowPath.empty()) {
            (void)loader->unlink(shadowPath);
        }
        return OkStatus();
    }

    uint32_t prevReloadCount = (existingIt != m_plugins.end()) ? existingIt->second.reloadCount : 0;
    auto pluginPtr = std::shared_ptr<IPlugin>(rawPlugin);

    PluginEntry entry;
    entry.name = pluginName;
    entry.plugin = pluginPtr;
    entry.scope = IsEngineBuiltinPluginPath(resolvedPath)
        ? PluginScope::Engine
        : pluginPtr->getScope();
    entry.state = PluginState::Unloaded;
    entry.isDynamic = true;
    entry.dynamicLibraryPath = resolvedPath;
    entry.sourcePath = resolvedPath;
    entry.shadowLibraryPath = shadowPath;
    entry.libraryHandle = handle;
    entry.lastWriteTimeSeconds = writeTime;
    entry.reloadCount = prevReloadCount;

    m_plugins[pluginName] = std::move(entry);
    return preloadPlugin(pluginName, engine);
#else
    (void)libraryPath;
    (void)engine;
    return UnimplementedError("非Windows平台的动态插件加载尚未配置");
#endif
}

Status PluginManager::preloadScriptPlugin(const std::string& scriptOrDirPath, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::string resolvedPath = resolveScriptPluginPath(scriptOrDirPath);
    if (resolvedPath.empty()) {
        resolvedPath = scriptOrDirPath;
    }

    auto manifestRes = ResolveScriptPluginManifest(resolvedPath, loader);
    CHECK_STATUS(manifestRes);

    const ScriptPluginManifestInfo& manifest = manifestRes.value();
    auto existingIt = m_plugins.find(manifest.name);
    if (existingIt != m_plugins.end() &&
        (existingIt->second.state == PluginState::Preloaded ||
         existingIt->second.state == PluginState::Loaded ||
         existingIt->second.state == PluginState::Initialized)) {
        return OkStatus();
    }

    uint32_t prevReloadCount = (existingIt != m_plugins.end()) ? existingIt->second.reloadCount : 0;
    auto scriptPlugin = std::make_shared<ScriptPluginAdapter>(manifest);
    PluginEntry entry;
    entry.name = manifest.name;
    entry.plugin = scriptPlugin;
    entry.scope = manifest.scope;
    entry.state = PluginState::Unloaded;
    entry.isScript = true;
    entry.dynamicLibraryPath = manifest.resolvedEntryPath;
    entry.sourcePath = manifest.resolvedEntryPath;
    entry.lastWriteTimeSeconds = loader->getLastWriteTimeSeconds(manifest.resolvedEntryPath);
    entry.reloadCount = prevReloadCount;

    m_plugins[manifest.name] = std::move(entry);
    return preloadPlugin(manifest.name, engine);
}

Status PluginManager::resolveAndPreloadPlugin(const std::string& pluginIdent,
                                              const std::string& baseDir,
                                              Engine* engine) {
    engine = ResolveEngine(engine);
    if (hasPlugin(pluginIdent)) {
        return preloadPlugin(pluginIdent, engine);
    }

    std::string scriptCandidate = resolveScriptPluginPath(pluginIdent, baseDir);
    if (!scriptCandidate.empty()) {
        return preloadScriptPlugin(scriptCandidate, engine);
    }

    if (SAI::Path::hasAnyExtension(pluginIdent, {".ts", ".js", ".py"}) ||
        SAI::Path::fileName(pluginIdent) == "plugin.json" ||
        SAI::Path::fileName(pluginIdent) == "plugin.mq") {
        return preloadScriptPlugin(pluginIdent, engine);
    }

    std::string dllCandidate = resolveDynamicLibraryPath(pluginIdent, baseDir);
    if (SAI::FileSystem::exists(dllCandidate) || SAI::Path::hasAnyExtension(pluginIdent, {".dll", ".so"})) {
        return preloadDynamicPlugin(dllCandidate, engine);
    }

    return NotFoundError(StrCat("无法解析或找到插件: ", pluginIdent));
}

Status PluginManager::loadDynamicPlugin(const std::string& libraryPath, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadDynamicPlugin(libraryPath, engine));
    return refreshComposites(engine);
}

Status PluginManager::loadScriptPlugin(const std::string& scriptOrDirPath, Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(preloadScriptPlugin(scriptOrDirPath, engine));
    return refreshComposites(engine);
}

Status PluginManager::resolveAndLoadPlugin(const std::string& pluginIdent,
                                           const std::string& baseDir,
                                           Engine* engine) {
    engine = ResolveEngine(engine);
    CHECK_STATUS(resolveAndPreloadPlugin(pluginIdent, baseDir, engine));
    return refreshComposites(engine);
}

Status PluginManager::reloadPlugin(const std::string& name, Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    auto it = m_plugins.find(name);
    if (it == m_plugins.end()) {
        return NotFoundError(StrCat("未找到待热重载的插件: ", name));
    }

    PluginEntry& entry = it->second;
    std::string sourcePath = entry.sourcePath.empty() ? entry.dynamicLibraryPath : entry.sourcePath;
    bool isDynamic = entry.isDynamic;
    bool isScript = entry.isScript;
    uint32_t nextReloadCount = entry.reloadCount + 1;

    std::string customState;
    if (entry.plugin && (entry.state == PluginState::Initialized || entry.state == PluginState::Loaded)) {
        customState = entry.plugin->onSerializeHotReloadState();
    }

    std::unordered_map<std::string, std::string> compositeStates;
    std::vector<std::string> allComponentsToCapture = entry.registeredComponents;
    for (const auto& compName : entry.compositeNames) {
        auto compIt = m_composites.find(compName);
        if (compIt != m_composites.end() && compIt->second.composite) {
            MergeUniqueItems(allComponentsToCapture, compIt->second.registeredComponents);
            if (compIt->second.state == PluginState::Initialized || compIt->second.state == PluginState::Loaded) {
                std::string compState = compIt->second.composite->onSerializeHotReloadState();
                if (!compState.empty()) {
                    compositeStates[compName] = std::move(compState);
                }
            }
        }
    }

    ComponentHotReloadSnapshot compSnapshot;
    ComponentReflection* compRefl = (engine && engine->foundation && engine->foundation->getComponentReflection())
        ? engine->foundation->getComponentReflection()
        : g_engineGlobal.componentReflection;
    DataManager* dataMgr = (engine && engine->scene && engine->scene->getDataManager())
        ? engine->scene->getDataManager()
        : g_engineGlobal.dataManager;

    if (compRefl && dataMgr && !allComponentsToCapture.empty()) {
        compSnapshot = compRefl->captureSnapshot(dataMgr->currentRegistries(), allComponentsToCapture);
    }

    CHECK_STATUS(unloadPlugin(name, engine));

    Status reloadStatus = OkStatus();
    if (isDynamic) {
        reloadStatus = loadDynamicPlugin(sourcePath, engine);
    } else if (isScript) {
        reloadStatus = loadScriptPlugin(sourcePath, engine);
    } else {
        reloadStatus = loadAndInitPlugin(name, engine);
    }

    if (!reloadStatus.ok()) {
        spdlog::error("插件 [{}] 热重载失败: {}", name, reloadStatus.message());
        return reloadStatus;
    }

    auto reloadedIt = m_plugins.find(name);
    if (reloadedIt != m_plugins.end()) {
        reloadedIt->second.reloadCount = nextReloadCount;
        if (compRefl && !compSnapshot.empty()) {
            compRefl->restoreSnapshot(compSnapshot);
        }
        if (reloadedIt->second.plugin && !customState.empty()) {
            reloadedIt->second.plugin->onRestoreHotReloadState(customState);
        }
        for (const auto& [compName, savedState] : compositeStates) {
            auto compIt = m_composites.find(compName);
            if (compIt != m_composites.end() && compIt->second.composite) {
                compIt->second.composite->onRestoreHotReloadState(savedState);
            }
        }
    }

    spdlog::info("插件 [{}] 热重载完成 (第 {} 次重载)", name, nextReloadCount);
    return OkStatus();
}

int PluginManager::checkAndReloadModified(Engine* engine) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    engine = ResolveEngine(engine);
    UniquePtr<DiskResourceLoader> fallbackLoader;
    ResourceLoader* loader = ResolveLoaderOrFallback(ResolveEngineResourceLoader(engine), fallbackLoader);

    std::vector<std::string> modifiedPlugins;
    for (const auto& name : m_loadOrder) {
        auto it = m_plugins.find(name);
        if (it == m_plugins.end()) {
            continue;
        }
        const PluginEntry& entry = it->second;
        if ((!entry.isDynamic && !entry.isScript) || entry.state != PluginState::Initialized) {
            continue;
        }
        std::string watchPath = entry.sourcePath.empty() ? entry.dynamicLibraryPath : entry.sourcePath;
        if (watchPath.empty() || !loader->exists(watchPath)) {
            continue;
        }
        int64_t currentWriteTime = loader->getLastWriteTimeSeconds(watchPath);
        if (currentWriteTime > 0 && entry.lastWriteTimeSeconds > 0 &&
            currentWriteTime > entry.lastWriteTimeSeconds) {
            modifiedPlugins.push_back(name);
        }
    }

    int reloadedCount = 0;
    for (const auto& name : modifiedPlugins) {
        if (reloadPlugin(name, engine).ok()) {
            ++reloadedCount;
        }
    }
    return reloadedCount;
}

void PluginManager::freeDynamicLibrary(PluginEntry& entry, ResourceLoader* resourceLoader) {
    entry.plugin.reset();
#ifdef _WIN32
    const bool isShuttingDown = Runtime::instance().isShuttingDown();
    if (!isShuttingDown && entry.isDynamic && entry.libraryHandle) {
        FreeLibrary(static_cast<HMODULE>(entry.libraryHandle));
        entry.libraryHandle = nullptr;
    } else if (isShuttingDown) {
        entry.libraryHandle = nullptr;
    }
    if (!entry.shadowLibraryPath.empty()) {
        if (!isShuttingDown) {
            UniquePtr<DiskResourceLoader> fallbackLoader;
            ResourceLoader* loader = ResolveLoaderOrFallback(resourceLoader, fallbackLoader);
            (void)loader->unlink(entry.shadowLibraryPath);
        }
        entry.shadowLibraryPath.clear();
    }
#else
    (void)resourceLoader;
#endif
}

bool PluginManager::hasPlugin(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_plugins.find(name) != m_plugins.end() || m_factories.find(name) != m_factories.end();
}

std::shared_ptr<IPlugin> PluginManager::getPlugin(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.plugin;
    }
    return nullptr;
}

PluginState PluginManager::getPluginState(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return it->second.state;
    }
    return PluginState::Unloaded;
}

std::vector<std::string> PluginManager::getAllPluginNames() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

std::vector<std::string> PluginManager::getPluginNamesByScope(PluginScope scope) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> names;
    for (const auto& pair : m_plugins) {
        if (pair.second.scope == scope) {
            names.push_back(pair.first);
        }
    }
    return names;
}

std::vector<std::string> PluginManager::getPluginsInState(PluginState state) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> names;
    for (const auto& pair : m_plugins) {
        if (pair.second.state == state) {
            names.push_back(pair.first);
        }
    }
    return names;
}

const PluginEntry* PluginManager::getPluginEntry(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_plugins.find(name);
    if (it != m_plugins.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace Runtime
} // namespace MQEngine
